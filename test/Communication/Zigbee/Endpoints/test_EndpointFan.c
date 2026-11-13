#ifndef RACOOL_TEST_NATIVE
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#endif
#include "Communication/Zigbee/Endpoints/EndpointFan.h"
#include "Infrastructure/support/test_mocks.h"
#define ZIGBEE_CONFIG_INTERNAL // To allow access to internal defines if needed
#include "Communication/Zigbee/ZigbeeConfig.h"
#include "unity.h"
#include "../test_zigbee_mock_eventbus.h"
#include "Communication/Zigbee/Endpoints/EndpointFan.c"

static esp_err_t calibration_result;
static unsigned calibration_calls;
static esp_err_t save_calibration(IConfigurationManager *self, const char *block,
    const ConfigurationFieldUpdate *fields, size_t count, bool persist) {
    (void)self; ++calibration_calls;
    if (!strcmp(block, "control")) {
        ControlConfigs candidate = mock_settings.control;
        for (size_t i = 0; i < count; ++i) memcpy((char *)&candidate + fields[i].offset, fields[i].data, fields[i].size);
        if (!SystemConfigs_IsFanSourceConfigValid(&candidate) || !SystemConfigs_IsFanSetpointPairValid(candidate.relay_open_temp, candidate.relay_close_temp)) return ESP_ERR_INVALID_ARG;
        if (calibration_result == ESP_OK) mock_settings.control = candidate;
        return calibration_result;
    }
    TEST_ASSERT_EQUAL_STRING("calibration", block);
    TEST_ASSERT_EQUAL(1, count); TEST_ASSERT_TRUE(persist);
    TEST_ASSERT_EQUAL(offsetof(CalibrationConfigs, ir_object_cal), fields[0].offset);
    TEST_ASSERT_EQUAL(sizeof(float), fields[0].size);
    if (calibration_result == ESP_OK) memcpy(&mock_settings.calibration.ir_object_cal, fields[0].data, fields[0].size);
    return calibration_result;
}

void setUp_EndpointFan(void) {
    ZIGBEE_COMMON_FAKES(RESET_FAKE);
    mock_publish_called = 0;
    memset(&mock_settings, 0, sizeof(SystemSettings));
    mock_event_bus.Publish = mock_publish;
    ServiceLocator_GetEventBus_fake.return_val = &mock_event_bus;
    ConfigurationManager_GetInstance_fake.return_val = &mock_config_inst;
    mock_config_inst.GetSettings = mock_get_settings;
    mock_config_inst.UpdateFields = save_calibration;
    calibration_result = ESP_OK; calibration_calls = 0;
    mock_settings.control.fan_delta_on_decic=20;mock_settings.control.fan_delta_off_decic=10;
    mock_settings.control.relay_close_temp = 20.0f;
    mock_settings.control.relay_open_temp = 30.0f;
}

void tearDown_EndpointFan(void) {}

void test_EndpointFan_Identification(void) {
    IZigbeeEndpoint *ep = EndpointFan_Create();
    TEST_ASSERT_EQUAL_STRING("FanSensorEndpoint", ep->GetName(ep));
}

void test_EndpointFan_HandleAction_OnOff(void) {
    IZigbeeEndpoint *ep = EndpointFan_Create();
    esp_err_t status = ESP_FAIL;
    bool val = true;
    
    // Modern ezb_zcl_set_attr_value_message_t
    ezb_zcl_set_attr_value_message_t msg = {
        .info = { 
            .dst_ep = ZIGBEE_EP_SENSOR_FAN, 
            .cluster_id = EZB_ZCL_CLUSTER_ID_ON_OFF 
        },
        .in.attribute = { 
            .id = EZB_ZCL_ATTR_ON_OFF_ON_OFF_ID, 
            .data = { .value = &val, .size = 1, .type = EZB_ZCL_ATTR_TYPE_BOOL } 
        }
    };
    
    bool handled = ep->HandleAction(ep, EZB_ZCL_CORE_SET_ATTR_VALUE_CB_ID, &msg, &status);
    
    TEST_ASSERT_TRUE(handled);
    TEST_ASSERT_TRUE(mock_settings.control.fan_manual_state);
}

