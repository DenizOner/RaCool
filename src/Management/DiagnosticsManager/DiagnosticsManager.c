#include "Core/DI/IDIRegistry.h"
#include "Core/ServiceLocator/ServiceLocator.h"
#include "Core/ServiceLocator/ServiceNames.h"
#include "Core/EventBus/IEventBus.h"
#include "Management/DiagnosticsManager/DiagnosticsManagerConfig.h"
#include "Management/DiagnosticsManager/IDiagnosticsManager.h"
#include "Management/ConfigurationManager/SystemConfigs.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdatomic.h>
#include <math.h>

static const char *TAG = "DiagnosticsManager";

typedef struct {
    IDiagnosticsManager base;
    _Atomic(ServiceState) state;
    DiagnosticsManagerConfig config;
    DiagnosticsTestResult lastResult;
    /* Owned by the existing worker; lifecycle clears only after joining it. */
    DiagnosticsTestResult pendingResult;
    bool scanPending;
    /* Owned by DiagnosticWorker; no health snapshot is published while Zigbee
     * is the sole temporarily failing periodic service. */
    /* Worker writes; the manual self-test API reads from another task. */
    atomic_bool periodicZigbeePending;
    TickType_t periodicZigbeeStarted;
    TickType_t periodicZigbeeLastProbe;
    bool pendingHeapWarning;
    uint32_t pendingFreeHeap;
    int32_t pendingPreviousScore;
    SemaphoreHandle_t mutex;
    TaskHandle_t taskHandle;
    StaticSemaphore_t mutexBuffer;
    DiagnosticsCustomCheckItem customChecks[DIAGNOSTICS_MAX_CUSTOM_CHECKS];
    char serviceNames[DIAGNOSTICS_MAX_CRITICAL_SERVICES][DIAGNOSTICS_NAME_CAPACITY];
    char customNames[DIAGNOSTICS_MAX_CUSTOM_CHECKS][DIAGNOSTICS_NAME_CAPACITY];
    uint32_t customCheckCount;
    bool periodicRunning;
    bool requested;
    bool initialPending;
    IEventBus *eventBus;
    bool subscribed;
    atomic_bool closing;
    atomic_bool stopRequested;
    atomic_bool exited;
    atomic_bool lifecycleBusy;
    atomic_bool emergency;
} DiagnosticsManagerImpl;

static DiagnosticsManagerImpl diagManager;

#define LOGD(FUNC, ...) ESP_LOGD(TAG, "[" FUNC "] " __VA_ARGS__)
#define LOGI(FUNC, ...) ESP_LOGI(TAG, "[" FUNC "] " __VA_ARGS__)
#define LOGW(FUNC, ...) ESP_LOGW(TAG, "[" FUNC "] " __VA_ARGS__)
#define LOGE(FUNC, ...) ESP_LOGE(TAG, "[" FUNC "] " __VA_ARGS__)

/**
 * @brief Publishes an event through EventBus if available.
 *
 * Args:
 *     eventName (const char*): Name of event.
 *     type (EventType): Classification type of event.
 *     data (void*): Pointer to payload.
 *     dataSize (uint32_t): Payload size in bytes.
 */
static void PublishEvent(const char *eventName, EventType type, void *data, uint32_t dataSize) {
    IEventBus *bus = GetEventBus();
    if (bus && bus->Publish) {
        BusEvent event = {
            .eventName = eventName,
            .type = type,
            .data = data,
            .dataSize = dataSize
        };
        if (bus->Publish(event) != 0) ESP_LOGW(TAG, "Event publication failed: %s", eventName);
    }
}

/**
 * @brief Thermal alert callback handler for critical temperature protection.
 *
 * Args:
 *     event (BusEvent): Event payload containing temperature float pointer.
 */
static void OnThermalAlert(BusEvent event) {
    if (event.type != EVENT_TYPE_ERROR || !event.data || event.dataSize != sizeof(float)) return;
    float temperature;
    memcpy(&temperature, event.data, sizeof(temperature));
    if (isfinite(temperature) && temperature > DIAGNOSTICS_EMERGENCY_TEMPERATURE_C) {
        atomic_store(&diagManager.emergency, true);
    }
}

