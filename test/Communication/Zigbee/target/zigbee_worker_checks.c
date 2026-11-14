#include "unity.h"
#define RACOOL_INTERNAL_TEST_ACCESS
#define ezb_config_memory TargetZigbee_ConfigureMemory
#define esp_zigbee_init TargetZigbee_Init
#define esp_zigbee_deinit TargetZigbee_Deinit
#define esp_zigbee_stop TargetZigbee_Stop
#define esp_zigbee_task_queue_post TargetZigbee_Post
#define esp_zigbee_launch_mainloop TargetZigbee_Mainloop
#define esp_zigbee_lock_acquire TargetZigbee_Lock
#define esp_zigbee_lock_release TargetZigbee_Unlock
#define ezb_af_create_device_desc TargetZigbee_CreateDevice
#define ezb_af_device_desc_register TargetZigbee_RegisterDevice
#define ezb_af_free_device_desc TargetZigbee_FreeDevice
#define ezb_zcl_core_action_handler_register TargetZigbee_RegisterActions
#define ezb_nwk_set_device_type TargetZigbee_SetDeviceType
#define ezb_nwk_set_max_children TargetZigbee_SetChildren
#define ezb_bdb_set_primary_channel_set TargetZigbee_SetPrimary
#define ezb_bdb_set_secondary_channel_set TargetZigbee_SetSecondary
#define ezb_dev_start TargetZigbee_StartDevice
#define ezb_set_tx_power TargetZigbee_SetPower
#define ezb_get_tx_power TargetZigbee_GetPower
#define ezb_zcl_get_attr_desc TargetZigbee_GetAttribute
#define ezb_zcl_attr_desc_get_type TargetZigbee_GetAttributeType
#define ezb_zcl_attr_desc_get_value TargetZigbee_GetAttributeValue
#define ezb_zcl_set_attr_value TargetZigbee_SetAttribute
#define GetEventBus TargetZigbee_GetEventBus
#define ConfigurationManager_GetInstance TargetZigbee_GetConfig
#include "../../../../src/Communication/Zigbee/Zigbee.c"

/* Production worker/lifecycle, FreeRTOS locks, static task storage and PM guard
 * are real. The injected SDK boundary models a blocked mainloop; it neither
 * joins nor resets a physical Zigbee network. */
static SemaphoreHandle_t sdk_lock, loop_release;
static atomic_bool sdk_locked, loop_entered, sdk_active, boundary_violation;
static esp_zigbee_callback_t queued_sdk_callback;
static void *queued_sdk_context;
static atomic_uint sdk_post_calls;
static atomic_uint deinit_calls, freed_devices;
static atomic_uint attribute_calls, transient_failures;
static atomic_bool permanent_failure;
static bool resources_owned;
static TaskHandle_t sdk_initializer;
static IZigbeeEndpoint target_endpoints[10];
static IZigbeeSignalHandler target_signal;

static bool check_boundary(bool condition) {
    if (!condition) atomic_store(&boundary_violation, true);
    return condition;
}