void test_EndpointFan_HandleAction_Thermostat(void) {
    IZigbeeEndpoint *ep = EndpointFan_Create();
    esp_err_t status = ESP_FAIL;
    int16_t setpoint = 2500; // 25.00C
    
    ezb_zcl_set_attr_value_message_t msg = {
        .info = { 
            .dst_ep = ZIGBEE_EP_SENSOR_FAN, 
            .cluster_id = EZB_ZCL_CLUSTER_ID_THERMOSTAT 
        },
        .in.attribute = { 
            .id = EZB_ZCL_ATTR_THERMOSTAT_OCCUPIED_COOLING_SETPOINT_ID, 
            .data = { .value = &setpoint, .size = sizeof(setpoint), .type = EZB_ZCL_ATTR_TYPE_INT16 } 
        }
    };
    
    bool handled = ep->HandleAction(ep, EZB_ZCL_CORE_SET_ATTR_VALUE_CB_ID, &msg, &status);
    
    TEST_ASSERT_TRUE(handled);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 25.0f, mock_settings.control.relay_open_temp);
}

void test_EndpointFan_RejectsInvalidThreshold(void) {
    IZigbeeEndpoint *ep = EndpointFan_Create();
    const int16_t values[] = {1900, 2000, 10000};
    for (unsigned i = 0; i < sizeof(values) / sizeof(values[0]); ++i) {
        esp_err_t status = ESP_OK;
        int16_t value = values[i];
        ezb_zcl_set_attr_value_message_t msg = {
            .info = {.dst_ep = ZIGBEE_EP_SENSOR_FAN, .cluster_id = EZB_ZCL_CLUSTER_ID_THERMOSTAT},
            .in.attribute = {.id = EZB_ZCL_ATTR_THERMOSTAT_OCCUPIED_COOLING_SETPOINT_ID,
                             .data = {.value = &value, .size = sizeof(value), .type = EZB_ZCL_ATTR_TYPE_INT16}},
        };
        TEST_ASSERT_TRUE(ep->HandleAction(ep, EZB_ZCL_CORE_SET_ATTR_VALUE_CB_ID, &msg, &status));
        TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, status);
        TEST_ASSERT_FLOAT_WITHIN(0.01f, 30.0f, mock_settings.control.relay_open_temp);
        TEST_ASSERT_EQUAL(0, mock_publish_called);
    }
}

static esp_err_t apply_calibration(int8_t *value, uint16_t size, uint8_t type) {
    IZigbeeEndpoint *ep = EndpointFan_Create();
    ezb_zcl_set_attr_value_message_t msg = {
        .info = {.dst_ep = ZIGBEE_EP_SENSOR_FAN, .cluster_id = EZB_ZCL_CLUSTER_ID_THERMOSTAT},
        .in.attribute = {.id = EZB_ZCL_ATTR_THERMOSTAT_LOCAL_TEMPERATURE_CALIBRATION_ID,
            .data = {.value = value, .size = size, .type = type}}
    };
    esp_err_t result = ESP_OK;
    TEST_ASSERT_TRUE(ep->HandleAction(ep, EZB_ZCL_CORE_SET_ATTR_VALUE_CB_ID, &msg, &result));
    return result;
}
void test_EndpointFan_CalibrationPersistsBeforeApplying(void) {
    int8_t value = -17;
    TEST_ASSERT_EQUAL(ESP_OK, apply_calibration(&value, 1, EZB_ZCL_ATTR_TYPE_INT8));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, -1.7f, mock_settings.calibration.ir_object_cal);
    TEST_ASSERT_EQUAL(1, calibration_calls);
    TEST_ASSERT_EQUAL(0, mock_publish_called);
    calibration_result = ESP_FAIL; value = 24;
    TEST_ASSERT_EQUAL(ESP_FAIL, apply_calibration(&value, 1, EZB_ZCL_ATTR_TYPE_INT8));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, -1.7f, mock_settings.calibration.ir_object_cal);
}
void test_EndpointFan_CalibrationRejectsMalformedInput(void) {
    int8_t value = 10;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, apply_calibration(NULL, 1, EZB_ZCL_ATTR_TYPE_INT8));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, apply_calibration(&value, 2, EZB_ZCL_ATTR_TYPE_INT8));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, apply_calibration(&value, 1, EZB_ZCL_ATTR_TYPE_UINT8));
    value = 100;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, apply_calibration(&value, 1, EZB_ZCL_ATTR_TYPE_INT8));
    TEST_ASSERT_EQUAL(0, calibration_calls);
}
void test_EndpointFan_CalibrationRequiresConfigurationService(void) {
    int8_t value = 10;
    ConfigurationManager_GetInstance_fake.return_val = NULL;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, apply_calibration(&value, 1, EZB_ZCL_ATTR_TYPE_INT8));
    TEST_ASSERT_EQUAL(0, calibration_calls);
}

