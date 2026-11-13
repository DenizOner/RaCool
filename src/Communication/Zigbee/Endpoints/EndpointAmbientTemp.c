#if !defined(RACOOL_TARGET_TESTING) || defined(RACOOL_INTERNAL_TEST_ACCESS)
#include "Communication/Zigbee/Endpoints/EndpointAmbientTemp.h"
#include "Communication/Zigbee/Endpoints/EndpointCommon.h"
#include "Communication/Zigbee/ZigbeeConfig.h"
#include "Management/ConfigurationManager/IConfigurationManager.h"
#include "Core/LogManager/ILogManager.h"
#include "Core/EventBus/IEventBus.h"
#include "Core/ServiceLocator/ServiceLocator.h"
#include "esp_zigbee.h"
#include "ezbee/zcl/cluster/temperature_measurement_desc.h"
#include "ezbee/zcl/cluster/thermostat_desc.h"
#include <string.h>

typedef struct {
    IZigbeeEndpoint base;
    uint8_t endpoint_id;
    uint8_t basic_power_source;
    uint16_t identify_time;
    uint16_t temp_val;
    uint16_t temp_min;
    uint16_t temp_max;
    int8_t cal_val;
} EndpointAmbientTemp;

static EndpointAmbientTemp ambientInstance;
static const char *TAG_Ambient = "AmbientTemp";

static const char *Ambient_GetName(IZigbeeEndpoint *self) { return "AmbientTempEndpoint"; }
static uint8_t Ambient_GetEndpointId(IZigbeeEndpoint *self) { return ((EndpointAmbientTemp *)self)->endpoint_id; }

static esp_err_t Ambient_RegisterClusters(IZigbeeEndpoint *self, ezb_af_device_desc_t device_desc) {
    if (!self || !device_desc) return ESP_ERR_INVALID_ARG;
    EndpointAmbientTemp *ep = (EndpointAmbientTemp *)self;
    IConfigurationManager *config = ConfigurationManager_GetInstance();
    SystemSettings snapshot;
    if (!config || !config->GetSnapshot || config->GetSnapshot(config, &snapshot) != ESP_OK) return ESP_FAIL;
    const SystemSettings *settings = &snapshot;

    ep->basic_power_source = ZIGBEE_POWER_SOURCE;
    ep->identify_time = 0;
    ep->temp_val = 0x0000;
    ep->temp_min = 0x954d; // -273.15 C
    ep->temp_max = 0x7fff;
    ep->cal_val = (int8_t)(settings->calibration.ir_ambient_cal * 10);

    ezb_af_ep_config_t ep_cfg = { .ep_id = ZIGBEE_EP_AMBIENT_TEMP, .app_profile_id = EZB_AF_HA_PROFILE_ID, .app_device_id = 0x0302, .app_device_version = 1 };
    ezb_af_ep_desc_t ep_desc = ezb_af_create_endpoint_desc(&ep_cfg);
    if (!ep_desc) return ESP_ERR_NO_MEM; // Temperature Sensor Device ID

    // Standard clusters (Basic, Identify)
    if (EndpointCommon_RegisterSharedClusters(ep_desc, ep->basic_power_source) != ESP_OK) {
        ezb_af_free_endpoint_desc(ep_desc); return ESP_FAIL;
    }

    // Temperature Measurement Cluster
    ezb_zcl_temperature_measurement_cluster_server_config_t temp_cfg = {
        .measured_value = ep->temp_val,
        .min_measured_value = ep->temp_min,
        .max_measured_value = ep->temp_max
    };
    ezb_zcl_cluster_desc_t temp_cluster = ezb_zcl_temperature_measurement_create_cluster_desc(&temp_cfg, EZB_ZCL_CLUSTER_SERVER);
    if (!temp_cluster) { ezb_af_free_endpoint_desc(ep_desc); return ESP_ERR_NO_MEM; }
    if (ZigbeeEndpoint_AddCluster(ep_desc, temp_cluster) != ESP_OK) {
        ezb_af_free_endpoint_desc(ep_desc); return ESP_FAIL;
    }

    // Thermostat Cluster (just for calibration attribute)
    ezb_zcl_cluster_desc_t thermo_cluster = ezb_zcl_thermostat_create_cluster_desc(NULL, EZB_ZCL_CLUSTER_SERVER);
    if (!thermo_cluster) { ezb_af_free_endpoint_desc(ep_desc); return ESP_ERR_NO_MEM; }
    if (ezb_zcl_thermostat_cluster_desc_add_attr(thermo_cluster, EZB_ZCL_ATTR_THERMOSTAT_LOCAL_TEMPERATURE_CALIBRATION_ID, &ep->cal_val) != EZB_ERR_NONE) {
        ezb_zcl_free_cluster_desc(thermo_cluster); ezb_af_free_endpoint_desc(ep_desc); return ESP_FAIL;
    }
    if (ZigbeeEndpoint_AddCluster(ep_desc, thermo_cluster) != ESP_OK) {
        ezb_af_free_endpoint_desc(ep_desc); return ESP_FAIL;
    }

    // Add endpoint to device
    if (ZigbeeEndpoint_RegisterReportingCluster(ep_desc, ZIGBEE_EP_AMBIENT_TEMP) != ESP_OK) {
        ezb_af_free_endpoint_desc(ep_desc);
        return ESP_FAIL;
    }
    if (ezb_af_device_add_endpoint_desc(device_desc, ep_desc) != EZB_ERR_NONE) {
        ezb_af_free_endpoint_desc(ep_desc); return ESP_FAIL;
    }

    ESP_LOGI(TAG_Ambient, "Registered EP%d (Ambient Temp)", ZIGBEE_EP_AMBIENT_TEMP);
    return ESP_OK;
}

