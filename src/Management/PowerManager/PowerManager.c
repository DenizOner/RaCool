#include "Management/PowerManager/IPowerManager.h"
#include "Management/PowerManager/PowerManagerConfig.h"
#include "Management/PowerManager/PowerManagerConsoleCommands.h"
#include "Management/ConfigurationManager/IConfigurationManager.h"
#include "Core/DI/IDIRegistry.h"
#include "Core/EventBus/IEventBus.h"
#include "Core/ServiceLocator/ServiceLocator.h"
#include "Core/ServiceLocator/ServiceNames.h"
#include "Hardware/PinManager/IPinManager.h"
#include "Hardware/OLED/IOLED.h"
#include "Hardware/Relay/IRelay.h"
#include "Hardware/RGBLED/IRGBLED.h"
#include "Hardware/BuiltinLED/IBuiltinLED.h"
#include "esp_log.h"
#include <esp_sleep.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>


#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#if __has_include("esp_pm.h")
#include "esp_pm.h"
#define HAS_ESP_PM 1
#endif

static const char *TAG = "PowerManager";

static bool Power_FactoryResetBlocked(void) {
    IConfigurationManager *config = ConfigurationManager_GetInstance();
    return config && config->IsMutationAllowed && !config->IsMutationAllowed(config);
}

#define LOGD(FUNC, ...) ESP_LOGD(TAG, "[" FUNC "] " __VA_ARGS__)
#define LOGI(FUNC, ...) ESP_LOGI(TAG, "[" FUNC "] " __VA_ARGS__)
#define LOGW(FUNC, ...) ESP_LOGW(TAG, "[" FUNC "] " __VA_ARGS__)
#define LOGE(FUNC, ...) ESP_LOGE(TAG, "[" FUNC "] " __VA_ARGS__)

#if defined(HAS_ESP_PM) && (defined(CONFIG_PM_ENABLE) || defined(RACOOL_TEST_NATIVE))
#define POWER_PM_SUPPORTED 1
typedef struct {
    char name[POWER_LOCK_NAME_CAPACITY];
    PowerLockKind kind;
    uint32_t references;
    esp_pm_lock_handle_t handle;
} PowerLockEntry;
#endif

typedef struct {
    IPowerManager base;
    _Atomic(ServiceState) state;
    esp_timer_handle_t restartTimer;
    atomic_bool restartPending;
    atomic_bool sleepPending;
    bool sleepTimerOwned;
    bool sleepGpioOwned;
    PowerManagerConfig config;
    PowerMode_t currentMode;
    PowerResetReason_t resetReason;
    PowerWakeupSource_t wakeCause;
    PowerStats_t stats;
    uint32_t activeLockCount;
    SemaphoreHandle_t mutex;
#ifdef POWER_PM_SUPPORTED
    PowerLockEntry locks[POWER_MAX_LOCK_OWNERS];
#endif
} PowerManagerImpl;

static PowerManagerImpl powerManager;

static bool Power_IsTransitionPending(IPowerManager *self) {
    return self != &powerManager.base || powerManager.state != SERVICE_STATE_STARTED ||
        atomic_load(&powerManager.restartPending) ||
        atomic_load(&powerManager.sleepPending);
}

static bool Power_LockMutex(void) {
    if (!powerManager.mutex) return false;
    TickType_t ticks = pdMS_TO_TICKS(POWER_MUTEX_TIMEOUT_MS);
    return xSemaphoreTake(powerManager.mutex, ticks ? ticks : 1) == pdTRUE;
}

static void Power_UnlockMutex(void) {
    if (xSemaphoreGive(powerManager.mutex) != pdTRUE)
        LOGE("Power_UnlockMutex", "Mutex release failed");
}

/* Only remove the wake trigger configured by this module; keep ownership on failure. */
static esp_err_t CleanupOwnedGpioWake(void) {
    if (!powerManager.sleepGpioOwned) return ESP_OK;
    esp_err_t result = esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_GPIO);
    if (result == ESP_OK) powerManager.sleepGpioOwned = false;
    else LOGE("CleanupOwnedGpioWake", "GPIO wake cleanup failed: 0x%x", (unsigned)result);
    return result;
}

static PowerResetReason_t MapEspResetReason(esp_reset_reason_t reason) {
    switch (reason) {
        case ESP_RST_POWERON:   return POWER_RESET_POWERON;
        case ESP_RST_EXT:       return POWER_RESET_EXT;
        case ESP_RST_SW:        return POWER_RESET_SW;
        case ESP_RST_PANIC:     return POWER_RESET_PANIC;
        case ESP_RST_INT_WDT:   return POWER_RESET_INT_WDT;
        case ESP_RST_TASK_WDT:  return POWER_RESET_TASK_WDT;
        case ESP_RST_WDT:       return POWER_RESET_WDT;
        case ESP_RST_DEEPSLEEP: return POWER_RESET_DEEPSLEEP;
        case ESP_RST_BROWNOUT:  return POWER_RESET_BROWNOUT;
        case ESP_RST_SDIO:      return POWER_RESET_SDIO;
        default:                return POWER_RESET_UNKNOWN;
    }
}

