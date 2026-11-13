/**
 * @file IWatchdog.h
 * @brief Watchdog Management Service Interface for the RaCool ESP32-C6 System.
 *
 * Provides a thread-safe, event-driven watchdog service wrapper over the
 * ESP-IDF Task Watchdog Timer (TWDT).  Key capabilities:
 *
 *  - Task registration and deregistration with overflow protection.
 *  - Periodic automatic feed via esp_timer (when auto_feed_enabled = true).
 *  - Warning events published to the EventBus before the watchdog trips.
 *  - Rich operational statistics exposed through GetStats().
 *  - Comprehensive self-test covering lock, feed, and stats consistency.
 *  - Full Debug Console CLI integration.
 *
 * All public methods are thread-safe and callable from any FreeRTOS task.
 *
 * @author RaCool DeepMind Team
 * @date 2026-08-01
 */

#ifndef IWATCHDOG_H
#define IWATCHDOG_H

#include "Core/IService.h"
#include "Management/Watchdog/WatchdogConfig.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -----------------------------------------------------------------------
 * EventBus topic strings
 * --------------------------------------------------------------------- */

/** Published after every successful manual or automatic feed operation. */
#define EVENT_WATCHDOG_FED           "watchdog.fed"

/** Published when the service has not been fed for warning_threshold_ms. */
#define EVENT_WATCHDOG_WARNING       "watchdog.warning"

/** Published when the TWDT fires (timeout reached). */
#define EVENT_WATCHDOG_TIMEOUT       "watchdog.timeout"

/** Published when a new task is added to the monitored set. */
#define EVENT_WATCHDOG_TASK_ADDED    "watchdog.task_added"

/** Published when a task is removed from the monitored set. */
#define EVENT_WATCHDOG_TASK_REMOVED  "watchdog.task_removed"

/** Published on any lifecycle state transition (Init / Start / Stop). */
#define EVENT_WATCHDOG_STATE_CHANGED "watchdog.state_changed"

/** Published when a system panic is imminent due to a watchdog trip. */
#define EVENT_WATCHDOG_PANIC         "watchdog.panic"

/** Published by the auto-feed timer on each successful automatic feed. */
#define EVENT_WATCHDOG_AUTO_FEED     "watchdog.auto_feed"

/* -----------------------------------------------------------------------
 * Statistics
 * --------------------------------------------------------------------- */

/**
 * @brief Operational metrics collected by the Watchdog service.
 *
 * Obtained through IWatchdog::GetStats().  All fields are accumulated since
 * the last call to Initialize() and are never reset during normal operation.
 */
typedef struct {
    /** Total number of manual feed operations that completed successfully. */
    uint32_t totalFeeds;

    /** Total number of automatic (timer-driven) feed operations. */
    uint32_t totalAutoFeeds;

    /** Total number of tasks added to the monitored set. */
    uint32_t totalTasksAdded;

    /** Total number of tasks removed from the monitored set. */
    uint32_t totalTasksRemoved;

    /** Current count of actively monitored tasks. */
    uint32_t activeTaskCount;

    /** Total number of warning threshold-breach events generated. */
    uint32_t totalWarnings;

    /** Total number of watchdog timeout (trip) events detected. */
    uint32_t totalTimeouts;

    /** Timestamp of the last successful feed in milliseconds since boot. */
    uint64_t lastFeedTimeMs;

    /** Timestamp of the last timeout event (0 if none occurred). */
    uint64_t lastTimeoutMs;
} WatchdogStats_t;

/* -----------------------------------------------------------------------
 * Interface
 * --------------------------------------------------------------------- */

/** Forward declaration for the vtable struct. */
typedef struct IWatchdog IWatchdog;

struct IWatchdog {
    /** Base service interface (must remain the first member for safe casting). */
    IService base;

    /**
     * @brief Feeds (resets) the hardware Task Watchdog Timer.
     *
     * No-op when the service is not in SERVICE_STATE_STARTED.
     *
     * @param self Pointer to the IWatchdog instance.
     */
    void (*Feed)(IWatchdog *self);