static esp_err_t save_reporting(IConfigurationManager *self, const char *name,
    const ConfigurationFieldUpdate *fields, size_t count, bool persist) {
    (void)self; ++calibration_calls;
    TEST_ASSERT_EQUAL_STRING("zigbee_reporting", name);
    TEST_ASSERT_EQUAL(1, count); TEST_ASSERT_TRUE(persist);
    if (calibration_result == ESP_OK)
        memcpy((uint8_t *)&mock_settings.zigbee_reporting + fields[0].offset, fields[0].data, fields[0].size);
    return calibration_result;
}
static unsigned refresh_count;
static int32_t refresh_result;
static int32_t refresh_reporting(IZigbee *self) { (void)self; ++refresh_count; return refresh_result; }
void test_EndpointFan_AllReportingControlsPersistZero(void) {
    mock_config_inst.UpdateFields = save_reporting;
    IZigbee zigbee = {.ReconfigureAllReporting = refresh_reporting};
    ServiceLocator_GetService_fake.return_val = &zigbee.base;
    refresh_count = 0; refresh_result = 0;
    IZigbeeEndpoint *ep = EndpointFan_Create();
    for (unsigned i = 0; i < 6; ++i) {
        uint16_t value = 0; esp_err_t status = ESP_FAIL;
        ezb_zcl_set_attr_value_message_t msg = {
            .info = {.dst_ep = ZIGBEE_EP_SENSOR_FAN, .cluster_id = EZB_ZCL_CLUSTER_ID_THERMOSTAT_UI_CONFIG},
            .in.attribute = {.id = 0x8030 + i, .data = {.value = &value, .size = 2, .type = EZB_ZCL_ATTR_TYPE_UINT16}}
        };
        TEST_ASSERT_TRUE(ep->HandleAction(ep, EZB_ZCL_CORE_SET_ATTR_VALUE_CB_ID, &msg, &status));
        TEST_ASSERT_EQUAL(ESP_OK, status);
        calibration_result = ESP_FAIL; value = 5;
        TEST_ASSERT_TRUE(ep->HandleAction(ep, EZB_ZCL_CORE_SET_ATTR_VALUE_CB_ID, &msg, &status));
        TEST_ASSERT_EQUAL(ESP_FAIL, status);
        calibration_result = ESP_OK;
        unsigned calls = calibration_calls; value = ZB_REPORT_MAX_S + 1;
        TEST_ASSERT_TRUE(ep->HandleAction(ep, EZB_ZCL_CORE_SET_ATTR_VALUE_CB_ID, &msg, &status));
        TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, status);
        TEST_ASSERT_EQUAL(calls, calibration_calls);
        value = 0; msg.in.attribute.data.size = 1;
        TEST_ASSERT_TRUE(ep->HandleAction(ep, EZB_ZCL_CORE_SET_ATTR_VALUE_CB_ID, &msg, &status));
        TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, status);
    }
    TEST_ASSERT_EQUAL(12, calibration_calls);
}

