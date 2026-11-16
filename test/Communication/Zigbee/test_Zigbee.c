#include "test_zigbee_shared.h"
FAKE_VALUE_FUNC1(ezb_err_t, memory_configure_request, const ezb_mem_config_t *);
#define ezb_config_memory memory_configure_request
FAKE_VALUE_FUNC1(ezb_err_t, fan_report_request, const ezb_zcl_report_attr_cmd_t *);
#define ezb_zcl_report_attr_cmd_req fan_report_request
extern IConfigurationManager mock_config_inst;
extern SystemSettings mock_settings;
static esp_err_t nativeRadioEnableResult, nativeRadioDisableResult;
static esp_err_t reporting_result;
static esp_err_t connected_result;
esp_err_t __real_esp_ieee802154_enable(void) { return nativeRadioEnableResult; }
esp_err_t __real_esp_ieee802154_disable(void) { return nativeRadioDisableResult; }
esp_err_t __wrap_esp_ieee802154_enable(void);
esp_err_t __wrap_esp_ieee802154_disable(void);
#include "unity.h"
#include "esp_pm.h"
#include "Hardware/BuiltinLED/BuiltinLEDConfig.h"
#include "freertos/timers.h"
#include <string.h>
#include <stdlib.h>
#include <setjmp.h>
#define configTICK_RATE_HZ 1000
#define ESP_ERR_NOT_FINISHED 0x10C
#define ESP_ERROR_CHECK(expression) do { if ((expression) != ESP_OK) abort(); } while (0)
FAKE_VALUE_FUNC1(const void *, ezb_app_signal_get_params, const ezb_app_signal_t *);

typedef uint32_t StackType_t;
typedef struct { uint32_t storage; } StaticTask_t;
FAKE_VALUE_FUNC7(TaskHandle_t, xTaskCreateStatic, TaskFunction_t, const char *, uint32_t, void *, uint32_t, StackType_t *, StaticTask_t *);
FAKE_VALUE_FUNC1(esp_err_t, nvs_flash_init_partition, const char *);
FAKE_VALUE_FUNC1(esp_err_t, nvs_flash_erase_partition, const char *);
FAKE_VALUE_FUNC2(ezb_err_t, ezb_plat_datasets_delete, uint16_t, int);
FAKE_VALUE_FUNC4(esp_err_t, esp_pm_lock_create, esp_pm_lock_type_t, int, const char *, esp_pm_lock_handle_t *);
FAKE_VALUE_FUNC1(esp_err_t, esp_pm_lock_acquire, esp_pm_lock_handle_t);
FAKE_VALUE_FUNC1(esp_err_t, esp_pm_lock_release, esp_pm_lock_handle_t);
FAKE_VALUE_FUNC1(esp_err_t, esp_pm_lock_delete, esp_pm_lock_handle_t);
FAKE_VALUE_FUNC5(TimerHandle_t, xTimerCreate, const char *, TickType_t, BaseType_t, void *, TimerCallbackFunction_t);
FAKE_VALUE_FUNC2(BaseType_t, xTimerStart, TimerHandle_t, TickType_t);
FAKE_VALUE_FUNC2(BaseType_t, xTimerStop, TimerHandle_t, TickType_t);
FAKE_VALUE_FUNC1(bool, esp_zigbee_lock_acquire, TickType_t);
FAKE_VOID_FUNC0(esp_zigbee_lock_release);
FAKE_VALUE_FUNC1(esp_err_t, esp_zigbee_init, const esp_zigbee_config_t *);
FAKE_VALUE_FUNC0(esp_err_t, esp_zigbee_launch_mainloop);
FAKE_VALUE_FUNC0(esp_err_t, esp_zigbee_stop);
FAKE_VALUE_FUNC2(esp_err_t, esp_zigbee_task_queue_post, esp_zigbee_callback_t, void *);
FAKE_VALUE_FUNC0(esp_err_t, esp_zigbee_deinit);
FAKE_VOID_FUNC1(ezb_af_free_device_desc, ezb_af_device_desc_t);
FAKE_VALUE_FUNC1(esp_err_t, startup_caller_watchdog_status, TaskHandle_t);
#define esp_task_wdt_status startup_caller_watchdog_status
#include "Communication/Zigbee/Zigbee.c"

FAKE_VALUE_FUNC0(esp_err_t, Zigbee_RegisterConsoleCommands);
void Zigbee_ConsoleSdkStopped(void) {}
FAKE_VALUE_FUNC0(int32_t, ClimateZigbeePlugin_Init);
FAKE_VALUE_FUNC0(int32_t, SystemZigbeePlugin_Init);
static IZigbeeEndpoint endpoints[10];
#define ENDPOINT_FACTORY(name, index) IZigbeeEndpoint *name(void) { return &endpoints[index]; }
ENDPOINT_FACTORY(EndpointFan_Create, 0)
ENDPOINT_FACTORY(EndpointAmbientTemp_Create, 1)
ENDPOINT_FACTORY(EndpointCPUTemp_Create, 2)
ENDPOINT_FACTORY(EndpointInternalLED_Create, 3)
ENDPOINT_FACTORY(EndpointRGBLED_Create, 4)
ENDPOINT_FACTORY(EndpointHealth_Create, 5)
ENDPOINT_FACTORY(EndpointMemory_Create, 6)
ENDPOINT_FACTORY(EndpointUptime_Create, 7)
ENDPOINT_FACTORY(EndpointGroups_Create, 8)
ENDPOINT_FACTORY(EndpointOTA_Create, 9)
static IZigbeeSignalHandler handler;
IZigbeeSignalHandler *SignalHandlerCore_Create(void) { return &handler; }

static esp_err_t create_reporting_timer(const esp_timer_create_args_t *args,esp_timer_handle_t *out) {
    (void)args;
    if (mocked_esp_timer_create_fake.return_val == ESP_OK) *out=(esp_timer_handle_t)3;
    return mocked_esp_timer_create_fake.return_val;
}
static TickType_t testTicks;
static bool workerCanExit;
static bool sdkCallbacksAllowed;
static esp_zigbee_callback_t queuedSdkCallback;
static void *queuedSdkContext;
static bool completeStartup;
static esp_err_t startupResult;
static TaskHandle_t create_worker(TaskFunction_t function, const char *name, uint32_t size,
    void *argument, uint32_t priority, StackType_t *stack, StaticTask_t *storage) {
    (void)function; (void)name; (void)size; (void)argument; (void)priority; (void)stack; (void)storage;
    if (xTaskCreateStatic_fake.return_val && completeStartup) {
        atomic_store(&zigbeeService.startupResult, startupResult);
        atomic_store(&zigbeeService.startupComplete, true);
        if (startupResult == ESP_OK) zigbeeService.currentState = SERVICE_STATE_STARTED;
    }
    return xTaskCreateStatic_fake.return_val;
}
static TickType_t current_ticks(void) { return testTicks; }
static bool reportingWorkerCanExit;
static unsigned radio_worker_deletes,report_worker_deletes;
static BaseType_t create_reporting_worker(TaskFunction_t entry,const char *name,uint32_t stack,void *arg,
    uint32_t priority,TaskHandle_t *out) {
    (void)entry;(void)name;(void)stack;(void)arg;
    TEST_ASSERT_LESS_THAN(ZIGBEE_TASK_PRIORITY,priority);
    *out=(TaskHandle_t)5;return pdPASS;
}
static void record_worker_delete(TaskHandle_t task) {
    if(task==(TaskHandle_t)2) ++radio_worker_deletes;
    else if(task==(TaskHandle_t)5) ++report_worker_deletes;
    else TEST_FAIL_MESSAGE("Unexpected task ownership deletion");
}
static void advance_worker(TickType_t ticks) {
    TEST_ASSERT_GREATER_THAN(0, ticks);
    testTicks += ticks;
    if (sdkCallbacksAllowed && queuedSdkCallback) {
        esp_zigbee_callback_t callback = queuedSdkCallback;
        void *context = queuedSdkContext;
        queuedSdkCallback = NULL;
        queuedSdkContext = NULL;
        callback(context);
    }
    if(reportingWorkerCanExit && atomic_load(&zigbeeService.reportingWorkerStop))
        atomic_store(&zigbeeService.reportingWorkerExited,true);
    if (workerCanExit && atomic_load(&zigbeeService.cleanupAllowed)) {
        TEST_ASSERT_TRUE(atomic_load(&zigbeeService.mainloopExited));
        TEST_ASSERT_EQUAL(0, atomic_load(&zigbeeService.sdkReaders));
        atomic_store(&zigbeeService.cleanupResult, ESP_OK);
        atomic_store(&zigbeeService.workerExited, true);
    }
}
static esp_err_t queue_sdk_callback(esp_zigbee_callback_t callback, void *context) {
    TEST_ASSERT_NULL(queuedSdkCallback);
    queuedSdkCallback = callback;
    queuedSdkContext = context;
    return ESP_OK;
}
static esp_err_t stop_worker(void) {
    TEST_ASSERT_FALSE(atomic_load(&zigbeeService.cleanupAllowed));
    atomic_store(&zigbeeService.mainloopExited, true);
    return ESP_OK;
}
static esp_err_t accept_stop_without_exit(void) { return ESP_OK; }
void setUp(void) {
    memset(&factoryResetStatus, 0, sizeof(factoryResetStatus));
    RESET_FAKE(ezb_app_signal_get_params);
    RESET_FAKE(esp_zigbee_task_queue_post);
    esp_zigbee_task_queue_post_fake.custom_fake = queue_sdk_callback;
    queuedSdkCallback = NULL; queuedSdkContext = NULL; sdkCallbacksAllowed = true;
    RESET_FAKE(ezb_plat_datasets_delete);
    reporting_result = ESP_OK;
    nativeRadioEnableResult = nativeRadioDisableResult = ESP_OK;
    ezb_zcl_get_attr_desc_fake.return_val = (ezb_zcl_attr_desc_t)1;
    ezb_zcl_attr_desc_get_type_fake.return_val = EZB_ZCL_ATTR_TYPE_UINT16;
    RESET_FAKE(mocked_esp_timer_create); RESET_FAKE(mocked_esp_timer_start_once); RESET_FAKE(mocked_esp_timer_stop);
    mocked_esp_timer_create_fake.custom_fake=create_reporting_timer;
    ZIGBEE_COMMON_FAKES(RESET_FAKE);
    RESET_FAKE(memory_configure_request);
    RESET_FAKE(fan_report_request);
    connected_result = ESP_OK;
    RESET_FAKE(mocked_xSemaphoreCreateMutex);
    RESET_FAKE(mocked_xSemaphoreTake);
    RESET_FAKE(mocked_xSemaphoreGive);
    RESET_FAKE(xTaskCreateStatic);
    RESET_FAKE(Zigbee_RegisterConsoleCommands);
    RESET_FAKE(mocked_vTaskDelete); RESET_FAKE(mocked_vTaskDelay);
    RESET_FAKE(mocked_xQueueCreate);RESET_FAKE(mocked_xQueueSend);RESET_FAKE(mocked_xQueueReceive);
    RESET_FAKE(mocked_xTaskCreate);
    mocked_xQueueCreate_fake.return_val=(QueueHandle_t)4;
    mocked_xQueueSend_fake.return_val=pdPASS;
    mocked_xTaskCreate_fake.custom_fake=create_reporting_worker;
    mocked_vTaskDelete_fake.custom_fake=record_worker_delete;
    reportingWorkerCanExit=true;radio_worker_deletes=report_worker_deletes=0;
    RESET_FAKE(mocked_eTaskGetState); RESET_FAKE(mocked_xTaskGetTickCount);
    RESET_FAKE(mocked_vTaskSuspend);
    RESET_FAKE(esp_zigbee_init); RESET_FAKE(esp_zigbee_launch_mainloop);
    RESET_FAKE(esp_zigbee_stop); RESET_FAKE(esp_zigbee_deinit);
    RESET_FAKE(ezb_af_free_device_desc); RESET_FAKE(nvs_flash_init_partition);
    RESET_FAKE(nvs_flash_erase_partition); RESET_FAKE(mocked_nvs_flash_erase);
    RESET_FAKE(mocked_esp_restart);
    RESET_FAKE(mocked_xTaskGetCurrentTaskHandle);
    mocked_xTaskGetCurrentTaskHandle_fake.return_val = (TaskHandle_t)7;
    RESET_FAKE(ClimateZigbeePlugin_Init);
    RESET_FAKE(SystemZigbeePlugin_Init);
    RESET_FAKE(xTimerCreate); RESET_FAKE(xTimerStart); RESET_FAKE(xTimerStop);
    RESET_FAKE(esp_zigbee_lock_acquire); RESET_FAKE(esp_zigbee_lock_release);
    RESET_FAKE(startup_caller_watchdog_status);
    startup_caller_watchdog_status_fake.return_val=ESP_ERR_NOT_FOUND;
    RESET_FAKE(mocked_esp_task_wdt_reset);
    RESET_FAKE(esp_pm_lock_create); RESET_FAKE(esp_pm_lock_acquire);
    RESET_FAKE(esp_pm_lock_release); RESET_FAKE(esp_pm_lock_delete);
    xTimerCreate_fake.return_val = (TimerHandle_t)3;
    xTimerStart_fake.return_val = pdPASS; xTimerStop_fake.return_val = pdPASS;
    esp_zigbee_lock_acquire_fake.return_val = true;
    mocked_xSemaphoreCreateMutex_fake.return_val = (SemaphoreHandle_t)1;
    mocked_xSemaphoreTake_fake.return_val = pdTRUE;
    xTaskCreateStatic_fake.return_val = (TaskHandle_t)2;
    xTaskCreateStatic_fake.custom_fake = create_worker;
    testTicks = 0; workerCanExit = true; completeStartup = true; startupResult = ESP_OK;
    mocked_xTaskGetTickCount_fake.custom_fake = current_ticks;
    mocked_vTaskDelay_fake.custom_fake = advance_worker;
    mocked_eTaskGetState_fake.return_val = eSuspended;
    esp_zigbee_stop_fake.custom_fake = stop_worker;
    IZigbee interface = *Zigbee_Create();
    memset(&zigbeeService, 0, sizeof(zigbeeService));
    zigbeeService.base = interface;
    s_steerRetryTimer = NULL;
    memset(endpoints, 0, sizeof(endpoints));
    memset(&handler, 0, sizeof(handler));
}
void tearDown(void) {}

static esp_err_t tx_persist_result;
static unsigned tx_persist_calls;
static esp_err_t persist_tx_profile(IConfigurationManager *self, const char *block,
    const ConfigurationFieldUpdate *updates, size_t count, bool persist) {
    (void)self;
    TEST_ASSERT_EQUAL_STRING("system", block);
    TEST_ASSERT_EQUAL(1, count);
    TEST_ASSERT_TRUE(persist);
    TEST_ASSERT_EQUAL(offsetof(SystemBaseConfigs, zigbee_tx_profile), updates[0].offset);
    TEST_ASSERT_EQUAL(1, updates[0].size);
    TEST_ASSERT_EQUAL(0, atomic_load(&zigbeeService.sdkReaders));
    ++tx_persist_calls;
    if (tx_persist_result == ESP_OK)
        mock_settings.system.zigbee_tx_profile = *(const uint8_t *)updates[0].data;
    return tx_persist_result;
}

void test_Zigbee_TxProfilesReadbackPersistenceAndRollback(void) {
    IZigbee *radio = Zigbee_Create();
    TEST_ASSERT_EQUAL(0, radio->base.Initialize(&radio->base, NULL));
    TEST_ASSERT_EQUAL(0, radio->base.Start(&radio->base));
    mock_config_inst.UpdateFields = persist_tx_profile;
    tx_persist_result = ESP_OK;
    tx_persist_calls = 0;
    const int8_t expected[] = {0, 10, 20};
    for (unsigned mode = 0; mode < 3; ++mode) {
        TEST_ASSERT_EQUAL(ESP_OK, radio->SetTxProfile(radio, (ZigbeeTxProfile)mode));
        ZigbeeTxStatus status;
        TEST_ASSERT_EQUAL(ESP_OK, radio->GetTxStatus(radio, &status));
        TEST_ASSERT_EQUAL(mode, status.profile);
        TEST_ASSERT_EQUAL(expected[mode], status.dbm);
        TEST_ASSERT_EQUAL(mode, mock_settings.system.zigbee_tx_profile);
    }
    tx_persist_result = ESP_FAIL;
    TEST_ASSERT_EQUAL(ESP_FAIL, radio->SetTxProfile(radio, ZIGBEE_TX_ECO));
    ZigbeeTxStatus status;
    TEST_ASSERT_EQUAL(ESP_OK, radio->GetTxStatus(radio, &status));
    TEST_ASSERT_EQUAL(ZIGBEE_TX_BOOST, status.profile);
    TEST_ASSERT_EQUAL(ZIGBEE_TX_BOOST, mock_settings.system.zigbee_tx_profile);
    TEST_ASSERT_EQUAL(4, tx_persist_calls);
    mock_config_inst.UpdateFields = NULL;
}

void test_Zigbee_TxProfilesRejectInvalidOwnerStateAndSdkFailure(void) {
    IZigbee *radio = Zigbee_Create();
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, radio->SetTxProfile(radio, (ZigbeeTxProfile)3));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, radio->GetTxStatus(NULL, NULL));
    TEST_ASSERT_EQUAL(0, radio->base.Initialize(&radio->base, NULL));
    TEST_ASSERT_EQUAL(0, radio->base.Start(&radio->base));
    mock_config_inst.UpdateFields = persist_tx_profile;
    tx_persist_result = ESP_OK;
    tx_persist_calls = 0;
    mocked_xTaskGetCurrentTaskHandle_fake.return_val = zigbeeService.workerTask;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, radio->SetTxProfile(radio, ZIGBEE_TX_ECO));
    mocked_xTaskGetCurrentTaskHandle_fake.return_val = (TaskHandle_t)7;
    esp_zigbee_lock_acquire_fake.return_val = false;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, radio->SetTxProfile(radio, ZIGBEE_TX_ECO));
    TEST_ASSERT_EQUAL(0, tx_persist_calls);
    ZigbeeTxStatus before = {.profile=ZIGBEE_TX_BOOST,.dbm=20}, status=before;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, radio->GetTxStatus(radio, &status));
    TEST_ASSERT_EQUAL_MEMORY(&before, &status, sizeof(before));
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_SUPPORTED, radio->SetPowerMode(radio, ZIGBEE_POWER_MODE_LIGHT_SLEEP));
    mock_config_inst.UpdateFields = NULL;
}