static TickType_t PositiveTicks(uint32_t ms) {
    TickType_t ticks = pdMS_TO_TICKS(ms);
    return ticks ? ticks : 1;
}

static bool Lock(void) {
    return diagManager.mutex && xSemaphoreTake(diagManager.mutex,
        PositiveTicks(DIAGNOSTICS_LOCK_TIMEOUT_MS)) == pdTRUE;
}

static bool ClaimLifecycle(void) {
    bool expected = false;
    return atomic_compare_exchange_strong(&diagManager.lifecycleBusy, &expected, true);
}

/**
 * @brief Evaluates health score based on healthy services vs checked count.
 *
 * Args:
 *     healthy (uint32_t): Healthy services count.
 *     total (uint32_t): Total services checked.
 *
 * Returns:
 *     int32_t: Calculated health score between 0 and 100.
 */
static int32_t CalculateHealthScore(uint32_t healthy, uint32_t total) {
    if (total == 0) {
        return 0;
    }
    return (int32_t)((healthy * 100) / total);
}

/* Retry result publication under the existing worker without rerunning callbacks. */
static bool CommitScan(void) {
    if (!diagManager.scanPending || atomic_load(&diagManager.closing) || !Lock()) return false;
    DiagnosticsTestResult result = diagManager.pendingResult;
    bool heapWarning = diagManager.pendingHeapWarning;
    uint32_t freeHeap = diagManager.pendingFreeHeap;
    int32_t previousScore = diagManager.pendingPreviousScore;
    diagManager.lastResult = result;
    diagManager.scanPending = false;
    xSemaphoreGive(diagManager.mutex);
    if (atomic_load(&diagManager.closing)) return true;
    if (heapWarning) PublishEvent(EVENT_DIAG_HEAP_WARNING, EVENT_TYPE_WARNING, &freeHeap, sizeof(freeHeap));
    int32_t finalScore = result.health_score;
    LOGI("SelfTestTask", "Self-test finished. Health Score: %ld", (long)finalScore);
    PublishEvent(EVENT_DIAG_SELFTEST_FINISHED, EVENT_TYPE_INFO, &finalScore, sizeof(finalScore));
    if (result.services_checked && finalScore != previousScore)
        PublishEvent(EVENT_DIAG_HEALTH_CHANGED, EVENT_TYPE_INFO, &finalScore, sizeof(finalScore));
    if (result.services_checked && finalScore < 50)
        PublishEvent(EVENT_DIAG_HEALTH_CRITICAL, EVENT_TYPE_ERROR, &finalScore, sizeof(finalScore));
    return true;
}

