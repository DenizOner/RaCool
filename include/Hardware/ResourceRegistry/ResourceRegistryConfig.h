#ifndef RESOURCE_REGISTRY_CONFIG_H
#define RESOURCE_REGISTRY_CONFIG_H

#include <stdbool.h>
#include <stdint.h>

#define RESOURCE_REGISTRY_OWNER_CAPACITY 16
#define RESOURCE_REGISTRY_MAX_SHARED_OWNERS 8
#define RESOURCE_REGISTRY_LOCK_TIMEOUT_MS 200
#define RESOURCE_REGISTRY_MAX_GPIO_ENTRIES 64
#define RESOURCE_REGISTRY_MAX_I2C_ENTRIES 8
#define RESOURCE_REGISTRY_MAX_PWM_ENTRIES 16
#define RESOURCE_REGISTRY_MAX_ADC_ENTRIES 16

/**
 * @file ResourceRegistryConfig.h
 * @brief Configuration structure and default factory for ResourceRegistry.
 */

/**
 * @brief Resource Registry Configuration Structure.
 *
 * Defines maximum capacities for each hardware resource type and feature toggles.
 */
typedef struct {
    uint32_t max_gpio_entries;    /**< Logical GPIO reservation capacity. */
    uint32_t max_i2c_entries;     /**< Logical I2C reservation capacity. */
    uint32_t max_pwm_entries;     /**< Logical PWM reservation capacity. */
    uint32_t max_adc_entries;     /**< Logical ADC reservation capacity. */
    bool enable_logging;          /**< Enable detailed logging of reservations/releases. */
    bool enable_events;           /**< Enable EventBus publishing for resource events. */
    bool enable_stats;            /**< Enable tracking of reservation/release statistics. */
} ResourceRegistryConfig;

/**
 * @brief Get the default configuration for the Resource Registry.
 *
 * Drivers validate physical SDK capabilities separately from registry capacities.
 *
 * @return ResourceRegistryConfig Default configuration object.
 */
ResourceRegistryConfig ResourceRegistryConfig_GetDefault(void);

#endif // RESOURCE_REGISTRY_CONFIG_H

