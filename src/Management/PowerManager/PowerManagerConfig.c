#include "Management/PowerManager/PowerManagerConfig.h"
#include <esp_sleep.h>

PowerManagerConfig PowerManagerConfig_GetDefault(void) {
    PowerManagerConfig config = {
        .defaultMode = 1,              // POWER_MODE_BALANCED
        .autoLightSleepEnabled = false,
        .preSleepDelayMs = 200,        // 200 ms grace period for events
        .enableRotaryWakeup = true,
        .rotaryWakeupLevel = 0,        // ESP_GPIO_WAKEUP_GPIO_LOW is 0
    };
    return config;
}



bool PowerManagerConfig_GetProfile(uint8_t mode, PowerModeProfile *profile) {
    static const PowerModeProfile profiles[] = {
        {160, 160, false},
        {80, 160, false},
        {40, 80, false},
    };
    if (!profile || mode >= sizeof(profiles) / sizeof(profiles[0])) return false;
    *profile = profiles[mode];
    return true;
}
