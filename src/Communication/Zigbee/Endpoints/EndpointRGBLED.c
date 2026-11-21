#include <math.h>
#if !defined(RACOOL_TARGET_TESTING) || defined(RACOOL_INTERNAL_TEST_ACCESS)
#include "Communication/Zigbee/Endpoints/EndpointRGBLED.h"
#include "Communication/Zigbee/Endpoints/EndpointCommon.h"
#include "Communication/Zigbee/ZigbeeConfig.h"
#include "Management/ConfigurationManager/IConfigurationManager.h"
#include "Hardware/RGBLED/IRGBLED.h"
#include "Core/LogManager/ILogManager.h"
#include "Core/EventBus/IEventBus.h"
#include "Core/ServiceLocator/ServiceLocator.h"
#include "Core/ServiceLocator/ServiceNames.h"
#include "esp_zigbee.h"
#include "ezbee/zcl/zcl_desc.h"
#include "Hardware/RGBLED/RGBLEDColorEngine.h"
#include <string.h>
#include <stddef.h>

typedef struct {
    IZigbeeEndpoint base;
    uint8_t endpoint_id;
    bool on_off_state;
    uint8_t brightness;
    uint8_t hue;
    uint8_t saturation;
    uint8_t color_mode;
    uint8_t ext_led_mode;    /**< LED operating mode (EXT_LED_MODE_*). */
    uint8_t ext_led_startup_anim; /**< Startup animation index. */
    uint32_t zone_xy[RGB_ZONE_COUNT];
    int16_t temp_cold_min;
    int16_t temp_cold_max;
    int16_t temp_warm_min;
    int16_t temp_warm_max;
    int16_t temp_hot_min;
    int16_t temp_hot_max;
    int16_t temp_danger;
    int16_t temp_freeze;
} EndpointRGBLED;

static EndpointRGBLED rgbLedInstance;
static const char *TAG_RGBLED = "RGBLED";

static const char *RGBLED_GetName(IZigbeeEndpoint *self) { return "RGBLEDEndpoint"; }
static uint8_t RGBLED_GetEndpointId(IZigbeeEndpoint *self) { return ((EndpointRGBLED *)self)->endpoint_id; }

