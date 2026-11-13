#include <math.h>
#include "Controllers/ClimateController/FanCommands.h"
#include "Controllers/ClimateController/IClimateController.h"
#include "Communication/Zigbee/Endpoints/FanSourceAttributes.h"
#if !defined(RACOOL_TARGET_TESTING) || defined(RACOOL_INTERNAL_TEST_ACCESS)
#include "Communication/Zigbee/Endpoints/EndpointFan.h"
#include "Communication/Zigbee/Endpoints/EndpointCommon.h"
#include "Communication/Zigbee/ZigbeeConfig.h"
#include "Management/ConfigurationManager/IConfigurationManager.h"
#include "Management/ConfigurationManager/ZigbeeReportingSettings.h"
#include "Management/ConfigurationManager/SystemConfigs.h"
#include "Core/LogManager/ILogManager.h"
#include "Core/EventBus/IEventBus.h"
#include "Core/ServiceLocator/ServiceLocator.h"
#include "Core/ServiceLocator/ServiceNames.h"
#include "Communication/Zigbee/IZigbee.h"
#include "Controllers/SystemController/ISystemController.h"
#include "Management/PowerManager/IPowerManager.h"
#include "Hardware/Relay/IRelay.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "esp_zigbee.h"
#include "ezbee/zcl/zcl_desc.h"
#ifndef RACOOL_TARGET_TESTING
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif
#include <string.h>

typedef struct {
    IZigbeeEndpoint base;
    uint8_t endpoint_id;
    uint16_t temp_val;
    uint16_t temp_min;
    uint16_t temp_max;
    bool on_off_state;
    int16_t cooling_setpoint;
    int16_t heating_setpoint;
    uint8_t system_mode;
    int8_t cal_val;
    uint8_t temp_unit;
    uint8_t  display_mode;       /**< 1=auto, 2=always-on */
    uint16_t oled_sleep_sec;     /**< Zigbee-configurable auto-sleep timeout (5-60s) */
    uint16_t rep_fan_s;
    uint16_t rep_ambient_s;
    uint16_t rep_cpu_s;
    uint16_t rep_health_s;
    uint16_t rep_memory_s;
    uint16_t rep_uptime_s;
    uint8_t reporting_refresh;
    uint8_t fan_mode;
    uint8_t object_source, ambient_source;
    int16_t delta_on, delta_off;
    FanSourceDiagnostics diagnostics;
    uint8_t source_values[7];
} EndpointFan;

static EndpointFan fanInstance;
static const char *TAG_Fan = "EndpointFan";


static const char *Fan_GetName(IZigbeeEndpoint *self) { return "FanSensorEndpoint"; }
static uint8_t Fan_GetEndpointId(IZigbeeEndpoint *self) { return ((EndpointFan *)self)->endpoint_id; }

