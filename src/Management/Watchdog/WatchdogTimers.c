/** @file WatchdogTimers.c
 * @brief Owned timers with checked SDK operations and retryable cleanup.
 */
#include "Management/Watchdog/WatchdogTimers.h"
#include <stddef.h>

void WatchdogTimers_Init(WatchdogTimers_t *timers) {
    if (!timers) return;
    timers->autoFeedTimer = NULL;
    timers->warningTimer = NULL;
}

/* An expired or never-started timer is already stopped. */
static esp_err_t StopTimer(esp_timer_handle_t timer) {
    esp_err_t err = esp_timer_stop(timer);
    return err == ESP_ERR_INVALID_STATE ? ESP_OK : err;
}

static esp_err_t DeleteTimer(esp_timer_handle_t *timer) {
    if (!*timer) return ESP_OK;
    esp_err_t err = StopTimer(*timer);
    if (err != ESP_OK) return err;
    err = esp_timer_delete(*timer);
    if (err == ESP_OK) *timer = NULL;
    return err;
}

esp_err_t WatchdogTimers_StartAutoFeed(WatchdogTimers_t *timers, uint32_t intervalMs,
                                     WatchdogTimerCallback_t cb, void *arg) {
    if (!timers || !cb || intervalMs == 0) return ESP_ERR_INVALID_ARG;
    if (!timers->autoFeedTimer) {
        esp_timer_create_args_t args = {
            .callback = cb, .arg = arg, .dispatch_method = ESP_TIMER_TASK,
            .name = "wdt_auto_feed",
        };
        esp_err_t err = esp_timer_create(&args, &timers->autoFeedTimer);
        if (err != ESP_OK) return err;
    } else {
        esp_err_t err = StopTimer(timers->autoFeedTimer);
        if (err != ESP_OK) return err;
    }
    /* Retain the handle on failure so the owner can retry or clean it up. */
    return esp_timer_start_periodic(timers->autoFeedTimer, (uint64_t)intervalMs * 1000ULL);
}

esp_err_t WatchdogTimers_StopAutoFeed(WatchdogTimers_t *timers) {
    if (!timers) return ESP_ERR_INVALID_ARG;
    return DeleteTimer(&timers->autoFeedTimer);
}

esp_err_t WatchdogTimers_CreateWarningTimer(WatchdogTimers_t *timers,
                                           WatchdogTimerCallback_t cb, void *arg) {
    if (!timers || !cb) return ESP_ERR_INVALID_ARG;
    if (timers->warningTimer) return ESP_OK;
    esp_timer_create_args_t args = {
        .callback = cb, .arg = arg, .dispatch_method = ESP_TIMER_TASK,
        .name = "wdt_warning",
    };
    return esp_timer_create(&args, &timers->warningTimer);
}

esp_err_t WatchdogTimers_ArmWarningTimer(WatchdogTimers_t *timers, uint32_t thresholdMs) {
    if (!timers || !timers->warningTimer || !thresholdMs) return ESP_ERR_INVALID_ARG;
    esp_err_t err = StopTimer(timers->warningTimer);
    if (err != ESP_OK) return err;
    return esp_timer_start_once(timers->warningTimer, (uint64_t)thresholdMs * 1000ULL);
}

esp_err_t WatchdogTimers_StopWarningTimer(WatchdogTimers_t *timers) {
    if (!timers) return ESP_ERR_INVALID_ARG;
    return DeleteTimer(&timers->warningTimer);
}