static void ignore_tx_write(int8_t value) { (void)value; }
static void unknown_tx_read(int8_t *value) { *value = 7; }
void test_Zigbee_TxProfilesRejectSdkMismatchBeforePersistence(void) {
    IZigbee *radio = Zigbee_Create();
    TEST_ASSERT_EQUAL(0, radio->base.Initialize(&radio->base, NULL));
    TEST_ASSERT_EQUAL(0, radio->base.Start(&radio->base));
    mock_config_inst.UpdateFields = persist_tx_profile;
    tx_persist_calls = 0; tx_persist_result = ESP_OK;
    mocked_ezb_set_tx_power_fake.custom_fake = ignore_tx_write;
    TEST_ASSERT_EQUAL(ESP_FAIL, radio->SetTxProfile(radio, ZIGBEE_TX_BOOST));
    TEST_ASSERT_EQUAL(0, tx_persist_calls);
    ZigbeeTxStatus status = {.profile=ZIGBEE_TX_NORMAL,.dbm=10};
    TEST_ASSERT_EQUAL(ESP_OK, radio->GetTxStatus(radio,&status));
    TEST_ASSERT_EQUAL(10, status.dbm);
    mocked_ezb_get_tx_power_fake.custom_fake = unknown_tx_read;
    ZigbeeTxStatus before = status;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, radio->GetTxStatus(radio,&status));
    TEST_ASSERT_EQUAL_MEMORY(&before,&status,sizeof(before));
    mock_config_inst.UpdateFields = NULL;
}
void test_Zigbee_Lifecycle(void) {
    IService *service = &Zigbee_Create()->base;
    TEST_ASSERT_EQUAL(0, service->Initialize(service, NULL));
    TEST_ASSERT_EQUAL(SERVICE_STATE_INITIALIZED, service->GetState(service));
    TEST_ASSERT_EQUAL(10, zigbeeService.endpointCount);
    TEST_ASSERT_EQUAL(0, service->Start(service));
    TEST_ASSERT_EQUAL(SERVICE_STATE_STARTED, service->GetState(service));
    TEST_ASSERT_EQUAL(1, xTaskCreateStatic_fake.call_count);
    TEST_ASSERT_EQUAL_STRING("Zigbee_main", xTaskCreateStatic_fake.arg1_val);
    TEST_ASSERT_EQUAL(ZIGBEE_TASK_PRIORITY, xTaskCreateStatic_fake.arg4_val);
    TEST_ASSERT_TRUE(xTaskCreateStatic_fake.arg4_val <= BUILTIN_LED_TASK_PRIORITY);
    TEST_ASSERT_EQUAL(0, service->Stop(service));
    TEST_ASSERT_EQUAL(SERVICE_STATE_STOPPED, service->GetState(service));
}

void test_Zigbee_RepeatedInitializeAndRegistrationDoNotCycle(void) {
    IZigbee *zb = Zigbee_Create();
    TEST_ASSERT_EQUAL(0, zb->base.Initialize(&zb->base, NULL));
    TEST_ASSERT_EQUAL(0, zb->base.Initialize(&zb->base, NULL));
    TEST_ASSERT_EQUAL(0, zb->RegisterEndpoint(zb, &endpoints[4]));
    TEST_ASSERT_EQUAL(0, zb->RegisterSignalHandler(zb, &handler));
    unsigned count = 0;
    for (IZigbeeEndpoint *ep = zigbeeService.endpointList; ep; ep = ep->next) {
        TEST_ASSERT_LESS_THAN(10, count);
        ++count;
    }
    TEST_ASSERT_EQUAL(10, count);
    TEST_ASSERT_NULL(handler.next);
}
void test_Zigbee_MutexFailureLeavesServiceUninitialized(void) {
    IService *service = &Zigbee_Create()->base;
    mocked_xSemaphoreCreateMutex_fake.return_val = NULL;
    TEST_ASSERT_NOT_EQUAL(0, service->Initialize(service, NULL));
    TEST_ASSERT_EQUAL(SERVICE_STATE_UNINITIALIZED, service->GetState(service));
    TEST_ASSERT_EQUAL(0, zigbeeService.endpointCount);
}
void test_Zigbee_TaskFailureCanBeRetried(void) {
    IService *service = &Zigbee_Create()->base;
    TEST_ASSERT_EQUAL(0, service->Initialize(service, NULL));
    xTaskCreateStatic_fake.return_val = NULL;
    TEST_ASSERT_NOT_EQUAL(0, service->Start(service));
    TEST_ASSERT_EQUAL(SERVICE_STATE_INITIALIZED, service->GetState(service));
    xTaskCreateStatic_fake.return_val = (TaskHandle_t)2;
    TEST_ASSERT_EQUAL(0, service->Start(service));
}
void test_Zigbee_LockFailureDoesNotReportSuccessfulInitialization(void) {
    IService *service = &Zigbee_Create()->base;
    mocked_xSemaphoreTake_fake.return_val = pdFALSE;
    TEST_ASSERT_NOT_EQUAL(0, service->Initialize(service, NULL));
    TEST_ASSERT_EQUAL(SERVICE_STATE_UNINITIALIZED, service->GetState(service));
    mocked_xSemaphoreTake_fake.return_val = pdTRUE;
    TEST_ASSERT_EQUAL(0, service->Initialize(service, NULL));
    TEST_ASSERT_EQUAL(10, zigbeeService.endpointCount);
}

static esp_err_t action_result;
static bool handle_action_result(IZigbeeEndpoint *self, ezb_zcl_core_action_callback_id_t id, const void *message, esp_err_t *result) {
    (void)self; (void)id; (void)message; *result=action_result; return true;
}
void test_Zigbee_AttributeActionStatusIsReturnedToStack(void) {
    zigbeeService.mutex=(SemaphoreHandle_t)1;
    endpoints[0].HandleAction=handle_action_result;
    zigbeeService.endpointList=&endpoints[0];
    ezb_zcl_set_attr_value_message_t message={0};
    action_result=ESP_ERR_INVALID_ARG;
    zb_action_handler(EZB_ZCL_CORE_SET_ATTR_VALUE_CB_ID,&message);
    TEST_ASSERT_EQUAL(EZB_ZCL_STATUS_INVALID_VALUE,message.out.result);
    action_result=ESP_FAIL;
    zb_action_handler(EZB_ZCL_CORE_SET_ATTR_VALUE_CB_ID,&message);
    TEST_ASSERT_EQUAL(EZB_ZCL_STATUS_FAIL,message.out.result);
    action_result=ESP_OK;
    zb_action_handler(EZB_ZCL_CORE_SET_ATTR_VALUE_CB_ID,&message);
    TEST_ASSERT_EQUAL(EZB_ZCL_STATUS_SUCCESS,message.out.result);
}
void test_Zigbee_AttributeLockFailureCannotReportSuccess(void) {
    mocked_xSemaphoreTake_fake.return_val=pdFALSE;
    ezb_zcl_set_attr_value_message_t message={0};
    zb_action_handler(EZB_ZCL_CORE_SET_ATTR_VALUE_CB_ID,&message);
    TEST_ASSERT_EQUAL(EZB_ZCL_STATUS_FAIL,message.out.result);
    zb_action_handler(EZB_ZCL_CORE_SET_ATTR_VALUE_CB_ID,NULL);
}
static unsigned ota_route_calls;
static bool handle_ota_action(IZigbeeEndpoint *self, ezb_zcl_core_action_callback_id_t id,
                              const void *message, esp_err_t *result) {
    (void)self;
    (void)result;
    ++ota_route_calls;
    if (id == EZB_ZCL_CORE_OTA_UPGRADE_CLIENT_PROGRESS_CB_ID) {
        ezb_zcl_ota_upgrade_client_progress_message_t *msg =
            (ezb_zcl_ota_upgrade_client_progress_message_t *)message;
        if (msg->in.progress == EZB_ZCL_OTA_UPGRADE_PROGRESS_ABORT) {
            /* A cancelled transfer may be acknowledged, never completed. */
            msg->out.result = EZB_ZCL_STATUS_SUCCESS;
            return true;
        }
    }
    return false;
}

void test_Zigbee_OtaProgressDefaultsToAbortWithoutHandler(void) {
    zigbeeService.mutex = (SemaphoreHandle_t)1;
    ezb_zcl_ota_upgrade_client_progress_message_t msg = {
        .in = {.progress = EZB_ZCL_OTA_UPGRADE_PROGRESS_RECEIVING},
        .out = {.result = EZB_ZCL_STATUS_SUCCESS}
    };
    zb_action_handler(EZB_ZCL_CORE_OTA_UPGRADE_CLIENT_PROGRESS_CB_ID, &msg);
    TEST_ASSERT_EQUAL(EZB_ZCL_STATUS_ABORT, msg.out.result);
    TEST_ASSERT_EQUAL(1, mocked_xSemaphoreTake_fake.call_count);
    TEST_ASSERT_EQUAL(1, mocked_xSemaphoreGive_fake.call_count);
}

void test_Zigbee_OtaProgressRejectsWhenServiceLockBusy(void) {
    zigbeeService.mutex = (SemaphoreHandle_t)1;
    endpoints[0].HandleAction = handle_ota_action;
    zigbeeService.endpointList = &endpoints[0];
    ota_route_calls = 0;
    mocked_xSemaphoreTake_fake.return_val = pdFALSE;
    ezb_zcl_ota_upgrade_client_progress_message_t msg = {
        .in = {.progress = EZB_ZCL_OTA_UPGRADE_PROGRESS_ABORT},
        .out = {.result = EZB_ZCL_STATUS_SUCCESS}
    };
    zb_action_handler(EZB_ZCL_CORE_OTA_UPGRADE_CLIENT_PROGRESS_CB_ID, &msg);
    TEST_ASSERT_EQUAL(EZB_ZCL_STATUS_ABORT, msg.out.result);
    TEST_ASSERT_EQUAL(0, ota_route_calls);
    TEST_ASSERT_EQUAL(0, mocked_xSemaphoreGive_fake.call_count);
}

void test_Zigbee_OtaProgressRejectsDuringServiceStop(void) {
    zigbeeService.mutex = (SemaphoreHandle_t)1;
    endpoints[0].HandleAction = handle_ota_action;
    zigbeeService.endpointList = &endpoints[0];
    ota_route_calls = 0;
    atomic_store(&zigbeeService.closing, true);
    ezb_zcl_ota_upgrade_client_progress_message_t msg = {
        .in = {.progress = EZB_ZCL_OTA_UPGRADE_PROGRESS_ABORT},
        .out = {.result = EZB_ZCL_STATUS_SUCCESS}
    };
    zb_action_handler(EZB_ZCL_CORE_OTA_UPGRADE_CLIENT_PROGRESS_CB_ID, &msg);
    TEST_ASSERT_EQUAL(EZB_ZCL_STATUS_ABORT, msg.out.result);
    TEST_ASSERT_EQUAL(0, ota_route_calls);
    TEST_ASSERT_EQUAL(0, mocked_xSemaphoreTake_fake.call_count);
}

void test_Zigbee_OtaExplicitCancelOnlyCanOverrideAbort(void) {
    zigbeeService.mutex = (SemaphoreHandle_t)1;
    endpoints[0].HandleAction = handle_ota_action;
    zigbeeService.endpointList = &endpoints[0];
    ota_route_calls = 0;
    ezb_zcl_ota_upgrade_client_progress_message_t msg = {
        .in = {.progress = EZB_ZCL_OTA_UPGRADE_PROGRESS_RECEIVING},
        .out = {.result = EZB_ZCL_STATUS_SUCCESS}
    };
    zb_action_handler(EZB_ZCL_CORE_OTA_UPGRADE_CLIENT_PROGRESS_CB_ID, &msg);
    TEST_ASSERT_EQUAL(EZB_ZCL_STATUS_ABORT, msg.out.result);
    TEST_ASSERT_EQUAL(1, ota_route_calls);
    msg.in.progress = EZB_ZCL_OTA_UPGRADE_PROGRESS_ABORT;
    zb_action_handler(EZB_ZCL_CORE_OTA_UPGRADE_CLIENT_PROGRESS_CB_ID, &msg);
    TEST_ASSERT_EQUAL(EZB_ZCL_STATUS_SUCCESS, msg.out.result);
    TEST_ASSERT_EQUAL(2, ota_route_calls);
}

void test_Zigbee_OtaQueryResponseDefaultsToAbort(void) {
    zigbeeService.mutex = (SemaphoreHandle_t)1;
    ezb_zcl_ota_upgrade_query_next_image_rsp_message_t msg = {
        .out = {.result = EZB_ZCL_STATUS_SUCCESS}
    };
    zb_action_handler(EZB_ZCL_CORE_OTA_UPGRADE_QUERY_NEXT_IMAGE_RSP_CB_ID, &msg);
    TEST_ASSERT_EQUAL(EZB_ZCL_STATUS_ABORT, msg.out.result);
    mocked_xSemaphoreTake_fake.return_val = pdFALSE;
    msg.out.result = EZB_ZCL_STATUS_SUCCESS;
    zb_action_handler(EZB_ZCL_CORE_OTA_UPGRADE_QUERY_NEXT_IMAGE_RSP_CB_ID, &msg);
    TEST_ASSERT_EQUAL(EZB_ZCL_STATUS_ABORT, msg.out.result);
    atomic_store(&zigbeeService.closing, true);
    mocked_xSemaphoreTake_fake.return_val = pdTRUE;
    msg.out.result = EZB_ZCL_STATUS_SUCCESS;
    zb_action_handler(EZB_ZCL_CORE_OTA_UPGRADE_QUERY_NEXT_IMAGE_RSP_CB_ID, &msg);
    TEST_ASSERT_EQUAL(EZB_ZCL_STATUS_ABORT, msg.out.result);
}

static unsigned reporting_calls, connected_calls;
static esp_err_t record_reporting(IZigbeeEndpoint *self) { (void)self; ++reporting_calls; return reporting_result; }
static esp_err_t record_connected(IZigbeeEndpoint *self) { (void)self; ++connected_calls; return connected_result; }
static IZigbee *reporting_failure_fixture(void) {
    IZigbee *zb = Zigbee_Create();
    TEST_ASSERT_EQUAL(0, zb->base.Initialize(&zb->base, NULL));
    zigbeeService.currentState = SERVICE_STATE_STARTED;
    atomic_store(&zigbeeService.isConnected, true);
    reporting_calls = 0;
    endpoints[0].ConfigureReporting = record_reporting;
    return zb;
}
void test_Zigbee_ReportingTransientFailureRetriesOnNextTimerTurn(void) {
    IZigbee *zb = reporting_failure_fixture();
    reporting_result = ESP_FAIL;
    TEST_ASSERT_EQUAL(0, zb->ReconfigureAllReporting(zb));
    ProcessReportingWork();
    TEST_ASSERT_EQUAL(1, reporting_calls);
    TEST_ASSERT_TRUE(atomic_load(&zigbeeService.reportingPending));
    TEST_ASSERT_TRUE(atomic_load(&zigbeeService.reportingApplyFailed));
    reporting_result = ESP_OK;
    ProcessReportingWork();
    TEST_ASSERT_EQUAL(2, reporting_calls);
    TEST_ASSERT_FALSE(atomic_load(&zigbeeService.reportingPending));
    TEST_ASSERT_FALSE(atomic_load(&zigbeeService.reportingApplyFailed));
}
void test_Zigbee_ReportingExhaustionAndNewRequestHaveSeparateBudgets(void) {
    IZigbee *zb = reporting_failure_fixture();
    reporting_result = ESP_FAIL;
    TEST_ASSERT_EQUAL(0, zb->ReconfigureAllReporting(zb));
    for (unsigned i = 0; i < ZIGBEE_REPORT_MAX_ATTEMPTS + 2; ++i) ProcessReportingWork();
    TEST_ASSERT_EQUAL(ZIGBEE_REPORT_MAX_ATTEMPTS, reporting_calls);
    TEST_ASSERT_FALSE(atomic_load(&zigbeeService.reportingPending));
    TEST_ASSERT_TRUE(atomic_load(&zigbeeService.reportingApplyFailed));
    TEST_ASSERT_EQUAL(0, zb->ReconfigureAllReporting(zb));
    ProcessReportingWork();
    TEST_ASSERT_TRUE(atomic_load(&zigbeeService.reportingPending));
    TEST_ASSERT_EQUAL(1, zigbeeService.reportingAttempts);
}
void test_Zigbee_ReportingMissingDescriptorIsNotRetried(void) {
    IZigbee *zb = reporting_failure_fixture();
    reporting_result = ESP_ERR_NOT_FOUND;
    TEST_ASSERT_EQUAL(0, zb->ReconfigureAllReporting(zb));
    ProcessReportingWork();
    ProcessReportingWork();
    TEST_ASSERT_EQUAL(1, reporting_calls);
    TEST_ASSERT_FALSE(atomic_load(&zigbeeService.reportingPending));
    TEST_ASSERT_TRUE(atomic_load(&zigbeeService.reportingApplyFailed));
}
void test_Zigbee_ReportingRequestDefersSdkAndDoesNotReplayConnection(void) {
    IZigbee *zb = Zigbee_Create();
    TEST_ASSERT_EQUAL(0, zb->base.Initialize(&zb->base, NULL));
    zigbeeService.currentState = SERVICE_STATE_STARTED;
    atomic_store(&zigbeeService.isConnected, true);
    reporting_calls = 0; connected_calls = 0;
    endpoints[0].ConfigureReporting = record_reporting;
    endpoints[0].OnConnected = record_connected;
    zb->ReconfigureAllReporting(zb);
    TEST_ASSERT_EQUAL(0, esp_zigbee_lock_acquire_fake.call_count);
    TEST_ASSERT_EQUAL(0, reporting_calls);
    ProcessReportingWork();
    TEST_ASSERT_EQUAL(1, reporting_calls);
    TEST_ASSERT_EQUAL(0, connected_calls);
    TEST_ASSERT_EQUAL(1, esp_zigbee_lock_release_fake.call_count);
    TEST_ASSERT_FALSE(atomic_load(&zigbeeService.reportingPending));
}
void test_Zigbee_ReportingContentionRetriesWithoutBlocking(void) {
    zigbeeService.currentState = SERVICE_STATE_STARTED;
    zigbeeService.reportingTimer=(esp_timer_handle_t)3;
    atomic_store(&zigbeeService.isConnected, true);
    atomic_store(&zigbeeService.reportingPending, true);
    esp_zigbee_lock_acquire_fake.return_val = false;
    ProcessReportingWork();
    TEST_ASSERT_EQUAL(0, esp_zigbee_lock_acquire_fake.arg0_val);
    TEST_ASSERT_EQUAL(1, mocked_esp_timer_start_once_fake.call_count);
    TEST_ASSERT_TRUE(atomic_load(&zigbeeService.reportingPending));
    TEST_ASSERT_EQUAL(0, esp_zigbee_lock_release_fake.call_count);
}
void test_Zigbee_ReportingTimerFailureCanBeRetried(void) {
    IZigbee *zb = Zigbee_Create();
    mocked_esp_timer_create_fake.return_val = ESP_FAIL;
    TEST_ASSERT_NOT_EQUAL(0, zb->base.Initialize(&zb->base, NULL));
    TEST_ASSERT_EQUAL(SERVICE_STATE_UNINITIALIZED, zigbeeService.currentState);
    mocked_esp_timer_create_fake.return_val = ESP_OK;
    TEST_ASSERT_EQUAL(0, zb->base.Initialize(&zb->base, NULL));
}
void test_Zigbee_StoppedReportingTimerDoesNotTouchSdk(void) {
    atomic_store(&zigbeeService.reportingPending, true);
    zigbeeService.currentState = SERVICE_STATE_STOPPED;
    ProcessReportingWork();
    TEST_ASSERT_EQUAL(0, esp_zigbee_lock_acquire_fake.call_count);
}
void test_Zigbee_AttributeNotificationRequiresSdkLock(void) {
    IZigbee *zb = Zigbee_Create();
    zigbeeService.currentState = SERVICE_STATE_STARTED;
    atomic_store(&zigbeeService.isConnected, true);
    uint16_t value = 1;
    esp_zigbee_lock_acquire_fake.return_val = false;
    TEST_ASSERT_EQUAL(ZIGBEE_ERROR_LOCK_FAIL, zb->NotifyAttributeChanged(zb, 1, 0x0204, 0x8030, &value, sizeof(value)));
    TEST_ASSERT_EQUAL(0, mocked_ezb_zcl_set_attr_value_fake.call_count);
    esp_zigbee_lock_acquire_fake.return_val = true;
    mocked_ezb_zcl_set_attr_value_fake.return_val = EZB_ZCL_STATUS_FAIL;
    TEST_ASSERT_EQUAL(ZIGBEE_ERROR_ATTRIBUTE_FAIL, zb->NotifyAttributeChanged(zb, 1, 0x0204, 0x8030, &value, sizeof(value)));
    TEST_ASSERT_EQUAL(1, esp_zigbee_lock_release_fake.call_count);
    TEST_ASSERT_EQUAL(ZIGBEE_ERROR_INVALID_PARAM, zb->NotifyAttributeChanged(zb, 1, 0x0204, 0x8030, NULL, 2));
}