static const char *GetResetReasonStr(PowerResetReason_t reason) {
    switch (reason) {
        case POWER_RESET_POWERON:   return "Power On Reset";
        case POWER_RESET_EXT:       return "External Pin Reset";
        case POWER_RESET_SW:        return "Software Reset";
        case POWER_RESET_PANIC:     return "System Panic / Exception Reset";
        case POWER_RESET_INT_WDT:   return "Interrupt Watchdog Reset";
        case POWER_RESET_TASK_WDT:  return "Task Watchdog Reset";
        case POWER_RESET_WDT:       return "Other Watchdog Reset";
        case POWER_RESET_DEEPSLEEP: return "Deep Sleep Reset";
        case POWER_RESET_BROWNOUT:  return "Brownout Reset";
        case POWER_RESET_SDIO:      return "SDIO Reset";
        default:                    return "Unknown Reset";
    }
}

static PowerWakeupSource_t MapEspWakeupCause(esp_sleep_source_t cause) {
    switch (cause) {
        case ESP_SLEEP_WAKEUP_UNDEFINED: return POWER_WAKEUP_UNDEFINED;
        case ESP_SLEEP_WAKEUP_ALL:       return POWER_WAKEUP_ALL;
        case ESP_SLEEP_WAKEUP_EXT0:      return POWER_WAKEUP_EXT0;
        case ESP_SLEEP_WAKEUP_EXT1:      return POWER_WAKEUP_EXT1;
        case ESP_SLEEP_WAKEUP_TIMER:     return POWER_WAKEUP_TIMER;
        case ESP_SLEEP_WAKEUP_TOUCHPAD:  return POWER_WAKEUP_TOUCHPAD;
        case ESP_SLEEP_WAKEUP_ULP:       return POWER_WAKEUP_ULP;
        case ESP_SLEEP_WAKEUP_GPIO:      return POWER_WAKEUP_GPIO;
        case ESP_SLEEP_WAKEUP_UART:      return POWER_WAKEUP_UART;
        case ESP_SLEEP_WAKEUP_WIFI:      return POWER_WAKEUP_WIFI;
        case ESP_SLEEP_WAKEUP_COCPU:     return POWER_WAKEUP_COCPU;
        default:                         return POWER_WAKEUP_UNDEFINED;
    }
}

static void PublishEvent(const char *event_name, int32_t val) {
    IEventBus *bus = (IEventBus *)ServiceLocator_GetService(SVC_NAME_EVENT_BUS);
    if (bus && bus->Publish) {
        BusEvent event = {
            .eventName = event_name,
            .type = EVENT_TYPE_INFO,
            .data = &val,
            .dataSize = sizeof(val)
        };
        int32_t result=bus->Publish(event);
        if (result!=0) LOGE("PublishEvent","Power event rejected: %s (%ld)",event_name,(long)result);
    }
}


/* Caller owns the power mutex; ConfigurationManager never calls back while locked. */
static int32_t Power_ApplyProfileLocked(PowerMode_t mode, bool persist) {
#ifdef POWER_PM_SUPPORTED
    PowerModeProfile profile;
    if (!PowerManagerConfig_GetProfile((uint8_t)mode, &profile)) return POWER_ERR_INVALID_ARG;
    IConfigurationManager *cm = (IConfigurationManager *)ServiceLocator_GetService(SVC_NAME_CONFIGURATION_MANAGER);
    if (persist && (!cm || !cm->UpdateFields)) return POWER_ERR_INVALID_STATE;
    esp_pm_config_t previous;
    if (esp_pm_get_configuration(&previous) != ESP_OK) return POWER_ERR_INVALID_STATE;
    const esp_pm_config_t desired = {
        .min_freq_mhz = profile.minFrequencyMHz,
        .max_freq_mhz = profile.maxFrequencyMHz,
        .light_sleep_enable = false,
    };
    esp_err_t result = esp_pm_configure(&desired);
    if (result != ESP_OK)
        return result == ESP_ERR_NOT_SUPPORTED ? POWER_ERR_NOT_SUPPORTED : POWER_ERR_INVALID_STATE;
    if (persist) {
        uint8_t saved = (uint8_t)mode;
        ConfigurationFieldUpdate update = {offsetof(SystemBaseConfigs, power_profile), &saved, sizeof(saved)};
        result = cm->UpdateFields(cm, "system", &update, 1, true);
        if (result != ESP_OK) {
            if (esp_pm_configure(&previous) != ESP_OK) {
                powerManager.state = SERVICE_STATE_ERROR;
                powerManager.currentMode = POWER_MODE_UNKNOWN;
                LOGE("Power_ApplyProfileLocked", "Persistence and SDK rollback failed");
            }
            return POWER_ERR_INVALID_STATE;
        }
    }
    powerManager.currentMode = mode;
    powerManager.stats.currentMode = mode;
    return POWER_OK;
#else
    (void)mode; (void)persist;
    return POWER_ERR_NOT_SUPPORTED;
#endif
}

