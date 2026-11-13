#include "../test_zigbee_shared.h"
#include "../test_zigbee_mock_eventbus.h"
#include "unity.h"
#include "Communication/Zigbee/Endpoints/EndpointCommon.h"
#include "Communication/Zigbee/Endpoints/EndpointFan.c"
#include "Communication/Zigbee/Endpoints/EndpointAmbientTemp.c"
#include "Communication/Zigbee/Endpoints/EndpointCPUTemp.c"
#include "Communication/Zigbee/Endpoints/EndpointHealth.c"
#include "Communication/Zigbee/Endpoints/EndpointMemory.c"
#include "Communication/Zigbee/Endpoints/EndpointUptime.c"
#include "Communication/Zigbee/Endpoints/EndpointInternalLED.c"
#include "Communication/Zigbee/Endpoints/EndpointRGBLED.c"
#include "Communication/Zigbee/Endpoints/EndpointGroups.c"
#include "Communication/Zigbee/Endpoints/EndpointOTA.c"

static IZigbeeEndpoint *(*const registrationFactories[])(void) = {
    EndpointFan_Create, EndpointAmbientTemp_Create, EndpointCPUTemp_Create,
    EndpointHealth_Create, EndpointMemory_Create, EndpointUptime_Create,
    EndpointInternalLED_Create, EndpointRGBLED_Create, EndpointGroups_Create, EndpointOTA_Create
};
void setUp(void) { ZIGBEE_COMMON_FAKES(RESET_FAKE); }
void tearDown(void) {}

static ezb_zcl_attr_desc_t nextControlAttribute(ezb_zcl_cluster_desc_t cluster, ezb_zcl_attr_desc_t previous) {
    (void)cluster;
    uintptr_t index = (uintptr_t)previous;
    return index < 3 ? (void *)(index + 1) : NULL;
}
static uint16_t controlAttributeId(ezb_zcl_attr_desc_t attribute) {
    return (uintptr_t)attribute == 3 ? 0x8040 : 0x8050;
}
static ezb_zcl_attr_access_t controlAttributeAccess(ezb_zcl_attr_desc_t attribute) {
    return (uintptr_t)attribute == 2 ? EZB_ZCL_ATTR_ACCESS_READ_ONLY : EZB_ZCL_ATTR_ACCESS_READ_WRITE;
}
void test_EndpointControlReportingMetadataPrecedesOwnershipTransfer(void) {
    mocked_ezb_zcl_cluster_desc_get_id_fake.return_val = EZB_ZCL_CLUSTER_ID_THERMOSTAT_UI_CONFIG;
    mocked_ezb_zcl_cluster_get_next_attr_desc_fake.custom_fake = nextControlAttribute;
    mocked_ezb_zcl_attr_desc_get_id_fake.custom_fake = controlAttributeId;
    mocked_ezb_zcl_attr_desc_get_access_fake.custom_fake = controlAttributeAccess;
    TEST_ASSERT_EQUAL(ESP_OK, ZigbeeEndpoint_AddCluster((void *)1, (void *)2));
    TEST_ASSERT_EQUAL(1, mocked_ezb_zcl_attr_desc_set_access_fake.call_count);
    TEST_ASSERT_EQUAL(EZB_ZCL_ATTR_ACCESS_READ_WRITE | EZB_ZCL_ATTR_ACCESS_REPORTING,
        mocked_ezb_zcl_attr_desc_set_access_fake.arg1_val);
    TEST_ASSERT_EQUAL(1, mocked_ezb_af_endpoint_add_cluster_desc_fake.call_count);
    mocked_ezb_zcl_attr_desc_set_access_fake.return_val = EZB_ERR_INV_ARG;
    TEST_ASSERT_EQUAL(ESP_FAIL, ZigbeeEndpoint_AddCluster((void *)1, (void *)2));
    TEST_ASSERT_EQUAL(1, mocked_ezb_af_endpoint_add_cluster_desc_fake.call_count);
    TEST_ASSERT_EQUAL(1, mocked_ezb_zcl_free_cluster_desc_fake.call_count);
    mocked_ezb_zcl_cluster_desc_get_id_fake.return_val = EZB_ZCL_CLUSTER_ID_IDENTIFY;
    unsigned before = mocked_ezb_zcl_attr_desc_set_access_fake.call_count;
    TEST_ASSERT_EQUAL(ESP_OK, ZigbeeEndpoint_AddCluster((void *)1, (void *)2));
    TEST_ASSERT_EQUAL(before, mocked_ezb_zcl_attr_desc_set_access_fake.call_count);
}

