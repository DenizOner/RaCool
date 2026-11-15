#ifndef RACOOL_TEST_NATIVE
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#endif
#include "Communication/Zigbee/Endpoints/EndpointOTA.h"
#include "Infrastructure/support/test_mocks.h"
#include "unity.h"
static unsigned freed_endpoints, freed_clusters;
static void count_freed_endpoint(ezb_af_ep_desc_t ep) { TEST_ASSERT_NOT_NULL(ep); freed_endpoints++; }
static void count_freed_cluster(ezb_zcl_cluster_desc_t cluster) { TEST_ASSERT_NOT_NULL(cluster); freed_clusters++; }
#include "Controllers/SystemController/ISystemController.h"
#include "Communication/Zigbee/Endpoints/EndpointOTA.c"
static IConfigurationManager config;
static IPowerManager power;
static ISystemController system_controller;
static unsigned shutdown_requests;
static int32_t shutdown_result;
static int32_t request_shutdown(ISystemController *self, uint32_t delay) {
    (void)self; TEST_ASSERT_EQUAL(ZIGBEE_POWER_COMMAND_DELAY_MS, delay);
    shutdown_requests++; return shutdown_result;
}
static unsigned profile_writes;
static PowerMode_t requested_mode;
static int32_t profile_result;
static IEventBus otaEventBus;
static unsigned otaErrorEvents;
static unsigned otaCompleteEvents;
static int32_t recordOtaEvent(BusEvent event) {
    if (event.eventName && !strcmp(event.eventName, "ota_error")) otaErrorEvents++;
    if (event.eventName && !strcmp(event.eventName, "ota_complete")) otaCompleteEvents++;
    return 0;
}
static esp_err_t snapshot(IConfigurationManager *self, SystemSettings *out) {
    (void)self; memset(out, 0, sizeof(*out)); out->system.power_profile = 1; return ESP_OK;
}
static int32_t set_profile(IPowerManager *self, PowerMode_t mode) {
    (void)self; profile_writes++; requested_mode = mode; return profile_result;
}
static IService *lookup(const char *name) {
    if (!strcmp(name, SVC_NAME_SYSTEM_CONTROLLER)) return (IService *)&system_controller;
    return strcmp(name, SVC_NAME_POWER_MANAGER) == 0 ? (IService *)&power : NULL;
}

void setUp_EndpointOTA(void) {
    ZIGBEE_COMMON_FAKES(RESET_FAKE);
    mocked_ezb_af_free_endpoint_desc_fake.custom_fake = count_freed_endpoint;
    mocked_ezb_zcl_free_cluster_desc_fake.custom_fake = count_freed_cluster;
    system_controller = (ISystemController){.RequestShutdown = request_shutdown};
    shutdown_requests = 0; shutdown_result = ESP_OK;
    memset(&config, 0, sizeof(config)); memset(&power, 0, sizeof(power));
    config.GetSnapshot = snapshot; power.SetPowerMode = set_profile;
    ConfigurationManager_GetInstance_fake.return_val = &config;
    ServiceLocator_GetService_fake.custom_fake = lookup;
    mocked_ezb_af_create_endpoint_desc_fake.return_val = (void *)0x100;
    mocked_ezb_zcl_ota_upgrade_create_cluster_desc_fake.return_val = (void *)0x200;
    ezb_zcl_thermostat_ui_config_create_cluster_desc_fake.return_val = (void *)0x300;
    profile_writes = freed_endpoints = freed_clusters = 0; profile_result = POWER_OK;
    otaEventBus = (IEventBus){.Publish = recordOtaEvent};
    otaErrorEvents = otaCompleteEvents = 0;
    mocked_ServiceLocator_GetEventBus_fake.return_val = &otaEventBus;
}

void tearDown_EndpointOTA(void) {}

void test_EndpointOTA_Identification(void) {
    IZigbeeEndpoint *ep = EndpointOTA_Create();
    TEST_ASSERT_EQUAL_STRING("OTAUpgradeEndpoint", ep->GetName(ep));
}