static int32_t Power_Start(IService *self) {
    LOGI("Power_Start", "Starting power manager service");
    esp_err_t registration_result = PowerManager_RegisterConsoleCommands();
    if (registration_result != ESP_OK) return registration_result;
    if (!Power_LockMutex()) { return POWER_ERR_LOCK_FAILED; }

    if (powerManager.state != SERVICE_STATE_INITIALIZED) {
        LOGE("Power_Start", "Failed to start: not initialized, state=%d", powerManager.state);
        Power_UnlockMutex();
        return -1;
    }

    IConfigurationManager *cm = (IConfigurationManager *)ServiceLocator_GetService(SVC_NAME_CONFIGURATION_MANAGER);
    SystemSettings snapshot;
    if (!cm || !cm->GetSnapshot || cm->GetSnapshot(cm, &snapshot) != ESP_OK) {
        Power_UnlockMutex();
        return POWER_ERR_INVALID_STATE;
    }
    int32_t result = Power_ApplyProfileLocked((PowerMode_t)snapshot.system.power_profile, false);
    if (result != POWER_OK) {
        Power_UnlockMutex();
        return result;
    }
    powerManager.state = SERVICE_STATE_STARTED;
    Power_UnlockMutex();

    PublishEvent(EVENT_POWER_STATE_CHANGED, (int32_t)SERVICE_STATE_STARTED);

    LOGI("Power_Start", "Power manager service started successfully");
    return 0;
}

static int32_t Power_Stop(IService *self) {
    if (Power_FactoryResetBlocked()) return POWER_ERR_BUSY;
    if (atomic_load(&powerManager.sleepPending)) return POWER_ERR_BUSY;
    LOGI("Power_Stop", "Stopping power manager service");
    if (!Power_LockMutex()) { return POWER_ERR_LOCK_FAILED; }
    if (Power_FactoryResetBlocked()) { Power_UnlockMutex(); return POWER_ERR_BUSY; }

    if (atomic_load(&powerManager.sleepPending)) { Power_UnlockMutex(); return POWER_ERR_BUSY; }
    if (powerManager.state != SERVICE_STATE_STARTED) {
        LOGW("Power_Stop", "Not started, state=%d", powerManager.state);
        Power_UnlockMutex();
        return -1;
    }

    if (powerManager.activeLockCount) { Power_UnlockMutex(); return POWER_ERR_BUSY; }
    if (CleanupOwnedGpioWake() != ESP_OK) { Power_UnlockMutex(); return POWER_ERR_INVALID_STATE; }
    if (powerManager.sleepTimerOwned) {
        if (esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER) != ESP_OK) {
            Power_UnlockMutex();
            return POWER_ERR_INVALID_STATE;
        }
        powerManager.sleepTimerOwned = false;
    }
    if (powerManager.restartTimer) {
        esp_err_t result = esp_timer_stop(powerManager.restartTimer);
        if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) {
            Power_UnlockMutex(); return POWER_ERR_TIMEOUT;
        }
        atomic_store(&powerManager.restartPending, false);
    }
#ifdef POWER_PM_SUPPORTED
    for (unsigned i = 0; i < POWER_MAX_LOCK_OWNERS; ++i) {
        PowerLockEntry *entry = &powerManager.locks[i];
        if (entry->handle && esp_pm_lock_delete(entry->handle) != ESP_OK) {
            Power_UnlockMutex();
            return POWER_ERR_LOCK_FAILED;
        }
        memset(entry, 0, sizeof(*entry));
    }
#endif
    powerManager.state = SERVICE_STATE_STOPPED;
    Power_UnlockMutex();

    PublishEvent(EVENT_POWER_STATE_CHANGED, (int32_t)SERVICE_STATE_STOPPED);
    LOGI("Power_Stop", "Power manager service stopped successfully");
    return 0;
}

static ServiceState Power_GetState(IService *self) {
    if (!Power_LockMutex()) { return SERVICE_STATE_ERROR; }
    ServiceState s = powerManager.state;
    Power_UnlockMutex();
    return s;
}

static int32_t Power_CheckHealth(IService *self) {
    if (!Power_LockMutex()) { return POWER_ERR_LOCK_FAILED; }
    int32_t health = (powerManager.state == SERVICE_STATE_STARTED) ? 0 : -1;
    Power_UnlockMutex();
    return health;
}

static void Power_Restart(IPowerManager *self) {
    if (Power_FactoryResetBlocked()) return;
    LOGW("Power_Restart", "System restart requested now");
    if (!Power_LockMutex()) { return; }
    if (Power_FactoryResetBlocked()) { Power_UnlockMutex(); return; }
    powerManager.stats.totalRestarts++;
    Power_UnlockMutex();

    PublishEvent(EVENT_POWER_RESTART_REQUEST, 0);

#ifndef RACOOL_TEST_NATIVE
    vTaskDelay(pdMS_TO_TICKS(100));
#endif
    esp_restart();
}

static void RestartTimerCallback(void *argument) {
    (void)argument;
    if (atomic_exchange(&powerManager.restartPending, false) &&
        powerManager.state == SERVICE_STATE_STARTED) esp_restart();
}

