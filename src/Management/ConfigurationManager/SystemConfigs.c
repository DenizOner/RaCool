#include <math.h>
#include <string.h>
#include <stddef.h>
/**
 * @file SystemConfigs.c
 * @brief Utility implementation for printing all system configuration settings.
 *
 * Provides SystemConfigs_Print(), which formats every field of the live
 * SystemSettings struct to the ESP-IDF logging facility (ESP_LOGI) and also
 * prints static hardware pin assignments for quick board verification.
 *
 * @author RaCool Team
 * @date   2026
 */

#include "Management/ConfigurationManager/SystemConfigs.h"
#include "Management/ConfigurationManager/IConfigurationManager.h"
#include "esp_log.h"

bool SystemConfigs_IsLocalizationValid(const LocalizationConfigs *configuration) {
    if (!configuration || configuration->version != LOCALIZATION_SCHEMA_VERSION) return false;
    const char *code = configuration->locale;
    const char *end = memchr(code, '\0', LOCALIZATION_LOCALE_CAPACITY);
    if (!end) return false;
    size_t length = (size_t)(end - code);
    size_t language_length = 0;
    while (language_length < length && code[language_length] >= 'a' && code[language_length] <= 'z')
        ++language_length;
    if (language_length != 2 && language_length != 3) return false;
    if (length != language_length &&
        (length != language_length + 3 || code[language_length] != '-' ||
         code[language_length + 1] < 'A' || code[language_length + 1] > 'Z' ||
         code[language_length + 2] < 'A' || code[language_length + 2] > 'Z')) return false;
    for (size_t index = length + 1; index < LOCALIZATION_LOCALE_CAPACITY; ++index)
        if (code[index] != '\0') return false;
    return true;
}

/** @brief ESP-IDF log tag for config print output. */
static const char *TAG = "Config";

/**
 * @brief Prints all current system settings to the ESP-IDF log output.
 *
 * Copies a consistent SystemSettings snapshot from @p configMgr and logs every field
 * grouped by sub-block.  Also logs static hardware pin definitions for quick
 * board verification at startup.
 *
 * @param configMgr Pointer to the active IConfigurationManager instance.
 *                  If NULL or if GetSnapshot() fails, an error is logged
 *                  and the function returns immediately.
 */
