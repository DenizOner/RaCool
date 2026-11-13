#ifndef RACOOL_TEST_NATIVE
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#endif
#include "Communication/Zigbee/Endpoints/EndpointGroups.h"
#include "Infrastructure/support/test_mocks.h"
#include "unity.h"
#include "../test_zigbee_mock_eventbus.h"
static unsigned freed_endpoints, freed_clusters;
static void count_freed_endpoint(ezb_af_ep_desc_t ep) { TEST_ASSERT_NOT_NULL(ep); freed_endpoints++; }
static void count_freed_cluster(ezb_zcl_cluster_desc_t cluster) { TEST_ASSERT_NOT_NULL(cluster); freed_clusters++; }
#include "Communication/Zigbee/Endpoints/EndpointGroups.c"

static esp_err_t display_save_result;
static unsigned display_saves;
static esp_err_t display_snapshot(IConfigurationManager *self, SystemSettings *out) {
    (void)self; *out = mock_settings; return ESP_OK;
}
static esp_err_t display_update(IConfigurationManager *self, const char *block,
        const ConfigurationFieldUpdate *fields, size_t count, bool persist) {
    (void)self;
    TEST_ASSERT_EQUAL_STRING("system", block); TEST_ASSERT_EQUAL(1, count); TEST_ASSERT_TRUE(persist);
    display_saves++;
    if (display_save_result == ESP_OK)
        memcpy((uint8_t *)&mock_settings.system + fields[0].offset, fields[0].data, fields[0].size);
    return display_save_result;
}
void setUp_EndpointGroups(void) {
    ZIGBEE_COMMON_FAKES(RESET_FAKE);
    mocked_ezb_af_free_endpoint_desc_fake.custom_fake = count_freed_endpoint;
    mocked_ezb_zcl_free_cluster_desc_fake.custom_fake = count_freed_cluster;
    freed_endpoints = freed_clusters = 0;
    mocked_ezb_af_create_endpoint_desc_fake.custom_fake = NULL;
    mocked_ezb_af_create_endpoint_desc_fake.return_val = (void *)0x100;
    mocked_ezb_zcl_groups_create_cluster_desc_fake.return_val = (void *)0x200;
    mocked_ezb_zcl_scenes_create_cluster_desc_fake.return_val = (void *)0x300;
    ConfigurationManager_GetInstance_fake.return_val = &mock_config_inst;
    mock_config_inst.GetSnapshot = display_snapshot;
    mock_config_inst.UpdateFields = display_update;
    memset(&mock_settings, 0, sizeof(mock_settings));
    mock_settings.system.oled_timeout_sec = 20;
    display_save_result = ESP_OK; display_saves = 0;
}

void tearDown_EndpointGroups(void) {}

void test_EndpointGroups_Identification(void) {
    IZigbeeEndpoint *ep = EndpointGroups_Create();
    TEST_ASSERT_EQUAL_STRING("OLEDDisplayEndpoint", ep->GetName(ep));
    TEST_ASSERT_EQUAL(ZIGBEE_EP_GROUPS, ep->GetEndpointId(ep));
}