static esp_err_t RGBLED_RegisterClusters(IZigbeeEndpoint *self, ezb_af_device_desc_t device) {
    if (!self || !device) return ESP_ERR_INVALID_ARG;
    EndpointRGBLED *ep = (EndpointRGBLED *)self;
    IConfigurationManager *config = ConfigurationManager_GetInstance();
    SystemSettings snapshot;
    if (!config || !config->GetSnapshot || config->GetSnapshot(config, &snapshot) != ESP_OK) return ESP_FAIL;
    const SystemSettings *settings = &snapshot;

    ep->on_off_state = settings->hardware.ext_led_state && settings->hardware.ext_led_mode != EXT_LED_MODE_OFF;
    ep->brightness = settings->hardware.ext_led_brightness;
    ep->hue = 0;
    ep->saturation = 0;
    ep->color_mode = 0;
    ep->ext_led_mode = settings->hardware.ext_led_mode;
    ep->ext_led_startup_anim = settings->hardware.ext_led_startup_anim;
    ep->temp_cold_min = (int16_t)lroundf(settings->hardware.ext_led_temp_cold_min * 100.0f);
    ep->temp_cold_max = (int16_t)lroundf(settings->hardware.ext_led_temp_cold_max * 100.0f);
    ep->temp_warm_min = (int16_t)lroundf(settings->hardware.ext_led_temp_warm_min * 100.0f);
    ep->temp_warm_max = (int16_t)lroundf(settings->hardware.ext_led_temp_warm_max * 100.0f);
    ep->temp_hot_min = (int16_t)lroundf(settings->hardware.ext_led_temp_hot_min * 100.0f);
    ep->temp_hot_max = (int16_t)lroundf(settings->hardware.ext_led_temp_hot_max * 100.0f);
    ep->temp_danger = (int16_t)lroundf(settings->hardware.ext_led_temp_danger * 100.0f);
    ep->temp_freeze = (int16_t)lroundf(settings->hardware.ext_led_temp_freeze * 100.0f);

    ezb_af_ep_config_t ep_cfg = { .ep_id = ZIGBEE_EP_RGB_LED, .app_profile_id = EZB_AF_HA_PROFILE_ID, .app_device_id = EZB_ZHA_ON_OFF_LIGHT_DEVICE_ID, .app_device_version = 1 };
    ezb_af_ep_desc_t endpoint = ezb_af_create_endpoint_desc(&ep_cfg);
    if (!endpoint) return ESP_ERR_NO_MEM;

    // Standard clusters (Basic, Identify)
    if (EndpointCommon_RegisterSharedClusters(endpoint, ZIGBEE_POWER_SOURCE) != ESP_OK) {
        ezb_af_free_endpoint_desc(endpoint); return ESP_FAIL;
    }

    // On/Off Cluster with READ_WRITE custom attributes
    ezb_zcl_cluster_desc_t on_off_cluster = ezb_zcl_on_off_create_cluster_desc(NULL, EZB_ZCL_CLUSTER_SERVER);
    if (!on_off_cluster) { ezb_af_free_endpoint_desc(endpoint); return ESP_ERR_NO_MEM; }

    bool ext_enabled  = (settings->hardware.ext_led_mode != EXT_LED_MODE_OFF);
    // Color zones stored as CIE XY uint16 pairs; expose nearest preset index (uint8) via ZCL
    uint8_t color_cold_idx    = (uint8_t)RGBLEDColorEngine_XYtoNearestPreset(
                                    settings->hardware.ext_led_color_cold_x,
                                    settings->hardware.ext_led_color_cold_y);
    uint8_t color_warm_idx = (uint8_t)RGBLEDColorEngine_XYtoNearestPreset(
                                    settings->hardware.ext_led_color_warm_x,
                                    settings->hardware.ext_led_color_warm_y);
    uint8_t color_hot_idx     = (uint8_t)RGBLEDColorEngine_XYtoNearestPreset(
                                    settings->hardware.ext_led_color_hot_x,
                                    settings->hardware.ext_led_color_hot_y);
    if (ezb_zcl_cluster_desc_add_manuf_attr(on_off_cluster, 0x8004, 0x10,
        EZB_ZCL_ATTR_ACCESS_READ_WRITE, EZB_ZCL_STD_MANUF_CODE, &ext_enabled) != EZB_ERR_NONE) {
        ezb_zcl_free_cluster_desc(on_off_cluster); ezb_af_free_endpoint_desc(endpoint); return ESP_FAIL;
    }
    if (ezb_zcl_cluster_desc_add_manuf_attr(on_off_cluster, 0x8010, 0x20,
        EZB_ZCL_ATTR_ACCESS_READ_WRITE, EZB_ZCL_STD_MANUF_CODE, &color_cold_idx) != EZB_ERR_NONE) {
        ezb_zcl_free_cluster_desc(on_off_cluster); ezb_af_free_endpoint_desc(endpoint); return ESP_FAIL;
    }
    if (ezb_zcl_cluster_desc_add_manuf_attr(on_off_cluster, 0x8011, 0x20,
        EZB_ZCL_ATTR_ACCESS_READ_WRITE, EZB_ZCL_STD_MANUF_CODE, &color_warm_idx) != EZB_ERR_NONE) {
        ezb_zcl_free_cluster_desc(on_off_cluster); ezb_af_free_endpoint_desc(endpoint); return ESP_FAIL;
    }
    if (ezb_zcl_cluster_desc_add_manuf_attr(on_off_cluster, 0x8012, 0x20,
        EZB_ZCL_ATTR_ACCESS_READ_WRITE, EZB_ZCL_STD_MANUF_CODE, &color_hot_idx) != EZB_ERR_NONE) {
        ezb_zcl_free_cluster_desc(on_off_cluster); ezb_af_free_endpoint_desc(endpoint); return ESP_FAIL;
    }
    uint8_t color_danger_idx = (uint8_t)RGBLEDColorEngine_XYtoNearestPreset(
        settings->hardware.ext_led_color_danger_x, settings->hardware.ext_led_color_danger_y);
    uint8_t color_freeze_idx = (uint8_t)RGBLEDColorEngine_XYtoNearestPreset(
        settings->hardware.ext_led_color_freeze_x, settings->hardware.ext_led_color_freeze_y);
    if (ezb_zcl_cluster_desc_add_manuf_attr(on_off_cluster, 0x8015, 0x20,
        EZB_ZCL_ATTR_ACCESS_READ_WRITE, EZB_ZCL_STD_MANUF_CODE, &color_danger_idx) != EZB_ERR_NONE) {
        ezb_zcl_free_cluster_desc(on_off_cluster); ezb_af_free_endpoint_desc(endpoint); return ESP_FAIL;
    }
    if (ezb_zcl_cluster_desc_add_manuf_attr(on_off_cluster, 0x8016, 0x20,
        EZB_ZCL_ATTR_ACCESS_READ_WRITE, EZB_ZCL_STD_MANUF_CODE, &color_freeze_idx) != EZB_ERR_NONE) {
        ezb_zcl_free_cluster_desc(on_off_cluster); ezb_af_free_endpoint_desc(endpoint); return ESP_FAIL;
    }
    // 0x8013: LED operating mode (0=temperature, 1=animation, 2=off)
    if (ezb_zcl_cluster_desc_add_manuf_attr(on_off_cluster, 0x8013, 0x20,
        EZB_ZCL_ATTR_ACCESS_READ_WRITE, EZB_ZCL_STD_MANUF_CODE, &ep->ext_led_mode) != EZB_ERR_NONE) {
        ezb_zcl_free_cluster_desc(on_off_cluster); ezb_af_free_endpoint_desc(endpoint); return ESP_FAIL;
    }
    // 0x8014: LED startup animation index
    if (ezb_zcl_cluster_desc_add_manuf_attr(on_off_cluster, 0x8014, 0x20,
        EZB_ZCL_ATTR_ACCESS_READ_WRITE, EZB_ZCL_STD_MANUF_CODE, &ep->ext_led_startup_anim) != EZB_ERR_NONE) {
        ezb_zcl_free_cluster_desc(on_off_cluster); ezb_af_free_endpoint_desc(endpoint); return ESP_FAIL;
    }
    if (ezb_zcl_cluster_desc_add_manuf_attr(on_off_cluster, 0x8020, 0x29, EZB_ZCL_ATTR_ACCESS_READ_WRITE, EZB_ZCL_STD_MANUF_CODE, &ep->temp_cold_min) != EZB_ERR_NONE) {
        ezb_zcl_free_cluster_desc(on_off_cluster); ezb_af_free_endpoint_desc(endpoint); return ESP_FAIL;
    }
    if (ezb_zcl_cluster_desc_add_manuf_attr(on_off_cluster, 0x8021, 0x29, EZB_ZCL_ATTR_ACCESS_READ_WRITE, EZB_ZCL_STD_MANUF_CODE, &ep->temp_cold_max) != EZB_ERR_NONE) {
        ezb_zcl_free_cluster_desc(on_off_cluster); ezb_af_free_endpoint_desc(endpoint); return ESP_FAIL;
    }
    if (ezb_zcl_cluster_desc_add_manuf_attr(on_off_cluster, 0x8022, 0x29, EZB_ZCL_ATTR_ACCESS_READ_WRITE, EZB_ZCL_STD_MANUF_CODE, &ep->temp_warm_min) != EZB_ERR_NONE) {
        ezb_zcl_free_cluster_desc(on_off_cluster); ezb_af_free_endpoint_desc(endpoint); return ESP_FAIL;
    }
    if (ezb_zcl_cluster_desc_add_manuf_attr(on_off_cluster, 0x8023, 0x29, EZB_ZCL_ATTR_ACCESS_READ_WRITE, EZB_ZCL_STD_MANUF_CODE, &ep->temp_warm_max) != EZB_ERR_NONE) {
        ezb_zcl_free_cluster_desc(on_off_cluster); ezb_af_free_endpoint_desc(endpoint); return ESP_FAIL;
    }
    if (ezb_zcl_cluster_desc_add_manuf_attr(on_off_cluster, 0x8024, 0x29, EZB_ZCL_ATTR_ACCESS_READ_WRITE, EZB_ZCL_STD_MANUF_CODE, &ep->temp_hot_min) != EZB_ERR_NONE) {
        ezb_zcl_free_cluster_desc(on_off_cluster); ezb_af_free_endpoint_desc(endpoint); return ESP_FAIL;
    }
    if (ezb_zcl_cluster_desc_add_manuf_attr(on_off_cluster, 0x8025, 0x29, EZB_ZCL_ATTR_ACCESS_READ_WRITE, EZB_ZCL_STD_MANUF_CODE, &ep->temp_hot_max) != EZB_ERR_NONE) {
        ezb_zcl_free_cluster_desc(on_off_cluster); ezb_af_free_endpoint_desc(endpoint); return ESP_FAIL;
    }
    if (ezb_zcl_cluster_desc_add_manuf_attr(on_off_cluster, 0x8026, 0x29, EZB_ZCL_ATTR_ACCESS_READ_WRITE, EZB_ZCL_STD_MANUF_CODE, &ep->temp_danger) != EZB_ERR_NONE) {
        ezb_zcl_free_cluster_desc(on_off_cluster); ezb_af_free_endpoint_desc(endpoint); return ESP_FAIL;
    }
    if (ezb_zcl_cluster_desc_add_manuf_attr(on_off_cluster, 0x8027, 0x29, EZB_ZCL_ATTR_ACCESS_READ_WRITE, EZB_ZCL_STD_MANUF_CODE, &ep->temp_freeze) != EZB_ERR_NONE) {
        ezb_zcl_free_cluster_desc(on_off_cluster); ezb_af_free_endpoint_desc(endpoint); return ESP_FAIL;
    }

    for (unsigned zone=0;zone<RGB_ZONE_COUNT;++zone) {
        uint16_t x,y;
        if (!SystemConfigs_GetRGBZoneXY(&settings->hardware,zone,&x,&y)) {
            ezb_zcl_free_cluster_desc(on_off_cluster);
            ezb_af_free_endpoint_desc(endpoint);
            return ESP_ERR_INVALID_STATE;
        }
        ep->zone_xy[zone]=(uint32_t)x|((uint32_t)y<<16);
        if (ezb_zcl_cluster_desc_add_manuf_attr(on_off_cluster,(uint16_t)(ZIGBEE_ATTR_RGB_ZONE_XY_FIRST+zone),
            EZB_ZCL_ATTR_TYPE_UINT32,EZB_ZCL_ATTR_ACCESS_READ_WRITE,EZB_ZCL_STD_MANUF_CODE,&ep->zone_xy[zone])!=EZB_ERR_NONE) {
            ezb_zcl_free_cluster_desc(on_off_cluster);ezb_af_free_endpoint_desc(endpoint);return ESP_FAIL;
        }
    }
    if (ZigbeeEndpoint_AddCluster(endpoint, on_off_cluster) != ESP_OK) {
        ezb_af_free_endpoint_desc(endpoint); return ESP_FAIL;
    }
    /* CurrentLevel 255 is reserved; initialize from the persisted hardware level. */
    ezb_zcl_level_cluster_server_config_t level_cfg = {
        .current_level = ep->brightness == 255 ? 254 : ep->brightness
    };
    if (ZigbeeEndpoint_AddCluster(endpoint, ezb_zcl_level_create_cluster_desc(&level_cfg, EZB_ZCL_CLUSTER_SERVER)) != ESP_OK) {
        ezb_af_free_endpoint_desc(endpoint); return ESP_FAIL;
    }

    // Color Control: provide config so Z2M can read colorMode & currentHue without UNSUPPORTED_ATTRIBUTE
    ezb_zcl_color_control_cluster_server_config_t color_cfg = {
        .current_x           = RGBLED_PRESET_WHITE_X,
        .current_y           = RGBLED_PRESET_WHITE_Y,
        .color_mode          = 0x01,   // 0x01 = CurrentX/Y (XY mode)
        .options             = 0x00,
        .enhanced_color_mode = 0x01,   // XY mode
        .color_capabilities  = 0x0008, // XY supported
        .number_of_primaries = 0,
    };
    if (ZigbeeEndpoint_AddCluster(endpoint, ezb_zcl_color_control_create_cluster_desc(&color_cfg, EZB_ZCL_CLUSTER_SERVER)) != ESP_OK) {
        ezb_af_free_endpoint_desc(endpoint); return ESP_FAIL;
    }

    if (ezb_af_device_add_endpoint_desc(device, endpoint) != EZB_ERR_NONE) {
        ezb_af_free_endpoint_desc(endpoint); return ESP_FAIL;
    }
    ESP_LOGI(TAG_RGBLED, "Registered EP%d (RGB LED)", ZIGBEE_EP_RGB_LED);
    return ESP_OK;
}

