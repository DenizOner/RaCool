#if !defined(RACOOL_TARGET_TESTING) || defined(RACOOL_INTERNAL_TEST_ACCESS)
#include "Communication/Zigbee/Endpoints/EndpointInternalLED.h"
#include "Communication/Zigbee/Endpoints/EndpointCommon.h"
#include "Communication/Zigbee/ZigbeeConfig.h"
#include "Management/ConfigurationManager/IConfigurationManager.h"
#include "Hardware/BuiltinLED/IBuiltinLED.h"
#include "Core/LogManager/ILogManager.h"
#include "Core/EventBus/IEventBus.h"
#include "Core/ServiceLocator/ServiceLocator.h"
#include "Core/ServiceLocator/ServiceNames.h"
#include "Communication/Zigbee/IZigbee.h"
#include "esp_zigbee.h"
#include "ezbee/zcl/zcl_desc.h"
#include <string.h>

typedef struct {
    IZigbeeEndpoint base;
    uint8_t endpoint_id;
    bool on_off_state;
} EndpointInternalLED;

static EndpointInternalLED internalLedInstance;
static const char *TAG_IntLED = "InternalLED";

static const char *IntLED_GetName(IZigbeeEndpoint *self) { return "InternalLEDEndpoint"; }
static uint8_t IntLED_GetEndpointId(IZigbeeEndpoint *self) { return ((EndpointInternalLED *)self)->endpoint_id; }

static esp_err_t InternalLED_RegisterClusters(IZigbeeEndpoint *self, ezb_af_device_desc_t device) {
    if (!self || !device) return ESP_ERR_INVALID_ARG;
    EndpointInternalLED *ep = (EndpointInternalLED *)self;
    IConfigurationManager *config = ConfigurationManager_GetInstance();
    SystemSettings snapshot;
    if (!config || !config->GetSnapshot || config->GetSnapshot(config, &snapshot) != ESP_OK) {
        ESP_LOGE(TAG_IntLED, "Cannot register LED attributes without settings");
        return ESP_FAIL;
    }
    ep->on_off_state = snapshot.hardware.builtin_led_anim_mode != 0;

    ezb_af_ep_config_t ep_cfg = { .ep_id = ZIGBEE_EP_INTERNAL_LED, .app_profile_id = EZB_AF_HA_PROFILE_ID, .app_device_id = EZB_ZHA_ON_OFF_LIGHT_DEVICE_ID, .app_device_version = 1 };
    ezb_af_ep_desc_t endpoint = ezb_af_create_endpoint_desc(&ep_cfg);
    if (!endpoint) return ESP_ERR_NO_MEM;

    // Standard clusters (Basic, Identify)
    if (EndpointCommon_RegisterSharedClusters(endpoint, ZIGBEE_POWER_SOURCE) != ESP_OK) {
        ezb_af_free_endpoint_desc(endpoint); return ESP_FAIL;
    }

    // On/Off Cluster with read-write animation state attributes
    ezb_zcl_cluster_desc_t on_off_cluster = ezb_zcl_on_off_create_cluster_desc(NULL, EZB_ZCL_CLUSTER_SERVER);
    if (!on_off_cluster) { ezb_af_free_endpoint_desc(endpoint); return ESP_ERR_NO_MEM; }

    uint8_t idle_anim = snapshot.hardware.builtin_led_idle_anim;
    uint8_t connecting_anim = snapshot.hardware.builtin_led_connecting_anim;
    uint8_t fault_anim = snapshot.hardware.builtin_led_fault_anim;
    uint8_t led_mode = snapshot.hardware.builtin_led_anim_mode;
    uint8_t brightness = snapshot.hardware.builtin_led_brightness;
    uint8_t startup_anim = snapshot.hardware.builtin_led_startup_anim;

    // Register as manufacturer-specific READ_WRITE attributes so Z2M can write them
    if (ezb_zcl_cluster_desc_add_manuf_attr(on_off_cluster, 0x8000, 0x20,
        EZB_ZCL_ATTR_ACCESS_READ_WRITE, EZB_ZCL_STD_MANUF_CODE, &idle_anim) != EZB_ERR_NONE) {
        ezb_zcl_free_cluster_desc(on_off_cluster); ezb_af_free_endpoint_desc(endpoint); return ESP_FAIL;
    }
    if (ezb_zcl_cluster_desc_add_manuf_attr(on_off_cluster, 0x8001, 0x20,
        EZB_ZCL_ATTR_ACCESS_READ_WRITE, EZB_ZCL_STD_MANUF_CODE, &led_mode) != EZB_ERR_NONE) {
        ezb_zcl_free_cluster_desc(on_off_cluster); ezb_af_free_endpoint_desc(endpoint); return ESP_FAIL;
    }
    if (ezb_zcl_cluster_desc_add_manuf_attr(on_off_cluster, 0x8002, 0x20,
        EZB_ZCL_ATTR_ACCESS_READ_WRITE, EZB_ZCL_STD_MANUF_CODE, &connecting_anim) != EZB_ERR_NONE) {
        ezb_zcl_free_cluster_desc(on_off_cluster); ezb_af_free_endpoint_desc(endpoint); return ESP_FAIL;
    }
    if (ezb_zcl_cluster_desc_add_manuf_attr(on_off_cluster, 0x8003, 0x20,
        EZB_ZCL_ATTR_ACCESS_READ_WRITE, EZB_ZCL_STD_MANUF_CODE, &fault_anim) != EZB_ERR_NONE) {
        ezb_zcl_free_cluster_desc(on_off_cluster); ezb_af_free_endpoint_desc(endpoint); return ESP_FAIL;
    }

    if (ezb_zcl_cluster_desc_add_manuf_attr(on_off_cluster, 0x8004, 0x20,
        EZB_ZCL_ATTR_ACCESS_READ_WRITE, EZB_ZCL_STD_MANUF_CODE, &brightness) != EZB_ERR_NONE) {
        ezb_zcl_free_cluster_desc(on_off_cluster); ezb_af_free_endpoint_desc(endpoint); return ESP_FAIL;
    }
    if (ezb_zcl_cluster_desc_add_manuf_attr(on_off_cluster, 0x8005, 0x20,
        EZB_ZCL_ATTR_ACCESS_READ_WRITE, EZB_ZCL_STD_MANUF_CODE, &startup_anim) != EZB_ERR_NONE) {
        ezb_zcl_free_cluster_desc(on_off_cluster); ezb_af_free_endpoint_desc(endpoint); return ESP_FAIL;
    }
    if (ZigbeeEndpoint_AddCluster(endpoint, on_off_cluster) != ESP_OK) {
        ezb_af_free_endpoint_desc(endpoint); return ESP_FAIL;
    }

    if (ezb_af_device_add_endpoint_desc(device, endpoint) != EZB_ERR_NONE) {
        ezb_af_free_endpoint_desc(endpoint); return ESP_FAIL;
    }
    ESP_LOGI(TAG_IntLED, "Registered EP%d (Internal LED)", ZIGBEE_EP_INTERNAL_LED);
    return ESP_OK;
}