void test_EndpointGroups_RegisterClusters(void) {
    IZigbeeEndpoint *ep = EndpointGroups_Create();
    ezb_af_device_desc_t mock_device = (void *)0x1234;
    
    ep->RegisterClusters(ep, mock_device);
    
    TEST_ASSERT_EQUAL(1, mocked_ezb_af_create_endpoint_desc_fake.call_count);
    TEST_ASSERT_EQUAL(1, ezb_zcl_thermostat_ui_config_create_cluster_desc_fake.call_count);
    TEST_ASSERT_EQUAL(2, ezb_zcl_cluster_desc_add_manuf_attr_fake.call_count);
    TEST_ASSERT_EQUAL(0x8020, ezb_zcl_cluster_desc_add_manuf_attr_fake.arg1_history[0]);
    TEST_ASSERT_EQUAL(0x8021, ezb_zcl_cluster_desc_add_manuf_attr_fake.arg1_history[1]);
    TEST_ASSERT_EQUAL(1, mocked_ezb_zcl_groups_create_cluster_desc_fake.call_count);
    TEST_ASSERT_EQUAL(1, mocked_ezb_zcl_scenes_create_cluster_desc_fake.call_count);
    TEST_ASSERT_EQUAL(1, mocked_ezb_af_device_desc_add_endpoint_fake.call_count);
    TEST_ASSERT_NULL(mocked_ezb_zcl_groups_create_cluster_desc_fake.arg0_val);
    TEST_ASSERT_NULL(mocked_ezb_zcl_scenes_create_cluster_desc_fake.arg0_val);
    TEST_ASSERT_EQUAL_PTR((void *)0x200, mocked_ezb_af_endpoint_add_cluster_desc_fake.arg1_history[2]);
    TEST_ASSERT_EQUAL_PTR((void *)0x300, mocked_ezb_af_endpoint_add_cluster_desc_fake.arg1_history[3]);
    TEST_ASSERT_EQUAL_PTR((void *)0x100, mocked_ezb_af_endpoint_add_cluster_desc_fake.arg0_history[2]);
    TEST_ASSERT_EQUAL(0, freed_endpoints);
}

void test_EndpointGroups_DisplayWriteValidation(void) {
    IZigbeeEndpoint *ep = EndpointGroups_Create();
    uint8_t mode = 2; esp_err_t result = ESP_FAIL;
    ezb_zcl_set_attr_value_message_t msg = {
        .info = {.dst_ep = 9, .cluster_id = EZB_ZCL_CLUSTER_ID_THERMOSTAT_UI_CONFIG},
        .in.attribute = {.id = 0x8020, .data = {.value = &mode, .size = 1, .type = EZB_ZCL_ATTR_TYPE_UINT8}}
    };
    TEST_ASSERT_TRUE(ep->HandleAction(ep, EZB_ZCL_CORE_SET_ATTR_VALUE_CB_ID, &msg, &result));
    TEST_ASSERT_EQUAL(ESP_OK, result); TEST_ASSERT_EQUAL(2, mock_settings.system.display_mode);
    mode = 3;
    ep->HandleAction(ep, EZB_ZCL_CORE_SET_ATTR_VALUE_CB_ID, &msg, &result);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, result); TEST_ASSERT_EQUAL(1, display_saves);
    mode = 0;
    ep->HandleAction(ep, EZB_ZCL_CORE_SET_ATTR_VALUE_CB_ID, &msg, &result);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, result); TEST_ASSERT_EQUAL(1, display_saves);
    mode = 1; display_save_result = ESP_FAIL;
    ep->HandleAction(ep, EZB_ZCL_CORE_SET_ATTR_VALUE_CB_ID, &msg, &result);
    TEST_ASSERT_EQUAL(ESP_FAIL, result); TEST_ASSERT_EQUAL(2, mock_settings.system.display_mode);
    uint16_t seconds = 60; display_save_result = ESP_OK;
    msg.in.attribute.id = 0x8021; msg.in.attribute.data.value = &seconds;
    msg.in.attribute.data.type = EZB_ZCL_ATTR_TYPE_UINT16; msg.in.attribute.data.size = 2;
    ep->HandleAction(ep, EZB_ZCL_CORE_SET_ATTR_VALUE_CB_ID, &msg, &result);
    TEST_ASSERT_EQUAL(ESP_OK, result); TEST_ASSERT_EQUAL(60, mock_settings.system.oled_timeout_sec);
    seconds = 4;
    ep->HandleAction(ep, EZB_ZCL_CORE_SET_ATTR_VALUE_CB_ID, &msg, &result);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, result); TEST_ASSERT_EQUAL(60, mock_settings.system.oled_timeout_sec);
    msg.info.dst_ep = 2;
    TEST_ASSERT_FALSE(ep->HandleAction(ep, EZB_ZCL_CORE_SET_ATTR_VALUE_CB_ID, &msg, &result));
}