static esp_err_t RGBLED_ConfigureReporting(IZigbeeEndpoint *self) {
    ezb_zcl_reporting_info_t info = ezb_zcl_reporting_info_find(
        ZIGBEE_EP_RGB_LED,
        EZB_ZCL_CLUSTER_ID_LEVEL,
        EZB_ZCL_CLUSTER_SERVER,
        EZB_ZCL_ATTR_LEVEL_CURRENT_LEVEL_ID,
        EZB_ZCL_STD_MANUF_CODE);

    if (info) {
        ezb_zcl_attr_variable_t delta = {
            .u8 = 1
        };
        return ZigbeeEndpoint_ApplyReporting(info, ZIGBEE_REPORT_MIN_INTERVAL,
            ZIGBEE_REPORT_MAX_INTERVAL, &delta);
    }
    return ESP_ERR_NOT_FOUND;
}

/* The SDK callback already owns stack access. Never acquire another SDK lease. */
static esp_err_t RGBLED_ReadCoordinate(uint16_t attribute, uint16_t *value) {
    ezb_zcl_attr_desc_t descriptor = ezb_zcl_get_attr_desc(ZIGBEE_EP_RGB_LED,
        EZB_ZCL_CLUSTER_ID_COLOR_CONTROL, EZB_ZCL_CLUSTER_SERVER, attribute, EZB_ZCL_STD_MANUF_CODE);
    if (!descriptor || ezb_zcl_attr_desc_get_type(descriptor) != EZB_ZCL_ATTR_TYPE_UINT16 ||
        ezb_zcl_attr_desc_get_value_size(descriptor) != sizeof(*value)) return ESP_ERR_INVALID_STATE;
    return ezb_zcl_attr_desc_get_value(descriptor, value) == EZB_ERR_NONE ? ESP_OK : ESP_FAIL;
}

