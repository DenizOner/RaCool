#include "Communication/Zigbee/Endpoints/FanSourceAttributes.h"
#include "Core/ServiceLocator/ServiceLocator.h"
#include "Core/ServiceLocator/ServiceNames.h"
#include "Core/EventBus/IEventBus.h"
#include "Core/EventBus/EventSubscriptions.h"
#include "Communication/Zigbee/IZigbee.h"
#include "Communication/Zigbee/ZigbeeConfig.h"
#include "Management/ConfigurationManager/IConfigurationManager.h"
#include "Management/ConfigurationManager/SystemConfigs.h"
#include "Hardware/RGBLED/RGBLEDColorEngine.h"
#include <esp_log.h>
#include <esp_zigbee.h>
#include <math.h>
#include <string.h>

static const char *TAG = "ClimateZBPlugin";

static int32_t QueueAttribute(IZigbee *radio, uint8_t endpoint, uint16_t cluster,
    uint16_t attribute, const void *value, uint8_t length) {
    int32_t result = radio && radio->QueueAttributeUpdate ?
        radio->QueueAttributeUpdate(radio, endpoint, cluster, attribute, value, length) : ZIGBEE_ERROR_INVALID_PARAM;
    if (result != ZIGBEE_ERROR_OK)
        ESP_LOGE(TAG, "Attribute queue admission failed: endpoint=%u cluster=0x%04x attribute=0x%04x result=%ld",
            (unsigned)endpoint, (unsigned)cluster, (unsigned)attribute, (long)result);
    return result;
}

/**
 * @brief Publish climate measurements and configuration through Zigbee.
 */

static void OnIRTempUpdated(BusEvent event) {
    if (!event.data || event.dataSize < sizeof(float)) return;
    float obj_temp;
    memcpy(&obj_temp, event.data, sizeof(obj_temp));
    if (!isfinite(obj_temp) || obj_temp < -327.67f || obj_temp > 327.67f) return;
    int16_t zb_obj_temp = (int16_t)(obj_temp * 100);

    IZigbee *zb = (IZigbee *)ServiceLocator_GetService(SVC_NAME_ZIGBEE);
    if (zb && zb->IsConnected && zb->IsConnected(zb) > 0) {
        // EP1: Object Temperature
        QueueAttribute(zb, ZIGBEE_EP_SENSOR_FAN, 
                                   EZB_ZCL_CLUSTER_ID_TEMPERATURE_MEASUREMENT,
                                   EZB_ZCL_ATTR_TEMPERATURE_MEASUREMENT_MEASURED_VALUE_ID,
                                   &zb_obj_temp, sizeof(zb_obj_temp));

        // EP4: Ambient Temperature
        if (event.dataSize >= 2 * sizeof(float)) {
            float amb_temp;
            memcpy(&amb_temp, (const uint8_t *)event.data + sizeof(float), sizeof(amb_temp));
            if (!isfinite(amb_temp) || amb_temp < -327.67f || amb_temp > 327.67f) return;
            int16_t zb_amb_temp = (int16_t)(amb_temp * 100);
            QueueAttribute(zb, ZIGBEE_EP_AMBIENT_TEMP,
                                       EZB_ZCL_CLUSTER_ID_TEMPERATURE_MEASUREMENT,
                                       EZB_ZCL_ATTR_TEMPERATURE_MEASUREMENT_MEASURED_VALUE_ID,
                                       &zb_amb_temp, sizeof(zb_amb_temp));
        }
    }
}

