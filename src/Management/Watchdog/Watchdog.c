/**
 * @file Watchdog.c
 * @brief Thread-safe, Event-Driven Watchdog Service Implementation for RaCool System.
 *
 * Re-architected Facade incorporating modular TaskRegistry and Timers components.
 *
 * @author RaCool DeepMind Team
 * @date 2026-08-01
 */

#include "Core/DI/IDIRegistry.h"
#include "Core/EventBus/IEventBus.h"
#include "Core/ServiceLocator/ServiceLocator.h"
#include "Core/ServiceLocator/ServiceNames.h"
#include "Management/Watchdog/IWatchdog.h"
#include "Management/Watchdog/WatchdogConfig.h"
#include "Management/Watchdog/WatchdogTaskRegistry.h"
#include "Management/Watchdog/WatchdogTimers.h"
#include "esp_log.h"
#include <esp_task_wdt.h>
#include <esp_timer.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifndef RACOOL_TEST_NATIVE
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#else
#ifndef SemaphoreHandle_t
typedef void *SemaphoreHandle_t;
#endif
#ifndef xSemaphoreCreateMutex
#define xSemaphoreCreateMutex() ((SemaphoreHandle_t)1)
#endif
#ifndef vSemaphoreDelete
#define vSemaphoreDelete(s) ((void)(s))
#endif
#ifndef xSemaphoreTake
#define xSemaphoreTake(s, t) (1)
#endif
#ifndef xSemaphoreGive
#define xSemaphoreGive(s) (1)
#endif
#ifndef portMAX_DELAY
#define portMAX_DELAY 0xFFFFFFFFUL
#endif
#endif

static const char *TAG = "Watchdog";

#define LOGD(FUNC, ...) ESP_LOGD(TAG, "[" FUNC "] " __VA_ARGS__)
#define LOGI(FUNC, ...) ESP_LOGI(TAG, "[" FUNC "] " __VA_ARGS__)
#define LOGW(FUNC, ...) ESP_LOGW(TAG, "[" FUNC "] " __VA_ARGS__)
#define LOGE(FUNC, ...) ESP_LOGE(TAG, "[" FUNC "] " __VA_ARGS__)

typedef struct {
    IWatchdog             base;
    ServiceState          state;
    WatchdogConfig        config;
    WatchdogStats_t       stats;
    bool                  isActive;
    bool                  warningTimerFailed;
    SemaphoreHandle_t     mutex;
    WatchdogTaskRegistry_t registry;
    WatchdogTimers_t      timers;
    bool autoFeedClosing;
    uint32_t autoFeedActive;
    uint64_t autoFeedLastMs;
    esp_task_wdt_user_handle_t autoFeedUser; /* Owned by this service, never by esp_timer task. */
} WatchdogServiceImpl;

static WatchdogServiceImpl watchdogService;
static portMUX_TYPE autoFeedLock = portMUX_INITIALIZER_UNLOCKED;

static bool Watchdog_Lock(void) {
    if (watchdogService.mutex == NULL) {
        return true;
    }
    return (xSemaphoreTake(watchdogService.mutex, portMAX_DELAY) == 1);
}

static void Watchdog_Unlock(void) {
    if (watchdogService.mutex != NULL) {
        xSemaphoreGive(watchdogService.mutex);
    }
}

static void Watchdog_PublishEvent(const char *topic, const void *data, size_t dataSize) {
    IEventBus *eventBus = (IEventBus *)ServiceLocator_GetService(SVC_NAME_EVENT_BUS);
    if (eventBus && eventBus->Publish) {
        BusEvent event = {
            .eventName = topic,
            .type      = EVENT_TYPE_INFO,
            .data      = (void *)data,
            .dataSize  = (uint32_t)dataSize,
        };
        eventBus->Publish(event);
    }
}

