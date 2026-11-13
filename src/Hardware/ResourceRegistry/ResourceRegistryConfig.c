#include "Hardware/ResourceRegistry/ResourceRegistryConfig.h"

ResourceRegistryConfig ResourceRegistryConfig_GetDefault(void) {
    ResourceRegistryConfig config = {
        /* Logical reservation slots; peripheral drivers validate SDK capabilities. */
        .max_gpio_entries = 32,
        .max_i2c_entries = 2,
        .max_pwm_entries = 6,
        .max_adc_entries = 2,
        .enable_logging = true,
        .enable_events = true,
        .enable_stats = true
    };
    return config;
}