static unsigned tx_requests;
static esp_err_t tx_admission;
static int32_t capture_tx_request(ISystemController *self, uint8_t mode) {
    (void)self; ++tx_requests; TEST_ASSERT_LESS_THAN(ZIGBEE_TX_PROFILE_COUNT, mode); return tx_admission;
}
void test_SystemEndpointTxActionValidatesTypeAndUsesNonblockingRequest(void) {
    IZigbeeEndpoint *ep = EndpointOTA_Create();
    ISystemController system = {.RequestZigbeeTxProfile=capture_tx_request};
    mocked_ServiceLocator_GetService_fake.return_val = &system.base;
    uint8_t mode = ZIGBEE_TX_BOOST;
    ezb_zcl_set_attr_value_message_t message = {0};
    message.info.dst_ep = ZIGBEE_EP_SYSTEM;
    message.info.cluster_id = ZIGBEE_SYSTEM_CONFIG_CLUSTER;
    message.in.attribute.id = ZIGBEE_ATTR_TX_PROFILE;
    message.in.attribute.data.value = &mode;
    message.in.attribute.data.size = 1;
    message.in.attribute.data.type = EZB_ZCL_ATTR_TYPE_UINT8;
    esp_err_t result;
    tx_requests = 0; tx_admission = ESP_ERR_NO_MEM;
    TEST_ASSERT_TRUE(ep->HandleAction(ep,EZB_ZCL_CORE_SET_ATTR_VALUE_CB_ID,&message,&result));
    TEST_ASSERT_EQUAL(ESP_ERR_NO_MEM, result);
    TEST_ASSERT_EQUAL(1, tx_requests);
    tx_admission = ESP_OK;
    TEST_ASSERT_TRUE(ep->HandleAction(ep,EZB_ZCL_CORE_SET_ATTR_VALUE_CB_ID,&message,&result));
    TEST_ASSERT_EQUAL(ESP_OK, result);
    mode = ZIGBEE_TX_PROFILE_COUNT;
    TEST_ASSERT_TRUE(ep->HandleAction(ep,EZB_ZCL_CORE_SET_ATTR_VALUE_CB_ID,&message,&result));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, result);
    mode = 0; message.in.attribute.data.type = EZB_ZCL_ATTR_TYPE_ENUM8;
    TEST_ASSERT_TRUE(ep->HandleAction(ep,EZB_ZCL_CORE_SET_ATTR_VALUE_CB_ID,&message,&result));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, result);
    TEST_ASSERT_EQUAL(2, tx_requests);
}

static ezb_zcl_cluster_desc_t captureDisplayConfig(const void *config, uint8_t role) {
    const ezb_zcl_thermostat_ui_config_cluster_server_config_t *display = config;
    TEST_ASSERT_NOT_NULL(display);
    TEST_ASSERT_EQUAL(EZB_ZCL_CLUSTER_SERVER, role);
    TEST_ASSERT_EQUAL(0, display->keypad_lockout);
    TEST_ASSERT_EQUAL(mock_settings.system.temp_unit, display->temperature_display_mode);
    return (void *)0x300;
}