void test_Zigbee_BootHealthRequiresLiveSdkMainloopNotNetworkJoin(void) {
    IZigbee *radio = Zigbee_Create();
    IService foreign = {0};
    TEST_ASSERT_FALSE(Zigbee_IsBootReady(NULL));
    TEST_ASSERT_FALSE(Zigbee_IsBootReady(&foreign));
    TEST_ASSERT_EQUAL(0, radio->base.Initialize(&radio->base, NULL));
    TEST_ASSERT_FALSE(Zigbee_IsBootReady(&radio->base));
    TEST_ASSERT_EQUAL(0, radio->base.Start(&radio->base));
    TEST_ASSERT_EQUAL(SERVICE_STATE_STARTED, radio->base.GetState(&radio->base));
    /* The Start mock publishes STARTED without entering the real SDK loop. */
    TEST_ASSERT_FALSE(Zigbee_IsBootReady(&radio->base));
    atomic_store(&zigbeeService.mainloopRunning, true);
    TEST_ASSERT_FALSE(GetConnected());
    TEST_ASSERT_TRUE(Zigbee_IsBootReady(&radio->base));
    atomic_store(&zigbeeService.mainloopExited, true);
    TEST_ASSERT_FALSE(Zigbee_IsBootReady(&radio->base));
    atomic_store(&zigbeeService.mainloopExited, false);
    atomic_store(&zigbeeService.closing, true);
    TEST_ASSERT_FALSE(Zigbee_IsBootReady(&radio->base));
    atomic_store(&zigbeeService.closing, false);
    atomic_store(&zigbeeService.workerExited, true);
    TEST_ASSERT_FALSE(Zigbee_IsBootReady(&radio->base));
    atomic_store(&zigbeeService.workerExited, false);
    atomic_store(&zigbeeService.mainloopRunning, false);
    TEST_ASSERT_FALSE(Zigbee_IsBootReady(&radio->base));
    atomic_store(&zigbeeService.mainloopRunning, true);
    atomic_store(&zigbeeService.startupResult, ESP_FAIL);
    TEST_ASSERT_FALSE(Zigbee_IsBootReady(&radio->base));
}
void test_Zigbee_ReportingDoesNotUseFullFreeRTOSTimerQueue(void) {
    IZigbee *zb=Zigbee_Create(); TEST_ASSERT_EQUAL(0,zb->base.Initialize(&zb->base,NULL));
    zigbeeService.currentState=SERVICE_STATE_STARTED;
    atomic_store(&zigbeeService.isConnected,true);
    xTimerStart_fake.return_val=pdFAIL;
    TEST_ASSERT_EQUAL(0,zb->ReconfigureAllReporting(zb));
    TEST_ASSERT_EQUAL(0,xTimerStart_fake.call_count);
    TEST_ASSERT_EQUAL(1,mocked_esp_timer_start_once_fake.call_count);
    TEST_ASSERT_TRUE(atomic_load(&zigbeeService.reportingPending));
}

void test_Zigbee_ReportingSchedulingFailureAndRecovery(void) {
    IZigbee *zb=Zigbee_Create(); TEST_ASSERT_EQUAL(0,zb->base.Initialize(&zb->base,NULL));
    TEST_ASSERT_EQUAL(0, zb->base.Start(&zb->base));
    atomic_store(&zigbeeService.isConnected,true);
    mocked_esp_timer_start_once_fake.return_val=ESP_FAIL;
    TEST_ASSERT_NOT_EQUAL(0,zb->ReconfigureAllReporting(zb));
    TEST_ASSERT_TRUE(atomic_load(&zigbeeService.reportingPending));
    TEST_ASSERT_NOT_EQUAL(0,zb->base.CheckHealth(&zb->base));
    mocked_esp_timer_start_once_fake.return_val=ESP_OK;
    TEST_ASSERT_EQUAL(0,zb->ReconfigureAllReporting(zb));
    TEST_ASSERT_EQUAL(0,zb->base.CheckHealth(&zb->base));
    reporting_calls=0; endpoints[0].ConfigureReporting=record_reporting;
    ProcessReportingWork();
    TEST_ASSERT_EQUAL(1,reporting_calls);
    TEST_ASSERT_FALSE(atomic_load(&zigbeeService.reportingPending));
    mocked_esp_timer_start_once_fake.return_val=ESP_ERR_INVALID_STATE;
    TEST_ASSERT_EQUAL(0,zb->ReconfigureAllReporting(zb));
    TEST_ASSERT_FALSE(atomic_load(&zigbeeService.reportingScheduleFailed));
}

void test_Zigbee_FailedPluginBindingsRetryWithoutAnotherRadioTask(void) {
    IZigbee *radio = Zigbee_Create();
    TEST_ASSERT_EQUAL(0, radio->base.Initialize(&radio->base, NULL));
    SystemZigbeePlugin_Init_fake.return_val = ESP_FAIL;
    TEST_ASSERT_EQUAL(ZIGBEE_ERROR_STACK_FAIL, radio->base.Start(&radio->base));
    TEST_ASSERT_EQUAL_UINT(1, xTaskCreateStatic_fake.call_count);
    TEST_ASSERT_FALSE(atomic_load(&zigbeeService.pluginsReady));
    atomic_store(&zigbeeService.isConnected, true);
    TEST_ASSERT_NOT_EQUAL(0, radio->base.CheckHealth(&radio->base));
    SystemZigbeePlugin_Init_fake.return_val = ESP_OK;
    TEST_ASSERT_EQUAL(ZIGBEE_ERROR_OK, radio->base.Start(&radio->base));
    TEST_ASSERT_EQUAL_UINT(1, xTaskCreateStatic_fake.call_count);
    TEST_ASSERT_TRUE(atomic_load(&zigbeeService.pluginsReady));
    TEST_ASSERT_EQUAL(0, radio->base.CheckHealth(&radio->base));
}

void test_Zigbee_LifecycleLockFailureLeavesRadioAndStateUntouched(void) {
    IZigbee *radio = Zigbee_Create();
    TEST_ASSERT_EQUAL(0, radio->base.Initialize(&radio->base, NULL));
    mocked_xSemaphoreTake_fake.return_val = pdFALSE;
    TEST_ASSERT_EQUAL(ZIGBEE_ERROR_LOCK_FAIL, radio->base.Start(&radio->base));
    TEST_ASSERT_EQUAL(SERVICE_STATE_INITIALIZED, radio->base.GetState(&radio->base));
    TEST_ASSERT_EQUAL_UINT(0, xTaskCreateStatic_fake.call_count);
    TEST_ASSERT_EQUAL(ZIGBEE_ERROR_LOCK_FAIL, radio->base.Stop(&radio->base));
    TEST_ASSERT_EQUAL(SERVICE_STATE_INITIALIZED, radio->base.GetState(&radio->base));
    mocked_xSemaphoreTake_fake.return_val = pdTRUE;
}

void test_Zigbee_ReportingStopFailureCanOnlyBeRetriedSafely(void) {
    IZigbee *zb=Zigbee_Create(); TEST_ASSERT_EQUAL(0,zb->base.Initialize(&zb->base,NULL));
    TEST_ASSERT_EQUAL(0,zb->base.Start(&zb->base));
    mocked_esp_timer_stop_fake.return_val=ESP_FAIL;
    TEST_ASSERT_NOT_EQUAL(0,zb->base.Stop(&zb->base));
    TEST_ASSERT_EQUAL(SERVICE_STATE_ERROR,zigbeeService.currentState);
    TEST_ASSERT_NOT_EQUAL(0,zb->base.Initialize(&zb->base,NULL));
    atomic_store(&zigbeeService.reportingPending,true);
    unsigned callsBeforeCallback = esp_zigbee_lock_acquire_fake.call_count;
    ProcessReportingWork();
    TEST_ASSERT_EQUAL(callsBeforeCallback,esp_zigbee_lock_acquire_fake.call_count);
    mocked_esp_timer_stop_fake.return_val=ESP_ERR_INVALID_STATE;
    TEST_ASSERT_EQUAL(0,zb->base.Stop(&zb->base));
    TEST_ASSERT_EQUAL(SERVICE_STATE_STOPPED,zigbeeService.currentState);
}

void test_Zigbee_ReportingServiceLockContentionReleasesSdk(void) {
    IZigbee *zb=Zigbee_Create(); TEST_ASSERT_EQUAL(0,zb->base.Initialize(&zb->base,NULL));
    zigbeeService.currentState=SERVICE_STATE_STARTED;
    atomic_store(&zigbeeService.isConnected,true);
    atomic_store(&zigbeeService.reportingPending,true);
    mocked_xSemaphoreTake_fake.return_val=pdFALSE;
    ProcessReportingWork();
    TEST_ASSERT_EQUAL(0,mocked_xSemaphoreTake_fake.arg1_val);
    TEST_ASSERT_EQUAL(1,esp_zigbee_lock_release_fake.call_count);
    TEST_ASSERT_EQUAL(1,mocked_esp_timer_start_once_fake.call_count);
    TEST_ASSERT_TRUE(atomic_load(&zigbeeService.reportingPending));
}

static uint16_t queued_values[128];
void test_ConnectedCallbackFailureRetainsWorkUntilCheckedSuccess(void) {
    IZigbee *radio = reporting_failure_fixture();
    endpoints[0].OnConnected = record_connected;
    connected_calls = 0;
    connected_result = ESP_ERR_TIMEOUT;
    NotifyEndpointsConnected(radio);
    ProcessReportingWork();
    TEST_ASSERT_TRUE(atomic_load(&zigbeeService.connectionNotificationPending));
    TEST_ASSERT_EQUAL(1, connected_calls);
    TEST_ASSERT_NOT_EQUAL(0, radio->base.CheckHealth(&radio->base));
    connected_result = ESP_OK;
    ProcessReportingWork();
    TEST_ASSERT_FALSE(atomic_load(&zigbeeService.connectionNotificationPending));
    TEST_ASSERT_EQUAL(2, connected_calls);
}
static unsigned queued_calls;
static bool replace_during_apply;
static ezb_zcl_status_t record_queued_attribute(uint8_t endpoint, uint16_t cluster, uint8_t role,
        uint16_t attribute, uint16_t manufacturer, void *value, bool check) {
    (void)role; (void)manufacturer; (void)check;
    TEST_ASSERT_TRUE(queued_calls < 128);
    memcpy(&queued_values[queued_calls++], value, sizeof(uint16_t));
    if (replace_during_apply) {
        replace_during_apply = false;
        uint16_t newer = 222;
        TEST_ASSERT_EQUAL(0, Zigbee_Create()->QueueAttributeUpdate(Zigbee_Create(), endpoint, cluster, attribute, &newer, sizeof(newer)));
    }
    return EZB_ZCL_STATUS_SUCCESS;
}
static IZigbee *prepare_attribute_queue(void) {
    IZigbee *radio = Zigbee_Create();
    TEST_ASSERT_EQUAL(0, radio->base.Initialize(&radio->base, NULL));
    TEST_ASSERT_EQUAL(0, radio->base.Start(&radio->base));
    atomic_store(&zigbeeService.isConnected, true);
    queued_calls = 0; replace_during_apply = false;
    mocked_ezb_zcl_set_attr_value_fake.custom_fake = record_queued_attribute;
    return radio;
}

void test_Zigbee_QueuedAttributeCopiesAndSurvivesSdkContention(void) {
    IZigbee *radio = prepare_attribute_queue();
    uint16_t value = 123;
    TEST_ASSERT_EQUAL(0, radio->QueueAttributeUpdate(radio, 5, 0x402, 0, &value, sizeof(value)));
    value = 999;
    TEST_ASSERT_EQUAL(0, esp_zigbee_lock_acquire_fake.call_count);
    TEST_ASSERT_EQUAL(0, queued_calls);
    esp_zigbee_lock_acquire_fake.return_val = false;
    ProcessReportingWork();
    ZigbeeAttributeStatus status;
    TEST_ASSERT_EQUAL(0, radio->GetAttributeStatus(radio, &status));
    TEST_ASSERT_EQUAL(1, status.pending); TEST_ASSERT_EQUAL(0, status.applied);
    TEST_ASSERT_EQUAL(1, status.lock_deferrals);
    TEST_ASSERT_EQUAL(0, esp_zigbee_lock_acquire_fake.arg0_val);
    esp_zigbee_lock_acquire_fake.return_val = true;
    ProcessReportingWork();
    TEST_ASSERT_EQUAL(123, queued_values[0]);
    radio->GetAttributeStatus(radio, &status);
    TEST_ASSERT_EQUAL(0, status.pending); TEST_ASSERT_EQUAL(1, status.applied);
    TEST_ASSERT_EQUAL(0, status.failed);
}

void test_Zigbee_QueueCoalescesAndBoundsEachDrain(void) {
    IZigbee *radio = prepare_attribute_queue();
    for (uint16_t n = 0; n < ZIGBEE_ATTRIBUTE_QUEUE_CAPACITY; ++n)
        TEST_ASSERT_EQUAL(0, radio->QueueAttributeUpdate(radio, 1, 0x402, n, &n, sizeof(n)));
    uint16_t value = 999;
    TEST_ASSERT_EQUAL(ZIGBEE_ERROR_QUEUE_FULL, radio->QueueAttributeUpdate(radio, 1, 0x402, 999, &value, sizeof(value)));
    TEST_ASSERT_EQUAL(0, radio->QueueAttributeUpdate(radio, 1, 0x402, 1, &value, sizeof(value)));
    ProcessReportingWork();
    TEST_ASSERT_EQUAL(ZIGBEE_ATTRIBUTE_BATCH_SIZE, queued_calls);
    TEST_ASSERT_EQUAL(999, queued_values[1]);
    for (unsigned n = ZIGBEE_ATTRIBUTE_BATCH_SIZE; n < ZIGBEE_ATTRIBUTE_QUEUE_CAPACITY; n += ZIGBEE_ATTRIBUTE_BATCH_SIZE)
        ProcessReportingWork();
    TEST_ASSERT_EQUAL(ZIGBEE_ATTRIBUTE_QUEUE_CAPACITY, queued_calls);
    TEST_ASSERT_FALSE(AttributesPending());
}

void test_Zigbee_QueueReplacementDuringApplicationIsNotLost(void) {
    IZigbee *radio = prepare_attribute_queue();
    uint16_t value = 111;
    TEST_ASSERT_EQUAL(0, radio->QueueAttributeUpdate(radio, 1, 0x402, 0, &value, sizeof(value)));
    replace_during_apply = true;
    ProcessReportingWork();
    TEST_ASSERT_EQUAL(2, queued_calls);
    TEST_ASSERT_EQUAL(111, queued_values[0]); TEST_ASSERT_EQUAL(222, queued_values[1]);
    TEST_ASSERT_FALSE(AttributesPending());
}

void test_Zigbee_QueueSchedulingFailureRetainsLatestUpdateForRetry(void) {
    IZigbee *radio = prepare_attribute_queue();
    uint16_t value = 1;
    mocked_esp_timer_start_once_fake.return_val = ESP_FAIL;
    TEST_ASSERT_EQUAL(ZIGBEE_ERROR_STACK_FAIL, radio->QueueAttributeUpdate(radio, 1, 0x402, 0, &value, sizeof(value)));
    TEST_ASSERT_TRUE(AttributesPending());
    TEST_ASSERT_NOT_EQUAL(0, radio->base.CheckHealth(&radio->base));
    mocked_esp_timer_start_once_fake.return_val = ESP_OK;
    value = 2;
    TEST_ASSERT_EQUAL(0, radio->QueueAttributeUpdate(radio, 1, 0x402, 0, &value, sizeof(value)));
    ProcessReportingWork();
    TEST_ASSERT_EQUAL(1, queued_calls); TEST_ASSERT_EQUAL(2, queued_values[0]);
}

void test_Zigbee_QueueClosingPreservesOwnershipUntilSdkStopRetry(void) {
    IZigbee *radio = prepare_attribute_queue();
    uint16_t value = 1;
    TEST_ASSERT_EQUAL(0, radio->QueueAttributeUpdate(radio, 1, 0x402, 0, &value, sizeof(value)));
    esp_zigbee_lock_acquire_fake.return_val = false;
    TEST_ASSERT_EQUAL(ZIGBEE_ERROR_LOCK_FAIL, radio->base.Stop(&radio->base));
    TEST_ASSERT_TRUE(AttributesPending());
    TEST_ASSERT_EQUAL(SERVICE_STATE_STARTED, zigbeeService.currentState);
    TEST_ASSERT_NOT_EQUAL(0, radio->base.Start(&radio->base));
    TEST_ASSERT_NOT_EQUAL(0, radio->QueueAttributeUpdate(radio, 1, 0x402, 0, &value, sizeof(value)));
    ProcessReportingWork(); TEST_ASSERT_EQUAL(0, queued_calls);
    esp_zigbee_lock_acquire_fake.return_val = true;
    TEST_ASSERT_EQUAL(0, radio->base.Stop(&radio->base));
    TEST_ASSERT_FALSE(AttributesPending());
    ProcessReportingWork(); TEST_ASSERT_EQUAL(0, queued_calls);
}

