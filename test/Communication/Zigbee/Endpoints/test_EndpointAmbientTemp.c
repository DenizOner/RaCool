#ifndef RACOOL_TEST_NATIVE
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#endif
#include "Communication/Zigbee/Endpoints/EndpointAmbientTemp.h"
#include "Infrastructure/support/test_mocks.h"
#include "unity.h"
#include "Communication/Zigbee/Endpoints/EndpointAmbientTemp.c"

#include "Communication/Zigbee/test_zigbee_mock_eventbus.h"

static esp_err_t field_result;
static unsigned field_calls;
static esp_err_t update_fields(IConfigurationManager *self, const char *block,
    const ConfigurationFieldUpdate *fields, size_t count, bool persist) {
    (void)self; ++field_calls;
    TEST_ASSERT_EQUAL_STRING("calibration", block);
    TEST_ASSERT_EQUAL(1, count); TEST_ASSERT_TRUE(persist);
    TEST_ASSERT_EQUAL(offsetof(CalibrationConfigs, ir_ambient_cal), fields[0].offset);
    TEST_ASSERT_EQUAL(sizeof(float), fields[0].size);
    if (field_result == ESP_OK) memcpy(&mock_settings.calibration.ir_ambient_cal, fields[0].data, fields[0].size);
    return field_result;
}

void setUp_EndpointAmbientTemp(void) {
    ZIGBEE_COMMON_FAKES(RESET_FAKE);
    mock_publish_called = 0;
    last_event_name = NULL;
    memset(&mock_settings, 0, sizeof(SystemSettings));
    
    mock_event_bus.Publish = mock_publish;
    ServiceLocator_GetEventBus_fake.return_val = &mock_event_bus;
    
    ConfigurationManager_GetInstance_fake.return_val = &mock_config_inst;
    mock_config_inst.GetSettings = mock_get_settings;
    mock_config_inst.UpdateFields = update_fields;
    field_result = ESP_OK; field_calls = 0;
}

void tearDown_EndpointAmbientTemp(void) {}

void test_EndpointAmbientTemp_Identification(void) {
    IZigbeeEndpoint *ep = EndpointAmbientTemp_Create();
    TEST_ASSERT_EQUAL_STRING("AmbientTempEndpoint", ep->GetName(ep));
    TEST_ASSERT_EQUAL(ZIGBEE_EP_AMBIENT_TEMP, ep->GetEndpointId(ep));
}

void test_EndpointAmbientTemp_RegisterClusters(void) {
    IZigbeeEndpoint *ep = EndpointAmbientTemp_Create();
    ezb_af_device_desc_t mock_device = (void *)0x1234;
    
    ep->RegisterClusters(ep, mock_device);
    
    TEST_ASSERT_EQUAL(1, mocked_ezb_af_create_endpoint_desc_fake.call_count);
    TEST_ASSERT_EQUAL(ZIGBEE_EP_AMBIENT_TEMP, last_endpoint_config.ep_id);
    TEST_ASSERT_EQUAL(1, mocked_ezb_af_device_desc_add_endpoint_fake.call_count);
}

void test_EndpointAmbientTemp_HandleAction_Calibration(void) {
    IZigbeeEndpoint *ep = EndpointAmbientTemp_Create();
    esp_err_t status = ESP_FAIL;
    
    int8_t cal = 5; // 0.5 C
    ezb_zcl_set_attr_value_message_t msg = {
        .info = { 
            .dst_ep = ZIGBEE_EP_AMBIENT_TEMP, 
            .cluster_id = EZB_ZCL_CLUSTER_ID_THERMOSTAT 
        },
        .in.attribute = { 
            .id = EZB_ZCL_ATTR_THERMOSTAT_LOCAL_TEMPERATURE_CALIBRATION_ID, 
            .data = { .value = &cal, .type = EZB_ZCL_ATTR_TYPE_INT8, .size = sizeof(cal) } 
        }
    };
    
    bool handled = ep->HandleAction(ep, EZB_ZCL_CORE_SET_ATTR_VALUE_CB_ID, &msg, &status);
    
    TEST_ASSERT_TRUE(handled);
    TEST_ASSERT_EQUAL(ESP_OK, status);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.5f, mock_settings.calibration.ir_ambient_cal);
    
    TEST_ASSERT_EQUAL(1, field_calls);
    TEST_ASSERT_EQUAL(0, mock_publish_called);
}