void test_EndpointGroups_RegistrationFailures(void) {
    IZigbeeEndpoint *ep = EndpointGroups_Create();
    mocked_ezb_af_create_endpoint_desc_fake.return_val = NULL;
    ep->RegisterClusters(ep, (void *)1);
    TEST_ASSERT_EQUAL(0, mocked_ezb_zcl_groups_create_cluster_desc_fake.call_count);
    TEST_ASSERT_EQUAL(0, freed_endpoints);
    mocked_ezb_af_create_endpoint_desc_fake.return_val = (void *)0x100;
    mocked_ezb_zcl_groups_create_cluster_desc_fake.return_val = NULL;
    ep->RegisterClusters(ep, (void *)1);
    TEST_ASSERT_EQUAL(1, freed_endpoints);
    TEST_ASSERT_EQUAL(0, mocked_ezb_af_device_desc_add_endpoint_fake.call_count);
    mocked_ezb_zcl_groups_create_cluster_desc_fake.return_val = (void *)0x200;
    mocked_ezb_af_endpoint_add_cluster_desc_fake.return_val = -1;
    ep->RegisterClusters(ep, (void *)1);
    TEST_ASSERT_EQUAL(2, freed_endpoints);
    TEST_ASSERT_EQUAL(1, freed_clusters);
    mocked_ezb_af_endpoint_add_cluster_desc_fake.return_val = EZB_ERR_NONE;
    mocked_ezb_zcl_scenes_create_cluster_desc_fake.return_val = NULL;
    ep->RegisterClusters(ep, (void *)1);
    TEST_ASSERT_EQUAL(3, freed_endpoints);
    mocked_ezb_zcl_scenes_create_cluster_desc_fake.return_val = (void *)0x300;
    mocked_ezb_af_device_add_endpoint_desc_fake.return_val = -1;
    ep->RegisterClusters(ep, (void *)1);
    TEST_ASSERT_EQUAL(4, freed_endpoints);
    TEST_ASSERT_EQUAL(1, mocked_ezb_af_device_desc_add_endpoint_fake.call_count);
    RESET_FAKE(mocked_ezb_af_device_add_endpoint_desc);
    TEST_ASSERT_EQUAL(0, mocked_ezb_af_device_desc_add_endpoint_fake.call_count);
}

void test_EndpointGroups_UnitValidationAndPersistence(void) {
    IZigbeeEndpoint *ep = EndpointGroups_Create(); uint8_t unit = 1; esp_err_t result;
    ezb_zcl_set_attr_value_message_t msg = {
        .info = {.dst_ep = 9, .cluster_id = EZB_ZCL_CLUSTER_ID_THERMOSTAT_UI_CONFIG},
        .in.attribute = {.id = 0, .data = {.value = &unit, .size = 1, .type = EZB_ZCL_ATTR_TYPE_ENUM8}}
    };
    TEST_ASSERT_TRUE(ep->HandleAction(ep, EZB_ZCL_CORE_SET_ATTR_VALUE_CB_ID, &msg, &result));
    TEST_ASSERT_EQUAL(ESP_OK, result); TEST_ASSERT_EQUAL(1, mock_settings.system.temp_unit);
    unit = 2; ep->HandleAction(ep, EZB_ZCL_CORE_SET_ATTR_VALUE_CB_ID, &msg, &result);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, result);
    unit = 0; display_save_result = ESP_FAIL;
    ep->HandleAction(ep, EZB_ZCL_CORE_SET_ATTR_VALUE_CB_ID, &msg, &result);
    TEST_ASSERT_EQUAL(ESP_FAIL, result); TEST_ASSERT_EQUAL(1, mock_settings.system.temp_unit);
}

void test_EndpointGroups_All(void) {
    RUN_TEST(test_EndpointGroups_UnitValidationAndPersistence);
    RUN_TEST(test_EndpointGroups_RegistrationFailures);
    RUN_TEST(test_EndpointGroups_Identification);
    RUN_TEST(test_EndpointGroups_RegisterClusters);
    RUN_TEST(test_EndpointGroups_DisplayWriteValidation);
}

#ifndef RACOOL_TARGET_TESTING
int main(void) {
    UNITY_BEGIN();
    test_EndpointGroups_All();
    return UNITY_END();
}
#endif