static esp_err_t IntLED_OnConnected(IZigbeeEndpoint *self) {
    // Synchronize the cached endpoint state; this is not a fresh driver reading.
    EndpointInternalLED *ep = (EndpointInternalLED *)self;
    IZigbee *zb = (IZigbee *)ServiceLocator_GetService(SVC_NAME_ZIGBEE);
    if (zb && zb->IsConnected(zb)) {
        bool state = ep->on_off_state;
        // The reporting timer owns the SDK lock while delivering OnConnected.
        ezb_zcl_status_t result = ezb_zcl_set_attr_value(ZIGBEE_EP_INTERNAL_LED,
            EZB_ZCL_CLUSTER_ID_ON_OFF, EZB_ZCL_CLUSTER_SERVER, EZB_ZCL_ATTR_ON_OFF_ON_OFF_ID,
            EZB_ZCL_STD_MANUF_CODE, &state, false);
        if (result != EZB_ZCL_STATUS_SUCCESS) ESP_LOGE(TAG_IntLED, "Initial LED attribute update failed: %d", result);
        return result == EZB_ZCL_STATUS_SUCCESS ? ESP_OK : ESP_FAIL;
    }
    return ESP_ERR_INVALID_STATE;
}

static esp_err_t IntLED_ConfigureReporting(IZigbeeEndpoint *self) {
    ezb_zcl_reporting_info_t info = ezb_zcl_reporting_info_find(
        ZIGBEE_EP_INTERNAL_LED,
        EZB_ZCL_CLUSTER_ID_ON_OFF,
        EZB_ZCL_CLUSTER_SERVER,
        EZB_ZCL_ATTR_ON_OFF_ON_OFF_ID,
        EZB_ZCL_STD_MANUF_CODE);

    if (info) {
        ezb_zcl_attr_variable_t delta = { .u8 = 0 };
        return ZigbeeEndpoint_ApplyReporting(info, ZIGBEE_REPORT_MIN_INTERVAL,
            ZIGBEE_REPORT_MAX_INTERVAL, &delta);
    }
    return ESP_ERR_NOT_FOUND;
}