static esp_err_t Fan_RegisterClusters(IZigbeeEndpoint *self, ezb_af_device_desc_t device) {
    if (!self || !device) return ESP_ERR_INVALID_ARG;
    EndpointFan *ep = (EndpointFan *)self;
    IConfigurationManager *config = ConfigurationManager_GetInstance();
    SystemSettings snapshot;
    if (!config || !config->GetSnapshot || config->GetSnapshot(config, &snapshot) != ESP_OK) return ESP_FAIL;
    const SystemSettings *settings = &snapshot;

    ep->fan_mode = settings->control.fan_manual_mode ? (settings->control.fan_manual_state ? 1 : 2) : (settings->control.fan_strategy == 1 ? 3 : 0);
    ep->object_source = settings->control.object_temperature_source;
    ep->ambient_source = settings->control.ambient_temperature_source;
    ep->delta_on = settings->control.fan_delta_on_decic;
    ep->delta_off = settings->control.fan_delta_off_decic;
    for (unsigned i=0; i<6; ++i) ep->diagnostics.temperatures[i] = INT16_MIN;
    ep->source_values[2] = ep->source_values[3] = TEMPERATURE_SOURCE_INVALID;
    ep->temp_val = 0;
    ep->temp_min = 0x954d;
    ep->temp_max = 0x7fff;
    ep->on_off_state = false;
    ep->cooling_setpoint = (int16_t)lroundf(settings->control.relay_open_temp * 100.0f);
    ep->heating_setpoint = (int16_t)lroundf(settings->control.relay_close_temp * 100.0f);
    ep->system_mode = ZIGBEE_FAN_MODE_AUTO;
    if (settings->control.fan_manual_mode) {
        ep->system_mode = settings->control.fan_manual_state ? ZIGBEE_FAN_MODE_ON : ZIGBEE_FAN_MODE_OFF;
    }
    ep->cal_val      = (int8_t)(settings->calibration.ir_object_cal * 10);
    ep->temp_unit    = settings->system.temp_unit;
    ep->display_mode = settings->system.display_mode;
    ep->rep_fan_s     = settings->zigbee_reporting.fan_s;
    ep->rep_ambient_s = settings->zigbee_reporting.ambient_s;
    ep->rep_cpu_s     = settings->zigbee_reporting.cpu_s;
    ep->rep_health_s  = settings->zigbee_reporting.health_s;
    ep->rep_memory_s  = settings->zigbee_reporting.memory_s;
    ep->rep_uptime_s  = settings->zigbee_reporting.uptime_s;
    // Clamp oled_sleep_sec to the Zigbee-accessible range (5-60 s)
    uint32_t raw_timeout = settings->system.oled_timeout_sec;
    ep->oled_sleep_sec = (uint16_t)((raw_timeout < OLED_SLEEP_ZB_MIN_SEC) ? OLED_SLEEP_ZB_MIN_SEC
                                   : (raw_timeout > OLED_SLEEP_ZB_MAX_SEC) ? OLED_SLEEP_ZB_MAX_SEC
                                   : raw_timeout);

    ezb_af_ep_config_t ep_cfg = { .ep_id = ZIGBEE_EP_SENSOR_FAN, .app_profile_id = EZB_AF_HA_PROFILE_ID, .app_device_id = EZB_ZHA_ON_OFF_OUTPUT_DEVICE_ID, .app_device_version = 1 };
    ezb_af_ep_desc_t endpoint = ezb_af_create_endpoint_desc(&ep_cfg);
    if (!endpoint) return ESP_ERR_NO_MEM;

    // Standard clusters (Basic, Identify)
    if (EndpointCommon_RegisterSharedClusters(endpoint, ZIGBEE_POWER_SOURCE) != ESP_OK) {
        ezb_af_free_endpoint_desc(endpoint); return ESP_FAIL;
    }

    // Temperature Measurement Cluster
    ezb_zcl_temperature_measurement_cluster_server_config_t temp_cfg = {
        .measured_value = ep->temp_val,
        .min_measured_value = ep->temp_min,
        .max_measured_value = ep->temp_max
    };
    if (ZigbeeEndpoint_AddCluster(endpoint, ezb_zcl_temperature_measurement_create_cluster_desc(&temp_cfg, EZB_ZCL_CLUSTER_SERVER)) != ESP_OK) {
        ezb_af_free_endpoint_desc(endpoint); return ESP_FAIL;
    }

    // On/Off Cluster
    if (ZigbeeEndpoint_AddCluster(endpoint, ezb_zcl_on_off_create_cluster_desc(NULL, EZB_ZCL_CLUSTER_SERVER)) != ESP_OK) {
        ezb_af_free_endpoint_desc(endpoint); return ESP_FAIL;
    }

    // Thermostat Cluster
    // Setpoint limits use the shared Celsius bounds in ZCL hundredths.
    // These limit attributes constrain what HA/Z2M shows as slider range.
    static int16_t s_setpoint_min = (int16_t)(FAN_SETPOINT_MIN_C * 100);
    static int16_t s_setpoint_max = (int16_t)(FAN_SETPOINT_MAX_C * 100);
    ezb_zcl_thermostat_cluster_server_config_t thermostat_config = {
        .local_temperature = (int16_t)ep->temp_val,
        .control_sequence_of_operation = EZB_ZCL_THERMOSTAT_CONTROL_SEQUENCE_OF_OPERATION_COOLING_ONLY,
        .system_mode = ep->system_mode
    };
    ezb_zcl_cluster_desc_t thermo_cluster = ezb_zcl_thermostat_create_cluster_desc(&thermostat_config, EZB_ZCL_CLUSTER_SERVER);
    if (!thermo_cluster) { ezb_af_free_endpoint_desc(endpoint); return ESP_ERR_NO_MEM; }
    if (ezb_zcl_thermostat_cluster_desc_add_attr(thermo_cluster, EZB_ZCL_ATTR_THERMOSTAT_OCCUPIED_COOLING_SETPOINT_ID, &ep->cooling_setpoint) != EZB_ERR_NONE) {
        ezb_zcl_free_cluster_desc(thermo_cluster); ezb_af_free_endpoint_desc(endpoint); return ESP_FAIL;
    }
    if (ezb_zcl_thermostat_cluster_desc_add_attr(thermo_cluster, EZB_ZCL_ATTR_THERMOSTAT_OCCUPIED_HEATING_SETPOINT_ID, &ep->heating_setpoint) != EZB_ERR_NONE) {
        ezb_zcl_free_cluster_desc(thermo_cluster); ezb_af_free_endpoint_desc(endpoint); return ESP_FAIL;
    }
    // Min/Max setpoint limit attributes (ZCL 0x0015-0x0018): match the shared fan setpoint bounds
    if (ezb_zcl_thermostat_cluster_desc_add_attr(thermo_cluster, EZB_ZCL_ATTR_THERMOSTAT_MIN_HEAT_SETPOINT_LIMIT_ID, &s_setpoint_min) != EZB_ERR_NONE) {
        ezb_zcl_free_cluster_desc(thermo_cluster); ezb_af_free_endpoint_desc(endpoint); return ESP_FAIL;
    }
    if (ezb_zcl_thermostat_cluster_desc_add_attr(thermo_cluster, EZB_ZCL_ATTR_THERMOSTAT_MAX_HEAT_SETPOINT_LIMIT_ID, &s_setpoint_max) != EZB_ERR_NONE) {
        ezb_zcl_free_cluster_desc(thermo_cluster); ezb_af_free_endpoint_desc(endpoint); return ESP_FAIL;
    }
    if (ezb_zcl_thermostat_cluster_desc_add_attr(thermo_cluster, EZB_ZCL_ATTR_THERMOSTAT_MIN_COOL_SETPOINT_LIMIT_ID, &s_setpoint_min) != EZB_ERR_NONE) {
        ezb_zcl_free_cluster_desc(thermo_cluster); ezb_af_free_endpoint_desc(endpoint); return ESP_FAIL;
    }
    if (ezb_zcl_thermostat_cluster_desc_add_attr(thermo_cluster, EZB_ZCL_ATTR_THERMOSTAT_MAX_COOL_SETPOINT_LIMIT_ID, &s_setpoint_max) != EZB_ERR_NONE) {
        ezb_zcl_free_cluster_desc(thermo_cluster); ezb_af_free_endpoint_desc(endpoint); return ESP_FAIL;
    }
    if (ezb_zcl_thermostat_cluster_desc_add_attr(thermo_cluster, EZB_ZCL_ATTR_THERMOSTAT_LOCAL_TEMPERATURE_CALIBRATION_ID, &ep->cal_val) != EZB_ERR_NONE) {
        ezb_zcl_free_cluster_desc(thermo_cluster); ezb_af_free_endpoint_desc(endpoint); return ESP_FAIL;
    }
    if (ZigbeeEndpoint_AddCluster(endpoint, thermo_cluster) != ESP_OK) {
        ezb_af_free_endpoint_desc(endpoint); return ESP_FAIL;
    }


    // Thermostat UI Config Cluster — display mode + sleep timeout as READ_WRITE attributes
    ezb_zcl_thermostat_ui_config_cluster_server_config_t ui_config = {
        .temperature_display_mode = ep->temp_unit,
        .keypad_lockout = EZB_ZCL_THERMOSTAT_UI_CONFIG_KEYPAD_LOCKOUT_DEFAULT_VALUE
    };
    ezb_zcl_cluster_desc_t ui_cluster = ezb_zcl_thermostat_ui_config_create_cluster_desc(&ui_config, EZB_ZCL_CLUSTER_SERVER);
    if (!ui_cluster) { ezb_af_free_endpoint_desc(endpoint); return ESP_ERR_NO_MEM; }
    // Custom READ_WRITE attributes: 0x8020 = display_mode, 0x8021 = oled_sleep_sec
    if (ezb_zcl_cluster_desc_add_manuf_attr(ui_cluster, 0x8020, 0x20,
        EZB_ZCL_ATTR_ACCESS_READ_WRITE, EZB_ZCL_STD_MANUF_CODE, &ep->display_mode) != EZB_ERR_NONE) {
        ezb_zcl_free_cluster_desc(ui_cluster); ezb_af_free_endpoint_desc(endpoint); return ESP_FAIL;
    }
    if (ezb_zcl_cluster_desc_add_manuf_attr(ui_cluster, 0x8021, 0x21,  // uint16
        EZB_ZCL_ATTR_ACCESS_READ_WRITE, EZB_ZCL_STD_MANUF_CODE, &ep->oled_sleep_sec) != EZB_ERR_NONE) {
        ezb_zcl_free_cluster_desc(ui_cluster); ezb_af_free_endpoint_desc(endpoint); return ESP_FAIL;
    }
    if (ezb_zcl_cluster_desc_add_manuf_attr(ui_cluster, 0x8030, 0x21, EZB_ZCL_ATTR_ACCESS_READ_WRITE, EZB_ZCL_STD_MANUF_CODE, &ep->rep_fan_s) != EZB_ERR_NONE) {
        ezb_zcl_free_cluster_desc(ui_cluster); ezb_af_free_endpoint_desc(endpoint); return ESP_FAIL;
    }
    if (ezb_zcl_cluster_desc_add_manuf_attr(ui_cluster, 0x8031, 0x21, EZB_ZCL_ATTR_ACCESS_READ_WRITE, EZB_ZCL_STD_MANUF_CODE, &ep->rep_ambient_s) != EZB_ERR_NONE) {
        ezb_zcl_free_cluster_desc(ui_cluster); ezb_af_free_endpoint_desc(endpoint); return ESP_FAIL;
    }
    if (ezb_zcl_cluster_desc_add_manuf_attr(ui_cluster, 0x8032, 0x21, EZB_ZCL_ATTR_ACCESS_READ_WRITE, EZB_ZCL_STD_MANUF_CODE, &ep->rep_cpu_s) != EZB_ERR_NONE) {
        ezb_zcl_free_cluster_desc(ui_cluster); ezb_af_free_endpoint_desc(endpoint); return ESP_FAIL;
    }
    if (ezb_zcl_cluster_desc_add_manuf_attr(ui_cluster, 0x8033, 0x21, EZB_ZCL_ATTR_ACCESS_READ_WRITE, EZB_ZCL_STD_MANUF_CODE, &ep->rep_health_s) != EZB_ERR_NONE) {
        ezb_zcl_free_cluster_desc(ui_cluster); ezb_af_free_endpoint_desc(endpoint); return ESP_FAIL;
    }
    if (ezb_zcl_cluster_desc_add_manuf_attr(ui_cluster, 0x8034, 0x21, EZB_ZCL_ATTR_ACCESS_READ_WRITE, EZB_ZCL_STD_MANUF_CODE, &ep->rep_memory_s) != EZB_ERR_NONE) {
        ezb_zcl_free_cluster_desc(ui_cluster); ezb_af_free_endpoint_desc(endpoint); return ESP_FAIL;
    }
    if (ezb_zcl_cluster_desc_add_manuf_attr(ui_cluster, 0x8035, 0x21, EZB_ZCL_ATTR_ACCESS_READ_WRITE, EZB_ZCL_STD_MANUF_CODE, &ep->rep_uptime_s) != EZB_ERR_NONE) {
        ezb_zcl_free_cluster_desc(ui_cluster); ezb_af_free_endpoint_desc(endpoint); return ESP_FAIL;
    }
    if (ezb_zcl_cluster_desc_add_manuf_attr(ui_cluster, 0x8036, EZB_ZCL_ATTR_TYPE_UINT8,
        EZB_ZCL_ATTR_ACCESS_READ_WRITE, EZB_ZCL_STD_MANUF_CODE, &ep->reporting_refresh) != EZB_ERR_NONE) {
        ezb_zcl_free_cluster_desc(ui_cluster); ezb_af_free_endpoint_desc(endpoint); return ESP_FAIL;
    }
    struct {uint16_t id; uint8_t type; void *value; bool writable;} source_attributes[] = {
        {FAN_ATTR_MODE, EZB_ZCL_ATTR_TYPE_UINT8, &ep->fan_mode, true},
        {FAN_ATTR_OBJECT_SOURCE, EZB_ZCL_ATTR_TYPE_UINT8, &ep->object_source, true},
        {FAN_ATTR_AMBIENT_SOURCE, EZB_ZCL_ATTR_TYPE_UINT8, &ep->ambient_source, true},
        {FAN_ATTR_DELTA_ON, EZB_ZCL_ATTR_TYPE_INT16, &ep->delta_on, true},
        {FAN_ATTR_DELTA_OFF, EZB_ZCL_ATTR_TYPE_INT16, &ep->delta_off, true},
    };
    for (unsigned i=0; i<sizeof(source_attributes)/sizeof(source_attributes[0]); ++i) {
        if (ezb_zcl_cluster_desc_add_manuf_attr(ui_cluster, source_attributes[i].id, source_attributes[i].type,
            EZB_ZCL_ATTR_ACCESS_READ_WRITE, EZB_ZCL_STD_MANUF_CODE, source_attributes[i].value) != EZB_ERR_NONE) {
            ezb_zcl_free_cluster_desc(ui_cluster); ezb_af_free_endpoint_desc(endpoint); return ESP_FAIL;
        }
    }
    for (unsigned i=0; i<6; ++i) {
        if (ezb_zcl_cluster_desc_add_manuf_attr(ui_cluster, FAN_ATTR_EXTERNAL_FIRST+i, EZB_ZCL_ATTR_TYPE_INT16,
            (i<3 ? EZB_ZCL_ATTR_ACCESS_READ_WRITE : EZB_ZCL_ATTR_ACCESS_READ_ONLY) | EZB_ZCL_ATTR_ACCESS_REPORTING,
            EZB_ZCL_STD_MANUF_CODE, &ep->diagnostics.temperatures[i]) != EZB_ERR_NONE) {
            ezb_zcl_free_cluster_desc(ui_cluster); ezb_af_free_endpoint_desc(endpoint); return ESP_FAIL;
        }
    }
    for (unsigned i=0; i<7; ++i) {
        if (ezb_zcl_cluster_desc_add_manuf_attr(ui_cluster, FAN_ATTR_EFFECTIVE_OBJECT_SOURCE+i,
            EZB_ZCL_ATTR_TYPE_UINT8, EZB_ZCL_ATTR_ACCESS_READ_ONLY | EZB_ZCL_ATTR_ACCESS_REPORTING, EZB_ZCL_STD_MANUF_CODE, &ep->source_values[i]) != EZB_ERR_NONE) {
            ezb_zcl_free_cluster_desc(ui_cluster); ezb_af_free_endpoint_desc(endpoint); return ESP_FAIL;
        }
    }
    uint8_t system_cmd = 0;
    if (ezb_zcl_cluster_desc_add_manuf_attr(ui_cluster, 0x8040, 0x20, EZB_ZCL_ATTR_ACCESS_READ_WRITE, EZB_ZCL_STD_MANUF_CODE, &system_cmd) != EZB_ERR_NONE) {
        ezb_zcl_free_cluster_desc(ui_cluster); ezb_af_free_endpoint_desc(endpoint); return ESP_FAIL;
    }
    if (ZigbeeEndpoint_AddCluster(endpoint, ui_cluster) != ESP_OK) {
        ezb_af_free_endpoint_desc(endpoint); return ESP_FAIL;
    }

    if (ezb_af_device_add_endpoint_desc(device, endpoint) != EZB_ERR_NONE) {
        ezb_af_free_endpoint_desc(endpoint); return ESP_FAIL;
    }
    ESP_LOGI(TAG_Fan, "Registered EP%d (Fan+Thermostat)", ZIGBEE_EP_SENSOR_FAN);
    return ESP_OK;
}

