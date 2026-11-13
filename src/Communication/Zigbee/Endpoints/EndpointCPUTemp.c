#if !defined(RACOOL_TARGET_TESTING) || defined(RACOOL_INTERNAL_TEST_ACCESS)
#include "Communication/Zigbee/Endpoints/EndpointCPUTemp.h"
#include "Communication/Zigbee/Endpoints/EndpointCommon.h"
#include "Communication/Zigbee/ZigbeeConfig.h"
#include "Core/LogManager/ILogManager.h"
#include "esp_zigbee.h"
#include <string.h>

typedef struct {
    IZigbeeEndpoint base;
    uint8_t endpoint_id;
    uint16_t temp_val;
    uint16_t temp_min;
    uint16_t temp_max;
} EndpointCPUTemp;

static EndpointCPUTemp cpuTempInstance;
static const char *TAG_CPU = "CPUTemp";

static const char *CPU_GetName(IZigbeeEndpoint *self) { return "CPUTempEndpoint"; }
static uint8_t CPU_GetEndpointId(IZigbeeEndpoint *self) { return ((EndpointCPUTemp *)self)->endpoint_id; }

static esp_err_t CPU_RegisterClusters(IZigbeeEndpoint *self, ezb_af_device_desc_t device) {
    if (!self || !device) return ESP_ERR_INVALID_ARG;
    EndpointCPUTemp *ep = (EndpointCPUTemp *)self;

    ep->temp_val = ZIGBEE_TEMPERATURE_INVALID;
    ep->temp_min = 0x954d;
    ep->temp_max = 0x7fff;

    ezb_af_ep_config_t ep_cfg = { .ep_id = ZIGBEE_EP_CPU_TEMP, .app_profile_id = EZB_AF_HA_PROFILE_ID, .app_device_id = 0x0302, .app_device_version = 1 };
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
    ezb_zcl_cluster_desc_t temp_cluster = ezb_zcl_temperature_measurement_create_cluster_desc(&temp_cfg, EZB_ZCL_CLUSTER_SERVER);
    if (!temp_cluster) { ezb_af_free_endpoint_desc(endpoint); return ESP_ERR_NO_MEM; }
    if (ZigbeeEndpoint_AddCluster(endpoint, temp_cluster) != ESP_OK) {
        ezb_af_free_endpoint_desc(endpoint); return ESP_FAIL;
    }

    if (ZigbeeEndpoint_RegisterReportingCluster(endpoint, ZIGBEE_EP_CPU_TEMP) != ESP_OK) {
        ezb_af_free_endpoint_desc(endpoint);
        return ESP_FAIL;
    }
    if (ezb_af_device_add_endpoint_desc(device, endpoint) != EZB_ERR_NONE) {
        ezb_af_free_endpoint_desc(endpoint); return ESP_FAIL;
    }
    ESP_LOGI(TAG_CPU, "Registered EP%d (CPU Temp)", ZIGBEE_EP_CPU_TEMP);
    return ESP_OK;
}

#include "Management/ConfigurationManager/IConfigurationManager.h"
#include "Management/ConfigurationManager/SystemConfigs.h"

static esp_err_t CPU_ConfigureReporting(IZigbeeEndpoint *self) {
    IConfigurationManager *config = ConfigurationManager_GetInstance();
    SystemSettings snapshot;
    if (!config || !config->GetSnapshot || config->GetSnapshot(config, &snapshot) != ESP_OK) return ESP_FAIL;
    const SystemSettings *settings = &snapshot;
    
    uint16_t max_interval = settings->zigbee_reporting.cpu_s;


    ezb_zcl_reporting_info_t info = ezb_zcl_reporting_info_find(
        ZIGBEE_EP_CPU_TEMP,
        EZB_ZCL_CLUSTER_ID_TEMPERATURE_MEASUREMENT,
        EZB_ZCL_CLUSTER_SERVER,
        EZB_ZCL_ATTR_TEMPERATURE_MEASUREMENT_MEASURED_VALUE_ID,
        EZB_ZCL_STD_MANUF_CODE);

    if (info) {


        ezb_zcl_attr_variable_t delta = {
            .s16 = ZIGBEE_REPORT_TEMP_THRESHOLD
        };
        return ZigbeeEndpoint_ApplyReporting(info, ZIGBEE_REPORT_MIN_INTERVAL, max_interval, &delta);
    }
    return ESP_ERR_NOT_FOUND;
}

static bool CPU_HandleAction(IZigbeeEndpoint *self, ezb_zcl_core_action_callback_id_t callback_id,
                          const void *message, esp_err_t *ret_status) {
    (void)self;
    if (callback_id != EZB_ZCL_CORE_SET_ATTR_VALUE_CB_ID || !message) return false;
    const ezb_zcl_set_attr_value_message_t *attribute = message;
    if (attribute->info.dst_ep != ZIGBEE_EP_CPU_TEMP) return false;
    return ZigbeeEndpoint_HandleReportingSetting(attribute, ret_status);
}

IZigbeeEndpoint *EndpointCPUTemp_Create(void) {
    memset(&cpuTempInstance, 0, sizeof(cpuTempInstance));
    cpuTempInstance.base.GetName = CPU_GetName;
    cpuTempInstance.base.GetEndpointId = CPU_GetEndpointId;
    cpuTempInstance.base.RegisterClusters = CPU_RegisterClusters;
    cpuTempInstance.base.ConfigureReporting = CPU_ConfigureReporting;
    cpuTempInstance.base.HandleAction = CPU_HandleAction;
    cpuTempInstance.base.OnConnected = IZigbeeEndpoint_DefaultOnConnected;
    cpuTempInstance.base.OnDisconnected = IZigbeeEndpoint_DefaultOnDisconnected;
    cpuTempInstance.base.Destroy = IZigbeeEndpoint_DefaultDestroy;
    cpuTempInstance.endpoint_id = ZIGBEE_EP_CPU_TEMP;
    return &cpuTempInstance.base;
}

#endif // RACOOL_TARGET_TESTING) && !defined(RACOOL_INTERNAL_TEST_ACCESS
