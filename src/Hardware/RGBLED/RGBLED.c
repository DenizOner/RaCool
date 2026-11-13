#include "Core/Localization/Localization.h"
#include "Hardware/RGBLED/RGBLEDCLIParsing.h"
#include "Hardware/LEDAnimations/LEDTransition.h"
#include "Hardware/LEDAnimations/LEDAnimations.h"
#include "Hardware/LEDAnimations/LEDAnimationCatalog.h"
#include "esp_timer.h"
#include "Management/Watchdog/WatchdogWorker.h"
#include "Management/Watchdog/WatchdogConfig.h"
#include "Hardware/RGBLED/IRGBLED.h"
#include "Hardware/RGBLED/RGBLEDAnimations.h"
#include "Hardware/RGBLED/RGBLEDColorEngine.h"
#include "Hardware/RGBLED/RGBLEDHardware.h"

#include "Core/DI/IDIRegistry.h"
#include "Core/EventBus/IEventBus.h"
#include "Core/ServiceLocator/ServiceLocator.h"
#include "Core/ServiceLocator/ServiceNames.h"
#include "Hardware/PinManager/IPinManager.h"
#include "Management/ConfigurationManager/IConfigurationManager.h"
#include "Management/ConfigurationManager/SystemConfigs.h"
#include "Controllers/ClimateController/IClimateController.h"
#include "Controllers/UIController/BootAnimation.h"
#include "Communication/DebugConsole/IDebugConsole.h"

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

static const char *TAG = "RGBLEDService";

/* ──────────────────────────────────────────
 * State & Lifecycle
 * ────────────────────────────────────────── */

typedef struct {
    IRGBLED interface;
    RGBLEDState state;
    QueueHandle_t cmdQueue;
    TaskHandle_t taskHandle;
    SemaphoreHandle_t stateMutex;
    atomic_bool isRunning;
    _Atomic(esp_err_t) outputError;
    _Atomic(esp_err_t) settingsError;
    _Atomic(ServiceState) lifecycleState;
    SemaphoreHandle_t lifecycleMutex;
    WatchdogWorker worker;
    uint8_t subscriptions;
    bool hardwareOwned;
    uint32_t animationClockMs;
    IEventBus *eventBus;
    LEDTransition transition;
    LEDFrame targetFrame;
    LEDTransition previewTransition;
    LEDFrame renderedFrame;
    RGBColor previewColor;
    uint32_t previewStartMs, previewDurationMs;
    bool previewActive;
    bool previewAnimated;
    bool previewBrightnessOnly;
    uint8_t previewBrightness;
    LEDAnimationInstance previewInstance;
    uint32_t previewClockMs;
    bool settingsApplied;
    uint8_t appliedMode;
    uint8_t appliedAnimation;
    uint8_t appliedBrightness;
    bool appliedSystemEnabled;
    atomic_bool settingsRefreshPending;
} RGBLEDServiceImpl;

static RGBLEDServiceImpl instance = {0};

static uint32_t RGBLED_NowMs(void) { return (uint32_t)(esp_timer_get_time() / 1000ULL); }

static esp_err_t StartEngine(RGBLEDAnimType type, RGBColor color, RGBLEDPriority priority, uint32_t duration) {
    /* Selected effects need a visible carrier even when the previous output was
     * black (startup/off). Global brightness and enabled state still own darkness. */
    if (!(color.r | color.g | color.b)) color = (RGBColor){255,255,255};
    esp_err_t result = RGBLEDAnimEngine_Start(type, color, priority, duration);
    if (result == ESP_OK) instance.animationClockMs = RGBLED_NowMs();
    return result;
}

static bool TickEngine(uint32_t now, RGBColor *color) {
    uint32_t elapsed = now - instance.animationClockMs;
    instance.animationClockMs = now;
    return RGBLEDAnimEngine_Tick(elapsed, color);
}

static void SetRenderTarget(RGBColor color, uint8_t brightness, uint32_t duration_ms) {
    instance.state.targetColor = color;
    LEDFrame target = {color.r, color.g, color.b, brightness};
    if (memcmp(&target, &instance.targetFrame, sizeof(target)) != 0) {
        LEDTransition_Start(&instance.transition, RGBLED_NowMs(), duration_ms);
        instance.targetFrame = target;
    }
    instance.state.isFading = instance.transition.active;
}

static void RenderFrame(uint32_t now_ms) {
    LEDFrame frame = LEDTransition_Sample(&instance.transition, instance.targetFrame, now_ms);
    instance.state.targetColor = (RGBColor){instance.targetFrame.r,instance.targetFrame.g,instance.targetFrame.b};
    instance.state.isFading = instance.transition.active;
    if (instance.previewActive) {
        bool allowed=instance.settingsApplied && instance.appliedSystemEnabled && instance.appliedMode!=EXT_LED_MODE_OFF &&
            !(RGBLEDAnimEngine_IsActive() && instance.state.animPriority>=RGBLED_PRIO_HIGH);
        if (!allowed || (uint32_t)(now_ms-instance.previewStartMs)>=instance.previewDurationMs) {
            instance.previewActive=false;
            if (allowed) {
                LEDTransition_Reset(&instance.transition,instance.renderedFrame);
                LEDTransition_Start(&instance.transition,now_ms,LED_TRANSITION_DEFAULT_MS);
                frame=LEDTransition_Sample(&instance.transition,instance.targetFrame,now_ms);
            }
        } else {
            if (instance.previewAnimated) {
                LEDColor color=instance.previewInstance.plugin.Update(&instance.previewInstance.plugin,now_ms-instance.previewClockMs);
                instance.previewClockMs=now_ms;
                instance.previewColor=(RGBColor){color.r,color.g,color.b};
            }
            LEDFrame preview=instance.previewBrightnessOnly ?
                (LEDFrame){frame.r,frame.g,frame.b,instance.previewBrightness} :
                (LEDFrame){instance.previewColor.r,instance.previewColor.g,instance.previewColor.b,instance.state.brightness};
            frame=LEDTransition_Sample(&instance.previewTransition,preview,now_ms);
        }
    }
    instance.renderedFrame=frame;
    esp_err_t result = RGBLEDHardware_SetColorImmediate((RGBColor){frame.r, frame.g, frame.b}, frame.brightness);
#if defined(RACOOL_LED_PHASE_DIAGNOSTIC)
    static uint32_t phase_log_ms;
    static uint64_t previous_output_us;
    static uint32_t maximum_gap_us;
    uint64_t output_us=esp_timer_get_time();
    uint64_t gap_us=previous_output_us ? output_us-previous_output_us : 0;
    previous_output_us=output_us;
    if(gap_us>maximum_gap_us) maximum_gap_us=gap_us>UINT32_MAX ? UINT32_MAX : (uint32_t)gap_us;
    if (now_ms < 24000U && (uint32_t)(now_ms - phase_log_ms) >= 200U) {
        phase_log_ms = now_ms;
        ESP_LOGI("LEDPHASE", "external t=%lu anim=%d rgb=%u,%u,%u brightness=%u result=%d output=%llu gap_max=%lu",
            (unsigned long)now_ms, (int)instance.state.activeAnim,
            frame.r, frame.g, frame.b, frame.brightness, (int)result,
            (unsigned long long)output_us,(unsigned long)maximum_gap_us);
        maximum_gap_us=0;
    }
#endif
    esp_err_t previous = atomic_exchange(&instance.outputError, result);
    if (result != ESP_OK && result != previous)
        ESP_LOGE(TAG, "LED output failed: %s", esp_err_to_name(result));
}


/* ──────────────────────────────────────────
 * Forward Declarations
 * ────────────────────────────────────────── */