static ezb_err_t rejectDuplicateThermostatAttribute(ezb_zcl_cluster_desc_t cluster, uint16_t id, const void *value) {
    (void)cluster; (void)value;
    return id == EZB_ZCL_ATTR_THERMOSTAT_SYSTEM_MODE_ID ? EZB_ERR_INV_ARG : EZB_ERR_NONE;
}

static uint8_t expected_fan_mode;
static ezb_zcl_cluster_desc_t captureFanConfiguration(const void *config, uint8_t role) {
    const ezb_zcl_thermostat_cluster_server_config_t *fan = config;
    TEST_ASSERT_NOT_NULL(fan);
    TEST_ASSERT_EQUAL(EZB_ZCL_CLUSTER_SERVER, role);
    TEST_ASSERT_EQUAL(EZB_ZCL_THERMOSTAT_CONTROL_SEQUENCE_OF_OPERATION_COOLING_ONLY, fan->control_sequence_of_operation);
    TEST_ASSERT_EQUAL(expected_fan_mode, fan->system_mode);
    return (void *)0x300;
}
void test_EndpointFanFactoryUsesCoolingCompatiblePersistedModes(void) {
    SystemSettings original = mock_settings;
    const uint8_t modes[] = {0, 1, 3};
    mocked_ezb_zcl_thermostat_create_cluster_desc_fake.custom_fake = captureFanConfiguration;
    for (unsigned i = 0; i < 3; ++i) {
        mock_settings.control.fan_manual_mode = i != 1;
        mock_settings.control.fan_manual_state = i == 2;
        expected_fan_mode = modes[i];
        IZigbeeEndpoint *ep = EndpointFan_Create();
        TEST_ASSERT_EQUAL(ESP_OK, ep->RegisterClusters(ep, (void *)1));
    }
    mock_settings = original;
    TEST_ASSERT_EQUAL(3, mocked_ezb_zcl_thermostat_create_cluster_desc_fake.call_count);
}

void test_EndpointDisplayUsesFactoryConfigurationWithoutDuplicateMandatoryAttribute(void) {
    mocked_ezb_zcl_thermostat_ui_config_create_cluster_desc_fake.custom_fake = captureDisplayConfig;
    /* Adding the mandatory attribute again must never be attempted. */
    mocked_ezb_zcl_thermostat_ui_config_cluster_desc_add_attr_fake.return_val = EZB_ERR_INV_ARG;
    mocked_ezb_zcl_thermostat_cluster_desc_add_attr_fake.custom_fake = rejectDuplicateThermostatAttribute;
    uint8_t original = mock_settings.system.temp_unit;
    for (uint8_t unit = 0; unit <= 1; ++unit) {
        mock_settings.system.temp_unit = unit;
        IZigbeeEndpoint *ep = EndpointGroups_Create();
        TEST_ASSERT_EQUAL(ESP_OK, ep->RegisterClusters(ep, (void *)1));
        ep = EndpointFan_Create();
        TEST_ASSERT_EQUAL(ESP_OK, ep->RegisterClusters(ep, (void *)1));
    }
    mock_settings.system.temp_unit = original;
    TEST_ASSERT_EQUAL(0, mocked_ezb_zcl_thermostat_ui_config_cluster_desc_add_attr_fake.call_count);
}