static void OnConfigChanged(BusEvent event) {
    IConfigurationManager *config = (IConfigurationManager *)ServiceLocator_GetService(SVC_NAME_CONFIGURATION_MANAGER);
    SystemSettings snapshot;
    if (!config || !config->GetSnapshot || config->GetSnapshot(config, &snapshot) != ESP_OK) return;
    SystemSettings *settings = &snapshot;

    IZigbee *zb = (IZigbee *)ServiceLocator_GetService(SVC_NAME_ZIGBEE);
    if (!zb || !zb->IsConnected || zb->IsConnected(zb) <= 0) return;

    uint8_t canonical_mode = settings->control.fan_manual_mode ? (settings->control.fan_manual_state ? 1 : 2) : (settings->control.fan_strategy == 1 ? 3 : 0);
    const uint8_t selections[] = {canonical_mode, settings->control.object_temperature_source, settings->control.ambient_temperature_source};
    for (unsigned i=0; i<3; ++i) QueueAttribute(zb, ZIGBEE_EP_SENSOR_FAN, EZB_ZCL_CLUSTER_ID_THERMOSTAT_UI_CONFIG,
        FAN_ATTR_MODE+i, &selections[i], sizeof(selections[i]));
    QueueAttribute(zb, ZIGBEE_EP_SENSOR_FAN, EZB_ZCL_CLUSTER_ID_THERMOSTAT_UI_CONFIG, FAN_ATTR_DELTA_ON,
        &settings->control.fan_delta_on_decic, sizeof(int16_t));
    QueueAttribute(zb, ZIGBEE_EP_SENSOR_FAN, EZB_ZCL_CLUSTER_ID_THERMOSTAT_UI_CONFIG, FAN_ATTR_DELTA_OFF,
        &settings->control.fan_delta_off_decic, sizeof(int16_t));
    /* Mode changes need publication even when the relay output stays unchanged. */
    uint8_t fan_mode = settings->control.fan_manual_mode
        ? (settings->control.fan_manual_state ? ZIGBEE_FAN_MODE_ON : ZIGBEE_FAN_MODE_OFF)
        : ZIGBEE_FAN_MODE_AUTO;
    QueueAttribute(zb, ZIGBEE_EP_SENSOR_FAN, EZB_ZCL_CLUSTER_ID_THERMOSTAT,
        EZB_ZCL_ATTR_THERMOSTAT_SYSTEM_MODE_ID, &fan_mode, sizeof(fan_mode));

    int32_t profile_result = QueueAttribute(zb, ZIGBEE_EP_SYSTEM,
        EZB_ZCL_CLUSTER_ID_THERMOSTAT_UI_CONFIG, ZIGBEE_ATTR_POWER_PROFILE,
        &settings->system.power_profile, sizeof(settings->system.power_profile));
    if (profile_result != ZIGBEE_ERROR_OK) ESP_LOGE(TAG, "Power profile sync failed: %ld", (long)profile_result);
    int32_t tx_result = QueueAttribute(zb, ZIGBEE_EP_SYSTEM,
        EZB_ZCL_CLUSTER_ID_THERMOSTAT_UI_CONFIG, ZIGBEE_ATTR_TX_PROFILE,
        &settings->system.zigbee_tx_profile, sizeof(settings->system.zigbee_tx_profile));
    if (tx_result != ZIGBEE_ERROR_OK) ESP_LOGE(TAG, "TX profile sync failed: %ld", (long)tx_result);

    // Keep CLI/OLED changes readable through the same HA control attributes.
    uint16_t intervals[] = {settings->zigbee_reporting.fan_s, settings->zigbee_reporting.ambient_s,
        settings->zigbee_reporting.cpu_s, settings->zigbee_reporting.health_s,
        settings->zigbee_reporting.memory_s, settings->zigbee_reporting.uptime_s};
    const uint8_t reportingEndpoints[] = {ZIGBEE_EP_SENSOR_FAN, ZIGBEE_EP_AMBIENT_TEMP,
        ZIGBEE_EP_CPU_TEMP, ZIGBEE_EP_SYSTEM_HEALTH, ZIGBEE_EP_MEMORY, ZIGBEE_EP_UPTIME};
    for (unsigned i = 0; i < sizeof(intervals)/sizeof(intervals[0]); ++i) {
        int32_t result = QueueAttribute(zb, ZIGBEE_EP_SENSOR_FAN,
            EZB_ZCL_CLUSTER_ID_THERMOSTAT_UI_CONFIG, 0x8030 + i, &intervals[i], sizeof(intervals[i]));
        if (result != ZIGBEE_ERROR_OK) ESP_LOGE(TAG, "Reporting control sync failed: %ld", (long)result);
        if (reportingEndpoints[i] != ZIGBEE_EP_SENSOR_FAN)
            QueueAttribute(zb, reportingEndpoints[i], EZB_ZCL_CLUSTER_ID_THERMOSTAT_UI_CONFIG,
                0x8030 + i, &intervals[i], sizeof(intervals[i]));
    }

    // 1. Temperature Unit
    QueueAttribute(zb, ZIGBEE_EP_SENSOR_FAN,
                               EZB_ZCL_CLUSTER_ID_THERMOSTAT_UI_CONFIG,
                               EZB_ZCL_ATTR_THERMOSTAT_UI_CONFIG_TEMPERATURE_DISPLAY_MODE_ID,
                               &settings->system.temp_unit, sizeof(uint8_t));

    QueueAttribute(zb, ZIGBEE_EP_DISPLAY,
        EZB_ZCL_CLUSTER_ID_THERMOSTAT_UI_CONFIG,
        EZB_ZCL_ATTR_THERMOSTAT_UI_CONFIG_TEMPERATURE_DISPLAY_MODE_ID,
        &settings->system.temp_unit, sizeof(uint8_t));

    // Mirror display settings to the dedicated route and legacy automation route.
    const uint8_t display_endpoints[] = {ZIGBEE_EP_DISPLAY, ZIGBEE_EP_SENSOR_FAN};
    uint16_t display_timeout = (uint16_t)settings->system.oled_timeout_sec;
    for (size_t i = 0; i < sizeof(display_endpoints); ++i) {
        int32_t mode_result = QueueAttribute(zb, display_endpoints[i],
            EZB_ZCL_CLUSTER_ID_THERMOSTAT_UI_CONFIG, 0x8020,
            &settings->system.display_mode, sizeof(uint8_t));
        int32_t time_result = QueueAttribute(zb, display_endpoints[i],
            EZB_ZCL_CLUSTER_ID_THERMOSTAT_UI_CONFIG, 0x8021,
            &display_timeout, sizeof(display_timeout));
        if (mode_result != ZIGBEE_ERROR_OK || time_result != ZIGBEE_ERROR_OK)
            ESP_LOGE(TAG, "Display control sync failed on EP%u", display_endpoints[i]);
    }

    // 2. Calibration
    int8_t obj_cal = (int8_t)(settings->calibration.ir_object_cal * 10);
    QueueAttribute(zb, ZIGBEE_EP_SENSOR_FAN,
                               EZB_ZCL_CLUSTER_ID_THERMOSTAT,
                               EZB_ZCL_ATTR_THERMOSTAT_LOCAL_TEMPERATURE_CALIBRATION_ID,
                               &obj_cal, sizeof(obj_cal));

    int8_t amb_cal = (int8_t)(settings->calibration.ir_ambient_cal * 10);
    QueueAttribute(zb, ZIGBEE_EP_AMBIENT_TEMP,
                               EZB_ZCL_CLUSTER_ID_THERMOSTAT,
                               EZB_ZCL_ATTR_THERMOSTAT_LOCAL_TEMPERATURE_CALIBRATION_ID,
                               &amb_cal, sizeof(amb_cal));

    // 3. Fan relay thresholds → sync OLED menu changes to HA automatically
    int16_t open_setpoint  = (int16_t)lroundf(settings->control.relay_open_temp * 100.0f);
    int16_t close_setpoint = (int16_t)lroundf(settings->control.relay_close_temp * 100.0f);
    QueueAttribute(zb, ZIGBEE_EP_SENSOR_FAN,
                               EZB_ZCL_CLUSTER_ID_THERMOSTAT,
                               EZB_ZCL_ATTR_THERMOSTAT_OCCUPIED_COOLING_SETPOINT_ID,
                               &open_setpoint, sizeof(open_setpoint));
    QueueAttribute(zb, ZIGBEE_EP_SENSOR_FAN,
                               EZB_ZCL_CLUSTER_ID_THERMOSTAT,
                               EZB_ZCL_ATTR_THERMOSTAT_OCCUPIED_HEATING_SETPOINT_ID,
                               &close_setpoint, sizeof(close_setpoint));

    // 4. RGB LED thermal threshold sync to HA
    int16_t thermal_values[] = {
        (int16_t)lroundf(settings->hardware.ext_led_temp_cold_min * 100.0f),
        (int16_t)lroundf(settings->hardware.ext_led_temp_cold_max * 100.0f),
        (int16_t)lroundf(settings->hardware.ext_led_temp_warm_min * 100.0f),
        (int16_t)lroundf(settings->hardware.ext_led_temp_warm_max * 100.0f),
        (int16_t)lroundf(settings->hardware.ext_led_temp_hot_min * 100.0f),
        (int16_t)lroundf(settings->hardware.ext_led_temp_hot_max * 100.0f),
        (int16_t)lroundf(settings->hardware.ext_led_temp_danger * 100.0f),
        (int16_t)lroundf(settings->hardware.ext_led_temp_freeze * 100.0f),
    };
    for (uint16_t i = 0; i < 8; ++i) {
        QueueAttribute(zb, ZIGBEE_EP_RGB_LED, EZB_ZCL_CLUSTER_ID_ON_OFF,
                                   (uint16_t)(0x8020 + i), &thermal_values[i], sizeof(thermal_values[i]));
    }
    for (unsigned zone=0;zone<RGB_ZONE_COUNT;++zone) {
        uint16_t x,y;
        if (!SystemConfigs_GetRGBZoneXY(&settings->hardware,zone,&x,&y)) continue;
        uint32_t packed=(uint32_t)x|((uint32_t)y<<16);
        QueueAttribute(zb,ZIGBEE_EP_RGB_LED,EZB_ZCL_CLUSTER_ID_ON_OFF,
            (uint16_t)(ZIGBEE_ATTR_RGB_ZONE_XY_FIRST+zone),&packed,sizeof(packed));
    }
    uint8_t danger_color = (uint8_t)RGBLEDColorEngine_XYtoNearestPreset(
        settings->hardware.ext_led_color_danger_x, settings->hardware.ext_led_color_danger_y);
    uint8_t freeze_color = (uint8_t)RGBLEDColorEngine_XYtoNearestPreset(
        settings->hardware.ext_led_color_freeze_x, settings->hardware.ext_led_color_freeze_y);
    QueueAttribute(zb, ZIGBEE_EP_RGB_LED, EZB_ZCL_CLUSTER_ID_ON_OFF, 0x8015,
                               &danger_color, sizeof(danger_color));
    QueueAttribute(zb, ZIGBEE_EP_RGB_LED, EZB_ZCL_CLUSTER_ID_ON_OFF, 0x8016,
                               &freeze_color, sizeof(freeze_color));
    uint8_t cold_color = (uint8_t)RGBLEDColorEngine_XYtoNearestPreset(
        settings->hardware.ext_led_color_cold_x, settings->hardware.ext_led_color_cold_y);
    uint8_t warm_color = (uint8_t)RGBLEDColorEngine_XYtoNearestPreset(
        settings->hardware.ext_led_color_warm_x, settings->hardware.ext_led_color_warm_y);
    uint8_t hot_color = (uint8_t)RGBLEDColorEngine_XYtoNearestPreset(
        settings->hardware.ext_led_color_hot_x, settings->hardware.ext_led_color_hot_y);
    QueueAttribute(zb, ZIGBEE_EP_RGB_LED, EZB_ZCL_CLUSTER_ID_ON_OFF, 0x8010,
                               &cold_color, sizeof(cold_color));
    QueueAttribute(zb, ZIGBEE_EP_RGB_LED, EZB_ZCL_CLUSTER_ID_ON_OFF, 0x8011,
                               &warm_color, sizeof(warm_color));
    QueueAttribute(zb, ZIGBEE_EP_RGB_LED, EZB_ZCL_CLUSTER_ID_ON_OFF, 0x8012,
                               &hot_color, sizeof(hot_color));

    const uint16_t internal_attrs[] = {0x8000,0x8001,0x8002,0x8003,0x8004,0x8005};
    const uint8_t internal_values[] = {settings->hardware.builtin_led_idle_anim,
        settings->hardware.builtin_led_anim_mode, settings->hardware.builtin_led_connecting_anim,
        settings->hardware.builtin_led_fault_anim, settings->hardware.builtin_led_brightness,
        settings->hardware.builtin_led_startup_anim};
    for (size_t i = 0; i < sizeof(internal_attrs)/sizeof(internal_attrs[0]); ++i) {
        QueueAttribute(zb, ZIGBEE_EP_INTERNAL_LED, EZB_ZCL_CLUSTER_ID_ON_OFF,
                                   internal_attrs[i], (void *)&internal_values[i], sizeof(uint8_t));
    }
    bool enabled = settings->hardware.builtin_led_anim_mode == 1;
    QueueAttribute(zb, ZIGBEE_EP_INTERNAL_LED, EZB_ZCL_CLUSTER_ID_ON_OFF,
                               EZB_ZCL_ATTR_ON_OFF_ON_OFF_ID, &enabled, sizeof(enabled));

    // 5. RGB LED operating mode and animation sync to HA
    bool rgb_on = settings->hardware.ext_led_state && settings->hardware.ext_led_mode != EXT_LED_MODE_OFF;
    QueueAttribute(zb, ZIGBEE_EP_RGB_LED, EZB_ZCL_CLUSTER_ID_ON_OFF,
                               EZB_ZCL_ATTR_ON_OFF_ON_OFF_ID, &rgb_on, sizeof(rgb_on));
    QueueAttribute(zb, ZIGBEE_EP_RGB_LED, EZB_ZCL_CLUSTER_ID_ON_OFF, 0x8013,
                               &settings->hardware.ext_led_mode, sizeof(uint8_t));
    QueueAttribute(zb, ZIGBEE_EP_RGB_LED, EZB_ZCL_CLUSTER_ID_ON_OFF, 0x8014,
                               &settings->hardware.ext_led_startup_anim, sizeof(uint8_t));
    /* CurrentLevel reserves 255; full hardware brightness maps to ZCL 254. */
    uint8_t externalLevel = settings->hardware.ext_led_brightness == 255 ? 254 : settings->hardware.ext_led_brightness;
    QueueAttribute(zb, ZIGBEE_EP_RGB_LED, EZB_ZCL_CLUSTER_ID_LEVEL,
        EZB_ZCL_ATTR_LEVEL_CURRENT_LEVEL_ID, &externalLevel, sizeof(externalLevel));
}