static void Watchdog_WarningTimerCallback(void *arg) {
    (void)arg;
    if (!watchdogService.mutex || xSemaphoreTake(watchdogService.mutex, 0) != 1) return;
    if (watchdogService.state != SERVICE_STATE_STARTED) {
        Watchdog_Unlock();
        return;
    }
    watchdogService.stats.totalWarnings++;
    uint64_t now_ms = (uint64_t)(esp_timer_get_time() / 1000);
    Watchdog_Unlock();

    LOGW("Watchdog_WarningTimer", "Feed threshold breached - warning event published");
    Watchdog_PublishEvent(EVENT_WATCHDOG_WARNING, &now_ms, sizeof(uint64_t));
}

/* SDK calls run outside the critical section. Admission reserves the user
 * until reset returns, so lifecycle operations cannot delete a borrowed handle. */
static void Watchdog_AutoFeedCallback(void *arg) {
    (void)arg;
    portENTER_CRITICAL(&autoFeedLock);
    if (watchdogService.autoFeedClosing || !watchdogService.autoFeedUser) {
        portEXIT_CRITICAL(&autoFeedLock);
        return;
    }
    esp_task_wdt_user_handle_t user = watchdogService.autoFeedUser;
    watchdogService.autoFeedActive++;
    portEXIT_CRITICAL(&autoFeedLock);

    esp_err_t err = esp_task_wdt_reset_user(user);
    uint64_t nowMs = (uint64_t)esp_timer_get_time() / 1000ULL;
    portENTER_CRITICAL(&autoFeedLock);
    watchdogService.autoFeedActive--;
    if (err == ESP_OK) {
        watchdogService.stats.totalAutoFeeds++;
        watchdogService.autoFeedLastMs = nowMs;
    }
    uint32_t count = watchdogService.stats.totalAutoFeeds;
    portEXIT_CRITICAL(&autoFeedLock);
    if (err != ESP_OK) {
        LOGE("Watchdog_AutoFeedCallback", "Automatic feed failed: 0x%x", err);
        return;
    }

    /* Statistics/warning maintenance may defer under contention, but the SDK
     * user has already been fed. Never block the shared timer dispatcher. */
    if (watchdogService.mutex && xSemaphoreTake(watchdogService.mutex, 0) == 1) {
        if (watchdogService.state == SERVICE_STATE_STARTED) {
            if (nowMs > watchdogService.stats.lastFeedTimeMs) watchdogService.stats.lastFeedTimeMs = nowMs;
            watchdogService.warningTimerFailed = WatchdogTimers_ArmWarningTimer(
                &watchdogService.timers, watchdogService.config.warning_threshold_ms) != ESP_OK;
        }
        Watchdog_Unlock();
    }
    Watchdog_PublishEvent(EVENT_WATCHDOG_AUTO_FEED, &count, sizeof(count));
}

/* The service mutex serializes owners. A failed reservation leaves admission
 * open so a retryable lifecycle failure does not itself starve the user. */
static bool Watchdog_CloseAutoFeedAdmission(void) {
    portENTER_CRITICAL(&autoFeedLock);
    bool idle = watchdogService.autoFeedActive == 0;
    if (idle) watchdogService.autoFeedClosing = true;
    portEXIT_CRITICAL(&autoFeedLock);
    return idle;
}
static void Watchdog_OpenAutoFeedAdmission(void) {
    portENTER_CRITICAL(&autoFeedLock);
    watchdogService.autoFeedClosing = false;
    portEXIT_CRITICAL(&autoFeedLock);
}

static esp_err_t Watchdog_StopAutomaticFeed(void) {
    if (!Watchdog_CloseAutoFeedAdmission()) return ESP_ERR_INVALID_STATE;
    if (watchdogService.autoFeedUser) {
        esp_err_t err = esp_task_wdt_delete_user(watchdogService.autoFeedUser);
        if (err != ESP_OK) {
            Watchdog_OpenAutoFeedAdmission();
            return err;
        }
        watchdogService.autoFeedUser = NULL;
    }
    return WatchdogTimers_StopAutoFeed(&watchdogService.timers);
}

