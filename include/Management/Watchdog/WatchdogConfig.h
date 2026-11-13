/**
 * @file WatchdogConfig.h
 * @brief Configuration data structures and compile-time defaults for the Watchdog module.
 *
 * All tuneable constants are defined here as macros so that a single change
 * propagates to both WatchdogConfig.c (defaults) and Watchdog.c (validation
 * boundaries) without duplicating magic numbers.
 *
 * @author RaCool DeepMind Team
 * @date 2026-08-01
 */

#ifndef WATCHDOG_CONFIG_H
#define WATCHDOG_CONFIG_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -----------------------------------------------------------------------
 * Compile-time defaults and boundary constants
 * --------------------------------------------------------------------- */

/** Default watchdog timeout in milliseconds. */
#define WATCHDOG_DEFAULT_TIMEOUT_MS      5000U

/** Registration retry cadence and bounded owner-control wait. */
#define WATCHDOG_WORKER_RETRY_MS          1000U
#define WATCHDOG_WORKER_CONTROL_TIMEOUT_MS 2000U

/** Minimum allowed timeout value (ms). Below this the hardware TWDT cannot
 *  operate reliably on ESP32-C6. */
#define WATCHDOG_MIN_TIMEOUT_MS          1000U

/** Maximum allowed timeout value (ms). Chosen conservatively; a service that
 *  cannot feed within 60 s has a functional problem regardless. */
#define WATCHDOG_MAX_TIMEOUT_MS          60000U

/** Default warning threshold (ms). A warning event is published when the
 *  service has not been fed for this long. Must be < WATCHDOG_DEFAULT_TIMEOUT_MS. */
#define WATCHDOG_WARNING_THRESHOLD_MS    4000U

/** Default maximum number of concurrently monitored tasks. Capped at 32 to
 *  keep the internal handle array size manageable on embedded targets. */
#define WATCHDOG_MAX_TASKS               16U

/** Absolute upper bound for max_tasks to prevent absurd configurations. */
#define WATCHDOG_MAX_TASKS_LIMIT         32U

/* -----------------------------------------------------------------------
 * Types
 * --------------------------------------------------------------------- */

/**
 * @brief Action taken when the watchdog trip is triggered.
 *
 * WATCHDOG_RESET_TASK   – Only the offending task is notified / deleted.
 * WATCHDOG_RESET_SYSTEM – A full system panic / reset is triggered.
 */
typedef enum {
    WATCHDOG_RESET_TASK   = 0,
    WATCHDOG_RESET_SYSTEM = 1
} WatchdogResetBehavior;

/**
 * @brief Watchdog module configuration structure.
 *
 * Populated with sensible defaults by WatchdogConfig_GetDefault() and
 * validated by WatchdogConfig_IsValid() before being applied.
 */
typedef struct {
    /** System watchdog timeout in milliseconds. */
    uint32_t timeout_ms;

    /** Warning alert threshold (ms) before the watchdog trips.
     *  Must be strictly less than timeout_ms. */
    uint32_t warning_threshold_ms;

    /** Action taken upon watchdog expiry. */
    WatchdogResetBehavior reset_behavior;

    /** When true, a periodic esp_timer will automatically feed the TWDT so
     *  that the service does not block routine background task monitoring. */
    bool auto_feed_enabled;

    /** When true, a system panic (and subsequent reset) is triggered on
     *  watchdog timeout. When false, only the offending task is notified. */
    bool panic_on_timeout;

    /** Maximum number of tasks that may be registered simultaneously. */
    uint8_t max_tasks;
} WatchdogConfig;

/* -----------------------------------------------------------------------
 * API
 * --------------------------------------------------------------------- */

/**
 * @brief Returns a WatchdogConfig populated with safe compile-time defaults.
 *
 * @return Fully initialised WatchdogConfig value.
 */
WatchdogConfig WatchdogConfig_GetDefault(void);

/**
 * @brief Validates all fields of a WatchdogConfig structure.
 *
 * Checks:
 *   - config is not NULL
 *   - timeout_ms is within [WATCHDOG_MIN_TIMEOUT_MS, WATCHDOG_MAX_TIMEOUT_MS]
 *   - warning_threshold_ms is in (0, timeout_ms)
 *   - reset_behavior is a known enum value
 *   - max_tasks is in [1, WATCHDOG_MAX_TASKS_LIMIT]
 *
 * @param config Pointer to the configuration structure to inspect.
 * @return true if all fields are valid, false otherwise.
 */
bool WatchdogConfig_IsValid(const WatchdogConfig *config);

#ifdef __cplusplus
}
#endif

#endif // WATCHDOG_CONFIG_H