static bool RunScan(bool periodic) {
    if (diagManager.scanPending) return CommitScan();
    if (atomic_load(&diagManager.closing) || !Lock()) return false;
    /* Names remain module-owned until the worker is joined. Registration only
     * appends callbacks; existing entries cannot be replaced during a scan. */
    DiagnosticsManagerConfig config = diagManager.config;
    DiagnosticsCustomCheckItem checks[DIAGNOSTICS_MAX_CUSTOM_CHECKS];
    uint32_t customCount = diagManager.customCheckCount;
    memcpy(checks, diagManager.customChecks, sizeof(checks));
    diagManager.pendingPreviousScore = diagManager.lastResult.health_score;
    diagManager.requested = false;
    diagManager.initialPending = false;
    diagManager.lastResult.is_running = true;
    diagManager.lastResult.completed = false;
    xSemaphoreGive(diagManager.mutex);

    DiagnosticsTestResult result = {0};
    LOGI("SelfTestTask", "Asynchronous self-test started");
    PublishEvent(EVENT_DIAG_SELFTEST_STARTED, EVENT_TYPE_INFO, NULL, 0);
    const char *periodicZigbee = NULL;
    for (uint32_t i = 0; i < config.criticalServiceCount && !atomic_load(&diagManager.closing); ++i) {
        const char *name = config.criticalServices[i];
        if (!name) continue;
        if (periodic && strcmp(name, SVC_NAME_ZIGBEE) == 0) {
            periodicZigbee = name;
            continue; /* Probe once after all other services and custom checks. */
        }
        PublishEvent(EVENT_DIAG_SELFTEST_STEP, EVENT_TYPE_INFO, (void *)name, (uint32_t)(strlen(name) + 1));
        IService *service = ServiceLocator_GetService(name);
        int64_t started = esp_timer_get_time();
        int32_t code = service && service->CheckHealth ? service->CheckHealth(service) : ESP_ERR_INVALID_STATE;
        int64_t elapsed = esp_timer_get_time() - started;
        bool healthy = code == 0 && elapsed <= (int64_t)config.serviceCheckTimeoutMs * 1000;
        ++result.services_checked;
        if (healthy) ++result.services_healthy;
        else {
            snprintf(result.last_failed_service, sizeof(result.last_failed_service), "%s", name);
            LOGE("SelfTestTask", "Service FAILED: %s result=%ld elapsed_us=%lld budget_ms=%lu",
                name, (long)code, (long long)elapsed, (unsigned long)config.serviceCheckTimeoutMs);
        }
        vTaskDelay(PositiveTicks(10));
    }
    for (uint32_t i = 0; i < customCount && !atomic_load(&diagManager.closing); ++i) {
        DiagnosticsCustomCheckItem item = checks[i];
        if (!item.check_fn) continue;
        const char *name = item.name ? item.name : "CustomCheck";
        PublishEvent(EVENT_DIAG_SELFTEST_STEP, EVENT_TYPE_INFO, (void *)name, (uint32_t)(strlen(name) + 1));
        int64_t started = esp_timer_get_time();
        bool healthy = item.check_fn(item.user_data);
        if (esp_timer_get_time() - started > (int64_t)config.serviceCheckTimeoutMs * 1000) healthy = false;
        ++result.services_checked;
        if (healthy) ++result.services_healthy;
        else {
            snprintf(result.last_failed_service, sizeof(result.last_failed_service), "%s", name);
            LOGE("SelfTestTask", "Custom check FAILED: %s", name);
        }
        vTaskDelay(PositiveTicks(10));
    }
    if (atomic_load(&diagManager.closing)) return false;
    uint32_t freeHeap = esp_get_free_heap_size();
    bool heapWarning = freeHeap < config.minFreeHeapThreshold ||
        heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) < config.minFreeBlockThreshold;
    if (heapWarning) LOGW("SelfTestTask", "Free heap below threshold: %lu < %lu bytes",
        (unsigned long)freeHeap, (unsigned long)config.minFreeHeapThreshold);
    if (periodicZigbee && !atomic_load(&diagManager.closing)) {
        PublishEvent(EVENT_DIAG_SELFTEST_STEP, EVENT_TYPE_INFO, (void *)periodicZigbee,
                     (uint32_t)(strlen(periodicZigbee) + 1));
        IService *service = ServiceLocator_GetService(SVC_NAME_ZIGBEE);
        int64_t started = esp_timer_get_time();
        int32_t code = service && service->CheckHealth ?
            service->CheckHealth(service) : ESP_ERR_INVALID_STATE;
        int64_t elapsed = esp_timer_get_time() - started;
        bool healthy = code == 0 && elapsed <= (int64_t)config.serviceCheckTimeoutMs * 1000;
        ++result.services_checked;
        if (healthy) ++result.services_healthy;
        else snprintf(result.last_failed_service, sizeof(result.last_failed_service),
                      "%s", periodicZigbee);
    }
    if (atomic_load(&diagManager.closing)) return false;
    result.completed = true;
    result.last_test_time = (uint32_t)(esp_timer_get_time() / 1000000);
    result.health_score = CalculateHealthScore(result.services_healthy, result.services_checked);
    diagManager.pendingResult = result;
    diagManager.pendingHeapWarning = heapWarning;
    diagManager.pendingFreeHeap = freeHeap;
    diagManager.scanPending = true;
    /* When only Zigbee is temporarily failing, keep the complete result
     * pending; never publish an incomplete/fake 100% health snapshot.
     * Failures in any other service, custom check or heap publish at once. */
    if (periodicZigbee && result.services_checked &&
        result.services_healthy + 1 == result.services_checked && !heapWarning) {
        atomic_store(&diagManager.periodicZigbeePending, true);
        diagManager.periodicZigbeeStarted = xTaskGetTickCount();
        diagManager.periodicZigbeeLastProbe = diagManager.periodicZigbeeStarted;
        return false;
    }
    return CommitScan();
}