static void OnRelayChanged(BusEvent event) {
    if (!event.data || event.dataSize != sizeof(bool)) return;
    uint8_t raw = *(const uint8_t *)event.data;
    if (raw > 1) return;
    bool status = raw != 0;

    IZigbee *zb = (IZigbee *)ServiceLocator_GetService(SVC_NAME_ZIGBEE);
    if (zb && zb->IsConnected && zb->IsConnected(zb) > 0) {
        QueueAttribute(zb, ZIGBEE_EP_SENSOR_FAN,
                                   EZB_ZCL_CLUSTER_ID_ON_OFF,
                                   EZB_ZCL_ATTR_ON_OFF_ON_OFF_ID,
                                   &status, sizeof(status));
        
        // Automatic fan operation must remain Auto when the relay changes state.
        IConfigurationManager *config = ConfigurationManager_GetInstance();
        SystemSettings snapshot;
        if (!config || !config->GetSnapshot || config->GetSnapshot(config, &snapshot) != ESP_OK) return;
        const SystemSettings *settings = &snapshot;
        uint8_t zMode = settings->control.fan_manual_mode
            ? (settings->control.fan_manual_state ? ZIGBEE_FAN_MODE_ON : ZIGBEE_FAN_MODE_OFF) : ZIGBEE_FAN_MODE_AUTO;
        QueueAttribute(zb, ZIGBEE_EP_SENSOR_FAN,
                                   EZB_ZCL_CLUSTER_ID_THERMOSTAT,
                                   EZB_ZCL_ATTR_THERMOSTAT_SYSTEM_MODE_ID,
                                   &zMode, sizeof(zMode));
    }
}

