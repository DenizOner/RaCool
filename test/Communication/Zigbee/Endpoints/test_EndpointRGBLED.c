#ifndef RACOOL_TEST_NATIVE
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#endif
#include "Communication/Zigbee/Endpoints/EndpointRGBLED.h"
#include "Infrastructure/support/test_mocks.h"
#include "unity.h"
#include "Communication/Zigbee/Endpoints/EndpointRGBLED.c"

#include "../test_zigbee_mock_eventbus.h"

static esp_err_t update_result;

void test_EndpointRGBLED_ReportingUsesUpdateWithoutRestart(void) {
    IZigbeeEndpoint *ep = EndpointRGBLED_Create();
    ezb_zcl_reporting_info_find_fake.return_val = (ezb_zcl_reporting_info_t)1;
    TEST_ASSERT_EQUAL(ESP_OK, ep->ConfigureReporting(ep));
    TEST_ASSERT_EQUAL(1, ezb_zcl_reporting_info_update_fake.call_count);
    TEST_ASSERT_EQUAL(0, ezb_zcl_reporting_start_attr_report_fake.call_count);
    ezb_zcl_reporting_info_update_fake.return_val = (ezb_err_t)1;
    TEST_ASSERT_EQUAL(ESP_FAIL, ep->ConfigureReporting(ep));
    TEST_ASSERT_EQUAL(0, ezb_zcl_reporting_start_attr_report_fake.call_count);
}
static unsigned update_count;
static unsigned mode_requests;
static esp_err_t mode_result;
static esp_err_t request_rgb_mode(IRGBLED *self) { (void)self; ++mode_requests; return mode_result; }
static IRGBLED rgb = {.ApplySelectedMode=request_rgb_mode};
static unsigned color_requests, coordinate_reads;
static RGBColor last_color;
static esp_err_t color_result;
static esp_err_t request_rgb_color(IRGBLED *self, RGBColor color) {
    (void)self; ++color_requests; last_color = color; return color_result;
}
static ezb_err_t read_coordinate(ezb_zcl_attr_desc_t descriptor, void *output) {
    (void)descriptor;
    uint16_t value = coordinate_reads++ % 2 == 0 ? RGBLED_PRESET_WHITE_X : RGBLED_PRESET_WHITE_Y;
    memcpy(output, &value, sizeof(value));
    return EZB_ERR_NONE;
}
static esp_err_t update_rgb_fields(IConfigurationManager *self, const char *block,
    const ConfigurationFieldUpdate *fields, size_t count, bool persist) {
    (void)self;
    TEST_ASSERT_EQUAL_STRING("hardware", block);
    TEST_ASSERT_TRUE(persist);
    ++update_count;
    if (update_result != ESP_OK) return update_result;
    for (size_t i = 0; i < count; ++i) memcpy((char *)&mock_settings.hardware + fields[i].offset, fields[i].data, fields[i].size);
    return ESP_OK;
}
void setUp_EndpointRGBLED(void) {
    ZIGBEE_COMMON_FAKES(RESET_FAKE);
    mock_publish_called = 0;
    memset(&mock_settings, 0, sizeof(SystemSettings));
    mock_event_bus.Publish = mock_publish;
    ServiceLocator_GetEventBus_fake.return_val = &mock_event_bus;
    ConfigurationManager_GetInstance_fake.return_val = &mock_config_inst;
    mock_config_inst.GetSettings = NULL;
    mock_config_inst.UpdateFields = update_rgb_fields;
    update_result = ESP_OK; update_count = 0;
    mode_requests = 0; mode_result = ESP_OK;
    mocked_GetRGBLEDService_fake.return_val = &rgb;
    rgb.SetColorRGB = request_rgb_color;
    color_requests = coordinate_reads = 0; color_result = ESP_OK;
}

void tearDown_EndpointRGBLED(void) {}

void test_EndpointRGBLED_Identification(void) {
    IZigbeeEndpoint *ep = EndpointRGBLED_Create();
    TEST_ASSERT_EQUAL_STRING("RGBLEDEndpoint", ep->GetName(ep));
}