static esp_err_t Watchdog_StartAutomaticFeed(uint32_t intervalMs) {
    if (!Watchdog_CloseAutoFeedAdmission()) return ESP_ERR_INVALID_STATE;
    esp_err_t err;
    if (!watchdogService.autoFeedUser) {
        err = esp_task_wdt_add_user("racool_auto_feed", &watchdogService.autoFeedUser);
    } else {
        err = esp_task_wdt_reset_user(watchdogService.autoFeedUser);
    }
    if (err != ESP_OK) {
        Watchdog_OpenAutoFeedAdmission();
        return err;
    }
    err = WatchdogTimers_StartAutoFeed(&watchdogService.timers, intervalMs, Watchdog_AutoFeedCallback, NULL);
    if (err != ESP_OK) {
        esp_err_t cleanup = Watchdog_StopAutomaticFeed();
        if (cleanup != ESP_OK) LOGE("Watchdog_StartAutomaticFeed", "User cleanup failed: 0x%x", cleanup);
    } else Watchdog_OpenAutoFeedAdmission();
    return err;
}
static esp_err_t Watchdog_ConfigureSDK(const WatchdogConfig *config) {
    esp_task_wdt_config_t cfg = {
        .timeout_ms = config->timeout_ms,
        .idle_core_mask = 1ULL,
        .trigger_panic = config->reset_behavior == WATCHDOG_RESET_SYSTEM || config->panic_on_timeout,
    };
    return esp_task_wdt_reconfigure(&cfg);
}

static int32_t Watchdog_Initialize(IService *self, IDIRegistry *registry) {
    (void)self;
    (void)registry;

    if (watchdogService.state >= SERVICE_STATE_INITIALIZED) {
        return 0;
    }

    LOGI("Watchdog_Initialize", "Initializing Watchdog service facade");
    uint64_t start_us = (uint64_t)esp_timer_get_time();

    if (watchdogService.mutex == NULL) {
        watchdogService.mutex = xSemaphoreCreateMutex();
        if (watchdogService.mutex == NULL) {
            LOGE("Watchdog_Initialize", "Failed to create mutex");
            return -1;
        }
    }

    if (!Watchdog_Lock()) return -1;

    watchdogService.config = WatchdogConfig_GetDefault();
    memset(&watchdogService.stats, 0, sizeof(WatchdogStats_t));
    WatchdogTaskRegistry_Init(&watchdogService.registry, watchdogService.config.max_tasks);
    WatchdogTimers_Init(&watchdogService.timers);

    bool trigger_panic = (watchdogService.config.reset_behavior == WATCHDOG_RESET_SYSTEM)
                             ? true
                             : watchdogService.config.panic_on_timeout;

    esp_task_wdt_config_t wdt_cfg = {
        .timeout_ms     = watchdogService.config.timeout_ms,
        .idle_core_mask = (1ULL << 0),
        .trigger_panic  = trigger_panic,
    };

    esp_err_t err = esp_task_wdt_reconfigure(&wdt_cfg);
    if (err == ESP_ERR_NOT_FOUND) {
        err = esp_task_wdt_init(&wdt_cfg);
    }

    if (err != ESP_OK) {
        LOGE("Watchdog_Initialize", "Failed to init/reconfigure TWDT: 0x%x", err);
        Watchdog_Unlock();
        return -1;
    }

    watchdogService.state    = SERVICE_STATE_INITIALIZED;
    watchdogService.isActive = false;

    Watchdog_Unlock();

    uint64_t duration_ms = ((uint64_t)esp_timer_get_time() - start_us) / 1000ULL;
    LOGI("Watchdog_Initialize", "Watchdog initialized in %llu ms", duration_ms);

    Watchdog_PublishEvent(EVENT_WATCHDOG_STATE_CHANGED, &watchdogService.state, sizeof(ServiceState));
    return 0;
}