static bool CompletePeriodicZigbee(TickType_t now, bool manualRequested) {
    if (!atomic_load(&diagManager.periodicZigbeePending) || !diagManager.scanPending) return false;
    TickType_t elapsed = (TickType_t)(now - diagManager.periodicZigbeeStarted);
    if (!manualRequested && elapsed < PositiveTicks(DIAGNOSTICS_ZIGBEE_RECOVERY_GRACE_MS) &&
        (TickType_t)(now - diagManager.periodicZigbeeLastProbe) <
        PositiveTicks(DIAGNOSTICS_ZIGBEE_PROBE_MS)) return false;
    diagManager.periodicZigbeeLastProbe = now;
    IService *service = ServiceLocator_GetService(SVC_NAME_ZIGBEE);
    int64_t started = esp_timer_get_time();
    int32_t code = service && service->CheckHealth ?
        service->CheckHealth(service) : ESP_ERR_INVALID_STATE;
    int64_t duration = esp_timer_get_time() - started;
    if (code == 0 && duration <= (int64_t)diagManager.config.serviceCheckTimeoutMs * 1000) {
        ++diagManager.pendingResult.services_healthy;
        diagManager.pendingResult.last_failed_service[0] = 0;
    } else if (!manualRequested && elapsed < PositiveTicks(DIAGNOSTICS_ZIGBEE_RECOVERY_GRACE_MS)) {
        return false;
    } else {
        LOGE("SelfTestTask", "Persistent Zigbee health failure after %lu ms",
             (unsigned long)(elapsed * portTICK_PERIOD_MS));
    }
    diagManager.pendingResult.health_score = CalculateHealthScore(
        diagManager.pendingResult.services_healthy, diagManager.pendingResult.services_checked);
    diagManager.pendingResult.last_test_time = (uint32_t)(esp_timer_get_time() / 1000000);
    atomic_store(&diagManager.periodicZigbeePending, false);
    return CommitScan();
}

/* One worker owns scans and the emergency deadline. Stop only deletes it after
 * the worker suspends outside every callback and lock. */
static void DiagnosticWorker(void *unused) {
    (void)unused;
    TickType_t started = xTaskGetTickCount(), lastScan = started, emergencyAt = 0;
    bool emergencyStarted = false;
    while (!atomic_load(&diagManager.stopRequested)) {
        TickType_t now = xTaskGetTickCount();
        if (atomic_load(&diagManager.emergency)) {
            if (!emergencyStarted) { emergencyAt = now; emergencyStarted = true; }
            if ((TickType_t)(now - emergencyAt) >= PositiveTicks(DIAGNOSTICS_EMERGENCY_DELAY_MS)) esp_restart();
        }
        if (atomic_load(&diagManager.closing)) {
            vTaskDelay(PositiveTicks(DIAGNOSTICS_WORKER_POLL_MS));
            continue;
        }
        if (atomic_load(&diagManager.periodicZigbeePending)) {
            bool manualRequested = false;
            if (Lock()) {
                manualRequested = diagManager.requested;
                xSemaphoreGive(diagManager.mutex);
            }
            if (CompletePeriodicZigbee(now, manualRequested)) lastScan = xTaskGetTickCount();
            /* The manual request remains queued and runs in the next poll.
             * This never blocks its admission for the full grace duration. */
            vTaskDelay(PositiveTicks(DIAGNOSTICS_WORKER_POLL_MS));
            continue;
        }
        bool run = false, initial = false, explicitRequest = false, periodicDue = false;
        if (Lock()) {
            initial = diagManager.initialPending;
            explicitRequest = diagManager.requested;
            periodicDue = !initial && diagManager.periodicRunning &&
                (TickType_t)(now - lastScan) >= PositiveTicks(diagManager.config.periodicIntervalMs);
            run = explicitRequest || periodicDue;
            xSemaphoreGive(diagManager.mutex);
        }
        if (initial && !run) {
            bool ready = true;
            for (uint32_t i = 0; i < diagManager.config.criticalServiceCount; ++i) {
                if (strcmp(diagManager.config.criticalServices[i], SVC_NAME_ZIGBEE) != 0) continue;
                IService *service = ServiceLocator_GetService(SVC_NAME_ZIGBEE);
                ready = service && service->CheckHealth && service->CheckHealth(service) == 0;
                break;
            }
            run = ready || (TickType_t)(now - started) >= PositiveTicks(DIAGNOSTICS_STARTUP_READY_TIMEOUT_MS);
        }
        if ((run || diagManager.scanPending) && RunScan(periodicDue && !explicitRequest && !initial))
            lastScan = xTaskGetTickCount();
        vTaskDelay(PositiveTicks(DIAGNOSTICS_WORKER_POLL_MS));
    }
    atomic_store(&diagManager.exited, true);
    vTaskSuspend(NULL);
}