static int32_t ServiceInitialize(IService *service, struct IDIRegistry *registry);
static int32_t ServiceStart(IService *service);
static int32_t ServiceStop(IService *service);
static ServiceState ServiceGetState(IService *service);
static int32_t ServiceCheckHealth(IService *service);

static esp_err_t SetColorRGB(IRGBLED *self, RGBColor color);
static esp_err_t SetColorHSV(IRGBLED *self, HSVColor color);
static esp_err_t PreviewColor(IRGBLED *self, RGBColor color, uint32_t duration_ms);
static esp_err_t PreviewAnimation(IRGBLED *self, RGBLEDAnimType type, uint32_t duration_ms);
static esp_err_t UpdateByTemperature(IRGBLED *self, float temperature);
static esp_err_t FadeToColor(IRGBLED *self, RGBColor target, uint32_t fade_ms);
static esp_err_t SetBrightness(IRGBLED *self, uint8_t brightness);
static esp_err_t PreviewBrightness(IRGBLED *self, uint8_t brightness, uint32_t duration_ms);
static esp_err_t StartAnimation(IRGBLED *self, RGBLEDAnimType type, RGBLEDPriority priority, uint32_t duration_ms);
static esp_err_t StopAnimation(IRGBLED *self);
static esp_err_t ApplySelectedMode(IRGBLED *self);
static esp_err_t TurnOff(IRGBLED *self);
static esp_err_t GetLEDState(IRGBLED *self, RGBLEDState *out_state);

static void RGBLEDTask(void *pvParameters);
static void HandleCommand(RGBLEDCommand cmd);
static void BroadcastStateChange(void);
static void OnTemperatureUpdate(BusEvent event);
static void ApplySettings(void);
static void OnSettingsChanged(BusEvent event);
static esp_err_t RegisterConsoleCommands(void);
static esp_err_t EnqueueWithTimeout(RGBLEDCommand cmd, TickType_t timeout);

/* ──────────────────────────────────────────
 * Initialization & Instance
 * ────────────────────────────────────────── */

IRGBLED *GetRGBLEDService(void) {
    if (!instance.interface.base.Start) {
        // Initialize interface pointers
        instance.interface.base.Initialize = ServiceInitialize;
        instance.interface.base.Start = ServiceStart;
        instance.interface.base.Stop = ServiceStop;
        instance.interface.base.GetState = ServiceGetState;
        instance.interface.base.CheckHealth = ServiceCheckHealth;

        instance.interface.SetColorRGB = SetColorRGB;
        instance.interface.SetColorHSV = SetColorHSV;
        instance.interface.PreviewColor = PreviewColor;
        instance.interface.PreviewAnimation = PreviewAnimation;
        instance.interface.UpdateByTemperature = UpdateByTemperature;
        instance.interface.FadeToColor = FadeToColor;
        instance.interface.SetBrightness = SetBrightness;
        instance.interface.PreviewBrightness = PreviewBrightness;
        instance.interface.StartAnimation = StartAnimation;
        instance.interface.StopAnimation = StopAnimation;
        instance.interface.ApplySelectedMode = ApplySelectedMode;
        instance.interface.TurnOff = TurnOff;
        instance.interface.GetLEDState = GetLEDState;
    }
    return &instance.interface;
}

/* Control resources survive Stop/Start. Initialization precedes consumers.
 * Lock order: lifecycle -> state; worker checkpoints hold neither lock. */
static int32_t ServiceInitialize(IService *service, struct IDIRegistry *registry) {
    (void)registry;
    if (service != &instance.interface.base) return -1;
    if (instance.lifecycleMutex) return 0;
    SemaphoreHandle_t lifecycle = xSemaphoreCreateMutex();
    if (!lifecycle) return ESP_ERR_NO_MEM;
    instance.stateMutex = xSemaphoreCreateRecursiveMutex();
    instance.cmdQueue = xQueueCreate(RGBLED_CMD_QUEUE_LENGTH, sizeof(RGBLEDCommand));
    if (!instance.stateMutex || !instance.cmdQueue || WatchdogWorker_Init(&instance.worker) != 0) {
        if (instance.stateMutex) vSemaphoreDelete(instance.stateMutex);
        if (instance.cmdQueue) vQueueDelete(instance.cmdQueue);
        instance.stateMutex = NULL;
        instance.cmdQueue = NULL;
        vSemaphoreDelete(lifecycle);
        return ESP_ERR_NO_MEM;
    }
    instance.lifecycleMutex = lifecycle;
    instance.lifecycleState = SERVICE_STATE_INITIALIZED;
    return 0;
}

static ServiceState ServiceGetState(IService *service) {
    (void)service;
    return instance.lifecycleState;
}

static int32_t ServiceCheckHealth(IService *service) {
    (void)service;
    return instance.isRunning && atomic_load(&instance.worker.task) &&
           !atomic_load(&instance.worker.stopRequested) && atomic_load(&instance.outputError) == ESP_OK &&
           atomic_load(&instance.settingsError) == ESP_OK ? 0 : -1;
}

static const char *const subscriptionEvents[] = {
    EVENT_IR_TEMP_UPDATED, EVENT_CONFIG_CHANGED, "settings_changed"
};
static const BusEventHandler subscriptionHandlers[] = {
    OnTemperatureUpdate, OnSettingsChanged, OnSettingsChanged
};

static int32_t StopOwned(void) {
    instance.isRunning = false;
    instance.lifecycleState = SERVICE_STATE_ERROR;
    if (WatchdogWorker_Stop(&instance.worker, pdMS_TO_TICKS(WATCHDOG_WORKER_CONTROL_TIMEOUT_MS)) != 0)
        return -1;
    instance.taskHandle = NULL;
    for (unsigned i = 0; i < 3; ++i) {
        if (!(instance.subscriptions & (1U << i))) continue;
        if (!instance.eventBus || !instance.eventBus->Unsubscribe ||
            instance.eventBus->Unsubscribe(instance.eventBus, subscriptionEvents[i], subscriptionHandlers[i]) != 0)
            return -1;
        instance.subscriptions &= ~(1U << i);
    }
    instance.eventBus = NULL;
    if (xSemaphoreTakeRecursive(instance.stateMutex, pdMS_TO_TICKS(WATCHDOG_WORKER_CONTROL_TIMEOUT_MS)) != pdTRUE)
        return -1;
    esp_err_t result = instance.hardwareOwned ? RGBLEDHardware_Deinit() : ESP_OK;
    if (result == ESP_OK) {
        instance.hardwareOwned = false;
        LEDTransition_Reset(&instance.transition, (LEDFrame){0});
        RGBLEDAnimEngine_Deinit();
    }
    xSemaphoreGiveRecursive(instance.stateMutex);
    if (result != ESP_OK) return result;
    xQueueReset(instance.cmdQueue);
    instance.lifecycleState = SERVICE_STATE_STOPPED;
    return 0;
}