static int32_t Watchdog_Start(IService *self) {
    (void)self;
    if (!Watchdog_Lock()) return -1;

    if (watchdogService.state == SERVICE_STATE_STARTED) {
        Watchdog_Unlock();
        return 0;
    }

    if (watchdogService.state != SERVICE_STATE_INITIALIZED &&
        watchdogService.state != SERVICE_STATE_STOPPED) {
        Watchdog_Unlock();
        return -1;
    }

    LOGI("Watchdog_Start", "Starting Watchdog service");

    esp_err_t err = Watchdog_ConfigureSDK(&watchdogService.config);
    if (err == ESP_OK)
        err = WatchdogTimers_CreateWarningTimer(&watchdogService.timers, Watchdog_WarningTimerCallback, NULL);
    if (err == ESP_OK && watchdogService.config.auto_feed_enabled)
        err = Watchdog_StartAutomaticFeed(watchdogService.config.warning_threshold_ms);
    if (err != ESP_OK) {
        if (WatchdogTimers_StopWarningTimer(&watchdogService.timers) != ESP_OK)
            LOGE("Watchdog_Start", "Warning timer cleanup failed; handle retained");
        Watchdog_Unlock();
        return -1;
    }

    watchdogService.warningTimerFailed = false;
    watchdogService.state = SERVICE_STATE_STARTED;
    watchdogService.isActive = WatchdogTaskRegistry_GetCount(&watchdogService.registry) > 0;
    Watchdog_Unlock();

    Watchdog_PublishEvent(EVENT_WATCHDOG_STATE_CHANGED, &watchdogService.state, sizeof(ServiceState));
    return 0;
}

static int32_t Watchdog_Stop(IService *self) {
    (void)self;
    if (!Watchdog_Lock()) return -1;

    if (watchdogService.state != SERVICE_STATE_STARTED &&
        watchdogService.state != SERVICE_STATE_ERROR && !watchdogService.autoFeedUser &&
        !watchdogService.timers.autoFeedTimer && !watchdogService.timers.warningTimer) {
        Watchdog_Unlock();
        return -1;
    }

    LOGI("Watchdog_Stop", "Stopping Watchdog service");

    if (Watchdog_StopAutomaticFeed() != ESP_OK) {
        if (!watchdogService.autoFeedUser) {
            watchdogService.state = SERVICE_STATE_ERROR;
            watchdogService.isActive = false;
        }
        Watchdog_Unlock();
        return -1;
    }
    if (WatchdogTimers_StopWarningTimer(&watchdogService.timers) != ESP_OK) {
        watchdogService.state = SERVICE_STATE_ERROR;
        watchdogService.isActive = false;
        Watchdog_Unlock();
        return -1;
    }

    watchdogService.state    = SERVICE_STATE_STOPPED;
    watchdogService.isActive = false;
    Watchdog_Unlock();

    Watchdog_PublishEvent(EVENT_WATCHDOG_STATE_CHANGED, &watchdogService.state, sizeof(ServiceState));
    return 0;
}

static ServiceState Watchdog_GetState(IService *self) {
    (void)self;
    if (!Watchdog_Lock()) return SERVICE_STATE_UNINITIALIZED;
    ServiceState state = watchdogService.state;
    Watchdog_Unlock();
    return state;
}

static int32_t Watchdog_CheckHealth(IService *self) {
    (void)self;
    if (!Watchdog_Lock()) return -1;

    if (watchdogService.state != SERVICE_STATE_STARTED) {
        Watchdog_Unlock();
        return -1;
    }

    if (watchdogService.warningTimerFailed) { Watchdog_Unlock(); return -3; }

    if (!watchdogService.config.auto_feed_enabled && watchdogService.stats.lastFeedTimeMs > 0) {
        uint64_t now_ms = (uint64_t)(esp_timer_get_time() / 1000);
        uint64_t age_ms = now_ms - watchdogService.stats.lastFeedTimeMs;
        if (age_ms > watchdogService.config.warning_threshold_ms) {
            Watchdog_Unlock();
            return -2;
        }
    }

    Watchdog_Unlock();
    return 0;
}

