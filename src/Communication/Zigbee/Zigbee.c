#if !defined(RACOOL_TARGET_TESTING) || defined(RACOOL_INTERNAL_TEST_ACCESS)
#include "Communication/Zigbee/IZigbee.h"
#include "Communication/Zigbee/Endpoints/FanSourceAttributes.h"
#include "Communication/Zigbee/ZigbeeConfig.h"
#include "Communication/Zigbee/ZigbeeConsoleCommands.h"
#include "Communication/Zigbee/Endpoints/EndpointCommon.h"
#include "Communication/Zigbee/Handlers/SignalHandlerCore.h"
#include "Communication/Zigbee/Endpoints/EndpointFan.h"
#include "Communication/Zigbee/Endpoints/EndpointAmbientTemp.h"
#include "Communication/Zigbee/Endpoints/EndpointCPUTemp.h"
#include "Communication/Zigbee/Endpoints/EndpointInternalLED.h"
#include "Communication/Zigbee/Endpoints/EndpointRGBLED.h"
#include "Communication/Zigbee/Endpoints/EndpointHealth.h"
#include "Communication/Zigbee/Endpoints/EndpointMemory.h"
#include "Communication/Zigbee/Endpoints/EndpointUptime.h"
#include "Communication/Zigbee/Endpoints/EndpointGroups.h"
#include "Communication/Zigbee/Endpoints/EndpointOTA.h"

#include "Core/DI/IDIRegistry.h"
#include "Core/EventBus/IEventBus.h"
#include "Core/IService.h"
#include "Core/ServiceLocator/ServiceLocator.h"
#include "Management/ConfigurationManager/SystemConfigs.h"
#include "Management/ConfigurationManager/IConfigurationManager.h"

#include "esp_log.h"
#include "esp_zigbee.h"
#include "Communication/Zigbee/ZigbeeRadioBoundary.h"
#include <ezbee/platform/datasets.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/timers.h"
#include "nvs_flash.h"
#include "esp_pm.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include <stdatomic.h>
#include <string.h>
#include <limits.h>
#include "Communication/Zigbee/ZigbeeAttributePolicy.h"

static const char *TAG = "Zigbee";
/* ConfigurationManager owns report intervals. Rebuild only the SDK's derived
 * reporting cache before endpoint registration. The SDK may restore reporting
 * entries while rewriting their dataset; stale duplicate schedulers otherwise
 * survive updates to the first matching handle. Network datasets are untouched. */
static esp_err_t ResetDerivedReportingCache(void) {
  ezb_err_t result = ezb_plat_datasets_delete(EZB_DATASETS_KEY_ZCL_REPORTING_INFO, -1);
  if (result == EZB_ERR_NONE || result == EZB_ERR_NOT_FOUND) return ESP_OK;
  ESP_LOGE(TAG, "Reporting cache reset failed: %d", result);
  return ESP_FAIL;
}
_Static_assert(ZIGBEE_ATTRIBUTE_MAX_ATTEMPTS >= 1u && ZIGBEE_ATTRIBUTE_MAX_ATTEMPTS <= UINT8_MAX,
    "Attribute attempt limit must fit the per-revision counter");
_Static_assert(ZIGBEE_REPORT_MAX_ATTEMPTS >= 1u && ZIGBEE_REPORT_MAX_ATTEMPTS <= UINT8_MAX,
    "Reporting attempt limit must fit the per-revision counter");

typedef struct {
  uint8_t endpoint;
  uint8_t length;
  uint8_t attempts;
  bool reportPending;
  uint32_t failedMs;
  uint16_t cluster;
  uint16_t attribute;
  uint64_t revision;
  union { uint64_t alignment; uint8_t bytes[ZIGBEE_ATTRIBUTE_MAX_BYTES]; } value;
} AttributeUpdate;
_Static_assert(ZIGBEE_ATTRIBUTE_REPORT_BATCH_SIZE > 0u &&
    ZIGBEE_ATTRIBUTE_REPORT_BATCH_SIZE <= ZIGBEE_ATTRIBUTE_BATCH_SIZE, "Invalid report batch budget");
_Static_assert(ZIGBEE_ATTRIBUTE_RETRY_BASE_MS > 0u &&
    ZIGBEE_ATTRIBUTE_RETRY_MAX_MS >= ZIGBEE_ATTRIBUTE_RETRY_BASE_MS &&
    ZIGBEE_ATTRIBUTE_RETRY_MAX_MS < UINT32_MAX / 2u, "Invalid attribute retry time bounds");

static uint32_t AttributeRetryRemainingMs(const AttributeUpdate *update, uint32_t nowMs) {
  if (!update->attempts) return 0;
  uint32_t delay = ZIGBEE_ATTRIBUTE_RETRY_BASE_MS;
  for (unsigned i = 1; i < update->attempts && delay < ZIGBEE_ATTRIBUTE_RETRY_MAX_MS; ++i)
    delay = delay > ZIGBEE_ATTRIBUTE_RETRY_MAX_MS / 2u ? ZIGBEE_ATTRIBUTE_RETRY_MAX_MS : delay * 2u;
  uint32_t elapsed = nowMs - update->failedMs;
  return elapsed < delay ? delay - elapsed : 0;
}

static portMUX_TYPE attributeMux = portMUX_INITIALIZER_UNLOCKED;
static portMUX_TYPE factoryResetMux = portMUX_INITIALIZER_UNLOCKED;
static ZigbeeFactoryResetStatus factoryResetStatus;

static bool FactoryResetPending(void) {
  portENTER_CRITICAL(&factoryResetMux);
  bool pending = factoryResetStatus.pending;
  portEXIT_CRITICAL(&factoryResetMux);
  return pending;
}

static bool FactoryResetMutationBlocked(void) {
  IConfigurationManager *config = ConfigurationManager_GetInstance();
  return config && config->IsMutationAllowed && !config->IsMutationAllowed(config);
}

typedef struct {
  IZigbee base;
  IZigbeeEndpoint *endpointList;
  IZigbeeSignalHandler *signalHandlerList;
  uint8_t endpointCount;
  _Atomic(ServiceState) currentState;
  atomic_bool isConnected;
  atomic_bool isPairing;
  uint32_t pairingStartTimeSec;
  uint32_t startTimeSec;
  SemaphoreHandle_t mutex;
  SemaphoreHandle_t lifecycleMutex;
  esp_timer_handle_t reportingTimer;
  /* Queue/timer control resources live for the service lifetime, across Stop.
   * The timer never borrows the reporting task handle or SDK ownership. */
  QueueHandle_t reportingQueue;
  _Atomic(TaskHandle_t) reportingWorkerTask;
  atomic_bool reportingWorkerStop;
  atomic_bool reportingWorkerExited;
  atomic_bool reportingScheduleFailed;
  atomic_bool reportingPending;
  atomic_bool connectionNotificationPending;
  atomic_bool reportingApplyFailed;
  atomic_uint reportingRevision;
  unsigned reportingAttemptRevision;
  uint8_t reportingAttempts;
  atomic_bool pluginsReady;
  atomic_bool closing;
  atomic_uint sdkReaders;
  _Atomic(TaskHandle_t) sdkAccessOwner;
  _Atomic(TaskHandle_t) workerTask;
  atomic_bool startupComplete;
  atomic_int startupResult;
  atomic_bool mainloopExited;
  /* Protected by lifecycleMutex; callback results are published atomically. */
  bool stopPosted;
  atomic_bool stopCallbackComplete;
  atomic_int stopCallbackResult;
  atomic_bool cleanupAllowed;
  atomic_bool workerExited;
  atomic_int cleanupResult;
  bool sdkInitialized;
  ezb_af_device_desc_t device;
  AttributeUpdate attributes[ZIGBEE_ATTRIBUTE_QUEUE_CAPACITY];
  uint16_t attributeHead;
  uint16_t attributeCount;
  uint64_t attributeRevision;
  uint32_t attributeApplied;
  uint32_t attributeFailed;
  uint32_t attributeLockDeferrals;
  uint32_t attributeRetries;
  uint32_t attributeDropped;
  uint32_t sourceReportMs[13];
  uint16_t sourceReportMask;
} ZigbeeService;

static ZigbeeService zigbeeService = {0};

static inline bool zb_take_mutex(TickType_t timeout) {
  return zigbeeService.mutex && (xSemaphoreTake(zigbeeService.mutex, timeout) == pdTRUE);
}

static inline void zb_give_mutex(void) {
  if (zigbeeService.mutex) {
    xSemaphoreGive(zigbeeService.mutex);
  }
}

/* Reuse static storage only after the old worker acknowledges outside SDK ownership. */
static StaticTask_t zb_task_buffer;
static StackType_t zb_stack[ZIGBEE_TASK_STACK_SIZE] __attribute__((aligned(16)));

// Forward declaration of external plugins
extern int32_t ClimateZigbeePlugin_Init(void);
extern int32_t SystemZigbeePlugin_Init(void);

static bool ScheduleReporting(void);
static void ProcessReportingWork(void);

static bool IsFanRelayAttribute(uint8_t endpoint, uint16_t cluster, uint16_t attribute) {
  return endpoint == ZIGBEE_EP_SENSOR_FAN && cluster == EZB_ZCL_CLUSTER_ID_ON_OFF &&
      attribute == EZB_ZCL_ATTR_ON_OFF_ON_OFF_ID;
}

static bool TxProfilePower(ZigbeeTxProfile profile, int8_t *dbm) {
  if (!dbm) return false;
  switch (profile) {
    case ZIGBEE_TX_ECO: *dbm = ZIGBEE_TX_ECO_DBM; return true;
    case ZIGBEE_TX_NORMAL: *dbm = ZIGBEE_TX_NORMAL_DBM; return true;
    case ZIGBEE_TX_BOOST: *dbm = ZIGBEE_TX_BOOST_DBM; return true;
    default: return false;
  }
}

/* SDK setters are void: accept only an exact configured-power readback. */
static esp_err_t ApplyTxPower(int8_t desired) {
  int8_t observed = INT8_MIN;
  ezb_set_tx_power(desired);
  ezb_get_tx_power(&observed);
  return observed == desired ? ESP_OK : ESP_FAIL;
}

static bool Zigbee_TrySdkAccess(IZigbee *self, uint32_t timeout_ms) {
  if (self != &zigbeeService.base || timeout_ms > ZIGBEE_LOCK_TIMEOUT_MS ||
      atomic_load(&zigbeeService.closing) || zigbeeService.currentState != SERVICE_STATE_STARTED) return false;
  TaskHandle_t caller = xTaskGetCurrentTaskHandle();
  if (!caller || atomic_load(&zigbeeService.sdkAccessOwner) == caller) return false;
  unsigned readers = atomic_load(&zigbeeService.sdkReaders);
  do {
    if (readers == UINT_MAX) return false;
  } while (!atomic_compare_exchange_weak(&zigbeeService.sdkReaders, &readers, readers + 1));
  if (atomic_load(&zigbeeService.closing) || zigbeeService.currentState != SERVICE_STATE_STARTED) {
    atomic_fetch_sub(&zigbeeService.sdkReaders, 1);
    return false;
  }
  TickType_t timeout = pdMS_TO_TICKS(timeout_ms);
  if (timeout_ms && !timeout) timeout = 1;
  if (!esp_zigbee_lock_acquire(timeout)) {
    atomic_fetch_sub(&zigbeeService.sdkReaders, 1);
    return false;
  }
  if (atomic_load(&zigbeeService.closing)) {
    esp_zigbee_lock_release();
    atomic_fetch_sub(&zigbeeService.sdkReaders, 1);
    return false;
  }
  atomic_store(&zigbeeService.sdkAccessOwner, caller);
  return true;
}