static int32_t Diag_Start(IService *self) {
    if (!self || !ClaimLifecycle()) return DIAG_ERR_BUSY;
    int32_t result = DIAG_ERR_INVALID_STATE;
    if ((diagManager.state != SERVICE_STATE_INITIALIZED && diagManager.state != SERVICE_STATE_STOPPED) ||
        diagManager.taskHandle || diagManager.subscribed) goto done;
    if (!Lock()) { result = DIAG_ERR_BUSY; goto done; }
    atomic_store(&diagManager.closing, false);
    atomic_store(&diagManager.exited, false);
    atomic_store(&diagManager.stopRequested, false);
    diagManager.requested = false;
    diagManager.initialPending = true;
    diagManager.periodicRunning = diagManager.config.enablePeriodicTest;
    diagManager.eventBus = GetEventBus();
    if (diagManager.eventBus) {
        if (!diagManager.eventBus->Subscribe || !diagManager.eventBus->Unsubscribe ||
            diagManager.eventBus->Subscribe(diagManager.eventBus, EVENT_SYSTEM_THERMAL_ALERT, OnThermalAlert) != 0) {
            xSemaphoreGive(diagManager.mutex);
            goto done;
        }
        diagManager.subscribed = true;
    }
    diagManager.state = SERVICE_STATE_STARTED;
    if (xTaskCreate(DiagnosticWorker, "DiagWorker", diagManager.config.taskStackSize, NULL,
        diagManager.config.taskPriority, &diagManager.taskHandle) != pdPASS) {
        atomic_store(&diagManager.closing, true);
        diagManager.state = SERVICE_STATE_INITIALIZED;
        if (diagManager.subscribed && diagManager.eventBus->Unsubscribe(diagManager.eventBus,
            EVENT_SYSTEM_THERMAL_ALERT, OnThermalAlert) == 0) diagManager.subscribed = false;
        result = DIAG_ERR_NO_MEM;
    } else result = DIAG_OK;
    xSemaphoreGive(diagManager.mutex);
done:
    atomic_store(&diagManager.lifecycleBusy, false);
    return result;
}

static int32_t Diag_Stop(IService *self) {
    if (!self || !ClaimLifecycle()) return DIAG_ERR_BUSY;
    int32_t result = DIAG_ERR_BUSY;
    /* Preserve the already requested emergency action. */
    if (atomic_load(&diagManager.emergency)) goto done;
    atomic_store(&diagManager.closing, true);
    if (diagManager.subscribed) {
        if (diagManager.eventBus->Unsubscribe(diagManager.eventBus, EVENT_SYSTEM_THERMAL_ALERT,
            OnThermalAlert) != 0) goto done;
        diagManager.subscribed = false;
    }
    /* All thermal callback references are now released. A late emergency must
     * still be serviced rather than stranded behind closed admission. */
    if (atomic_load(&diagManager.emergency)) goto done;
    atomic_store(&diagManager.stopRequested, true);
    if (diagManager.taskHandle) {
        if (xTaskGetCurrentTaskHandle() == diagManager.taskHandle) goto done;
        TickType_t started = xTaskGetTickCount();
        while (!atomic_load(&diagManager.exited) || eTaskGetState(diagManager.taskHandle) != eSuspended) {
            if ((TickType_t)(xTaskGetTickCount() - started) >= PositiveTicks(DIAGNOSTICS_STOP_TIMEOUT_MS)) goto done;
            vTaskDelay(1);
        }
        vTaskDelete(diagManager.taskHandle);
        diagManager.taskHandle = NULL;
    }
    if (!Lock()) goto done;
    diagManager.periodicRunning = false;
    diagManager.requested = false;
    diagManager.lastResult.is_running = false;
    diagManager.scanPending = false;
    atomic_store(&diagManager.periodicZigbeePending, false);
    diagManager.customCheckCount = 0;
    memset(diagManager.customChecks, 0, sizeof(diagManager.customChecks));
    diagManager.state = SERVICE_STATE_STOPPED;
    xSemaphoreGive(diagManager.mutex);
    result = DIAG_OK;
done:
    atomic_store(&diagManager.lifecycleBusy, false);
    return result;
}