void test_EndpointOTA_RegisterClusters(void) {
    IZigbeeEndpoint *ep = EndpointOTA_Create();
    ezb_af_device_desc_t mock_device = (void *)0x1111;
    
    ep->RegisterClusters(ep, mock_device);
    
    TEST_ASSERT_EQUAL(1, mocked_ezb_af_create_endpoint_desc_fake.call_count);
    TEST_ASSERT_EQUAL(1, mocked_ezb_zcl_ota_upgrade_create_cluster_desc_fake.call_count);
    TEST_ASSERT_EQUAL(1, mocked_ezb_af_device_desc_add_endpoint_fake.call_count);
    TEST_ASSERT_EQUAL(2, mocked_ezb_af_endpoint_add_cluster_desc_fake.call_count);
    TEST_ASSERT_EQUAL(3, ezb_zcl_cluster_desc_add_manuf_attr_fake.call_count);
    TEST_ASSERT_EQUAL(ZIGBEE_ATTR_POWER_PROFILE, ezb_zcl_cluster_desc_add_manuf_attr_fake.arg1_history[0]);
    TEST_ASSERT_EQUAL(ZIGBEE_ATTR_TX_PROFILE, ezb_zcl_cluster_desc_add_manuf_attr_fake.arg1_history[1]);
    TEST_ASSERT_EQUAL(ZIGBEE_ATTR_SYSTEM_COMMAND, ezb_zcl_cluster_desc_add_manuf_attr_fake.arg1_history[2]);
    TEST_ASSERT_EQUAL(0, freed_endpoints);
}

void test_EndpointOTA_HandleAction_Progress(void) {
    IZigbeeEndpoint *ep = EndpointOTA_Create();
    ezb_zcl_ota_upgrade_client_progress_message_t messages[] = {
        {.in = {.progress = EZB_ZCL_OTA_UPGRADE_PROGRESS_START}},
        {.in = {.progress = EZB_ZCL_OTA_UPGRADE_PROGRESS_RECEIVING}},
        {.in = {.progress = EZB_ZCL_OTA_UPGRADE_PROGRESS_CHECK}},
        {.in = {.progress = EZB_ZCL_OTA_UPGRADE_PROGRESS_APPLY}},
        {.in = {.progress = EZB_ZCL_OTA_UPGRADE_PROGRESS_FINISH}},
    };

    /* No flash writer is registered: all transfer phases must fail closed,
     * even if progress is received out of order or the counters are stale. */
    for (size_t i = 0; i < sizeof(messages) / sizeof(messages[0]); ++i) {
        esp_err_t status = ESP_OK;
        otaInstance.state = OTA_STATE_DOWNLOADING;
        otaInstance.bytesReceived = UINT32_MAX;
        otaInstance.totalSize = 500000;
        messages[i].out.result = EZB_ZCL_STATUS_SUCCESS;
        TEST_ASSERT_TRUE(ep->HandleAction(ep, EZB_ZCL_CORE_OTA_UPGRADE_CLIENT_PROGRESS_CB_ID,
                                          &messages[i], &status));
        TEST_ASSERT_EQUAL(ESP_ERR_NOT_SUPPORTED, status);
        TEST_ASSERT_EQUAL(EZB_ZCL_STATUS_ABORT, messages[i].out.result);
        TEST_ASSERT_EQUAL(OTA_STATE_ERROR, otaInstance.state);
        TEST_ASSERT_EQUAL_UINT32(0, otaInstance.bytesReceived);
        TEST_ASSERT_EQUAL_UINT32(0, otaInstance.totalSize);
        TEST_ASSERT_EQUAL(i + 1u, otaErrorEvents);
        TEST_ASSERT_EQUAL(0, otaCompleteEvents);
    }
}