esp_err_t TargetZigbee_Init(const esp_zigbee_config_t *config) {
    if (!check_boundary(config != NULL)) return ESP_ERR_INVALID_ARG;
    sdk_initializer = xTaskGetCurrentTaskHandle();
    atomic_store(&sdk_active, true);
    return ESP_OK;
}
esp_err_t TargetZigbee_Deinit(void) {
    if (!check_boundary(sdk_initializer == xTaskGetCurrentTaskHandle() &&
        !atomic_load(&sdk_locked) && atomic_load(&zigbeeService.mainloopExited) &&
        atomic_load(&zigbeeService.cleanupAllowed) && !atomic_load(&zigbeeService.sdkReaders) &&
        atomic_load(&freed_devices) == atomic_load(&deinit_calls) + 1)) return ESP_FAIL;
    atomic_fetch_add(&deinit_calls, 1);
    atomic_store(&sdk_active, false);
    return ESP_OK;
}
esp_err_t TargetZigbee_Mainloop(void) {
    atomic_store(&loop_entered, true);
    xSemaphoreTake(loop_release, portMAX_DELAY);
    if (!check_boundary(xSemaphoreTake(sdk_lock, portMAX_DELAY) == pdTRUE)) return ESP_FAIL;
    atomic_store(&sdk_locked, true);
    if (queued_sdk_callback) {
        esp_zigbee_callback_t callback = queued_sdk_callback;
        void *context = queued_sdk_context;
        queued_sdk_callback = NULL;
        queued_sdk_context = NULL;
        callback(context);
    }
    atomic_store(&sdk_locked, false);
    xSemaphoreGive(sdk_lock);
    return ESP_OK;
}
esp_err_t TargetZigbee_Post(esp_zigbee_callback_t callback, void *context) {
    if (!check_boundary(atomic_load(&sdk_locked) && callback && !queued_sdk_callback))
        return ESP_ERR_INVALID_STATE;
    queued_sdk_callback = callback;
    queued_sdk_context = context;
    atomic_fetch_add(&sdk_post_calls, 1);
    return ESP_OK;
}
esp_err_t TargetZigbee_Stop(void) {
    if (!check_boundary(atomic_load(&sdk_locked))) return ESP_ERR_INVALID_STATE;
    /* The test retains the mainloop until it explicitly releases this wait. */
    return ESP_OK;
}
bool TargetZigbee_Lock(TickType_t timeout) {
    if (xSemaphoreTake(sdk_lock, timeout) != pdTRUE) return false;
    atomic_store(&sdk_locked, true);
    return true;
}
void TargetZigbee_Unlock(void) {
    atomic_store(&sdk_locked, false);
    xSemaphoreGive(sdk_lock);
}
ezb_err_t TargetZigbee_ConfigureMemory(const ezb_mem_config_t *config) {
    return check_boundary(atomic_load(&sdk_active) && config &&
        config->buffer_pool_size == ZIGBEE_PACKET_BUFFER_CAPACITY &&
        sdk_initializer == xTaskGetCurrentTaskHandle()) ? EZB_ERR_NONE : EZB_ERR_INV_STATE;
}