static int32_t Power_RestartAsync(IPowerManager *self, uint32_t delay_ms) {
    if (!self || delay_ms > POWER_MAX_RESTART_DELAY_MS) return POWER_ERR_INVALID_ARG;
    if (!Power_LockMutex()) { return POWER_ERR_LOCK_FAILED; }
    if (Power_FactoryResetBlocked()) { Power_UnlockMutex(); return POWER_ERR_BUSY; }
    if (powerManager.state != SERVICE_STATE_STARTED) {
        Power_UnlockMutex(); return POWER_ERR_INVALID_STATE;
    }
    if (atomic_load(&powerManager.restartPending)) {
        Power_UnlockMutex(); return POWER_ERR_BUSY;
    }
    if (!powerManager.restartTimer) {
        const esp_timer_create_args_t args = {
            .callback = RestartTimerCallback, .dispatch_method = ESP_TIMER_TASK, .name = "PowerRestart"
        };
        esp_timer_handle_t created = NULL;
        esp_err_t result = esp_timer_create(&args, &created);
        if (result != ESP_OK || !created) {
            Power_UnlockMutex();
            return result == ESP_ERR_NO_MEM ? POWER_ERR_NO_MEM : POWER_ERR_INVALID_STATE;
        }
        powerManager.restartTimer = created;
    }
    atomic_store(&powerManager.restartPending, true);
    esp_err_t result = esp_timer_start_once(powerManager.restartTimer,
        (uint64_t)(delay_ms ? delay_ms : 1u) * 1000u);
    if (result != ESP_OK) {
        atomic_store(&powerManager.restartPending, false);
        Power_UnlockMutex(); return POWER_ERR_TIMEOUT;
    }
    if (powerManager.stats.totalRestarts < UINT32_MAX) powerManager.stats.totalRestarts++;
    Power_UnlockMutex();
    PublishEvent(EVENT_POWER_RESTART_REQUEST, (int32_t)delay_ms);
    return POWER_OK;
}

static esp_err_t ConfigureWakeupGPIOs(void) {
    if (!powerManager.config.enableRotaryWakeup) return ESP_OK;
    IPinManager *pins = (IPinManager *)ServiceLocator_GetService(SVC_NAME_PIN_MANAGER);
    if (!pins || !pins->GetPin) return ESP_ERR_INVALID_STATE;
    gpio_num_t pin = pins->GetPin(pins, PIN_FUNC_ROTARY_SW);
    if (pin < 0 || pin >= 64 || !esp_sleep_is_valid_wakeup_gpio(pin)) return ESP_ERR_INVALID_ARG;
    /* IDF 5.3.1 installs the GPIO trigger even if the low-level enable reports failure. */
    powerManager.sleepGpioOwned = true;
    esp_err_t result = esp_deep_sleep_enable_gpio_wakeup(1ULL << pin,
        (esp_deepsleep_gpio_wake_up_mode_t)powerManager.config.rotaryWakeupLevel);
    return result;
}

/* Stop producers before actuators so queued events cannot turn outputs back on. */
static bool StopSleepService(const char *name, IService **stopped) {
    *stopped = NULL;
    IService *service = (IService *)ServiceLocator_GetService(name);
    if (!service) return true;
    if (!service->GetState) {
        LOGE("StopSleepService", "Missing lifecycle state: %s", name);
        return false;
    }
    ServiceState state = service->GetState(service);
    if (state == SERVICE_STATE_UNINITIALIZED || state == SERVICE_STATE_INITIALIZED ||
        state == SERVICE_STATE_STOPPED) return true;
    if (state != SERVICE_STATE_STARTED) {
        /* ERROR may still own a worker after a failed cooperative shutdown. */
        LOGE("StopSleepService", "Unsafe lifecycle state: %s (%d)", name, (int)state);
        return false;
    }
    if (!service->Start || !service->Stop) {
        LOGE("StopSleepService","Missing lifecycle method: %s",name);return false;
    }
    int32_t result=service->Stop(service);
    if (result!=0) {
        LOGE("StopSleepService","Failed to stop %s (%ld)",name,(long)result);return false;
    }
    *stopped = service;
    return true;
}

static bool RestoreSleepServices(IService **services,const char *const *names,size_t count) {
    bool restored=true;
    while (count) {
        IService *service=services[--count];
        if (!service) continue;
        int32_t result=service->Start ? service->Start(service) : POWER_ERR_INVALID_STATE;
        if (result!=0) {
            LOGE("RestoreSleepServices","Failed to restart %s (%ld)",names[count],(long)result);
            restored=false;
        }
    }
    return restored;
}

static void SleepPreparationDelay(void) {
#ifndef RACOOL_TEST_NATIVE
    if (powerManager.config.preSleepDelayMs) {
        TickType_t ticks = pdMS_TO_TICKS(powerManager.config.preSleepDelayMs);
        vTaskDelay(ticks ? ticks : 1);
    }
#endif
}

static int32_t SleepDurationMilliseconds(uint64_t duration_us) {
    uint64_t milliseconds = duration_us / 1000u;
    return milliseconds > INT32_MAX ? INT32_MAX : (int32_t)milliseconds;
}

static int32_t AdmitSleep(void) {
    if (Power_FactoryResetBlocked()) return POWER_ERR_BUSY;
    if (atomic_exchange(&powerManager.sleepPending, true)) return POWER_ERR_BUSY;
    if (!Power_LockMutex()) { atomic_store(&powerManager.sleepPending, false); return POWER_ERR_LOCK_FAILED; }
    if (Power_FactoryResetBlocked()) {
        Power_UnlockMutex(); atomic_store(&powerManager.sleepPending, false); return POWER_ERR_BUSY;
    }
    bool ready = powerManager.state == SERVICE_STATE_STARTED || powerManager.state == SERVICE_STATE_INITIALIZED;
    Power_UnlockMutex();
    if (!ready) {
        atomic_store(&powerManager.sleepPending, false);
        return POWER_ERR_INVALID_STATE;
    }
    if (powerManager.sleepTimerOwned) {
        if (esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER) != ESP_OK) {
            atomic_store(&powerManager.sleepPending, false);
            return POWER_ERR_INVALID_STATE;
        }
        powerManager.sleepTimerOwned = false;
    }
    if (CleanupOwnedGpioWake() != ESP_OK) {
        atomic_store(&powerManager.sleepPending, false);
        return POWER_ERR_INVALID_STATE;
    }
    return POWER_OK;
}