void test_EndpointOTA_AbortAndUnknownProgress(void) {
    IZigbeeEndpoint *ep = EndpointOTA_Create();
    ezb_zcl_ota_upgrade_client_progress_message_t msg = {
        .in = {.progress = EZB_ZCL_OTA_UPGRADE_PROGRESS_ABORT}
    };
    otaInstance.state = OTA_STATE_DOWNLOADING;
    otaInstance.bytesReceived = 1024;
    otaInstance.totalSize = 500000;
    esp_err_t status = ESP_FAIL;
    TEST_ASSERT_TRUE(ep->HandleAction(ep, EZB_ZCL_CORE_OTA_UPGRADE_CLIENT_PROGRESS_CB_ID, &msg, &status));
    TEST_ASSERT_EQUAL(ESP_OK, status);
    /* Successful cancellation acknowledgment is NOT a successful update. */
    TEST_ASSERT_EQUAL(EZB_ZCL_STATUS_SUCCESS, msg.out.result);
    TEST_ASSERT_EQUAL(OTA_STATE_ERROR, otaInstance.state);
    TEST_ASSERT_EQUAL_UINT32(0, otaInstance.bytesReceived);
    TEST_ASSERT_EQUAL_UINT32(0, otaInstance.totalSize);

    msg.in.progress = 0xff;
    msg.out.result = EZB_ZCL_STATUS_SUCCESS;
    status = ESP_OK;
    TEST_ASSERT_TRUE(ep->HandleAction(ep, EZB_ZCL_CORE_OTA_UPGRADE_CLIENT_PROGRESS_CB_ID, &msg, &status));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, status);
    TEST_ASSERT_EQUAL(EZB_ZCL_STATUS_ABORT, msg.out.result);
    TEST_ASSERT_EQUAL(OTA_STATE_ERROR, otaInstance.state);
    TEST_ASSERT_EQUAL(1, otaErrorEvents);
    TEST_ASSERT_EQUAL(0, otaCompleteEvents);
}

void test_EndpointOTA_ProfileValidationAndErrors(void) {
    IZigbeeEndpoint *ep = EndpointOTA_Create();
    uint8_t value = 0; esp_err_t result = ESP_FAIL;
    ezb_zcl_set_attr_value_message_t msg = {
        .info = {.dst_ep = ZIGBEE_EP_SYSTEM, .cluster_id = EZB_ZCL_CLUSTER_ID_THERMOSTAT_UI_CONFIG},
        .in.attribute = {.id = ZIGBEE_ATTR_POWER_PROFILE,
            .data = {.value = &value, .size = 1, .type = EZB_ZCL_ATTR_TYPE_UINT8}}
    };
    for (value = 0; value < POWER_PROFILE_COUNT; value++) {
        TEST_ASSERT_TRUE(ep->HandleAction(ep, EZB_ZCL_CORE_SET_ATTR_VALUE_CB_ID, &msg, &result));
        TEST_ASSERT_EQUAL(ESP_OK, result); TEST_ASSERT_EQUAL(value, requested_mode);
    }
    TEST_ASSERT_EQUAL(3, profile_writes);
    ep->HandleAction(ep, EZB_ZCL_CORE_SET_ATTR_VALUE_CB_ID, &msg, &result);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, result); TEST_ASSERT_EQUAL(3, profile_writes);
    value = 1; msg.in.attribute.data.size = 2;
    ep->HandleAction(ep, EZB_ZCL_CORE_SET_ATTR_VALUE_CB_ID, &msg, &result);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, result); TEST_ASSERT_EQUAL(3, profile_writes);
    msg.in.attribute.data.size = 1; msg.in.attribute.data.type = EZB_ZCL_ATTR_TYPE_BOOL;
    ep->HandleAction(ep, EZB_ZCL_CORE_SET_ATTR_VALUE_CB_ID, &msg, &result);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, result); TEST_ASSERT_EQUAL(3, profile_writes);
    msg.in.attribute.data.type = EZB_ZCL_ATTR_TYPE_UINT8; profile_result = POWER_ERR_INVALID_STATE;
    ep->HandleAction(ep, EZB_ZCL_CORE_SET_ATTR_VALUE_CB_ID, &msg, &result);
    TEST_ASSERT_EQUAL(ESP_FAIL, result);
    power.SetPowerMode = NULL;
    ep->HandleAction(ep, EZB_ZCL_CORE_SET_ATTR_VALUE_CB_ID, &msg, &result);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, result);
    msg.info.dst_ep = 1;
    TEST_ASSERT_FALSE(ep->HandleAction(ep, EZB_ZCL_CORE_SET_ATTR_VALUE_CB_ID, &msg, &result));
    TEST_ASSERT_FALSE(ep->HandleAction(ep, EZB_ZCL_CORE_SET_ATTR_VALUE_CB_ID, NULL, &result));
}
void test_EndpointOTA_RegistrationFailurePreservesOwnership(void) {
    IZigbeeEndpoint *ep = EndpointOTA_Create();
    ezb_zcl_thermostat_ui_config_create_cluster_desc_fake.return_val = NULL;
    ep->RegisterClusters(ep, (void *)1);
    TEST_ASSERT_EQUAL(1, freed_endpoints); TEST_ASSERT_EQUAL(0, freed_clusters);
    TEST_ASSERT_EQUAL(0, mocked_ezb_af_device_desc_add_endpoint_fake.call_count);
    ezb_zcl_thermostat_ui_config_create_cluster_desc_fake.return_val = (void *)0x300;
    ezb_zcl_cluster_desc_add_manuf_attr_fake.return_val = 1;
    ep->RegisterClusters(ep, (void *)1);
    TEST_ASSERT_EQUAL(2, freed_endpoints); TEST_ASSERT_EQUAL(1, freed_clusters);
    TEST_ASSERT_EQUAL(0, mocked_ezb_af_device_desc_add_endpoint_fake.call_count);
}