static int32_t Zigbee_ReleaseSdkAccess(IZigbee *self) {
  TaskHandle_t caller = xTaskGetCurrentTaskHandle();
  if (self != &zigbeeService.base || !caller || atomic_load(&zigbeeService.sdkAccessOwner) != caller)
    return ZIGBEE_ERROR_INVALID_PARAM;
  atomic_store(&zigbeeService.sdkAccessOwner, NULL);
  esp_zigbee_lock_release();
  atomic_fetch_sub(&zigbeeService.sdkReaders, 1);
  return ZIGBEE_ERROR_OK;
}


static bool AttributesPending(void) {
  portENTER_CRITICAL(&attributeMux);
  bool pending = zigbeeService.attributeCount != 0;
  portEXIT_CRITICAL(&attributeMux);
  return pending;
}

/* Caller owns the SDK lock. This API accepts fixed-width numeric attributes only. */
static ezb_zcl_status_t SetNumericAttribute(uint8_t endpoint, uint16_t cluster, uint16_t attribute,
    void *value, uint8_t length, bool retryReport, bool *reportRequested) {
  if (reportRequested) *reportRequested = false;
  ezb_zcl_attr_desc_t descriptor = ezb_zcl_get_attr_desc(endpoint, cluster,
      EZB_ZCL_CLUSTER_SERVER, attribute, EZB_ZCL_STD_MANUF_CODE);
  if (!descriptor) return EZB_ZCL_STATUS_UNSUP_ATTRIB;
  ezb_zcl_attr_type_t type = ezb_zcl_attr_desc_get_type(descriptor);
  unsigned width = 0;
  if (type >= EZB_ZCL_ATTR_TYPE_UINT8 && type <= EZB_ZCL_ATTR_TYPE_UINT64)
    width = (unsigned)(type - EZB_ZCL_ATTR_TYPE_UINT8) + 1u;
  else if (type >= EZB_ZCL_ATTR_TYPE_INT8 && type <= EZB_ZCL_ATTR_TYPE_INT64)
    width = (unsigned)(type - EZB_ZCL_ATTR_TYPE_INT8) + 1u;
  else switch (type) {
    case EZB_ZCL_ATTR_TYPE_BOOL: case EZB_ZCL_ATTR_TYPE_ENUM8: width = 1; break;
    case EZB_ZCL_ATTR_TYPE_ENUM16: case EZB_ZCL_ATTR_TYPE_SEMI: width = 2; break;
    case EZB_ZCL_ATTR_TYPE_SINGLE: width = 4; break;
    case EZB_ZCL_ATTR_TYPE_DOUBLE: width = 8; break;
    default: break;
  }
  if (!width || width != length) return EZB_ZCL_STATUS_INVALID_TYPE;
  bool sourceDiagnostic = endpoint == ZIGBEE_EP_SENSOR_FAN &&
      cluster == EZB_ZCL_CLUSTER_ID_THERMOSTAT_UI_CONFIG &&
      attribute >= FAN_ATTR_EXTERNAL_FIRST && attribute <= FAN_ATTR_EXTERNAL_FIRST + 12;
  bool valueChanged = true;
  bool configurationChanged = false;
  if (ZigbeeAttribute_IsConfiguration(endpoint, cluster, attribute) || sourceDiagnostic) {
    uint8_t previous[ZIGBEE_ATTRIBUTE_MAX_BYTES] = {0};
    if (length > sizeof(previous)) return EZB_ZCL_STATUS_INVALID_TYPE;
    valueChanged = ezb_zcl_attr_desc_get_value(descriptor, previous) != EZB_ERR_NONE ||
        memcmp(previous, value, length) != 0;
    configurationChanged = ZigbeeAttribute_IsConfiguration(endpoint, cluster, attribute) && (retryReport || valueChanged);
  }
  ezb_zcl_status_t result = ezb_zcl_set_attr_value(endpoint, cluster, EZB_ZCL_CLUSTER_SERVER,
      attribute, EZB_ZCL_STD_MANUF_CODE, value, false);

  unsigned sourceIndex = sourceDiagnostic ? attribute - FAN_ATTR_EXTERNAL_FIRST : 0;
  uint32_t nowMs = (uint32_t)(esp_timer_get_time() / 1000ULL);
  bool sourceDue = sourceDiagnostic && (!(zigbeeService.sourceReportMask & (1u << sourceIndex)) ||
      (uint32_t)(nowMs - zigbeeService.sourceReportMs[sourceIndex]) >= FAN_SOURCE_DIAGNOSTIC_REFRESH_MS);
  /* Unchanged diagnostics need no new report. Retain an admitted failure's retry,
   * since a successful local setter does not establish remote delivery. */
  bool sourceReport = sourceDue && (valueChanged || retryReport);
  if (result == EZB_ZCL_STATUS_SUCCESS && sourceDue && !sourceReport) {
    zigbeeService.sourceReportMs[sourceIndex] = nowMs;
    zigbeeService.sourceReportMask |= (uint16_t)(1u << sourceIndex);
  }
  if (result == EZB_ZCL_STATUS_SUCCESS && (IsFanRelayAttribute(endpoint, cluster, attribute) || sourceReport || configurationChanged)) {
    /* Binding owns destinations. Report each queued edge without a periodic interval. */
    ezb_zcl_report_attr_cmd_t report = {
      .cmd_ctrl = {.src_ep = endpoint, .cluster_id = cluster,
        .manuf_code = EZB_ZCL_STD_MANUF_CODE, .fc = {.direction = 1, .dis_default_rsp = 1}},
      .payload = {.attr_id = attribute}
    };
    if (reportRequested) *reportRequested = true;
    ezb_err_t reportError = ezb_zcl_report_attr_cmd_req(&report);
    if (reportError != EZB_ERR_NONE) {
      ESP_LOGE(TAG, "Attribute report request rejected: endpoint=%u cluster=0x%04x attribute=0x%04x sdk_error=%d changed=%u retry=%u",
          (unsigned)endpoint, (unsigned)cluster, (unsigned)attribute, (int)reportError, valueChanged ? 1u : 0u, retryReport ? 1u : 0u);
      if (reportError == EZB_ERR_NO_MEM) result = EZB_ZCL_STATUS_INSUFFICIENT_SPACE;
      else if (reportError == EZB_ERR_TIMEOUT) result = EZB_ZCL_STATUS_TIMEOUT;
      else if (reportError == EZB_ERR_NOT_FOUND || reportError == EZB_ERR_NOT_SUPPORTED)
        result = EZB_ZCL_STATUS_UNREPORTBLE_ATTRIB;
      else if (reportError == EZB_ERR_INV_ARG || reportError == EZB_ERR_INV_SIZE)
        result = EZB_ZCL_STATUS_INVALID_VALUE;
      else result = EZB_ZCL_STATUS_FAIL;
    }
    else if (sourceDue) {
      zigbeeService.sourceReportMs[sourceIndex] = nowMs;
      zigbeeService.sourceReportMask |= (uint16_t)(1u << sourceIndex);
    }
  }

  return result;
}

/* SDK lock is owned by the reporting worker. Producers never borrow storage. */
static void ApplyQueuedAttributes(void) {
  unsigned reports = 0;
  for (unsigned n = 0; n < ZIGBEE_ATTRIBUTE_BATCH_SIZE; ++n) {
    AttributeUpdate update;
    portENTER_CRITICAL(&attributeMux);
    bool available = zigbeeService.attributeCount != 0;
    if (available) update = zigbeeService.attributes[zigbeeService.attributeHead];
    portEXIT_CRITICAL(&attributeMux);
    if (!available) break;
    uint32_t nowMs = (uint32_t)(esp_timer_get_time() / 1000ULL);
    if (AttributeRetryRemainingMs(&update, nowMs)) break;
    bool reportRequested = false;
    ezb_zcl_status_t status = SetNumericAttribute(update.endpoint, update.cluster,
        update.attribute, update.value.bytes, update.length, update.attempts != 0, &reportRequested);
    if (reportRequested) ++reports;
    bool retry = false;
    uint32_t failedMs = (uint32_t)(esp_timer_get_time() / 1000ULL);
    portENTER_CRITICAL(&attributeMux);
    if (status == EZB_ZCL_STATUS_SUCCESS) ++zigbeeService.attributeApplied;
    else ++zigbeeService.attributeFailed;
    /* A producer may have replaced this key while the SDK was applying its copy. */
    bool transient = status == EZB_ZCL_STATUS_FAIL || status == EZB_ZCL_STATUS_TIMEOUT ||
        status == EZB_ZCL_STATUS_INSUFFICIENT_SPACE;
    if (zigbeeService.attributes[zigbeeService.attributeHead].revision == update.revision) {
      /* A local write followed by rejected report admission still owns delivery.
       * Pool backpressure must not turn an unchanged state into a dropped event.
       * Work stays bounded to one attempt here; release the SDK and back off. */
      retry = transient && (update.reportPending || reportRequested ||
          (unsigned)update.attempts + 1u < ZIGBEE_ATTRIBUTE_MAX_ATTEMPTS);
      if (retry) {
        if (zigbeeService.attributes[zigbeeService.attributeHead].attempts < UINT8_MAX)
          ++zigbeeService.attributes[zigbeeService.attributeHead].attempts;
        zigbeeService.attributes[zigbeeService.attributeHead].reportPending = update.reportPending || reportRequested;
        zigbeeService.attributes[zigbeeService.attributeHead].failedMs = failedMs;
        ++zigbeeService.attributeRetries;
      } else {
        if (status != EZB_ZCL_STATUS_SUCCESS) ++zigbeeService.attributeDropped;
        zigbeeService.attributeHead = (zigbeeService.attributeHead + 1u) % ZIGBEE_ATTRIBUTE_QUEUE_CAPACITY;
        --zigbeeService.attributeCount;
      }
    } else if (transient && (update.reportPending || reportRequested)) {
      /* The producer replaced this slot while the SDK call was in flight.
       * Its newest value still owns the rejected report and its backoff. */
      AttributeUpdate *current = &zigbeeService.attributes[zigbeeService.attributeHead];
      current->reportPending = true;
      if (current->attempts < UINT8_MAX) ++current->attempts;
      current->failedMs = failedMs;
      ++zigbeeService.attributeRetries;
      retry = true;
    }
    portEXIT_CRITICAL(&attributeMux);
    if (status != EZB_ZCL_STATUS_SUCCESS)
      ESP_LOGE(TAG, "Queued attribute failed: endpoint=%u cluster=0x%04x attribute=0x%04x status=%u attempt=%u retry=%u",
          (unsigned)update.endpoint, (unsigned)update.cluster, (unsigned)update.attribute, (unsigned)status,
          update.attempts < UINT8_MAX ? (unsigned)update.attempts + 1u : (unsigned)UINT8_MAX, (unsigned)retry);
    /* Retry after elapsed backoff; never sleep or exhaust attempts in one callback. */
    if (retry || reports >= ZIGBEE_ATTRIBUTE_REPORT_BATCH_SIZE) break;
  }
}

