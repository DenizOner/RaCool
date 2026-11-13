/**
 * @file WatchdogTimers.h
 * @brief Auto-feed and Warning hardware timer management for Watchdog module.
 *
 * @author RaCool DeepMind Team
 * @date 2026-08-01
 */

#ifndef WATCHDOG_TIMERS_H
#define WATCHDOG_TIMERS_H

#include "esp_err.h"
#include <esp_timer.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*WatchdogTimerCallback_t)(void *user_data);

typedef struct {
    esp_timer_handle_t autoFeedTimer;
    esp_timer_handle_t warningTimer;
} WatchdogTimers_t;

/**
 * @brief Initializes timer handlers to NULL state.
 */
void WatchdogTimers_Init(WatchdogTimers_t *timers);

/**
 * @brief Creates and starts the auto-feed periodic timer.
 */
esp_err_t WatchdogTimers_StartAutoFeed(WatchdogTimers_t *timers, uint32_t intervalMs, WatchdogTimerCallback_t cb, void *arg);

/**
 * @brief Stops and deletes auto-feed timer.
 */
esp_err_t WatchdogTimers_StopAutoFeed(WatchdogTimers_t *timers);

/**
 * @brief Creates warning one-shot timer.
 */
esp_err_t WatchdogTimers_CreateWarningTimer(WatchdogTimers_t *timers, WatchdogTimerCallback_t cb, void *arg);

/**
 * @brief Arms/re-arms warning timer for given duration.
 */
esp_err_t WatchdogTimers_ArmWarningTimer(WatchdogTimers_t *timers, uint32_t warningThresholdMs);

/**
 * @brief Stops and deletes warning timer.
 */
esp_err_t WatchdogTimers_StopWarningTimer(WatchdogTimers_t *timers);

#ifdef __cplusplus
}
#endif

#endif // WATCHDOG_TIMERS_H
