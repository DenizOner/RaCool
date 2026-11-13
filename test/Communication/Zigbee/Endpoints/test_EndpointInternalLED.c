#ifndef RACOOL_TEST_NATIVE
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#endif
#include "Communication/Zigbee/Endpoints/EndpointInternalLED.h"
#include "Infrastructure/support/test_mocks.h"
#include "unity.h"
#include "Communication/Zigbee/Endpoints/EndpointInternalLED.c"

#include "../test_zigbee_mock_eventbus.h"

static esp_err_t save_result;
static unsigned saves;
static esp_err_t snapshot(IConfigurationManager *self, SystemSettings *out) {
    (void)self; *out = mock_settings; return ESP_OK;
}
static esp_err_t update_fields(IConfigurationManager *self, const char *block,
        const ConfigurationFieldUpdate *fields, size_t count, bool persist) {
    (void)self;
    TEST_ASSERT_EQUAL_STRING("hardware", block);
    TEST_ASSERT_TRUE(persist);
    saves++;
    if (save_result != ESP_OK) return save_result;
    for (size_t i = 0; i < count; ++i)
        memcpy((uint8_t *)&mock_settings.hardware + fields[i].offset, fields[i].data, fields[i].size);
    return ESP_OK;
}

void setUp_EndpointInternalLED(void) {
    ZIGBEE_COMMON_FAKES(RESET_FAKE);
    mock_publish_called = 0;
    memset(&mock_settings, 0, sizeof(SystemSettings));
    mock_event_bus.Publish = mock_publish;
    ServiceLocator_GetEventBus_fake.return_val = &mock_event_bus;
    ConfigurationManager_GetInstance_fake.return_val = &mock_config_inst;
    mock_config_inst.GetSettings = mock_get_settings;
    mock_config_inst.GetSnapshot = snapshot;
    mock_config_inst.UpdateFields = update_fields;
    save_result = ESP_OK; saves = 0;
}

void tearDown_EndpointInternalLED(void) {}

void test_EndpointInternalLED_ReportingUsesUpdateWithoutRestart(void) {
    IZigbeeEndpoint *ep = EndpointInternalLED_Create();
    ezb_zcl_reporting_info_find_fake.return_val = (ezb_zcl_reporting_info_t)1;
    TEST_ASSERT_EQUAL(ESP_OK, ep->ConfigureReporting(ep));
    TEST_ASSERT_EQUAL(1, ezb_zcl_reporting_info_update_fake.call_count);
    TEST_ASSERT_EQUAL(0, ezb_zcl_reporting_start_attr_report_fake.call_count);
    ezb_zcl_reporting_info_update_fake.return_val = (ezb_err_t)1;
    TEST_ASSERT_EQUAL(ESP_FAIL, ep->ConfigureReporting(ep));
    TEST_ASSERT_EQUAL(0, ezb_zcl_reporting_start_attr_report_fake.call_count);
}

void test_EndpointInternalLED_Identification(void) {
    IZigbeeEndpoint *ep = EndpointInternalLED_Create();
    TEST_ASSERT_EQUAL_STRING("InternalLEDEndpoint", ep->GetName(ep));
}

void test_EndpointInternalLED_HandleAction(void) {
    IZigbeeEndpoint *ep = EndpointInternalLED_Create();
    esp_err_t status = ESP_FAIL;
    bool val = true;
    
    ezb_zcl_set_attr_value_message_t msg = {
        .info = { 
            .dst_ep = ZIGBEE_EP_INTERNAL_LED, 
            .cluster_id = EZB_ZCL_CLUSTER_ID_ON_OFF 
        },
        .in.attribute = { 
            .id = EZB_ZCL_ATTR_ON_OFF_ON_OFF_ID, 
            .data = { .value = &val, .size = 1, .type = 0x10 } 
        }
    };

    bool handled = ep->HandleAction(ep, EZB_ZCL_CORE_SET_ATTR_VALUE_CB_ID, &msg, &status);

    TEST_ASSERT_TRUE(handled);
    TEST_ASSERT_TRUE(mock_settings.hardware.builtin_led_state);
}

