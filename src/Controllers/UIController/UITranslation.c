#include "Hardware/LEDAnimations/LEDAnimationLabels.h"
#include "Controllers/UIController/UITranslation.h"
#include "Hardware/LEDAnimations/LEDAnimationCatalog.h"

const char *UITranslation_MenuLabel(UIState state, const char *fallback,
                                   const LocalizationCatalog *catalog) {
    static const char *const keys[UI_STATE_COUNT] = {
        [UI_STATE_CAT_BUILTIN_LED] = "menu.builtin_led",
        [UI_STATE_CAT_EXTERNAL_LED] = "menu.external_led",
        [UI_STATE_CAT_BUILTIN_ANIMATIONS] = "menu.animations",
        [UI_STATE_CAT_RGB_REGIONS] = "menu.regions",
        [UI_STATE_CAT_ZONE_FREEZE] = "menu.zone_freeze",
        [UI_STATE_CAT_ZONE_COLD] = "menu.zone_cold",
        [UI_STATE_CAT_ZONE_WARM] = "menu.zone_warm",
        [UI_STATE_CAT_ZONE_HOT] = "menu.zone_hot",
        [UI_STATE_CAT_ZONE_DANGER] = "menu.zone_danger",
        [UI_STATE_CAT_SCREEN] = "menu.screen",
        [UI_STATE_CAT_CALIBRATION] = "menu.calibration",
        [UI_STATE_CAT_ZIGBEE] = "menu.zigbee",
        [UI_STATE_CAT_ENERGY] = "menu.energy",
        [UI_STATE_CAT_ADVANCED] = "menu.advanced",
        [UI_STATE_CAT_MAINTENANCE] = "menu.maintenance",
        [UI_STATE_SET_BUILTIN_MODE] = "menu.builtin_state",
        [UI_STATE_SET_DISPLAY_MODE] = "menu.display_mode",
        [UI_STATE_MONITOR] = "menu.monitor",
        [UI_STATE_MENU_ROOT] = "menu.root",
        [UI_STATE_CAT_TEMP] = "menu.temperature",
        [UI_STATE_CAT_LIGHT] = "menu.lighting",
        [UI_STATE_CAT_DEVICE] = "menu.device",
        [UI_STATE_CAT_POWER] = "menu.power",
        [UI_STATE_EXIT] = "menu.exit",
        [UI_STATE_BACK] = "menu.back",
        [UI_STATE_CAT_RGB_XY] = "menu.zone_xy",
        [UI_STATE_SET_LOW_TEMP] = "menu.fan_stop_temperature",
        [UI_STATE_SET_HIGH_TEMP] = "menu.fan_start_temperature",
        [UI_STATE_SET_TEMP_UNIT] = "menu.temperature_unit",
        [UI_STATE_FAN_MODE] = "menu.fan_mode",
        [UI_STATE_FAN_DELTA_ON] = "menu.fan_delta_on",
        [UI_STATE_FAN_DELTA_OFF] = "menu.fan_delta_off",
        [UI_STATE_OBJECT_SOURCE] = "menu.object_source",
        [UI_STATE_AMBIENT_SOURCE] = "menu.ambient_source",
        [UI_STATE_CALIBRATE_OBJECT] = "menu.object_calibration",
        [UI_STATE_CALIBRATE_AMBIENT] = "menu.ambient_calibration",
        [UI_STATE_SET_LED_MODE] = "menu.external_led_mode",
        [UI_STATE_SET_BUILTIN_BRIGHTNESS] = "menu.internal_brightness",
        [UI_STATE_SET_BUILTIN_IDLE] = "menu.internal_idle_animation",
        [UI_STATE_SET_BUILTIN_STARTUP] = "menu.internal_startup_animation",
        [UI_STATE_SET_BUILTIN_HEATING] = "menu.internal_heating_animation",
        [UI_STATE_SET_BUILTIN_CONNECTING] = "menu.internal_connecting_animation",
        [UI_STATE_SET_BUILTIN_FAULT] = "menu.internal_fault_animation",
        [UI_STATE_SET_LED_ANIM] = "menu.boot_animation",
        [UI_STATE_SET_RGB_COLD] = "menu.cold_color",
        [UI_STATE_SET_RGB_WARM] = "menu.warm_color",
        [UI_STATE_SET_RGB_HOT] = "menu.hot_color",
        [UI_STATE_SET_RGB_DANGER] = "menu.danger_color",
        [UI_STATE_SET_RGB_FREEZE] = "menu.freeze_color",
        [UI_STATE_SET_RGB_BRIGHTNESS] = "menu.external_brightness",
        [UI_STATE_SET_TIMEOUT] = "menu.display_timeout",
        [UI_STATE_SET_MENU_AUTO_EXIT] = "menu.auto_exit",
        [UI_STATE_SET_LANGUAGE] = "display.language",
        [UI_STATE_SYSTEM_STATUS] = "menu.system_status",
        [UI_STATE_ZIGBEE_PAIR] = "menu.zigbee_pairing",
        [UI_STATE_SET_TELEMETRY] = "menu.telemetry",
        [UI_STATE_CAT_ZIGBEE_REP] = "menu.zigbee_reports",
        [UI_STATE_ZB_REP_FAN] = "menu.radiator_report",
        [UI_STATE_ZB_REP_AMBIENT] = "menu.ambient_report",
        [UI_STATE_ZB_REP_CPU] = "menu.cpu_report",
        [UI_STATE_ZB_REP_HEALTH] = "menu.health_report",
        [UI_STATE_ZB_REP_MEMORY] = "menu.memory_report",
        [UI_STATE_ZB_REP_UPTIME] = "menu.uptime_report",
        [UI_STATE_FACTORY_RESET] = "menu.factory_reset",
        [UI_STATE_SET_POWER_PROFILE] = "menu.cpu_profile",
        [UI_STATE_SET_ZIGBEE_TX_PROFILE] = "menu.radio_profile",
        [UI_STATE_RESTART] = "menu.restart",
        [UI_STATE_SHUTDOWN] = "menu.shutdown",
        [UI_STATE_SET_RGB_TEMP_COLD] = "menu.cold_min",
        [UI_STATE_SET_RGB_TEMP_COLD_MAX] = "menu.cold_max",
        [UI_STATE_SET_RGB_TEMP_WARM] = "menu.warm_min",
        [UI_STATE_SET_RGB_TEMP_WARM_MAX] = "menu.warm_max",
        [UI_STATE_SET_RGB_TEMP_HOT] = "menu.hot_min",
        [UI_STATE_SET_RGB_TEMP_HOT_MAX] = "menu.hot_max",
        [UI_STATE_SET_RGB_TEMP_DANGER] = "menu.danger_threshold",
        [UI_STATE_SET_RGB_TEMP_FREEZE] = "menu.freeze_threshold",
        [UI_STATE_RGB_COLD_X] = "menu.cold_x",
        [UI_STATE_RGB_COLD_Y] = "menu.cold_y",
        [UI_STATE_RGB_WARM_X] = "menu.warm_x",
        [UI_STATE_RGB_WARM_Y] = "menu.warm_y",
        [UI_STATE_RGB_HOT_X] = "menu.hot_x",
        [UI_STATE_RGB_HOT_Y] = "menu.hot_y",
        [UI_STATE_RGB_DANGER_X] = "menu.danger_x",
        [UI_STATE_RGB_DANGER_Y] = "menu.danger_y",
        [UI_STATE_RGB_FREEZE_X] = "menu.freeze_x",
        [UI_STATE_RGB_FREEZE_Y] = "menu.freeze_y",
    };
    if ((unsigned)state < UI_STATE_COUNT && keys[state]) {
        const char *text = Localization_TextKey(catalog,keys[state]);
        if (text) return text;
    }
    return fallback ? fallback : Localization_Text(catalog,LOC_COMMON_UNKNOWN);
}

const char *UITranslation_Animation(uint8_t id, bool builtin,
                                   const LocalizationCatalog *catalog) {
    return LEDAnimationLabel_Localized(id, builtin, catalog);
}

const char *UITranslation_Color(PresetColorIndex color,
                               const LocalizationCatalog *catalog) {
    static const char *const keys[RGB_COLOR_COUNT] = {
        [RGB_COLOR_BLUE] = "color.blue",
        [RGB_COLOR_CYAN] = "color.cyan",
        [RGB_COLOR_GREEN] = "color.green",
        [RGB_COLOR_YELLOW] = "color.yellow",
        [RGB_COLOR_ORANGE] = "color.orange",
        [RGB_COLOR_RED] = "color.red",
        [RGB_COLOR_PURPLE] = "color.purple",
        [RGB_COLOR_WHITE] = "color.white",
    };
    if ((unsigned)color >= RGB_COLOR_COUNT) return Localization_Text(catalog,LOC_COMMON_UNKNOWN);
    const char *text=Localization_TextKey(catalog,keys[color]);
    return text ? text : Localization_Text(catalog,LOC_COMMON_UNKNOWN);
}