static void SetConnected(bool connected) {
  if (connected && (zigbeeService.currentState != SERVICE_STATE_STARTED || atomic_load(&zigbeeService.closing))) return;
  bool prev = atomic_exchange(&zigbeeService.isConnected, connected);
  if (!connected) atomic_store(&zigbeeService.connectionNotificationPending, false);
  if (connected && (atomic_load(&zigbeeService.reportingPending) || AttributesPending())) ScheduleReporting();
  if (prev != connected) {
    IEventBus *bus = GetEventBus();
    if (bus && bus->Publish) {
      BusEvent evt = {
        .eventName = connected ? "ZIGBEE_CONNECTED" : "ZIGBEE_DISCONNECTED",
        .type = connected ? EVENT_TYPE_INFO : EVENT_TYPE_WARNING,
        .data = NULL,
        .dataSize = 0
      };
      bus->Publish(evt);
    }
  }
}

static bool GetConnected(void) {
  return zigbeeService.currentState == SERVICE_STATE_STARTED && !atomic_load(&zigbeeService.closing) &&
      atomic_load(&zigbeeService.isConnected);
}

static void NotifyEndpointsConnected(IZigbee *self) {
  (void)self;
  if (!GetConnected()) return;
  /* The SDK signal callback only admits work. The existing timer retains it
   * across SDK/service lock contention; the worker owns endpoint execution. */
  atomic_store(&zigbeeService.connectionNotificationPending, true);
  atomic_fetch_add(&zigbeeService.reportingRevision, 1);
  atomic_store(&zigbeeService.reportingPending, true);
  ScheduleReporting();
}

/* ESP timer scheduling does not depend on the FreeRTOS timer command queue.
 * A concurrent request may find the one-shot already armed; that is success. */
static bool ScheduleReporting(void) {
  if (zigbeeService.currentState != SERVICE_STATE_STARTED || atomic_load(&zigbeeService.closing)) return false;
  uint32_t delayMs = ZIGBEE_REPORT_APPLY_DELAY_MS;
  if (!atomic_load(&zigbeeService.reportingPending) && !atomic_load(&zigbeeService.connectionNotificationPending)) {
    uint32_t nowMs = (uint32_t)(esp_timer_get_time() / 1000ULL);
    portENTER_CRITICAL(&attributeMux);
    if (zigbeeService.attributeCount) {
      uint32_t remaining = AttributeRetryRemainingMs(&zigbeeService.attributes[zigbeeService.attributeHead], nowMs);
      if (remaining > delayMs) delayMs = remaining;
    }
    portEXIT_CRITICAL(&attributeMux);
  }
  esp_err_t result = zigbeeService.reportingTimer ?
      esp_timer_start_once(zigbeeService.reportingTimer, (uint64_t)delayMs * 1000U) : ESP_ERR_INVALID_ARG;
  bool scheduled = result == ESP_OK || result == ESP_ERR_INVALID_STATE;
  atomic_store(&zigbeeService.reportingScheduleFailed, !scheduled);
  if (!scheduled) ESP_LOGE(TAG, "Reporting timer scheduling failed: %s", esp_err_to_name(result));
  return scheduled;
}

/* ESP timer work is admission only. Queue storage remains valid across Stop,
 * so an already-entered callback cannot notify a deleted task handle. */
static void ReportingTimerCallback(void *arg) {
  (void)arg;
  if (atomic_load(&zigbeeService.closing) || zigbeeService.currentState!=SERVICE_STATE_STARTED ||
      !zigbeeService.reportingQueue) return;
#if defined(RACOOL_ZIGBEE_REPORT_LEGACY_CONTEXT_DIAGNOSTIC)
  /* Diagnostic negative control only: reproduce the former execution context.
   * Production never performs SDK work in this shared timer callback. */
  ProcessReportingWork();
#else
  uint8_t token=1;
  /* A full one-slot queue already owns the wakeup; durable flags hold the work. */
  (void)xQueueSend(zigbeeService.reportingQueue,&token,0);
#endif
}

static void ProcessReportingWork(void) {
#if defined(RACOOL_LED_PHASE_DIAGNOSTIC)
  int64_t profileStart=esp_timer_get_time();
#endif
  if ((!atomic_load(&zigbeeService.reportingPending) &&
       !atomic_load(&zigbeeService.connectionNotificationPending) && !AttributesPending()) ||
      zigbeeService.currentState != SERVICE_STATE_STARTED || atomic_load(&zigbeeService.closing) || !GetConnected()) return;
  if (!Zigbee_TrySdkAccess(&zigbeeService.base, 0)) {
    portENTER_CRITICAL(&attributeMux);
    ++zigbeeService.attributeLockDeferrals;
    portEXIT_CRITICAL(&attributeMux);
    ScheduleReporting(); return;
  }
  if (!zb_take_mutex(0)) {
    Zigbee_ReleaseSdkAccess(&zigbeeService.base);
    ScheduleReporting();
    return;
  }
  if (GetConnected() && atomic_exchange(&zigbeeService.connectionNotificationPending, false)) {
    bool failed = false;
    for (IZigbeeEndpoint *ep = zigbeeService.endpointList; ep; ep = ep->next)
      if (ep->OnConnected && ep->OnConnected(ep) != ESP_OK) failed = true;
    if (failed && GetConnected()) atomic_store(&zigbeeService.connectionNotificationPending, true);
  }
  if (zigbeeService.currentState == SERVICE_STATE_STARTED && !atomic_load(&zigbeeService.closing) && GetConnected() &&
      atomic_exchange(&zigbeeService.reportingPending, false)) {
    atomic_store(&zigbeeService.reportingScheduleFailed, false);
    unsigned revision = atomic_load(&zigbeeService.reportingRevision);
    if (revision != zigbeeService.reportingAttemptRevision) {
      zigbeeService.reportingAttemptRevision = revision;
      zigbeeService.reportingAttempts = 0;
    }
    ++zigbeeService.reportingAttempts;
    bool failed = false, retry = false;
    for (IZigbeeEndpoint *ep = zigbeeService.endpointList; ep; ep = ep->next)
      if (ep->ConfigureReporting) {
        esp_err_t result = ep->ConfigureReporting(ep);
        if (result != ESP_OK) {
          failed = true;
          if (result != ESP_ERR_NOT_FOUND && result != ESP_ERR_INVALID_ARG) retry = true;
        }
      }
    atomic_store(&zigbeeService.reportingApplyFailed, failed);
    if (retry && zigbeeService.reportingAttempts < ZIGBEE_REPORT_MAX_ATTEMPTS)
      atomic_store(&zigbeeService.reportingPending, true);
    if (failed) ESP_LOGE(TAG, "Reporting apply failed: attempt=%u retry=%u",
        zigbeeService.reportingAttempts, retry && zigbeeService.reportingAttempts < ZIGBEE_REPORT_MAX_ATTEMPTS);
  }
  zb_give_mutex();
  if (zigbeeService.currentState == SERVICE_STATE_STARTED && !atomic_load(&zigbeeService.closing) && GetConnected())
    ApplyQueuedAttributes();
  Zigbee_ReleaseSdkAccess(&zigbeeService.base);
  if (AttributesPending() || atomic_load(&zigbeeService.reportingPending) ||
      atomic_load(&zigbeeService.connectionNotificationPending)) ScheduleReporting();
#if defined(RACOOL_LED_PHASE_DIAGNOSTIC)
  int64_t profileEnd=esp_timer_get_time();
  ESP_LOGI("LEDWORK", "reporting start=%llu end=%llu duration=%llu pending=%u",
      (unsigned long long)profileStart,(unsigned long long)profileEnd,
      (unsigned long long)(profileEnd-profileStart),(unsigned)AttributesPending());
#endif
}

/* Dedicated bounded peripheral work must not run in the shared high-priority
 * esp_timer task. Every checkpoint is outside SDK and service ownership. */
static void ReportingWorker(void *arg) {
  (void)arg;
  TickType_t wait=pdMS_TO_TICKS(ZIGBEE_REPORT_WORKER_POLL_MS);
  if(!wait) wait=1;
  for(;;) {
    if(atomic_load(&zigbeeService.reportingWorkerStop)) {
      atomic_store(&zigbeeService.reportingWorkerExited,true);
      for(;;) vTaskSuspend(NULL);
    }
    uint8_t token;
    if(xQueueReceive(zigbeeService.reportingQueue,&token,wait)==pdTRUE &&
        !atomic_load(&zigbeeService.closing)) ProcessReportingWork();
  }
}

static int32_t StopReportingWorker(TickType_t started,TickType_t budget) {
  TaskHandle_t task=zigbeeService.reportingWorkerTask;
  if(!task) return ZIGBEE_ERROR_OK;
  if(task==xTaskGetCurrentTaskHandle()) return ZIGBEE_ERROR_LOCK_FAIL;
  atomic_store(&zigbeeService.reportingWorkerStop,true);
  uint8_t token=1;
  (void)xQueueSend(zigbeeService.reportingQueue,&token,0);
  while(!atomic_load(&zigbeeService.reportingWorkerExited) || eTaskGetState(task)!=eSuspended) {
    if((TickType_t)(xTaskGetTickCount()-started)>=budget) return ZIGBEE_ERROR_TIMEOUT;
    vTaskDelay(1);
  }
  /* The worker is parked at a lock-free boundary; timeouts retain ownership. */
  vTaskDelete(task);
  zigbeeService.reportingWorkerTask=NULL;
  return ZIGBEE_ERROR_OK;
}

static int32_t Zigbee_ReconfigureAllReporting(IZigbee *self) {
  if (!self) return ZIGBEE_ERROR_INVALID_PARAM;
  atomic_fetch_add(&zigbeeService.reportingRevision, 1);
  atomic_store(&zigbeeService.reportingPending, true);
  if (zigbeeService.currentState != SERVICE_STATE_STARTED) return ZIGBEE_ERROR_STACK_FAIL;
  return ScheduleReporting() ? ZIGBEE_ERROR_OK : ZIGBEE_ERROR_STACK_FAIL;
}

static void NotifyEndpointsDisconnected(void) {
  if (zb_take_mutex(pdMS_TO_TICKS(100))) {
    IZigbeeEndpoint *curr = zigbeeService.endpointList;
    while (curr) {
      if (curr->OnDisconnected) curr->OnDisconnected(curr);
      curr = curr->next;
    }
    zb_give_mutex();
  }
}

static void zb_action_handler(ezb_zcl_core_action_callback_id_t callback_id, void *message) {
  if (!message) return;
  ezb_zcl_set_attr_value_message_t *attribute_message =
      callback_id == EZB_ZCL_CORE_SET_ATTR_VALUE_CB_ID ? message : NULL;
  if (atomic_load(&zigbeeService.closing) || !zb_take_mutex(pdMS_TO_TICKS(50))) {
    if (attribute_message) attribute_message->out.result = EZB_ZCL_STATUS_FAIL;
    return;
  }
  IZigbeeEndpoint *curr = zigbeeService.endpointList;
  while (curr) {
    esp_err_t result = ESP_OK;
    if (curr->HandleAction && curr->HandleAction(curr, callback_id, message, &result)) {
      if (attribute_message) attribute_message->out.result = result == ESP_OK ? EZB_ZCL_STATUS_SUCCESS :
          result == ESP_ERR_INVALID_ARG || result == ESP_ERR_INVALID_SIZE ? EZB_ZCL_STATUS_INVALID_VALUE : EZB_ZCL_STATUS_FAIL;
      zb_give_mutex();
      return;
    }
    curr = curr->next;
  }
  zb_give_mutex();
}