static bool RGBLED_HandleAction(IZigbeeEndpoint *self, ezb_zcl_core_action_callback_id_t callback_id,
                               const void *message, esp_err_t *ret_status) {
    (void)self;
    if (callback_id != EZB_ZCL_CORE_SET_ATTR_VALUE_CB_ID || !message || !ret_status) return false;
    const ezb_zcl_set_attr_value_message_t *msg = message;
    if (msg->info.dst_ep != ZIGBEE_EP_RGB_LED) return false;
    uint16_t attr = msg->in.attribute.id;
    uint16_t cluster = msg->info.cluster_id;
    if (cluster==EZB_ZCL_CLUSTER_ID_ON_OFF && attr>=ZIGBEE_ATTR_RGB_ZONE_XY_FIRST &&
        attr<ZIGBEE_ATTR_RGB_ZONE_XY_FIRST+RGB_ZONE_COUNT) {
        *ret_status=ESP_ERR_INVALID_ARG;
        if (!msg->in.attribute.data.value || msg->in.attribute.data.size!=sizeof(uint32_t) ||
            msg->in.attribute.data.type!=EZB_ZCL_ATTR_TYPE_UINT32) return true;
        uint32_t packed;memcpy(&packed,msg->in.attribute.data.value,sizeof(packed));
        uint16_t x=(uint16_t)packed,y=(uint16_t)(packed>>16);
        if (!SystemConfigs_ValidateRGBCoordinates(x,y)) return true;
        size_t offsetX,offsetY;
        if (!SystemConfigs_RGBZoneOffsets(attr-ZIGBEE_ATTR_RGB_ZONE_XY_FIRST,&offsetX,&offsetY)) return true;
        IConfigurationManager *config=ConfigurationManager_GetInstance();
        ConfigurationFieldUpdate fields[]={{offsetX,&x,sizeof(x)},{offsetY,&y,sizeof(y)}};
        *ret_status=config && config->UpdateFields ? config->UpdateFields(config,"hardware",fields,2,true) : ESP_ERR_INVALID_STATE;
        return true;
    }
    if (cluster == EZB_ZCL_CLUSTER_ID_COLOR_CONTROL &&
        (attr == EZB_ZCL_ATTR_COLOR_CONTROL_CURRENT_X_ID || attr == EZB_ZCL_ATTR_COLOR_CONTROL_CURRENT_Y_ID)) {
        *ret_status = ESP_ERR_INVALID_ARG;
        if (!msg->in.attribute.data.value || msg->in.attribute.data.size != sizeof(uint16_t) ||
            msg->in.attribute.data.type != EZB_ZCL_ATTR_TYPE_UINT16) return true;
        uint16_t x, y;
        *ret_status = RGBLED_ReadCoordinate(EZB_ZCL_ATTR_COLOR_CONTROL_CURRENT_X_ID, &x);
        if (*ret_status == ESP_OK) *ret_status = RGBLED_ReadCoordinate(EZB_ZCL_ATTR_COLOR_CONTROL_CURRENT_Y_ID, &y);
        if (*ret_status != ESP_OK) return true;
        memcpy(attr == EZB_ZCL_ATTR_COLOR_CONTROL_CURRENT_X_ID ? &x : &y,
            msg->in.attribute.data.value, sizeof(uint16_t));
        /* X/Y writes can arrive separately. Validate component ranges without
         * rejecting an intermediate pair; the color engine projects to RGB gamut. */
        if (!SystemConfigs_ValidateRGBCoordinates(x,y)) {
            *ret_status = ESP_ERR_INVALID_ARG; return true;
        }
        IRGBLED *rgb = GetRGBLEDService();
        *ret_status = rgb && rgb->SetColorRGB
            ? rgb->SetColorRGB(rgb, RGBLEDColorEngine_XYtoRGB(x, y)) : ESP_ERR_INVALID_STATE;
        return true;
    }
    bool onoff = cluster == EZB_ZCL_CLUSTER_ID_ON_OFF &&
                 (attr == EZB_ZCL_ATTR_ON_OFF_ON_OFF_ID || attr == 0x8004);
    bool preset = cluster == EZB_ZCL_CLUSTER_ID_ON_OFF &&
                  ((attr >= 0x8010 && attr <= 0x8012) || attr == 0x8015 || attr == 0x8016);
    bool mode = cluster == EZB_ZCL_CLUSTER_ID_ON_OFF && attr == 0x8013;
    bool animation = cluster == EZB_ZCL_CLUSTER_ID_ON_OFF && attr == 0x8014;
    bool thermal = cluster == EZB_ZCL_CLUSTER_ID_ON_OFF && attr >= 0x8020 && attr <= 0x8027;
    bool brightness = cluster == EZB_ZCL_CLUSTER_ID_LEVEL && attr == EZB_ZCL_ATTR_LEVEL_CURRENT_LEVEL_ID;
    if (!onoff && !preset && !mode && !animation && !thermal && !brightness) return false;
    *ret_status = ESP_ERR_INVALID_ARG;
    uint8_t type = onoff ? EZB_ZCL_ATTR_TYPE_BOOL : thermal ? EZB_ZCL_ATTR_TYPE_INT16 : EZB_ZCL_ATTR_TYPE_UINT8;
    size_t size = thermal ? sizeof(int16_t) : sizeof(uint8_t);
    if (!msg->in.attribute.data.value || msg->in.attribute.data.size != size || msg->in.attribute.data.type != type) return true;
    IConfigurationManager *config = ConfigurationManager_GetInstance();
    if (!config || !config->UpdateFields) { *ret_status = ESP_ERR_INVALID_STATE; return true; }
    uint8_t value = 0;
    bool enabled = false;
    float temperature = 0;
    uint16_t x = 0, y = 0;
    ConfigurationFieldUpdate fields[4];
    size_t count = 1;
    if (thermal) {
        int16_t raw;
        memcpy(&raw, msg->in.attribute.data.value, sizeof(raw));
        temperature = raw / 100.0f;
        static const size_t offsets[] = {
            offsetof(HardwareConfigs, ext_led_temp_cold_min), offsetof(HardwareConfigs, ext_led_temp_cold_max),
            offsetof(HardwareConfigs, ext_led_temp_warm_min), offsetof(HardwareConfigs, ext_led_temp_warm_max),
            offsetof(HardwareConfigs, ext_led_temp_hot_min), offsetof(HardwareConfigs, ext_led_temp_hot_max),
            offsetof(HardwareConfigs, ext_led_temp_danger), offsetof(HardwareConfigs, ext_led_temp_freeze)
        };
        fields[0] = (ConfigurationFieldUpdate){offsets[attr - 0x8020], &temperature, sizeof(temperature)};
    } else {
        memcpy(&value, msg->in.attribute.data.value, sizeof(value));
        if (onoff) {
            if (value > 1) return true;
            enabled = value != 0;
            value = enabled ? EXT_LED_MODE_TEMPERATURE : EXT_LED_MODE_OFF;
            fields[0] = (ConfigurationFieldUpdate){offsetof(HardwareConfigs, ext_led_mode), &value, sizeof(value)};
            if (attr == EZB_ZCL_ATTR_ON_OFF_ON_OFF_ID) {
                fields[1] = (ConfigurationFieldUpdate){offsetof(HardwareConfigs, ext_led_state), &enabled, sizeof(enabled)};
                count = 2;
            }
        } else if (preset) {
            if (value >= RGB_COLOR_COUNT) return true;
            static const size_t offsets[][2] = {
                {offsetof(HardwareConfigs, ext_led_color_cold_x), offsetof(HardwareConfigs, ext_led_color_cold_y)},
                {offsetof(HardwareConfigs, ext_led_color_warm_x), offsetof(HardwareConfigs, ext_led_color_warm_y)},
                {offsetof(HardwareConfigs, ext_led_color_hot_x), offsetof(HardwareConfigs, ext_led_color_hot_y)},
                {offsetof(HardwareConfigs, ext_led_color_danger_x), offsetof(HardwareConfigs, ext_led_color_danger_y)},
                {offsetof(HardwareConfigs, ext_led_color_freeze_x), offsetof(HardwareConfigs, ext_led_color_freeze_y)}
            };
            unsigned index = attr <= 0x8012 ? attr - 0x8010 : attr - 0x8015 + 3;
            RGBLEDColorEngine_PresetToXY((PresetColorIndex)value, &x, &y);
            fields[0] = (ConfigurationFieldUpdate){offsets[index][0], &x, sizeof(x)};
            fields[1] = (ConfigurationFieldUpdate){offsets[index][1], &y, sizeof(y)};
            count = 2;
        } else {
            if (mode && value >= EXT_LED_MODE_COUNT) return true;
            size_t offset = mode ? offsetof(HardwareConfigs, ext_led_mode) : animation ?
                            offsetof(HardwareConfigs, ext_led_startup_anim) : offsetof(HardwareConfigs, ext_led_brightness);
            fields[0] = (ConfigurationFieldUpdate){offset, &value, sizeof(value)};
        }
    }
    /* ConfigurationManager validates and commits before publishing the worker update. */
    *ret_status = config->UpdateFields(config, "hardware", fields, count, true);
    if (*ret_status == ESP_OK && (mode || onoff)) {
        IRGBLED *rgb = GetRGBLEDService();
        *ret_status = rgb && rgb->ApplySelectedMode ? rgb->ApplySelectedMode(rgb) : ESP_ERR_INVALID_STATE;
    }
    return true;
}

IZigbeeEndpoint *EndpointRGBLED_Create(void) {
    memset(&rgbLedInstance, 0, sizeof(rgbLedInstance));
    rgbLedInstance.base.GetName = RGBLED_GetName;
    rgbLedInstance.base.GetEndpointId = RGBLED_GetEndpointId;
    rgbLedInstance.base.RegisterClusters = RGBLED_RegisterClusters;
    rgbLedInstance.base.ConfigureReporting = RGBLED_ConfigureReporting;
    rgbLedInstance.base.HandleAction = RGBLED_HandleAction;
    rgbLedInstance.base.OnConnected = IZigbeeEndpoint_DefaultOnConnected;
    rgbLedInstance.base.OnDisconnected = IZigbeeEndpoint_DefaultOnDisconnected;
    rgbLedInstance.base.Destroy = IZigbeeEndpoint_DefaultDestroy;
    rgbLedInstance.endpoint_id = ZIGBEE_EP_RGB_LED;
    return &rgbLedInstance.base;
}

#endif // RACOOL_TARGET_TESTING) && !defined(RACOOL_INTERNAL_TEST_ACCESS