void test_Zigbee_QueueSdkFailureIsReportedSeparatelyFromAdmission(void) {
    IZigbee *radio = prepare_attribute_queue();
    mocked_ezb_zcl_set_attr_value_fake.custom_fake = NULL;
    mocked_ezb_zcl_set_attr_value_fake.return_val = EZB_ZCL_STATUS_FAIL;
    uint16_t value = 1;
    TEST_ASSERT_EQUAL(0, radio->QueueAttributeUpdate(radio, 1, 0x402, 0, &value, sizeof(value)));
    ProcessReportingWork();
    ZigbeeAttributeStatus status;
    radio->GetAttributeStatus(radio, &status);
    TEST_ASSERT_EQUAL(0, status.applied); TEST_ASSERT_EQUAL(1, status.failed);
    TEST_ASSERT_EQUAL(1, status.pending);
    TEST_ASSERT_EQUAL(1, status.retries);
    TEST_ASSERT_EQUAL(0, status.dropped);
    for (unsigned attempt = 1; attempt < ZIGBEE_ATTRIBUTE_MAX_ATTEMPTS; ++attempt) {
        mocked_esp_timer_get_time_fake.return_val += (int64_t)ZIGBEE_ATTRIBUTE_RETRY_MAX_MS * 1000;
        ProcessReportingWork();
    }
    radio->GetAttributeStatus(radio, &status);
    TEST_ASSERT_EQUAL(ZIGBEE_ATTRIBUTE_MAX_ATTEMPTS, status.failed);
    TEST_ASSERT_EQUAL(ZIGBEE_ATTRIBUTE_MAX_ATTEMPTS - 1, status.retries);
    TEST_ASSERT_EQUAL(0, status.pending);
    TEST_ASSERT_EQUAL(1, status.dropped);
    TEST_ASSERT_NOT_EQUAL(0, radio->QueueAttributeUpdate(radio, 0, 0x402, 0, &value, sizeof(value)));
    TEST_ASSERT_NOT_EQUAL(0, radio->QueueAttributeUpdate(radio, 1, 0x402, 0, NULL, sizeof(value)));
    TEST_ASSERT_NOT_EQUAL(0, radio->QueueAttributeUpdate(radio, 1, 0x402, 0, &value, ZIGBEE_ATTRIBUTE_MAX_BYTES + 1));
}

void test_Zigbee_QueuedAttributesResumeAfterReconnect(void) {
    IZigbee *radio = prepare_attribute_queue();
    uint16_t value = 1;
    TEST_ASSERT_EQUAL(0, radio->QueueAttributeUpdate(radio, 1, 0x402, 0, &value, sizeof(value)));
    SetConnected(false);
    ProcessReportingWork(); TEST_ASSERT_EQUAL(0, queued_calls);
    unsigned scheduled = mocked_esp_timer_start_once_fake.call_count;
    SetConnected(true);
    TEST_ASSERT_EQUAL(scheduled + 1, mocked_esp_timer_start_once_fake.call_count);
    ProcessReportingWork(); TEST_ASSERT_EQUAL(1, queued_calls);
}

void test_Zigbee_TransientAttributeFailureRecoversOnNextTimerTurn(void) {
    IZigbee *radio = prepare_attribute_queue();
    uint16_t value = 77;
    mocked_ezb_zcl_set_attr_value_fake.custom_fake = NULL;
    mocked_ezb_zcl_set_attr_value_fake.return_val = EZB_ZCL_STATUS_TIMEOUT;
    TEST_ASSERT_EQUAL(0, radio->QueueAttributeUpdate(radio, 1, 0x402, 0, &value, sizeof(value)));
    ProcessReportingWork();
    TEST_ASSERT_EQUAL(1, mocked_ezb_zcl_set_attr_value_fake.call_count);
    TEST_ASSERT_TRUE(AttributesPending());
    mocked_ezb_zcl_set_attr_value_fake.custom_fake = record_queued_attribute;
    mocked_esp_timer_get_time_fake.return_val += (int64_t)ZIGBEE_ATTRIBUTE_RETRY_BASE_MS * 1000;
    ProcessReportingWork();
    ZigbeeAttributeStatus status;
    radio->GetAttributeStatus(radio, &status);
    TEST_ASSERT_EQUAL(1, queued_calls);
    TEST_ASSERT_EQUAL(value, queued_values[0]);
    TEST_ASSERT_EQUAL(1, status.failed);
    TEST_ASSERT_EQUAL(1, status.applied);
    TEST_ASSERT_EQUAL(1, status.retries);
    TEST_ASSERT_EQUAL(0, status.dropped);
    TEST_ASSERT_EQUAL(0, status.pending);
}

void test_Zigbee_PermanentAttributeFailureDoesNotBlockFollowingKey(void) {
    IZigbee *radio = prepare_attribute_queue();
    uint16_t value = 77;
    mocked_ezb_zcl_set_attr_value_fake.custom_fake = NULL;
    mocked_ezb_zcl_set_attr_value_fake.return_val = EZB_ZCL_STATUS_INVALID_VALUE;
    TEST_ASSERT_EQUAL(0, radio->QueueAttributeUpdate(radio, 1, 0x402, 0, &value, sizeof(value)));
    TEST_ASSERT_EQUAL(0, radio->QueueAttributeUpdate(radio, 1, 0x402, 1, &value, sizeof(value)));
    ProcessReportingWork();
    ZigbeeAttributeStatus status;
    radio->GetAttributeStatus(radio, &status);
    TEST_ASSERT_EQUAL(2, mocked_ezb_zcl_set_attr_value_fake.call_count);
    TEST_ASSERT_EQUAL(2, status.failed);
    TEST_ASSERT_EQUAL(2, status.dropped);
    TEST_ASSERT_EQUAL(0, status.retries);
    TEST_ASSERT_EQUAL(0, status.pending);
}

void test_Zigbee_NewAttributeRevisionResetsRetryBudget(void) {
    IZigbee *radio = prepare_attribute_queue();
    uint16_t value = 11;
    mocked_ezb_zcl_set_attr_value_fake.custom_fake = NULL;
    mocked_ezb_zcl_set_attr_value_fake.return_val = EZB_ZCL_STATUS_FAIL;
    TEST_ASSERT_EQUAL(0, radio->QueueAttributeUpdate(radio, 1, 0x402, 0, &value, sizeof(value)));
    for (unsigned i = 1; i < ZIGBEE_ATTRIBUTE_MAX_ATTEMPTS; ++i) {
        mocked_esp_timer_get_time_fake.return_val += (int64_t)ZIGBEE_ATTRIBUTE_RETRY_MAX_MS * 1000;
        ProcessReportingWork();
    }
    value = 22;
    TEST_ASSERT_EQUAL(0, radio->QueueAttributeUpdate(radio, 1, 0x402, 0, &value, sizeof(value)));
    ProcessReportingWork();
    TEST_ASSERT_TRUE(AttributesPending());
    mocked_ezb_zcl_set_attr_value_fake.custom_fake = record_queued_attribute;
    mocked_esp_timer_get_time_fake.return_val += (int64_t)ZIGBEE_ATTRIBUTE_RETRY_BASE_MS * 1000;
    ProcessReportingWork();
    TEST_ASSERT_EQUAL(value, queued_values[0]);
    ZigbeeAttributeStatus status;
    radio->GetAttributeStatus(radio, &status);
    TEST_ASSERT_EQUAL(0, status.dropped);
    TEST_ASSERT_EQUAL(1, status.applied);
}

void test_Zigbee_AttributeTypeAndWidthAreCheckedBeforeSdkReadsPayload(void) {
    IZigbee *radio = prepare_attribute_queue();
    uint16_t value = 1;
    ezb_zcl_attr_desc_get_type_fake.return_val = EZB_ZCL_ATTR_TYPE_STRING;
    TEST_ASSERT_EQUAL(0, radio->QueueAttributeUpdate(radio, 1, 0x402, 0, &value, sizeof(value)));
    ProcessReportingWork();
    TEST_ASSERT_EQUAL(0, queued_calls);
    ezb_zcl_attr_desc_get_type_fake.return_val = EZB_ZCL_ATTR_TYPE_SINGLE;
    TEST_ASSERT_EQUAL(ZIGBEE_ERROR_ATTRIBUTE_FAIL, radio->NotifyAttributeChanged(radio,1,0x402,0,&value,sizeof(value)));
    TEST_ASSERT_EQUAL(0, queued_calls);
    ezb_zcl_get_attr_desc_fake.return_val = NULL;
    TEST_ASSERT_EQUAL(ZIGBEE_ERROR_ATTRIBUTE_FAIL, radio->NotifyAttributeChanged(radio,1,0x402,0,&value,sizeof(value)));
    TEST_ASSERT_EQUAL(0, queued_calls);
}

static uint8_t configuration_previous;
static ezb_err_t read_configuration_previous(ezb_zcl_attr_desc_t descriptor, void *value) {
    (void)descriptor;
    *(uint8_t *)value = configuration_previous;
    return EZB_ERR_NONE;
}
static bool repeat_source_during_apply;
static ezb_zcl_status_t apply_source_status(uint8_t endpoint, uint16_t cluster,
    uint8_t role, uint16_t attribute, uint16_t manufacturer, void *value, bool check) {
    (void)endpoint; (void)cluster; (void)role; (void)attribute; (void)manufacturer; (void)check;
    configuration_previous = *(uint8_t *)value;
    if (repeat_source_during_apply) {
        repeat_source_during_apply = false;
        TEST_ASSERT_EQUAL(0, zigbeeService.base.QueueAttributeUpdate(&zigbeeService.base,
            endpoint, cluster, attribute, value, 1));
    }
    return EZB_ZCL_STATUS_SUCCESS;
}

void test_Zigbee_RepeatedAmbientStatusRetainsRejectedReportRetry(void) {
    IZigbee *radio = prepare_attribute_queue();
    mocked_ezb_zcl_attr_desc_get_type_fake.return_val = EZB_ZCL_ATTR_TYPE_UINT8;
    mocked_ezb_zcl_attr_desc_get_value_fake.custom_fake = read_configuration_previous;
    mocked_ezb_zcl_set_attr_value_fake.custom_fake = apply_source_status;
    configuration_previous = 0;
    uint8_t value = 1;
    fan_report_request_fake.return_val = EZB_ERR_FAIL;
    TEST_ASSERT_EQUAL(0, radio->QueueAttributeUpdate(radio, 1, 0x204, FAN_ATTR_AMBIENT_STATUS, &value, 1));
    ProcessReportingWork();
    TEST_ASSERT_EQUAL(1, fan_report_request_fake.call_count);
    TEST_ASSERT_EQUAL(1, configuration_previous);
    TEST_ASSERT_EQUAL(0, radio->QueueAttributeUpdate(radio, 1, 0x204, FAN_ATTR_AMBIENT_STATUS, &value, 1));
    mocked_esp_timer_get_time_fake.return_val += (int64_t)ZIGBEE_ATTRIBUTE_RETRY_BASE_MS * 1000;
    fan_report_request_fake.return_val = EZB_ERR_NONE;
    ProcessReportingWork();
    TEST_ASSERT_EQUAL(2, fan_report_request_fake.call_count);
    TEST_ASSERT_EQUAL(0, zigbeeService.attributeCount);
    TEST_ASSERT_EQUAL(1, zigbeeService.attributeApplied);
    TEST_ASSERT_EQUAL(0, zigbeeService.attributeDropped);
}

void test_Zigbee_ChangedStatusReturningToLocalValueRetainsReportDebt(void) {
    IZigbee *radio = prepare_attribute_queue();
    mocked_ezb_zcl_attr_desc_get_type_fake.return_val = EZB_ZCL_ATTR_TYPE_UINT8;
    mocked_ezb_zcl_attr_desc_get_value_fake.custom_fake = read_configuration_previous;
    mocked_ezb_zcl_set_attr_value_fake.custom_fake = apply_source_status;
    configuration_previous = 0;
    uint8_t value = 1;
    fan_report_request_fake.return_val = EZB_ERR_NO_MEM;
    TEST_ASSERT_EQUAL(0, radio->QueueAttributeUpdate(radio, 1, 0x204, FAN_ATTR_AMBIENT_STATUS, &value, 1));
    ProcessReportingWork();
    value = 2;
    TEST_ASSERT_EQUAL(0, radio->QueueAttributeUpdate(radio, 1, 0x204, FAN_ATTR_AMBIENT_STATUS, &value, 1));
    value = 1;
    TEST_ASSERT_EQUAL(0, radio->QueueAttributeUpdate(radio, 1, 0x204, FAN_ATTR_AMBIENT_STATUS, &value, 1));
    fan_report_request_fake.return_val = EZB_ERR_NONE;
    mocked_esp_timer_get_time_fake.return_val += (int64_t)ZIGBEE_ATTRIBUTE_RETRY_MAX_MS * 1000;
    ProcessReportingWork();
    TEST_ASSERT_EQUAL(2, fan_report_request_fake.call_count);
    TEST_ASSERT_EQUAL(0, zigbeeService.attributeCount);
}

static ezb_err_t replace_status_during_report(const ezb_zcl_report_attr_cmd_t *request) {
    (void)request;
    uint8_t value = 2;
    TEST_ASSERT_EQUAL(0, zigbeeService.base.QueueAttributeUpdate(&zigbeeService.base, 1, 0x204,
        FAN_ATTR_AMBIENT_STATUS, &value, 1));
    value = 1;
    TEST_ASSERT_EQUAL(0, zigbeeService.base.QueueAttributeUpdate(&zigbeeService.base, 1, 0x204,
        FAN_ATTR_AMBIENT_STATUS, &value, 1));
    return EZB_ERR_NO_MEM;
}

void test_Zigbee_ReplacedInFlightStatusRetainsRejectedReportDebt(void) {
    IZigbee *radio = prepare_attribute_queue();
    mocked_ezb_zcl_attr_desc_get_type_fake.return_val = EZB_ZCL_ATTR_TYPE_UINT8;
    mocked_ezb_zcl_attr_desc_get_value_fake.custom_fake = read_configuration_previous;
    mocked_ezb_zcl_set_attr_value_fake.custom_fake = apply_source_status;
    configuration_previous = 0;
    uint8_t value = 1;
    fan_report_request_fake.custom_fake = replace_status_during_report;
    TEST_ASSERT_EQUAL(0, radio->QueueAttributeUpdate(radio, 1, 0x204, FAN_ATTR_AMBIENT_STATUS, &value, 1));
    ProcessReportingWork();
    fan_report_request_fake.custom_fake = NULL;
    fan_report_request_fake.return_val = EZB_ERR_NONE;
    mocked_esp_timer_get_time_fake.return_val += (int64_t)ZIGBEE_ATTRIBUTE_RETRY_MAX_MS * 1000;
    ProcessReportingWork();
    TEST_ASSERT_EQUAL(2, fan_report_request_fake.call_count);
    TEST_ASSERT_EQUAL(0, zigbeeService.attributeCount);
}

void test_Zigbee_IdenticalStatusDuringSdkApplicationKeepsRetryAndBackoff(void) {
    IZigbee *radio = prepare_attribute_queue();
    mocked_ezb_zcl_attr_desc_get_type_fake.return_val = EZB_ZCL_ATTR_TYPE_UINT8;
    mocked_ezb_zcl_attr_desc_get_value_fake.custom_fake = read_configuration_previous;
    mocked_ezb_zcl_set_attr_value_fake.custom_fake = apply_source_status;
    configuration_previous = 0;
    repeat_source_during_apply = true;
    fan_report_request_fake.return_val = EZB_ERR_FAIL;
    uint8_t value = 1;
    TEST_ASSERT_EQUAL(0, radio->QueueAttributeUpdate(radio, 1, 0x204, FAN_ATTR_AMBIENT_STATUS, &value, 1));
    ProcessReportingWork();
    TEST_ASSERT_FALSE(repeat_source_during_apply);
    TEST_ASSERT_EQUAL(1, zigbeeService.attributeRetries);
    TEST_ASSERT_EQUAL(1, zigbeeService.attributes[zigbeeService.attributeHead].attempts);
    ProcessReportingWork();
    TEST_ASSERT_EQUAL(1, fan_report_request_fake.call_count);
    mocked_esp_timer_get_time_fake.return_val += (int64_t)ZIGBEE_ATTRIBUTE_RETRY_BASE_MS * 1000;
    fan_report_request_fake.return_val = EZB_ERR_NONE;
    ProcessReportingWork();
    TEST_ASSERT_EQUAL(2, fan_report_request_fake.call_count);
    TEST_ASSERT_EQUAL(0, zigbeeService.attributeCount);
    TEST_ASSERT_EQUAL(0, zigbeeService.attributeDropped);
}

void test_Zigbee_AmbientReportBackpressureSurvivesAttemptBudgetAndReconnect(void) {
    IZigbee *radio = prepare_attribute_queue();
    mocked_ezb_zcl_attr_desc_get_type_fake.return_val = EZB_ZCL_ATTR_TYPE_UINT8;
    mocked_ezb_zcl_attr_desc_get_value_fake.custom_fake = read_configuration_previous;
    mocked_ezb_zcl_set_attr_value_fake.custom_fake = apply_source_status;
    configuration_previous = 0;
    uint8_t value = 1;
    fan_report_request_fake.return_val = EZB_ERR_FAIL;
    TEST_ASSERT_EQUAL(0, radio->QueueAttributeUpdate(radio, 1, 0x204, FAN_ATTR_AMBIENT_STATUS, &value, 1));
    for (unsigned i = 0; i < 6; ++i) {
        ProcessReportingWork();
        TEST_ASSERT_EQUAL(1, zigbeeService.attributeCount);
        TEST_ASSERT_EQUAL(0, zigbeeService.attributeDropped);
        TEST_ASSERT_EQUAL(0, zigbeeService.attributeApplied);
        mocked_esp_timer_get_time_fake.return_val += (int64_t)ZIGBEE_ATTRIBUTE_RETRY_MAX_MS * 1000;
    }
    mocked_ezb_zcl_set_attr_value_fake.custom_fake = NULL;
    mocked_ezb_zcl_set_attr_value_fake.return_val = EZB_ZCL_STATUS_TIMEOUT;
    ProcessReportingWork();
    TEST_ASSERT_EQUAL(1, zigbeeService.attributeCount);
    TEST_ASSERT_EQUAL(0, zigbeeService.attributeDropped);
    mocked_esp_timer_get_time_fake.return_val += (int64_t)ZIGBEE_ATTRIBUTE_RETRY_MAX_MS * 1000;
    mocked_ezb_zcl_set_attr_value_fake.custom_fake = apply_source_status;
    mocked_ezb_zcl_set_attr_value_fake.return_val = EZB_ZCL_STATUS_SUCCESS;
    SetConnected(false); ProcessReportingWork();
    TEST_ASSERT_EQUAL(6, fan_report_request_fake.call_count);
    SetConnected(true);
    fan_report_request_fake.return_val = EZB_ERR_NONE;
    ProcessReportingWork();
    TEST_ASSERT_EQUAL(7, fan_report_request_fake.call_count);
    TEST_ASSERT_EQUAL(0, zigbeeService.attributeCount);
    TEST_ASSERT_EQUAL(1, zigbeeService.attributeApplied);
}