static bool IntLED_HandleAction(IZigbeeEndpoint *self, ezb_zcl_core_action_callback_id_t callback_id,
                          const void *message, esp_err_t *ret_status) {
    if (!self || !message || !ret_status || callback_id != EZB_ZCL_CORE_SET_ATTR_VALUE_CB_ID) return false;
    const ezb_zcl_set_attr_value_message_t *msg = message;
    if (msg->info.dst_ep != ZIGBEE_EP_INTERNAL_LED || msg->info.cluster_id != EZB_ZCL_CLUSTER_ID_ON_OFF) return false;
    uint16_t id = msg->in.attribute.id;
    bool standard = id == EZB_ZCL_ATTR_ON_OFF_ON_OFF_ID;
    if (!standard && (id < 0x8000 || id > 0x8005)) return false;
    *ret_status = ESP_ERR_INVALID_ARG;
    if (!msg->in.attribute.data.value || msg->in.attribute.data.size != 1 ||
        msg->in.attribute.data.type != (standard ? 0x10 : 0x20)) return true;
    uint8_t value = *(const uint8_t *)msg->in.attribute.data.value;
    if ((standard || id == 0x8001) && value > 1) return true;
    if ((id == 0x8000 || id == 0x8002 || id == 0x8003 || id == 0x8005) &&
        value >= BUILTIN_LED_ANIM_COUNT && value != (uint8_t)BUILTIN_LED_ANIM_NONE) return true;
    IConfigurationManager *config = ConfigurationManager_GetInstance();
    if (!config || !config->UpdateFields) { *ret_status = ESP_ERR_INVALID_STATE; return true; }
    size_t offset = offsetof(HardwareConfigs, builtin_led_brightness);
    switch (id) {
        case 0x8000: offset = offsetof(HardwareConfigs, builtin_led_idle_anim); break;
        case 0x8002: offset = offsetof(HardwareConfigs, builtin_led_connecting_anim); break;
        case 0x8003: offset = offsetof(HardwareConfigs, builtin_led_fault_anim); break;
        case 0x8005: offset = offsetof(HardwareConfigs, builtin_led_startup_anim); break;
        default: break;
    }
    ConfigurationFieldUpdate fields[2] = {{offset, &value, sizeof(value)}};
    size_t count = 1;
    bool enabled = value != 0;
    if (standard || id == 0x8001) {
        fields[0] = (ConfigurationFieldUpdate){offsetof(HardwareConfigs, builtin_led_anim_mode), &value, sizeof(value)};
        fields[1] = (ConfigurationFieldUpdate){offsetof(HardwareConfigs, builtin_led_state), &enabled, sizeof(enabled)};
        count = 2;
    }
    *ret_status = config->UpdateFields(config, "hardware", fields, count, true);
    if (*ret_status == ESP_OK && (standard || id == 0x8001))
        ((EndpointInternalLED *)self)->on_off_state = enabled;
    return true;
}

IZigbeeEndpoint *EndpointInternalLED_Create(void) {
    memset(&internalLedInstance, 0, sizeof(internalLedInstance));
    internalLedInstance.base.GetName = IntLED_GetName;
    internalLedInstance.base.GetEndpointId = IntLED_GetEndpointId;
    internalLedInstance.base.RegisterClusters = InternalLED_RegisterClusters;
    internalLedInstance.base.ConfigureReporting = IntLED_ConfigureReporting;
    internalLedInstance.base.HandleAction = IntLED_HandleAction;
    internalLedInstance.base.OnConnected = IntLED_OnConnected;
    internalLedInstance.base.OnDisconnected = IZigbeeEndpoint_DefaultOnDisconnected;
    internalLedInstance.base.Destroy = IZigbeeEndpoint_DefaultDestroy;
    internalLedInstance.endpoint_id = ZIGBEE_EP_INTERNAL_LED;
    return &internalLedInstance.base;
}

#endif // RACOOL_TARGET_TESTING) && !defined(RACOOL_INTERNAL_TEST_ACCESS