#ifndef ESP_ZB_APP_SIGNAL_T_DEFINED
typedef struct esp_zb_app_signal_s {
    uint32_t *p_app_signal;
    esp_err_t esp_err_status;
} esp_zb_app_signal_t;
#define ESP_ZB_APP_SIGNAL_T_DEFINED
#endif

static TimerHandle_t s_steerRetryTimer = NULL;

static void SteeringRetryTimerCb(TimerHandle_t xTimer) {
  (void)xTimer;
  if (zigbeeService.currentState == SERVICE_STATE_STARTED && !atomic_load(&zigbeeService.closing) &&
      atomic_load(&zigbeeService.isPairing) && !GetConnected()) {
    // The shared FreeRTOS timer daemon must never wait for the Zigbee worker.
    // This periodic timer naturally retries on its next expiry.
    if (!Zigbee_TrySdkAccess(&zigbeeService.base, 0)) return;
    if (zigbeeService.currentState != SERVICE_STATE_STARTED || atomic_load(&zigbeeService.closing) ||
        !atomic_load(&zigbeeService.isPairing) || GetConnected()) {
      Zigbee_ReleaseSdkAccess(&zigbeeService.base);
      return;
    }
    ESP_LOGI(TAG, "[PAIRING] Periodic re-steering scan pass starting...");
    ezb_err_t result = ezb_bdb_set_primary_channel_set(RACOOL_ZB_PRIMARY_CHANNELS);
    if (result == EZB_ERR_NONE) result = ezb_bdb_start_top_level_commissioning(EZB_BDB_MODE_NETWORK_STEERING);
    Zigbee_ReleaseSdkAccess(&zigbeeService.base);
    if (result != EZB_ERR_NONE) ESP_LOGE(TAG, "Pairing retry failed: %d", (int)result);
  }
}

void esp_zb_app_signal_handler(esp_zb_app_signal_t *signal_struct) {
  if (!signal_struct || !signal_struct->p_app_signal || atomic_load(&zigbeeService.closing)) return;

  uint32_t *p_sg_p = signal_struct->p_app_signal;
  esp_err_t err_status = signal_struct->esp_err_status;
  esp_zb_app_signal_type_t sig_type = (esp_zb_app_signal_type_t)(*p_sg_p & 0xFFFF);
  const void *params = ezb_app_signal_get_params((const ezb_app_signal_t *)p_sg_p);

  /* Only a new RESET-type terminal signal can complete an admitted SDK reset.
   * Record before external handlers; no allocation, publication or waiting. */
  if (sig_type == EZB_ZDO_SIGNAL_LEAVE && params &&
      ((const ezb_zdo_signal_leave_params_t *)params)->leave_type == EZB_ZDO_LEAVE_TYPE_RESET) {
    portENTER_CRITICAL(&factoryResetMux);
    if (factoryResetStatus.pending) {
      factoryResetStatus.result = err_status;
      factoryResetStatus.pending = false;
    }
    portEXIT_CRITICAL(&factoryResetMux);
  }

  if (zb_take_mutex(pdMS_TO_TICKS(50))) {
    IZigbeeSignalHandler *curr = zigbeeService.signalHandlerList;
    while (curr) {
      curr->HandleSignal(curr, sig_type, err_status, params);
      curr = curr->next;
    }
    zb_give_mutex();
  }

  const char *sig_str = ezb_app_signal_to_string(sig_type);
  ESP_LOGI(TAG, "[ZB-SIGNAL] Signal: 0x%04X (%s), Status: %s (0x%x)",
           (unsigned int)sig_type, sig_str ? sig_str : "UNKNOWN",
           esp_err_to_name(err_status), (unsigned int)err_status);

  if (sig_type == EZB_BDB_SIGNAL_DEVICE_FIRST_START || sig_type == EZB_BDB_SIGNAL_DEVICE_REBOOT) {
    if (err_status == ESP_OK) {
      if (!ezb_bdb_is_factory_new()) {
        SetConnected(true);
        if (zigbeeService.startTimeSec == 0) zigbeeService.startTimeSec = xTaskGetTickCount() / configTICK_RATE_HZ;
        ESP_LOGI(TAG, "[PAIRING] >>> Device joined Zigbee network successfully (reboot/start). <<<");
        NotifyEndpointsConnected(&zigbeeService.base);

        IEventBus *bus = GetEventBus();
        if (bus && bus->Publish) {
          bus->Publish((BusEvent){.eventName = "ZIGBEE_PAIRING_SUCCESS", .type = EVENT_TYPE_INFO});
          bus->Publish((BusEvent){.eventName = "zigbee_connected", .type = EVENT_TYPE_INFO});
        }
      } else {
        ESP_LOGI(TAG, "[PAIRING] Device started factory-new. Ready for pairing.");
      }
    } else {
      ESP_LOGE(TAG, "[PAIRING] Device start/reboot error: %s", esp_err_to_name(err_status));
    }
  } else if (sig_type == EZB_BDB_SIGNAL_STEERING) {
    if (err_status == ESP_OK) {
      if (s_steerRetryTimer) {
        xTimerStop(s_steerRetryTimer, 0);
      }
      atomic_store(&zigbeeService.isPairing, false);
      SetConnected(true);
      if (zigbeeService.startTimeSec == 0) zigbeeService.startTimeSec = xTaskGetTickCount() / configTICK_RATE_HZ;
      ESP_LOGI(TAG, "[PAIRING] >>> Zigbee Network Steering SUCCESS! Connected to network. <<<");
      NotifyEndpointsConnected(&zigbeeService.base);

      IEventBus *bus = GetEventBus();
      if (bus && bus->Publish) {
        bus->Publish((BusEvent){.eventName = "ZIGBEE_PAIRING_SUCCESS", .type = EVENT_TYPE_INFO});
        bus->Publish((BusEvent){.eventName = "zigbee_connected", .type = EVENT_TYPE_INFO});
        bus->Publish((BusEvent){.eventName = "zigbee_pairing_stop", .type = EVENT_TYPE_INFO});
      }
    } else if (err_status == ESP_ERR_NOT_FINISHED) {
      atomic_store(&zigbeeService.isPairing, true);
      ESP_LOGI(TAG, "[PAIRING] >>> Zigbee Network Steering IN PROGRESS (searching channels)... <<<");
    } else {
      bool isPairing = atomic_load(&zigbeeService.isPairing);
      uint32_t nowSec = (uint32_t)(esp_timer_get_time() / 1000000ULL);
      uint32_t elapsedSec = (nowSec >= zigbeeService.pairingStartTimeSec) ? (nowSec - zigbeeService.pairingStartTimeSec) : 0;

      if (isPairing && elapsedSec < 180) {
        ESP_LOGI(TAG, "[PAIRING] Scan pass finished (%s). Scheduling retry in 1.5s (elapsed: %lus / 180s)...",
                 esp_err_to_name(err_status), (unsigned long)elapsedSec);
        if (!s_steerRetryTimer) {
          s_steerRetryTimer = xTimerCreate("zb_steer_tmr", pdMS_TO_TICKS(1500), pdFALSE, NULL, SteeringRetryTimerCb);
        }
        if (s_steerRetryTimer) {
          xTimerStart(s_steerRetryTimer, 0);
        }
      } else {
        if (s_steerRetryTimer) {
          xTimerStop(s_steerRetryTimer, 0);
        }
        atomic_store(&zigbeeService.isPairing, false);
        SetConnected(false);
        ESP_LOGW(TAG, "[PAIRING] >>> Zigbee Network Steering FAILED or TIMED OUT (status=%s). <<<", esp_err_to_name(err_status));

        IEventBus *bus = GetEventBus();
        if (bus && bus->Publish) {
          bus->Publish((BusEvent){.eventName = "ZIGBEE_PAIRING_FAILED", .type = EVENT_TYPE_WARNING});
          bus->Publish((BusEvent){.eventName = "zigbee_pairing_failed", .type = EVENT_TYPE_WARNING});
        }
      }
    }
  } else if (sig_type == EZB_ZDO_SIGNAL_LEAVE) {
    atomic_store(&zigbeeService.isPairing, false);
    SetConnected(false);
    ESP_LOGW(TAG, "[ZB-SIGNAL] Received Leave signal - Disconnected from network.");
    NotifyEndpointsDisconnected();
    IEventBus *bus = GetEventBus();
    if (bus && bus->Publish) {
      bus->Publish((BusEvent){.eventName = "zigbee_disconnected", .type = EVENT_TYPE_WARNING});
    }
  }
}

typedef struct {
  esp_pm_lock_handle_t handle;
  bool held;
} ZigbeeInitPowerGuard;

/* The radio worker owns this guard until cleanup succeeds or chip restart. */
static esp_err_t ReleaseInitPowerGuard(ZigbeeInitPowerGuard *guard) {
  if (!guard) return ESP_ERR_INVALID_ARG;
  if (!guard->handle) return ESP_OK;
  if (guard->held) {
    esp_err_t result = esp_pm_lock_release(guard->handle);
    if (result != ESP_OK) return result;
    guard->held = false;
  }
  esp_err_t result = esp_pm_lock_delete(guard->handle);
  if (result == ESP_OK) guard->handle = NULL;
  return result;
}

static esp_err_t AcquireInitPowerGuard(ZigbeeInitPowerGuard *guard) {
  if (!guard) return ESP_ERR_INVALID_ARG;
  if (guard->handle || guard->held) return ESP_ERR_INVALID_STATE;
  esp_err_t result = esp_pm_lock_create(ESP_PM_CPU_FREQ_MAX, 0, "zb_init", &guard->handle);
  if (result != ESP_OK) return result;
  if (!guard->handle) return ESP_FAIL;
  result = esp_pm_lock_acquire(guard->handle);
  if (result == ESP_OK) guard->held = true;
  else {
    esp_err_t cleanup = ReleaseInitPowerGuard(guard);
    if (cleanup != ESP_OK) ESP_LOGE(TAG, "Radio PM guard rollback failed: %s", esp_err_to_name(cleanup));
  }
  return result;
}