void test_EndpointRegistrationAllTenRequireValidArgumentsAndAllocation(void) {
    for (unsigned i = 0; i < 10; ++i) {
        setUp();
        IZigbeeEndpoint *ep = registrationFactories[i]();
        TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, ep->RegisterClusters(NULL, (void *)1));
        TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, ep->RegisterClusters(ep, NULL));
        mocked_ezb_af_create_endpoint_desc_fake.custom_fake = NULL;
        mocked_ezb_af_create_endpoint_desc_fake.return_val = NULL;
        TEST_ASSERT_NOT_EQUAL(ESP_OK, ep->RegisterClusters(ep, (void *)1));
        TEST_ASSERT_EQUAL(0, mocked_ezb_af_device_add_endpoint_desc_fake.call_count);
        TEST_ASSERT_EQUAL(0, mocked_ezb_af_free_endpoint_desc_fake.call_count);
    }
}
void test_EndpointRegistrationCommonAllocationAndAttributeFailuresUnwind(void) {
    for (unsigned i = 0; i < 9; ++i) {
        /* Memory and uptime use dedicated analog/reporting clusters. */
        if (i == 4 || i == 5) continue;
        setUp();
        IZigbeeEndpoint *ep = registrationFactories[i]();
        mocked_ezb_zcl_basic_create_cluster_desc_fake.return_val = NULL;
        TEST_ASSERT_NOT_EQUAL_MESSAGE(ESP_OK, ep->RegisterClusters(ep, (void *)1), ep->GetName(ep));
        TEST_ASSERT_EQUAL(1, mocked_ezb_af_free_endpoint_desc_fake.call_count);
        TEST_ASSERT_EQUAL(0, mocked_ezb_af_device_add_endpoint_desc_fake.call_count);
        setUp();
        ep = registrationFactories[i]();
        mocked_ezb_zcl_basic_cluster_desc_add_attr_fake.return_val = EZB_ERR_INV_ARG;
        TEST_ASSERT_NOT_EQUAL(ESP_OK, ep->RegisterClusters(ep, (void *)1));
        TEST_ASSERT_EQUAL(1, mocked_ezb_zcl_free_cluster_desc_fake.call_count);
        TEST_ASSERT_EQUAL(1, mocked_ezb_af_free_endpoint_desc_fake.call_count);
        TEST_ASSERT_EQUAL(0, mocked_ezb_af_device_add_endpoint_desc_fake.call_count);
    }
}
void test_EndpointRegistrationAllTenRejectClusterAttachmentAndParentFailures(void) {
    for (unsigned i = 0; i < 10; ++i) {
        setUp();
        IZigbeeEndpoint *ep = registrationFactories[i]();
        mocked_ezb_af_endpoint_add_cluster_desc_fake.return_val = EZB_ERR_INV_ARG;
        TEST_ASSERT_NOT_EQUAL(ESP_OK, ep->RegisterClusters(ep, (void *)1));
        TEST_ASSERT_EQUAL(1, mocked_ezb_zcl_free_cluster_desc_fake.call_count);
        TEST_ASSERT_EQUAL(1, mocked_ezb_af_free_endpoint_desc_fake.call_count);
        TEST_ASSERT_EQUAL(0, mocked_ezb_af_device_add_endpoint_desc_fake.call_count);
        setUp();
        ep = registrationFactories[i]();
        mocked_ezb_af_device_add_endpoint_desc_fake.return_val = EZB_ERR_INV_ARG;
        TEST_ASSERT_NOT_EQUAL(ESP_OK, ep->RegisterClusters(ep, (void *)1));
        TEST_ASSERT_EQUAL(1, mocked_ezb_af_device_add_endpoint_desc_fake.call_count);
        TEST_ASSERT_EQUAL(1, mocked_ezb_af_free_endpoint_desc_fake.call_count);
    }
}
void test_EndpointRegistrationAllTenTransferOwnershipOnlyOnSuccess(void) {
    for (unsigned i = 0; i < 10; ++i) {
        setUp();
        IZigbeeEndpoint *ep = registrationFactories[i]();
        TEST_ASSERT_EQUAL(ESP_OK, ep->RegisterClusters(ep, (void *)1));
        TEST_ASSERT_EQUAL(1, mocked_ezb_af_device_add_endpoint_desc_fake.call_count);
        TEST_ASSERT_EQUAL(0, mocked_ezb_af_free_endpoint_desc_fake.call_count);
        TEST_ASSERT_EQUAL(0, mocked_ezb_zcl_free_cluster_desc_fake.call_count);
    }
}