static int32_t StartOwned(void) {
    if (instance.lifecycleState == SERVICE_STATE_STARTED) {
        int32_t result = ServiceCheckHealth(&instance.interface.base);
        return result == 0 ? RegisterConsoleCommands() : result;
    }
    if (instance.lifecycleState != SERVICE_STATE_INITIALIZED && instance.lifecycleState != SERVICE_STATE_STOPPED)
        return -1;
    esp_err_t registration_result = RegisterConsoleCommands();
    if (registration_result != ESP_OK) return registration_result;
    xQueueReset(instance.cmdQueue);
    ESP_LOGI(TAG, "Starting RGBLED Service...");

    // Get configuration
    RGBLEDConfig config = RGBLED_GetDefaultConfig();

    // Init Hardware
    instance.hardwareOwned = true;
    esp_err_t err = RGBLEDHardware_Init(&config);
    if (err != ESP_OK) { (void)StopOwned(); return err; }
    err = RGBLEDAnimEngine_Init();
    if (err != ESP_OK) { (void)StopOwned(); return err; }

    LEDTransition_Reset(&instance.transition, (LEDFrame){0});
    instance.targetFrame = (LEDFrame){0};

    atomic_store(&instance.outputError, ESP_OK);
    atomic_store(&instance.settingsError, ESP_OK);
    // Default state
    memset(&instance.state, 0, sizeof(RGBLEDState));
    instance.state.brightness = 255;
    instance.state.colorMode = RGBLED_COLOR_MODE_RGB;
    instance.state.lastTemperature = -127.0f; // Mark as uninitialized
    instance.settingsApplied = false;
    atomic_store(&instance.settingsRefreshPending, false);

    instance.eventBus = (IEventBus *)ServiceLocator_GetService(SVC_NAME_EVENT_BUS);
    if (instance.eventBus) {
        for (unsigned i = 0; i < 3; ++i) {
            if (!instance.eventBus->Subscribe || !instance.eventBus->Unsubscribe ||
                instance.eventBus->Subscribe(instance.eventBus, subscriptionEvents[i], subscriptionHandlers[i]) != 0) {
                (void)StopOwned();
                return -1;
            }
            instance.subscriptions |= (1U << i);
        }
    }
    instance.isRunning = true;
    if (WatchdogWorker_Start(&instance.worker, RGBLEDTask, "RGBLEDTask", RGBLED_TASK_STACK_SIZE,
                            NULL, RGBLED_TASK_PRIORITY) != 0) {
        (void)StopOwned();
        return ESP_ERR_NO_MEM;
    }
    instance.taskHandle = atomic_load(&instance.worker.task);
    instance.lifecycleState = SERVICE_STATE_STARTED;

    // Use the same enable, brightness and boot-selection policy on every settings path.
    RGBLEDCommand start_cmd = {.type = RGBLED_CMD_APPLY_SETTINGS};
    if (xQueueSend(instance.cmdQueue, &start_cmd, 0) != pdTRUE) {
        (void)StopOwned(); return ESP_ERR_TIMEOUT;
    }

    ESP_LOGI(TAG, "RGBLED Service started successfully");
    return 0;
}

static int32_t ServiceStart(IService *service) {
    if (ServiceInitialize(service, NULL) != 0 ||
        xSemaphoreTake(instance.lifecycleMutex, pdMS_TO_TICKS(WATCHDOG_WORKER_CONTROL_TIMEOUT_MS)) != pdTRUE)
        return -1;
    int32_t result = StartOwned();
    xSemaphoreGive(instance.lifecycleMutex);
    return result;
}

static int32_t ServiceStop(IService *service) {
    if (service != &instance.interface.base) return -1;
    if (!instance.lifecycleMutex) return 0;
    if (atomic_load(&instance.worker.task) == xTaskGetCurrentTaskHandle() && atomic_load(&instance.worker.task))
        return -1;
    if (xSemaphoreTake(instance.lifecycleMutex, pdMS_TO_TICKS(WATCHDOG_WORKER_CONTROL_TIMEOUT_MS)) != pdTRUE)
        return -1;
    int32_t result = StopOwned();
    xSemaphoreGive(instance.lifecycleMutex);
    return result;
}

/* ──────────────────────────────────────────
 * Async Task & Command Handling
 * ────────────────────────────────────────── */

static TickType_t FrameWait(TickType_t last_render, TickType_t now, TickType_t frame_period) {
    TickType_t elapsed = now - last_render;
    return elapsed < frame_period ? frame_period - elapsed : 0;
}

static void RGBLEDTask(void *pvParameters) {
    RGBLEDCommand cmd;
    TickType_t frame_period = pdMS_TO_TICKS(RGBLED_ANIM_TICK_MS);
    if (!frame_period) frame_period = 1;
    TickType_t last_render = xTaskGetTickCount();

    instance.previewActive=false;
    bool boot_was_running = BootAnimation_IsRunning();
    while (1) {
        WatchdogWorker_Checkpoint(&instance.worker, NULL, RGBLED_NowMs());
        if (!instance.isRunning) { vTaskDelay(1); continue; }
        bool boot_is_running = BootAnimation_IsRunning();
        if (boot_was_running && !boot_is_running) {
            xSemaphoreTakeRecursive(instance.stateMutex, portMAX_DELAY);
            RGBLEDAnimEngine_Stop();
            instance.state.activeAnim = RGBLED_ANIM_NONE;
            instance.settingsApplied = false;
            ApplySettings();
            xSemaphoreGiveRecursive(instance.stateMutex);
        }
        boot_was_running = boot_is_running;
        if (atomic_exchange(&instance.settingsRefreshPending, false)) {
            xSemaphoreTakeRecursive(instance.stateMutex, portMAX_DELAY);
            if (instance.isRunning) ApplySettings();
            xSemaphoreGiveRecursive(instance.stateMutex);
        }
        // Wait for command or timeout (for animation tick)
        TickType_t wait_time = FrameWait(last_render, xTaskGetTickCount(), frame_period);
        if (xQueueReceive(instance.cmdQueue, &cmd, wait_time) == pdPASS) {
            xSemaphoreTakeRecursive(instance.stateMutex, portMAX_DELAY);
            if (instance.isRunning) HandleCommand(cmd);
            xSemaphoreGiveRecursive(instance.stateMutex);
        }

        xSemaphoreTakeRecursive(instance.stateMutex, portMAX_DELAY);
        if (instance.isRunning && RGBLEDAnimEngine_IsActive()) {
            RGBColor color;
            if (TickEngine(RGBLED_NowMs(), &color)) {
                // Advance the animation target without restarting the entry fade.
                instance.targetFrame = (LEDFrame){color.r, color.g, color.b, instance.state.brightness};
            } else if (!RGBLEDAnimEngine_IsActive()) {
                instance.state.activeAnim = RGBLED_ANIM_NONE;
                instance.settingsApplied = false;
                ApplySettings();
                BroadcastStateChange();
            }
        }
        if (instance.isRunning) RenderFrame(RGBLED_NowMs());
        last_render = xTaskGetTickCount();
        xSemaphoreGiveRecursive(instance.stateMutex);

        /* Yield briefly to ensure IDLE task and lower priority tasks are not starved */
        vTaskDelay(1);
    }

}