void test_EndpointFan_ReportingRefreshDoesNotRewriteSettings(void) {
    IZigbeeEndpoint *ep = EndpointFan_Create();
    IZigbee zigbee = {.ReconfigureAllReporting = refresh_reporting};
    ServiceLocator_GetService_fake.return_val = &zigbee.base;
    refresh_count = 0; refresh_result = 0;
    uint8_t value = 0; esp_err_t status = ESP_FAIL;
    ezb_zcl_set_attr_value_message_t msg = {
        .info = {.dst_ep = ZIGBEE_EP_SENSOR_FAN, .cluster_id = EZB_ZCL_CLUSTER_ID_THERMOSTAT_UI_CONFIG},
        .in.attribute = {.id = 0x8036, .data = {.value = &value, .size = 1, .type = EZB_ZCL_ATTR_TYPE_UINT8}}
    };
    TEST_ASSERT_TRUE(ep->HandleAction(ep, EZB_ZCL_CORE_SET_ATTR_VALUE_CB_ID, &msg, &status));
    TEST_ASSERT_EQUAL(ESP_OK, status); TEST_ASSERT_EQUAL(1, refresh_count);
    TEST_ASSERT_EQUAL(0, calibration_calls);
    refresh_result = -1;
    TEST_ASSERT_TRUE(ep->HandleAction(ep, EZB_ZCL_CORE_SET_ATTR_VALUE_CB_ID, &msg, &status));
    TEST_ASSERT_EQUAL(ESP_FAIL, status);
    refresh_result = 0;
    value = 1;
    TEST_ASSERT_TRUE(ep->HandleAction(ep, EZB_ZCL_CORE_SET_ATTR_VALUE_CB_ID, &msg, &status));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, status);
    value = 0; msg.in.attribute.data.size = 2;
    TEST_ASSERT_TRUE(ep->HandleAction(ep, EZB_ZCL_CORE_SET_ATTR_VALUE_CB_ID, &msg, &status));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, status);
    msg.in.attribute.data.size = 1; ServiceLocator_GetService_fake.return_val = NULL;
    TEST_ASSERT_TRUE(ep->HandleAction(ep, EZB_ZCL_CORE_SET_ATTR_VALUE_CB_ID, &msg, &status));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, status);
    TEST_ASSERT_EQUAL(2, refresh_count); TEST_ASSERT_EQUAL(0, calibration_calls);
}

static esp_err_t save_legacy_display(IConfigurationManager *self, const char *block,
        const ConfigurationFieldUpdate *fields, size_t count, bool persist) {
    (void)self;
    TEST_ASSERT_EQUAL_STRING("system", block); TEST_ASSERT_EQUAL(1, count); TEST_ASSERT_TRUE(persist);
    TEST_ASSERT_EQUAL(offsetof(SystemBaseConfigs, display_mode), fields[0].offset);
    TEST_ASSERT_EQUAL(1, fields[0].size);
    return ESP_FAIL;
}
void test_EndpointFan_LegacyDisplayUsesPersistentPath(void) {
    mock_config_inst.UpdateFields = save_legacy_display;
    uint8_t mode = 1; esp_err_t result = ESP_OK;
    ezb_zcl_set_attr_value_message_t msg = {
        .info = {.dst_ep = 1, .cluster_id = EZB_ZCL_CLUSTER_ID_THERMOSTAT_UI_CONFIG},
        .in.attribute = {.id = 0x8020, .data = {.value = &mode, .size = 1, .type = EZB_ZCL_ATTR_TYPE_UINT8}}
    };
    IZigbeeEndpoint *ep = EndpointFan_Create();
    TEST_ASSERT_TRUE(ep->HandleAction(ep, EZB_ZCL_CORE_SET_ATTR_VALUE_CB_ID, &msg, &result));
    TEST_ASSERT_EQUAL(ESP_FAIL, result);
    TEST_ASSERT_EQUAL(0, mock_settings.system.display_mode);
    TEST_ASSERT_EQUAL(0, mock_publish_called);
}