static int32_t Watchdog_FeedChecked(IWatchdog *self) {
    if (self != &watchdogService.base) return -1;
    if (!Watchdog_Lock()) return -1;

    if (watchdogService.state != SERVICE_STATE_STARTED) {
        Watchdog_Unlock();
        return -1;
    }

    esp_err_t err = esp_task_wdt_reset();
    if (err != ESP_OK) {
        LOGE("Watchdog_Feed", "esp_task_wdt_reset failed: 0x%x", err);
        Watchdog_Unlock();
        return -1;
    }

    watchdogService.stats.totalFeeds++;
    watchdogService.stats.lastFeedTimeMs = (uint64_t)(esp_timer_get_time() / 1000);
    watchdogService.warningTimerFailed = WatchdogTimers_ArmWarningTimer(
        &watchdogService.timers, watchdogService.config.warning_threshold_ms) != ESP_OK;
    if (watchdogService.warningTimerFailed) LOGE("Watchdog_Feed", "Warning timer arm failed");

    int32_t result = watchdogService.warningTimerFailed ? -1 : 0;
    uint32_t feed_count = watchdogService.stats.totalFeeds;
    Watchdog_Unlock();

    Watchdog_PublishEvent(EVENT_WATCHDOG_FED, &feed_count, sizeof(uint32_t));
    return result;
}

static void Watchdog_Feed(IWatchdog *self) { (void)Watchdog_FeedChecked(self); }

static int32_t Watchdog_AddTask(IWatchdog *self, void *taskHandle, uint32_t timeoutMs) {
    (void)self;
    (void)timeoutMs;

    if (!Watchdog_Lock()) return -1;

    if (watchdogService.state != SERVICE_STATE_STARTED) {
        Watchdog_Unlock();
        return -1;
    }

    if (!taskHandle) taskHandle = (void *)xTaskGetCurrentTaskHandle();
    if (!taskHandle || WatchdogTaskRegistry_Add(&watchdogService.registry, taskHandle) != 0) {
        Watchdog_Unlock();
        return -1;
    }

    esp_err_t err = esp_task_wdt_add((TaskHandle_t)taskHandle);
    if (err != ESP_OK && err != ESP_ERR_INVALID_ARG) {
        WatchdogTaskRegistry_Remove(&watchdogService.registry, taskHandle);
        Watchdog_Unlock();
        return -1;
    }

    watchdogService.stats.totalTasksAdded++;
    watchdogService.stats.activeTaskCount = WatchdogTaskRegistry_GetCount(&watchdogService.registry);
    watchdogService.isActive              = (watchdogService.stats.activeTaskCount > 0);

    Watchdog_Unlock();
    Watchdog_PublishEvent(EVENT_WATCHDOG_TASK_ADDED, &taskHandle, sizeof(void *));
    return 0;
}

static int32_t Watchdog_RemoveTask(IWatchdog *self, void *taskHandle) {
    (void)self;
    if (!taskHandle) return -1;

    if (!Watchdog_Lock()) return -1;

    if (!WatchdogTaskRegistry_Contains(&watchdogService.registry, taskHandle)) {
        Watchdog_Unlock();
        return -1;
    }

    esp_err_t err = esp_task_wdt_delete((TaskHandle_t)taskHandle);
    if (err != ESP_OK) {
        Watchdog_Unlock();
        return -1;
    }

    /* Membership is stable under the service mutex; commit only after SDK success. */
    WatchdogTaskRegistry_Remove(&watchdogService.registry, taskHandle);
    watchdogService.stats.totalTasksRemoved++;
    watchdogService.stats.activeTaskCount = WatchdogTaskRegistry_GetCount(&watchdogService.registry);
    watchdogService.isActive              = (watchdogService.stats.activeTaskCount > 0);

    Watchdog_Unlock();
    Watchdog_PublishEvent(EVENT_WATCHDOG_TASK_REMOVED, &taskHandle, sizeof(void *));
    return 0;
}