static ServiceState Diag_GetState(IService *self) {
    return self ? atomic_load(&diagManager.state) : SERVICE_STATE_ERROR;
}

static int32_t Diag_CheckHealth(IService *self) {
    return self && atomic_load(&diagManager.state) == SERVICE_STATE_STARTED &&
        !atomic_load(&diagManager.closing) ? DIAG_OK : DIAG_ERR_INVALID_STATE;
}

static void Diag_RunSelfTest(IDiagnosticsManager *self) {
    LOGW("Diag_RunSelfTest", "Synchronous self-test called (Delegating to async test)");
    self->StartAsyncSelfTest(self);
}

static int32_t Diag_StartAsyncSelfTest(IDiagnosticsManager *self) {
    if (!self || !Lock()) return DIAG_ERR_BUSY;
    int32_t result = DIAG_ERR_INVALID_STATE;
    if (diagManager.state == SERVICE_STATE_STARTED && !atomic_load(&diagManager.closing)) {
        if (diagManager.requested || (diagManager.lastResult.is_running && !atomic_load(&diagManager.periodicZigbeePending))) result = DIAG_ERR_BUSY;
        else { diagManager.requested = true; result = DIAG_OK; }
    }
    xSemaphoreGive(diagManager.mutex);
    return result;
}

static DiagnosticsTestResult Diag_GetLastTestResult(IDiagnosticsManager *self) {
    DiagnosticsTestResult result = {0};
    if (!self || !Lock()) return result;
    result = diagManager.lastResult;
    xSemaphoreGive(diagManager.mutex);
    return result;
}

static DiagHealthStatus_t Classify(DiagnosticsTestResult result) {
    if (!result.completed || !result.services_checked) return DIAG_HEALTH_UNKNOWN;
    if (result.health_score < 50) return DIAG_HEALTH_CRITICAL;
    if (result.health_score < 80) return DIAG_HEALTH_WARNING;
    return DIAG_HEALTH_HEALTHY;
}

static int32_t Diag_GetSystemHealthScore(IDiagnosticsManager *self) {
    return Diag_GetLastTestResult(self).health_score;
}

static DiagHealthStatus_t Diag_GetHealthStatus(IDiagnosticsManager *self) {
    return Classify(Diag_GetLastTestResult(self));
}

static DiagnosticsDetailedMetrics Diag_GetDetailedMetrics(IDiagnosticsManager *self) {
    DiagnosticsDetailedMetrics metrics;
    memset(&metrics, 0, sizeof(metrics));

    metrics.free_heap = esp_get_free_heap_size();
    metrics.minimum_free_heap = esp_get_minimum_free_heap_size();
    metrics.largest_free_block = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    metrics.uptime_seconds = (uint32_t)(esp_timer_get_time() / 1000000);
    metrics.task_count = (uint32_t)uxTaskGetNumberOfTasks();
    DiagnosticsTestResult result = Diag_GetLastTestResult(self);
    metrics.health_score = result.health_score;
    metrics.health_status = Classify(result);

    return metrics;
}

static uint32_t Diag_GetFreeHeap(IDiagnosticsManager *self) {
    return esp_get_free_heap_size();
}