static esp_err_t Ambient_ConfigureReporting(IZigbeeEndpoint *self) {
    int16_t temp_change = ZIGBEE_REPORT_TEMP_THRESHOLD;
    IConfigurationManager *config = ConfigurationManager_GetInstance();
    SystemSettings snapshot;
    if (!config || !config->GetSnapshot || config->GetSnapshot(config, &snapshot) != ESP_OK) return ESP_FAIL;
    const SystemSettings *settings = &snapshot;
    
    uint16_t max_interval = settings->zigbee_reporting.ambient_s;


    ezb_zcl_reporting_info_t info = ezb_zcl_reporting_info_find(
        ZIGBEE_EP_AMBIENT_TEMP,
        EZB_ZCL_CLUSTER_ID_TEMPERATURE_MEASUREMENT,
        EZB_ZCL_CLUSTER_SERVER,
        EZB_ZCL_ATTR_TEMPERATURE_MEASUREMENT_MEASURED_VALUE_ID,
        EZB_ZCL_STD_MANUF_CODE);

    if (info) {

        ezb_zcl_attr_variable_t delta = {
            .s16 = temp_change
        };
        return ZigbeeEndpoint_ApplyReporting(info, ZIGBEE_REPORT_MIN_INTERVAL, max_interval, &delta);
    }
    return ESP_ERR_NOT_FOUND;
}

static bool Ambient_HandleAction(IZigbeeEndpoint *self, ezb_zcl_core_action_callback_id_t callback_id,
                          const void *message, esp_err_t *ret_status) {
    (void)self;
    if (callback_id != EZB_ZCL_CORE_SET_ATTR_VALUE_CB_ID || !message || !ret_status) return false;
    const ezb_zcl_set_attr_value_message_t *msg = message;
    if (msg->info.dst_ep != ZIGBEE_EP_AMBIENT_TEMP) return false;
    if (ZigbeeEndpoint_HandleReportingSetting(msg, ret_status)) return true;
    if (
        msg->info.cluster_id != EZB_ZCL_CLUSTER_ID_THERMOSTAT ||
        msg->in.attribute.id != EZB_ZCL_ATTR_THERMOSTAT_LOCAL_TEMPERATURE_CALIBRATION_ID) return false;
    *ret_status = ESP_ERR_INVALID_ARG;
    if (!msg->in.attribute.data.value || msg->in.attribute.data.size != sizeof(int8_t) ||
        msg->in.attribute.data.type != EZB_ZCL_ATTR_TYPE_INT8) return true;
    int8_t raw;
    memcpy(&raw, msg->in.attribute.data.value, sizeof(raw));
    float calibration = raw / 10.0f;
    if (calibration < CALIBRATION_MIN || calibration > CALIBRATION_MAX) return true;
    IConfigurationManager *config = ConfigurationManager_GetInstance();
    if (!config || !config->UpdateFields) {
        *ret_status = ESP_ERR_INVALID_STATE;
        return true;
    }
    ConfigurationFieldUpdate field = {
        .offset = offsetof(CalibrationConfigs, ir_ambient_cal),
        .data = &calibration, .size = sizeof(calibration)
    };
    *ret_status = config->UpdateFields(config, "calibration", &field, 1, true);
    if (*ret_status == ESP_OK) ESP_LOGI(TAG_Ambient, "Ambient calibration saved: %.1f", calibration);
    return true;
}

IZigbeeEndpoint *EndpointAmbientTemp_Create(void) {
    memset(&ambientInstance, 0, sizeof(ambientInstance));
    ambientInstance.base.GetName = Ambient_GetName;
    ambientInstance.base.GetEndpointId = Ambient_GetEndpointId;
    ambientInstance.base.RegisterClusters = Ambient_RegisterClusters;
    ambientInstance.base.ConfigureReporting = Ambient_ConfigureReporting;
    ambientInstance.base.HandleAction = Ambient_HandleAction;
    ambientInstance.base.OnConnected = IZigbeeEndpoint_DefaultOnConnected;
    ambientInstance.base.OnDisconnected = IZigbeeEndpoint_DefaultOnDisconnected;
    ambientInstance.base.Destroy = IZigbeeEndpoint_DefaultDestroy;
    ambientInstance.endpoint_id = ZIGBEE_EP_AMBIENT_TEMP;
    return &ambientInstance.base;
}

#endif // RACOOL_TARGET_TESTING) && !defined(RACOOL_INTERNAL_TEST_ACCESS
