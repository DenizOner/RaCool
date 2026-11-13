/**
 * @file WatchdogConfig.c
 * @brief Default configuration and validation logic for the Watchdog module.
 *
 * All numeric literals in this file are sourced from the macros defined in
 * WatchdogConfig.h so that a single edit propagates everywhere.
 *
 * @author RaCool DeepMind Team
 * @date 2026-08-01
 */

#include "Management/Watchdog/WatchdogConfig.h"
#include <stddef.h>

/* -----------------------------------------------------------------------
 * Public API
 * --------------------------------------------------------------------- */

WatchdogConfig WatchdogConfig_GetDefault(void) {
    WatchdogConfig config = {
        .timeout_ms           = WATCHDOG_DEFAULT_TIMEOUT_MS,
        .warning_threshold_ms = WATCHDOG_WARNING_THRESHOLD_MS,
        .reset_behavior       = WATCHDOG_RESET_TASK,
        .auto_feed_enabled    = true,
        .panic_on_timeout     = true,
        .max_tasks            = WATCHDOG_MAX_TASKS,
    };
    return config;
}

bool WatchdogConfig_IsValid(const WatchdogConfig *config) {
    if (config == NULL) {
        return false;
    }

    /* Timeout must be within the hardware-supported range. */
    if (config->timeout_ms < WATCHDOG_MIN_TIMEOUT_MS ||
        config->timeout_ms > WATCHDOG_MAX_TIMEOUT_MS) {
        return false;
    }

    /* Warning threshold must be positive and strictly less than the timeout
     * so that the warning event always precedes the trip. */
    if (config->warning_threshold_ms == 0 ||
        config->warning_threshold_ms >= config->timeout_ms) {
        return false;
    }

    /* Only the two declared enum values are valid. */
    if (config->reset_behavior != WATCHDOG_RESET_TASK &&
        config->reset_behavior != WATCHDOG_RESET_SYSTEM) {
        return false;
    }

    /* Task count must be at least 1 and not exceed the absolute upper bound. */
    if (config->max_tasks == 0 || config->max_tasks > WATCHDOG_MAX_TASKS_LIMIT) {
        return false;
    }

    return true;
}