static uint32_t Diag_GetSystemUptime(IDiagnosticsManager *self) {
    return (uint32_t)(esp_timer_get_time() / 1000000);
}

static int32_t Diag_RegisterCustomCheck(IDiagnosticsManager *self,
                                         const char *name,
                                         DiagnosticsCustomCheckFn check_fn,
                                         void *user_data) {
    if (!self || !name || !name[0] || strnlen(name, DIAGNOSTICS_NAME_CAPACITY) >= DIAGNOSTICS_NAME_CAPACITY || !check_fn) {
        return DIAG_ERR_INVALID_ARG;
    }

    if (!Lock()) return DIAG_ERR_BUSY;
    if (atomic_load(&diagManager.closing) || diagManager.requested || diagManager.lastResult.is_running) {
        xSemaphoreGive(diagManager.mutex);
        return DIAG_ERR_BUSY;
    }
    for (uint32_t i = 0; i < diagManager.customCheckCount; ++i) {
        if (strcmp(diagManager.customNames[i], name) == 0) {
            bool same = diagManager.customChecks[i].check_fn == check_fn && diagManager.customChecks[i].user_data == user_data;
            xSemaphoreGive(diagManager.mutex);
            return same ? DIAG_OK : DIAG_ERR_BUSY;
        }
    }
    if (diagManager.customCheckCount >= DIAGNOSTICS_MAX_CUSTOM_CHECKS) {
        if (diagManager.mutex) {
            xSemaphoreGive(diagManager.mutex);
        }
        return DIAG_ERR_FULL;
    }

    uint32_t idx = diagManager.customCheckCount;
    snprintf(diagManager.customNames[idx], DIAGNOSTICS_NAME_CAPACITY, "%s", name);
    diagManager.customChecks[idx].name = diagManager.customNames[idx];
    diagManager.customChecks[idx].check_fn = check_fn;
    diagManager.customChecks[idx].user_data = user_data;
    diagManager.customCheckCount++;

    if (diagManager.mutex) {
        xSemaphoreGive(diagManager.mutex);
    }

    LOGI("RegisterCustomCheck", "Registered custom check: %s", name);
    return DIAG_OK;
}

static int32_t Diag_SetPeriodicTest(IDiagnosticsManager *self, bool enable, uint32_t interval_ms) {
    if (!self || (interval_ms && interval_ms < DIAGNOSTICS_MIN_PERIOD_MS) || interval_ms > DIAGNOSTICS_MAX_PERIOD_MS) return DIAG_ERR_INVALID_ARG;
    if (atomic_load(&diagManager.closing)) return DIAG_ERR_INVALID_STATE;
    if (!Lock()) return DIAG_ERR_BUSY;
    if (atomic_load(&diagManager.closing)) { xSemaphoreGive(diagManager.mutex); return DIAG_ERR_INVALID_STATE; }
    diagManager.periodicRunning = enable;
    if (interval_ms > 0) {
        diagManager.config.periodicIntervalMs = interval_ms;
    }
    if (diagManager.mutex) {
        xSemaphoreGive(diagManager.mutex);
    }

    LOGI("SetPeriodicTest", "Periodic test set to %s (interval: %lu ms)",
         enable ? "ENABLED" : "DISABLED",
         (unsigned long)interval_ms);
    return DIAG_OK;
}