static int32_t Power_EnterDeepSleep(IPowerManager *self, uint64_t duration_us) {
    LOGI("Power_EnterDeepSleep", "Entering deep sleep, duration=%llu us", (unsigned long long)duration_us);

    if (!self || duration_us > POWER_SLEEP_MAX_DURATION_US) return POWER_ERR_INVALID_ARG;
    int32_t admission = AdmitSleep();
    if (admission != POWER_OK) {
        LOGE("Power_EnterDeepSleep", "Sleep admission rejected");
        return admission;
    }
    esp_err_t wake_err = ConfigureWakeupGPIOs();
    if (wake_err != ESP_OK) {
        LOGE("Power_EnterDeepSleep", "Wakeup configuration failed: 0x%x", (unsigned)wake_err);
        CleanupOwnedGpioWake();
        atomic_store(&powerManager.sleepPending, false);
        return POWER_ERR_INVALID_STATE;
    }
    PublishEvent(EVENT_POWER_PREPARE_SLEEP, SleepDurationMilliseconds(duration_us));

    SleepPreparationDelay();

    const char *names[] = {SVC_NAME_UI_CONTROLLER, SVC_NAME_CLIMATE_CONTROLLER, SVC_NAME_RELAY,
                           SVC_NAME_RGB_LED, SVC_NAME_BUILTIN_LED};
    IService *services[sizeof(names) / sizeof(names[0])] = {0};
    bool stopped = true;
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
        stopped &= StopSleepService(names[i], &services[i]);
    if (!stopped) {
        bool restored=RestoreSleepServices(services,names,sizeof(services)/sizeof(services[0]));
        esp_err_t cleanup = CleanupOwnedGpioWake();
        atomic_store(&powerManager.sleepPending, false);
        return restored && cleanup == ESP_OK ? POWER_ERR_TIMEOUT : POWER_ERR_INVALID_STATE;
    }

    IOLED *oled = (IOLED *)ServiceLocator_GetService(SVC_NAME_OLED);
    if (oled && oled->SetPower) {
        if (oled->SetPower(oled, false) != ESP_OK) {
            LOGE("Power_EnterDeepSleep", "OLED power-off failed; sleep aborted");
            if (oled->SetPower(oled, true) != ESP_OK) LOGE("Power_EnterDeepSleep", "OLED restoration failed");
            RestoreSleepServices(services,names,sizeof(services)/sizeof(services[0]));
            CleanupOwnedGpioWake();
            atomic_store(&powerManager.sleepPending, false);
            return POWER_ERR_INVALID_STATE;
        }
    }

    PublishEvent(EVENT_POWER_ENTERING_SLEEP, 1);
    if (!Power_LockMutex()) {
        bool oledRestored = !oled || !oled->SetPower || oled->SetPower(oled, true) == ESP_OK;
        if (!oledRestored)
            LOGE("Power_EnterDeepSleep", "OLED restoration failed after mutex timeout");
        bool restored=RestoreSleepServices(services,names,sizeof(services)/sizeof(services[0]));
        esp_err_t cleanup = CleanupOwnedGpioWake();
        atomic_store(&powerManager.sleepPending, false);
        return restored && oledRestored && cleanup == ESP_OK ? POWER_ERR_LOCK_FAILED : POWER_ERR_INVALID_STATE;
    }
    if (powerManager.stats.totalSleeps < UINT32_MAX) powerManager.stats.totalSleeps++;
    Power_UnlockMutex();
    if (duration_us > 0) {
        esp_deep_sleep(duration_us);
    } else {
        esp_deep_sleep_start();
    }
    /* The SDK does not return; native injected boundaries may return. */
    esp_err_t cleanup = CleanupOwnedGpioWake();
    atomic_store(&powerManager.sleepPending, false);
#ifdef RACOOL_TEST_NATIVE
    return cleanup == ESP_OK ? POWER_OK : POWER_ERR_INVALID_STATE;
#else
    (void)cleanup;
    return POWER_ERR_INVALID_STATE;
#endif
}

static int32_t Power_EnterLightSleep(IPowerManager *self, uint64_t duration_us) {
    if (!self || !duration_us || duration_us > POWER_SLEEP_MAX_DURATION_US) return POWER_ERR_INVALID_ARG;
    int32_t admission = AdmitSleep();
    if (admission != POWER_OK) return admission;
    LOGI("Power_EnterLightSleep", "Entering light sleep for %llu us", (unsigned long long)duration_us);

    PublishEvent(EVENT_POWER_PREPARE_SLEEP, SleepDurationMilliseconds(duration_us));

    SleepPreparationDelay();

    if (duration_us > 0) {
        if (esp_sleep_enable_timer_wakeup(duration_us) != ESP_OK) {
            atomic_store(&powerManager.sleepPending, false);
            return POWER_ERR_INVALID_STATE;
        }
        powerManager.sleepTimerOwned = true;
    }

    PublishEvent(EVENT_POWER_ENTERING_SLEEP, 0);
    esp_err_t err = esp_light_sleep_start();
    esp_err_t cleanup = esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
    if (cleanup == ESP_OK) powerManager.sleepTimerOwned = false;
    if (err != ESP_OK) {
        atomic_store(&powerManager.sleepPending, false);
        return POWER_ERR_INVALID_STATE;
    }

    if (!Power_LockMutex()) { atomic_store(&powerManager.sleepPending, false); return POWER_ERR_LOCK_FAILED; }
    if (powerManager.stats.totalSleeps < UINT32_MAX) powerManager.stats.totalSleeps++;
    if (powerManager.stats.totalWakeups < UINT32_MAX) powerManager.stats.totalWakeups++;
    powerManager.wakeCause = MapEspWakeupCause(esp_sleep_get_wakeup_cause());
    Power_UnlockMutex();

    PublishEvent(EVENT_POWER_WAKEUP, (int32_t)powerManager.wakeCause);
    LOGI("Power_EnterLightSleep", "Woke up from light sleep, cause=%d", powerManager.wakeCause);

    atomic_store(&powerManager.sleepPending, false);
    return cleanup == ESP_OK ? POWER_OK : POWER_ERR_INVALID_STATE;
}