void test_EndpointOTA_SystemCommandsValidateAndShareQueueErrors(void) {
    IZigbeeEndpoint *ep = EndpointOTA_Create();
    uint8_t command = 2;
    ezb_zcl_set_attr_value_message_t message = {
        .info = {.dst_ep = ZIGBEE_EP_SYSTEM, .cluster_id = EZB_ZCL_CLUSTER_ID_THERMOSTAT_UI_CONFIG},
        .in.attribute = {.id = ZIGBEE_ATTR_SYSTEM_COMMAND,
            .data = {.type = EZB_ZCL_ATTR_TYPE_UINT8, .size = 1, .value = &command}}
    };
    esp_err_t result = ESP_FAIL;
    TEST_ASSERT_TRUE(ep->HandleAction(ep, EZB_ZCL_CORE_SET_ATTR_VALUE_CB_ID, &message, &result));
    TEST_ASSERT_EQUAL(ESP_OK, result); TEST_ASSERT_EQUAL(1, shutdown_requests);
    command = 3; shutdown_result = ESP_ERR_NO_MEM;
    ep->HandleAction(ep, EZB_ZCL_CORE_SET_ATTR_VALUE_CB_ID, &message, &result);
    TEST_ASSERT_EQUAL(ESP_ERR_NO_MEM, result); TEST_ASSERT_EQUAL(2, shutdown_requests);
    message.in.attribute.data.size = 2;
    ep->HandleAction(ep, EZB_ZCL_CORE_SET_ATTR_VALUE_CB_ID, &message, &result);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, result); TEST_ASSERT_EQUAL(2, shutdown_requests);
    message.info.dst_ep = ZIGBEE_EP_SENSOR_FAN;
    TEST_ASSERT_FALSE(ep->HandleAction(ep, EZB_ZCL_CORE_SET_ATTR_VALUE_CB_ID, &message, &result));
}

void test_EndpointOTA_All(void) {
    RUN_TEST(test_EndpointOTA_SystemCommandsValidateAndShareQueueErrors);
    RUN_TEST(test_EndpointOTA_ProfileValidationAndErrors);
    RUN_TEST(test_EndpointOTA_RegistrationFailurePreservesOwnership);
    RUN_TEST(test_EndpointOTA_Identification);
    RUN_TEST(test_EndpointOTA_RegisterClusters);
    RUN_TEST(test_EndpointOTA_HandleAction_Progress);
    RUN_TEST(test_EndpointOTA_AbortAndUnknownProgress);
}

#ifndef RACOOL_TARGET_TESTING
int main(void) {
    UNITY_BEGIN();
    test_EndpointOTA_All();
    return UNITY_END();
}
#endif