void test_EndpointFan_ControlWriteRejectsMalformedAndPreservesStateOnFailure(void) {
    IZigbeeEndpoint *ep = EndpointFan_Create();
    esp_err_t status;
    uint8_t value = 4;
    ezb_zcl_set_attr_value_message_t msg = {
        .info = {.dst_ep = ZIGBEE_EP_SENSOR_FAN, .cluster_id = EZB_ZCL_CLUSTER_ID_THERMOSTAT},
        .in.attribute = {.id = EZB_ZCL_ATTR_THERMOSTAT_SYSTEM_MODE_ID,
            .data = {.value = &value, .size = 0, .type = EZB_ZCL_ATTR_TYPE_ENUM8}}
    };
    TEST_ASSERT_TRUE(ep->HandleAction(ep, EZB_ZCL_CORE_SET_ATTR_VALUE_CB_ID, &msg, &status));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, status);
    TEST_ASSERT_EQUAL_UINT(0, calibration_calls);
    msg.in.attribute.data.size = 1;
    value = 4; /* Heat is not valid for the cooling-only server. */
    TEST_ASSERT_TRUE(ep->HandleAction(ep, EZB_ZCL_CORE_SET_ATTR_VALUE_CB_ID, &msg, &status));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, status);
    value = 3; calibration_result = ESP_FAIL;
    TEST_ASSERT_TRUE(ep->HandleAction(ep, EZB_ZCL_CORE_SET_ATTR_VALUE_CB_ID, &msg, &status));
    TEST_ASSERT_EQUAL(ESP_FAIL, status);
    TEST_ASSERT_FALSE(mock_settings.control.fan_manual_mode);
    TEST_ASSERT_EQUAL_INT(0, mock_publish_called);
    calibration_result = ESP_OK;
    TEST_ASSERT_TRUE(ep->HandleAction(ep, EZB_ZCL_CORE_SET_ATTR_VALUE_CB_ID, &msg, &status));
    TEST_ASSERT_EQUAL(ESP_OK, status);
    TEST_ASSERT_TRUE(mock_settings.control.fan_manual_mode);
    TEST_ASSERT_TRUE(mock_settings.control.fan_manual_state);
}

static unsigned shutdown_requests;
static int32_t shutdown_result;
static int32_t request_shutdown(ISystemController *self, uint32_t delay) {
    (void)self;
    ++shutdown_requests;
    TEST_ASSERT_EQUAL_UINT(ZIGBEE_POWER_COMMAND_DELAY_MS, delay);
    return shutdown_result;
}
static IService *shutdown_controller(const char *name) {
    static ISystemController system = {.RequestShutdown = request_shutdown};
    return !strcmp(name, SVC_NAME_SYSTEM_CONTROLLER) ? (IService *)&system : NULL;
}
void test_EndpointFan_DeepSleepUsesExistingControllerQueueWithoutCreatingTasks(void) {
    IZigbeeEndpoint *ep = EndpointFan_Create();
    uint8_t command = 3;
    esp_err_t status;
    ezb_zcl_set_attr_value_message_t msg = {
        .info = {.dst_ep = ZIGBEE_EP_SENSOR_FAN, .cluster_id = EZB_ZCL_CLUSTER_ID_THERMOSTAT_UI_CONFIG},
        .in.attribute = {.id = 0x8040, .data = {.value = &command, .size = 1, .type = EZB_ZCL_ATTR_TYPE_UINT8}}
    };
    RESET_FAKE(mocked_xTaskCreate);
    ServiceLocator_GetService_fake.custom_fake = shutdown_controller;
    shutdown_requests = 0; shutdown_result = ESP_FAIL;
    TEST_ASSERT_TRUE(ep->HandleAction(ep, EZB_ZCL_CORE_SET_ATTR_VALUE_CB_ID, &msg, &status));
    TEST_ASSERT_EQUAL(ESP_FAIL, status);
    shutdown_result = ESP_OK;
    TEST_ASSERT_TRUE(ep->HandleAction(ep, EZB_ZCL_CORE_SET_ATTR_VALUE_CB_ID, &msg, &status));
    TEST_ASSERT_EQUAL(ESP_OK, status);
    TEST_ASSERT_EQUAL_UINT(2, shutdown_requests);
    TEST_ASSERT_EQUAL_UINT(0, mocked_xTaskCreate_fake.call_count);
}