static int32_t Power_SetPowerMode(IPowerManager *self, PowerMode_t mode) {
    PowerModeProfile profile;
    if (!self || mode < POWER_MODE_PERFORMANCE || mode > POWER_MODE_LOW_POWER ||
        !PowerManagerConfig_GetProfile((uint8_t)mode, &profile)) return POWER_ERR_INVALID_ARG;

#if defined(HAS_ESP_PM) && (defined(CONFIG_PM_ENABLE) || defined(RACOOL_TEST_NATIVE))
    if (!Power_LockMutex()) { return POWER_ERR_LOCK_FAILED; }
    if (Power_FactoryResetBlocked()) { Power_UnlockMutex(); return POWER_ERR_BUSY; }
    if (powerManager.state != SERVICE_STATE_INITIALIZED && powerManager.state != SERVICE_STATE_STARTED) {
        Power_UnlockMutex();
        return POWER_ERR_INVALID_STATE;
    }
    int32_t result = Power_ApplyProfileLocked(mode, true);
    if (result != POWER_OK) {
        Power_UnlockMutex();
        LOGE("Power_SetPowerMode", "Profile transaction failed: %ld", (long)result);
        return result;
    }
    Power_UnlockMutex();
    PublishEvent(EVENT_POWER_MODE_CHANGED, (int32_t)mode);
    LOGI("Power_SetPowerMode", "Applied profile %d (%d-%d MHz)", (int)mode,
         profile.minFrequencyMHz, profile.maxFrequencyMHz);
    return POWER_OK;
#else
    return POWER_ERR_NOT_SUPPORTED;
#endif
}

static PowerMode_t Power_GetPowerMode(IPowerManager *self) {
    if (!Power_LockMutex()) { return POWER_MODE_UNKNOWN; }
    PowerMode_t mode = powerManager.currentMode;
    Power_UnlockMutex();
    return mode;
}

static int32_t Power_GetLockSnapshot(IPowerManager *self, PowerLockSnapshot *entries,
                                     size_t capacity, size_t *count) {
    if (!self || !count || (!entries && capacity)) return POWER_ERR_INVALID_ARG;
    if (!Power_LockMutex()) { return POWER_ERR_LOCK_FAILED; }
    size_t required = 0;
#ifdef POWER_PM_SUPPORTED
    for (unsigned i = 0; i < POWER_MAX_LOCK_OWNERS; ++i)
        if (powerManager.locks[i].references) ++required;
#endif
    *count = required;
    if (capacity < required) { Power_UnlockMutex(); return POWER_ERR_NO_MEM; }
#ifdef POWER_PM_SUPPORTED
    size_t index = 0;
    for (unsigned i = 0; i < POWER_MAX_LOCK_OWNERS; ++i) {
        const PowerLockEntry *entry = &powerManager.locks[i];
        if (!entry->references) continue;
        memcpy(entries[index].name, entry->name, sizeof(entries[index].name));
        entries[index].kind = entry->kind;
        entries[index].references = entry->references;
        ++index;
    }
#endif
    Power_UnlockMutex();
    return POWER_OK;
}

static int32_t Power_AcquireTypedPowerLock(IPowerManager *self, const char *name, PowerLockKind kind) {
    if (!self || !name || !*name || strnlen(name, POWER_LOCK_NAME_CAPACITY) >= POWER_LOCK_NAME_CAPACITY ||
        kind < POWER_LOCK_CPU_MAX || kind > POWER_LOCK_NO_LIGHT_SLEEP) return POWER_ERR_INVALID_ARG;
#ifdef POWER_PM_SUPPORTED
    const esp_pm_lock_type_t sdkKinds[] = {ESP_PM_CPU_FREQ_MAX, ESP_PM_APB_FREQ_MAX, ESP_PM_NO_LIGHT_SLEEP};
    if (!Power_LockMutex()) { return POWER_ERR_LOCK_FAILED; }
    if (powerManager.state != SERVICE_STATE_INITIALIZED && powerManager.state != SERVICE_STATE_STARTED) {
        Power_UnlockMutex();
        return POWER_ERR_INVALID_STATE;
    }
    PowerLockEntry *entry = NULL, *empty = NULL;
    for (unsigned i = 0; i < POWER_MAX_LOCK_OWNERS; ++i) {
        if (!strcmp(powerManager.locks[i].name, name)) entry = &powerManager.locks[i];
        if (!powerManager.locks[i].name[0] && !empty) empty = &powerManager.locks[i];
    }
    if ((entry && entry->kind != kind) || powerManager.activeLockCount == UINT32_MAX) {
        Power_UnlockMutex();
        return entry && entry->kind != kind ? POWER_ERR_INVALID_ARG : POWER_ERR_BUSY;
    }
    if (!entry) {
        if (!empty) { Power_UnlockMutex(); return POWER_ERR_NO_MEM; }
        entry = empty;
        strcpy(entry->name, name);
        entry->kind = kind;
        esp_err_t result = esp_pm_lock_create(sdkKinds[kind], 0, entry->name, &entry->handle);
        if (result != ESP_OK) {
            memset(entry, 0, sizeof(*entry));
            Power_UnlockMutex();
            return POWER_ERR_LOCK_FAILED;
        }
    }
    if (entry->references == 0 && esp_pm_lock_acquire(entry->handle) != ESP_OK) {
        // Retain the inactive handle for retry/cleanup if deletion itself fails.
        if (esp_pm_lock_delete(entry->handle) == ESP_OK) memset(entry, 0, sizeof(*entry));
        Power_UnlockMutex();
        return POWER_ERR_LOCK_FAILED;
    }
    ++entry->references;
    ++powerManager.activeLockCount;
    powerManager.stats.activePowerLocks = powerManager.activeLockCount;
    Power_UnlockMutex();
    return POWER_OK;
#else
    return POWER_ERR_NOT_SUPPORTED;
#endif
}