esp_err_t SystemConfigs_Print(IConfigurationManager *configMgr) {
    if (!configMgr) {
        ESP_LOGE(TAG, "Configuration Manager instance is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    SystemSettings snapshot;
    if (!configMgr->GetSnapshot) return ESP_ERR_INVALID_STATE;
    esp_err_t result = configMgr->GetSnapshot(configMgr, &snapshot);
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "Failed to retrieve settings for print");
        return result;
    }
    const SystemSettings *s = &snapshot;

    ESP_LOGI(TAG, "==================================================");
    ESP_LOGI(TAG, "           RACOOL SYSTEM CONFIGURATIONS           ");
    ESP_LOGI(TAG, "==================================================");

    /* --- System --- */
    ESP_LOGI(TAG, "[System Settings] (v%u)", (unsigned)s->system.version);
    ESP_LOGI(TAG, "  System Enabled: %s", s->system.system_enabled ? "YES" : "NO");
    ESP_LOGI(TAG, "  Temp Unit     : %s",
             s->system.temp_unit == 0 ? "Celsius" : "Fahrenheit");
    ESP_LOGI(TAG, "  Telemetry Int : %u ms",
             (unsigned)s->system.telemetry_interval_ms);
    ESP_LOGI(TAG, "  OLED Timeout  : %u sec",
             (unsigned)s->system.oled_timeout_sec);

    /* --- Zigbee Reporting --- */
    ESP_LOGI(TAG, "[Zigbee Reporting] (v%u)",
             (unsigned)s->zigbee_reporting.version);
    ESP_LOGI(TAG, "  Fan: %u s  Ambient: %u s  CPU: %u s",
             (unsigned)s->zigbee_reporting.fan_s,
             (unsigned)s->zigbee_reporting.ambient_s,
             (unsigned)s->zigbee_reporting.cpu_s);
    ESP_LOGI(TAG, "  Health: %u s  Memory: %u s  Uptime: %u s",
             (unsigned)s->zigbee_reporting.health_s,
             (unsigned)s->zigbee_reporting.memory_s,
             (unsigned)s->zigbee_reporting.uptime_s);

    /* --- Control --- */
    ESP_LOGI(TAG, "[Control Settings] (v%u)", (unsigned)s->control.version);
    ESP_LOGI(TAG, "  High Threshold: %.2f C", s->control.threshold_high);
    ESP_LOGI(TAG, "  Low Threshold : %.2f C", s->control.threshold_low);
    ESP_LOGI(TAG, "  Relay Open    : %.2f C", s->control.relay_open_temp);
    ESP_LOGI(TAG, "  Relay Close   : %.2f C", s->control.relay_close_temp);
    ESP_LOGI(TAG, "  Fan Manual Mod: %s",
             s->control.fan_manual_mode ? "ACTIVE" : (s->control.fan_strategy == 1 ? "AMBIENT_DELTA" : "OBJECT_SENSITIVE"));
    ESP_LOGI(TAG, "  Fan State     : %s",
             s->control.fan_manual_state ? "RUNNING" : "STOPPED");
    ESP_LOGI(TAG, "  Fan Sources   : object=%u ambient=%u",(unsigned)s->control.object_temperature_source,
             (unsigned)s->control.ambient_temperature_source);
    ESP_LOGI(TAG, "  Fan Delta     : ON=%.1f OFF=%.1f C",s->control.fan_delta_on_decic/10.0f,
             s->control.fan_delta_off_decic/10.0f);

    /* --- Hardware --- */
    ESP_LOGI(TAG, "[Hardware Settings] (v%u)", (unsigned)s->hardware.version);
    ESP_LOGI(TAG, "  Built-in LED  : %s",
             s->hardware.builtin_led_state ? "ON" : "OFF");
    ESP_LOGI(TAG, "  Built-in Anim : %s",
             s->hardware.builtin_led_anim_mode ? "AUTO" : "MANUAL");
    ESP_LOGI(TAG, "  Built-in Cond Anims: Idle:%u Connecting:%u Fault:%u",
             (unsigned)s->hardware.builtin_led_idle_anim,
             (unsigned)s->hardware.builtin_led_connecting_anim,
             (unsigned)s->hardware.builtin_led_fault_anim);
    ESP_LOGI(TAG, "  Built-in Boot Anim: %u", (unsigned)s->hardware.builtin_led_startup_anim);
    ESP_LOGI(TAG, "  RGB LED States: R:%s G:%s B:%s",
             s->hardware.rgb_red_state   ? "1" : "0",
             s->hardware.rgb_green_state ? "1" : "0",
             s->hardware.rgb_blue_state  ? "1" : "0");
    ESP_LOGI(TAG, "  RGB Brightness: %u", (unsigned)s->hardware.ext_led_brightness);
    ESP_LOGI(TAG, "  RGB Temp Zones: Cold:%.1f-%.1f Warm:%.1f-%.1f Hot:%.1f-%.1f Danger:%.1f Freeze:%.1f",
             s->hardware.ext_led_temp_cold_min, s->hardware.ext_led_temp_cold_max,
             s->hardware.ext_led_temp_warm_min, s->hardware.ext_led_temp_warm_max,
             s->hardware.ext_led_temp_hot_min, s->hardware.ext_led_temp_hot_max,
             s->hardware.ext_led_temp_danger, s->hardware.ext_led_temp_freeze);
    ESP_LOGI(TAG, "  RGB Colors    : Cold(%04x,%04x) Warm(%04x,%04x) Hot(%04x,%04x) Danger(%04x,%04x) Freeze(%04x,%04x)",
             s->hardware.ext_led_color_cold_x, s->hardware.ext_led_color_cold_y,
             s->hardware.ext_led_color_warm_x, s->hardware.ext_led_color_warm_y,
             s->hardware.ext_led_color_hot_x, s->hardware.ext_led_color_hot_y,
             s->hardware.ext_led_color_danger_x, s->hardware.ext_led_color_danger_y,
             s->hardware.ext_led_color_freeze_x, s->hardware.ext_led_color_freeze_y);
    ESP_LOGI(TAG, "  RGB Mode      : %u  Anim: %u",
             (unsigned)s->hardware.ext_led_mode,
             (unsigned)s->hardware.ext_led_anim);

    /* --- Calibration --- */
    ESP_LOGI(TAG, "[Calibration Settings] (v%u)",
             (unsigned)s->calibration.version);
    ESP_LOGI(TAG, "  Radiator Cal  : %.2f C", s->calibration.ir_object_cal);
    ESP_LOGI(TAG, "  IR Ambient Cal: %.2f C", s->calibration.ir_ambient_cal);

    /* --- Thermal --- */
    ESP_LOGI(TAG, "[Thermal Settings] (v%u)", (unsigned)s->thermal.version);
    ESP_LOGI(TAG, "  CPU Warning   : %.1f C", s->thermal.cpu_temp_warning);
    ESP_LOGI(TAG, "  CPU Critical  : %.1f C", s->thermal.cpu_temp_critical);
    ESP_LOGI(TAG, "  CPU Emergency : %.1f C", s->thermal.cpu_temp_emergency);
    ESP_LOGI(TAG, "  CPU Polling   : %u ms",
             (unsigned)s->thermal.cpu_polling_interval_ms);

    /* --- Static Pin Definitions (board verification) --- */
    ESP_LOGI(TAG, "--- Static Pin Definitions ---");
    ESP_LOGI(TAG, "  Relay: %d | MLX SCL: %d  SDA: %d",
             RELAY_IN_PIN, MLX90614_SCL_PIN, MLX90614_SDA_PIN);
    ESP_LOGI(TAG, "  Rotary CLK: %d  DT: %d  SW: %d",
             ROTARY_CLK_PIN, ROTARY_DT_PIN, ROTARY_SW_PIN);
    ESP_LOGI(TAG, "  RGB R: %d  G: %d  B: %d",
             RGB_RED_PIN, RGB_GREEN_PIN, RGB_BLUE_PIN);
    ESP_LOGI(TAG, "  Built-in LED: %d", BUILTIN_LED_PIN);

    ESP_LOGI(TAG, "==================================================");
    return ESP_OK;
}