static void OnTemperatureSourcesUpdated(BusEvent event) {
    if (!event.data || event.dataSize != sizeof(FanSourceDiagnostics)) return;
    FanSourceDiagnostics data; memcpy(&data, event.data, sizeof(data));
    IZigbee *radio = (IZigbee *)ServiceLocator_GetService(SVC_NAME_ZIGBEE);
    if (!radio || !radio->IsConnected || radio->IsConnected(radio) <= 0) return;
    for (unsigned i=0; i<6; ++i) QueueAttribute(radio, ZIGBEE_EP_SENSOR_FAN, EZB_ZCL_CLUSTER_ID_THERMOSTAT_UI_CONFIG,
        FAN_ATTR_EXTERNAL_FIRST+i, &data.temperatures[i], sizeof(int16_t));
    const uint8_t values[] = {data.sources & 15, data.sources >> 4, data.status & 15, data.status >> 4, data.slot_states & 3, (data.slot_states >> 2) & 3, (data.slot_states >> 4) & 3};
    for (unsigned i=0; i<7; ++i) QueueAttribute(radio, ZIGBEE_EP_SENSOR_FAN, EZB_ZCL_CLUSTER_ID_THERMOSTAT_UI_CONFIG,
        FAN_ATTR_EFFECTIVE_OBJECT_SOURCE+i, &values[i], sizeof(uint8_t));
}
static EventSubscriptions subscriptions;
static const EventSubscription subscriptionTable[] = {
    {EVENT_IR_TEMP_UPDATED, OnIRTempUpdated},
    {EVENT_TEMPERATURE_SOURCES_UPDATED, OnTemperatureSourcesUpdated},
    {EVENT_RELAY_STATE_CHANGED, OnRelayChanged},
    {EVENT_CONFIG_CHANGED, OnConfigChanged},
    {"ZIGBEE_CONNECTED", OnConfigChanged}
};

int32_t ClimateZigbeePlugin_Init(void) {
    IEventBus *bus = ServiceLocator_GetEventBus();
    int32_t result = EventSubscriptions_Attach(&subscriptions, bus, subscriptionTable,
        sizeof(subscriptionTable)/sizeof(subscriptionTable[0]));
    if (result != 0)
        ESP_LOGE(TAG, "Event subscription failed: result=%ld retained=0x%lx", (long)result, (unsigned long)subscriptions.owned);
    return result;
}