static int32_t Power_AcquirePowerLock(IPowerManager *self, const char *name) {
    return Power_AcquireTypedPowerLock(self, name, POWER_LOCK_CPU_MAX);
}

static int32_t Power_ReleasePowerLock(IPowerManager *self, const char *name) {
    if (!self || !name || !*name || strnlen(name, POWER_LOCK_NAME_CAPACITY) >= POWER_LOCK_NAME_CAPACITY)
        return POWER_ERR_INVALID_ARG;
#ifdef POWER_PM_SUPPORTED
    if (!Power_LockMutex()) { return POWER_ERR_LOCK_FAILED; }
    PowerLockEntry *entry = NULL;
    for (unsigned i = 0; i < POWER_MAX_LOCK_OWNERS; ++i)
        if (!strcmp(powerManager.locks[i].name, name)) entry = &powerManager.locks[i];
    if (!entry) { Power_UnlockMutex(); return POWER_ERR_INVALID_ARG; }
    if (entry->references == 1 && esp_pm_lock_release(entry->handle) != ESP_OK) {
        Power_UnlockMutex();
        return POWER_ERR_LOCK_FAILED;
    }
    if (entry->references) {
        --entry->references;
        --powerManager.activeLockCount;
        powerManager.stats.activePowerLocks = powerManager.activeLockCount;
    }
    if (!entry->references) {
        if (esp_pm_lock_delete(entry->handle) != ESP_OK) {
            Power_UnlockMutex();
            return POWER_ERR_LOCK_FAILED;
        }
        memset(entry, 0, sizeof(*entry));
    }
    Power_UnlockMutex();
    return POWER_OK;
#else
    return POWER_ERR_NOT_SUPPORTED;
#endif
}

static int32_t Power_GetStats(IPowerManager *self, PowerStats_t *stats) {
    if (!self || !stats) return POWER_ERR_INVALID_ARG;
    if (!Power_LockMutex()) { return POWER_ERR_LOCK_FAILED; }
    powerManager.stats.currentMode = powerManager.currentMode;
    powerManager.stats.resetReason = powerManager.resetReason;
    powerManager.stats.wakeCause = powerManager.wakeCause;
    powerManager.stats.activePowerLocks = powerManager.activeLockCount;
#ifndef RACOOL_TEST_NATIVE
    powerManager.stats.uptimeSeconds = (uint32_t)(esp_timer_get_time() / 1000000ULL);
#else
    powerManager.stats.uptimeSeconds = 0;
#endif
    *stats = powerManager.stats;
    Power_UnlockMutex();
    return POWER_OK;
}

static PowerResetReason_t Power_GetResetReason(IPowerManager *self) {
    if (!Power_LockMutex()) { return POWER_RESET_UNKNOWN; }
    PowerResetReason_t r = powerManager.resetReason;
    Power_UnlockMutex();
    return r;
}

static const char *Power_GetResetReasonString(IPowerManager *self) {
    return GetResetReasonStr(Power_GetResetReason(self));
}

static PowerWakeupSource_t Power_GetWakeupCause(IPowerManager *self) {
    if (!Power_LockMutex()) { return POWER_WAKEUP_UNDEFINED; }
    PowerWakeupSource_t w = powerManager.wakeCause;
    Power_UnlockMutex();
    return w;
}

