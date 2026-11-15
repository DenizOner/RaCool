#if !defined(RACOOL_TARGET_TESTING) || defined(RACOOL_INTERNAL_TEST_ACCESS)
#include "Communication/Zigbee/Endpoints/EndpointOTA.h"
#include "Communication/Zigbee/Endpoints/EndpointCommon.h"
#include "Communication/Zigbee/ZigbeeConfig.h"
#include "Core/EventBus/IEventBus.h"
#include "Core/ServiceLocator/ServiceLocator.h"
#include "esp_zigbee.h"
#include "Management/ConfigurationManager/IConfigurationManager.h"
#include "Management/PowerManager/IPowerManager.h"
#include "Controllers/SystemController/ISystemController.h"
#include "Core/ServiceLocator/ServiceNames.h"
#include <string.h>

typedef struct {
    IZigbeeEndpoint base;
    uint8_t endpoint_id;
    OtaUpgradeState state;
    uint32_t bytesReceived;
    uint32_t totalSize;
} EndpointOTA;

static EndpointOTA otaInstance;
static const char *TAG_OTA = "EndpointOTA";

static void PublishOtaError(void) {
    IEventBus *bus = ServiceLocator_GetEventBus();
    if (bus && bus->Publish)
        bus->Publish((BusEvent){.eventName = "ota_error", .type = EVENT_TYPE_ERROR});
}

static void RejectUnimplementedOta(EndpointOTA *ota) {
    /* Preserve the existing error event, but publish it only once per failed
     * transfer. Never publish ota_complete without verified flash/boot state. */
    bool firstFailure = ota->state != OTA_STATE_ERROR;
    ota->state = OTA_STATE_ERROR;
    ota->bytesReceived = 0;
    ota->totalSize = 0;
    if (firstFailure) PublishOtaError();
}

static const char *OTA_GetName(IZigbeeEndpoint *self) { return "OTAUpgradeEndpoint"; }
static uint8_t OTA_GetEndpointId(IZigbeeEndpoint *self) { return ((EndpointOTA *)self)->endpoint_id; }