static void HandleCommand(RGBLEDCommand cmd) {
    bool state_changed = false;
    bool gate_enabled = !instance.settingsApplied ||
        (instance.appliedSystemEnabled && instance.appliedMode != EXT_LED_MODE_OFF);

    if (cmd.type!=RGBLED_CMD_PREVIEW_COLOR && cmd.type!=RGBLED_CMD_PREVIEW_ANIMATION && cmd.type!=RGBLED_CMD_PREVIEW_BRIGHTNESS && cmd.type!=RGBLED_CMD_UPDATE_TEMPERATURE &&
        cmd.type!=RGBLED_CMD_SET_BRIGHTNESS && cmd.type!=RGBLED_CMD_APPLY_SETTINGS)
        instance.previewActive=false;
    switch (cmd.type) {
        case RGBLED_CMD_PREVIEW_BRIGHTNESS:
            if (!gate_enabled || (RGBLEDAnimEngine_IsActive() && instance.state.animPriority>=RGBLED_PRIO_HIGH)) break;
            instance.previewAnimated=false;
            instance.previewBrightnessOnly=true;
            instance.previewBrightness=cmd.payload.brightness_preview.brightness;
            instance.previewStartMs=RGBLED_NowMs();
            instance.previewDurationMs=cmd.payload.brightness_preview.duration_ms;
            LEDTransition_Reset(&instance.previewTransition,instance.renderedFrame);
            LEDTransition_Start(&instance.previewTransition,instance.previewStartMs,LED_TRANSITION_DEFAULT_MS);
            instance.previewActive=true;
            break;

        case RGBLED_CMD_PREVIEW_COLOR:
            if (!gate_enabled || (RGBLEDAnimEngine_IsActive() && instance.state.animPriority>=RGBLED_PRIO_HIGH)) break;
            instance.previewBrightnessOnly=false;
            instance.previewAnimated=false;
            instance.previewColor=cmd.payload.preview.color;
            instance.previewStartMs=RGBLED_NowMs();
            instance.previewDurationMs=cmd.payload.preview.duration_ms;
            LEDTransition_Reset(&instance.previewTransition,instance.renderedFrame);
            LEDTransition_Start(&instance.previewTransition,instance.previewStartMs,LED_TRANSITION_DEFAULT_MS);
            instance.previewActive=true;
            break;

        case RGBLED_CMD_PREVIEW_ANIMATION: {
            if (!gate_enabled || (RGBLEDAnimEngine_IsActive() && instance.state.animPriority>=RGBLED_PRIO_HIGH)) break;
            size_t count;
            const LEDAnimationCatalogEntry *entries=LEDAnimationCatalog_Entries(&count);
            size_t index=LEDAnimationCatalog_Index(cmd.payload.animation.animType,false);
            if (index>=count) break;
            uint8_t type=entries[index].builtin_id;
            if (type!=255 && LEDAnimations_CreateInstance((BuiltinLEDAnimationType)type,&instance.previewInstance)!=ESP_OK) break;
            instance.previewBrightnessOnly=false;
            instance.previewAnimated=type!=255;
            if (instance.previewAnimated) instance.previewInstance.plugin.render_brightness=255;
            instance.previewColor=(RGBColor){0};
            instance.previewStartMs=instance.previewClockMs=RGBLED_NowMs();
            instance.previewDurationMs=cmd.payload.animation.duration_ms;
            LEDTransition_Reset(&instance.previewTransition,instance.renderedFrame);
            LEDTransition_Start(&instance.previewTransition,instance.previewStartMs,LED_TRANSITION_DEFAULT_MS);
            instance.previewActive=true;
            break;
        }

        case RGBLED_CMD_SET_COLOR_RGB:
            instance.state.targetColor = cmd.payload.rgb;
            instance.state.currentColor = cmd.payload.rgb;
            instance.state.colorMode = RGBLED_COLOR_MODE_RGB;
            instance.state.isOn = gate_enabled && (cmd.payload.rgb.r > 0 || cmd.payload.rgb.g > 0 || cmd.payload.rgb.b > 0);
            
            RGBLEDAnimEngine_Stop();
            instance.state.activeAnim = RGBLED_ANIM_NONE;
            SetRenderTarget(cmd.payload.rgb, instance.state.isOn ? instance.state.brightness : 0, LED_TRANSITION_DEFAULT_MS);
            state_changed = true;
            break;

        case RGBLED_CMD_FADE_TO_COLOR:
            instance.state.targetColor = cmd.payload.fade.target;
            instance.state.currentColor = cmd.payload.fade.target;
            instance.state.colorMode = RGBLED_COLOR_MODE_RGB;
            instance.state.isOn = gate_enabled;

            RGBLEDAnimEngine_Stop();
            instance.state.activeAnim = RGBLED_ANIM_NONE;
            SetRenderTarget(cmd.payload.fade.target, instance.state.isOn ? instance.state.brightness : 0, cmd.payload.fade.fade_ms);
            state_changed = true;
            break;

        case RGBLED_CMD_SET_COLOR_HSV: {
            RGBColor rgb = RGBLEDColorEngine_HSVtoRGB(cmd.payload.hsv);
            instance.state.currentHSV = cmd.payload.hsv;
            instance.state.currentColor = rgb;
            instance.state.colorMode = RGBLED_COLOR_MODE_HSV;
            instance.state.isOn = gate_enabled && (cmd.payload.hsv.v > 0);
            
            RGBLEDAnimEngine_Stop();
            instance.state.activeAnim = RGBLED_ANIM_NONE;
            SetRenderTarget(rgb, instance.state.isOn ? instance.state.brightness : 0, LED_TRANSITION_DEFAULT_MS);
            state_changed = true;
            break;
        }

        case RGBLED_CMD_SET_BRIGHTNESS:
            if (instance.previewActive && instance.previewBrightnessOnly) {
                instance.previewActive=false;
                LEDTransition_Reset(&instance.transition,instance.renderedFrame);
            }
            instance.state.brightness = cmd.payload.brightness;
            SetRenderTarget((RGBColor){instance.targetFrame.r, instance.targetFrame.g, instance.targetFrame.b},
                            instance.state.isOn ? instance.state.brightness : 0, LED_TRANSITION_DEFAULT_MS);
            state_changed = true;
            break;

        case RGBLED_CMD_TURN_OFF:
            instance.state.isOn = false;
            instance.state.colorMode = RGBLED_COLOR_MODE_RGB;
            instance.state.activeAnim = RGBLED_ANIM_NONE;
            RGBColor black = {0, 0, 0};
            RGBLEDAnimEngine_Stop();
            SetRenderTarget(black, 0, LED_TRANSITION_DEFAULT_MS);
            state_changed = true;
            break;

        case RGBLED_CMD_SET_ANIMATION:
            if (!gate_enabled) break;
            if (StartEngine(cmd.payload.animation.animType, 
                                       instance.state.currentColor, 
                                       cmd.payload.animation.priority, 
                                       cmd.payload.animation.duration_ms) == ESP_OK) {
                instance.state.colorMode = RGBLED_COLOR_MODE_ANIMATION;
                LEDTransition_Start(&instance.transition, RGBLED_NowMs(), LED_TRANSITION_DEFAULT_MS);
                instance.state.activeAnim = cmd.payload.animation.animType;
                instance.state.animPriority = cmd.payload.animation.priority;
                instance.state.isOn = true;
                state_changed = true;
            }
            break;

        case RGBLED_CMD_STOP_ANIMATION:
            RGBLEDAnimEngine_Stop();
            instance.state.activeAnim = RGBLED_ANIM_NONE;
            instance.settingsApplied = false;
            ApplySettings();
            state_changed = true;
            break;

        case RGBLED_CMD_APPLY_SETTINGS:
            if (cmd.payload.force_mode) instance.settingsApplied = false;
            ApplySettings();
            state_changed = true;
            break;

        case RGBLED_CMD_UPDATE_TEMPERATURE:
            if (!isfinite(cmd.payload.temperature) || cmd.payload.temperature < -50.0f || cmd.payload.temperature > 100.0f) break;
            // Calculate the temperature color even while off so enabling uses the latest reading.
            if (instance.state.colorMode == RGBLED_COLOR_MODE_TEMPERATURE) {
                IConfigurationManager *config = ConfigurationManager_GetInstance();
                SystemSettings snapshot;
                const SystemSettings *settings = config && config->GetSnapshot &&
                    config->GetSnapshot(config, &snapshot) == ESP_OK ? &snapshot : NULL;
                if (!settings) break;

                float cold_min = settings ? settings->hardware.ext_led_temp_cold_min : 17.0f;
                float cold_max = settings ? settings->hardware.ext_led_temp_cold_max : 19.0f;
                float warm_min = settings ? settings->hardware.ext_led_temp_warm_min : 35.0f;
                float warm_max = settings ? settings->hardware.ext_led_temp_warm_max : 37.0f;
                float hot_min = settings ? settings->hardware.ext_led_temp_hot_min : 50.0f;
                float hot_max = settings ? settings->hardware.ext_led_temp_hot_max : 52.0f;
                float danger = settings ? settings->hardware.ext_led_temp_danger : 60.0f;
                float freeze = settings ? settings->hardware.ext_led_temp_freeze : 2.0f;

                RGBColor color_cold_rgb    = settings
                    ? RGBLEDColorEngine_XYtoRGB(settings->hardware.ext_led_color_cold_x, settings->hardware.ext_led_color_cold_y)
                    : RGBLEDColorEngine_GetPresetColor(RGB_COLOR_BLUE);
                RGBColor color_warm_rgb = settings
                    ? RGBLEDColorEngine_XYtoRGB(settings->hardware.ext_led_color_warm_x, settings->hardware.ext_led_color_warm_y)
                    : RGBLEDColorEngine_GetPresetColor(RGB_COLOR_GREEN);
                RGBColor color_hot_rgb     = settings
                    ? RGBLEDColorEngine_XYtoRGB(settings->hardware.ext_led_color_hot_x, settings->hardware.ext_led_color_hot_y)
                    : RGBLEDColorEngine_GetPresetColor(RGB_COLOR_RED);

                RGBColor color_danger_rgb = settings
                    ? RGBLEDColorEngine_XYtoRGB(settings->hardware.ext_led_color_danger_x, settings->hardware.ext_led_color_danger_y)
                    : RGBLEDColorEngine_GetPresetColor(RGB_COLOR_PURPLE);
                RGBColor color_freeze_rgb = settings
                    ? RGBLEDColorEngine_XYtoRGB(settings->hardware.ext_led_color_freeze_x, settings->hardware.ext_led_color_freeze_y)
                    : RGBLEDColorEngine_GetPresetColor(RGB_COLOR_WHITE);

                RGBColor tempColor = RGBLEDColorEngine_TemperatureToColor(
                    cmd.payload.temperature, cold_min, cold_max, warm_min, warm_max,
                    hot_min, hot_max, danger, freeze,
                    color_cold_rgb, color_warm_rgb, color_hot_rgb,
                    color_danger_rgb,
                    color_freeze_rgb);

                bool color_changed = (tempColor.r != instance.state.currentColor.r ||
                                      tempColor.g != instance.state.currentColor.g ||
                                      tempColor.b != instance.state.currentColor.b);

                ESP_LOGD(TAG, "Temp Update: %.1f C, Color: R:%d G:%d B:%d (Changed: %d, IsOn: %d)",
                         cmd.payload.temperature, tempColor.r, tempColor.g, tempColor.b, color_changed, instance.state.isOn);

                instance.state.isOn = settings->system.system_enabled && settings->hardware.ext_led_mode != EXT_LED_MODE_OFF;
                SetRenderTarget(tempColor, instance.state.isOn ? instance.state.brightness : 0, LED_TRANSITION_DEFAULT_MS);
                instance.state.targetColor      = tempColor;
                instance.state.lastTemperature  = cmd.payload.temperature;

                if (color_changed) {
                    state_changed = true;
                }
            } else {
                // Cache temperature while starting or using another color mode.
                instance.state.lastTemperature = cmd.payload.temperature;
            }
            break;

        default:
            break;
    }

    if (state_changed) {
        BroadcastStateChange();
    }
}

