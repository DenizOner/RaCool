#if !defined(RACOOL_TARGET_TESTING) || defined(RACOOL_INTERNAL_TEST_ACCESS)
#include "Communication/Zigbee/Endpoints/EndpointGroups.h"
#include "Communication/Zigbee/Endpoints/EndpointCommon.h"
#include "Communication/Zigbee/ZigbeeConfig.h"
#include "Core/LogManager/ILogManager.h"
#include "esp_zigbee.h"
#include <string.h>

typedef struct {
    IZigbeeEndpoint base;
    uint8_t endpoint_id;
} EndpointGroups;

static EndpointGroups groupsInstance;
static const char *TAG_Groups = "Groups";

static const char *Groups_GetName(IZigbeeEndpoint *self) { return "OLEDDisplayEndpoint"; }
static uint8_t Groups_GetEndpointId(IZigbeeEndpoint *self) { return ((EndpointGroups *)self)->endpoint_id; }

static esp_err_t Groups_RegisterClusters(IZigbeeEndpoint *self, ezb_af_device_desc_t device) {
    if (!self || !device) return ESP_ERR_INVALID_ARG;
    EndpointGroups *ep = (EndpointGroups *)self;

    ezb_af_ep_config_t ep_cfg = { .ep_id = ep->endpoint_id, .app_profile_id = EZB_AF_HA_PROFILE_ID, .app_device_id = EZB_ZHA_SIMPLE_SENSOR_DEVICE_ID, .app_device_version = 1 };
    ezb_af_ep_desc_t endpoint = ezb_af_create_endpoint_desc(&ep_cfg);
    if (!endpoint) return ESP_ERR_NO_MEM;


    // Standard clusters (Basic, Identify)
    if (EndpointCommon_RegisterSharedClusters(endpoint, ZIGBEE_POWER_SOURCE) != ESP_OK) {
        ezb_af_free_endpoint_desc(endpoint); return ESP_FAIL;
    }

    // Factories return descriptors; their first argument is configuration, not an endpoint.
    ezb_zcl_cluster_desc_t groups = ezb_zcl_groups_create_cluster_desc(NULL, EZB_ZCL_CLUSTER_SERVER);
    if (!groups) { ezb_af_free_endpoint_desc(endpoint); return ESP_ERR_NO_MEM; }
    ezb_err_t sdk_result = ezb_af_endpoint_add_cluster_desc(endpoint, groups);
    if (sdk_result != EZB_ERR_NONE) {
        ESP_LOGE(TAG_Groups, "Groups attachment failed: %d", (int)sdk_result);
        ezb_zcl_free_cluster_desc(groups);
        goto failed;
    }
    ezb_zcl_cluster_desc_t scenes = ezb_zcl_scenes_create_cluster_desc(NULL, EZB_ZCL_CLUSTER_SERVER);
    if (!scenes) { ezb_af_free_endpoint_desc(endpoint); return ESP_ERR_NO_MEM; }
    sdk_result = ezb_af_endpoint_add_cluster_desc(endpoint, scenes);
    if (sdk_result != EZB_ERR_NONE) {
        ESP_LOGE(TAG_Groups, "Scenes attachment failed: %d", (int)sdk_result);
        ezb_zcl_free_cluster_desc(scenes);
        goto failed;
    }

    esp_err_t display_result = ZigbeeEndpoint_RegisterDisplayCluster(endpoint);
    if (display_result != ESP_OK) {
        ESP_LOGE(TAG_Groups, "Display cluster registration failed: %s", esp_err_to_name(display_result));
        goto failed;
    }
    sdk_result = ezb_af_device_add_endpoint_desc(device, endpoint);
    if (sdk_result != EZB_ERR_NONE) {
        ESP_LOGE(TAG_Groups, "Endpoint attachment failed: %d", (int)sdk_result);
        goto failed;
    }
    ESP_LOGI(TAG_Groups, "Registered EP%d (OLED, Groups & Scenes)", ep->endpoint_id);
    return ESP_OK;
failed:
    ESP_LOGE(TAG_Groups, "EP%d registration failed", ep->endpoint_id);
    ezb_af_free_endpoint_desc(endpoint);
    return ESP_FAIL;
}

static esp_err_t Groups_ConfigureReporting(IZigbeeEndpoint *self) { (void)self; return ESP_OK; }

static bool Groups_HandleAction(IZigbeeEndpoint *self, ezb_zcl_core_action_callback_id_t callback_id,
                          const void *message, esp_err_t *ret_status) {
    if (!self || !message || !ret_status || callback_id != EZB_ZCL_CORE_SET_ATTR_VALUE_CB_ID) return false;
    const ezb_zcl_set_attr_value_message_t *msg = message;
    if (msg->info.dst_ep != ZIGBEE_EP_DISPLAY) return false;
    return ZigbeeEndpoint_HandleDisplaySetting(msg, ret_status);
}

IZigbeeEndpoint *EndpointGroups_Create(void) {
    memset(&groupsInstance, 0, sizeof(groupsInstance));
    groupsInstance.base.GetName = Groups_GetName;
    groupsInstance.base.GetEndpointId = Groups_GetEndpointId;
    groupsInstance.base.RegisterClusters = Groups_RegisterClusters;
    groupsInstance.base.ConfigureReporting = Groups_ConfigureReporting;
    groupsInstance.base.HandleAction = Groups_HandleAction;
    groupsInstance.base.OnConnected = IZigbeeEndpoint_DefaultOnConnected;
    groupsInstance.base.OnDisconnected = IZigbeeEndpoint_DefaultOnDisconnected;
    groupsInstance.base.Destroy = IZigbeeEndpoint_DefaultDestroy;
    groupsInstance.endpoint_id = ZIGBEE_EP_GROUPS;
    return &groupsInstance.base;
}

#endif // RACOOL_TARGET_TESTING) && !defined(RACOOL_INTERNAL_TEST_ACCESS