static esp_err_t OTA_RegisterClusters(IZigbeeEndpoint *self, ezb_af_device_desc_t device) {
    if (!self || !device) return ESP_ERR_INVALID_ARG;
    IConfigurationManager *config = ConfigurationManager_GetInstance();
    SystemSettings settings;
    if (!config || !config->GetSnapshot || config->GetSnapshot(config, &settings) != ESP_OK) return ESP_FAIL;
    EndpointOTA *ota = (EndpointOTA *)self;

    ezb_af_ep_config_t ep_cfg = { .ep_id = ota->endpoint_id, .app_profile_id = EZB_AF_HA_PROFILE_ID, .app_device_id = EZB_ZHA_CUSTOM_GATEWAY_DEVICE_ID, .app_device_version = 1 };
    ezb_af_ep_desc_t endpoint = ezb_af_create_endpoint_desc(&ep_cfg);
    if (!endpoint) return ESP_ERR_NO_MEM;

    ezb_zcl_ota_upgrade_cluster_client_config_t ota_cfg = {
        .upgrade_server_id = EZB_ZCL_OTA_UPGRADE_UPGRADE_SERVER_ID_DEFAULT_VALUE,
        .file_offset = 0,
        .image_upgrade_status = EZB_ZCL_OTA_UPGRADE_IMAGE_UPGRADE_STATUS_NORMAL,
        .manufacturer_id = ZIGBEE_OTA_MANUFACTURER_CODE,
        .image_type_id = ZIGBEE_OTA_IMAGE_TYPE,
    };

    ezb_zcl_cluster_desc_t cluster = ezb_zcl_ota_upgrade_create_cluster_desc(&ota_cfg, EZB_ZCL_CLUSTER_CLIENT);
    if (!cluster) { ezb_af_free_endpoint_desc(endpoint); return ESP_ERR_NO_MEM; }
    
    if (!cluster) goto failed;
    uint32_t current_version = ZIGBEE_FIRMWARE_VERSION;
    if (ezb_zcl_ota_upgrade_cluster_desc_add_attr(cluster,
            EZB_ZCL_ATTR_OTA_UPGRADE_CURRENT_FILE_VERSION_ID, &current_version) != EZB_ERR_NONE) {
        ezb_zcl_free_cluster_desc(cluster);
        goto failed;
    }
    if (ezb_af_endpoint_add_cluster_desc(endpoint, cluster) != EZB_ERR_NONE) {
        ezb_zcl_free_cluster_desc(cluster);
        goto failed;
    }
    cluster = ezb_zcl_thermostat_ui_config_create_cluster_desc(NULL, EZB_ZCL_CLUSTER_SERVER);
    if (!cluster) goto failed;
    if (ezb_zcl_cluster_desc_add_manuf_attr(cluster, ZIGBEE_ATTR_POWER_PROFILE,
            EZB_ZCL_ATTR_TYPE_UINT8, EZB_ZCL_ATTR_ACCESS_READ_WRITE,
            EZB_ZCL_STD_MANUF_CODE, &settings.system.power_profile) != EZB_ERR_NONE) {
        ezb_zcl_free_cluster_desc(cluster);
        goto failed;
    }
    uint8_t command = 0;
    if (ezb_zcl_cluster_desc_add_manuf_attr(cluster, ZIGBEE_ATTR_TX_PROFILE,
            EZB_ZCL_ATTR_TYPE_UINT8, EZB_ZCL_ATTR_ACCESS_READ_WRITE,
            EZB_ZCL_STD_MANUF_CODE, &settings.system.zigbee_tx_profile) != EZB_ERR_NONE) {
        ezb_zcl_free_cluster_desc(cluster);
        goto failed;
    }
    if (ezb_zcl_cluster_desc_add_manuf_attr(cluster, ZIGBEE_ATTR_SYSTEM_COMMAND,
            EZB_ZCL_ATTR_TYPE_UINT8, EZB_ZCL_ATTR_ACCESS_READ_WRITE,
            EZB_ZCL_STD_MANUF_CODE, &command) != EZB_ERR_NONE) {
        ezb_zcl_free_cluster_desc(cluster);
        goto failed;
    }
    if (ezb_af_endpoint_add_cluster_desc(endpoint, cluster) != EZB_ERR_NONE) {
        ezb_zcl_free_cluster_desc(cluster);
        goto failed;
    }
    if (ezb_af_device_add_endpoint_desc(device, endpoint) != EZB_ERR_NONE) goto failed;
    ESP_LOGI(TAG_OTA, "Registered EP%d (System management and OTA client)", ota->endpoint_id);
    return ESP_OK;
failed:
    ESP_LOGE(TAG_OTA, "System endpoint registration failed");
    ezb_af_free_endpoint_desc(endpoint);
    return ESP_FAIL;
}

static esp_err_t OTA_ConfigureReporting(IZigbeeEndpoint *self) { (void)self; return ESP_OK; }