static int32_t Diag_InitializeWithConfig(IDiagnosticsManager *self,
                                          DiagnosticsManagerConfig config,
                                          struct IDIRegistry *registry) {
    (void)registry;
    if (!self || config.criticalServiceCount > DIAGNOSTICS_MAX_CRITICAL_SERVICES ||
        config.periodicIntervalMs < DIAGNOSTICS_MIN_PERIOD_MS || config.periodicIntervalMs > DIAGNOSTICS_MAX_PERIOD_MS ||
        !config.serviceCheckTimeoutMs || config.serviceCheckTimeoutMs > DIAGNOSTICS_STOP_TIMEOUT_MS ||
        config.taskStackSize < DIAGNOSTICS_MIN_STACK_BYTES || config.taskStackSize > DIAGNOSTICS_MAX_STACK_BYTES ||
        !config.taskPriority || config.taskPriority >= configMAX_PRIORITIES) return DIAG_ERR_INVALID_ARG;
    for (uint32_t i = 0; i < config.criticalServiceCount; ++i) {
        if (!config.criticalServices[i] || !config.criticalServices[i][0] ||
            strnlen(config.criticalServices[i], DIAGNOSTICS_NAME_CAPACITY) >= DIAGNOSTICS_NAME_CAPACITY) return DIAG_ERR_INVALID_ARG;
    }
    if (!ClaimLifecycle()) return DIAG_ERR_BUSY;
    if (diagManager.taskHandle || diagManager.subscribed) {
        atomic_store(&diagManager.lifecycleBusy, false);
        return DIAG_ERR_BUSY;
    }
    if (!diagManager.mutex) diagManager.mutex = xSemaphoreCreateMutexStatic(&diagManager.mutexBuffer);
    if (!Lock()) { atomic_store(&diagManager.lifecycleBusy, false); return DIAG_ERR_NO_MEM; }
    diagManager.config = config;
    for (uint32_t i = 0; i < config.criticalServiceCount; ++i) {
        snprintf(diagManager.serviceNames[i], DIAGNOSTICS_NAME_CAPACITY, "%s", config.criticalServices[i]);
        diagManager.config.criticalServices[i] = diagManager.serviceNames[i];
    }
    diagManager.state = SERVICE_STATE_INITIALIZED;
    diagManager.customCheckCount = 0;
    diagManager.periodicRunning = config.enablePeriodicTest;
    memset(&diagManager.lastResult, 0, sizeof(DiagnosticsTestResult));
    diagManager.scanPending = false;
    atomic_store(&diagManager.periodicZigbeePending, false);
    atomic_store(&diagManager.closing, false);
    atomic_store(&diagManager.emergency, false);
    if (diagManager.mutex) {
        xSemaphoreGive(diagManager.mutex);
    }

    atomic_store(&diagManager.lifecycleBusy, false);
    return DIAG_OK;
}

static int32_t Diag_Initialize(IService *self, IDIRegistry *registry) {
    if (!self) return DIAG_ERR_INVALID_ARG;
    ServiceState state = atomic_load(&diagManager.state);
    if (state == SERVICE_STATE_INITIALIZED || state == SERVICE_STATE_STARTED) return DIAG_OK;
    return Diag_InitializeWithConfig((IDiagnosticsManager *)self, DiagnosticsManagerConfig_GetDefault(), registry);
}

IDiagnosticsManager *DiagnosticsManager_GetModule(void) {
    static bool vtable_initialized = false;
    if (!vtable_initialized) {
        diagManager.base.base.Initialize = Diag_Initialize;
        diagManager.base.base.Start = Diag_Start;
        diagManager.base.base.Stop = Diag_Stop;
        diagManager.base.base.GetState = Diag_GetState;
        diagManager.base.base.CheckHealth = Diag_CheckHealth;
        
        diagManager.base.InitializeWithConfig = Diag_InitializeWithConfig;
        diagManager.base.RunSelfTest = Diag_RunSelfTest;
        diagManager.base.StartAsyncSelfTest = Diag_StartAsyncSelfTest;
        diagManager.base.GetSystemHealthScore = Diag_GetSystemHealthScore;
        diagManager.base.GetHealthStatus = Diag_GetHealthStatus;
        diagManager.base.GetLastTestResult = Diag_GetLastTestResult;
        diagManager.base.GetDetailedMetrics = Diag_GetDetailedMetrics;
        diagManager.base.GetFreeHeap = Diag_GetFreeHeap;
        diagManager.base.GetSystemUptime = Diag_GetSystemUptime;
        diagManager.base.RegisterCustomCheck = Diag_RegisterCustomCheck;
        diagManager.base.SetPeriodicTest = Diag_SetPeriodicTest;
        
        diagManager.state = SERVICE_STATE_UNINITIALIZED;
        diagManager.mutex = xSemaphoreCreateMutexStatic(&diagManager.mutexBuffer);
        vtable_initialized = true;
    }
    return &diagManager.base;
}