    /**
     * @brief Registers a FreeRTOS task for watchdog supervision.
     *
     * Pass NULL as taskHandle to register the calling task.  The task must
     * call Feed() itself within the configured timeout or the
     * watchdog will trip. Automatic feeding supervises a separate SDK user;
     * it does not feed registered tasks.
     *
     * @param self       Pointer to the IWatchdog instance.
     * @param taskHandle FreeRTOS TaskHandle_t or NULL for the current task.
     * @param timeoutMs  Reserved for future per-task timeout; pass 0.
     * @return           0 on success, negative error code on failure.
     */
    int32_t (*AddTask)(IWatchdog *self, void *taskHandle, uint32_t timeoutMs);

    /**
     * @brief Deregisters a previously registered task from watchdog supervision.
     *
     * @param self       Pointer to the IWatchdog instance.
     * @param taskHandle FreeRTOS TaskHandle_t. Must not be NULL.
     * @return           0 on success, negative error code on failure.
     */
    int32_t (*RemoveTask)(IWatchdog *self, void *taskHandle);

    /**
     * @brief Returns whether the watchdog is actively supervising at least
     *        one task.
     *
     * @param self Pointer to the IWatchdog instance.
     * @return     true if active, false otherwise.
     */
    bool (*IsActive)(IWatchdog *self);

    /**
     * @brief Returns the configured system-wide watchdog timeout in ms.
     *
     * @param self Pointer to the IWatchdog instance.
     * @return     Timeout duration in milliseconds, or 0 on error.
     */
    uint32_t (*GetTimeout)(IWatchdog *self);

    /**
     * @brief Copies the current operational statistics into *outStats.
     *
     * @param self     Pointer to the IWatchdog instance.
     * @param outStats Destination buffer; must not be NULL.
     * @return         0 on success, -1 on invalid parameter.
     */
    int32_t (*GetStats)(IWatchdog *self, WatchdogStats_t *outStats);

    /**
     * @brief Returns the number of tasks currently registered for supervision.
     *
     * Equivalent to stats.activeTaskCount but cheaper when only the count is
     * needed.
     *
     * @param self Pointer to the IWatchdog instance.
     * @return     Active task count, or 0 on error.
     */
    uint32_t (*GetRegisteredTaskCount)(IWatchdog *self);

    /**
     * @brief Executes the built-in self-test suite.
     *
     * Tests performed:
     *  - Lock / unlock round-trip.
     *  - Feed increments the totalFeeds counter (when started).
     *  - Stats consistency check.
     *
     * @param self Pointer to the IWatchdog instance.
     * @return     0 on success, negative error code on failure.
     */
    int32_t (*SelfTest)(IWatchdog *self);

    /**
     * @brief Dynamically replaces the active watchdog configuration.
     *
     * The new configuration is validated with WatchdogConfig_IsValid() before
     * being applied to the ESP-IDF TWDT via esp_task_wdt_reconfigure().  The
     * auto-feed timer is restarted with the new period when auto_feed_enabled
     * is true.
     *
     * @param self   Pointer to the IWatchdog instance.
     * @param config Pointer to the new WatchdogConfig; must pass validation.
     * @return       0 on success, -1 on invalid configuration or TWDT error.
     */
    int32_t (*UpdateConfig)(IWatchdog *self, const WatchdogConfig *config);

    /** Atomically changes only timeout and, if necessary, its warning threshold.
     * Other current settings are preserved; returns 0 on complete success. */
    int32_t (*UpdateTimeout)(IWatchdog *self, uint32_t timeoutMs);

    /** Checked manual feed, including SDK reset and warning-timer rearm.
     * Legacy Feed remains best effort. Returns 0 on complete success. */
    int32_t (*FeedChecked)(IWatchdog *self);
};

/* -----------------------------------------------------------------------
 * Singleton accessor
 * --------------------------------------------------------------------- */

/**
 * @brief Returns the global singleton Watchdog service instance.
 *
 * The returned pointer is valid for the lifetime of the application.  The
 * vtable is set up on the first call; subsequent calls return the same object.
 *
 * @return Pointer to the IWatchdog implementation.
 */
IWatchdog *Watchdog_GetModule(void);

#ifdef __cplusplus
}
#endif

#endif // IWATCHDOG_H