void test_ReportRoutesRejectWrongEndpointAndExposePartialRefreshFailure(void) {
    mock_config_inst.UpdateFields = save_reporting;
    IZigbee zigbee = {.ReconfigureAllReporting = refresh_reporting};
    ServiceLocator_GetService_fake.return_val = &zigbee.base;
    refresh_result = 0; refresh_count = 0;
    const uint8_t endpoints[] = {1,4,5,6,7,8};
    uint16_t seconds = 0;
    for (unsigned kind = 0; kind < 6; ++kind) {
        ezb_zcl_set_attr_value_message_t message = {
            .info = {.dst_ep = endpoints[kind], .cluster_id = EZB_ZCL_CLUSTER_ID_THERMOSTAT_UI_CONFIG},
            .in.attribute = {.id = 0x8030 + kind,
                .data = {.value = &seconds, .size = 2, .type = EZB_ZCL_ATTR_TYPE_UINT16}}
        };
        esp_err_t result;
        TEST_ASSERT_TRUE(ZigbeeEndpoint_HandleReportingSetting(&message, &result));
        TEST_ASSERT_EQUAL(ESP_OK, result);
        message.info.dst_ep = ZIGBEE_EP_SYSTEM;
        TEST_ASSERT_FALSE(ZigbeeEndpoint_HandleReportingSetting(&message, &result));
    }
    TEST_ASSERT_EQUAL(6, refresh_count);
    seconds = 7;
    ezb_zcl_set_attr_value_message_t message = {
        .info = {.dst_ep = 4, .cluster_id = EZB_ZCL_CLUSTER_ID_THERMOSTAT_UI_CONFIG},
        .in.attribute = {.id = 0x8031, .data = {.value = &seconds, .size = 2, .type = EZB_ZCL_ATTR_TYPE_UINT16}}
    };
    refresh_result = -1; esp_err_t result;
    ZigbeeEndpoint_HandleReportingSetting(&message, &result);
    TEST_ASSERT_EQUAL(ESP_FAIL, result);
    TEST_ASSERT_EQUAL(7, mock_settings.zigbee_reporting.ambient_s);
    ServiceLocator_GetService_fake.return_val = NULL;
    ZigbeeEndpoint_HandleReportingSetting(&message, &result);
    TEST_ASSERT_EQUAL(ESP_FAIL, result);
}

void test_ReportClusterFailureReleasesOnlyUnattachedDescriptor(void) {
    mocked_ezb_zcl_thermostat_ui_config_create_cluster_desc_fake.return_val = NULL;
    TEST_ASSERT_EQUAL(ESP_ERR_NO_MEM, ZigbeeEndpoint_RegisterReportingCluster((void *)1, 4));
    TEST_ASSERT_EQUAL(0, mocked_ezb_zcl_free_cluster_desc_fake.call_count);
    mocked_ezb_zcl_thermostat_ui_config_create_cluster_desc_fake.return_val = (void *)2;
    ezb_zcl_cluster_desc_add_manuf_attr_fake.return_val = 1;
    TEST_ASSERT_EQUAL(ESP_FAIL, ZigbeeEndpoint_RegisterReportingCluster((void *)1, 4));
    TEST_ASSERT_EQUAL(1, mocked_ezb_zcl_free_cluster_desc_fake.call_count);
    ezb_zcl_cluster_desc_add_manuf_attr_fake.return_val = 0;
    mocked_ezb_af_endpoint_add_cluster_desc_fake.return_val = 1;
    TEST_ASSERT_EQUAL(ESP_FAIL, ZigbeeEndpoint_RegisterReportingCluster((void *)1, 4));
    TEST_ASSERT_EQUAL(2, mocked_ezb_zcl_free_cluster_desc_fake.call_count);
    mocked_ezb_af_endpoint_add_cluster_desc_fake.return_val = 0;
    TEST_ASSERT_EQUAL(ESP_OK, ZigbeeEndpoint_RegisterReportingCluster((void *)1, 4));
    TEST_ASSERT_EQUAL(2, mocked_ezb_zcl_free_cluster_desc_fake.call_count);
}