void test_Zigbee_ReportBackoffSaturatesAcrossCounterAndClockWrap(void) {
    IZigbee *radio = prepare_attribute_queue();
    mocked_ezb_zcl_attr_desc_get_type_fake.return_val = EZB_ZCL_ATTR_TYPE_UINT8;
    mocked_ezb_zcl_attr_desc_get_value_fake.custom_fake = read_configuration_previous;
    mocked_ezb_zcl_set_attr_value_fake.custom_fake = apply_source_status;
    configuration_previous = 0;
    mocked_esp_timer_get_time_fake.return_val = ((int64_t)UINT32_MAX - 50) * 1000;
    uint8_t value = 1;
    fan_report_request_fake.return_val = EZB_ERR_NO_MEM;
    TEST_ASSERT_EQUAL(0, radio->QueueAttributeUpdate(radio, 1, 0x204, FAN_ATTR_AMBIENT_STATUS, &value, 1));
    for (unsigned i = 0; i < 300; ++i) {
        ProcessReportingWork();
        TEST_ASSERT_EQUAL(i + 1, fan_report_request_fake.call_count);
        ProcessReportingWork();
        TEST_ASSERT_EQUAL(i + 1, fan_report_request_fake.call_count);
        TEST_ASSERT_EQUAL(1, zigbeeService.attributeCount);
        TEST_ASSERT_EQUAL(0, zigbeeService.attributeDropped);
        mocked_esp_timer_get_time_fake.return_val += (int64_t)ZIGBEE_ATTRIBUTE_RETRY_MAX_MS * 1000;
    }
    TEST_ASSERT_EQUAL(UINT8_MAX, zigbeeService.attributes[zigbeeService.attributeHead].attempts);
    fan_report_request_fake.return_val = EZB_ERR_NONE;
    ProcessReportingWork();
    TEST_ASSERT_EQUAL(301, fan_report_request_fake.call_count);
    TEST_ASSERT_EQUAL(0, zigbeeService.attributeCount);
}

void test_Zigbee_PermanentReportContractFailureDoesNotRetainQueueHead(void) {
    IZigbee *radio = prepare_attribute_queue();
    mocked_ezb_zcl_attr_desc_get_type_fake.return_val = EZB_ZCL_ATTR_TYPE_UINT8;
    mocked_ezb_zcl_attr_desc_get_value_fake.custom_fake = read_configuration_previous;
    mocked_ezb_zcl_set_attr_value_fake.custom_fake = apply_source_status;
    configuration_previous = 0;
    uint8_t value = 1;
    fan_report_request_fake.return_val = EZB_ERR_NOT_SUPPORTED;
    TEST_ASSERT_EQUAL(0, radio->QueueAttributeUpdate(radio, 1, 0x204, FAN_ATTR_AMBIENT_STATUS, &value, 1));
    ProcessReportingWork();
    TEST_ASSERT_EQUAL(1, fan_report_request_fake.call_count);
    TEST_ASSERT_EQUAL(0, zigbeeService.attributeCount);
    TEST_ASSERT_EQUAL(1, zigbeeService.attributeDropped);
    TEST_ASSERT_EQUAL(0, zigbeeService.attributeRetries);
}

void test_Zigbee_ConfigurationReportsChangedValuesAndRetriesWithoutTelemetryFlood(void) {
    ezb_zcl_attr_desc_get_type_fake.return_val = EZB_ZCL_ATTR_TYPE_UINT8;
    mocked_ezb_zcl_attr_desc_get_value_fake.custom_fake = read_configuration_previous;
    mocked_ezb_zcl_set_attr_value_fake.custom_fake = NULL;
    mocked_ezb_zcl_set_attr_value_fake.return_val = EZB_ZCL_STATUS_SUCCESS;
    configuration_previous = 255;
    uint8_t value = 207;
    bool reported;
    TEST_ASSERT_EQUAL(EZB_ZCL_STATUS_SUCCESS, SetNumericAttribute(2, 6, 0x8004, &value, 1, false, &reported));
    TEST_ASSERT_TRUE(reported);
    configuration_previous = value;
    TEST_ASSERT_EQUAL(EZB_ZCL_STATUS_SUCCESS, SetNumericAttribute(2, 6, 0x8004, &value, 1, false, &reported));
    TEST_ASSERT_FALSE(reported);
    TEST_ASSERT_EQUAL(1, fan_report_request_fake.call_count);
    fan_report_request_fake.return_val = EZB_ERR_NO_MEM;
    TEST_ASSERT_EQUAL(EZB_ZCL_STATUS_INSUFFICIENT_SPACE, SetNumericAttribute(2, 6, 0x8004, &value, 1, true, &reported));
    TEST_ASSERT_TRUE(reported);
    fan_report_request_fake.return_val = EZB_ERR_NONE;
    TEST_ASSERT_EQUAL(EZB_ZCL_STATUS_SUCCESS, SetNumericAttribute(2, 6, 0x8004, &value, 1, true, &reported));
    TEST_ASSERT_TRUE(reported);
    TEST_ASSERT_EQUAL(3, fan_report_request_fake.call_count);
    TEST_ASSERT_FALSE(ZigbeeAttribute_IsConfiguration(1, 0x402, 0));
    TEST_ASSERT_FALSE(ZigbeeAttribute_IsConfiguration(1, 0x204, FAN_ATTR_EFFECTIVE_OBJECT));
    TEST_ASSERT_TRUE(ZigbeeAttribute_IsConfiguration(3, 8, 0));
    TEST_ASSERT_TRUE(ZigbeeAttribute_IsConfiguration(9, 0x204, 0x8020));
    TEST_ASSERT_TRUE(ZigbeeAttribute_IsConfiguration(10, 0x204, 0x8041));
    /* An unchanged diagnostic does not consume a report buffer on startup.
     * A failed changed diagnostic still retries after its local value settles. */
    zigbeeService.sourceReportMask = 0;
    configuration_previous = value;
    TEST_ASSERT_EQUAL(EZB_ZCL_STATUS_SUCCESS, SetNumericAttribute(1, 0x204,
        FAN_ATTR_EXTERNAL_FIRST, &value, 1, false, &reported));
    TEST_ASSERT_FALSE(reported);
    TEST_ASSERT_EQUAL(3, fan_report_request_fake.call_count);
    zigbeeService.sourceReportMask = 0;
    configuration_previous = 0;
    fan_report_request_fake.return_val = EZB_ERR_NO_MEM;
    TEST_ASSERT_EQUAL(EZB_ZCL_STATUS_INSUFFICIENT_SPACE, SetNumericAttribute(1, 0x204,
        FAN_ATTR_EXTERNAL_FIRST, &value, 1, false, &reported));
    TEST_ASSERT_TRUE(reported);
    configuration_previous = value;
    fan_report_request_fake.return_val = EZB_ERR_NONE;
    TEST_ASSERT_EQUAL(EZB_ZCL_STATUS_SUCCESS, SetNumericAttribute(1, 0x204,
        FAN_ATTR_EXTERNAL_FIRST, &value, 1, true, &reported));
    TEST_ASSERT_TRUE(reported);
    TEST_ASSERT_EQUAL(5, fan_report_request_fake.call_count);
}

static unsigned info_neighbors;
static bool info_has_coordinator, info_iterator_fails, info_iterator_unbounded, info_sdk_owned;
static bool info_lock(TickType_t wait) { (void)wait; info_sdk_owned = true; return true; }
static void info_unlock(void) { TEST_ASSERT_TRUE(info_sdk_owned); info_sdk_owned = false; }
static uint16_t info_pan(void) { TEST_ASSERT_TRUE(info_sdk_owned); return 0x1234; }
static ezb_err_t info_neighbor(ezb_nwk_info_iterator_t *iterator, ezb_nwk_neighbor_info_t *neighbor) {
    (void)iterator; TEST_ASSERT_TRUE(info_sdk_owned);
    if (info_iterator_fails) return EZB_ERR_INV_ARG;
    if (!info_iterator_unbounded && info_neighbors >= 2) return EZB_ERR_NOT_FOUND;
    *neighbor = (ezb_nwk_neighbor_info_t){.short_addr=info_has_coordinator && !info_neighbors ? 0 : 0x3456,
        .rssi=-67,.lqi=123,.relationship=info_neighbors ? EZB_NWK_RELATIONSHIP_CHILD : EZB_NWK_RELATIONSHIP_SIBLING};
    ++info_neighbors;
    return EZB_ERR_NONE;
}
static IZigbee *prepare_network_info(void) {
    IZigbee *radio = prepare_attribute_queue();
    info_neighbors = 0; info_has_coordinator = true; info_iterator_fails = info_iterator_unbounded = info_sdk_owned = false;
    esp_zigbee_lock_acquire_fake.custom_fake = info_lock;
    esp_zigbee_lock_release_fake.custom_fake = info_unlock;
    mocked_ezb_get_panid_fake.custom_fake = info_pan;
    mocked_ezb_nwk_get_next_neighbor_fake.custom_fake = info_neighbor;
    return radio;
}
void test_Zigbee_NetworkInfoUsesLockedRealCoordinatorMetrics(void) {
    IZigbee *radio = prepare_network_info();
    ZigbeeNetworkInfo info;
    TEST_ASSERT_EQUAL(0, radio->GetNetworkInfo(radio, &info));
    TEST_ASSERT_FALSE(info_sdk_owned);
    TEST_ASSERT_EQUAL(0x1234, info.pan_id); TEST_ASSERT_EQUAL(2, info.neighbor_count);
    TEST_ASSERT_EQUAL(1, info.child_count); TEST_ASSERT_EQUAL(10, info.endpoint_count);
    TEST_ASSERT_TRUE(info.coordinator_link_valid);
    TEST_ASSERT_EQUAL(-67, info.rssi); TEST_ASSERT_EQUAL(123, info.lqi);
    TEST_ASSERT_EQUAL(pdMS_TO_TICKS(ZIGBEE_LOCK_TIMEOUT_MS), esp_zigbee_lock_acquire_fake.arg0_val);
}
void test_Zigbee_NetworkInfoFailurePreservesCallerSnapshot(void) {
    IZigbee *radio = prepare_network_info();
    ZigbeeNetworkInfo info, original;
    memset(&info, 0xAA, sizeof(info)); original = info;
    info_iterator_fails = true;
    TEST_ASSERT_EQUAL(ZIGBEE_ERROR_STACK_FAIL, radio->GetNetworkInfo(radio, &info));
    TEST_ASSERT_EQUAL_MEMORY(&original, &info, sizeof(info));
    TEST_ASSERT_FALSE(info_sdk_owned);
    esp_zigbee_lock_acquire_fake.custom_fake = NULL;
    esp_zigbee_lock_acquire_fake.return_val = false;
    TEST_ASSERT_EQUAL(ZIGBEE_ERROR_LOCK_FAIL, radio->GetNetworkInfo(radio, &info));
    TEST_ASSERT_EQUAL_MEMORY(&original, &info, sizeof(info));
}
void test_Zigbee_NetworkInfoDoesNotInventUnavailableLinkMetrics(void) {
    IZigbee *radio = prepare_network_info(); info_has_coordinator = false;
    ZigbeeNetworkInfo info;
    TEST_ASSERT_EQUAL(0, radio->GetNetworkInfo(radio, &info));
    TEST_ASSERT_FALSE(info.coordinator_link_valid);
    TEST_ASSERT_EQUAL(0, info.rssi); TEST_ASSERT_EQUAL(0, info.lqi);
    TEST_ASSERT_EQUAL(2, info.neighbor_count);
}
void test_Zigbee_NetworkInfoBoundsBrokenIterator(void) {
    IZigbee *radio = prepare_network_info(); info_iterator_unbounded = true;
    ZigbeeNetworkInfo info = {0};
    TEST_ASSERT_EQUAL(ZIGBEE_ERROR_STACK_FAIL, radio->GetNetworkInfo(radio, &info));
    TEST_ASSERT_EQUAL(256, info_neighbors);
    TEST_ASSERT_FALSE(info_sdk_owned);
    TEST_ASSERT_EQUAL(0, info.neighbor_count);
}

void test_Zigbee_SteeringTimerDefersContentionWithoutBlockingDaemon(void) {
    zigbeeService.currentState = SERVICE_STATE_STARTED;
    atomic_store(&zigbeeService.isPairing, true);
    esp_zigbee_lock_acquire_fake.return_val = false;
    SteeringRetryTimerCb(NULL);
    TEST_ASSERT_EQUAL(0, esp_zigbee_lock_acquire_fake.arg0_val);
    TEST_ASSERT_EQUAL(0, mocked_ezb_bdb_start_top_level_commissioning_fake.call_count);
    TEST_ASSERT_EQUAL(0, esp_zigbee_lock_release_fake.call_count);
    esp_zigbee_lock_acquire_fake.return_val = true;
    SteeringRetryTimerCb(NULL);
    TEST_ASSERT_EQUAL(1, mocked_ezb_bdb_start_top_level_commissioning_fake.call_count);
    TEST_ASSERT_EQUAL(1, esp_zigbee_lock_release_fake.call_count);
}

void test_Zigbee_SteeringTimerRejectsClosingAndFailedChannelSetup(void) {
    zigbeeService.currentState = SERVICE_STATE_STARTED;
    atomic_store(&zigbeeService.isPairing, true);
    atomic_store(&zigbeeService.closing, true);
    SteeringRetryTimerCb(NULL);
    TEST_ASSERT_EQUAL(0, esp_zigbee_lock_acquire_fake.call_count);
    atomic_store(&zigbeeService.closing, false);
    mocked_ezb_bdb_set_primary_channel_set_fake.return_val = EZB_ERR_INV_ARG;
    SteeringRetryTimerCb(NULL);
    TEST_ASSERT_EQUAL(0, mocked_ezb_bdb_start_top_level_commissioning_fake.call_count);
    TEST_ASSERT_EQUAL(1, esp_zigbee_lock_release_fake.call_count);
    zigbeeService.currentState = SERVICE_STATE_STOPPED;
    SteeringRetryTimerCb(NULL);
    TEST_ASSERT_EQUAL(1, esp_zigbee_lock_acquire_fake.call_count);
}

static bool close_during_steering_lock(TickType_t timeout) {
    TEST_ASSERT_EQUAL(0, timeout);
    atomic_store(&zigbeeService.closing, true);
    return true;
}
void test_Zigbee_SteeringTimerRechecksShutdownAfterSDKAdmission(void) {
    zigbeeService.currentState = SERVICE_STATE_STARTED;
    atomic_store(&zigbeeService.isPairing, true);
    esp_zigbee_lock_acquire_fake.custom_fake = close_during_steering_lock;
    SteeringRetryTimerCb(NULL);
    TEST_ASSERT_EQUAL(0, mocked_ezb_bdb_set_primary_channel_set_fake.call_count);
    TEST_ASSERT_EQUAL(0, mocked_ezb_bdb_start_top_level_commissioning_fake.call_count);
    TEST_ASSERT_EQUAL(1, esp_zigbee_lock_release_fake.call_count);
}

static esp_err_t create_init_pm_guard(esp_pm_lock_type_t type, int arg, const char *name,
    esp_pm_lock_handle_t *handle) {
    TEST_ASSERT_EQUAL(ESP_PM_CPU_FREQ_MAX, type);
    TEST_ASSERT_EQUAL(0, arg); TEST_ASSERT_EQUAL_STRING("zb_init", name);
    *handle = (esp_pm_lock_handle_t)7;
    return ESP_OK;
}
void test_Zigbee_InitPowerGuardChecksCreationBeforeAcquisition(void) {
    ZigbeeInitPowerGuard guard = {0};
    esp_pm_lock_create_fake.return_val = ESP_ERR_NO_MEM;
    TEST_ASSERT_EQUAL(ESP_ERR_NO_MEM, AcquireInitPowerGuard(&guard));
    TEST_ASSERT_EQUAL(0, esp_pm_lock_acquire_fake.call_count);
    TEST_ASSERT_EQUAL(0, esp_pm_lock_delete_fake.call_count);
    esp_pm_lock_create_fake.return_val = ESP_OK;
    TEST_ASSERT_EQUAL(ESP_FAIL, AcquireInitPowerGuard(&guard));
    TEST_ASSERT_EQUAL(0, esp_pm_lock_acquire_fake.call_count);
}
void test_Zigbee_InitPowerGuardBalancesSuccessAndRejectsDuplicateAcquire(void) {
    ZigbeeInitPowerGuard guard = {0};
    esp_pm_lock_create_fake.custom_fake = create_init_pm_guard;
    TEST_ASSERT_EQUAL(ESP_OK, AcquireInitPowerGuard(&guard));
    TEST_ASSERT_TRUE(guard.held);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, AcquireInitPowerGuard(&guard));
    TEST_ASSERT_EQUAL(1, esp_pm_lock_create_fake.call_count);
    TEST_ASSERT_EQUAL(ESP_OK, ReleaseInitPowerGuard(&guard));
    TEST_ASSERT_FALSE(guard.held); TEST_ASSERT_NULL(guard.handle);
    TEST_ASSERT_EQUAL(1, esp_pm_lock_release_fake.call_count);
    TEST_ASSERT_EQUAL(1, esp_pm_lock_delete_fake.call_count);
    TEST_ASSERT_EQUAL(ESP_OK, ReleaseInitPowerGuard(&guard));
    TEST_ASSERT_EQUAL(1, esp_pm_lock_delete_fake.call_count);
}
void test_Zigbee_InitPowerGuardFailedAcquireRetainsFailedCleanupForRetry(void) {
    ZigbeeInitPowerGuard guard = {0};
    esp_pm_lock_create_fake.custom_fake = create_init_pm_guard;
    esp_pm_lock_acquire_fake.return_val = ESP_FAIL;
    esp_pm_lock_delete_fake.return_val = ESP_ERR_INVALID_STATE;
    TEST_ASSERT_EQUAL(ESP_FAIL, AcquireInitPowerGuard(&guard));
    TEST_ASSERT_FALSE(guard.held); TEST_ASSERT_NOT_NULL(guard.handle);
    TEST_ASSERT_EQUAL(0, esp_pm_lock_release_fake.call_count);
    esp_pm_lock_delete_fake.return_val = ESP_OK;
    TEST_ASSERT_EQUAL(ESP_OK, ReleaseInitPowerGuard(&guard));
    TEST_ASSERT_NULL(guard.handle);
}
void test_Zigbee_InitPowerGuardReleaseAndDeleteFailuresPreserveOwnership(void) {
    ZigbeeInitPowerGuard guard = {.handle = (esp_pm_lock_handle_t)7, .held = true};
    esp_pm_lock_release_fake.return_val = ESP_FAIL;
    TEST_ASSERT_EQUAL(ESP_FAIL, ReleaseInitPowerGuard(&guard));
    TEST_ASSERT_TRUE(guard.held); TEST_ASSERT_NOT_NULL(guard.handle);
    TEST_ASSERT_EQUAL(0, esp_pm_lock_delete_fake.call_count);
    esp_pm_lock_release_fake.return_val = ESP_OK;
    esp_pm_lock_delete_fake.return_val = ESP_ERR_INVALID_STATE;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, ReleaseInitPowerGuard(&guard));
    TEST_ASSERT_FALSE(guard.held); TEST_ASSERT_NOT_NULL(guard.handle);
    esp_pm_lock_delete_fake.return_val = ESP_OK;
    TEST_ASSERT_EQUAL(ESP_OK, ReleaseInitPowerGuard(&guard));
    TEST_ASSERT_NULL(guard.handle);
    TEST_ASSERT_EQUAL(2, esp_pm_lock_release_fake.call_count);
}