void test_EndpointRGBLED_HandleAction_OnOff(void) {
    IZigbeeEndpoint *ep = EndpointRGBLED_Create();
    esp_err_t status = ESP_FAIL;
    bool val = true;
    
    ezb_zcl_set_attr_value_message_t msg = {
        .info = { 
            .dst_ep = ZIGBEE_EP_RGB_LED, 
            .cluster_id = EZB_ZCL_CLUSTER_ID_ON_OFF 
        },
        .in.attribute = { 
            .id = EZB_ZCL_ATTR_ON_OFF_ON_OFF_ID, 
            .data = { .value = &val, .size = sizeof(val), .type = EZB_ZCL_ATTR_TYPE_BOOL } 
        }
    };

    bool handled = ep->HandleAction(ep, EZB_ZCL_CORE_SET_ATTR_VALUE_CB_ID, &msg, &status);

    TEST_ASSERT_TRUE(handled);
    TEST_ASSERT_TRUE(mock_settings.hardware.rgb_red_state);
}

void test_EndpointRGBLED_HandleAction_Level(void) {
    IZigbeeEndpoint *ep = EndpointRGBLED_Create();
    esp_err_t status = ESP_FAIL;
    uint8_t val = 200;
    
    ezb_zcl_set_attr_value_message_t msg = {
        .info = { 
            .dst_ep = ZIGBEE_EP_RGB_LED, 
            .cluster_id = EZB_ZCL_CLUSTER_ID_LEVEL 
        },
        .in.attribute = { 
            .id = EZB_ZCL_ATTR_LEVEL_CURRENT_LEVEL_ID, 
            .data = { .value = &val, .size = sizeof(val), .type = EZB_ZCL_ATTR_TYPE_UINT8 } 
        }
    };

    bool handled = ep->HandleAction(ep, EZB_ZCL_CORE_SET_ATTR_VALUE_CB_ID, &msg, &status);

    TEST_ASSERT_TRUE(handled);
    TEST_ASSERT_EQUAL(200, mock_settings.hardware.ext_led_brightness);
}

void test_EndpointRGBLED_RejectsMalformedBeforeFieldUpdate(void) {
    IZigbeeEndpoint *ep = EndpointRGBLED_Create();
    esp_err_t status;
    uint8_t value = 30;
    ezb_zcl_set_attr_value_message_t msg = {
        .info = {.dst_ep = ZIGBEE_EP_RGB_LED, .cluster_id = EZB_ZCL_CLUSTER_ID_LEVEL},
        .in.attribute = {.id = EZB_ZCL_ATTR_LEVEL_CURRENT_LEVEL_ID,
            .data = {.value = &value, .size = 0, .type = EZB_ZCL_ATTR_TYPE_UINT8}}
    };
    TEST_ASSERT_TRUE(ep->HandleAction(ep, EZB_ZCL_CORE_SET_ATTR_VALUE_CB_ID, &msg, &status));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, status);
    TEST_ASSERT_EQUAL_UINT(0, update_count);
    msg.in.attribute.data.size = 1;
    msg.in.attribute.data.type = EZB_ZCL_ATTR_TYPE_INT8;
    TEST_ASSERT_TRUE(ep->HandleAction(ep, EZB_ZCL_CORE_SET_ATTR_VALUE_CB_ID, &msg, &status));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, status);
    msg.in.attribute.data.type = EZB_ZCL_ATTR_TYPE_UINT8;
    update_result = ESP_FAIL;
    TEST_ASSERT_TRUE(ep->HandleAction(ep, EZB_ZCL_CORE_SET_ATTR_VALUE_CB_ID, &msg, &status));
    TEST_ASSERT_EQUAL(ESP_FAIL, status);
    TEST_ASSERT_EQUAL_UINT8(0, mock_settings.hardware.ext_led_brightness);
    TEST_ASSERT_EQUAL_INT(0, mock_publish_called);
}