static bool relay_read_value, observed_relay_value;
static esp_err_t relay_read_result;
static esp_err_t checked_relay_read(IRelay *self, bool *value) {
    (void)self;
    if (relay_read_result == ESP_OK) *value = relay_read_value;
    return relay_read_result;
}
static ezb_zcl_status_t observe_initial_relay(uint8_t endpoint, uint16_t cluster, uint8_t role,
    uint16_t attribute, uint16_t manufacturer, void *value, bool check) {
    (void)role; (void)manufacturer; (void)check;
    TEST_ASSERT_EQUAL(1, endpoint); TEST_ASSERT_EQUAL(6, cluster); TEST_ASSERT_EQUAL(0, attribute);
    observed_relay_value = *(bool *)value;
    return EZB_ZCL_STATUS_SUCCESS;
}
void test_EndpointFan_ConnectedReadsCheckedActualStateAndRejectsUnavailable(void) {
    IRelay relay = {.ReadState = checked_relay_read};
    ServiceLocator_GetService_fake.return_val = &relay.base;
    IZigbeeEndpoint *endpoint = EndpointFan_Create();
    relay_read_result = ESP_ERR_TIMEOUT;
    TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT, endpoint->OnConnected(endpoint));
    TEST_ASSERT_EQUAL(0, mocked_ezb_zcl_set_attr_value_fake.call_count);
    relay_read_result = ESP_OK;
    mocked_ezb_zcl_set_attr_value_fake.custom_fake = observe_initial_relay;
    relay_read_value = true;
    TEST_ASSERT_EQUAL(ESP_OK, endpoint->OnConnected(endpoint));
    TEST_ASSERT_TRUE(observed_relay_value);
    relay_read_value = false;
    TEST_ASSERT_EQUAL(ESP_OK, endpoint->OnConnected(endpoint));
    TEST_ASSERT_FALSE(observed_relay_value);
    mocked_ezb_zcl_set_attr_value_fake.custom_fake = NULL;
    mocked_ezb_zcl_set_attr_value_fake.return_val = EZB_ZCL_STATUS_FAIL;
    TEST_ASSERT_EQUAL(ESP_FAIL, endpoint->OnConnected(endpoint));
}

static esp_err_t apply_source_attribute(uint16_t id,void *value,uint16_t size,uint8_t type) {
    IZigbeeEndpoint *endpoint=EndpointFan_Create();
    ezb_zcl_set_attr_value_message_t message={.info={.dst_ep=1,.cluster_id=EZB_ZCL_CLUSTER_ID_THERMOSTAT_UI_CONFIG},
        .in.attribute={.id=id,.data={.value=value,.size=size,.type=type}}};
    esp_err_t result=ESP_FAIL;
    TEST_ASSERT_TRUE(endpoint->HandleAction(endpoint,EZB_ZCL_CORE_SET_ATTR_VALUE_CB_ID,&message,&result));return result;
}
void test_EndpointFan_SignedDeltaAndSourcesShareValidatedCommandPath(void) {
    int16_t off=-20,on=-10;
    TEST_ASSERT_EQUAL(ESP_OK,apply_source_attribute(FAN_ATTR_DELTA_OFF,&off,2,EZB_ZCL_ATTR_TYPE_INT16));
    TEST_ASSERT_EQUAL(ESP_OK,apply_source_attribute(FAN_ATTR_DELTA_ON,&on,2,EZB_ZCL_ATTR_TYPE_INT16));
    TEST_ASSERT_EQUAL(-10,mock_settings.control.fan_delta_on_decic);TEST_ASSERT_EQUAL(-20,mock_settings.control.fan_delta_off_decic);
    off=-10;TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,apply_source_attribute(FAN_ATTR_DELTA_OFF,&off,2,EZB_ZCL_ATTR_TYPE_INT16));
    TEST_ASSERT_EQUAL(-20,mock_settings.control.fan_delta_off_decic);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,apply_source_attribute(FAN_ATTR_DELTA_ON,&on,2,EZB_ZCL_ATTR_TYPE_UINT16));
    uint8_t source=2;TEST_ASSERT_EQUAL(ESP_OK,apply_source_attribute(FAN_ATTR_OBJECT_SOURCE,&source,1,EZB_ZCL_ATTR_TYPE_UINT8));
    source=1;TEST_ASSERT_EQUAL(ESP_OK,apply_source_attribute(FAN_ATTR_AMBIENT_SOURCE,&source,1,EZB_ZCL_ATTR_TYPE_UINT8));
    TEST_ASSERT_EQUAL(2,mock_settings.control.object_temperature_source);TEST_ASSERT_EQUAL(1,mock_settings.control.ambient_temperature_source);
    source=4;TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,apply_source_attribute(FAN_ATTR_AMBIENT_SOURCE,&source,1,EZB_ZCL_ATTR_TYPE_UINT8));
    uint8_t mode=3;TEST_ASSERT_EQUAL(ESP_OK,apply_source_attribute(FAN_ATTR_MODE,&mode,1,EZB_ZCL_ATTR_TYPE_UINT8));
    TEST_ASSERT_FALSE(mock_settings.control.fan_manual_mode);TEST_ASSERT_EQUAL(1,mock_settings.control.fan_strategy);
}
static unsigned external_calls,clear_calls;
static uint8_t external_slot;
static float external_value;
static esp_err_t inject_external(IClimateController *self,uint8_t slot,float value) {
    (void)self;++external_calls;external_slot=slot;external_value=value;return ESP_OK;
}
static esp_err_t clear_external(IClimateController *self,uint8_t slot) {
    (void)self;++clear_calls;external_slot=slot;return ESP_OK;
}
void test_EndpointFan_ExternalMeasurementAndClearNeverPersistConfiguration(void) {
    IClimateController controller={.SetExternalTemperature=inject_external,.ClearExternalTemperature=clear_external};
    ServiceLocator_GetService_fake.return_val=&controller.base;
    external_calls=clear_calls=0;
    int16_t value=2170;
    TEST_ASSERT_EQUAL(ESP_OK,apply_source_attribute(FAN_ATTR_EXTERNAL_FIRST+1,&value,2,EZB_ZCL_ATTR_TYPE_INT16));
    TEST_ASSERT_EQUAL(2,external_slot);TEST_ASSERT_FLOAT_WITHIN(.001,21.7,external_value);TEST_ASSERT_EQUAL(1,external_calls);
    value=INT16_MIN;TEST_ASSERT_EQUAL(ESP_OK,apply_source_attribute(FAN_ATTR_EXTERNAL_FIRST+1,&value,2,EZB_ZCL_ATTR_TYPE_INT16));
    TEST_ASSERT_EQUAL(1,clear_calls);TEST_ASSERT_EQUAL(0,calibration_calls);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,apply_source_attribute(FAN_ATTR_EXTERNAL_FIRST,&value,1,EZB_ZCL_ATTR_TYPE_INT16));
}