void test_Zigbee_SdkAccessRetainsOwnerAndRejectsForeignRelease(void) {
    IZigbee *radio = Zigbee_Create();
    zigbeeService.currentState = SERVICE_STATE_STARTED;
    TEST_ASSERT_TRUE(radio->TrySdkAccess(radio, 0));
    TEST_ASSERT_EQUAL(1, atomic_load(&zigbeeService.sdkReaders));
    TEST_ASSERT_FALSE(radio->TrySdkAccess(radio, 0));
    TEST_ASSERT_EQUAL(1, atomic_load(&zigbeeService.sdkReaders));
    mocked_xTaskGetCurrentTaskHandle_fake.return_val = (TaskHandle_t)8;
    TEST_ASSERT_EQUAL(ZIGBEE_ERROR_INVALID_PARAM, radio->ReleaseSdkAccess(radio));
    TEST_ASSERT_EQUAL(1, atomic_load(&zigbeeService.sdkReaders));
    TEST_ASSERT_EQUAL(0, esp_zigbee_lock_release_fake.call_count);
    mocked_xTaskGetCurrentTaskHandle_fake.return_val = (TaskHandle_t)7;
    TEST_ASSERT_EQUAL(0, radio->ReleaseSdkAccess(radio));
    TEST_ASSERT_EQUAL(0, atomic_load(&zigbeeService.sdkReaders));
    TEST_ASSERT_EQUAL(1, esp_zigbee_lock_release_fake.call_count);
}
void test_Zigbee_SdkAccessRejectsClosingOverflowAndLockFailure(void) {
    IZigbee *radio = Zigbee_Create();
    zigbeeService.currentState = SERVICE_STATE_STARTED;
    atomic_store(&zigbeeService.closing, true);
    TEST_ASSERT_FALSE(radio->TrySdkAccess(radio, 0));
    TEST_ASSERT_EQUAL(0, esp_zigbee_lock_acquire_fake.call_count);
    atomic_store(&zigbeeService.closing, false);
    atomic_store(&zigbeeService.sdkReaders, UINT_MAX);
    TEST_ASSERT_FALSE(radio->TrySdkAccess(radio, 0));
    atomic_store(&zigbeeService.sdkReaders, 0);
    esp_zigbee_lock_acquire_fake.return_val = false;
    TEST_ASSERT_FALSE(radio->TrySdkAccess(radio, ZIGBEE_LOCK_TIMEOUT_MS));
    TEST_ASSERT_EQUAL(0, atomic_load(&zigbeeService.sdkReaders));
    TEST_ASSERT_NULL(atomic_load(&zigbeeService.sdkAccessOwner));
}

static uint8_t fan_edges[8];
static unsigned fan_edge_count;
static ezb_zcl_status_t record_fan_edge(uint8_t endpoint, uint16_t cluster, uint8_t role,
        uint16_t attribute, uint16_t manufacturer, void *value, bool check) {
    (void)role; (void)manufacturer; (void)check;
    TEST_ASSERT_EQUAL(1, endpoint); TEST_ASSERT_EQUAL(6, cluster); TEST_ASSERT_EQUAL(0, attribute);
    TEST_ASSERT_TRUE(fan_edge_count < sizeof(fan_edges));
    fan_edges[fan_edge_count++] = *(uint8_t *)value;
    return EZB_ZCL_STATUS_SUCCESS;
}
static ezb_err_t capture_fan_report(const ezb_zcl_report_attr_cmd_t *report) {
    TEST_ASSERT_EQUAL(1, report->cmd_ctrl.src_ep);
    TEST_ASSERT_EQUAL(6, report->cmd_ctrl.cluster_id);
    TEST_ASSERT_EQUAL(EZB_ADDR_MODE_NONE, report->cmd_ctrl.dst_addr.addr_mode);
    TEST_ASSERT_EQUAL(0, report->payload.attr_id);
    return fan_report_request_fake.return_val;
}
void test_FanRelayQueuePreservesEdgesAndExplicitReportFailureRetries(void) {
    IZigbee *radio = prepare_attribute_queue();
    mocked_ezb_zcl_attr_desc_get_type_fake.return_val = EZB_ZCL_ATTR_TYPE_BOOL;
    mocked_ezb_zcl_set_attr_value_fake.custom_fake = record_fan_edge;
    fan_edge_count = 0;
    fan_report_request_fake.custom_fake = capture_fan_report;
    const uint8_t edges[] = {0, 1, 0};
    for (unsigned i = 0; i < sizeof(edges); ++i)
        TEST_ASSERT_EQUAL(0, radio->QueueAttributeUpdate(radio, 1, 6, 0, &edges[i], 1));
    TEST_ASSERT_EQUAL(3, zigbeeService.attributeCount);
    for (unsigned i = 0; i < sizeof(edges); ++i) {
        ApplyQueuedAttributes();
        TEST_ASSERT_EQUAL(i + 1, fan_report_request_fake.call_count);
        TEST_ASSERT_EQUAL(sizeof(edges) - i - 1, zigbeeService.attributeCount);
    }
    TEST_ASSERT_EQUAL(3, fan_report_request_fake.call_count);
    TEST_ASSERT_EQUAL_MEMORY(edges, fan_edges, sizeof(edges));
    TEST_ASSERT_EQUAL(0, zigbeeService.attributeCount);
    uint8_t next = 1;
    fan_report_request_fake.return_val = EZB_ERR_NO_MEM;
    TEST_ASSERT_EQUAL(0, radio->QueueAttributeUpdate(radio, 1, 6, 0, &next, 1));
    ApplyQueuedAttributes();
    TEST_ASSERT_EQUAL(1, zigbeeService.attributeCount);
    fan_report_request_fake.return_val = EZB_ERR_NONE;
    mocked_esp_timer_get_time_fake.return_val += (int64_t)ZIGBEE_ATTRIBUTE_RETRY_BASE_MS * 1000;
    ApplyQueuedAttributes();
    TEST_ASSERT_EQUAL(0, zigbeeService.attributeCount);
    TEST_ASSERT_EQUAL(1, zigbeeService.attributeRetries);
}

void test_FanRelayEdgesCannotOvertakeRejectedReportAdmission(void) {
    IZigbee *radio = prepare_attribute_queue();
    mocked_ezb_zcl_attr_desc_get_type_fake.return_val = EZB_ZCL_ATTR_TYPE_BOOL;
    mocked_ezb_zcl_set_attr_value_fake.custom_fake = record_fan_edge;
    fan_edge_count = 0;
    fan_report_request_fake.custom_fake = capture_fan_report;
    fan_report_request_fake.return_val = EZB_ERR_FAIL;
    const uint8_t edges[] = {1, 0};
    for (unsigned i = 0; i < sizeof(edges); ++i)
        TEST_ASSERT_EQUAL(0, radio->QueueAttributeUpdate(radio, 1, 6, 0, &edges[i], 1));
    for (unsigned i = 0; i < 4; ++i) {
        ProcessReportingWork();
        TEST_ASSERT_EQUAL(2, zigbeeService.attributeCount);
        TEST_ASSERT_EQUAL(0, zigbeeService.attributeDropped);
        mocked_esp_timer_get_time_fake.return_val += (int64_t)ZIGBEE_ATTRIBUTE_RETRY_MAX_MS * 1000;
    }
    fan_report_request_fake.return_val = EZB_ERR_NONE;
    ProcessReportingWork();
    TEST_ASSERT_EQUAL(1, zigbeeService.attributeCount);
    ProcessReportingWork();
    const uint8_t expected[] = {1, 1, 1, 1, 1, 0};
    TEST_ASSERT_EQUAL(sizeof(expected), fan_edge_count);
    TEST_ASSERT_EQUAL_MEMORY(expected, fan_edges, sizeof(expected));
    TEST_ASSERT_EQUAL(0, zigbeeService.attributeCount);
    TEST_ASSERT_EQUAL(2, zigbeeService.attributeApplied);
}

void test_Zigbee_StopFromSdkOwnerPreservesLeaseAndQueuedData(void) {
    IZigbee *radio = prepare_attribute_queue();
    uint16_t value = 1;
    TEST_ASSERT_EQUAL(0, radio->QueueAttributeUpdate(radio, 1, 0x402, 0, &value, sizeof(value)));
    TEST_ASSERT_TRUE(radio->TrySdkAccess(radio, 0));
    unsigned calls = esp_zigbee_lock_acquire_fake.call_count;
    TEST_ASSERT_EQUAL(ZIGBEE_ERROR_LOCK_FAIL, radio->base.Stop(&radio->base));
    TEST_ASSERT_EQUAL(calls, esp_zigbee_lock_acquire_fake.call_count);
    TEST_ASSERT_EQUAL(1, atomic_load(&zigbeeService.sdkReaders));
    TEST_ASSERT_TRUE(AttributesPending());
    TEST_ASSERT_TRUE(atomic_load(&zigbeeService.closing));
    TEST_ASSERT_EQUAL(0, radio->ReleaseSdkAccess(radio));
    TEST_ASSERT_EQUAL(0, radio->base.Stop(&radio->base));
    TEST_ASSERT_FALSE(AttributesPending());
}

void test_Zigbee_SdkAdmissionClosingUnwindsReaderAndLock(void) {
    IZigbee *radio = Zigbee_Create();
    zigbeeService.currentState = SERVICE_STATE_STARTED;
    esp_zigbee_lock_acquire_fake.custom_fake = close_during_steering_lock;
    TEST_ASSERT_FALSE(radio->TrySdkAccess(radio, 0));
    TEST_ASSERT_EQUAL(0, atomic_load(&zigbeeService.sdkReaders));
    TEST_ASSERT_NULL(atomic_load(&zigbeeService.sdkAccessOwner));
    TEST_ASSERT_EQUAL(1, esp_zigbee_lock_release_fake.call_count);
}

void test_Zigbee_FactoryResetUsesSdkOwnershipWithoutErasingUserSettingsOrFrameCounter(void) {
    IZigbee *radio = prepare_attribute_queue();
    TEST_ASSERT_TRUE(radio->IsConnected(radio));
    TEST_ASSERT_EQUAL(0, radio->ResetToFactorySettings(radio));
    TEST_ASSERT_EQUAL(1, ezb_bdb_reset_via_local_action_fake.call_count);
    TEST_ASSERT_EQUAL(0, nvs_flash_erase_partition_fake.call_count);
    TEST_ASSERT_EQUAL(0, mocked_nvs_flash_erase_fake.call_count);
    TEST_ASSERT_EQUAL(0, mocked_esp_restart_fake.call_count);
    TEST_ASSERT_EQUAL(0, atomic_load(&zigbeeService.sdkReaders));
    TEST_ASSERT_TRUE(radio->IsConnected(radio));
    esp_zigbee_lock_acquire_fake.return_val = false;
    TEST_ASSERT_NOT_EQUAL(0, radio->ResetToFactorySettings(radio));
    TEST_ASSERT_EQUAL(1, ezb_bdb_reset_via_local_action_fake.call_count);
}

static void emit_reset_leave(ezb_zdo_leave_type_t type, esp_err_t result) {
    /* Endpoint startup uses an empty handler fixture. This test exercises the
     * SDK owner rather than invoking that unimplemented injected callback. */
    zigbeeService.signalHandlerList = NULL;
    ezb_zdo_signal_leave_params_t params = {.leave_type = type};
    uint32_t signal = EZB_ZDO_SIGNAL_LEAVE;
    esp_zb_app_signal_t event = {.p_app_signal = &signal, .esp_err_status = result};
    ezb_app_signal_get_params_fake.return_val = &params;
    esp_zb_app_signal_handler(&event);
    ezb_app_signal_get_params_fake.return_val = NULL;
}
static bool reject_factory_mutation(IConfigurationManager *self) { (void)self; return false; }
void test_Zigbee_FactoryGuardRejectsForeignSteeringResetAndTxBeforeSdkWrite(void) {
    IZigbee *radio = prepare_attribute_queue();
    mock_config_inst.IsMutationAllowed = reject_factory_mutation;
    mock_config_inst.UpdateFields = persist_tx_profile;
    unsigned writes = mocked_ezb_set_tx_power_fake.call_count;
    uint32_t receipt = 99;
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FINISHED, radio->ConnectToNetwork(radio));
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FINISHED, radio->DisconnectFromNetwork(radio));
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FINISHED, radio->BeginFactoryReset(radio, &receipt));
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FINISHED, radio->SetTxProfile(radio, ZIGBEE_TX_BOOST));
    TEST_ASSERT_EQUAL(99, receipt);
    TEST_ASSERT_EQUAL(writes, mocked_ezb_set_tx_power_fake.call_count);
    TEST_ASSERT_EQUAL(0, ezb_bdb_reset_via_local_action_fake.call_count);
    TEST_ASSERT_EQUAL(0, ezb_bdb_start_top_level_commissioning_fake.call_count);
    TEST_ASSERT_EQUAL(0, atomic_load(&zigbeeService.sdkReaders));
    mock_config_inst.IsMutationAllowed = NULL;
    mock_config_inst.UpdateFields = NULL;
}
void test_Zigbee_FactoryResetReceiptRejectsDuplicatesAndWrongLeave(void) {
    IZigbee *radio = prepare_attribute_queue();
    emit_reset_leave(EZB_ZDO_LEAVE_TYPE_RESET, ESP_OK);
    atomic_store(&zigbeeService.isConnected, true);
    uint32_t receipt = 0, duplicate = 99;
    TEST_ASSERT_EQUAL(ESP_OK, radio->BeginFactoryReset(radio, &receipt));
    TEST_ASSERT_NOT_EQUAL(0, receipt);
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FINISHED, radio->BeginFactoryReset(radio, &duplicate));
    TEST_ASSERT_EQUAL(99, duplicate);
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FINISHED, radio->ConnectToNetwork(radio));
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FINISHED, radio->DisconnectFromNetwork(radio));
    emit_reset_leave(EZB_ZDO_LEAVE_TYPE_REJOIN, ESP_OK);
    ZigbeeFactoryResetStatus status;
    TEST_ASSERT_EQUAL(ESP_OK, radio->GetFactoryResetStatus(radio, &status));
    TEST_ASSERT_TRUE(status.pending);
    TEST_ASSERT_EQUAL(receipt, status.operation_id);
    emit_reset_leave(EZB_ZDO_LEAVE_TYPE_RESET, ESP_FAIL);
    TEST_ASSERT_EQUAL(ESP_OK, radio->GetFactoryResetStatus(radio, &status));
    TEST_ASSERT_FALSE(status.pending);
    TEST_ASSERT_EQUAL(ESP_FAIL, status.result);
    TEST_ASSERT_EQUAL(1, ezb_bdb_reset_via_local_action_fake.call_count);
}
void test_Zigbee_FactoryResetAlreadyNewAndDisconnectedIsExplicitlyComplete(void) {
    IZigbee *radio = prepare_attribute_queue();
    atomic_store(&zigbeeService.isConnected, false);
    ezb_bdb_is_factory_new_fake.return_val = true;
    uint32_t receipt;
    TEST_ASSERT_EQUAL(ESP_OK, radio->BeginFactoryReset(radio, &receipt));
    ZigbeeFactoryResetStatus status;
    TEST_ASSERT_EQUAL(ESP_OK, radio->GetFactoryResetStatus(radio, &status));
    TEST_ASSERT_FALSE(status.pending);
    TEST_ASSERT_TRUE(status.factory_new);
    TEST_ASSERT_FALSE(status.connected);
    TEST_ASSERT_EQUAL(receipt, status.operation_id);
    TEST_ASSERT_EQUAL(0, ezb_bdb_reset_via_local_action_fake.call_count);
    ezb_bdb_is_factory_new_fake.return_val = false;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, radio->BeginFactoryReset(radio, &receipt));
}
void test_Zigbee_FactoryResetCompletionAndStopPreservePendingSdkOwnership(void) {
    IZigbee *radio = prepare_attribute_queue();
    uint32_t receipt;
    TEST_ASSERT_EQUAL(ESP_OK, radio->BeginFactoryReset(radio, &receipt));
    TEST_ASSERT_EQUAL(ZIGBEE_ERROR_TIMEOUT, radio->base.Stop(&radio->base));
    TEST_ASSERT_FALSE(atomic_load(&zigbeeService.closing));
    TEST_ASSERT_EQUAL(0, esp_zigbee_stop_fake.call_count);
    emit_reset_leave(EZB_ZDO_LEAVE_TYPE_RESET, ESP_OK);
    ezb_bdb_is_factory_new_fake.return_val = true;
    ZigbeeFactoryResetStatus status;
    TEST_ASSERT_EQUAL(ESP_OK, radio->GetFactoryResetStatus(radio, &status));
    TEST_ASSERT_FALSE(status.pending);
    TEST_ASSERT_TRUE(status.factory_new);
    TEST_ASSERT_FALSE(status.connected);
    TEST_ASSERT_EQUAL(ESP_OK, status.result);
    TEST_ASSERT_EQUAL(0, radio->base.Stop(&radio->base));
}

