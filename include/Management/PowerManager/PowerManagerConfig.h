#ifndef POWER_MANAGER_CONFIG_H
#define POWER_MANAGER_CONFIG_H

#include <stdbool.h>
#include <stdint.h>

#define POWER_DEFAULT_CONSOLE_SLEEP_MS 1000u
#define POWER_DEFAULT_RESTART_DELAY_MS 500u
#define POWER_MAX_RESTART_DELAY_MS 60000u
#define POWER_MUTEX_TIMEOUT_MS 1000u
/* ESP-IDF 5.3.1 C6 RTC conversion shifts microseconds left by 19 bits. */
#define POWER_SLEEP_MAX_DURATION_US (UINT64_MAX >> 19)
#define POWER_MAX_LOCK_OWNERS 8
#define POWER_LOCK_NAME_CAPACITY 32

/**
 * @file PowerManagerConfig.h
 * @brief Configuration parameters for PowerManager.
 */

/**
 * @brief Configuration structure for the PowerManager
 */
typedef struct {
    uint8_t defaultMode;         /**< Initial power mode (0: Perf, 1: Balanced, 2: LowPower) */
    bool autoLightSleepEnabled;   /**< Enables ESP-IDF automatic light sleep */
    uint32_t preSleepDelayMs;     /**< Grace period delay in ms for pre-sleep EventBus processing */
    bool enableRotaryWakeup;      /**< Enable Rotary SW button GPIO wakeup */
    uint8_t rotaryWakeupLevel;    /**< Wakeup trigger logic level (0 for LOW, 1 for HIGH) */
} PowerManagerConfig;

/**
 * @brief Get the default configuration for the PowerManager
 *
 * @return PowerManagerConfig struct with default assignments
 */
PowerManagerConfig PowerManagerConfig_GetDefault(void);

typedef struct {
    int minFrequencyMHz;
    int maxFrequencyMHz;
    bool automaticLightSleep;
} PowerModeProfile;

/** Return the router-safe profile; reject unknown modes without modifying output. */
bool PowerManagerConfig_GetProfile(uint8_t mode, PowerModeProfile *profile);

#endif // POWER_MANAGER_CONFIG_H