static bool OTA_HandleAction(IZigbeeEndpoint *self, ezb_zcl_core_action_callback_id_t callback_id,
                          const void *message, esp_err_t *ret_status) {
    if (!self || !message || !ret_status) return false;
    if (callback_id == EZB_ZCL_CORE_SET_ATTR_VALUE_CB_ID) {
        const ezb_zcl_set_attr_value_message_t *msg = message;
        if (msg->info.dst_ep != ZIGBEE_EP_SYSTEM) return false;
        if (ZigbeeEndpoint_HandleSystemCommand(msg, ret_status)) return true;
        if (msg->info.cluster_id == ZIGBEE_SYSTEM_CONFIG_CLUSTER &&
            msg->in.attribute.id == ZIGBEE_ATTR_TX_PROFILE) {
            *ret_status = ESP_ERR_INVALID_ARG;
            if (!msg->in.attribute.data.value || msg->in.attribute.data.size != 1 ||
                msg->in.attribute.data.type != EZB_ZCL_ATTR_TYPE_UINT8) return true;
            uint8_t profile = *(const uint8_t *)msg->in.attribute.data.value;
            if (profile >= ZIGBEE_TX_PROFILE_COUNT) return true;
            ISystemController *system = (ISystemController *)ServiceLocator_GetService(SVC_NAME_SYSTEM_CONTROLLER);
            *ret_status = system && system->RequestZigbeeTxProfile ?
                system->RequestZigbeeTxProfile(system, profile) : ESP_ERR_INVALID_STATE;
            return true;
        }
        if (msg->info.dst_ep != ZIGBEE_EP_SYSTEM ||
            msg->info.cluster_id != EZB_ZCL_CLUSTER_ID_THERMOSTAT_UI_CONFIG ||
            msg->in.attribute.id != ZIGBEE_ATTR_POWER_PROFILE) return false;
        *ret_status = ESP_ERR_INVALID_ARG;
        if (!msg->in.attribute.data.value || msg->in.attribute.data.size != 1 ||
            msg->in.attribute.data.type != EZB_ZCL_ATTR_TYPE_UINT8) return true;
        uint8_t mode = *(const uint8_t *)msg->in.attribute.data.value;
        if (mode >= POWER_PROFILE_COUNT) return true;
        IPowerManager *power = (IPowerManager *)ServiceLocator_GetService(SVC_NAME_POWER_MANAGER);
        if (!power || !power->SetPowerMode) { *ret_status = ESP_ERR_INVALID_STATE; return true; }
        *ret_status = power->SetPowerMode(power, (PowerMode_t)mode) == POWER_OK ? ESP_OK : ESP_FAIL;
        return true;
    }
    if (callback_id != EZB_ZCL_CORE_OTA_UPGRADE_CLIENT_PROGRESS_CB_ID) return false;

    EndpointOTA *ota = (EndpointOTA *)self;
    ezb_zcl_ota_upgrade_client_progress_message_t *msg =
        (ezb_zcl_ota_upgrade_client_progress_message_t *)message;

    /*
     * This endpoint does not yet own a validated OTA parser, inactive flash
     * partition, esp_ota handle or boot/rollback contract. The SDK forwards
     * this callback result to the sender: reporting SUCCESS before any actual
     * flash operation could falsely advertise an installed image.
     * Reject every transfer stage until the complete OTA owner is implemented.
     */
    if (msg->in.progress == EZB_ZCL_OTA_UPGRADE_PROGRESS_START)
        ESP_LOGW(TAG_OTA, "OTA rejected: verified image writer is not implemented");

    switch (msg->in.progress) {
    case EZB_ZCL_OTA_UPGRADE_PROGRESS_START:
    case EZB_ZCL_OTA_UPGRADE_PROGRESS_RECEIVING:
    case EZB_ZCL_OTA_UPGRADE_PROGRESS_CHECK:
    case EZB_ZCL_OTA_UPGRADE_PROGRESS_APPLY:
    case EZB_ZCL_OTA_UPGRADE_PROGRESS_FINISH:
        RejectUnimplementedOta(ota);
        *ret_status = ESP_ERR_NOT_SUPPORTED;
        msg->out.result = EZB_ZCL_STATUS_ABORT;
        return true;

    case EZB_ZCL_OTA_UPGRADE_PROGRESS_ABORT:
        /* Acknowledge cancellation only, never successful installation. */
        RejectUnimplementedOta(ota);
        *ret_status = ESP_OK;
        msg->out.result = EZB_ZCL_STATUS_SUCCESS;
        return true;

    default:
        RejectUnimplementedOta(ota);
        *ret_status = ESP_ERR_INVALID_ARG;
        msg->out.result = EZB_ZCL_STATUS_ABORT;
        return true;
    }
}

IZigbeeEndpoint *EndpointOTA_Create(void) {
    memset(&otaInstance, 0, sizeof(otaInstance));
    otaInstance.base.GetName = OTA_GetName;
    otaInstance.base.GetEndpointId = OTA_GetEndpointId;
    otaInstance.base.RegisterClusters = OTA_RegisterClusters;
    otaInstance.base.ConfigureReporting = OTA_ConfigureReporting;
    otaInstance.base.HandleAction = OTA_HandleAction;
    otaInstance.base.OnConnected = IZigbeeEndpoint_DefaultOnConnected;
    otaInstance.base.OnDisconnected = IZigbeeEndpoint_DefaultOnDisconnected;
    otaInstance.base.Destroy = IZigbeeEndpoint_DefaultDestroy;
    otaInstance.endpoint_id = ZIGBEE_EP_OTA;
    otaInstance.state = OTA_STATE_IDLE;
    return &otaInstance.base;
}

#endif // RACOOL_TARGET_TESTING) && !defined(RACOOL_INTERNAL_TEST_ACCESS