ezb_af_device_desc_t TargetZigbee_CreateDevice(void) { return (ezb_af_device_desc_t)1; }
ezb_err_t TargetZigbee_RegisterDevice(ezb_af_device_desc_t device) {
    return check_boundary(device != NULL) ? EZB_ERR_NONE : EZB_ERR_INV_ARG;
}
void TargetZigbee_FreeDevice(ezb_af_device_desc_t device) {
    check_boundary(device != NULL && atomic_load(&sdk_active) &&
                   atomic_load(&zigbeeService.mainloopExited) &&
                   atomic_load(&zigbeeService.cleanupAllowed) &&
                   !atomic_load(&zigbeeService.sdkReaders));
    atomic_fetch_add(&freed_devices, 1);
}
void TargetZigbee_RegisterActions(ezb_zcl_core_action_callback_t callback) { (void)callback; }
ezb_err_t TargetZigbee_SetDeviceType(ezb_nwk_device_type_t type) { (void)type; return EZB_ERR_NONE; }
ezb_err_t TargetZigbee_SetChildren(uint8_t count) { (void)count; return EZB_ERR_NONE; }
ezb_err_t TargetZigbee_SetPrimary(uint32_t channels) { (void)channels; return EZB_ERR_NONE; }
ezb_err_t TargetZigbee_SetSecondary(uint32_t channels) { (void)channels; return EZB_ERR_NONE; }
ezb_err_t TargetZigbee_StartDevice(bool autostart) { (void)autostart; return EZB_ERR_NONE; }
static int8_t target_tx_power;
static uint8_t target_saved_profile = DEFAULT_ZIGBEE_TX_PROFILE;
static esp_err_t target_tx_snapshot(IConfigurationManager *self, SystemSettings *settings) {
    (void)self; memset(settings, 0, sizeof(*settings));
    settings->system.zigbee_tx_profile = target_saved_profile;
    return ESP_OK;
}
static esp_err_t target_tx_persist(IConfigurationManager *self, const char *block,
    const ConfigurationFieldUpdate *fields, size_t count, bool persist) {
    (void)self;
    if (!check_boundary(!atomic_load(&sdk_locked) && !atomic_load(&zigbeeService.sdkReaders) &&
        strcmp(block,"system") == 0 && count == 1 && persist &&
        fields[0].offset == offsetof(SystemBaseConfigs,zigbee_tx_profile) && fields[0].size == 1)) return ESP_FAIL;
    target_saved_profile = *(const uint8_t *)fields[0].data;
    return ESP_OK;
}
static IConfigurationManager target_radio_config = {.GetSnapshot=target_tx_snapshot,.UpdateFields=target_tx_persist};
IConfigurationManager *TargetZigbee_GetConfig(void) { return &target_radio_config; }
void TargetZigbee_SetPower(int8_t power) { target_tx_power = power; }
void TargetZigbee_GetPower(int8_t *power) { *power = target_tx_power; }
IEventBus *TargetZigbee_GetEventBus(void) { return NULL; }
ezb_zcl_attr_desc_t TargetZigbee_GetAttribute(uint8_t endpoint, uint16_t cluster,
    uint8_t role, uint16_t attribute, uint16_t manufacturer) {
    (void)endpoint; (void)cluster; (void)role; (void)attribute; (void)manufacturer;
    check_boundary(atomic_load(&sdk_locked));
    return (ezb_zcl_attr_desc_t)1;
}
ezb_zcl_attr_type_t TargetZigbee_GetAttributeType(ezb_zcl_attr_desc_t descriptor) {
    check_boundary(descriptor != NULL && atomic_load(&sdk_locked));
    return EZB_ZCL_ATTR_TYPE_UINT16;
}
ezb_err_t TargetZigbee_GetAttributeValue(ezb_zcl_attr_desc_t descriptor, void *value) {
    check_boundary(descriptor != NULL && value != NULL && atomic_load(&sdk_locked));
    /* The injected descriptor has no backing storage; force an explicit report. */
    return EZB_ERR_FAIL;
}
ezb_zcl_status_t TargetZigbee_SetAttribute(uint8_t endpoint, uint16_t cluster, uint8_t role,
    uint16_t attribute, uint16_t manufacturer, void *value, bool check) {
    (void)endpoint; (void)cluster; (void)role; (void)attribute; (void)manufacturer; (void)check;
    check_boundary(value != NULL && atomic_load(&sdk_locked));
    atomic_fetch_add(&attribute_calls, 1);
    if (atomic_load(&permanent_failure)) return EZB_ZCL_STATUS_INVALID_VALUE;
    if (atomic_load(&transient_failures)) {
        atomic_fetch_sub(&transient_failures, 1);
        return EZB_ZCL_STATUS_FAIL;
    }
    return EZB_ZCL_STATUS_SUCCESS;
}
int32_t ClimateZigbeePlugin_Init(void) { return 0; }
int32_t SystemZigbeePlugin_Init(void) { return 0; }
esp_err_t Zigbee_RegisterConsoleCommands(void) { return ESP_OK; }
void Zigbee_ConsoleSdkStopped(void) { check_boundary(!atomic_load(&sdk_active)); }
static esp_err_t target_register_endpoint(IZigbeeEndpoint *self, ezb_af_device_desc_t device) {
    return check_boundary(self != NULL && device != NULL) ? ESP_OK : ESP_ERR_INVALID_ARG;
}
static atomic_uint reporting_calls, reporting_failures;
static atomic_uint connected_calls;
static esp_err_t target_connected(IZigbeeEndpoint *self) {
    check_boundary(self != NULL && atomic_load(&sdk_locked));
    atomic_fetch_add(&connected_calls, 1);
    return ESP_OK;
}
static esp_err_t target_reporting(IZigbeeEndpoint *self) {
    check_boundary(self != NULL && atomic_load(&sdk_locked));
    atomic_fetch_add(&reporting_calls, 1);
    if (atomic_load(&reporting_failures)) {
        atomic_fetch_sub(&reporting_failures, 1);
        return ESP_FAIL;
    }
    return ESP_OK;
}
#define TARGET_ENDPOINT_FACTORY(name, index) \
    IZigbeeEndpoint *name(void) { \
        target_endpoints[index].RegisterClusters = target_register_endpoint; \
        return &target_endpoints[index]; \
    }
TARGET_ENDPOINT_FACTORY(EndpointFan_Create, 0)
TARGET_ENDPOINT_FACTORY(EndpointAmbientTemp_Create, 1)
TARGET_ENDPOINT_FACTORY(EndpointCPUTemp_Create, 2)
TARGET_ENDPOINT_FACTORY(EndpointInternalLED_Create, 3)
TARGET_ENDPOINT_FACTORY(EndpointRGBLED_Create, 4)
TARGET_ENDPOINT_FACTORY(EndpointHealth_Create, 5)
TARGET_ENDPOINT_FACTORY(EndpointMemory_Create, 6)
TARGET_ENDPOINT_FACTORY(EndpointUptime_Create, 7)
TARGET_ENDPOINT_FACTORY(EndpointGroups_Create, 8)
TARGET_ENDPOINT_FACTORY(EndpointOTA_Create, 9)
IZigbeeSignalHandler *SignalHandlerCore_Create(void) { return &target_signal; }

