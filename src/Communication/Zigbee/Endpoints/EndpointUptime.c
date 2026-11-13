#if !defined(RACOOL_TARGET_TESTING) || defined(RACOOL_INTERNAL_TEST_ACCESS)
#include "Communication/Zigbee/Endpoints/EndpointUptime.h"
#include "Communication/Zigbee/Endpoints/EndpointCommon.h"
#include "Communication/Zigbee/ZigbeeConfig.h"
#include "Management/ConfigurationManager/IConfigurationManager.h"
#include "Management/ConfigurationManager/SystemConfigs.h"
#include "esp_zigbee.h"
#include <string.h>

typedef struct {
    IZigbeeEndpoint base;
    float present_value;
} EndpointUptime;

static EndpointUptime uptimeInstance;

static const char *Uptime_GetName(IZigbeeEndpoint *self) { return "System Uptime"; }
static uint8_t Uptime_GetEndpointId(IZigbeeEndpoint *self) { return ZIGBEE_EP_UPTIME; }

static esp_err_t Uptime_RegisterClusters(IZigbeeEndpoint *self, ezb_af_device_desc_t device) {
    if (!self || !device) return ESP_ERR_INVALID_ARG;
    EndpointUptime *ep = (EndpointUptime *)self;
    ezb_af_ep_config_t ep_cfg = { .ep_id = ZIGBEE_EP_UPTIME, .app_profile_id = EZB_AF_HA_PROFILE_ID, .app_device_id = EZB_ZHA_SIMPLE_SENSOR_DEVICE_ID, .app_device_version = 1 };
    ezb_af_ep_desc_t endpoint = ezb_af_create_endpoint_desc(&ep_cfg);
    if (!endpoint) return ESP_ERR_NO_MEM;

    ezb_zcl_analog_input_cluster_server_config_t cfg = {
        .out_of_service = false,
        .present_value = ep->present_value,
        .status_flags = 0
    };
    ezb_zcl_cluster_desc_t cluster = ezb_zcl_analog_input_create_cluster_desc(&cfg, EZB_ZCL_CLUSTER_SERVER);
    if (!cluster) { ezb_af_free_endpoint_desc(endpoint); return ESP_ERR_NO_MEM; }
    
    // Set application type to Time in Seconds
    uint32_t app_type = EZB_ZCL_ANALOG_INPUT_APPLICATION_TYPE_TIME_IN_SECONDS;
    if (ezb_zcl_analog_input_cluster_desc_add_attr(cluster, EZB_ZCL_ATTR_ANALOG_INPUT_APPLICATION_TYPE_ID, &app_type) != EZB_ERR_NONE) {
        ezb_zcl_free_cluster_desc(cluster); ezb_af_free_endpoint_desc(endpoint); return ESP_FAIL;
    }

    if (ZigbeeEndpoint_AddCluster(endpoint, cluster) != ESP_OK) {
        ezb_af_free_endpoint_desc(endpoint); return ESP_FAIL;
    }
    if (ZigbeeEndpoint_RegisterReportingCluster(endpoint, ZIGBEE_EP_UPTIME) != ESP_OK) {
        ezb_af_free_endpoint_desc(endpoint);
        return ESP_FAIL;
    }
    if (ezb_af_device_add_endpoint_desc(device, endpoint) != EZB_ERR_NONE) {
        ezb_af_free_endpoint_desc(endpoint); return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t Uptime_ConfigureReporting(IZigbeeEndpoint *self) {
    IConfigurationManager *config = ConfigurationManager_GetInstance();
    SystemSettings snapshot;
    if (!config || !config->GetSnapshot || config->GetSnapshot(config, &snapshot) != ESP_OK) return ESP_FAIL;
    const SystemSettings *settings = &snapshot;
    
    uint16_t max_interval = settings->zigbee_reporting.uptime_s;


    ezb_zcl_reporting_info_t info = ezb_zcl_reporting_info_find(
        ZIGBEE_EP_UPTIME,
        EZB_ZCL_CLUSTER_ID_ANALOG_INPUT,
        EZB_ZCL_CLUSTER_SERVER,
        EZB_ZCL_ATTR_ANALOG_INPUT_PRESENT_VALUE_ID,
        EZB_ZCL_STD_MANUF_CODE);

    if (info) {


        ezb_zcl_attr_variable_t delta = {
            .f32 = 0.0f
        };
        return ZigbeeEndpoint_ApplyReporting(info, ZIGBEE_REPORT_MIN_INTERVAL, max_interval, &delta);
    }
    return ESP_ERR_NOT_FOUND;
}

static bool Uptime_HandleAction(IZigbeeEndpoint *self, ezb_zcl_core_action_callback_id_t callback_id,
        const void *message, esp_err_t *result) {
    (void)self;
    if (callback_id != EZB_ZCL_CORE_SET_ATTR_VALUE_CB_ID || !message) return false;
    const ezb_zcl_set_attr_value_message_t *attribute = message;
    if (attribute->info.dst_ep != ZIGBEE_EP_UPTIME) return false;
    return ZigbeeEndpoint_HandleReportingSetting(attribute, result);
}

IZigbeeEndpoint *EndpointUptime_Create(void) {
    memset(&uptimeInstance, 0, sizeof(EndpointUptime));
    uptimeInstance.base.GetName = Uptime_GetName;
    uptimeInstance.base.GetEndpointId = Uptime_GetEndpointId;
    uptimeInstance.base.RegisterClusters = Uptime_RegisterClusters;
    uptimeInstance.base.ConfigureReporting = Uptime_ConfigureReporting;
    uptimeInstance.base.HandleAction = Uptime_HandleAction;
    uptimeInstance.base.OnConnected = IZigbeeEndpoint_DefaultOnConnected;
    uptimeInstance.base.OnDisconnected = IZigbeeEndpoint_DefaultOnDisconnected;
    uptimeInstance.base.Destroy = IZigbeeEndpoint_DefaultDestroy;
    
    uptimeInstance.present_value = 0.0f;
    return &uptimeInstance.base;
}

#endif