static bool Watchdog_IsActive(IWatchdog *self) {
    (void)self;
    if (!Watchdog_Lock()) return false;
    bool active = watchdogService.isActive;
    Watchdog_Unlock();
    return active;
}

static uint32_t Watchdog_GetTimeout(IWatchdog *self) {
    (void)self;
    if (!Watchdog_Lock()) return 0;
    uint32_t timeout = watchdogService.config.timeout_ms;
    Watchdog_Unlock();
    return timeout;
}

static int32_t Watchdog_GetStats(IWatchdog *self, WatchdogStats_t *outStats) {
    (void)self;
    if (!outStats) return -1;
    if (!Watchdog_Lock()) return -1;
    portENTER_CRITICAL(&autoFeedLock);
    memcpy(outStats, &watchdogService.stats, sizeof(WatchdogStats_t));
    if (watchdogService.autoFeedLastMs > outStats->lastFeedTimeMs)
        outStats->lastFeedTimeMs = watchdogService.autoFeedLastMs;
    portEXIT_CRITICAL(&autoFeedLock);
    Watchdog_Unlock();
    return 0;
}

static uint32_t Watchdog_GetRegisteredTaskCount(IWatchdog *self) {
    (void)self;
    if (!Watchdog_Lock()) return 0;
    uint32_t count = WatchdogTaskRegistry_GetCount(&watchdogService.registry);
    Watchdog_Unlock();
    return count;
}

static int32_t Watchdog_UpdateConfigLocked(const WatchdogConfig *config) {
    if (config->max_tasks < WatchdogTaskRegistry_GetCount(&watchdogService.registry)) {
        return -1;
    }

    WatchdogConfig previous = watchdogService.config;
    esp_err_t err = Watchdog_ConfigureSDK(config);
    if (err != ESP_OK) { return -1; }

    if (watchdogService.state == SERVICE_STATE_STARTED) {
        err = config->auto_feed_enabled ? Watchdog_StartAutomaticFeed(config->warning_threshold_ms)
                                        : Watchdog_StopAutomaticFeed();
        if (err == ESP_OK)
            err = WatchdogTimers_ArmWarningTimer(&watchdogService.timers, config->warning_threshold_ms);
        if (err != ESP_OK) {
            /* Restore both the shared TWDT configuration and our owned timer/user. */
            esp_err_t sdkRollback = Watchdog_ConfigureSDK(&previous);
            esp_err_t feedRollback = previous.auto_feed_enabled
                ? Watchdog_StartAutomaticFeed(previous.warning_threshold_ms) : Watchdog_StopAutomaticFeed();
            esp_err_t warningRollback = WatchdogTimers_ArmWarningTimer(
                &watchdogService.timers, previous.warning_threshold_ms);
            if (sdkRollback != ESP_OK || feedRollback != ESP_OK || warningRollback != ESP_OK) {
                watchdogService.state = SERVICE_STATE_ERROR;
                watchdogService.isActive = false;
                LOGE("Watchdog_UpdateConfig", "Rollback failed: sdk=0x%x feed=0x%x warning=0x%x",
                     sdkRollback, feedRollback, warningRollback);
            }
            return -1;
        }
        watchdogService.warningTimerFailed = false;
    }
    watchdogService.config = *config;
    watchdogService.registry.maxTasks = config->max_tasks;

    return 0;
}