void target_zigbee_worker_cleanup(void) {
    if (!resources_owned) return;
    xSemaphoreGive(loop_release);
    if (atomic_load(&zigbeeService.sdkAccessOwner) == xTaskGetCurrentTaskHandle())
        TEST_ASSERT_EQUAL(0, Zigbee_ReleaseSdkAccess(&zigbeeService.base));
    TEST_ASSERT_EQUAL(0, Zigbee_Stop(&zigbeeService.base.base));
    TEST_ASSERT_NULL(zigbeeService.workerTask);
    vSemaphoreDelete(loop_release); loop_release = NULL;
    vSemaphoreDelete(sdk_lock); sdk_lock = NULL;
    resources_owned = false;
}

void target_zigbee_worker_verify_shutdown(void) {
    sdk_lock = xSemaphoreCreateMutex();
    loop_release = xSemaphoreCreateBinary();
    TEST_ASSERT_NOT_NULL(sdk_lock); TEST_ASSERT_NOT_NULL(loop_release);
    resources_owned = true;
    atomic_store(&loop_entered, false);
    atomic_store(&boundary_violation, false);
    atomic_store(&sdk_locked, false);
    atomic_store(&deinit_calls, 0);
    atomic_store(&freed_devices, 0);
    atomic_store(&sdk_post_calls, 0);
    queued_sdk_callback = NULL; queued_sdk_context = NULL;
    IZigbee *radio = Zigbee_Create();
    TEST_ASSERT_EQUAL(0, radio->base.Initialize(&radio->base, NULL));
    TEST_ASSERT_EQUAL(0, radio->base.Start(&radio->base));
    for (unsigned i = 0; i < 100 && !atomic_load(&loop_entered); ++i) vTaskDelay(1);
    TEST_ASSERT_TRUE(atomic_load(&loop_entered));
    for (unsigned mode = 0; mode < ZIGBEE_TX_PROFILE_COUNT; ++mode) {
        TEST_ASSERT_EQUAL(ESP_OK, radio->SetTxProfile(radio, (ZigbeeTxProfile)mode));
        ZigbeeTxStatus status;
        TEST_ASSERT_EQUAL(ESP_OK, radio->GetTxStatus(radio, &status));
        TEST_ASSERT_EQUAL(mode, status.profile);
        TEST_ASSERT_EQUAL(mode * 10, status.dbm);
    }
    atomic_store(&attribute_calls, 0);
    atomic_store(&transient_failures, 2);
    atomic_store(&permanent_failure, false);
    SetConnected(true);
    uint16_t attribute_value = 0x1234;
    TEST_ASSERT_EQUAL(0, radio->QueueAttributeUpdate(radio, 1, 0x402, 0, &attribute_value, sizeof(attribute_value)));
    ZigbeeAttributeStatus queue_status;
    for (unsigned i = 0; i < 100; ++i) {
        TEST_ASSERT_EQUAL(0, radio->GetAttributeStatus(radio, &queue_status));
        if (!queue_status.pending) break;
        vTaskDelay(1);
    }
    TEST_ASSERT_EQUAL(0, queue_status.pending);
    TEST_ASSERT_EQUAL(3, atomic_load(&attribute_calls));
    TEST_ASSERT_EQUAL(1, queue_status.applied);
    TEST_ASSERT_EQUAL(2, queue_status.failed);
    TEST_ASSERT_EQUAL(2, queue_status.retries);
    TEST_ASSERT_EQUAL(0, queue_status.dropped);
    atomic_store(&permanent_failure, true);
    TEST_ASSERT_EQUAL(0, radio->QueueAttributeUpdate(radio, 1, 0x402, 0, &attribute_value, sizeof(attribute_value)));
    for (unsigned i = 0; i < 100; ++i) {
        TEST_ASSERT_EQUAL(0, radio->GetAttributeStatus(radio, &queue_status));
        if (!queue_status.pending) break;
        vTaskDelay(1);
    }
    TEST_ASSERT_EQUAL(0, queue_status.pending);
    TEST_ASSERT_EQUAL(1, queue_status.dropped);
    TEST_ASSERT_EQUAL(2, queue_status.retries);
    vTaskDelay(10);
    TEST_ASSERT_EQUAL(4, atomic_load(&attribute_calls));
    TEST_ASSERT_TRUE(radio->TrySdkAccess(radio, 0));
    target_endpoints[0].ConfigureReporting = target_reporting;
    atomic_store(&reporting_calls, 0);
    atomic_store(&reporting_failures, 2);
    TEST_ASSERT_EQUAL(0, radio->ReleaseSdkAccess(radio));
    TEST_ASSERT_EQUAL(0, radio->ReconfigureAllReporting(radio));
    for (unsigned i = 0; i < 100 && atomic_load(&reporting_calls) < 3; ++i) vTaskDelay(1);
    TEST_ASSERT_EQUAL(3, atomic_load(&reporting_calls));
    vTaskDelay(3);
    TEST_ASSERT_FALSE(atomic_load(&zigbeeService.reportingPending));
    TEST_ASSERT_FALSE(atomic_load(&zigbeeService.reportingApplyFailed));
    target_endpoints[0].OnConnected = target_connected;
    atomic_store(&connected_calls, 0);
    atomic_store(&reporting_calls, 0);
    TEST_ASSERT_EQUAL(pdTRUE, xSemaphoreTake(zigbeeService.mutex, portMAX_DELAY));
    NotifyEndpointsConnected(radio);
    vTaskDelay(5);
    TEST_ASSERT_EQUAL(0, atomic_load(&connected_calls));
    TEST_ASSERT_EQUAL(0, atomic_load(&reporting_calls));
    TEST_ASSERT_TRUE(atomic_load(&zigbeeService.connectionNotificationPending));
    TEST_ASSERT_EQUAL(pdTRUE, xSemaphoreGive(zigbeeService.mutex));
    for (unsigned i = 0; i < 100 && !atomic_load(&connected_calls); ++i) vTaskDelay(1);
    TEST_ASSERT_EQUAL(1, atomic_load(&connected_calls));
    for (unsigned i = 0; i < 100 && !atomic_load(&reporting_calls); ++i) vTaskDelay(1);
    TEST_ASSERT_EQUAL(1, atomic_load(&reporting_calls));
    vTaskDelay(3);
    TEST_ASSERT_EQUAL(1, atomic_load(&connected_calls));
    SetConnected(false);
    TEST_ASSERT_TRUE(radio->TrySdkAccess(radio, 0));
    TEST_ASSERT_EQUAL(ZIGBEE_ERROR_LOCK_FAIL, radio->base.Stop(&radio->base));
    TEST_ASSERT_EQUAL(0, radio->ReleaseSdkAccess(radio));
    TaskHandle_t worker = zigbeeService.workerTask;
    TEST_ASSERT_EQUAL(ZIGBEE_ERROR_TIMEOUT, radio->base.Stop(&radio->base));
    TEST_ASSERT_EQUAL(1, atomic_load(&sdk_post_calls));
    TEST_ASSERT_EQUAL_PTR(worker, zigbeeService.workerTask);
    TEST_ASSERT_TRUE(atomic_load(&sdk_active));
    TEST_ASSERT_EQUAL(0, atomic_load(&deinit_calls));
    TEST_ASSERT_FALSE(radio->TrySdkAccess(radio, 0));
    TEST_ASSERT_NOT_EQUAL(0, radio->base.Start(&radio->base));
    xSemaphoreGive(loop_release);
    TEST_ASSERT_EQUAL(0, radio->base.Stop(&radio->base));
    TEST_ASSERT_EQUAL(1, atomic_load(&sdk_post_calls));
    TEST_ASSERT_NULL(zigbeeService.workerTask);
    TEST_ASSERT_EQUAL(1, atomic_load(&deinit_calls));
    TEST_ASSERT_EQUAL(1, atomic_load(&freed_devices));
    atomic_store(&loop_entered, false);
    TEST_ASSERT_EQUAL(0, radio->base.Start(&radio->base));
    for (unsigned i = 0; i < 100 && !atomic_load(&loop_entered); ++i) vTaskDelay(1);
    TEST_ASSERT_TRUE(atomic_load(&loop_entered));
    xSemaphoreGive(loop_release);
    TEST_ASSERT_EQUAL(0, radio->base.Stop(&radio->base));
    TEST_ASSERT_EQUAL(2, atomic_load(&deinit_calls));
    TEST_ASSERT_EQUAL(2, atomic_load(&freed_devices));
    TEST_ASSERT_FALSE(atomic_load(&boundary_violation));
}