static int32_t Power_SelfTest(IPowerManager *self) {
    if (!self) return POWER_ERR_INVALID_ARG;
    if (powerManager.state != SERVICE_STATE_STARTED) return POWER_ERR_INVALID_STATE;
    LOGI("Power_SelfTest", "Starting PowerManager self-test sequence...");

    // Test 1: Lock acquiring and releasing
    int32_t res = self->AcquirePowerLock(self, "SelfTest");
    if (res != POWER_OK) return res;

    res = self->ReleasePowerLock(self, "SelfTest");
    if (res != POWER_OK) return res;

    // Test 2: Inspect the actual SDK profile without changing persistent settings.
#ifdef POWER_PM_SUPPORTED
    if (!Power_LockMutex()) { return POWER_ERR_LOCK_FAILED; }
    PowerModeProfile expected;
    esp_pm_config_t actual;
    bool matches = PowerManagerConfig_GetProfile((uint8_t)powerManager.currentMode, &expected) &&
        esp_pm_get_configuration(&actual) == ESP_OK &&
        actual.min_freq_mhz == expected.minFrequencyMHz &&
        actual.max_freq_mhz == expected.maxFrequencyMHz && !actual.light_sleep_enable;
    Power_UnlockMutex();
    if (!matches) return POWER_ERR_INVALID_STATE;
#else
    return POWER_ERR_NOT_SUPPORTED;
#endif

    // Test 3: Stats reading
    PowerStats_t stats;
    res = self->GetStats(self, &stats);
    if (res != POWER_OK) return res;

    LOGI("Power_SelfTest", "PowerManager self-test passed successfully!");
    return POWER_OK;
}

static int32_t Power_InitializeWithConfig(IPowerManager *self,
                                           PowerManagerConfig config,
                                           IDIRegistry *registry) {
    PowerModeProfile initialProfile;
    if (atomic_load(&powerManager.sleepPending)) return POWER_ERR_BUSY;
    if (!self || config.autoLightSleepEnabled || config.rotaryWakeupLevel > 1 ||
        !PowerManagerConfig_GetProfile(config.defaultMode, &initialProfile)) return POWER_ERR_INVALID_ARG;
    LOGI("Power_InitializeWithConfig", "Initializing PowerManager service...");

    if (!powerManager.mutex) {
        powerManager.mutex = xSemaphoreCreateMutex();
        if (!powerManager.mutex) return POWER_ERR_NO_MEM;
    }

    if (!Power_LockMutex()) { return POWER_ERR_LOCK_FAILED; }
    if (powerManager.state == SERVICE_STATE_STARTED || powerManager.activeLockCount ||
        powerManager.sleepTimerOwned || powerManager.sleepGpioOwned || atomic_load(&powerManager.restartPending) || atomic_load(&powerManager.sleepPending)) {
        Power_UnlockMutex(); return POWER_ERR_BUSY;
    }
#ifdef POWER_PM_SUPPORTED
    for (unsigned i = 0; i < POWER_MAX_LOCK_OWNERS; ++i) {
        if (powerManager.locks[i].handle) {
            Power_UnlockMutex();
            return POWER_ERR_BUSY;
        }
    }
#endif
    powerManager.config = config;
    powerManager.currentMode = POWER_MODE_UNKNOWN;
    powerManager.resetReason = MapEspResetReason(esp_reset_reason());
    powerManager.wakeCause = MapEspWakeupCause(esp_sleep_get_wakeup_cause());
    powerManager.state = SERVICE_STATE_INITIALIZED;
    powerManager.activeLockCount = 0;
    memset(&powerManager.stats, 0, sizeof(powerManager.stats));
    powerManager.stats.resetReason = powerManager.resetReason;
    powerManager.stats.wakeCause = powerManager.wakeCause;
    Power_UnlockMutex();

    PublishEvent(EVENT_POWER_RESET_REASON, (int32_t)powerManager.resetReason);
    LOGI("Power_InitializeWithConfig", "Initialized: resetReason=%s, wakeCause=%d",
         GetResetReasonStr(powerManager.resetReason), (int)powerManager.wakeCause);

    return POWER_OK;
}

static int32_t Power_Initialize(IService *self, IDIRegistry *registry) {
    return Power_InitializeWithConfig((IPowerManager *)self, PowerManagerConfig_GetDefault(), registry);
}

IPowerManager *PowerManager_GetInstance(void) {
    static bool initialized = false;
    if (!initialized) {
        powerManager.base.base.Initialize = Power_Initialize;
        powerManager.base.base.Start = Power_Start;
        powerManager.base.base.Stop = Power_Stop;
        powerManager.base.base.GetState = Power_GetState;
        powerManager.base.base.CheckHealth = Power_CheckHealth;

        powerManager.base.InitializeWithConfig = Power_InitializeWithConfig;
        powerManager.base.EnterDeepSleep = Power_EnterDeepSleep;
        powerManager.base.EnterLightSleep = Power_EnterLightSleep;
        powerManager.base.Restart = Power_Restart;
        powerManager.base.RestartAsync = Power_RestartAsync;
        powerManager.base.IsTransitionPending = Power_IsTransitionPending;
        powerManager.base.SetPowerMode = Power_SetPowerMode;
        powerManager.base.GetPowerMode = Power_GetPowerMode;
        powerManager.base.AcquirePowerLock = Power_AcquirePowerLock;
        powerManager.base.AcquireTypedPowerLock = Power_AcquireTypedPowerLock;
        powerManager.base.GetLockSnapshot = Power_GetLockSnapshot;
        powerManager.base.ReleasePowerLock = Power_ReleasePowerLock;
        powerManager.base.GetStats = Power_GetStats;
        powerManager.base.GetResetReason = Power_GetResetReason;
        powerManager.base.GetResetReasonString = Power_GetResetReasonString;
        powerManager.base.GetWakeupCause = Power_GetWakeupCause;
        powerManager.base.SelfTest = Power_SelfTest;

        powerManager.state = SERVICE_STATE_UNINITIALIZED;
        initialized = true;
        LOGI("PowerManager_GetInstance", "PowerManager instance created successfully");
    }
    return &powerManager.base;
}