static void esp_zb_task(void *pvParameters) {
    esp_zigbee_config_t config = {
        .device_config = {
            .device_type = EZB_NWK_DEVICE_TYPE_ROUTER,
            .install_code_policy = false,
            .zczr_config = {
                .max_children = ZIGBEE_ROUTER_MAX_CHILDREN,
            },
        },
        .platform_config = {
            .storage_partition_name = "zb_storage",
            .radio_config = {
                .radio_mode = ESP_ZIGBEE_RADIO_MODE_NATIVE,
            },
        },
    };
    
    (void)pvParameters;
    ZigbeeInitPowerGuard pm_guard = {0};
    bool afRegistrationSucceeded = false;
    /* Storage failures must not silently erase network credentials or counters. */
    esp_err_t err = nvs_flash_init_partition("zb_storage");
    if (err != ESP_OK) goto startup_done;
    err = AcquireInitPowerGuard(&pm_guard);
    if (err != ESP_OK) goto startup_done;
    ZigbeeRadioBoundary_Begin();
    err = esp_zigbee_init(&config);
    if (err != ESP_OK) goto startup_done;
    zigbeeService.sdkInitialized = true;
    err = ZigbeeRadioBoundary_Result();
    if (err != ESP_OK) goto startup_done;
    ezb_mem_config_t memory = {.buffer_pool_size = ZIGBEE_PACKET_BUFFER_CAPACITY,
        .aps_bind_table_src_size = ZIGBEE_BINDING_TABLE_CAPACITY,
        .aps_bind_table_dst_size = ZIGBEE_BINDING_TABLE_CAPACITY};
    ezb_err_t memoryResult = ezb_config_memory(&memory);
    if (memoryResult != EZB_ERR_NONE) {
      ESP_LOGE(TAG, "SDK packet memory configuration failed: error=%d capacity=%u",
          (int)memoryResult, (unsigned)memory.buffer_pool_size);
      err = memoryResult == EZB_ERR_NO_MEM ? ESP_ERR_NO_MEM : ESP_FAIL;
      goto startup_done;
    }
    ESP_LOGI(TAG, "SDK packet buffer capacity: %u", (unsigned)memory.buffer_pool_size);
    zigbeeService.device = ezb_af_create_device_desc();
    if (!zigbeeService.device) { err = ESP_ERR_NO_MEM; goto startup_done; }
    
    IZigbeeEndpoint *curr_ep = zigbeeService.endpointList;
    while (curr_ep) {
        if (!curr_ep->RegisterClusters) { err = ESP_ERR_INVALID_STATE; goto startup_done; }
        err = curr_ep->RegisterClusters(curr_ep, zigbeeService.device);
        if (err != ESP_OK) goto startup_done;
        curr_ep = curr_ep->next;
    }


    ESP_LOGI(TAG, "Registering SDK device descriptors");
    err = ResetDerivedReportingCache();
    if (err != ESP_OK) goto startup_done;
    ESP_LOGI(TAG, "Derived reporting cache cleared; registering endpoints");
    ezb_err_t registrationError = ezb_af_device_desc_register(zigbeeService.device);
    if (registrationError != EZB_ERR_NONE) {
        ESP_LOGE(TAG, "Device descriptor registration failed: sdk_error=%d", (int)registrationError);
        err = ESP_FAIL; goto startup_done;
    }
    afRegistrationSucceeded = true;
    ezb_zcl_core_action_handler_register(zb_action_handler);
    ESP_LOGI(TAG, "SDK device descriptors registered; starting network");

    if (ezb_nwk_set_device_type(EZB_NWK_DEVICE_TYPE_ROUTER) != EZB_ERR_NONE ||
        ezb_nwk_set_max_children(ZIGBEE_ROUTER_MAX_CHILDREN) != EZB_ERR_NONE ||
        ezb_bdb_set_primary_channel_set(RACOOL_ZB_PRIMARY_CHANNELS) != EZB_ERR_NONE ||
        ezb_bdb_set_secondary_channel_set(RACOOL_ZB_SECONDARY_CHANNELS) != EZB_ERR_NONE ||
        ezb_dev_start(false) != EZB_ERR_NONE) { err = ESP_FAIL; goto startup_done; }
    {
      IConfigurationManager *cm = ConfigurationManager_GetInstance();
      SystemSettings settings;
      int8_t dbm;
      if (!cm || !cm->GetSnapshot || cm->GetSnapshot(cm, &settings) != ESP_OK ||
          !TxProfilePower((ZigbeeTxProfile)settings.system.zigbee_tx_profile, &dbm)) {
        err = ESP_ERR_INVALID_STATE;
        goto startup_done;
      }
      err = ApplyTxPower(dbm);
    }
    
startup_done:
    {
      esp_err_t guard_result = ReleaseInitPowerGuard(&pm_guard);
      if (err == ESP_OK && guard_result != ESP_OK) err = guard_result;
    }
    atomic_store(&zigbeeService.startupResult, err);
    if (err == ESP_OK && !atomic_load(&zigbeeService.closing))
      zigbeeService.currentState = SERVICE_STATE_STARTED;
    atomic_store(&zigbeeService.startupComplete, true);
    if (err == ESP_OK && !atomic_load(&zigbeeService.closing)) {
      ESP_LOGI(TAG, "Starting Zigbee stack mainloop...");
      err = esp_zigbee_launch_mainloop();
      if (err != ESP_OK) ESP_LOGE(TAG, "SDK mainloop failed: %s", esp_err_to_name(err));
    }
    atomic_store(&zigbeeService.closing, true);
    atomic_store(&zigbeeService.mainloopExited, true);
    /* Stop releases its SDK lock before permitting deinit. Startup failures
     * have never admitted external readers, so their rollback is already safe. */
    if (atomic_load(&zigbeeService.startupResult) != ESP_OK)
      atomic_store(&zigbeeService.cleanupAllowed, true);
    while (!atomic_load(&zigbeeService.cleanupAllowed) || atomic_load(&zigbeeService.sdkReaders))
      vTaskDelay(1);
    esp_err_t cleanup = ESP_OK;
    /* SDK 2.0.5: create allocates an unregistered AF tree. Registration
     * installs the root in core globals, but clears it on a failed result;
     * deinit only traverses the installed root. The application must free
     * creation/endpoint/reporting-cache/registration failures itself. */
    if (!afRegistrationSucceeded && zigbeeService.device) {
      ezb_af_free_device_desc(zigbeeService.device);
      zigbeeService.device = NULL;
    }
    if (zigbeeService.sdkInitialized) {
      /* Once registered, the SDK's ezb_core_deinit -> af_desc_deinit owns
       * the AF device/endpoint tree. An explicit application free would
       * double-free it. Wait for mainloop exit and reader drain first. */
      ZigbeeRadioBoundary_Begin();
      cleanup = esp_zigbee_deinit();
      /* Invalidate the borrowed handle even on a partial SDK teardown:
       * retrying deinit or dereferencing it would be unsafe. */
      zigbeeService.device = NULL;
      if (cleanup == ESP_OK) cleanup = ZigbeeRadioBoundary_Result();
      if (cleanup != ESP_OK) ESP_LOGE(TAG, "SDK owner deinit failed: %s", esp_err_to_name(cleanup));
      zigbeeService.sdkInitialized = false;
    }
    if (cleanup == ESP_OK) Zigbee_ConsoleSdkStopped();
    esp_err_t guard_cleanup = ReleaseInitPowerGuard(&pm_guard);
    if (cleanup == ESP_OK) cleanup = guard_cleanup;
    atomic_store(&zigbeeService.cleanupResult, cleanup);
    atomic_store(&zigbeeService.workerExited, true);
    for (;;) vTaskSuspend(NULL);
}

static int32_t Zigbee_StartOwned(IService *self) {
  if (atomic_load(&zigbeeService.closing)) return ZIGBEE_ERROR_STACK_FAIL;
  if ((zigbeeService.currentState == SERVICE_STATE_INITIALIZED || zigbeeService.currentState == SERVICE_STATE_STOPPED) &&
      !zigbeeService.workerTask) {
    atomic_store(&zigbeeService.startupComplete, false);
    atomic_store(&zigbeeService.startupResult, ESP_ERR_INVALID_STATE);
    atomic_store(&zigbeeService.mainloopExited, false);
    zigbeeService.stopPosted = false;
    atomic_store(&zigbeeService.stopCallbackComplete, false);
    atomic_store(&zigbeeService.stopCallbackResult, ESP_ERR_NOT_FINISHED);
    atomic_store(&zigbeeService.cleanupAllowed, false);
    atomic_store(&zigbeeService.workerExited, false);
    atomic_store(&zigbeeService.cleanupResult, ESP_ERR_INVALID_STATE);
    if(!zigbeeService.reportingWorkerTask) {
      atomic_store(&zigbeeService.reportingWorkerStop,false);
      atomic_store(&zigbeeService.reportingWorkerExited,false);
      uint8_t stale;
      (void)xQueueReceive(zigbeeService.reportingQueue,&stale,0);
      TaskHandle_t task=NULL;
      if(xTaskCreate(ReportingWorker,"Zigbee_report",ZIGBEE_REPORT_WORKER_STACK_SIZE,NULL,
          ZIGBEE_REPORT_WORKER_PRIORITY,&task)!=pdPASS || !task) return ZIGBEE_ERROR_STACK_FAIL;
      zigbeeService.reportingWorkerTask=task;
    }
    // Use aligned static storage for the SDK radio worker.
    zigbeeService.workerTask = xTaskCreateStatic(esp_zb_task, "Zigbee_main", ZIGBEE_TASK_STACK_SIZE, NULL, ZIGBEE_TASK_PRIORITY, zb_stack, &zb_task_buffer);
    if (!zigbeeService.workerTask) {
      int32_t cleanup=StopReportingWorker(xTaskGetTickCount(),pdMS_TO_TICKS(ZIGBEE_WORKER_STOP_TIMEOUT_MS));
      if(cleanup!=ZIGBEE_ERROR_OK) atomic_store(&zigbeeService.closing,true);
      return cleanup==ZIGBEE_ERROR_OK ? ZIGBEE_ERROR_STACK_FAIL : cleanup;
    }
    TickType_t started = xTaskGetTickCount();
    TickType_t budget = pdMS_TO_TICKS(ZIGBEE_STARTUP_TIMEOUT_MS);
    if (!budget) budget = 1;
    /* A registered caller remains alive while waiting on this bounded SDK
     * operation. Do not unregister it or extend either watchdog/startup deadline. */
    esp_err_t watched=esp_task_wdt_status(NULL);
    if(watched!=ESP_OK && watched!=ESP_ERR_NOT_FOUND && watched!=ESP_ERR_INVALID_STATE) {
      atomic_store(&zigbeeService.closing,true);
      return ZIGBEE_ERROR_STACK_FAIL;
    }
    TickType_t heartbeat=started;
    TickType_t feedPeriod=pdMS_TO_TICKS(ZIGBEE_STARTUP_WATCHDOG_FEED_MS);
    if(!feedPeriod) feedPeriod=1;
    while (!atomic_load(&zigbeeService.startupComplete)) {
      TickType_t now=xTaskGetTickCount();
      if ((TickType_t)(now - started) >= budget) {
        atomic_store(&zigbeeService.closing, true);
        return ZIGBEE_ERROR_TIMEOUT;
      }
      if(watched==ESP_OK && (TickType_t)(now-heartbeat)>=feedPeriod) {
        esp_err_t feed=esp_task_wdt_reset();
        if(feed!=ESP_OK) {
          ESP_LOGE(TAG,"Startup caller watchdog feed failed: %s",esp_err_to_name(feed));
          atomic_store(&zigbeeService.closing,true);
          return ZIGBEE_ERROR_STACK_FAIL;
        }
        heartbeat=now;
      }
      vTaskDelay(1);
    }
    if (atomic_load(&zigbeeService.startupResult) != ESP_OK) {
      atomic_store(&zigbeeService.closing, true);
      return ZIGBEE_ERROR_STACK_FAIL;
    }
    if (atomic_load(&zigbeeService.reportingPending)) ScheduleReporting();
    
  }
  if (zigbeeService.currentState != SERVICE_STATE_STARTED) return ZIGBEE_ERROR_STACK_FAIL;
  if (Zigbee_RegisterConsoleCommands() != ESP_OK) return ZIGBEE_ERROR_STACK_FAIL;
  /* Retry incomplete bindings without creating a second SDK radio worker. */
  int32_t climate = ClimateZigbeePlugin_Init();
  int32_t system = SystemZigbeePlugin_Init();
  bool ready = climate == 0 && system == 0;
  atomic_store(&zigbeeService.pluginsReady, ready);
  return ready ? ZIGBEE_ERROR_OK : ZIGBEE_ERROR_STACK_FAIL;
}