void test_EndpointFan_All(void) {
    RUN_TEST(test_EndpointFan_SignedDeltaAndSourcesShareValidatedCommandPath);
    RUN_TEST(test_EndpointFan_ExternalMeasurementAndClearNeverPersistConfiguration);
    RUN_TEST(test_EndpointFan_ConnectedReadsCheckedActualStateAndRejectsUnavailable);
    RUN_TEST(test_ReportRoutesRejectWrongEndpointAndExposePartialRefreshFailure);
    RUN_TEST(test_ReportClusterFailureReleasesOnlyUnattachedDescriptor);
    RUN_TEST(test_EndpointFan_DeepSleepUsesExistingControllerQueueWithoutCreatingTasks);
    RUN_TEST(test_EndpointFan_ControlWriteRejectsMalformedAndPreservesStateOnFailure);
    RUN_TEST(test_EndpointFan_ReportingRefreshDoesNotRewriteSettings);
    RUN_TEST(test_EndpointFan_AllReportingControlsPersistZero);
    RUN_TEST(test_EndpointFan_CalibrationPersistsBeforeApplying);
    RUN_TEST(test_EndpointFan_CalibrationRejectsMalformedInput);
    RUN_TEST(test_EndpointFan_CalibrationRequiresConfigurationService);
    RUN_TEST(test_EndpointFan_Identification);
    RUN_TEST(test_EndpointFan_LegacyDisplayUsesPersistentPath);
    RUN_TEST(test_EndpointFan_RejectsInvalidThreshold);
    RUN_TEST(test_EndpointFan_HandleAction_OnOff);
    RUN_TEST(test_EndpointFan_HandleAction_Thermostat);
}

#ifndef RACOOL_TARGET_TESTING
int main(void) {
    UNITY_BEGIN();
    test_EndpointFan_All();
    return UNITY_END();
}
#endif