/* Shared by NVS validation and Zigbee command handlers. */
bool SystemConfigs_ValidateRGBThermal(const HardwareConfigs *h) {
    if (!h) return false;
#define VALID_RANGE(a, b, lo, hi) (isfinite(h->a) && isfinite(h->b) && h->a >= (lo) && h->b <= (hi) && h->a < h->b)
    return VALID_RANGE(ext_led_temp_cold_min, ext_led_temp_cold_max, RGB_COLD_MIN_LIMIT, RGB_COLD_MAX_LIMIT) &&
           VALID_RANGE(ext_led_temp_warm_min, ext_led_temp_warm_max, RGB_WARM_MIN_LIMIT, RGB_WARM_MAX_LIMIT) &&
           VALID_RANGE(ext_led_temp_hot_min, ext_led_temp_hot_max, RGB_HOT_MIN_LIMIT, RGB_HOT_MAX_LIMIT) &&
           isfinite(h->ext_led_temp_danger) && isfinite(h->ext_led_temp_freeze) &&
           h->ext_led_temp_danger >= RGB_DANGER_MIN_LIMIT && h->ext_led_temp_danger <= RGB_DANGER_MAX_LIMIT &&
           h->ext_led_temp_freeze >= RGB_FREEZE_MIN_LIMIT && h->ext_led_temp_freeze <= RGB_FREEZE_MAX_LIMIT;
#undef VALID_RANGE
}

bool SystemConfigs_RGBZoneOffsets(unsigned zone, size_t *x, size_t *y) {
    static const size_t offsets[RGB_ZONE_COUNT][2] = {
        {offsetof(HardwareConfigs,ext_led_color_cold_x),offsetof(HardwareConfigs,ext_led_color_cold_y)},
        {offsetof(HardwareConfigs,ext_led_color_warm_x),offsetof(HardwareConfigs,ext_led_color_warm_y)},
        {offsetof(HardwareConfigs,ext_led_color_hot_x),offsetof(HardwareConfigs,ext_led_color_hot_y)},
        {offsetof(HardwareConfigs,ext_led_color_danger_x),offsetof(HardwareConfigs,ext_led_color_danger_y)},
        {offsetof(HardwareConfigs,ext_led_color_freeze_x),offsetof(HardwareConfigs,ext_led_color_freeze_y)}};
    if (zone>=RGB_ZONE_COUNT || !x || !y) return false;
    *x=offsets[zone][0];*y=offsets[zone][1];return true;
}
bool SystemConfigs_GetRGBZoneXY(const HardwareConfigs *hardware, unsigned zone, uint16_t *x, uint16_t *y) {
    size_t offsetX,offsetY;
    if (!hardware || !x || !y || !SystemConfigs_RGBZoneOffsets(zone,&offsetX,&offsetY)) return false;
    memcpy(x,(const uint8_t *)hardware+offsetX,sizeof(*x));
    memcpy(y,(const uint8_t *)hardware+offsetY,sizeof(*y));return true;
}
bool SystemConfigs_ValidateRGBXY(const HardwareConfigs *hardware) {
    if (!hardware) return false;
    for (unsigned zone=0;zone<RGB_ZONE_COUNT;++zone) {
        uint16_t x,y;
        if (!SystemConfigs_GetRGBZoneXY(hardware,zone,&x,&y) || !SystemConfigs_ValidateRGBCoordinates(x,y)) return false;
    }
    return true;
}
bool SystemConfigs_ValidateRGBCoordinates(uint16_t x,uint16_t y) {
    return x<=RGB_XY_MAX_COORDINATE && y>0 && y<=RGB_XY_MAX_COORDINATE;
}