static int32_t Zigbee_Start(IService *self) {
  if (self != &zigbeeService.base.base || !zigbeeService.lifecycleMutex) return ZIGBEE_ERROR_INVALID_PARAM;
  if (xSemaphoreTake(zigbeeService.lifecycleMutex, pdMS_TO_TICKS(ZIGBEE_CONTROL_TIMEOUT_MS)) != pdTRUE)
    return ZIGBEE_ERROR_LOCK_FAIL;
  int32_t result = Zigbee_StartOwned(self);
  xSemaphoreGive(zigbeeService.lifecycleMutex);
  return result;
}

/* The SDK queue callback runs in the mainloop. This avoids treating the SDK's
 * successful no-op before mainloop entry as an accepted stop request. */
static void Zigbee_RequestStopInMainloop(void *context) {
  (void)context;
  esp_err_t result = esp_zigbee_stop();
  atomic_store(&zigbeeService.stopCallbackResult, result);
  atomic_store(&zigbeeService.stopCallbackComplete, true);
}

static int32_t Zigbee_StopOwned(IService *self) {
  (void)self;
  atomic_store(&zigbeeService.closing, true);
  TaskHandle_t caller = xTaskGetCurrentTaskHandle();
  if ((caller && atomic_load(&zigbeeService.sdkAccessOwner) == caller) ||
      (caller && (zigbeeService.workerTask == caller || zigbeeService.reportingWorkerTask == caller))) return ZIGBEE_ERROR_LOCK_FAIL;
  TickType_t started = xTaskGetTickCount();
  TickType_t budget = pdMS_TO_TICKS(ZIGBEE_WORKER_STOP_TIMEOUT_MS);
  if (!budget) budget = 1;
  while (atomic_load(&zigbeeService.sdkReaders) ||
      (zigbeeService.workerTask && !atomic_load(&zigbeeService.startupComplete))) {
    if ((TickType_t)(xTaskGetTickCount() - started) >= budget) return ZIGBEE_ERROR_TIMEOUT;
    vTaskDelay(1);
  }
  if (FactoryResetPending() || FactoryResetMutationBlocked()) {
    /* Keep the SDK and terminal signal admission alive. Retry after LEAVE;
     * stopping its owner now would orphan an asynchronous reset. */
    atomic_store(&zigbeeService.closing, false);
    return ZIGBEE_ERROR_TIMEOUT;
  }
  if (zigbeeService.reportingTimer) {
    esp_err_t result = esp_timer_stop(zigbeeService.reportingTimer);
    if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) {
      zigbeeService.currentState = SERVICE_STATE_ERROR;
      atomic_store(&zigbeeService.reportingScheduleFailed, true);
      return ZIGBEE_ERROR_STACK_FAIL;
    }
  }
  int32_t reportingStop=StopReportingWorker(started,budget);
  if(reportingStop!=ZIGBEE_ERROR_OK) return reportingStop;
  if (s_steerRetryTimer && xTimerStop(s_steerRetryTimer, 0) != pdPASS)
    return ZIGBEE_ERROR_STACK_FAIL;
  if (zigbeeService.workerTask) {
    while (!atomic_load(&zigbeeService.workerExited) || eTaskGetState(zigbeeService.workerTask) != eSuspended) {
      TickType_t now = xTaskGetTickCount();
      if ((TickType_t)(now - started) >= budget) return ZIGBEE_ERROR_TIMEOUT;
      if (atomic_load(&zigbeeService.startupResult) == ESP_OK && !atomic_load(&zigbeeService.mainloopExited) &&
          !zigbeeService.stopPosted) {
        if (!esp_zigbee_lock_acquire(pdMS_TO_TICKS(ZIGBEE_LOCK_TIMEOUT_MS))) return ZIGBEE_ERROR_LOCK_FAIL;
        atomic_store(&zigbeeService.stopCallbackComplete, false);
        esp_err_t result = esp_zigbee_task_queue_post(Zigbee_RequestStopInMainloop, NULL);
        esp_zigbee_lock_release();
        if (result != ESP_OK) return ZIGBEE_ERROR_STACK_FAIL;
        zigbeeService.stopPosted = true;
      }
      if (zigbeeService.stopPosted && atomic_load(&zigbeeService.stopCallbackComplete) &&
          atomic_load(&zigbeeService.stopCallbackResult) != ESP_OK) {
        zigbeeService.stopPosted = false;
        return ZIGBEE_ERROR_STACK_FAIL;
      }
      /* The SDK stop request only schedules exit. Deinit is worker-owned and
       * may begin only after this caller has released the SDK lock. */
      if (atomic_load(&zigbeeService.mainloopExited)) atomic_store(&zigbeeService.cleanupAllowed, true);
      vTaskDelay(1);
    }
    if (atomic_load(&zigbeeService.cleanupResult) != ESP_OK) {
      zigbeeService.currentState = SERVICE_STATE_ERROR;
      return ZIGBEE_ERROR_STACK_FAIL;
    }
    vTaskDelete(zigbeeService.workerTask);
    zigbeeService.workerTask = NULL;
  } else if (zigbeeService.currentState == SERVICE_STATE_STARTED || zigbeeService.sdkInitialized) {
    /* Missing SDK ownership cannot be repaired by guessing that deinit ran. */
    zigbeeService.currentState = SERVICE_STATE_ERROR;
    return ZIGBEE_ERROR_STACK_FAIL;
  }
  zigbeeService.currentState = SERVICE_STATE_STOPPED;
  atomic_store(&zigbeeService.reportingPending, false);
  atomic_store(&zigbeeService.connectionNotificationPending, false);
  atomic_store(&zigbeeService.reportingScheduleFailed, false);
  atomic_store(&zigbeeService.reportingApplyFailed, false);
  atomic_store(&zigbeeService.reportingRevision, 0);
  zigbeeService.reportingAttemptRevision = 0;
  zigbeeService.reportingAttempts = 0;
  atomic_store(&zigbeeService.isPairing, false);
  atomic_store(&zigbeeService.pluginsReady, false);
  SetConnected(false);
  NotifyEndpointsDisconnected();
  portENTER_CRITICAL(&attributeMux);
  zigbeeService.attributeCount = 0;
  zigbeeService.attributeHead = 0;
  zigbeeService.sourceReportMask = 0;
  portEXIT_CRITICAL(&attributeMux);
  atomic_store(&zigbeeService.closing, false);
  return 0;
}

static int32_t Zigbee_Stop(IService *self) {
  if (self != &zigbeeService.base.base || !zigbeeService.lifecycleMutex) return ZIGBEE_ERROR_INVALID_PARAM;
  TaskHandle_t caller = xTaskGetCurrentTaskHandle();
  if (caller && (caller == zigbeeService.workerTask || caller == zigbeeService.reportingWorkerTask || caller == atomic_load(&zigbeeService.sdkAccessOwner))) {
    atomic_store(&zigbeeService.closing, true);
    return ZIGBEE_ERROR_LOCK_FAIL;
  }
  if (xSemaphoreTake(zigbeeService.lifecycleMutex, pdMS_TO_TICKS(ZIGBEE_CONTROL_TIMEOUT_MS)) != pdTRUE)
    return ZIGBEE_ERROR_LOCK_FAIL;
  int32_t result = Zigbee_StopOwned(self);
  xSemaphoreGive(zigbeeService.lifecycleMutex);
  return result;
}

static ServiceState Zigbee_GetState(IService *self) { return zigbeeService.currentState; }

static int32_t Zigbee_CheckHealth(IService *self) {
  return (zigbeeService.currentState == SERVICE_STATE_STARTED && GetConnected() &&
      !atomic_load(&zigbeeService.closing) && atomic_load(&zigbeeService.pluginsReady) &&
      !atomic_load(&zigbeeService.connectionNotificationPending) &&
      !atomic_load(&zigbeeService.reportingScheduleFailed) &&
      !atomic_load(&zigbeeService.reportingApplyFailed)) ? 0 : -1;
}

static int32_t Zigbee_QueueAttributeUpdate(IZigbee *self, uint8_t endpoint, uint16_t cluster,
    uint16_t attribute, const void *value, uint8_t length) {
  if (!self || !value || !length || length > ZIGBEE_ATTRIBUTE_MAX_BYTES || !endpoint)
    return ZIGBEE_ERROR_INVALID_PARAM;
  portENTER_CRITICAL(&attributeMux);
  if (zigbeeService.currentState != SERVICE_STATE_STARTED || atomic_load(&zigbeeService.closing) || !GetConnected()) {
    portEXIT_CRITICAL(&attributeMux);
    return ZIGBEE_ERROR_NOT_CONNECTED;
  }
  AttributeUpdate *slot = NULL;
  for (unsigned i = 0; i < zigbeeService.attributeCount; ++i) {
    AttributeUpdate *candidate = &zigbeeService.attributes[(zigbeeService.attributeHead + i) % ZIGBEE_ATTRIBUTE_QUEUE_CAPACITY];
    if (!IsFanRelayAttribute(endpoint, cluster, attribute) &&
        candidate->endpoint == endpoint && candidate->cluster == cluster && candidate->attribute == attribute) {
      /* Identical samples do not replace an outstanding delivery obligation.
       * Preserve both an in-flight revision and its retry/backoff after the
       * local setter succeeded but the report request was rejected. */
      if (candidate->length == length && memcmp(candidate->value.bytes, value, length) == 0) {
        portEXIT_CRITICAL(&attributeMux);
        return ScheduleReporting() ? ZIGBEE_ERROR_OK : ZIGBEE_ERROR_STACK_FAIL;
      }
      slot = candidate; break;
    }
  }
  bool replacing = slot != NULL;
  if (!slot) {
    if (zigbeeService.attributeCount == ZIGBEE_ATTRIBUTE_QUEUE_CAPACITY) {
      portEXIT_CRITICAL(&attributeMux);
      return ZIGBEE_ERROR_QUEUE_FULL;
    }
    slot = &zigbeeService.attributes[(zigbeeService.attributeHead + zigbeeService.attributeCount++) % ZIGBEE_ATTRIBUTE_QUEUE_CAPACITY];
  }
  /* A new value supersedes the sample, not a previously rejected report. */
  bool reportPending = replacing && slot->reportPending;
  uint8_t attempts = reportPending ? slot->attempts : 0;
  uint32_t failedMs = reportPending ? slot->failedMs : 0;
  *slot = (AttributeUpdate){.endpoint=endpoint, .cluster=cluster, .attribute=attribute,
      .length=length, .revision=++zigbeeService.attributeRevision,
      .reportPending=reportPending, .attempts=attempts, .failedMs=failedMs};
  memcpy(slot->value.bytes, value, length);
  portEXIT_CRITICAL(&attributeMux);
  return ScheduleReporting() ? ZIGBEE_ERROR_OK : ZIGBEE_ERROR_STACK_FAIL;
}

