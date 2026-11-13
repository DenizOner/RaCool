#ifndef PINMANAGER_CONFIG_H
#define PINMANAGER_CONFIG_H

#include "Hardware/PinManager/PinDefinitions.h"
#include <driver/gpio.h>
#include <stdint.h>

#define PIN_MANAGER_LOCK_TIMEOUT_MS 200

/**
 * @file PinManagerConfig.h
 * @brief PinManager configuration definitions.
 */

/**
 * @brief Startup configuration for one pin function.
 */
typedef struct {
    gpio_num_t pin;             ///< GPIO number
    PinMode_t mode;             ///< Initial mode
    PinPull_t pull;             ///< Initial pull resistor selection
    bool initialState;          ///< Initial output level
    uint32_t debounceMs;        ///< Software debounce interval in milliseconds
} PinEntryConfig_t;

/**
 * @brief PinManager service configuration.
 */
typedef struct {
  PinEntryConfig_t entries[PIN_FUNC_MAX_COUNT]; ///< Configuration per function
  uint32_t defaultDebounceMs;                   ///< Default debounce interval
} PinManagerConfig;

/**
 * @brief Returns the default PinManager configuration.
 * @return PinManagerConfig
 */
PinManagerConfig PinManagerConfig_GetDefault(void);

#endif // PINMANAGER_CONFIG_H