void test_Zigbee_WorkerStopWaitsForAcknowledgmentAndAllowsRestart(void) {
    IZigbee *radio = Zigbee_Create();
    TEST_ASSERT_EQUAL(0, radio->base.Initialize(&radio->base, NULL));
    TEST_ASSERT_EQUAL(0, radio->base.Start(&radio->base));
    workerCanExit = false;
    TEST_ASSERT_EQUAL(ZIGBEE_ERROR_TIMEOUT, radio->base.Stop(&radio->base));
    TEST_ASSERT_EQUAL_PTR((TaskHandle_t)2, zigbeeService.workerTask);
    TEST_ASSERT_EQUAL(0, radio_worker_deletes);
    TEST_ASSERT_TRUE(atomic_load(&zigbeeService.closing));
    TEST_ASSERT_NOT_EQUAL(0, radio->base.Start(&radio->base));
    TEST_ASSERT_EQUAL(1, xTaskCreateStatic_fake.call_count);
    workerCanExit = true;
    TEST_ASSERT_EQUAL(0, radio->base.Stop(&radio->base));
    TEST_ASSERT_EQUAL(1, radio_worker_deletes);
    TEST_ASSERT_NULL(zigbeeService.workerTask);
    TEST_ASSERT_EQUAL(0, radio->base.Start(&radio->base));
    TEST_ASSERT_EQUAL(2, xTaskCreateStatic_fake.call_count);
}
void test_Zigbee_AcceptedStopIsNotResentWhileExitIsPending(void) {
    IZigbee *radio = Zigbee_Create();
    TEST_ASSERT_EQUAL(0, radio->base.Initialize(&radio->base, NULL));
    TEST_ASSERT_EQUAL(0, radio->base.Start(&radio->base));
    esp_zigbee_stop_fake.custom_fake = accept_stop_without_exit;
    TEST_ASSERT_EQUAL(ZIGBEE_ERROR_TIMEOUT, radio->base.Stop(&radio->base));
    TEST_ASSERT_EQUAL(1, esp_zigbee_stop_fake.call_count);
    TEST_ASSERT_EQUAL(ZIGBEE_ERROR_TIMEOUT, radio->base.Stop(&radio->base));
    TEST_ASSERT_EQUAL(1, esp_zigbee_stop_fake.call_count);
    TEST_ASSERT_EQUAL_PTR((TaskHandle_t)2, zigbeeService.workerTask);
    atomic_store(&zigbeeService.mainloopExited, true);
    TEST_ASSERT_EQUAL(0, radio->base.Stop(&radio->base));
}
void test_Zigbee_EarlyStopWaitsForOneMainloopCallbackAcrossTimeouts(void) {
    IZigbee *radio = Zigbee_Create();
    TEST_ASSERT_EQUAL(0, radio->base.Initialize(&radio->base, NULL));
    TEST_ASSERT_EQUAL(0, radio->base.Start(&radio->base));
    sdkCallbacksAllowed = false;
    TEST_ASSERT_EQUAL(ZIGBEE_ERROR_TIMEOUT, radio->base.Stop(&radio->base));
    TEST_ASSERT_EQUAL(ZIGBEE_ERROR_TIMEOUT, radio->base.Stop(&radio->base));
    TEST_ASSERT_EQUAL(1, esp_zigbee_task_queue_post_fake.call_count);
    TEST_ASSERT_EQUAL(0, esp_zigbee_stop_fake.call_count);
    TEST_ASSERT_EQUAL_PTR((TaskHandle_t)2, zigbeeService.workerTask);
    sdkCallbacksAllowed = true;
    TEST_ASSERT_EQUAL(0, radio->base.Stop(&radio->base));
    TEST_ASSERT_EQUAL(1, esp_zigbee_task_queue_post_fake.call_count);
    TEST_ASSERT_EQUAL(1, esp_zigbee_stop_fake.call_count);
}
void test_Zigbee_StopQueueFailurePreservesOwnerUntilExplicitRetry(void) {
    IZigbee *radio = Zigbee_Create();
    TEST_ASSERT_EQUAL(0, radio->base.Initialize(&radio->base, NULL));
    TEST_ASSERT_EQUAL(0, radio->base.Start(&radio->base));
    esp_zigbee_task_queue_post_fake.custom_fake = NULL;
    esp_zigbee_task_queue_post_fake.return_val = ESP_FAIL;
    TEST_ASSERT_EQUAL(ZIGBEE_ERROR_STACK_FAIL, radio->base.Stop(&radio->base));
    TEST_ASSERT_EQUAL_PTR((TaskHandle_t)2, zigbeeService.workerTask);
    TEST_ASSERT_EQUAL(0, esp_zigbee_stop_fake.call_count);
    esp_zigbee_task_queue_post_fake.custom_fake = queue_sdk_callback;
    TEST_ASSERT_EQUAL(0, radio->base.Stop(&radio->base));
    TEST_ASSERT_EQUAL(2, esp_zigbee_task_queue_post_fake.call_count);
    TEST_ASSERT_EQUAL(1, esp_zigbee_stop_fake.call_count);
}
void test_Zigbee_WorkerStopRejectsSdkFailureBeforeCleanupPermission(void) {
    IZigbee *radio = Zigbee_Create();
    TEST_ASSERT_EQUAL(0, radio->base.Initialize(&radio->base, NULL));
    TEST_ASSERT_EQUAL(0, radio->base.Start(&radio->base));
    esp_zigbee_stop_fake.custom_fake = NULL;
    esp_zigbee_stop_fake.return_val = ESP_FAIL;
    TEST_ASSERT_EQUAL(ZIGBEE_ERROR_STACK_FAIL, radio->base.Stop(&radio->base));
    TEST_ASSERT_FALSE(atomic_load(&zigbeeService.cleanupAllowed));
    TEST_ASSERT_EQUAL(1, esp_zigbee_lock_release_fake.call_count);
    TEST_ASSERT_EQUAL(0, radio_worker_deletes);
    esp_zigbee_stop_fake.custom_fake = stop_worker;
    TEST_ASSERT_EQUAL(0, radio->base.Stop(&radio->base));
}
void test_Zigbee_WorkerCleanupFailureNeverDeletesOrCallsSdkTwice(void) {
    IZigbee *radio = Zigbee_Create();
    TEST_ASSERT_EQUAL(0, radio->base.Initialize(&radio->base, NULL));
    TEST_ASSERT_EQUAL(0, radio->base.Start(&radio->base));
    atomic_store(&zigbeeService.mainloopExited, true);
    atomic_store(&zigbeeService.workerExited, true);
    atomic_store(&zigbeeService.cleanupResult, ESP_FAIL);
    TEST_ASSERT_EQUAL(ZIGBEE_ERROR_STACK_FAIL, radio->base.Stop(&radio->base));
    TEST_ASSERT_EQUAL(ZIGBEE_ERROR_STACK_FAIL, radio->base.Stop(&radio->base));
    TEST_ASSERT_EQUAL(0, esp_zigbee_stop_fake.call_count);
    TEST_ASSERT_EQUAL(0, radio_worker_deletes);
    TEST_ASSERT_EQUAL_PTR((TaskHandle_t)2, zigbeeService.workerTask);
    TEST_ASSERT_EQUAL(SERVICE_STATE_ERROR, zigbeeService.currentState);
}
void test_Zigbee_StartupTimeoutRetainsWorkerUntilExplicitStop(void) {
    IZigbee *radio = Zigbee_Create();
    TEST_ASSERT_EQUAL(0, radio->base.Initialize(&radio->base, NULL));
    completeStartup = false;
    testTicks = UINT32_MAX - 8;
    TEST_ASSERT_EQUAL(ZIGBEE_ERROR_TIMEOUT, radio->base.Start(&radio->base));
    TEST_ASSERT_EQUAL(0, radio_worker_deletes);
    TEST_ASSERT_EQUAL_PTR((TaskHandle_t)2, zigbeeService.workerTask);
    atomic_store(&zigbeeService.startupComplete, true);
    atomic_store(&zigbeeService.startupResult, ESP_FAIL);
    atomic_store(&zigbeeService.mainloopExited, true);
    TEST_ASSERT_EQUAL(0, radio->base.Stop(&radio->base));
    TEST_ASSERT_EQUAL(0, esp_zigbee_stop_fake.call_count);
}

static jmp_buf worker_return;
void test_ZigbeeLifecycleRejectsForeignSelfBeforeClosingOrSdkAccess(void) {
    IZigbee *radio = Zigbee_Create();
    TEST_ASSERT_EQUAL(0, radio->base.Initialize(&radio->base, NULL));
    IService foreign = {0};
    unsigned locks = mocked_xSemaphoreTake_fake.call_count;
    TEST_ASSERT_EQUAL(ZIGBEE_ERROR_INVALID_PARAM, radio->base.Start(&foreign));
    TEST_ASSERT_EQUAL(ZIGBEE_ERROR_INVALID_PARAM, radio->base.Stop(&foreign));
    TEST_ASSERT_FALSE(atomic_load(&zigbeeService.closing));
    TEST_ASSERT_EQUAL(locks, mocked_xSemaphoreTake_fake.call_count);
    TEST_ASSERT_EQUAL(0, xTaskCreateStatic_fake.call_count);
}
void test_ZigbeeStartedWithoutOwnerCannotClaimSuccessfulSdkStop(void) {
    IZigbee *radio = Zigbee_Create();
    TEST_ASSERT_EQUAL(0, radio->base.Initialize(&radio->base, NULL));
    zigbeeService.currentState = SERVICE_STATE_STARTED;
    TEST_ASSERT_NULL(zigbeeService.workerTask);
    TEST_ASSERT_EQUAL(ZIGBEE_ERROR_STACK_FAIL, radio->base.Stop(&radio->base));
    TEST_ASSERT_EQUAL(SERVICE_STATE_ERROR, zigbeeService.currentState);
    TEST_ASSERT_TRUE(atomic_load(&zigbeeService.closing));
    TEST_ASSERT_EQUAL(0, esp_zigbee_stop_fake.call_count);
    TEST_ASSERT_EQUAL(0, esp_zigbee_deinit_fake.call_count);
}
static void acknowledge_and_return(TaskHandle_t task) {
    TEST_ASSERT_NULL(task);
    TEST_ASSERT_TRUE(atomic_load(&zigbeeService.workerExited));
    longjmp(worker_return, 1);
}
static esp_err_t sdk_discards_radio_enable(const esp_zigbee_config_t *config) {
    (void)config; __wrap_esp_ieee802154_enable(); return ESP_OK;
}
static esp_err_t sdk_discards_radio_disable(void) {
    TEST_ASSERT_EQUAL(1, ezb_af_free_device_desc_fake.call_count);
    TEST_ASSERT_NULL(zigbeeService.device);
    __wrap_esp_ieee802154_disable(); return ESP_OK;
}
static esp_err_t sdk_registered_deinit_failure(void) {
    TEST_ASSERT_EQUAL(0, ezb_af_free_device_desc_fake.call_count);
    TEST_ASSERT_EQUAL_PTR((void *)1, zigbeeService.device);
    TEST_ASSERT_TRUE(atomic_load(&zigbeeService.mainloopExited));
    TEST_ASSERT_EQUAL(0, atomic_load(&zigbeeService.sdkReaders));
    return ESP_FAIL;
}
static esp_err_t sdk_requires_unregistered_descriptor_released(void) {
    TEST_ASSERT_EQUAL(1, ezb_af_free_device_desc_fake.call_count);
    TEST_ASSERT_EQUAL_PTR((void *)1, ezb_af_free_device_desc_fake.arg0_val);
    TEST_ASSERT_NULL(zigbeeService.device);
    TEST_ASSERT_TRUE(atomic_load(&zigbeeService.mainloopExited));
    TEST_ASSERT_EQUAL(0, atomic_load(&zigbeeService.sdkReaders));
    return ESP_OK;
}
void test_ZigbeeOwnerRejectsHiddenRadioInitializationFailure(void) {
    esp_pm_lock_create_fake.custom_fake = create_init_pm_guard;
    nativeRadioEnableResult = ESP_FAIL;
    esp_zigbee_init_fake.custom_fake = sdk_discards_radio_enable;
    mocked_vTaskSuspend_fake.custom_fake = acknowledge_and_return;
    if (!setjmp(worker_return)) esp_zb_task(NULL);
    TEST_ASSERT_EQUAL(ESP_FAIL, atomic_load(&zigbeeService.startupResult));
    TEST_ASSERT_EQUAL(0, mocked_ezb_af_create_device_desc_fake.call_count);
    TEST_ASSERT_EQUAL(1, esp_zigbee_deinit_fake.call_count);
    TEST_ASSERT_EQUAL(0, esp_zigbee_launch_mainloop_fake.call_count);
}
void test_ZigbeeOwnerReleasesUnregisteredAfTreeDespiteRadioCleanupFailure(void) {
    esp_pm_lock_create_fake.custom_fake = create_init_pm_guard;
    mocked_ezb_af_create_device_desc_fake.return_val = (void *)1;
    mocked_ezb_af_device_desc_register_fake.return_val = EZB_ERR_INV_ARG;
    nativeRadioDisableResult = ESP_FAIL;
    esp_zigbee_deinit_fake.custom_fake = sdk_discards_radio_disable;
    mocked_vTaskSuspend_fake.custom_fake = acknowledge_and_return;
    if (!setjmp(worker_return)) esp_zb_task(NULL);
    TEST_ASSERT_EQUAL(ESP_FAIL, atomic_load(&zigbeeService.cleanupResult));
    TEST_ASSERT_EQUAL(1, esp_zigbee_deinit_fake.call_count);
    TEST_ASSERT_EQUAL(1, ezb_af_free_device_desc_fake.call_count);
    TEST_ASSERT_NULL(zigbeeService.device);
    TEST_ASSERT_TRUE(atomic_load(&zigbeeService.closing));
    TEST_ASSERT_FALSE(zigbeeService.sdkInitialized);
}
void test_ZigbeeOwnerInvalidatesRegisteredAfHandleOnSdkDeinitFailureWithoutDoubleFree(void) {
    esp_pm_lock_create_fake.custom_fake = create_init_pm_guard;
    mocked_ezb_af_create_device_desc_fake.return_val = (void *)1;
    mocked_ezb_dev_start_fake.return_val = EZB_ERR_INV_STATE;
    esp_zigbee_deinit_fake.custom_fake = sdk_registered_deinit_failure;
    mocked_vTaskSuspend_fake.custom_fake = acknowledge_and_return;
    if (!setjmp(worker_return)) esp_zb_task(NULL);
    TEST_ASSERT_EQUAL(ESP_FAIL, atomic_load(&zigbeeService.cleanupResult));
    TEST_ASSERT_EQUAL(1, esp_zigbee_deinit_fake.call_count);
    TEST_ASSERT_EQUAL(0, ezb_af_free_device_desc_fake.call_count);
    TEST_ASSERT_NULL(zigbeeService.device);
    TEST_ASSERT_FALSE(zigbeeService.sdkInitialized);
}
void test_ZigbeeOwnerFreesUnregisteredTreeEvenWhenSdkDeinitFails(void) {
    esp_pm_lock_create_fake.custom_fake = create_init_pm_guard;
    mocked_ezb_af_create_device_desc_fake.return_val = (void *)1;
    mocked_ezb_af_device_desc_register_fake.return_val = EZB_ERR_INV_ARG;
    esp_zigbee_deinit_fake.return_val = ESP_FAIL;
    mocked_vTaskSuspend_fake.custom_fake = acknowledge_and_return;
    if (!setjmp(worker_return)) esp_zb_task(NULL);
    TEST_ASSERT_EQUAL(ESP_FAIL, atomic_load(&zigbeeService.cleanupResult));
    TEST_ASSERT_EQUAL(1, esp_zigbee_deinit_fake.call_count);
    TEST_ASSERT_EQUAL(1, ezb_af_free_device_desc_fake.call_count);
    TEST_ASSERT_NULL(zigbeeService.device);
    TEST_ASSERT_FALSE(zigbeeService.sdkInitialized);
}
void test_Zigbee_StorageFailurePropagatesWithoutEraseOrSdkAdmission(void) {
    nvs_flash_init_partition_fake.return_val = ESP_ERR_NVS_NO_FREE_PAGES;
    mocked_vTaskSuspend_fake.custom_fake = acknowledge_and_return;
    if (!setjmp(worker_return)) esp_zb_task(NULL);
    TEST_ASSERT_EQUAL(ESP_ERR_NVS_NO_FREE_PAGES, atomic_load(&zigbeeService.startupResult));
    TEST_ASSERT_TRUE(atomic_load(&zigbeeService.startupComplete));
    TEST_ASSERT_TRUE(atomic_load(&zigbeeService.closing));
    TEST_ASSERT_EQUAL(0, nvs_flash_erase_partition_fake.call_count);
    TEST_ASSERT_EQUAL(0, esp_zigbee_init_fake.call_count);
    TEST_ASSERT_EQUAL(0, esp_zigbee_deinit_fake.call_count);
}
void test_Zigbee_DescriptorFailureRunsOwnedSdkAndPowerRollback(void) {
    esp_pm_lock_create_fake.custom_fake = create_init_pm_guard;
    mocked_ezb_af_create_device_desc_fake.return_val = NULL;
    mocked_vTaskSuspend_fake.custom_fake = acknowledge_and_return;
    if (!setjmp(worker_return)) esp_zb_task(NULL);
    TEST_ASSERT_EQUAL(ESP_ERR_NO_MEM, atomic_load(&zigbeeService.startupResult));
    TEST_ASSERT_EQUAL(1, esp_zigbee_init_fake.call_count);
    TEST_ASSERT_EQUAL(1, esp_zigbee_deinit_fake.call_count);
    TEST_ASSERT_EQUAL(0, esp_zigbee_launch_mainloop_fake.call_count);
    TEST_ASSERT_EQUAL(1, esp_pm_lock_release_fake.call_count);
    TEST_ASSERT_EQUAL(1, esp_pm_lock_delete_fake.call_count);
    TEST_ASSERT_EQUAL(ESP_OK, atomic_load(&zigbeeService.cleanupResult));
}

static esp_err_t rejected_endpoint(IZigbeeEndpoint *self, ezb_af_device_desc_t device) {
    (void)self; TEST_ASSERT_EQUAL_PTR((void *)1, device); return ESP_ERR_NO_MEM;
}
void test_Zigbee_EndpointFailurePreventsDeviceStartupAndUnwindsOwner(void) {
    esp_pm_lock_create_fake.custom_fake = create_init_pm_guard;
    mocked_ezb_af_create_device_desc_fake.return_val = (void *)1;
    endpoints[0].RegisterClusters = rejected_endpoint;
    zigbeeService.endpointList = &endpoints[0];
    mocked_vTaskSuspend_fake.custom_fake = acknowledge_and_return;
    if (!setjmp(worker_return)) esp_zb_task(NULL);
    TEST_ASSERT_EQUAL(ESP_ERR_NO_MEM, atomic_load(&zigbeeService.startupResult));
    TEST_ASSERT_EQUAL(0, mocked_ezb_af_device_desc_register_fake.call_count);
    TEST_ASSERT_EQUAL(0, mocked_ezb_dev_start_fake.call_count);
    TEST_ASSERT_EQUAL(1, esp_zigbee_deinit_fake.call_count);
    TEST_ASSERT_EQUAL(1, ezb_af_free_device_desc_fake.call_count);
    TEST_ASSERT_EQUAL_PTR((void *)1, ezb_af_free_device_desc_fake.arg0_val);
    TEST_ASSERT_NULL(zigbeeService.device);
}
void test_Zigbee_DeviceRegistrationFailurePreventsLoopAndReclaimsUnregisteredDescriptor(void) {
    esp_pm_lock_create_fake.custom_fake = create_init_pm_guard;
    mocked_ezb_af_create_device_desc_fake.return_val = (void *)1;
    mocked_ezb_af_device_desc_register_fake.return_val = EZB_ERR_INV_ARG;
    esp_zigbee_deinit_fake.custom_fake = sdk_requires_unregistered_descriptor_released;
    mocked_vTaskSuspend_fake.custom_fake = acknowledge_and_return;
    if (!setjmp(worker_return)) esp_zb_task(NULL);
    TEST_ASSERT_EQUAL(ESP_FAIL, atomic_load(&zigbeeService.startupResult));
    TEST_ASSERT_EQUAL(0, mocked_ezb_dev_start_fake.call_count);
    TEST_ASSERT_EQUAL(0, esp_zigbee_launch_mainloop_fake.call_count);
    TEST_ASSERT_EQUAL(1, esp_zigbee_deinit_fake.call_count);
    TEST_ASSERT_EQUAL(1, ezb_af_free_device_desc_fake.call_count);
    TEST_ASSERT_NULL(zigbeeService.device);
}
void test_Zigbee_ConsoleFailureRetriesWithoutCreatingAnotherSdkOwner(void) {
    IZigbee *radio = Zigbee_Create();
    TEST_ASSERT_EQUAL(0, radio->base.Initialize(&radio->base, NULL));
    Zigbee_RegisterConsoleCommands_fake.return_val = ESP_ERR_NO_MEM;
    TEST_ASSERT_EQUAL(ZIGBEE_ERROR_STACK_FAIL, radio->base.Start(&radio->base));
    TEST_ASSERT_EQUAL_PTR((TaskHandle_t)2, zigbeeService.workerTask);
    TEST_ASSERT_EQUAL(1, xTaskCreateStatic_fake.call_count);
    Zigbee_RegisterConsoleCommands_fake.return_val = ESP_OK;
    TEST_ASSERT_EQUAL(0, radio->base.Start(&radio->base));
    TEST_ASSERT_EQUAL(1, xTaskCreateStatic_fake.call_count);
    TEST_ASSERT_EQUAL(2, Zigbee_RegisterConsoleCommands_fake.call_count);
}