static int32_t Zigbee_GetAttributeStatus(IZigbee *self, ZigbeeAttributeStatus *status) {
  if (!self || !status) return ZIGBEE_ERROR_INVALID_PARAM;
  portENTER_CRITICAL(&attributeMux);
  *status = (ZigbeeAttributeStatus){.pending=zigbeeService.attributeCount,
      .applied=zigbeeService.attributeApplied, .failed=zigbeeService.attributeFailed,
      .lock_deferrals=zigbeeService.attributeLockDeferrals,
      .retries=zigbeeService.attributeRetries, .dropped=zigbeeService.attributeDropped};
  portEXIT_CRITICAL(&attributeMux);
  return ZIGBEE_ERROR_OK;
}

static int32_t Zigbee_NotifyAttributeChanged(IZigbee *self, uint8_t endpoint, uint16_t cluster, uint16_t attribute, void *value, uint8_t length) {
    if (!self || !value || length == 0) return ZIGBEE_ERROR_INVALID_PARAM;
    if (!GetConnected()) return ZIGBEE_ERROR_NOT_CONNECTED;
    TickType_t lockStart = xTaskGetTickCount();
    if (!Zigbee_TrySdkAccess(&zigbeeService.base, ZIGBEE_LOCK_TIMEOUT_MS)) {
#ifndef RACOOL_TEST_NATIVE
        ESP_LOGE(TAG, "Attribute SDK lock timeout: caller=%s wait_ms=%lu endpoint=%u cluster=0x%04x attribute=0x%04x",
            pcTaskGetName(NULL), (unsigned long)((xTaskGetTickCount() - lockStart) * portTICK_PERIOD_MS),
            (unsigned)endpoint, (unsigned)cluster, (unsigned)attribute);
#else
        (void)lockStart;
#endif
        return ZIGBEE_ERROR_LOCK_FAIL;
    }
    ezb_zcl_status_t status = SetNumericAttribute(endpoint, cluster, attribute, value, length, false, NULL);
    Zigbee_ReleaseSdkAccess(&zigbeeService.base);
    return (status == EZB_ZCL_STATUS_SUCCESS) ? ZIGBEE_ERROR_OK : ZIGBEE_ERROR_ATTRIBUTE_FAIL;
}

static int32_t Zigbee_ConnectToNetwork(IZigbee *self) {
  if (!Zigbee_TrySdkAccess(self, ZIGBEE_LOCK_TIMEOUT_MS)) return ZIGBEE_ERROR_LOCK_FAIL;
  if (FactoryResetPending() || FactoryResetMutationBlocked()) {
    Zigbee_ReleaseSdkAccess(self);
    return ESP_ERR_NOT_FINISHED;
  }
  ESP_LOGI(TAG, "[PAIRING] >>> User initiated Zigbee Network Steering (Pairing Mode)... <<<");
  atomic_store(&zigbeeService.isPairing, true);
  zigbeeService.pairingStartTimeSec = (uint32_t)(esp_timer_get_time() / 1000000ULL);

  if (s_steerRetryTimer) {
    xTimerStop(s_steerRetryTimer, 0);
  }

  IEventBus *bus = ServiceLocator_GetEventBus();
  if (bus) {
    bus->Publish((BusEvent){.eventName = "ZIGBEE_PAIRING_STARTED", .type = EVENT_TYPE_INFO});
    bus->Publish((BusEvent){.eventName = "zigbee_pairing_start", .type = EVENT_TYPE_INFO});
  }

  if (!ezb_bdb_is_factory_new()) {
    ESP_LOGW(TAG, "[PAIRING] Device not factory-new -> resetting local action before steering");
    ezb_bdb_reset_via_local_action();
  }
  ezb_bdb_set_primary_channel_set(RACOOL_ZB_PRIMARY_CHANNELS);
  ezb_bdb_set_secondary_channel_set(RACOOL_ZB_SECONDARY_CHANNELS);

  esp_err_t ret = ezb_bdb_start_top_level_commissioning(EZB_BDB_MODE_NETWORK_STEERING);
  Zigbee_ReleaseSdkAccess(self);

  ESP_LOGI(TAG, "[PAIRING] ezb_bdb_start_top_level_commissioning executed, ret=%d (%s)",
           ret, esp_err_to_name(ret));

  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "[PAIRING] Failed to start commissioning: %s. Clearing isPairing flag.", esp_err_to_name(ret));
    atomic_store(&zigbeeService.isPairing, false);
  }

  return (ret == ESP_OK) ? 0 : -1;
}


static bool Zigbee_IsPairing(IZigbee *self) {
  return atomic_load(&zigbeeService.isPairing);
}

static int32_t Zigbee_DisconnectFromNetwork(IZigbee *self) {
  if (!Zigbee_TrySdkAccess(self, ZIGBEE_LOCK_TIMEOUT_MS)) return ZIGBEE_ERROR_LOCK_FAIL;
  if (FactoryResetPending() || FactoryResetMutationBlocked()) {
    Zigbee_ReleaseSdkAccess(self);
    return ESP_ERR_NOT_FINISHED;
  }
  atomic_store(&zigbeeService.isPairing, false);
  if (s_steerRetryTimer) {
    xTimerStop(s_steerRetryTimer, 0);
  }
  ezb_bdb_reset_via_local_action();
  Zigbee_ReleaseSdkAccess(self);
  SetConnected(false);

  IEventBus *bus = GetEventBus();
  if (bus && bus->Publish) {
    bus->Publish((BusEvent){.eventName = "zigbee_pairing_stop", .type = EVENT_TYPE_INFO});
  }
  return 0;
}

static int32_t Zigbee_BeginFactoryReset(IZigbee *self, uint32_t *operation_id) {
  if (self != &zigbeeService.base || !operation_id) return ESP_ERR_INVALID_ARG;
  if (!Zigbee_TrySdkAccess(self, ZIGBEE_LOCK_TIMEOUT_MS)) return ZIGBEE_ERROR_LOCK_FAIL;
  if (FactoryResetMutationBlocked()) {
    Zigbee_ReleaseSdkAccess(self);
    return ESP_ERR_NOT_FINISHED;
  }
  bool factory_new = ezb_bdb_is_factory_new();
  bool connected = GetConnected();
  int32_t result = ESP_OK;
  portENTER_CRITICAL(&factoryResetMux);
  if (factoryResetStatus.pending) result = ESP_ERR_NOT_FINISHED;
  else if (atomic_load(&zigbeeService.isPairing) ||
           (!connected && !factory_new) || (connected && factory_new)) result = ESP_ERR_INVALID_STATE;
  else {
    if (++factoryResetStatus.operation_id == 0) ++factoryResetStatus.operation_id;
    factoryResetStatus.pending = !factory_new;
    factoryResetStatus.result = ESP_OK;
    *operation_id = factoryResetStatus.operation_id;
  }
  portEXIT_CRITICAL(&factoryResetMux);
  /* The SDK owns the asynchronous leave/reset and preserves the outgoing
   * network frame counter. Raw partition erasure would violate that contract
   * and erasing default NVS would also destroy unrelated user settings. */
  if (result == ESP_OK && !factory_new) {
    ESP_LOGI(TAG, "Requesting SDK-owned Zigbee local reset");
    ezb_bdb_reset_via_local_action();
  }
  Zigbee_ReleaseSdkAccess(self);
  return result;
}

static int32_t Zigbee_ResetToFactorySettings(IZigbee *self) {
  uint32_t operation_id;
  return Zigbee_BeginFactoryReset(self, &operation_id);
}

static int32_t Zigbee_GetFactoryResetStatus(IZigbee *self, ZigbeeFactoryResetStatus *status) {
  if (self != &zigbeeService.base || !status) return ESP_ERR_INVALID_ARG;
  if (!Zigbee_TrySdkAccess(self, ZIGBEE_LOCK_TIMEOUT_MS)) return ZIGBEE_ERROR_LOCK_FAIL;
  portENTER_CRITICAL(&factoryResetMux);
  ZigbeeFactoryResetStatus snapshot = factoryResetStatus;
  portEXIT_CRITICAL(&factoryResetMux);
  snapshot.factory_new = ezb_bdb_is_factory_new();
  snapshot.connected = GetConnected();
  Zigbee_ReleaseSdkAccess(self);
  *status = snapshot;
  return ESP_OK;
}

static int32_t Zigbee_IsConnected(IZigbee *self) { return GetConnected(); }

static int32_t Zigbee_GetNetworkInfo(IZigbee *self, ZigbeeNetworkInfo *info) {
  if (!self || !info) return ZIGBEE_ERROR_INVALID_PARAM;
  if (!GetConnected()) return ZIGBEE_ERROR_NOT_CONNECTED;
  if (!Zigbee_TrySdkAccess(&zigbeeService.base, ZIGBEE_LOCK_TIMEOUT_MS)) return ZIGBEE_ERROR_LOCK_FAIL;
  ZigbeeNetworkInfo snapshot = {0};
  if (!zb_take_mutex(0)) { Zigbee_ReleaseSdkAccess(&zigbeeService.base); return ZIGBEE_ERROR_LOCK_FAIL; }
  snapshot.endpoint_count = zigbeeService.endpointCount;
  zb_give_mutex();
  snapshot.pan_id = ezb_get_panid();
  snapshot.short_address = ezb_get_short_address();
  snapshot.channel = ezb_get_current_channel();
  snapshot.is_router = (ezb_nwk_get_device_type() == EZB_NWK_DEVICE_TYPE_ROUTER);
  ezb_get_extended_panid((ezb_extpanid_t *)snapshot.ext_pan_id);
  ezb_nwk_info_iterator_t iterator = EZB_NWK_INFO_ITERATOR_INIT;
  ezb_nwk_neighbor_info_t neighbor = {0};
  ezb_err_t result;
  while ((result = ezb_nwk_get_next_neighbor(&iterator, &neighbor)) == EZB_ERR_NONE) {
    if (snapshot.neighbor_count == UINT8_MAX) {
      Zigbee_ReleaseSdkAccess(&zigbeeService.base); return ZIGBEE_ERROR_STACK_FAIL;
    }
    ++snapshot.neighbor_count;
    if (neighbor.relationship == EZB_NWK_RELATIONSHIP_CHILD) ++snapshot.child_count;
    if (neighbor.short_addr == 0) {
      snapshot.rssi = neighbor.rssi;
      snapshot.lqi = neighbor.lqi;
      snapshot.coordinator_link_valid = true;
    }
  }
  Zigbee_ReleaseSdkAccess(&zigbeeService.base);
  if (result != EZB_ERR_NOT_FOUND) return ZIGBEE_ERROR_STACK_FAIL;
  snapshot.firmware_version = ZIGBEE_FIRMWARE_VERSION;
  snapshot.uptime_sec = (xTaskGetTickCount() / configTICK_RATE_HZ) - zigbeeService.startTimeSec;
  *info = snapshot;
  return ZIGBEE_ERROR_OK;
}

static int32_t Zigbee_SetPowerMode(IZigbee *self, ZigbeePowerMode mode) {
  if (self != &zigbeeService.base || (mode != ZIGBEE_POWER_MODE_ACTIVE && mode != ZIGBEE_POWER_MODE_LIGHT_SLEEP))
    return ESP_ERR_INVALID_ARG;
  /* Router sleep is owned by PowerManager, not a successful no-op here. */
  return ESP_ERR_NOT_SUPPORTED;
}