static esp_err_t Fan_ConfigureReporting(IZigbeeEndpoint *self) {
    IConfigurationManager *config = ConfigurationManager_GetInstance();
    SystemSettings snapshot;
    if (!config || !config->GetSnapshot || config->GetSnapshot(config, &snapshot) != ESP_OK) return ESP_FAIL;
    const SystemSettings *settings = &snapshot;
    
    uint16_t max_interval = settings->zigbee_reporting.fan_s;


    ezb_zcl_reporting_info_t info = ezb_zcl_reporting_info_find(
        ZIGBEE_EP_SENSOR_FAN,
        EZB_ZCL_CLUSTER_ID_TEMPERATURE_MEASUREMENT,
        EZB_ZCL_CLUSTER_SERVER,
        EZB_ZCL_ATTR_TEMPERATURE_MEASUREMENT_MEASURED_VALUE_ID,
        EZB_ZCL_STD_MANUF_CODE);

    if (info) {


        ezb_zcl_attr_variable_t delta = {
            .s16 = ZIGBEE_REPORT_TEMP_THRESHOLD
        };
        esp_err_t result = ZigbeeEndpoint_ApplyReporting(info, ZIGBEE_REPORT_MIN_INTERVAL, max_interval, &delta);
        if (result != ESP_OK) return result;
        ezb_zcl_reporting_info_t relay_info = ezb_zcl_reporting_info_find(ZIGBEE_EP_SENSOR_FAN,
            EZB_ZCL_CLUSTER_ID_ON_OFF, EZB_ZCL_CLUSTER_SERVER, EZB_ZCL_ATTR_ON_OFF_ON_OFF_ID,
            EZB_ZCL_STD_MANUF_CODE);
        if (!relay_info) return ESP_ERR_NOT_FOUND;
        /* Discrete output changes have no periodic reporting deadline. The
         * queue additionally submits each edge explicitly under SDK ownership. */
        /* SDK 2.0.4 dereferences delta even for discrete Boolean attributes. */
        ezb_zcl_attr_variable_t relay_delta = {0};
        return ezb_zcl_reporting_info_update(relay_info, 0, 0, &relay_delta) == EZB_ERR_NONE ? ESP_OK : ESP_FAIL;
    }
    return ESP_ERR_NOT_FOUND;
}