void test_Zigbee_ReportingCacheResetTouchesOnlyDerivedReportingDataset(void) {
    TEST_ASSERT_EQUAL(ESP_OK, ResetDerivedReportingCache());
    TEST_ASSERT_EQUAL_HEX16(EZB_DATASETS_KEY_ZCL_REPORTING_INFO, ezb_plat_datasets_delete_fake.arg0_val);
    TEST_ASSERT_EQUAL(-1, ezb_plat_datasets_delete_fake.arg1_val);
    TEST_ASSERT_EQUAL(1, ezb_plat_datasets_delete_fake.call_count);
    TEST_ASSERT_EQUAL(0, nvs_flash_erase_partition_fake.call_count);
    ezb_plat_datasets_delete_fake.return_val = EZB_ERR_NOT_FOUND;
    TEST_ASSERT_EQUAL(ESP_OK, ResetDerivedReportingCache());
    ezb_plat_datasets_delete_fake.return_val = EZB_ERR_NO_MEM;
    TEST_ASSERT_EQUAL(ESP_FAIL, ResetDerivedReportingCache());
}

void test_Zigbee_ReportingCacheFailurePreventsSdkRegistrationAndUnwindsOwner(void) {
    esp_pm_lock_create_fake.custom_fake = create_init_pm_guard;
    mocked_ezb_af_create_device_desc_fake.return_val = (void *)1;
    ezb_plat_datasets_delete_fake.return_val = EZB_ERR_NO_MEM;
    esp_zigbee_deinit_fake.custom_fake = sdk_requires_unregistered_descriptor_released;
    mocked_vTaskSuspend_fake.custom_fake = acknowledge_and_return;
    if (!setjmp(worker_return)) esp_zb_task(NULL);
    TEST_ASSERT_EQUAL(ESP_FAIL, atomic_load(&zigbeeService.startupResult));
    TEST_ASSERT_EQUAL(0, mocked_ezb_af_device_desc_register_fake.call_count);
    TEST_ASSERT_EQUAL(0, mocked_ezb_dev_start_fake.call_count);
    TEST_ASSERT_EQUAL(1, esp_zigbee_deinit_fake.call_count);
    TEST_ASSERT_EQUAL(1, ezb_af_free_device_desc_fake.call_count);
    TEST_ASSERT_NULL(zigbeeService.device);
}

static IZigbee *prepare_connected_notification(void) {
    IZigbee *radio = Zigbee_Create();
    TEST_ASSERT_EQUAL(0, radio->base.Initialize(&radio->base, NULL));
    TEST_ASSERT_EQUAL(0, radio->base.Start(&radio->base));
    SetConnected(true);
    endpoints[0].OnConnected = record_connected;
    endpoints[0].ConfigureReporting = record_reporting;
    connected_calls = reporting_calls = 0;
    RESET_FAKE(mocked_xSemaphoreTake);
    mocked_xSemaphoreTake_fake.return_val = pdTRUE;
    return radio;
}

void test_Zigbee_ConnectedNotificationSurvivesServiceLockContention(void) {
    IZigbee *radio = prepare_connected_notification();
    mocked_xSemaphoreTake_fake.return_val = pdFALSE;
    NotifyEndpointsConnected(radio);
    TEST_ASSERT_EQUAL(0, mocked_xSemaphoreTake_fake.call_count);
    ProcessReportingWork();
    TEST_ASSERT_EQUAL(0, mocked_xSemaphoreTake_fake.arg1_val);
    TEST_ASSERT_EQUAL(0, connected_calls);
    TEST_ASSERT_TRUE(atomic_load(&zigbeeService.connectionNotificationPending));
    mocked_xSemaphoreTake_fake.return_val = pdTRUE;
    ProcessReportingWork();
    TEST_ASSERT_EQUAL(1, connected_calls);
    TEST_ASSERT_EQUAL(1, reporting_calls);
    TEST_ASSERT_FALSE(atomic_load(&zigbeeService.connectionNotificationPending));
    ProcessReportingWork();
    TEST_ASSERT_EQUAL(1, connected_calls);
}

void test_Zigbee_ConnectedNotificationSurvivesSdkLockContention(void) {
    IZigbee *radio = prepare_connected_notification();
    NotifyEndpointsConnected(radio);
    esp_zigbee_lock_acquire_fake.return_val = false;
    ProcessReportingWork();
    TEST_ASSERT_EQUAL(0, connected_calls);
    TEST_ASSERT_TRUE(atomic_load(&zigbeeService.connectionNotificationPending));
    esp_zigbee_lock_acquire_fake.return_val = true;
    ProcessReportingWork();
    TEST_ASSERT_EQUAL(1, connected_calls);
    TEST_ASSERT_EQUAL(1, reporting_calls);
}

void test_Zigbee_DisconnectCancelsUndeliveredConnectedNotification(void) {
    IZigbee *radio = prepare_connected_notification();
    NotifyEndpointsConnected(radio);
    SetConnected(false);
    ProcessReportingWork();
    TEST_ASSERT_EQUAL(0, connected_calls);
    TEST_ASSERT_FALSE(atomic_load(&zigbeeService.connectionNotificationPending));
    SetConnected(true);
    ProcessReportingWork();
    TEST_ASSERT_EQUAL(0, connected_calls);
    NotifyEndpointsConnected(radio);
    ProcessReportingWork();
    TEST_ASSERT_EQUAL(1, connected_calls);
}

void test_Zigbee_TransientReportBackoffPreservesEdgeAcrossClockWrap(void) {
    IZigbee *radio = prepare_attribute_queue();
    mocked_ezb_zcl_attr_desc_get_type_fake.return_val = EZB_ZCL_ATTR_TYPE_BOOL;
    mocked_ezb_zcl_set_attr_value_fake.custom_fake = record_fan_edge;
    fan_edge_count = 0; fan_report_request_fake.custom_fake = capture_fan_report;
    fan_report_request_fake.return_val = EZB_ERR_NO_MEM;
    int64_t start = ((int64_t)UINT32_MAX - 50) * 1000;
    mocked_esp_timer_get_time_fake.return_val = start;
    uint8_t edge = 1;
    TEST_ASSERT_EQUAL(0, radio->QueueAttributeUpdate(radio, 1, 6, 0, &edge, 1));
    ProcessReportingWork();
    TEST_ASSERT_EQUAL(1, fan_report_request_fake.call_count);
    TEST_ASSERT_EQUAL_UINT32(100000, mocked_esp_timer_start_once_fake.arg1_val);
    for (unsigned ms = 20; ms < 100; ms += 20) {
        mocked_esp_timer_get_time_fake.return_val = start + (int64_t)ms * 1000;
        ProcessReportingWork();
        TEST_ASSERT_EQUAL(1, fan_report_request_fake.call_count);
        TEST_ASSERT_EQUAL(1, zigbeeService.attributeCount);
        TEST_ASSERT_EQUAL(0, zigbeeService.attributeDropped);
    }
    mocked_esp_timer_get_time_fake.return_val = start + 100000;
    ProcessReportingWork(); TEST_ASSERT_EQUAL(2, fan_report_request_fake.call_count);
    fan_report_request_fake.return_val = EZB_ERR_NONE;
    mocked_esp_timer_get_time_fake.return_val = start + 150000;
    ProcessReportingWork(); TEST_ASSERT_EQUAL(2, fan_report_request_fake.call_count);
    mocked_esp_timer_get_time_fake.return_val = start + 300000;
    ProcessReportingWork(); TEST_ASSERT_EQUAL(3, fan_report_request_fake.call_count);
    TEST_ASSERT_EQUAL(0, zigbeeService.attributeCount);
    TEST_ASSERT_EQUAL(0, zigbeeService.attributeDropped);
    TEST_ASSERT_EQUAL(1, zigbeeService.attributeApplied);
}

static ezb_mem_config_t captured_memory_config;
static ezb_err_t capture_memory_config(const ezb_mem_config_t *config) {
    captured_memory_config = *config;
    return memory_configure_request_fake.return_val;
}
void test_Zigbee_PacketMemoryFailureUnwindsBeforeEndpointRegistration(void) {
    esp_pm_lock_create_fake.custom_fake = create_init_pm_guard;
    memory_configure_request_fake.custom_fake = capture_memory_config;
    memory_configure_request_fake.return_val = EZB_ERR_NO_MEM;
    mocked_vTaskSuspend_fake.custom_fake = acknowledge_and_return;
    if (!setjmp(worker_return)) esp_zb_task(NULL);
    TEST_ASSERT_EQUAL(ZIGBEE_PACKET_BUFFER_CAPACITY, captured_memory_config.buffer_pool_size);
    TEST_ASSERT_EQUAL(0, captured_memory_config.address_table_size);
    TEST_ASSERT_EQUAL(0, captured_memory_config.neighbor_table_size);
    TEST_ASSERT_EQUAL(0, captured_memory_config.route_table_size);
    TEST_ASSERT_EQUAL(0, captured_memory_config.route_discovery_table_size);
    TEST_ASSERT_EQUAL(0, captured_memory_config.route_record_table_size);
    TEST_ASSERT_EQUAL(0, captured_memory_config.aps_key_pair_set_size);
    TEST_ASSERT_EQUAL(ZIGBEE_BINDING_TABLE_CAPACITY, captured_memory_config.aps_bind_table_src_size);
    TEST_ASSERT_EQUAL(ZIGBEE_BINDING_TABLE_CAPACITY, captured_memory_config.aps_bind_table_dst_size);
    TEST_ASSERT_EQUAL(ESP_ERR_NO_MEM, atomic_load(&zigbeeService.startupResult));
    TEST_ASSERT_EQUAL(0, mocked_ezb_af_create_device_desc_fake.call_count);
    TEST_ASSERT_EQUAL(0, mocked_ezb_af_device_desc_register_fake.call_count);
    TEST_ASSERT_EQUAL(0, mocked_ezb_dev_start_fake.call_count);
    TEST_ASSERT_EQUAL(1, esp_zigbee_deinit_fake.call_count);
}


void test_ReportingTimerOnlyCoalescesWakeupWithoutSdkOrEndpointWork(void) {
    IZigbee *radio=reporting_failure_fixture();
    TEST_ASSERT_EQUAL(0,radio->ReconfigureAllReporting(radio));
    unsigned locks=esp_zigbee_lock_acquire_fake.call_count;
    RESET_FAKE(mocked_xQueueSend);mocked_xQueueSend_fake.return_val=pdFALSE;
    ReportingTimerCallback(NULL);
    TEST_ASSERT_EQUAL(1,mocked_xQueueSend_fake.call_count);
    TEST_ASSERT_EQUAL_PTR(zigbeeService.reportingQueue,mocked_xQueueSend_fake.arg0_val);
    TEST_ASSERT_EQUAL(0,mocked_xQueueSend_fake.arg2_val);
    TEST_ASSERT_EQUAL(locks,esp_zigbee_lock_acquire_fake.call_count);
    TEST_ASSERT_EQUAL(0,reporting_calls);
    TEST_ASSERT_TRUE(atomic_load(&zigbeeService.reportingPending));
    atomic_store(&zigbeeService.closing,true);ReportingTimerCallback(NULL);
    TEST_ASSERT_EQUAL(1,mocked_xQueueSend_fake.call_count);
}
void test_ReportingWorkerCreationFailureNeverStartsRadioAndRetryWorks(void) {
    IZigbee *radio=Zigbee_Create();TEST_ASSERT_EQUAL(0,radio->base.Initialize(&radio->base,NULL));
    mocked_xTaskCreate_fake.custom_fake=NULL;mocked_xTaskCreate_fake.return_val=pdFAIL;
    TEST_ASSERT_EQUAL(ZIGBEE_ERROR_STACK_FAIL,radio->base.Start(&radio->base));
    TEST_ASSERT_NULL(zigbeeService.reportingWorkerTask);TEST_ASSERT_NULL(zigbeeService.workerTask);
    TEST_ASSERT_EQUAL(0,xTaskCreateStatic_fake.call_count);
    mocked_xTaskCreate_fake.custom_fake=create_reporting_worker;
    TEST_ASSERT_EQUAL(0,radio->base.Start(&radio->base));
    TEST_ASSERT_EQUAL_PTR((TaskHandle_t)5,zigbeeService.reportingWorkerTask);
}
void test_ReportingWorkerStopTimeoutPreservesBothOwnersUntilRetry(void) {
    IZigbee *radio=Zigbee_Create();TEST_ASSERT_EQUAL(0,radio->base.Initialize(&radio->base,NULL));
    TEST_ASSERT_EQUAL(0,radio->base.Start(&radio->base));reportingWorkerCanExit=false;
    TEST_ASSERT_EQUAL(ZIGBEE_ERROR_TIMEOUT,radio->base.Stop(&radio->base));
    TEST_ASSERT_EQUAL(0,radio_worker_deletes);TEST_ASSERT_EQUAL(0,report_worker_deletes);
    TEST_ASSERT_EQUAL_PTR((TaskHandle_t)5,zigbeeService.reportingWorkerTask);
    TEST_ASSERT_EQUAL_PTR((TaskHandle_t)2,zigbeeService.workerTask);
    TEST_ASSERT_TRUE(atomic_load(&zigbeeService.closing));
    TEST_ASSERT_EQUAL(0,esp_zigbee_stop_fake.call_count);
    reportingWorkerCanExit=true;TEST_ASSERT_EQUAL(0,radio->base.Stop(&radio->base));
    TEST_ASSERT_EQUAL(1,radio_worker_deletes);TEST_ASSERT_EQUAL(1,report_worker_deletes);
    TEST_ASSERT_NULL(zigbeeService.reportingWorkerTask);
    unsigned sends=mocked_xQueueSend_fake.call_count;
    ReportingTimerCallback(NULL);TEST_ASSERT_EQUAL(sends,mocked_xQueueSend_fake.call_count);
}
void test_ReportingQueueAllocationFailureDoesNotPublishInitializedService(void) {
    IZigbee *radio=Zigbee_Create();mocked_xQueueCreate_fake.return_val=NULL;
    TEST_ASSERT_EQUAL(ZIGBEE_ERROR_STACK_FAIL,radio->base.Initialize(&radio->base,NULL));
    TEST_ASSERT_NOT_EQUAL(SERVICE_STATE_INITIALIZED,zigbeeService.currentState);
    TEST_ASSERT_NULL(zigbeeService.reportingQueue);TEST_ASSERT_NULL(zigbeeService.reportingWorkerTask);
    mocked_xQueueCreate_fake.return_val=(QueueHandle_t)4;
    TEST_ASSERT_EQUAL(0,radio->base.Initialize(&radio->base,NULL));
}
static jmp_buf reporting_loop_done;
static BaseType_t deliver_reporting_once(QueueHandle_t queue,void *out,TickType_t wait) {
    TEST_ASSERT_EQUAL_PTR(zigbeeService.reportingQueue,queue);TEST_ASSERT_GREATER_THAN(0,wait);
    *(uint8_t *)out=1;atomic_store(&zigbeeService.reportingWorkerStop,true);return pdTRUE;
}
static void reporting_park(TaskHandle_t task) {
    TEST_ASSERT_NULL(task);TEST_ASSERT_TRUE(atomic_load(&zigbeeService.reportingWorkerExited));
    TEST_ASSERT_EQUAL(0,atomic_load(&zigbeeService.sdkReaders));
    TEST_ASSERT_NULL(atomic_load(&zigbeeService.sdkAccessOwner));
    longjmp(reporting_loop_done,1);
}
void test_ReportingWorkerAppliesAdmittedWorkAndParksOutsideSdkOwnership(void) {
    IZigbee *radio=reporting_failure_fixture();TEST_ASSERT_EQUAL(0,radio->ReconfigureAllReporting(radio));
    mocked_xQueueReceive_fake.custom_fake=deliver_reporting_once;
    mocked_vTaskSuspend_fake.custom_fake=reporting_park;
    if(!setjmp(reporting_loop_done)) ReportingWorker(NULL);
    TEST_ASSERT_EQUAL(1,reporting_calls);TEST_ASSERT_FALSE(atomic_load(&zigbeeService.reportingPending));
}

static void delayed_sdk_ready(TickType_t ticks) {
    advance_worker(ticks);
    if(testTicks>=2500) {
        atomic_store(&zigbeeService.startupResult,ESP_OK);
        atomic_store(&zigbeeService.startupComplete,true);
        zigbeeService.currentState=SERVICE_STATE_STARTED;
    }
}
void test_ZigbeeBoundedStartFeedsRegisteredCallerWithoutChangingDeadlines(void) {
    IZigbee *radio=Zigbee_Create();TEST_ASSERT_EQUAL(0,radio->base.Initialize(&radio->base,NULL));
    completeStartup=false;startup_caller_watchdog_status_fake.return_val=ESP_OK;
    mocked_vTaskDelay_fake.custom_fake=delayed_sdk_ready;
    TEST_ASSERT_EQUAL(0,radio->base.Start(&radio->base));
    TEST_ASSERT_EQUAL(2,mocked_esp_task_wdt_reset_fake.call_count);
    TEST_ASSERT_EQUAL(2500,testTicks);
}
void test_ZigbeeCallerWatchdogFeedFailureRetainsOwnersAndRejectsStart(void) {
    IZigbee *radio=Zigbee_Create();TEST_ASSERT_EQUAL(0,radio->base.Initialize(&radio->base,NULL));
    completeStartup=false;startup_caller_watchdog_status_fake.return_val=ESP_OK;
    mocked_esp_task_wdt_reset_fake.return_val=ESP_FAIL;
    TEST_ASSERT_EQUAL(ZIGBEE_ERROR_STACK_FAIL,radio->base.Start(&radio->base));
    TEST_ASSERT_EQUAL(1,mocked_esp_task_wdt_reset_fake.call_count);
    TEST_ASSERT_TRUE(atomic_load(&zigbeeService.closing));
    TEST_ASSERT_EQUAL_PTR((TaskHandle_t)2,zigbeeService.workerTask);
    TEST_ASSERT_EQUAL_PTR((TaskHandle_t)5,zigbeeService.reportingWorkerTask);
    TEST_ASSERT_EQUAL(0,radio_worker_deletes);TEST_ASSERT_EQUAL(0,report_worker_deletes);
}
void test_ZigbeeFedCallerStillTimesOutWhenSdkNeverCompletes(void) {
    IZigbee *radio=Zigbee_Create();TEST_ASSERT_EQUAL(0,radio->base.Initialize(&radio->base,NULL));
    completeStartup=false;startup_caller_watchdog_status_fake.return_val=ESP_OK;
    TEST_ASSERT_EQUAL(ZIGBEE_ERROR_TIMEOUT,radio->base.Start(&radio->base));
    TEST_ASSERT_EQUAL(ZIGBEE_STARTUP_TIMEOUT_MS,testTicks);
    TEST_ASSERT_EQUAL(19,mocked_esp_task_wdt_reset_fake.call_count);
}