static int32_t Zigbee_GetTxStatus(IZigbee *self, ZigbeeTxStatus *status) {
  if (self != &zigbeeService.base || !status) return ESP_ERR_INVALID_ARG;
  if (!Zigbee_TrySdkAccess(self, ZIGBEE_LOCK_TIMEOUT_MS)) return ESP_ERR_INVALID_STATE;
  int8_t observed = INT8_MIN;
  ezb_get_tx_power(&observed);
  Zigbee_ReleaseSdkAccess(self);
  for (unsigned mode = 0; mode < ZIGBEE_TX_PROFILE_COUNT; ++mode) {
    int8_t expected;
    if (TxProfilePower((ZigbeeTxProfile)mode, &expected) && expected == observed) {
      *status = (ZigbeeTxStatus){.profile=(ZigbeeTxProfile)mode, .dbm=observed};
      return ESP_OK;
    }
  }
  return ESP_ERR_INVALID_STATE;
}

static int32_t Zigbee_SetTxProfile(IZigbee *self, ZigbeeTxProfile profile) {
  int8_t desired;
  if (self != &zigbeeService.base || !TxProfilePower(profile, &desired)) return ESP_ERR_INVALID_ARG;
  TaskHandle_t caller = xTaskGetCurrentTaskHandle();
  if (!caller || caller == zigbeeService.workerTask || atomic_load(&zigbeeService.sdkAccessOwner) == caller)
    return ESP_ERR_INVALID_STATE;
  IConfigurationManager *cm = ConfigurationManager_GetInstance();
  if (!cm || !cm->UpdateFields) return ESP_ERR_INVALID_STATE;
  if (cm->IsMutationAllowed && !cm->IsMutationAllowed(cm)) return ESP_ERR_NOT_FINISHED;
  if (!zigbeeService.lifecycleMutex ||
      xSemaphoreTake(zigbeeService.lifecycleMutex, pdMS_TO_TICKS(ZIGBEE_CONTROL_TIMEOUT_MS)) != pdTRUE)
    return ESP_ERR_TIMEOUT;
  esp_err_t result = ESP_ERR_INVALID_STATE;
  if (Zigbee_TrySdkAccess(self, ZIGBEE_LOCK_TIMEOUT_MS)) {
    int8_t previous = INT8_MIN;
    ezb_get_tx_power(&previous);
    if (previous != INT8_MIN) {
      result = ApplyTxPower(desired);
      Zigbee_ReleaseSdkAccess(self);
      if (result == ESP_OK) {
        uint8_t saved = (uint8_t)profile;
        ConfigurationFieldUpdate update = {offsetof(SystemBaseConfigs, zigbee_tx_profile), &saved, sizeof(saved)};
        result = cm->UpdateFields(cm, "system", &update, 1, true);
      }
      if (result != ESP_OK) {
        /* Keep flash writes outside SDK ownership; lifecycle gate prevents Stop.
         * Failed rollback is explicit, never a false applied-profile success. */
        esp_err_t rollback = ESP_ERR_TIMEOUT;
        if (Zigbee_TrySdkAccess(self, ZIGBEE_LOCK_TIMEOUT_MS)) {
          rollback = ApplyTxPower(previous);
          Zigbee_ReleaseSdkAccess(self);
        }
        if (rollback != ESP_OK) {
          ESP_LOGE(TAG, "TX profile rollback failed: %s", esp_err_to_name(rollback));
          result = rollback;
        }
      }
    } else Zigbee_ReleaseSdkAccess(self);
  }
  xSemaphoreGive(zigbeeService.lifecycleMutex);
  return result;
}

static int32_t Zigbee_RegisterEndpoint(IZigbee *self, IZigbeeEndpoint *endpoint) {
  if (!endpoint) return ZIGBEE_ERROR_INVALID_PARAM;
  if (!zb_take_mutex(pdMS_TO_TICKS(100))) return ZIGBEE_ERROR_LOCK_FAIL;
  if (zigbeeService.workerTask || atomic_load(&zigbeeService.closing)) {
    zb_give_mutex(); return ZIGBEE_ERROR_STACK_FAIL;
  }

  for (IZigbeeEndpoint *current = zigbeeService.endpointList; current; current = current->next) {
    if (current == endpoint) {
      zb_give_mutex();
      return ZIGBEE_ERROR_OK;
    }
  }
  if (zigbeeService.endpointCount >= ZIGBEE_MAX_ENDPOINTS) {
    zb_give_mutex();
    return ZIGBEE_ERROR_MAX_ENDPOINTS;
  }
  endpoint->next = zigbeeService.endpointList;
  zigbeeService.endpointList = endpoint;
  zigbeeService.endpointCount++;
  zb_give_mutex();
  return ZIGBEE_ERROR_OK;
}

static int32_t Zigbee_RegisterSignalHandler(IZigbee *self, IZigbeeSignalHandler *handler) {
  if (!handler) return ZIGBEE_ERROR_INVALID_PARAM;
  if (!zb_take_mutex(pdMS_TO_TICKS(100))) return ZIGBEE_ERROR_LOCK_FAIL;
  if (zigbeeService.workerTask || atomic_load(&zigbeeService.closing)) {
    zb_give_mutex(); return ZIGBEE_ERROR_STACK_FAIL;
  }

  for (IZigbeeSignalHandler *current = zigbeeService.signalHandlerList; current; current = current->next) {
    if (current == handler) {
      zb_give_mutex();
      return ZIGBEE_ERROR_OK;
    }
  }
  handler->next = zigbeeService.signalHandlerList;
  zigbeeService.signalHandlerList = handler;
  zb_give_mutex();
  return ZIGBEE_ERROR_OK;
}

static uint8_t Zigbee_GetEndpointCount(IZigbee *self) { return zigbeeService.endpointCount; }

static int32_t Zigbee_Initialize(IService *self, IDIRegistry *registry) {
  (void)registry;
  if (self != &zigbeeService.base.base) return ZIGBEE_ERROR_INVALID_PARAM;
  if (atomic_load(&zigbeeService.closing)) return ZIGBEE_ERROR_STACK_FAIL;
  if (zigbeeService.currentState == SERVICE_STATE_INITIALIZED ||
      zigbeeService.currentState == SERVICE_STATE_STARTED) return ZIGBEE_ERROR_OK;
  // Restart preserves the service locks and endpoint registry; use Start after Stop.
  if (zigbeeService.currentState == SERVICE_STATE_STOPPED ||
      zigbeeService.currentState == SERVICE_STATE_ERROR) return ZIGBEE_ERROR_INVALID_PARAM;
  if (zigbeeService.mutex == NULL) {
    zigbeeService.mutex = xSemaphoreCreateMutex();
    if (!zigbeeService.mutex) return ZIGBEE_ERROR_LOCK_FAIL;
  }
  if (!zigbeeService.lifecycleMutex) {
    zigbeeService.lifecycleMutex = xSemaphoreCreateMutex();
    if (!zigbeeService.lifecycleMutex) return ZIGBEE_ERROR_LOCK_FAIL;
  }

  if(!zigbeeService.reportingQueue) {
    zigbeeService.reportingQueue=xQueueCreate(1,sizeof(uint8_t));
    if(!zigbeeService.reportingQueue) return ZIGBEE_ERROR_STACK_FAIL;
  }
  if (!zigbeeService.reportingTimer) {
    const esp_timer_create_args_t args = {
      .callback = ReportingTimerCallback, .name = "zb_reporting", .dispatch_method = ESP_TIMER_TASK
    };
    if (esp_timer_create(&args, &zigbeeService.reportingTimer) != ESP_OK || !zigbeeService.reportingTimer)
      return ZIGBEE_ERROR_STACK_FAIL;
  }

  IZigbee *zigbee = &zigbeeService.base;
  int32_t result = zigbee->RegisterSignalHandler(zigbee, SignalHandlerCore_Create());
  if (result != ZIGBEE_ERROR_OK) return result;
  IZigbeeEndpoint *(*factories[])(void) = {
    EndpointFan_Create, EndpointAmbientTemp_Create, EndpointCPUTemp_Create,
    EndpointInternalLED_Create, EndpointRGBLED_Create, EndpointHealth_Create,
    EndpointMemory_Create, EndpointUptime_Create, EndpointGroups_Create, EndpointOTA_Create
  };
  for (size_t i = 0; i < sizeof(factories) / sizeof(factories[0]); ++i) {
    result = zigbee->RegisterEndpoint(zigbee, factories[i]());
    if (result != ZIGBEE_ERROR_OK) return result;
  }

  zigbeeService.currentState = SERVICE_STATE_INITIALIZED;
  atomic_store(&zigbeeService.isConnected, false);
  zigbeeService.startTimeSec = 0;
  return ZIGBEE_ERROR_OK;
}

IZigbee *Zigbee_Create(void) {
  static bool initialized = false;
  if (!initialized) {
    memset(&zigbeeService, 0, sizeof(ZigbeeService));
    if (zigbeeService.mutex == NULL) {
      zigbeeService.mutex = xSemaphoreCreateMutex();
    }
    zigbeeService.base.base.Initialize = Zigbee_Initialize;
    zigbeeService.base.base.Start = Zigbee_Start;
    zigbeeService.base.base.Stop = Zigbee_Stop;
    zigbeeService.base.base.GetState = Zigbee_GetState;
    zigbeeService.base.base.CheckHealth = Zigbee_CheckHealth;

    zigbeeService.base.NotifyAttributeChanged = Zigbee_NotifyAttributeChanged;
    zigbeeService.base.QueueAttributeUpdate = Zigbee_QueueAttributeUpdate;
    zigbeeService.base.GetAttributeStatus = Zigbee_GetAttributeStatus;
    zigbeeService.base.TrySdkAccess = Zigbee_TrySdkAccess;
    zigbeeService.base.ReleaseSdkAccess = Zigbee_ReleaseSdkAccess;
    zigbeeService.base.ConnectToNetwork = Zigbee_ConnectToNetwork;
    zigbeeService.base.DisconnectFromNetwork = Zigbee_DisconnectFromNetwork;
    zigbeeService.base.IsConnected = Zigbee_IsConnected;
    zigbeeService.base.IsPairing = Zigbee_IsPairing;
    zigbeeService.base.ResetToFactorySettings = Zigbee_ResetToFactorySettings;
    zigbeeService.base.BeginFactoryReset = Zigbee_BeginFactoryReset;
    zigbeeService.base.GetFactoryResetStatus = Zigbee_GetFactoryResetStatus;
    zigbeeService.base.GetNetworkInfo = Zigbee_GetNetworkInfo;
    zigbeeService.base.SetPowerMode = Zigbee_SetPowerMode;
    zigbeeService.base.SetTxProfile = Zigbee_SetTxProfile;
    zigbeeService.base.GetTxStatus = Zigbee_GetTxStatus;
    zigbeeService.base.RegisterEndpoint = Zigbee_RegisterEndpoint;
    zigbeeService.base.RegisterSignalHandler = Zigbee_RegisterSignalHandler;
    zigbeeService.base.GetEndpointCount = Zigbee_GetEndpointCount;
    zigbeeService.base.ReconfigureAllReporting = Zigbee_ReconfigureAllReporting;

    initialized = true;
  }
  return &zigbeeService.base;
}
#endif // RACOOL_TARGET_TESTING