static bool Fan_HandleAction(IZigbeeEndpoint *self, ezb_zcl_core_action_callback_id_t callback_id,
                          const void *message, esp_err_t *ret_status) {
    if (callback_id != EZB_ZCL_CORE_SET_ATTR_VALUE_CB_ID || !message || !ret_status) return false;

    ezb_zcl_set_attr_value_message_t *msg = (ezb_zcl_set_attr_value_message_t *)message;
    if (msg->info.dst_ep != ZIGBEE_EP_SENSOR_FAN) return false;

    if (ZigbeeEndpoint_HandleReportingSetting(msg, ret_status)) return true;
    if (ZigbeeEndpoint_HandleSystemCommand(msg, ret_status)) return true;
    if (ZigbeeEndpoint_HandleDisplaySetting(msg, ret_status)) return true;

    IConfigurationManager *config = ConfigurationManager_GetInstance();
    if (msg->info.cluster_id == EZB_ZCL_CLUSTER_ID_THERMOSTAT_UI_CONFIG &&
        msg->in.attribute.id >= FAN_ATTR_MODE && msg->in.attribute.id <= FAN_ATTR_EXTERNAL_FIRST+2) {
        uint16_t id = msg->in.attribute.id;
        bool signed_value = id >= FAN_ATTR_DELTA_ON;
        *ret_status = ESP_ERR_INVALID_ARG;
        if (!msg->in.attribute.data.value || msg->in.attribute.data.size != (signed_value ? 2 : 1) ||
            msg->in.attribute.data.type != (signed_value ? EZB_ZCL_ATTR_TYPE_INT16 : EZB_ZCL_ATTR_TYPE_UINT8)) return true;
        int16_t raw = 0;
        if (signed_value) memcpy(&raw, msg->in.attribute.data.value, 2);
        else raw = *(const uint8_t *)msg->in.attribute.data.value;
        if (id >= FAN_ATTR_EXTERNAL_FIRST) {
            IClimateController *controller = (IClimateController *)ServiceLocator_GetService(SVC_NAME_CLIMATE_CONTROLLER);
            if (raw==INT16_MIN) {
                *ret_status=controller && controller->ClearExternalTemperature ? controller->ClearExternalTemperature(controller,
                    (uint8_t)(id-FAN_ATTR_EXTERNAL_FIRST+1)) : ESP_ERR_INVALID_STATE;
                return true;
            }
            *ret_status = controller && controller->SetExternalTemperature ? controller->SetExternalTemperature(controller,
                (uint8_t)(id-FAN_ATTR_EXTERNAL_FIRST+1), raw/100.0f) : ESP_ERR_INVALID_STATE;
            return true;
        }
        const FanCommandType types[] = {FAN_COMMAND_MODE, FAN_COMMAND_OBJECT_SOURCE, FAN_COMMAND_AMBIENT_SOURCE,
            FAN_COMMAND_DELTA_ON, FAN_COMMAND_DELTA_OFF};
        *ret_status = FanCommands_Dispatch(config, (FanCommand){types[id-FAN_ATTR_MODE], signed_value ? raw/10.0f : raw, 0}, true);
        return true;
    }
    if (msg->info.cluster_id == EZB_ZCL_CLUSTER_ID_THERMOSTAT &&
        msg->in.attribute.id == EZB_ZCL_ATTR_THERMOSTAT_LOCAL_TEMPERATURE_CALIBRATION_ID) {
        *ret_status = ESP_ERR_INVALID_ARG;
        if (!msg->in.attribute.data.value || msg->in.attribute.data.size != sizeof(int8_t) ||
            msg->in.attribute.data.type != EZB_ZCL_ATTR_TYPE_INT8) return true;
        int8_t raw;
        memcpy(&raw, msg->in.attribute.data.value, sizeof(raw));
        float calibration = raw / 10.0f;
        if (calibration < CALIBRATION_MIN || calibration > CALIBRATION_MAX) return true;
        if (!config || !config->UpdateFields) {
            *ret_status = ESP_ERR_INVALID_STATE;
            return true;
        }
        ConfigurationFieldUpdate field = {
            .offset = offsetof(CalibrationConfigs, ir_object_cal),
            .data = &calibration, .size = sizeof(calibration)
        };
        *ret_status = config->UpdateFields(config, "calibration", &field, 1, true);
        if (*ret_status == ESP_OK) ESP_LOGI(TAG_Fan, "Radiator calibration saved: %.1f", calibration);
        return true;
    }
    if (!msg->in.attribute.data.value) { *ret_status = ESP_ERR_INVALID_ARG; return true; }
    if (!config || !config->UpdateFields) { *ret_status = ESP_ERR_INVALID_STATE; return true; }
    bool handled = false;
    uint16_t attr = msg->in.attribute.id;
    bool onoff = msg->info.cluster_id == EZB_ZCL_CLUSTER_ID_ON_OFF && attr == EZB_ZCL_ATTR_ON_OFF_ON_OFF_ID;
    bool thermostat = msg->info.cluster_id == EZB_ZCL_CLUSTER_ID_THERMOSTAT;
    bool setpoint = thermostat && (attr == EZB_ZCL_ATTR_THERMOSTAT_OCCUPIED_COOLING_SETPOINT_ID ||
                                  attr == EZB_ZCL_ATTR_THERMOSTAT_OCCUPIED_HEATING_SETPOINT_ID);
    bool mode = thermostat && attr == EZB_ZCL_ATTR_THERMOSTAT_SYSTEM_MODE_ID;
    if (onoff || setpoint || mode) {
        *ret_status = ESP_ERR_INVALID_ARG;
        uint8_t type = setpoint ? EZB_ZCL_ATTR_TYPE_INT16 : onoff ? EZB_ZCL_ATTR_TYPE_BOOL : EZB_ZCL_ATTR_TYPE_ENUM8;
        if (msg->in.attribute.data.size != (setpoint ? sizeof(int16_t) : sizeof(uint8_t)) ||
            msg->in.attribute.data.type != type) return true;
        FanCommand command = {0};
        if (setpoint) {
            int16_t raw; memcpy(&raw, msg->in.attribute.data.value, sizeof(raw));
            command = (FanCommand){attr == EZB_ZCL_ATTR_THERMOSTAT_OCCUPIED_COOLING_SETPOINT_ID ? FAN_COMMAND_OBJECT_ON : FAN_COMMAND_OBJECT_OFF, raw/100.0f, 0};
        } else {
            uint8_t raw = *(const uint8_t *)msg->in.attribute.data.value;
            ClimateFanMode selected;
            if (onoff) { if (raw > 1) return true; selected = raw ? CLIMATE_FAN_MANUAL_ON : CLIMATE_FAN_MANUAL_OFF; }
            else if (raw == ZIGBEE_FAN_MODE_AUTO) selected = CLIMATE_FAN_OBJECT_SENSITIVE;
            else if (raw == ZIGBEE_FAN_MODE_ON) selected = CLIMATE_FAN_MANUAL_ON;
            else if (raw == ZIGBEE_FAN_MODE_OFF) selected = CLIMATE_FAN_MANUAL_OFF;
            else return true;
            command = (FanCommand){FAN_COMMAND_MODE, selected, 0};
        }
        *ret_status = FanCommands_Dispatch(config, command, true);
        return true;
    }
    if (msg->info.cluster_id == EZB_ZCL_CLUSTER_ID_THERMOSTAT_UI_CONFIG) {
        if (msg->in.attribute.id == EZB_ZCL_ATTR_THERMOSTAT_UI_CONFIG_TEMPERATURE_DISPLAY_MODE_ID) {
            *ret_status = ESP_ERR_INVALID_ARG;
            if (msg->in.attribute.data.size != 1 || msg->in.attribute.data.type != EZB_ZCL_ATTR_TYPE_ENUM8) return true;
            uint8_t unit;
            memcpy(&unit, msg->in.attribute.data.value, sizeof(unit));
            ConfigurationFieldUpdate field = {offsetof(SystemBaseConfigs, temp_unit), &unit, sizeof(unit)};
            *ret_status = config->UpdateFields(config, "system", &field, 1, true);
            return true;
        } else if (msg->in.attribute.id == 0x8036) {
            *ret_status = ESP_ERR_INVALID_ARG;
            if (msg->in.attribute.data.size != 1 || msg->in.attribute.data.type != EZB_ZCL_ATTR_TYPE_UINT8 ||
                *(uint8_t *)msg->in.attribute.data.value != 0) return true;
            IZigbee *zb = (IZigbee *)ServiceLocator_GetService(SVC_NAME_ZIGBEE);
            if (!zb || !zb->ReconfigureAllReporting) {
                *ret_status = ESP_ERR_INVALID_STATE;
                return true;
            }
            /* Refresh runtime reporting after coordinator configuration without rewriting settings. */
            *ret_status = zb->ReconfigureAllReporting(zb) == 0 ? ESP_OK : ESP_FAIL;
            return true;
        }
    }

    if (handled) {
        *ret_status = ESP_OK;
    }
    return handled;
}