void test_EndpointAmbientTemp_RejectsMalformedAndOutOfRangeInput(void) {
    IZigbeeEndpoint *ep = EndpointAmbientTemp_Create();
    esp_err_t status;
    int8_t value = 127;
    ezb_zcl_set_attr_value_message_t msg = {.info = {.dst_ep=ZIGBEE_EP_AMBIENT_TEMP, .cluster_id=EZB_ZCL_CLUSTER_ID_THERMOSTAT},
        .in.attribute = {.id=EZB_ZCL_ATTR_THERMOSTAT_LOCAL_TEMPERATURE_CALIBRATION_ID,
            .data={.value=&value, .type=EZB_ZCL_ATTR_TYPE_INT8, .size=1}}};
    TEST_ASSERT_TRUE(ep->HandleAction(ep,EZB_ZCL_CORE_SET_ATTR_VALUE_CB_ID,&msg,&status));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,status);
    value = 5; msg.in.attribute.data.size = 0;
    TEST_ASSERT_TRUE(ep->HandleAction(ep,EZB_ZCL_CORE_SET_ATTR_VALUE_CB_ID,&msg,&status));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,status);
    msg.in.attribute.data.size = 1; msg.in.attribute.data.value = NULL;
    TEST_ASSERT_TRUE(ep->HandleAction(ep,EZB_ZCL_CORE_SET_ATTR_VALUE_CB_ID,&msg,&status));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,status);
    TEST_ASSERT_FALSE(ep->HandleAction(ep,EZB_ZCL_CORE_SET_ATTR_VALUE_CB_ID,NULL,&status));
    TEST_ASSERT_EQUAL(0,field_calls);
}
void test_EndpointAmbientTemp_PersistenceFailurePropagates(void) {
    IZigbeeEndpoint *ep = EndpointAmbientTemp_Create();
    field_result = ESP_FAIL;
    int8_t value = 7; esp_err_t status = ESP_OK;
    ezb_zcl_set_attr_value_message_t msg = {.info = {.dst_ep=ZIGBEE_EP_AMBIENT_TEMP,.cluster_id=EZB_ZCL_CLUSTER_ID_THERMOSTAT},
        .in.attribute = {.id=EZB_ZCL_ATTR_THERMOSTAT_LOCAL_TEMPERATURE_CALIBRATION_ID,
            .data={.value=&value,.type=EZB_ZCL_ATTR_TYPE_INT8,.size=1}}};
    TEST_ASSERT_TRUE(ep->HandleAction(ep,EZB_ZCL_CORE_SET_ATTR_VALUE_CB_ID,&msg,&status));
    TEST_ASSERT_EQUAL(ESP_FAIL,status);
    TEST_ASSERT_EQUAL_FLOAT(0,mock_settings.calibration.ir_ambient_cal);
    TEST_ASSERT_EQUAL(0,mock_publish_called);
}

void test_EndpointAmbientTemp_All(void) {
    RUN_TEST(test_EndpointAmbientTemp_RejectsMalformedAndOutOfRangeInput);
    RUN_TEST(test_EndpointAmbientTemp_PersistenceFailurePropagates);
    RUN_TEST(test_EndpointAmbientTemp_Identification);
    RUN_TEST(test_EndpointAmbientTemp_RegisterClusters);
    RUN_TEST(test_EndpointAmbientTemp_HandleAction_Calibration);
}

#ifndef RACOOL_TARGET_TESTING
int main(void) {
    UNITY_BEGIN();
    test_EndpointAmbientTemp_All();
    return UNITY_END();
}
#endif
