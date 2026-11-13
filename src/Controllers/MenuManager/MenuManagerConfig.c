#include "Controllers/MenuManager/MenuManagerConfig.h"
#include <stddef.h>

static const char* const stateLabels[UI_STATE_COUNT] = {
    [UI_STATE_CAT_BUILTIN_LED] = "Built-in LED",
    [UI_STATE_CAT_EXTERNAL_LED] = "External LED",
    [UI_STATE_CAT_BUILTIN_ANIMATIONS] = "Animations",
    [UI_STATE_CAT_RGB_REGIONS] = "Temperature zones",
    [UI_STATE_CAT_ZONE_FREEZE] = "Freezing",
    [UI_STATE_CAT_ZONE_COLD] = "Cold",
    [UI_STATE_CAT_ZONE_WARM] = "Warm",
    [UI_STATE_CAT_ZONE_HOT] = "Hot",
    [UI_STATE_CAT_ZONE_DANGER] = "Danger",
    [UI_STATE_CAT_SCREEN] = "Display",
    [UI_STATE_CAT_CALIBRATION] = "Calibration",
    [UI_STATE_CAT_ZIGBEE] = "Zigbee",
    [UI_STATE_CAT_ENERGY] = "Power management",
    [UI_STATE_CAT_ADVANCED] = "Advanced",
    [UI_STATE_CAT_MAINTENANCE] = "Maintenance",
    [UI_STATE_SET_BUILTIN_MODE] = "State",
    [UI_STATE_SET_DISPLAY_MODE] = "Display mode",
    [UI_STATE_SET_MENU_AUTO_EXIT] = "Menu auto exit",

    [UI_STATE_MONITOR] = "Izleme",
    [UI_STATE_MENU_ROOT] = "Menu",
    [UI_STATE_CAT_TEMP] = "Fan",
    [UI_STATE_CAT_LIGHT] = "Lighting",
    [UI_STATE_CAT_RGB_XY] = "Ozel RGB XY",
    [UI_STATE_CAT_DEVICE] = "Preferences",
    [UI_STATE_CAT_POWER] = "System",
    [UI_STATE_EXIT] = "Cikis",
    
    [UI_STATE_SET_LOW_TEMP] = "Fan Kapanma Sıcaklığı",
    [UI_STATE_SET_HIGH_TEMP] = "Fan Açılma Sıcaklığı",
    [UI_STATE_SET_TEMP_UNIT] = "Birim",
    [UI_STATE_FAN_MODE] = "Fan Çalışma Modu",
    [UI_STATE_FAN_DELTA_ON] = "Fan Açılma Sıcaklık Farkı",
    [UI_STATE_FAN_DELTA_OFF] = "Fan Kapanma Sıcaklık Farkı",
    [UI_STATE_OBJECT_SOURCE] = "Nesne Sıcaklığı Kaynağı",
    [UI_STATE_AMBIENT_SOURCE] = "Ortam Sıcaklığı Kaynağı",
    [UI_STATE_CALIBRATE_OBJECT] = "Radyator Kalib",
    [UI_STATE_CALIBRATE_AMBIENT] = "Ortam Kalib",
    
    [UI_STATE_SET_LED_MODE] = "Harici LED",
    [UI_STATE_SET_BUILTIN_BRIGHTNESS] = "Dahili Parlak",
    [UI_STATE_SET_BUILTIN_IDLE] = "Dahili Bosta",
    [UI_STATE_SET_BUILTIN_STARTUP] = "Dahili Baslangic",
    [UI_STATE_SET_BUILTIN_HEATING] = "Dahili Isitma",
    [UI_STATE_SET_BUILTIN_CONNECTING] = "Dahili Baglanti",
    [UI_STATE_SET_BUILTIN_FAULT] = "Dahili Hata",
    [UI_STATE_SET_LED_ANIM] = "Boot Anim",
    [UI_STATE_SET_RGB_COLD] = "RGB Soguk",
    [UI_STATE_SET_RGB_WARM] = "RGB Ilik",
    [UI_STATE_SET_RGB_HOT] = "RGB Sicak",
    [UI_STATE_SET_RGB_DANGER] = "Danger zone color",
    [UI_STATE_SET_RGB_FREEZE] = "Freeze zone color",
    [UI_STATE_SET_RGB_BRIGHTNESS] = "Parlaklik",
    [UI_STATE_SET_RGB_TEMP_COLD] = "Soguk Esik",
    [UI_STATE_SET_RGB_TEMP_COLD_MAX] = "Soguk Ust Esik",
    [UI_STATE_SET_RGB_TEMP_WARM] = "Ilik Esik",
    [UI_STATE_SET_RGB_TEMP_WARM_MAX] = "Ilik Ust Esik",
    [UI_STATE_SET_RGB_TEMP_HOT] = "Sicak Esik",
    [UI_STATE_SET_RGB_TEMP_HOT_MAX] = "Sicak Ust Esik",
    [UI_STATE_SET_RGB_TEMP_DANGER] = "Tehlike Siniri",
    [UI_STATE_SET_RGB_TEMP_FREEZE] = "Donma Siniri",
    
    [UI_STATE_SET_TIMEOUT] = "Ekran Sure",
    [UI_STATE_SET_LANGUAGE] = "Dil / Language",
    [UI_STATE_SYSTEM_STATUS] = "Sistem Durum",
    [UI_STATE_ZIGBEE_PAIR] = "Zigbee Esles",
    [UI_STATE_SET_TELEMETRY] = "Telemetri",
    
    [UI_STATE_CAT_ZIGBEE_REP] = "Zigbee Rapor",
    [UI_STATE_ZB_REP_FAN] = "Radyator (s)",
    [UI_STATE_ZB_REP_AMBIENT] = "Ortam (s)",
    [UI_STATE_ZB_REP_CPU] = "CPU (s)",
    [UI_STATE_ZB_REP_HEALTH] = "Saglik (s)",
    [UI_STATE_ZB_REP_MEMORY] = "Bellek (s)",
    [UI_STATE_ZB_REP_UPTIME] = "Calisma (s)",
    
    [UI_STATE_FACTORY_RESET] = "Fabrika Sifir",
    [UI_STATE_SET_POWER_PROFILE] = "Guc Profili",
    [UI_STATE_SET_ZIGBEE_TX_PROFILE] = "Zigbee Gucu",
    [UI_STATE_RESTART] = "Yeniden Baslat",
    [UI_STATE_SHUTDOWN] = "Kapat",
    
    [UI_STATE_BACK] = "Geri"
};

MenuManagerConfig MenuManager_GetDefaultConfig(void) {
    MenuManagerConfig config = {
        .eventQueueSize = 16,
        /* Synchronous validation, NVS and event callbacks share this worker. */
        .taskStackSize = MENU_TASK_STACK_DEFAULT_BYTES,
        .taskPriority = 5,
        .circularNavigation = true
    };
    return config;
}

const char* MenuManager_GetStateLabel(UIState state) {
    if ((unsigned)state < UI_STATE_COUNT) {
        return stateLabels[state];
    }
    return "Unknown";
}