/* Called under SDK ownership. Relay read is explicitly nonblocking. */
static esp_err_t Fan_OnConnected(IZigbeeEndpoint *self) {
    IRelay *relay = (IRelay *)ServiceLocator_GetService(SVC_NAME_RELAY);
    bool state;
    if (!relay || !relay->ReadState) return ESP_ERR_INVALID_STATE;
    esp_err_t result = relay->ReadState(relay, &state);
    if (result != ESP_OK) return result;
    ezb_zcl_status_t status = ezb_zcl_set_attr_value(ZIGBEE_EP_SENSOR_FAN,
        EZB_ZCL_CLUSTER_ID_ON_OFF, EZB_ZCL_CLUSTER_SERVER, EZB_ZCL_ATTR_ON_OFF_ON_OFF_ID,
        EZB_ZCL_STD_MANUF_CODE, &state, false);
    if (status != EZB_ZCL_STATUS_SUCCESS) return ESP_FAIL;
    ((EndpointFan *)self)->on_off_state = state;
    return ESP_OK;
}

IZigbeeEndpoint *EndpointFan_Create(void) {
    memset(&fanInstance, 0, sizeof(fanInstance));
    fanInstance.base.GetName = Fan_GetName;
    fanInstance.base.GetEndpointId = Fan_GetEndpointId;
    fanInstance.base.RegisterClusters = Fan_RegisterClusters;
    fanInstance.base.ConfigureReporting = Fan_ConfigureReporting;
    fanInstance.base.HandleAction = Fan_HandleAction;
    fanInstance.base.OnConnected = Fan_OnConnected;
    fanInstance.base.OnDisconnected = IZigbeeEndpoint_DefaultOnDisconnected;
    fanInstance.base.Destroy = IZigbeeEndpoint_DefaultDestroy;
    fanInstance.endpoint_id = ZIGBEE_EP_SENSOR_FAN;
    return &fanInstance.base;
}

#endif // RACOOL_TARGET_TESTING) && !defined(RACOOL_INTERNAL_TEST_ACCESS