static int32_t Watchdog_UpdateConfig(IWatchdog *self, const WatchdogConfig *config) {
    if (self != &watchdogService.base || !WatchdogConfig_IsValid(config) || !Watchdog_Lock()) return -1;
    int32_t result = Watchdog_UpdateConfigLocked(config);
    Watchdog_Unlock();
    return result;
}
static int32_t Watchdog_UpdateTimeout(IWatchdog *self, uint32_t timeoutMs) {
    if (self != &watchdogService.base || timeoutMs < WATCHDOG_MIN_TIMEOUT_MS ||
        timeoutMs > WATCHDOG_MAX_TIMEOUT_MS || !Watchdog_Lock()) return -1;
    WatchdogConfig candidate = watchdogService.config;
    candidate.timeout_ms = timeoutMs;
    if (candidate.warning_threshold_ms >= timeoutMs) candidate.warning_threshold_ms = timeoutMs / 2u;
    int32_t result = WatchdogConfig_IsValid(&candidate) ? Watchdog_UpdateConfigLocked(&candidate) : -1;
    Watchdog_Unlock();
    return result;
}

static int32_t Watchdog_SelfTest(IWatchdog *self) {
    if (self != &watchdogService.base || !self->FeedChecked || !self->GetStats || !self->GetRegisteredTaskCount) return -1;
    LOGI("Watchdog_SelfTest", "Executing Watchdog self-test");
    if (!Watchdog_Lock()) return -1;

    if (watchdogService.state < SERVICE_STATE_INITIALIZED || watchdogService.state == SERVICE_STATE_ERROR) {
        Watchdog_Unlock();
        return -1;
    }

    uint32_t feeds_before = watchdogService.stats.totalFeeds;
    bool started = watchdogService.state == SERVICE_STATE_STARTED;
    Watchdog_Unlock();

    if (started) {
        if (self->FeedChecked(self) != 0) return -2;
        WatchdogStats_t stats_after;
        if (self->GetStats(self, &stats_after) != 0 || stats_after.totalFeeds == feeds_before) {
            return -2;
        }
    }

    WatchdogStats_t final_stats;
    if (self->GetStats(self, &final_stats) != 0) return -3;
    {
        uint32_t reported_count = self->GetRegisteredTaskCount(self);
        if (reported_count != final_stats.activeTaskCount) {
            return -4;
        }
    }

    LOGI("Watchdog_SelfTest", "Self-test PASSED");
    return 0;
}

IWatchdog *Watchdog_GetModule(void) {
    static bool initialized = false;
    if (!initialized) {
        watchdogService.base.base.Initialize      = Watchdog_Initialize;
        watchdogService.base.base.Start           = Watchdog_Start;
        watchdogService.base.base.Stop            = Watchdog_Stop;
        watchdogService.base.base.GetState         = Watchdog_GetState;
        watchdogService.base.base.CheckHealth      = Watchdog_CheckHealth;

        watchdogService.base.Feed                  = Watchdog_Feed;
        watchdogService.base.AddTask               = Watchdog_AddTask;
        watchdogService.base.RemoveTask            = Watchdog_RemoveTask;
        watchdogService.base.IsActive              = Watchdog_IsActive;
        watchdogService.base.GetTimeout            = Watchdog_GetTimeout;
        watchdogService.base.GetStats              = Watchdog_GetStats;
        watchdogService.base.GetRegisteredTaskCount= Watchdog_GetRegisteredTaskCount;
        watchdogService.base.SelfTest              = Watchdog_SelfTest;
        watchdogService.base.UpdateConfig          = Watchdog_UpdateConfig;
        watchdogService.base.UpdateTimeout         = Watchdog_UpdateTimeout;
        watchdogService.base.FeedChecked           = Watchdog_FeedChecked;

        watchdogService.state                      = SERVICE_STATE_UNINITIALIZED;
        watchdogService.config                     = WatchdogConfig_GetDefault();
        memset(&watchdogService.stats, 0, sizeof(WatchdogStats_t));
        WatchdogTaskRegistry_Init(&watchdogService.registry, watchdogService.config.max_tasks);
        WatchdogTimers_Init(&watchdogService.timers);
        watchdogService.isActive                   = false;
        watchdogService.mutex                      = NULL;

        initialized = true;
    }
    return &watchdogService.base;
}