void test_EndpointRGBLED_ModeReselectsWorkerOnlyAfterSuccessfulPersistence(void) {
    IZigbeeEndpoint *ep = EndpointRGBLED_Create();
    uint8_t mode = EXT_LED_MODE_TEMPERATURE;
    ezb_zcl_set_attr_value_message_t message = {0};
    message.info.dst_ep = ZIGBEE_EP_RGB_LED;
    message.info.cluster_id = EZB_ZCL_CLUSTER_ID_ON_OFF;
    message.in.attribute.id = 0x8013;
    message.in.attribute.data.value = &mode;
    message.in.attribute.data.size = sizeof(mode);
    message.in.attribute.data.type = EZB_ZCL_ATTR_TYPE_UINT8;
    esp_err_t result;
    update_result = ESP_FAIL;
    TEST_ASSERT_TRUE(ep->HandleAction(ep,EZB_ZCL_CORE_SET_ATTR_VALUE_CB_ID,&message,&result));
    TEST_ASSERT_EQUAL(ESP_FAIL, result);
    TEST_ASSERT_EQUAL(0, mode_requests);
    update_result = ESP_OK;
    TEST_ASSERT_TRUE(ep->HandleAction(ep,EZB_ZCL_CORE_SET_ATTR_VALUE_CB_ID,&message,&result));
    TEST_ASSERT_EQUAL(ESP_OK, result);
    TEST_ASSERT_EQUAL(1, mode_requests);
    mode_result = ESP_ERR_TIMEOUT;
    TEST_ASSERT_TRUE(ep->HandleAction(ep,EZB_ZCL_CORE_SET_ATTR_VALUE_CB_ID,&message,&result));
    TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT, result);
    TEST_ASSERT_EQUAL(2, mode_requests);
}

void test_EndpointRGBLED_CoordinatesReachColorWorkerAndValidateSdkBuffer(void) {
    IZigbeeEndpoint *ep = EndpointRGBLED_Create();
    uint16_t x = RGBLED_PRESET_RED_X;
    ezb_zcl_set_attr_value_message_t message = {0};
    message.info.dst_ep = ZIGBEE_EP_RGB_LED;
    message.info.cluster_id = EZB_ZCL_CLUSTER_ID_COLOR_CONTROL;
    message.in.attribute.id = EZB_ZCL_ATTR_COLOR_CONTROL_CURRENT_X_ID;
    message.in.attribute.data.value = &x;
    message.in.attribute.data.size = sizeof(x);
    message.in.attribute.data.type = EZB_ZCL_ATTR_TYPE_UINT16;
    ezb_zcl_attr_desc_get_type_fake.return_val = EZB_ZCL_ATTR_TYPE_UINT16;
    mocked_ezb_zcl_attr_desc_get_value_fake.custom_fake = read_coordinate;
    esp_err_t result;
    TEST_ASSERT_TRUE(ep->HandleAction(ep,EZB_ZCL_CORE_SET_ATTR_VALUE_CB_ID,&message,&result));
    TEST_ASSERT_EQUAL(ESP_OK, result);
    TEST_ASSERT_EQUAL(1, color_requests);
    RGBColor expected = RGBLEDColorEngine_XYtoRGB(x, RGBLED_PRESET_WHITE_Y);
    TEST_ASSERT_EQUAL_MEMORY(&expected, &last_color, sizeof(expected));
    TEST_ASSERT_EQUAL(0, update_count);
    mocked_ezb_zcl_attr_desc_get_value_size_fake.return_val = 8;
    TEST_ASSERT_TRUE(ep->HandleAction(ep,EZB_ZCL_CORE_SET_ATTR_VALUE_CB_ID,&message,&result));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, result);
    TEST_ASSERT_EQUAL(2, coordinate_reads);
    TEST_ASSERT_EQUAL(1, color_requests);
    mocked_ezb_zcl_attr_desc_get_value_size_fake.return_val = sizeof(x);
    x = 0xffff;
    TEST_ASSERT_TRUE(ep->HandleAction(ep,EZB_ZCL_CORE_SET_ATTR_VALUE_CB_ID,&message,&result));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, result);
    TEST_ASSERT_EQUAL(1, color_requests);
    x = RGBLED_PRESET_RED_X; color_result = ESP_ERR_TIMEOUT;
    TEST_ASSERT_TRUE(ep->HandleAction(ep,EZB_ZCL_CORE_SET_ATTR_VALUE_CB_ID,&message,&result));
    TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT, result);
    TEST_ASSERT_EQUAL(2, color_requests);
}