static void BroadcastStateChange(void) {
    if (instance.eventBus) {
        BusEvent e = {
            .eventName = RGBLED_EVENT_STATE_CHANGED,
            .type = EVENT_TYPE_INFO,
            .data = &instance.state,
            .dataSize = sizeof(RGBLEDState)
        };
        instance.eventBus->Publish(e);
    }
}

/* ──────────────────────────────────────────
 * Event Handlers
 * ────────────────────────────────────────── */

static void OnTemperatureUpdate(BusEvent event) {
    if (!event.data || event.dataSize < sizeof(float)) return;
    
    // Payload from MLX90614 is float[2] {objectTemp, ambientTemp} or single float
    float temp = *(const float *)event.data;
    
    // Send command instead of directly calling to prevent blocking ISR/Event thread
    if (instance.isRunning && instance.cmdQueue) {
        RGBLEDCommand cmd = {
            .type = RGBLED_CMD_UPDATE_TEMPERATURE,
            .payload.temperature = temp
        };
        (void)EnqueueWithTimeout(cmd, 0);
    }
}

/* ──────────────────────────────────────────
 * API Implementations
 * ────────────────────────────────────────── */

static esp_err_t EnqueueWithTimeout(RGBLEDCommand cmd, TickType_t timeout) {
    if (!instance.lifecycleMutex) return ESP_ERR_INVALID_STATE;
    if (xSemaphoreTake(instance.lifecycleMutex, timeout) != pdTRUE) return ESP_ERR_TIMEOUT;
    esp_err_t result = ESP_ERR_INVALID_STATE;
    if (instance.isRunning && instance.cmdQueue)
        result = xQueueSend(instance.cmdQueue, &cmd, 0) == pdPASS ? ESP_OK : ESP_ERR_TIMEOUT;
    xSemaphoreGive(instance.lifecycleMutex);
    return result;
}

static esp_err_t EnqueueCommand(RGBLEDCommand cmd) {
    /* These public producers may run in an SDK/event callback. */
    return EnqueueWithTimeout(cmd, 0);
}

static esp_err_t SetColorRGB(IRGBLED *self, RGBColor color) {
    RGBLEDCommand cmd = {
        .type = RGBLED_CMD_SET_COLOR_RGB,
        .payload.rgb = color
    };
    return EnqueueCommand(cmd);
}

static esp_err_t PreviewAnimation(IRGBLED *self, RGBLEDAnimType type, uint32_t duration_ms) {
    if (self!=&instance.interface || !duration_ms || duration_ms>RGBLED_COLOR_PREVIEW_MAX_MS ||
        !LEDAnimationCatalog_IsValid((uint8_t)type,false) || (unsigned)type>UINT8_MAX) return ESP_ERR_INVALID_ARG;
    RGBLEDCommand command={.type=RGBLED_CMD_PREVIEW_ANIMATION,.payload.animation={type,RGBLED_PRIO_LOW,duration_ms}};
    return EnqueueCommand(command);
}

static esp_err_t PreviewColor(IRGBLED *self, RGBColor color, uint32_t duration_ms) {
    if (self!=&instance.interface || !duration_ms || duration_ms>RGBLED_COLOR_PREVIEW_MAX_MS) return ESP_ERR_INVALID_ARG;
    RGBLEDCommand command={.type=RGBLED_CMD_PREVIEW_COLOR,.payload.preview={color,duration_ms}};
    return EnqueueCommand(command);
}

static esp_err_t SetColorHSV(IRGBLED *self, HSVColor color) {
    RGBLEDCommand cmd = {
        .type = RGBLED_CMD_SET_COLOR_HSV,
        .payload.hsv = color
    };
    return EnqueueCommand(cmd);
}

static esp_err_t UpdateByTemperature(IRGBLED *self, float temperature) {
    if (!isfinite(temperature) || temperature < -50.0f || temperature > 100.0f)
        return ESP_ERR_INVALID_ARG;
    RGBLEDCommand cmd = {
        .type = RGBLED_CMD_UPDATE_TEMPERATURE,
        .payload.temperature = temperature
    };
    return EnqueueCommand(cmd);
}

static esp_err_t FadeToColor(IRGBLED *self, RGBColor target, uint32_t fade_ms) {
    RGBLEDCommand cmd = {
        .type = RGBLED_CMD_FADE_TO_COLOR,
        .payload.fade.target = target,
        .payload.fade.fade_ms = fade_ms
    };
    return EnqueueCommand(cmd);
}