void test_EndpointInternalLED_BrightnessValidationAndFailure(void) {
    IZigbeeEndpoint *ep = EndpointInternalLED_Create();
    esp_err_t status = ESP_FAIL;
    uint8_t value = 0;
    ezb_zcl_set_attr_value_message_t msg = {
        .info = {.dst_ep = ZIGBEE_EP_INTERNAL_LED, .cluster_id = EZB_ZCL_CLUSTER_ID_ON_OFF},
        .in.attribute = {.id = 0x8004, .data = {.value = &value, .size = 1, .type = 0x20}}
    };
    TEST_ASSERT_TRUE(ep->HandleAction(ep, EZB_ZCL_CORE_SET_ATTR_VALUE_CB_ID, &msg, &status));
    TEST_ASSERT_EQUAL(ESP_OK, status);
    TEST_ASSERT_EQUAL(0, mock_settings.hardware.builtin_led_brightness);
    value = 255;
    ep->HandleAction(ep, EZB_ZCL_CORE_SET_ATTR_VALUE_CB_ID, &msg, &status);
    TEST_ASSERT_EQUAL(255, mock_settings.hardware.builtin_led_brightness);
    save_result = ESP_FAIL; value = 10;
    ep->HandleAction(ep, EZB_ZCL_CORE_SET_ATTR_VALUE_CB_ID, &msg, &status);
    TEST_ASSERT_EQUAL(ESP_FAIL, status);
    TEST_ASSERT_EQUAL(255, mock_settings.hardware.builtin_led_brightness);
    unsigned before = saves;
    msg.in.attribute.data.size = 2;
    ep->HandleAction(ep, EZB_ZCL_CORE_SET_ATTR_VALUE_CB_ID, &msg, &status);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, status);
    msg.in.attribute.data.size = 1; msg.in.attribute.data.type = 0x10;
    ep->HandleAction(ep, EZB_ZCL_CORE_SET_ATTR_VALUE_CB_ID, &msg, &status);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, status);
    msg.in.attribute.data.value = NULL;
    ep->HandleAction(ep, EZB_ZCL_CORE_SET_ATTR_VALUE_CB_ID, &msg, &status);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, status);
    TEST_ASSERT_EQUAL(before, saves);
    TEST_ASSERT_FALSE(ep->HandleAction(ep, EZB_ZCL_CORE_SET_ATTR_VALUE_CB_ID, NULL, &status));
}

void test_EndpointInternalLED_StartupSelectionIsIndependentAndValidated(void) {
    IZigbeeEndpoint *ep = EndpointInternalLED_Create();
    uint8_t selection = 255;
    mock_settings.hardware.ext_led_anim = RGBLED_ANIM_RAINBOW;
    esp_err_t status = ESP_FAIL;
    ezb_zcl_set_attr_value_message_t msg = {
        .info = {.dst_ep = ZIGBEE_EP_INTERNAL_LED, .cluster_id = EZB_ZCL_CLUSTER_ID_ON_OFF},
        .in.attribute = {.id = 0x8005, .data = {.value = &selection, .size = 1, .type = 0x20}}
    };
    TEST_ASSERT_TRUE(ep->HandleAction(ep,EZB_ZCL_CORE_SET_ATTR_VALUE_CB_ID,&msg,&status));
    TEST_ASSERT_EQUAL(ESP_OK,status);
    TEST_ASSERT_EQUAL(255,mock_settings.hardware.builtin_led_startup_anim);
    TEST_ASSERT_EQUAL(RGBLED_ANIM_RAINBOW,mock_settings.hardware.ext_led_anim);
    selection=250;
    ep->HandleAction(ep,EZB_ZCL_CORE_SET_ATTR_VALUE_CB_ID,&msg,&status);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,status);
    TEST_ASSERT_EQUAL(1,saves);
}

void test_EndpointInternalLED_All(void) {
    RUN_TEST(test_EndpointInternalLED_Identification);
    RUN_TEST(test_EndpointInternalLED_ReportingUsesUpdateWithoutRestart);
    RUN_TEST(test_EndpointInternalLED_HandleAction);
    RUN_TEST(test_EndpointInternalLED_BrightnessValidationAndFailure);
    RUN_TEST(test_EndpointInternalLED_StartupSelectionIsIndependentAndValidated);
}

#ifndef RACOOL_TARGET_TESTING
int main(void) {
    UNITY_BEGIN();
    test_EndpointInternalLED_All();
    return UNITY_END();
}
#endif