void test_EndpointRGBLED_FreeXYAllZonesValidateAndPreserveFailedWrites(void) {
    IZigbeeEndpoint *ep=EndpointRGBLED_Create();
    ezb_zcl_set_attr_value_message_t message={0};
    message.info.dst_ep=ZIGBEE_EP_RGB_LED;
    message.info.cluster_id=EZB_ZCL_CLUSTER_ID_ON_OFF;
    uint32_t packed=1234u|(4321u<<16);
    message.in.attribute.data.value=&packed;
    message.in.attribute.data.size=sizeof(packed);
    message.in.attribute.data.type=EZB_ZCL_ATTR_TYPE_UINT32;
    esp_err_t result;
    for (unsigned zone=0;zone<RGB_ZONE_COUNT;++zone) {
        message.in.attribute.id=ZIGBEE_ATTR_RGB_ZONE_XY_FIRST+zone;
        TEST_ASSERT_TRUE(ep->HandleAction(ep,EZB_ZCL_CORE_SET_ATTR_VALUE_CB_ID,&message,&result));
        TEST_ASSERT_EQUAL(ESP_OK,result);
        uint16_t x,y;
        TEST_ASSERT_TRUE(SystemConfigs_GetRGBZoneXY(&mock_settings.hardware,zone,&x,&y));
        TEST_ASSERT_EQUAL(1234,x);TEST_ASSERT_EQUAL(4321,y);
    }
    HardwareConfigs before=mock_settings.hardware;
    update_result=ESP_FAIL;
    packed=5678u|(8765u<<16);
    TEST_ASSERT_TRUE(ep->HandleAction(ep,EZB_ZCL_CORE_SET_ATTR_VALUE_CB_ID,&message,&result));
    TEST_ASSERT_EQUAL(ESP_FAIL,result);
    TEST_ASSERT_EQUAL_MEMORY(&before,&mock_settings.hardware,sizeof(before));
    unsigned calls=update_count;
    uint32_t bad[]={1u,65535u|(10u<<16),1u|(65535u<<16)};
    for (unsigned i=0;i<3;++i) {
        packed=bad[i];
        TEST_ASSERT_TRUE(ep->HandleAction(ep,EZB_ZCL_CORE_SET_ATTR_VALUE_CB_ID,&message,&result));
        TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,result);
    }
    message.in.attribute.data.size=2;
    TEST_ASSERT_TRUE(ep->HandleAction(ep,EZB_ZCL_CORE_SET_ATTR_VALUE_CB_ID,&message,&result));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,result);
    message.in.attribute.data.size=4;message.in.attribute.data.type=EZB_ZCL_ATTR_TYPE_UINT16;
    TEST_ASSERT_TRUE(ep->HandleAction(ep,EZB_ZCL_CORE_SET_ATTR_VALUE_CB_ID,&message,&result));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,result);
    TEST_ASSERT_EQUAL(calls,update_count);
}

void test_EndpointRGBLED_All(void) {
    RUN_TEST(test_EndpointRGBLED_FreeXYAllZonesValidateAndPreserveFailedWrites);
    RUN_TEST(test_EndpointRGBLED_CoordinatesReachColorWorkerAndValidateSdkBuffer);
    RUN_TEST(test_EndpointRGBLED_ModeReselectsWorkerOnlyAfterSuccessfulPersistence);
    RUN_TEST(test_EndpointRGBLED_RejectsMalformedBeforeFieldUpdate);
    RUN_TEST(test_EndpointRGBLED_Identification);
    RUN_TEST(test_EndpointRGBLED_ReportingUsesUpdateWithoutRestart);
    RUN_TEST(test_EndpointRGBLED_HandleAction_OnOff);
    RUN_TEST(test_EndpointRGBLED_HandleAction_Level);
}

#ifndef RACOOL_TARGET_TESTING
int main(void) {
    UNITY_BEGIN();
    test_EndpointRGBLED_All();
    return UNITY_END();
}
#endif