static esp_err_t PreviewBrightness(IRGBLED *self, uint8_t brightness, uint32_t duration_ms) {
    if (self != &instance.interface || !duration_ms || duration_ms > RGBLED_COLOR_PREVIEW_MAX_MS) return ESP_ERR_INVALID_ARG;
    RGBLEDCommand cmd={.type=RGBLED_CMD_PREVIEW_BRIGHTNESS,.payload.brightness_preview={brightness,duration_ms}};
    return EnqueueCommand(cmd);
}

static esp_err_t SetBrightness(IRGBLED *self, uint8_t brightness) {
    RGBLEDCommand cmd = {
        .type = RGBLED_CMD_SET_BRIGHTNESS,
        .payload.brightness = brightness
    };
    return EnqueueCommand(cmd);
}

static esp_err_t StartAnimation(IRGBLED *self, RGBLEDAnimType type, RGBLEDPriority priority, uint32_t duration_ms) {
    if ((unsigned)type >= RGBLED_ANIM_COUNT || type == RGBLED_ANIM_TEMPERATURE ||
        (unsigned)priority > RGBLED_PRIO_CRITICAL) return ESP_ERR_INVALID_ARG;
    RGBLEDCommand cmd = {
        .type = RGBLED_CMD_SET_ANIMATION,
        .payload.animation.animType = type,
        .payload.animation.priority = priority,
        .payload.animation.duration_ms = duration_ms
    };
    return EnqueueCommand(cmd);
}

static esp_err_t StopAnimation(IRGBLED *self) {
    RGBLEDCommand cmd = {
        .type = RGBLED_CMD_STOP_ANIMATION
    };
    return EnqueueCommand(cmd);
}

static esp_err_t ApplySelectedMode(IRGBLED *self) {
    if (self != &instance.interface) return ESP_ERR_INVALID_ARG;
    RGBLEDCommand cmd = {.type=RGBLED_CMD_APPLY_SETTINGS,.payload.force_mode=true};
    return EnqueueCommand(cmd);
}

static esp_err_t TurnOff(IRGBLED *self) {
    RGBLEDCommand cmd = {
        .type = RGBLED_CMD_TURN_OFF
    };
    return EnqueueCommand(cmd);
}

static esp_err_t GetLEDState(IRGBLED *self, RGBLEDState *out_state) {
    if (!out_state || !instance.isRunning || !instance.stateMutex) {
        return ESP_ERR_INVALID_STATE;
    }
    
    // Thread-safe copy
    if (xSemaphoreTakeRecursive(instance.stateMutex, pdMS_TO_TICKS(WATCHDOG_WORKER_CONTROL_TIMEOUT_MS)) != pdTRUE)
        return ESP_ERR_TIMEOUT;
    if (!instance.isRunning) {
        xSemaphoreGiveRecursive(instance.stateMutex);
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t output_error = atomic_load(&instance.outputError);
    if (output_error == ESP_OK) output_error = atomic_load(&instance.settingsError);
    if (output_error != ESP_OK) {
        xSemaphoreGiveRecursive(instance.stateMutex);
        return output_error;
    }
    memcpy(out_state, &instance.state, sizeof(RGBLEDState));
    uint32_t preview_elapsed=RGBLED_NowMs()-instance.previewStartMs;
    out_state->previewActive=instance.previewActive && preview_elapsed<instance.previewDurationMs;
    out_state->previewColor=instance.previewColor;
    out_state->previewRemainingMs=out_state->previewActive ? instance.previewDurationMs-preview_elapsed : 0;
    xSemaphoreGiveRecursive(instance.stateMutex);
    
    return ESP_OK;
}

static void OnSettingsChanged(BusEvent event) {
    (void)event;
    /* Coalesce latest settings independently of the bounded command queue. */
    if (instance.isRunning) atomic_store(&instance.settingsRefreshPending, true);
}

static int cmd_rgbled_info(int argc, char **argv);
static int cmd_rgbled_set(int argc, char **argv);
static int cmd_rgbled_mode(int argc, char **argv);
static int cmd_rgbled_brightness(int argc, char **argv);
static int cmd_rgbled_anim(int argc, char **argv);

static esp_err_t RegisterConsoleCommands(void) {
    IDebugConsole *console = (IDebugConsole *)ServiceLocator_GetService(SVC_NAME_DEBUG_CONSOLE);
    if (!console) {
        /* CLI is optional for a hardware-only composition. Production startup
         * separately requires the public CLI registrar. */
        return ESP_OK;
    }
    if (!console->RegisterCommand) return ESP_ERR_INVALID_STATE;

    static const esp_console_cmd_t info_cmd = {
        .command = "rgbled_info",
        .help = "Show current RGBLED status",
        .hint = NULL,
        .func = cmd_rgbled_info,
        .argtable = NULL
    };
    esp_err_t result = console->RegisterCommand(console, &info_cmd);
    if (result != ESP_OK) return result;

    static const esp_console_cmd_t set_cmd = {
        .command = "rgbled_set",
        .help = "Set RGBLED color (r g b)",
        .hint = "<r> <g> <b>",
        .func = cmd_rgbled_set,
        .argtable = NULL
    };
    result = console->RegisterCommand(console, &set_cmd);
    if (result != ESP_OK) return result;

    static const esp_console_cmd_t mode_cmd = {
        .command = "rgbled_mode",
        .help = "Set RGBLED mode (0: Temp, 1: Anim, 2: Off)",
        .hint = "<mode>",
        .func = cmd_rgbled_mode,
        .argtable = NULL
    };
    result = console->RegisterCommand(console, &mode_cmd);
    if (result != ESP_OK) return result;

    static const esp_console_cmd_t brightness_cmd = {
        .command = "rgbled_brightness",
        .help = "Set RGBLED brightness (0-255)",
        .hint = "<level>",
        .func = cmd_rgbled_brightness,
        .argtable = NULL
    };
    result = console->RegisterCommand(console, &brightness_cmd);
    if (result != ESP_OK) return result;

    static const esp_console_cmd_t anim_cmd = {
        .command = "rgbled_anim",
        .help = "Set RGBLED animation (0: None, 1: Breathe, 2: Rainbow, 3: Pulse, 4: Candle, 5: Alert)",
        .hint = "<type> <duration_ms>",
        .func = cmd_rgbled_anim,
        .argtable = NULL
    };
    result = console->RegisterCommand(console, &anim_cmd);
    if (result != ESP_OK) return result;

    ESP_LOGI(TAG, "Console commands registered successfully");
    return ESP_OK;
}

static int cmd_rgbled_info(int argc, char **argv) {
    const LocalizationCatalog *catalog = Localization_Snapshot();
    if (argc < 1 || argc > 2 || !argv || !argv[0] ||
        (argc == 2 && (!argv[1] || strcmp(argv[1], "--machine") != 0))) return -1;
    RGBLEDState s;
    if (GetLEDState(GetRGBLEDService(), &s) != ESP_OK) {
        LOC_PRINTF(catalog, LOC_CLI_RGB_FAILED_TO_GET_RGBLED_STATE, "Failed to get RGBLED state.\n");
        return -1;
    }

    if (argc == 2) {
        RGBLED_PrintMachineState(&s);
        return 0;
    }

    LOC_PRINTF(catalog, LOC_CLI_RGB_RGBLED_STATUS, "\n--- RGBLED Status ---\n");
    LOC_PRINTF(catalog, LOC_CLI_RGB_IS_ON_S, "Is On:             %s\n", s.isOn ? Localization_Text(catalog, LOC_CLI_RGB_YES) : Localization_Text(catalog, LOC_CLI_RGB_NO));
    LOC_PRINTF(catalog, LOC_CLI_RGB_BRIGHTNESS_D, "Brightness:        %d\n", s.brightness);
    LOC_PRINTF(catalog, LOC_CLI_RGB_COLOR_MODE_D_0_RGB_1_HSV_2_TEMP_3_ANIM, "Color Mode:        %d (0:RGB, 1:HSV, 2:Temp, 3:Anim)\n", s.colorMode);
    LOC_PRINTF(catalog, LOC_CLI_RGB_CURRENT_COLOR_R_D_G_D_B_D, "Current Color:     R:%d G:%d B:%d\n", s.currentColor.r, s.currentColor.g, s.currentColor.b);
    LOC_PRINTF(catalog, LOC_CLI_RGB_TARGET_COLOR_R_D_G_D_B_D, "Target Color:      R:%d G:%d B:%d\n", s.targetColor.r, s.targetColor.g, s.targetColor.b);
    LOC_PRINTF(catalog, LOC_CLI_RGB_LAST_TEMPERATURE_2F_C, "Last Temperature:  %.2f C\n", s.lastTemperature);
    LOC_PRINTF(catalog, LOC_CLI_RGB_ACTIVE_ANIM_D, "Active Anim:       %d\n", s.activeAnim);
    LOC_PRINTF(catalog, LOC_CLI_RGB_ANIM_PRIORITY_D, "Anim Priority:     %d\n", s.animPriority);
    return 0;
}

static int ReportCommandAcceptance(const LocalizationCatalog *catalog, esp_err_t result) {
    if (result != ESP_OK) {
        LOC_PRINTF(catalog, LOC_CLI_RGB_RGBLED_COMMAND_REJECTED_D, "RGBLED command rejected: %d.\n", (int)result);
        return -1;
    }
    LOC_PRINTF(catalog, LOC_CLI_RGB_RGBLED_COMMAND_ENQUEUED_WORKER_APPLICATION_IS_ASYNCHRONOUS, "RGBLED command enqueued; worker application is asynchronous.\n");
    return 0;
}

static int cmd_rgbled_set(int argc, char **argv) {
    if (!RGBLED_ArgumentsValid(argc, argv, 4)) return -1;
    const LocalizationCatalog *catalog = Localization_Snapshot();
    uint32_t r, g, b;
    if (argc != 4 || !RGBLED_ParseBoundedDecimal(argv[1], 255, &r) ||
        !RGBLED_ParseBoundedDecimal(argv[2], 255, &g) || !RGBLED_ParseBoundedDecimal(argv[3], 255, &b)) {
        LOC_PRINTF(catalog, LOC_CLI_RGB_USAGE_RGBLED_SET_R_0_255_G_0_255_B_0_255, "Usage: rgbled_set <r:0-255> <g:0-255> <b:0-255>\n");
        return -1;
    }
    RGBColor color = {.r=(uint8_t)r, .g=(uint8_t)g, .b=(uint8_t)b};
    return ReportCommandAcceptance(catalog, SetColorRGB(GetRGBLEDService(), color));
}

static int cmd_rgbled_mode(int argc, char **argv) {
    if (!RGBLED_ArgumentsValid(argc, argv, 2)) return -1;
    const LocalizationCatalog *catalog = Localization_Snapshot();
    if (argc != 2 || !argv[1] || strlen(argv[1]) != 1 || argv[1][0] < '0' ||
        argv[1][0] >= '0' + EXT_LED_MODE_COUNT) {
        LOC_PRINTF(catalog, LOC_CLI_RGB_USAGE_RGBLED_MODE_0_1_2, "Usage: rgbled_mode <0|1|2>\n");
        return -1;
    }
    uint8_t mode = (uint8_t)(argv[1][0] - '0');
    IConfigurationManager *config = (IConfigurationManager *)ServiceLocator_GetService(SVC_NAME_CONFIGURATION_MANAGER);
    ConfigurationFieldUpdate update = {
        .offset = offsetof(HardwareConfigs, ext_led_mode), .data = &mode, .size = sizeof(mode)
    };
    if (!config || !config->UpdateFields || config->UpdateFields(config, "hardware", &update, 1, true) != ESP_OK) {
        LOC_PRINTF(catalog, LOC_CLI_RGB_RGBLED_MODE_PERSISTENCE_FAILED, "RGBLED mode persistence failed.\n");
        return -1;
    }
    LOC_PRINTF(catalog, LOC_CLI_RGB_RGBLED_MODE_U_SAVED_WORKER_APPLICATION_IS_ASYNCHRONOUS, "RGBLED mode %u saved; worker application is asynchronous.\n", (unsigned)mode);
    return ReportCommandAcceptance(catalog, ApplySelectedMode(GetRGBLEDService()));
}

esp_err_t RGBLED_SaveBrightnessText(const char *value) {
    uint32_t brightness;
    if (!RGBLED_ParseBoundedDecimal(value, 255, &brightness)) return ESP_ERR_INVALID_ARG;
    IConfigurationManager *config = (IConfigurationManager *)ServiceLocator_GetService(SVC_NAME_CONFIGURATION_MANAGER);
    if (!config || !config->UpdateFields) return ESP_ERR_INVALID_STATE;
    uint8_t setting = (uint8_t)brightness;
    ConfigurationFieldUpdate update = {
        .offset = offsetof(HardwareConfigs, ext_led_brightness), .data = &setting, .size = sizeof(setting)
    };
    return config->UpdateFields(config, "hardware", &update, 1, true);
}

static int cmd_rgbled_brightness(int argc, char **argv) {
    if (!RGBLED_ArgumentsValid(argc, argv, 2)) return -1;
    const LocalizationCatalog *catalog = Localization_Snapshot();
    uint32_t brightness;
    if (argc != 2 || !RGBLED_ParseBoundedDecimal(argv[1], 255, &brightness)) {
        LOC_PRINTF(catalog, LOC_CLI_RGB_USAGE_RGBLED_BRIGHTNESS_0_255, "Usage: rgbled_brightness <0-255>\n");
        return -1;
    }
    esp_err_t result = RGBLED_SaveBrightnessText(argv[1]);
    LOC_PRINTF(catalog, LOC_CLI_RGB_RGBLED_BRIGHTNESS_QUEUE_RESULT_D, "[RGBLED] Brightness persistence result: %d; worker application is asynchronous.\n", (int)result);
    return result == ESP_OK ? 0 : -1;
}

static int cmd_rgbled_anim(int argc, char **argv) {
    if (!RGBLED_ArgumentsValid(argc, argv, 3)) return -1;
    const LocalizationCatalog *catalog = Localization_Snapshot();
    uint32_t type, duration;
    if (argc != 3 || !RGBLED_ParseBoundedDecimal(argv[1], RGBLED_ANIM_COUNT - 1, &type) ||
        !RGBLED_ParseBoundedDecimal(argv[2], UINT32_MAX, &duration)) {
        LOC_PRINTF(catalog, LOC_CLI_RGB_USAGE_RGBLED_ANIM_TYPE_DURATION_MS_0_4294967295, "Usage: rgbled_anim <type> <duration_ms:0-4294967295>\n");
        return -1;
    }
    return ReportCommandAcceptance(catalog, StartAnimation(GetRGBLEDService(), (RGBLEDAnimType)type,
                                                   RGBLED_PRIO_MEDIUM, duration));
}

static void RecordSettings(const SystemSettings *settings) {
    instance.appliedMode = settings->hardware.ext_led_mode;
    instance.appliedAnimation = settings->hardware.ext_led_anim;
    instance.appliedBrightness = settings->hardware.ext_led_brightness;
    instance.appliedSystemEnabled = settings->system.system_enabled;
    instance.settingsApplied = true;
}

static void ApplySettings(void) {
    IConfigurationManager *configMgr = (IConfigurationManager *)ServiceLocator_GetService(SVC_NAME_CONFIGURATION_MANAGER);
    SystemSettings snapshot;
    esp_err_t settings_result = configMgr && configMgr->GetSnapshot
        ? configMgr->GetSnapshot(configMgr, &snapshot) : ESP_ERR_INVALID_STATE;
    if (settings_result == ESP_OK && snapshot.hardware.ext_led_mode >= EXT_LED_MODE_COUNT)
        settings_result = ESP_ERR_INVALID_ARG;
    esp_err_t previous_settings_error = atomic_exchange(&instance.settingsError, settings_result);
    if (settings_result != ESP_OK) {
        if (settings_result != previous_settings_error)
            ESP_LOGE(TAG, "LED settings snapshot rejected: %s", esp_err_to_name(settings_result));
        if (settings_result != ESP_ERR_INVALID_ARG)
            atomic_store(&instance.settingsRefreshPending, true);
        return;
    }
    const SystemSettings *settings = &snapshot;

    bool selection_changed = !instance.settingsApplied ||
        instance.appliedMode != settings->hardware.ext_led_mode ||
        ((settings->hardware.ext_led_mode == EXT_LED_MODE_ANIMATION || BootAnimation_IsRunning()) &&
         instance.appliedAnimation != settings->hardware.ext_led_anim) ||
        instance.appliedSystemEnabled != settings->system.system_enabled;
    bool brightness_changed = !instance.settingsApplied ||
        instance.appliedBrightness != settings->hardware.ext_led_brightness;
    if (instance.previewActive && instance.previewBrightnessOnly && (selection_changed || brightness_changed)) {
        instance.previewActive=false;
        LEDTransition_Reset(&instance.transition,instance.renderedFrame);
    }
    if (selection_changed || brightness_changed)
        instance.state.brightness = settings->hardware.ext_led_brightness;
    if (!selection_changed) {
        if (brightness_changed) {
            SetRenderTarget((RGBColor){instance.targetFrame.r,instance.targetFrame.g,instance.targetFrame.b},
                instance.state.isOn ? instance.state.brightness : 0, LED_TRANSITION_DEFAULT_MS);
            instance.appliedBrightness = settings->hardware.ext_led_brightness;
        }
        /* A brightness or unrelated change must not reset manual color or phase. */
        if (instance.state.colorMode != RGBLED_COLOR_MODE_TEMPERATURE) return;
    }
    instance.settingsApplied = false;

    // During the splash, honor the configured boot animation and enable state.
    // Never allow background settings or temperature mode to preempt it while the boot screen is active.
    if (BootAnimation_IsRunning() && settings->hardware.ext_led_anim != RGBLED_ANIM_NONE &&
        settings->hardware.ext_led_mode != EXT_LED_MODE_OFF && settings->system.system_enabled) {
        RGBLEDAnimType bootAnim = (RGBLEDAnimType)settings->hardware.ext_led_anim;
        
        if (!RGBLEDAnimEngine_IsActive() || instance.state.activeAnim != bootAnim) {
            if (StartEngine(bootAnim, instance.state.currentColor, RGBLED_PRIO_HIGH, 0) != ESP_OK) return;
            instance.state.colorMode = RGBLED_COLOR_MODE_ANIMATION;
            instance.state.activeAnim = bootAnim;
            instance.state.animPriority = RGBLED_PRIO_HIGH;
            instance.state.isOn = true;
        }
        RecordSettings(settings);
        return;
    }

    // Apply mode
    if (!settings->system.system_enabled || settings->hardware.ext_led_mode == EXT_LED_MODE_OFF) {
        instance.state.colorMode = RGBLED_COLOR_MODE_RGB;
        instance.state.isOn = false;
        instance.state.activeAnim = RGBLED_ANIM_NONE;
        RGBColor black = {0, 0, 0};
        RGBLEDAnimEngine_Stop();
        SetRenderTarget(black, 0, LED_TRANSITION_DEFAULT_MS);
    } 
    else if (settings->hardware.ext_led_mode == EXT_LED_MODE_ANIMATION) {
        RGBLEDAnimType animation = (RGBLEDAnimType)settings->hardware.ext_led_anim;
        if (StartEngine(animation, instance.state.currentColor, RGBLED_PRIO_MEDIUM, 0) != ESP_OK) return;
        instance.state.colorMode = RGBLED_COLOR_MODE_ANIMATION;
        instance.state.activeAnim = animation;
        instance.state.animPriority = RGBLED_PRIO_MEDIUM;
        instance.state.isOn = animation != RGBLED_ANIM_NONE;
        if (!instance.state.isOn) SetRenderTarget((RGBColor){0}, 0, LED_TRANSITION_DEFAULT_MS);
    } 
    else {
        // If an animation with HIGH priority is actively running (e.g. Boot Rainbow),
        // do not kill it with background settings updates.
        if (RGBLEDAnimEngine_IsActive() && instance.state.animPriority >= RGBLED_PRIO_HIGH) {
            return;
        }

        instance.state.colorMode = RGBLED_COLOR_MODE_TEMPERATURE;
        instance.state.isOn = true;
        RGBLEDAnimEngine_Stop();
        
        float temp = instance.state.lastTemperature;
        if (!isfinite(temp) || temp < -50.0f || temp > 100.0f) {
            IClimateController *climate = (IClimateController *)ServiceLocator_GetService(SVC_NAME_CLIMATE_CONTROLLER);
            if (climate && climate->GetCurrentObjectTemperature)
                temp = climate->GetCurrentObjectTemperature(climate);
        }
        
        if (!isfinite(temp) || temp < -50.0f || temp > 100.0f) {
            instance.state.isOn = false;
            instance.state.activeAnim = RGBLED_ANIM_NONE;
            SetRenderTarget((RGBColor){0}, 0, LED_TRANSITION_DEFAULT_MS);
            RecordSettings(settings);
            return;
        }

        float coldMin = settings->hardware.ext_led_temp_cold_min;
        float coldMax = settings->hardware.ext_led_temp_cold_max;
        float warmMin = settings->hardware.ext_led_temp_warm_min;
        float warmMax = settings->hardware.ext_led_temp_warm_max;
        float hotMin = settings->hardware.ext_led_temp_hot_min;
        float hotMax = settings->hardware.ext_led_temp_hot_max;

        RGBColor colorCold    = RGBLEDColorEngine_XYtoRGB(settings->hardware.ext_led_color_cold_x,    settings->hardware.ext_led_color_cold_y);
        RGBColor colorWarm = RGBLEDColorEngine_XYtoRGB(settings->hardware.ext_led_color_warm_x, settings->hardware.ext_led_color_warm_y);
        RGBColor colorHot     = RGBLEDColorEngine_XYtoRGB(settings->hardware.ext_led_color_hot_x,     settings->hardware.ext_led_color_hot_y);

        RGBColor tempColor = RGBLEDColorEngine_TemperatureToColor(
            temp, coldMin, coldMax, warmMin, warmMax, hotMin, hotMax,
            settings->hardware.ext_led_temp_danger, settings->hardware.ext_led_temp_freeze,
            colorCold, colorWarm, colorHot,
            RGBLEDColorEngine_XYtoRGB(settings->hardware.ext_led_color_danger_x, settings->hardware.ext_led_color_danger_y),
            RGBLEDColorEngine_XYtoRGB(settings->hardware.ext_led_color_freeze_x, settings->hardware.ext_led_color_freeze_y));
            
        instance.state.currentColor = tempColor;
        instance.state.lastTemperature = temp;
        
        SetRenderTarget(tempColor, instance.state.brightness, LED_TRANSITION_DEFAULT_MS);
    }
    RecordSettings(settings);
}
