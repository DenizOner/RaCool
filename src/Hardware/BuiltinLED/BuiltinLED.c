#include "Core/Localization/Localization.h"
#include "Hardware/LEDAnimations/LEDAnimationLabels.h"
#include "Hardware/LEDAnimations/LEDTransition.h"
#include "Hardware/LEDAnimations/LEDColorTransfer.h"
#include "Hardware/RGBLED/RGBLEDConfig.h"
#include "Hardware/RGBLED/Animations/RainbowFlowAnim.h"
#include "Management/Watchdog/WatchdogWorker.h"
/**
 * @file BuiltinLED.c
 * @brief BuiltinLED service implementation for the RaCool firmware.
 *
 * Drives the single WS2812 RGB LED (GPIO-8) on the ESP32-C6 DevKit via the
 * SPI-backed ESP-IDF led_strip driver. The service exposes a thread-safe,
 * fully asynchronous command queue so callers never block on hardware I/O.
 *
 * Architecture:
 *   - Command methods admit copied commands to a nonblocking queue; status
 *     uses a bounded mutex read and driver error uses an atomic read.
 *   - A dedicated FreeRTOS task (BuiltinLED_AnimTask) drains the queue,
 *     advances animation state, and writes to the LED hardware.
 *   - Hardware I/O shares the state mutex; Stop waits for a lock-free worker checkpoint.
 *   - Built-in animations are registered synchronously during Initialize()
 *     so they are available even before the animation task starts.
 *   - External plugins use the public RegisterAnimation() API (queue-based)
 *     and may be registered at any time after the service is started.
 */

#include "Hardware/BuiltinLED/IBuiltinLED.h"
#include "Hardware/BuiltinLED/BuiltinLEDInput.h"
#include "Hardware/BuiltinLED/BuiltinLEDCLI.h"
#include "Hardware/BuiltinLED/IAnimationPlugin.h"
#include "Hardware/BuiltinLED/Animations/BuiltinAnimations.h"
#include "Hardware/LEDAnimations/LEDAnimations.h"
#include "Hardware/LEDAnimations/LEDAnimationCatalog.h"
#include "Hardware/PinManager/IPinManager.h"
#include "Management/ConfigurationManager/IConfigurationManager.h"
#include "Management/ConfigurationManager/SystemConfigs.h"
#include "Core/DI/IDIRegistry.h"
#include "Core/IService.h"
#include "Core/ServiceLocator/ServiceLocator.h"
#include "Core/ServiceLocator/ServiceNames.h"
#include "Communication/DebugConsole/IDebugConsole.h"
#include "Core/EventBus/IEventBus.h"
#include "esp_log.h"
#include "led_strip.h"
#include <esp_err.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <freertos/queue.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* Compile-time guard: BuiltinLEDStatus must fit in the EventBus inline buffer
 * to avoid dangling-pointer risks when publishing events. */
_Static_assert(
    sizeof(BuiltinLEDStatus) <= BUS_EVENT_INLINE_DATA_SIZE,
    "BuiltinLEDStatus exceeds BUS_EVENT_INLINE_DATA_SIZE â€” "
    "reduce the struct or increase BUS_EVENT_INLINE_DATA_SIZE."
);

static const char *TAG = "BuiltinLED";

/* Structured log helpers that prepend the calling function name. */
#define LOGD(FUNC, ...) ESP_LOGD(TAG, "[" FUNC "] " __VA_ARGS__)
#define LOGI(FUNC, ...) ESP_LOGI(TAG, "[" FUNC "] " __VA_ARGS__)
#define LOGW(FUNC, ...) ESP_LOGW(TAG, "[" FUNC "] " __VA_ARGS__)
#define LOGE(FUNC, ...) ESP_LOGE(TAG, "[" FUNC "] " __VA_ARGS__)

static esp_err_t BuiltinLED_SetAnimation(IBuiltinLED *self, BuiltinLEDAnimationType type,
                                          BuiltinLEDPriority priority, uint32_t duration_ms);


// ---------------------------------------------------------------------------
// Command queue types
// ---------------------------------------------------------------------------

/**
 * @brief Discriminator tag for command messages posted to the task queue.
 */
typedef enum {
    CMD_INIT_MODULE,      ///< Update brightness while preserving the owned GPIO.
    CMD_SET_ANIMATION,    ///< Start an animation with priority and duration.
    CMD_STOP_ANIMATION,   ///< Immediately stop all animation and clear the LED.
    CMD_TURN_ON,          ///< Set LED to static solid white.
    CMD_TURN_OFF,         ///< Turn the LED off.
    CMD_REGISTER_PLUGIN,  ///< Register an external AnimationPlugin.
    CMD_SET_BRIGHTNESS,   ///< Update the maximum brightness level.
    CMD_SETTINGS_CHANGED, ///< Propagate a system configuration change.
    CMD_PREVIEW_ANIMATION,
    CMD_PREVIEW_BRIGHTNESS,
    CMD_SET_STARTUP,      ///< Change the owned splash phase without replacing the runtime default.
    CMD_CONNECTION_NOTICE,
    CMD_CANCEL_CONNECTION_NOTICE,
} BuiltinLEDCmdType;

/**
 * @brief Tagged-union command message posted to the animation task queue.
 *
 * Sizeof this struct must remain small enough to be efficiently copied
 * by xQueueSend/xQueueReceive on a resource-constrained MCU.
 */
typedef struct {
    BuiltinLEDCmdType type;
    union {
        struct { int8_t pin; uint8_t max_brightness;                           } init;
        struct { BuiltinLEDAnimationType anim_type;
                 BuiltinLEDPriority priority; uint32_t duration_ms;            } set_anim;
        struct { AnimationPlugin *plugin;                                       } reg_plugin;
        struct { uint8_t brightness;                                            } set_brightness;
        struct { uint8_t brightness; uint32_t duration_ms; } brightness_preview;
        struct { BuiltinLEDAnimationType default_anim, startup_anim; uint8_t brightness; bool disabled; } settings;
        struct { bool active; } startup;
    } data;
} BuiltinLEDCmd;

// ---------------------------------------------------------------------------
// Service implementation state
// ---------------------------------------------------------------------------

/**
 * @brief Concrete service implementation â€” extends IBuiltinLED via composition.
 */
typedef struct {
    IBuiltinLED       base;           ///< Public interface (must be first member).
    _Atomic(ServiceState) state;          ///< Current lifecycle state.
    _Atomic(esp_err_t) driverError;
    uint8_t           pin;            ///< GPIO pin of the WS2812 LED.
    uint8_t           max_brightness; ///< Current maximum brightness (0â€“255).

    led_strip_handle_t led_strip;     ///< LED strip driver handle.
    SemaphoreHandle_t  mutex;         ///< Protects status and plugin registry.
    SemaphoreHandle_t controlMutex;
    WatchdogWorker worker;
    QueueHandle_t      cmd_queue;     ///< Command queue consumed by AnimTask.

    AnimationPlugin   *plugins[BUILTIN_LED_MAX_PLUGINS]; ///< Plugin registry.
    uint8_t            plugin_count;  ///< Number of registered plugins.

    BuiltinLEDStatus   status;        ///< Public-facing status snapshot.
    BuiltinLEDAnimationType defaultAnimType; ///< Animation restored after a timed override.
    BuiltinLEDAnimationType configuredDefaultAnim; ///< Last saved base; unrelated settings must not replace UI-selected status.
    BuiltinLEDAnimationType startupAnimType;
    bool startupActive; ///< Worker-owned phase, supplied by the UI splash owner.
    bool connectionNoticePending, connectionNoticeActive;

    LEDAnimationInstance previewInstance;
    bool previewActive,previewAnimated;
    bool brightnessPreviewActive;
    uint8_t previewBrightness;
    uint32_t brightnessPreviewStarted,brightnessPreviewDuration;
    uint32_t previewStarted,previewDuration,previewClock;
    LEDTransition previewTransition;
    LEDFrame renderedFrame;
    bool disabled; ///< Persisted global gate; notifications cannot override it.
    LEDTransition transition;
    LEDFrame targetFrame;
    TaskHandle_t  animTaskHandle;     ///< Handle of the animation task.
    uint8_t subscriptions;
    bool driverCleanupFailed;
    IEventBus    *eventBus;           ///< System event bus (may be NULL).
} BuiltinLEDServiceImpl;

/** Singleton service instance (zero-initialised by the C runtime). */
static BuiltinLEDServiceImpl builtinLEDService;

static BuiltinLEDAnimationType BaseAnimation(void) {
    if (builtinLEDService.disabled) return BUILTIN_LED_ANIM_NONE;
    return builtinLEDService.startupActive ? builtinLEDService.startupAnimType : builtinLEDService.defaultAnimType;
}

typedef struct {
    uint64_t time_us, output_us;
    uint64_t advanced_ms;
    uint32_t delta_ms, lock_us, hardware_us, generation;
    uint32_t radio_us, timer_us, ticks;
    int32_t animation;
    int16_t command;
    uint8_t r, g, b;
} BuiltinLEDTraceRow;
typedef struct {
    BuiltinLEDTraceRow *pages[BUILTIN_LED_TRACE_PAGE_COUNT];
    uint64_t start_us, previous_us, sum_gap_us, advanced_ms;
    uint32_t count, dropped, samples, max_gap_us, commands;
    bool enabled, have_color;
    TaskHandle_t radio_task, timer_task;
    uint32_t previous_radio, previous_timer, previous_ticks;
    LEDFrame previous_color;
} BuiltinLEDTrace;
static BuiltinLEDTrace ledTrace;
static uint32_t animationGeneration;

static BuiltinLEDTraceRow *TraceRowAt(BuiltinLEDTrace *trace, uint32_t index) {
    return &trace->pages[index / BUILTIN_LED_TRACE_PAGE_ROWS][index % BUILTIN_LED_TRACE_PAGE_ROWS];
}

/* Detached pages are freed outside the worker mutex, including partial allocation. */
static void TraceRelease(BuiltinLEDTrace *trace) {
    for (unsigned i = 0; i < BUILTIN_LED_TRACE_PAGE_COUNT; ++i) {
        free(trace->pages[i]);
        trace->pages[i] = NULL;
    }
}

/* Persistent SDK tasks are sampled only during an explicit capture. */
static uint32_t TraceTaskRuntime(TaskHandle_t task) {
#if CONFIG_FREERTOS_USE_TRACE_FACILITY && CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS
    if (task) {
        TaskStatus_t status;
        vTaskGetInfo(task, &status, pdFALSE, eInvalid);
        return (uint32_t)status.ulRunTimeCounter;
    }
#else
    (void)task;
#endif
    return 0;
}

/* Called under the existing LED mutex. Capture is inert unless explicitly enabled. */
static void TraceFrame(uint64_t now, uint32_t delta, uint32_t advanced, int command,
                       LEDFrame color, uint32_t lock_us, uint32_t hardware_us) {
    if (!ledTrace.enabled || !ledTrace.pages[0]) return;
    uint32_t radio = TraceTaskRuntime(ledTrace.radio_task);
    uint32_t timer = TraceTaskRuntime(ledTrace.timer_task);
    uint32_t ticks = xTaskGetTickCount();
    uint32_t radio_delta = radio - ledTrace.previous_radio;
    uint32_t timer_delta = timer - ledTrace.previous_timer;
    uint32_t tick_delta = ticks - ledTrace.previous_ticks;
    ledTrace.previous_radio = radio;
    ledTrace.previous_timer = timer;
    ledTrace.previous_ticks = ticks;
    uint64_t gap = ledTrace.previous_us ? now - ledTrace.previous_us : 0;
    ledTrace.previous_us = now;
    ledTrace.sum_gap_us += gap;
    if (gap > ledTrace.max_gap_us) ledTrace.max_gap_us = gap > UINT32_MAX ? UINT32_MAX : (uint32_t)gap;
    ledTrace.samples++;
    ledTrace.advanced_ms += advanced;
    if (command >= 0) ledTrace.commands++;
    bool changed = !ledTrace.have_color || color.r != ledTrace.previous_color.r ||
        color.g != ledTrace.previous_color.g || color.b != ledTrace.previous_color.b;
    ledTrace.previous_color = color;
    ledTrace.have_color = true;
    if (!changed && command < 0 && gap <= BUILTIN_LED_TRACE_GAP_US) return;
    if (ledTrace.count == BUILTIN_LED_TRACE_CAPACITY) { ledTrace.dropped++; return; }
    *TraceRowAt(&ledTrace, ledTrace.count++) = (BuiltinLEDTraceRow){
        .time_us = now, .output_us = esp_timer_get_time(), .advanced_ms = ledTrace.advanced_ms, .delta_ms = delta,
        .lock_us = lock_us, .hardware_us = hardware_us, .generation = animationGeneration,
        .radio_us = radio_delta, .timer_us = timer_delta, .ticks = tick_delta,
        .animation = builtinLEDService.status.current_animation, .command = command,
        .r = color.r, .g = color.g, .b = color.b};
}

static int cmd_builtin_led_trace(int argc, char **argv) {
    if (!BuiltinLEDInput_Arguments(argc, argv, 2, 2) || (strcmp(argv[1], "start") && strcmp(argv[1], "dump") && strcmp(argv[1], "stop") && strcmp(argv[1], "status"))) {
        printf("Usage: builtin_led_trace <start|stop|dump|status>\n");
        return 1;
    }
    BuiltinLEDTrace allocated = {0};
    bool start = strcmp(argv[1], "start") == 0;
    if (start) {
        if (!builtinLEDService.mutex || xSemaphoreTake(builtinLEDService.mutex, pdMS_TO_TICKS(1000)) != pdTRUE) return 1;
        bool active = ledTrace.pages[0] != NULL;
        xSemaphoreGive(builtinLEDService.mutex);
        if (active) {
            printf("LEDTRACE already active; stop or dump the existing capture first\n");
            return 1;
        }
        for (unsigned i = 0; i < BUILTIN_LED_TRACE_PAGE_COUNT; ++i) {
            allocated.pages[i] = calloc(BUILTIN_LED_TRACE_PAGE_ROWS, sizeof(BuiltinLEDTraceRow));
            if (!allocated.pages[i]) {
                TraceRelease(&allocated);
                printf("LEDTRACE allocation failed\n");
                return 1;
            }
        }
    }
    if (!builtinLEDService.mutex || xSemaphoreTake(builtinLEDService.mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        TraceRelease(&allocated);
        return 1;
    }
    if (strcmp(argv[1], "status") == 0) {
        bool enabled = ledTrace.enabled;
        uint32_t count = ledTrace.count, dropped = ledTrace.dropped;
        xSemaphoreGive(builtinLEDService.mutex);
        printf("LEDTRACE status enabled=%u capacity=%u rows=%lu dropped=%lu\n",
               (unsigned)enabled, (unsigned)BUILTIN_LED_TRACE_CAPACITY,
               (unsigned long)count, (unsigned long)dropped);
        return 0;
    }
    if (start && ledTrace.pages[0]) {
        xSemaphoreGive(builtinLEDService.mutex);
        TraceRelease(&allocated);
        printf("LEDTRACE already active; stop or dump the existing capture first\n");
        return 1;
    }
    BuiltinLEDTrace captured = ledTrace;
    ledTrace = (BuiltinLEDTrace){0};
    if (start) {
        ledTrace = allocated;
        ledTrace.enabled = true;
        ledTrace.start_us = esp_timer_get_time();
    }
#if CONFIG_FREERTOS_USE_TRACE_FACILITY && CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS
    if (start) {
        ledTrace.radio_task = xTaskGetHandle("Zigbee_main");
        ledTrace.timer_task = xTaskGetHandle("esp_timer");
        ledTrace.previous_radio = TraceTaskRuntime(ledTrace.radio_task);
        ledTrace.previous_timer = TraceTaskRuntime(ledTrace.timer_task);
        ledTrace.previous_ticks = xTaskGetTickCount();
    }
#endif
    xSemaphoreGive(builtinLEDService.mutex);
    /* Ownership transfers to this command: the worker cannot touch captured rows.
     * Dumping happens after capture, outside the LED mutex and measurement window. */
    if (strcmp(argv[1], "dump") == 0) {
        printf("LEDTRACE summary start=%llu samples=%lu rows=%lu dropped=%lu commands=%lu advanced=%llu gap_sum=%llu gap_max=%lu\n",
            (unsigned long long)captured.start_us, (unsigned long)captured.samples,
            (unsigned long)captured.count, (unsigned long)captured.dropped,
            (unsigned long)captured.commands, (unsigned long long)captured.advanced_ms,
            (unsigned long long)captured.sum_gap_us, (unsigned long)captured.max_gap_us);
        for (uint32_t i = 0; i < captured.count; ++i) {
            BuiltinLEDTraceRow *row = TraceRowAt(&captured, i);
            printf("LEDTRACE row t=%llu output=%llu advance=%llu dt=%lu lock=%lu hw=%lu gen=%lu anim=%" PRId32 " cmd=%d rgb=%u,%u,%u radio=%lu timer=%lu ticks=%lu\n",
                (unsigned long long)row->time_us, (unsigned long long)row->output_us, (unsigned long long)row->advanced_ms,
                (unsigned long)row->delta_ms, (unsigned long)row->lock_us,
                (unsigned long)row->hardware_us, (unsigned long)row->generation,
                row->animation, row->command, row->r, row->g, row->b,
                (unsigned long)row->radio_us, (unsigned long)row->timer_us, (unsigned long)row->ticks);
        }
    }
    TraceRelease(&captured);
    printf("LEDTRACE %s done\n", argv[1]);
    return 0;
}

/* Forward declarations */
static esp_err_t RegisterConsoleCommands(void);
static void SettingsChangedHandler(BusEvent event);

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

/**
 * @brief Publishes a BuiltinLED event to the system EventBus.
 *
 * The current BuiltinLEDStatus is copied into the BusEvent payload.
 * Must be called while holding the service mutex so the status snapshot
 * is consistent.
 *
 * Args:
 *     event_name: Null-terminated event name string (one of EVENT_BUILTIN_LED_*).
 *     type:       Event severity type.
 */
static void PublishLEDEvent(const char *event_name, EventType type) {
    if (!builtinLEDService.eventBus || !builtinLEDService.eventBus->Publish) return;

    builtinLEDService.status.enabled = !builtinLEDService.disabled;
    BusEvent event = {0};
    event.eventName = event_name;
    event.type      = type;
    event.data      = &builtinLEDService.status;
    event.dataSize  = sizeof(BuiltinLEDStatus);

    /* Copy status into the inline buffer so the pointer is never dangling. */
    if (event.dataSize <= BUS_EVENT_INLINE_DATA_SIZE) {
        memcpy(event.inlineData, &builtinLEDService.status, event.dataSize);
        event.data = event.inlineData;
    }

    builtinLEDService.eventBus->Publish(event);
}

/**
 * @brief Registers a plugin directly into the plugin array (bypasses queue).
 *
 * Used internally during Initialize() before the animation task is running.
 * Must be called while holding the service mutex.
 *
 * Args:
 *     plugin: Pointer to the AnimationPlugin to register. Ignored if NULL,
 *             if the plugin table is full, or if a plugin with the same type
 *             is already registered.
 */
static void RegisterPlugin_Direct(AnimationPlugin *plugin) {
    if (!plugin) {
        return;
    }
    if (builtinLEDService.plugin_count >= BUILTIN_LED_MAX_PLUGINS) {
        LOGW("RegisterPlugin_Direct", "Plugin table full, cannot register '%s'", plugin->name);
        return;
    }
    for (uint8_t i = 0; i < builtinLEDService.plugin_count; i++) {
        if (builtinLEDService.plugins[i] && builtinLEDService.plugins[i]->type == plugin->type) {
            LOGW("RegisterPlugin_Direct", "Duplicate plugin type %d ('%s'), skipping",
                 plugin->type, plugin->name);
            return;
        }
    }
    builtinLEDService.plugins[builtinLEDService.plugin_count++] = plugin;
    builtinLEDService.status.registered_plugins_count = builtinLEDService.plugin_count;
}

/**
 * @brief Finds the registered plugin for a given animation type.
 *
 * Must be called while holding the service mutex.
 *
 * Args:
 *     type: The animation type to look up.
 *
 * Returns:
 *     Pointer to the matching AnimationPlugin, or NULL if not found.
 */
static AnimationPlugin *FindPlugin(BuiltinLEDAnimationType type) {
    for (uint8_t i = 0; i < builtinLEDService.plugin_count; i++) {
        if (builtinLEDService.plugins[i]->type == type) {
            return builtinLEDService.plugins[i];
        }
    }
    return NULL;
}

/**
 * @brief Applies global brightness scaling to a plugin-produced color.
 *
 * Plugins output colors at BUILTIN_LED_DEFAULT_BRIGHTNESS scale. This
 * function linearly scales the result to the current max_brightness.
 *
 * Args:
 *     color: Raw color from the plugin's Update() call.
 *
 * Returns:
 *     Brightness-adjusted LEDColor, clamped to [0, 255] per channel.
 */
static LEDColor ApplyBrightnessAt(LEDColor color, uint8_t brightness) {
    uint32_t half = BUILTIN_LED_DEFAULT_BRIGHTNESS / 2U;
    uint32_t r = ((uint32_t)color.r * brightness + half)
                 / BUILTIN_LED_DEFAULT_BRIGHTNESS;
    uint32_t g = ((uint32_t)color.g * brightness + half)
                 / BUILTIN_LED_DEFAULT_BRIGHTNESS;
    uint32_t b = ((uint32_t)color.b * brightness + half)
                 / BUILTIN_LED_DEFAULT_BRIGHTNESS;

    return (LEDColor){
        .r = (r > 255U) ? 255U : (uint8_t)r,
        .g = (g > 255U) ? 255U : (uint8_t)g,
        .b = (b > 255U) ? 255U : (uint8_t)b,
    };
}

/* Smooth catalog effects quantize once at the actual output brightness.
 * Legacy/custom plugins retain their nominal-scale contract. The descriptor
 * copy borrows worker-owned context; it never changes registered metadata. */
static LEDColor RenderAnimationAt(AnimationPlugin *plugin, uint32_t elapsed_ms, uint8_t brightness) {
    switch (plugin->type) {
        case RAINBOW_FLOW: {
            /* Quantize once at full resolution, then match PWM channel transfer.
             * Do not gamma-correct the shared plugin: external previews also use it. */
            AnimationPlugin renderer = *plugin;
            renderer.render_brightness = 255;
            LEDColor color = renderer.Update(&renderer, elapsed_ms);
#if RGBLED_GAMMA_ENABLED
            color = (LEDColor){LEDColor_ApplyOutputCurve(color.r), LEDColor_ApplyOutputCurve(color.g), LEDColor_ApplyOutputCurve(color.b)};
#endif
            return (LEDColor){
                (uint32_t)color.r * brightness / 255U,
                (uint32_t)color.g * brightness / 255U,
                (uint32_t)color.b * brightness / 255U};
        }
        case BREATHING_RED:
        case BREATHING_GREEN:
        case FADE_BLUE:
        case GENERIC_BREATHING:
        case GENERIC_FADE:
        case GENERIC_CANDLE: {
            AnimationPlugin renderer = *plugin;
            renderer.render_brightness = brightness ? brightness : 1;
            LEDColor color = renderer.Update(&renderer, elapsed_ms);
            return brightness ? color : (LEDColor){0};
        }
        default: return ApplyBrightnessAt(plugin->Update(plugin, elapsed_ms), brightness);
    }
}

static LEDColor RenderAnimation(AnimationPlugin *plugin, uint32_t elapsed_ms) {
    return RenderAnimationAt(plugin, elapsed_ms, builtinLEDService.max_brightness);
}

/**
 * @brief Handles the duration countdown for timed animations.
 *
 * When the remaining time reaches zero, the default animation is restored.
 * Must be called while holding the service mutex.
 *
 * Args:
 *     delta_ms: Milliseconds elapsed since the last task tick.
 */
static void UpdateDurationCountdown(uint32_t delta_ms) {
    if (builtinLEDService.status.remaining_ms == 0) {
        return;
    }

    if (builtinLEDService.status.remaining_ms > delta_ms) {
        builtinLEDService.status.remaining_ms -= delta_ms;
    } else {
        builtinLEDService.connectionNoticeActive = false;
        /* Timed animation expired â€” fall back to the default. */
        builtinLEDService.status.remaining_ms       = 0;
        builtinLEDService.status.current_animation  = BaseAnimation();
        builtinLEDService.status.current_priority   = BUILTIN_LED_PRIO_LOW;
        builtinLEDService.status.is_on =
            (BaseAnimation() != BUILTIN_LED_ANIM_NONE);

        /* Reset the restored animation so it starts from frame 0. */
        AnimationPlugin *p = FindPlugin(BaseAnimation());
        if (p) { p->Reset(p); animationGeneration++; }

        PublishLEDEvent(EVENT_BUILTIN_LED_ANIM_CHANGED, EVENT_TYPE_INFO);
        LOGD("UpdateDurationCountdown", "Timed animation expired, restored default (type=%d)",
             builtinLEDService.defaultAnimType);
    }
}

/* Worker-only informational overlay. Duplicate connected/pairing-stop events
 * coalesce and never restart its deadline. Startup playback has precedence. */
static void StartConnectionNotice(void) {
    if (builtinLEDService.disabled || builtinLEDService.connectionNoticeActive) return;
    builtinLEDService.connectionNoticeActive = true;
    builtinLEDService.status.current_animation = SOLID_GREEN;
    builtinLEDService.status.current_priority = BUILTIN_LED_PRIO_HIGH;
    builtinLEDService.status.remaining_ms = BUILTIN_LED_CONNECTED_NOTICE_MS;
    builtinLEDService.status.is_on = true;
    AnimationPlugin *plugin = FindPlugin(SOLID_GREEN);
    if (plugin) { plugin->Reset(plugin); animationGeneration++; }
    PublishLEDEvent(EVENT_BUILTIN_LED_ANIM_CHANGED, EVENT_TYPE_INFO);
    LOGI("ConnectionNotice", "Green confirmation started after startup=%u", (unsigned)builtinLEDService.startupActive);
}

/**
 * @brief Processes a single command dequeued from the command queue.
 *
 * Updates service state only. Hardware I/O is signalled via the
 * pending_clear / pending_pixel output arguments so that the caller can
 * perform hardware writes under the lifecycle mutex.
 *
 * Must be called while holding the service mutex.
 *
 * Args:
 *     cmd:           Pointer to the command to process.
 *     out_clear:     Set to true if the LED should be cleared after this call.
 *     out_pixel:     Set to true if the LED should be refreshed with out_color.
 *     out_color:     Color to write when out_pixel is true.
 */
static void ProcessCommand(const BuiltinLEDCmd *cmd,
                           bool *out_clear, bool *out_pixel, LEDColor *out_color) {
    *out_clear = false;
    *out_pixel = false;
    *out_color = (LEDColor){0, 0, 0};

    if (builtinLEDService.disabled &&
        (cmd->type == CMD_SET_ANIMATION || cmd->type == CMD_TURN_ON)) return;

    if (cmd->type==CMD_STOP_ANIMATION || cmd->type==CMD_TURN_OFF || cmd->type==CMD_TURN_ON ||
        (cmd->type==CMD_SET_ANIMATION && cmd->data.set_anim.priority>=BUILTIN_LED_PRIO_HIGH))
        builtinLEDService.previewActive=builtinLEDService.brightnessPreviewActive=false;

    if (cmd->type == CMD_TURN_OFF || cmd->type == CMD_STOP_ANIMATION || cmd->type == CMD_TURN_ON ||
        (cmd->type == CMD_SET_ANIMATION && cmd->data.set_anim.priority == BUILTIN_LED_PRIO_HIGH)) {
        builtinLEDService.connectionNoticePending = false;
        builtinLEDService.connectionNoticeActive = false;
    }
    switch (cmd->type) {
        case CMD_CONNECTION_NOTICE:
            if (builtinLEDService.disabled) break;
            if (builtinLEDService.startupActive) {
                builtinLEDService.connectionNoticePending = true;
                LOGD("ConnectionNotice", "Deferred until startup completes");
            } else {
                StartConnectionNotice();
            }
            break;
        case CMD_CANCEL_CONNECTION_NOTICE:
            builtinLEDService.connectionNoticePending = false;
            if (builtinLEDService.connectionNoticeActive)
                UpdateDurationCountdown(builtinLEDService.status.remaining_ms);
            break;

        case CMD_PREVIEW_BRIGHTNESS:
            if (builtinLEDService.disabled || builtinLEDService.status.current_priority>BUILTIN_LED_PRIO_LOW) break;
            builtinLEDService.previewActive=false;
            builtinLEDService.brightnessPreviewActive=true;
            builtinLEDService.previewBrightness=cmd->data.brightness_preview.brightness;
            builtinLEDService.brightnessPreviewStarted=(uint32_t)(esp_timer_get_time()/1000ULL);
            builtinLEDService.brightnessPreviewDuration=cmd->data.brightness_preview.duration_ms;
            LEDTransition_Reset(&builtinLEDService.transition,builtinLEDService.renderedFrame);
            LEDTransition_Start(&builtinLEDService.transition,builtinLEDService.brightnessPreviewStarted,LED_TRANSITION_DEFAULT_MS);
            break;

        case CMD_PREVIEW_ANIMATION: {
            if (builtinLEDService.disabled || builtinLEDService.status.current_priority>BUILTIN_LED_PRIO_LOW) break;
            BuiltinLEDAnimationType type=cmd->data.set_anim.anim_type;
            if ((int)type==255) type=BUILTIN_LED_ANIM_NONE;
            if (type!=BUILTIN_LED_ANIM_NONE && LEDAnimations_CreateInstance(type,&builtinLEDService.previewInstance)!=ESP_OK) break;
            builtinLEDService.brightnessPreviewActive=false;
            builtinLEDService.previewAnimated=type!=BUILTIN_LED_ANIM_NONE;
            builtinLEDService.previewStarted=builtinLEDService.previewClock=(uint32_t)(esp_timer_get_time()/1000ULL);
            builtinLEDService.previewDuration=cmd->data.set_anim.duration_ms;
            LEDTransition_Reset(&builtinLEDService.previewTransition,builtinLEDService.renderedFrame);
            LEDTransition_Start(&builtinLEDService.previewTransition,builtinLEDService.previewStarted,LED_TRANSITION_DEFAULT_MS);
            builtinLEDService.previewActive=true;
            break;
        }

        case CMD_INIT_MODULE:
            builtinLEDService.pin            = (uint8_t)cmd->data.init.pin;
            builtinLEDService.max_brightness = cmd->data.init.max_brightness;
            builtinLEDService.status.brightness = cmd->data.init.max_brightness;
            LOGI("ProcessCommand", "Module reconfigured: pin=%d brightness=%d",
                 builtinLEDService.pin, builtinLEDService.max_brightness);
            break;

        case CMD_SET_ANIMATION:
            /* System INITIALIZING/READY indications arrive after this worker
             * has started. They must not replace the owned startup effect.
             * Keep runtime selection current, while fault/pairing can preempt. */
            if (builtinLEDService.startupActive && cmd->data.set_anim.priority < BUILTIN_LED_PRIO_HIGH) {
                if (cmd->data.set_anim.priority == BUILTIN_LED_PRIO_LOW)
                    builtinLEDService.defaultAnimType = cmd->data.set_anim.anim_type;
                break;
            }
            if (cmd->data.set_anim.priority >= builtinLEDService.status.current_priority) {
                builtinLEDService.status.current_animation = cmd->data.set_anim.anim_type;
                builtinLEDService.status.current_priority  = cmd->data.set_anim.priority;
                builtinLEDService.status.remaining_ms = cmd->data.set_anim.duration_ms;
                builtinLEDService.status.is_on =
                    (cmd->data.set_anim.anim_type != BUILTIN_LED_ANIM_NONE);

                if (cmd->data.set_anim.priority == BUILTIN_LED_PRIO_LOW) {
                    builtinLEDService.defaultAnimType = cmd->data.set_anim.anim_type;
                }

                /* Reset plugin to ensure it starts from frame 0. */
                AnimationPlugin *p = FindPlugin(cmd->data.set_anim.anim_type);
                if (p) { p->Reset(p); animationGeneration++; }

                PublishLEDEvent(EVENT_BUILTIN_LED_ANIM_CHANGED, EVENT_TYPE_INFO);
                LOGD("ProcessCommand", "Animation set: type=%d priority=%d duration=%lu ms",
                     cmd->data.set_anim.anim_type,
                     cmd->data.set_anim.priority,
                     (unsigned long)cmd->data.set_anim.duration_ms);
            } else {
                LOGD("ProcessCommand",
                     "Animation ignored (requested priority %d < current %d)",
                     cmd->data.set_anim.priority,
                     builtinLEDService.status.current_priority);
            }
            break;

        case CMD_STOP_ANIMATION:
            builtinLEDService.status.current_animation = BUILTIN_LED_ANIM_NONE;
            builtinLEDService.status.current_priority  = BUILTIN_LED_PRIO_LOW;
            builtinLEDService.status.remaining_ms      = 0;
            builtinLEDService.status.is_on             = false;
            *out_clear = true;
            PublishLEDEvent(EVENT_BUILTIN_LED_STATE_CHANGED, EVENT_TYPE_INFO);
            LOGI("ProcessCommand", "Animation stopped");
            break;

        case CMD_TURN_ON:
            builtinLEDService.status.current_animation = BUILTIN_LED_ANIM_NONE;
            builtinLEDService.status.current_priority  = BUILTIN_LED_PRIO_LOW;
            builtinLEDService.status.remaining_ms      = 0;
            builtinLEDService.status.is_on             = true;
            *out_pixel = true;
            out_color->r = builtinLEDService.max_brightness;
            out_color->g = builtinLEDService.max_brightness;
            out_color->b = builtinLEDService.max_brightness;
            PublishLEDEvent(EVENT_BUILTIN_LED_STATE_CHANGED, EVENT_TYPE_INFO);
            LOGI("ProcessCommand", "LED turned on (static white)");
            break;

        case CMD_TURN_OFF:
            builtinLEDService.status.current_animation = BUILTIN_LED_ANIM_NONE;
            builtinLEDService.status.current_priority  = BUILTIN_LED_PRIO_LOW;
            builtinLEDService.status.remaining_ms      = 0;
            builtinLEDService.status.is_on             = false;
            *out_clear = true;
            PublishLEDEvent(EVENT_BUILTIN_LED_STATE_CHANGED, EVENT_TYPE_INFO);
            LOGI("ProcessCommand", "LED turned off");
            break;

        case CMD_REGISTER_PLUGIN:
            /* Runtime plugin registration (after service start). */
            RegisterPlugin_Direct(cmd->data.reg_plugin.plugin);
            break;

        case CMD_SET_BRIGHTNESS:
            if (builtinLEDService.brightnessPreviewActive) {
                builtinLEDService.brightnessPreviewActive=false;
                LEDTransition_Reset(&builtinLEDService.transition,builtinLEDService.renderedFrame);
            }
            builtinLEDService.max_brightness          = cmd->data.set_brightness.brightness;
            builtinLEDService.status.brightness       = cmd->data.set_brightness.brightness;
            PublishLEDEvent(EVENT_BUILTIN_LED_BRIGHTNESS_CHANGED, EVENT_TYPE_INFO);
            LOGI("ProcessCommand", "Brightness updated to %d", cmd->data.set_brightness.brightness);
            break;

        case CMD_SETTINGS_CHANGED: {
            if (builtinLEDService.brightnessPreviewActive &&
                (cmd->data.settings.brightness!=builtinLEDService.max_brightness || cmd->data.settings.disabled)) {
                builtinLEDService.brightnessPreviewActive=false;
                LEDTransition_Reset(&builtinLEDService.transition,builtinLEDService.renderedFrame);
            }
            bool was_disabled = builtinLEDService.disabled;
            bool follows_saved_default = builtinLEDService.defaultAnimType == builtinLEDService.configuredDefaultAnim;
            builtinLEDService.disabled = cmd->data.settings.disabled;
            builtinLEDService.max_brightness = cmd->data.settings.brightness;
            builtinLEDService.status.brightness = cmd->data.settings.brightness;
            if (follows_saved_default || was_disabled || builtinLEDService.disabled)
                builtinLEDService.defaultAnimType = cmd->data.settings.default_anim;
            builtinLEDService.configuredDefaultAnim = cmd->data.settings.default_anim;
            builtinLEDService.startupAnimType = cmd->data.settings.startup_anim;
            if (builtinLEDService.disabled) {
                builtinLEDService.connectionNoticePending = false;
                builtinLEDService.connectionNoticeActive = false;
                builtinLEDService.status.current_animation = BUILTIN_LED_ANIM_NONE;
                builtinLEDService.status.current_priority = BUILTIN_LED_PRIO_LOW;
                builtinLEDService.status.remaining_ms = 0;
                builtinLEDService.status.is_on = false;
                *out_clear = true;
                if (!was_disabled) PublishLEDEvent(EVENT_BUILTIN_LED_STATE_CHANGED, EVENT_TYPE_INFO);
            } else if ((was_disabled || builtinLEDService.status.current_animation != BaseAnimation()) &&
                       builtinLEDService.status.current_priority == BUILTIN_LED_PRIO_LOW) {
                builtinLEDService.status.remaining_ms = 0;
                builtinLEDService.status.current_animation = BaseAnimation();
                builtinLEDService.status.is_on =
                    (BaseAnimation() != BUILTIN_LED_ANIM_NONE);

                AnimationPlugin *p = FindPlugin(BaseAnimation());
                if (p) { p->Reset(p); animationGeneration++; }

                PublishLEDEvent(EVENT_BUILTIN_LED_ANIM_CHANGED, EVENT_TYPE_INFO);
            }
            LOGD("ProcessCommand", "Default animation updated to type=%d",
                 cmd->data.settings.default_anim);
            break;
        }
        case CMD_SET_STARTUP: {
            builtinLEDService.startupActive = cmd->data.startup.active;
            if (!builtinLEDService.startupActive && builtinLEDService.connectionNoticePending) {
                builtinLEDService.connectionNoticePending = false;
                if (builtinLEDService.status.current_priority == BUILTIN_LED_PRIO_LOW)
                    StartConnectionNotice();
            }
            BuiltinLEDAnimationType selected = BaseAnimation();
            if (builtinLEDService.status.current_priority != BUILTIN_LED_PRIO_LOW ||
                (builtinLEDService.status.current_animation == selected &&
                 (selected != BUILTIN_LED_ANIM_NONE || !builtinLEDService.status.is_on))) break;
            builtinLEDService.status.current_animation = selected;
            builtinLEDService.status.remaining_ms = 0;
            builtinLEDService.status.is_on = selected != BUILTIN_LED_ANIM_NONE;
            AnimationPlugin *plugin = FindPlugin(selected);
            if (plugin) { plugin->Reset(plugin); animationGeneration++; }
            if (selected == BUILTIN_LED_ANIM_NONE) *out_clear = true;
            PublishLEDEvent(EVENT_BUILTIN_LED_ANIM_CHANGED, EVENT_TYPE_INFO);
            break;
        }
    }
}

// ---------------------------------------------------------------------------
// Animation task
// ---------------------------------------------------------------------------

/**
 * @brief FreeRTOS task â€” processes commands and drives LED animation frames.
 *
 * Design:
 *   - Blocks on the command queue with a timeout equal to BUILTIN_LED_FRAME_INTERVAL_MS
 *     when an animation is active, or portMAX_DELAY when the LED is idle.
 *   - State mutations and color computation are performed under the service mutex.
 *   - Hardware I/O shares the lifecycle mutex with Stop to protect driver resources.
 *
 * Args:
 *     pvParameters: Unused task parameter.
 */
static esp_err_t BuiltinLED_PostCommand(const BuiltinLEDCmd *cmd) {
    if (!cmd || !builtinLEDService.controlMutex ||
        xSemaphoreTake(builtinLEDService.controlMutex, 0) != pdTRUE) return ESP_ERR_INVALID_STATE;
    ServiceState state = builtinLEDService.state;
    esp_err_t result = ESP_ERR_INVALID_STATE;
    if ((state == SERVICE_STATE_INITIALIZED || state == SERVICE_STATE_STARTED) && builtinLEDService.cmd_queue)
        result = xQueueSend(builtinLEDService.cmd_queue, cmd, 0) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
    xSemaphoreGive(builtinLEDService.controlMutex);
    return result;
}

static void BuiltinLED_AnimTask(void *pvParameters) {
    (void)pvParameters;

    builtinLEDService.previewActive=false;
    uint64_t last_time   = esp_timer_get_time();
    TickType_t ticks_to_wait = builtinLEDService.status.is_on &&
        builtinLEDService.status.current_animation != BUILTIN_LED_ANIM_NONE
        ? pdMS_TO_TICKS(BUILTIN_LED_FRAME_INTERVAL_MS) : portMAX_DELAY;

    /* Render state and hardware access share the lifecycle mutex. */
    bool     pending_pixel = false;
    bool     pending_clear = false;
    LEDColor pending_color = {0, 0, 0};

    LOGI("BuiltinLED_AnimTask", "Task started");

    while (1) {
        /* Acknowledge stop only outside plugin, state and driver ownership. */
        WatchdogWorker_Checkpoint(&builtinLEDService.worker, NULL, 0);
        TickType_t controlWait = pdMS_TO_TICKS(BUILTIN_LED_IDLE_POLL_MS);
        if (!controlWait) controlWait = 1;
        TickType_t wait = ticks_to_wait < controlWait ? ticks_to_wait : controlWait;
        if (!wait) wait = 1;
        BuiltinLEDCmd cmd;
        BaseType_t cmd_received =
            xQueueReceive(builtinLEDService.cmd_queue, &cmd, wait);
        if (cmd_received != pdTRUE && ticks_to_wait == portMAX_DELAY) continue;

        uint64_t wait_started = esp_timer_get_time();

        pending_pixel = false;
        pending_clear = false;
        uint32_t advanced_ms = 0;

        /* â”€â”€ Critical section: state updates and color computation â”€â”€ */
        if (xSemaphoreTake(builtinLEDService.mutex, portMAX_DELAY) != pdTRUE) {
            atomic_store(&builtinLEDService.driverError, ESP_ERR_TIMEOUT);
            ticks_to_wait = pdMS_TO_TICKS(BUILTIN_LED_FRAME_INTERVAL_MS);
            continue;
        }
        uint64_t now = esp_timer_get_time();
        uint32_t lock_us = (uint32_t)(now - wait_started);
        /* Retain sub-millisecond time across frames, including command wakes.
         * Sample after acquiring the lock so contention is included this frame. */
        uint64_t elapsed_ms = (now - last_time) / 1000ULL;
        uint32_t delta_ms = elapsed_ms > UINT32_MAX ? UINT32_MAX : (uint32_t)elapsed_ms;
        last_time += (uint64_t)delta_ms * 1000ULL;
        uint32_t animation_ms = delta_ms;
        uint32_t remaining = builtinLEDService.status.remaining_ms;
        if (remaining != 0 && delta_ms >= remaining)
            animation_ms -= remaining;
        UpdateDurationCountdown(delta_ms);
        uint32_t generation_before_command = animationGeneration;

        if (cmd_received == pdTRUE) {
            BuiltinLEDStatus previous = builtinLEDService.status;
            ProcessCommand(&cmd, &pending_clear, &pending_pixel, &pending_color);
            if (pending_clear || pending_pixel ||
                previous.brightness != builtinLEDService.status.brightness ||
                previous.current_animation != builtinLEDService.status.current_animation ||
                previous.is_on != builtinLEDService.status.is_on) {
                LEDTransition_Start(&builtinLEDService.transition, (uint32_t)(now / 1000ULL), LED_TRANSITION_DEFAULT_MS);
                if (builtinLEDService.status.is_on &&
                    builtinLEDService.status.current_animation == BUILTIN_LED_ANIM_NONE && !pending_clear) {
                    pending_color = ApplyBrightnessAt((LEDColor){BUILTIN_LED_DEFAULT_BRIGHTNESS, BUILTIN_LED_DEFAULT_BRIGHTNESS, BUILTIN_LED_DEFAULT_BRIGHTNESS}, builtinLEDService.max_brightness);
                    pending_pixel = true;
                }
            }
        }
        uint32_t previewNow=(uint32_t)(now/1000ULL);
        bool brightnessPreviewEnded=false;
        if (builtinLEDService.brightnessPreviewActive &&
            (builtinLEDService.disabled || builtinLEDService.status.current_priority>BUILTIN_LED_PRIO_LOW ||
             (uint32_t)(previewNow-builtinLEDService.brightnessPreviewStarted)>=builtinLEDService.brightnessPreviewDuration)) {
            builtinLEDService.brightnessPreviewActive=false;
            brightnessPreviewEnded=true;
            LEDTransition_Reset(&builtinLEDService.transition,builtinLEDService.renderedFrame);
            LEDTransition_Start(&builtinLEDService.transition,previewNow,LED_TRANSITION_DEFAULT_MS);
        }
        uint8_t renderBrightness=builtinLEDService.brightnessPreviewActive ? builtinLEDService.previewBrightness : builtinLEDService.max_brightness;
        if (builtinLEDService.status.is_on && builtinLEDService.status.current_animation==BUILTIN_LED_ANIM_NONE &&
            (builtinLEDService.brightnessPreviewActive || brightnessPreviewEnded)) {
            pending_color=ApplyBrightnessAt((LEDColor){BUILTIN_LED_DEFAULT_BRIGHTNESS,BUILTIN_LED_DEFAULT_BRIGHTNESS,BUILTIN_LED_DEFAULT_BRIGHTNESS},renderBrightness);
            pending_pixel=true;
        }
        /* A newly selected animation starts now; elapsed time belongs to the
         * previous selection. Unrelated commands must not pause playback. */
        if (generation_before_command != animationGeneration) animation_ms = 0;
        {
            if (builtinLEDService.status.is_on &&
                builtinLEDService.status.current_animation != BUILTIN_LED_ANIM_NONE) {

                AnimationPlugin *plugin =
                    FindPlugin(builtinLEDService.status.current_animation);

                if (plugin) {
                    advanced_ms = animation_ms;
                    pending_color = RenderAnimationAt(plugin, animation_ms, renderBrightness);
                    pending_pixel = true;
                } else {
                    LOGW("BuiltinLED_AnimTask",
                         "No plugin for active animation type %d",
                         builtinLEDService.status.current_animation);
                }
            }
        }

        /* Expiry can restore the disabled default without a command. Never
         * retain the last lit frame after that state transition. */
        if (!builtinLEDService.status.is_on) pending_clear = true;
        if (pending_pixel) {
            builtinLEDService.targetFrame = (LEDFrame){pending_color.r, pending_color.g, pending_color.b, 255};
        } else if (pending_clear) {
            builtinLEDService.targetFrame = (LEDFrame){0};
        }
        LEDFrame rendered = LEDTransition_Sample(&builtinLEDService.transition,
            builtinLEDService.targetFrame, (uint32_t)(now / 1000ULL));

        uint32_t renderNow=(uint32_t)(now/1000ULL);
        if (builtinLEDService.previewActive) {
            bool allowed=!builtinLEDService.disabled && builtinLEDService.status.current_priority==BUILTIN_LED_PRIO_LOW;
            if (!allowed || (uint32_t)(renderNow-builtinLEDService.previewStarted)>=builtinLEDService.previewDuration) {
                builtinLEDService.previewActive=false;
                if (allowed) {
                    LEDTransition_Reset(&builtinLEDService.transition,builtinLEDService.renderedFrame);
                    LEDTransition_Start(&builtinLEDService.transition,renderNow,LED_TRANSITION_DEFAULT_MS);
                    rendered=LEDTransition_Sample(&builtinLEDService.transition,builtinLEDService.targetFrame,renderNow);
                }
            } else {
                LEDColor color={0};
                if (builtinLEDService.previewAnimated)
                    color=RenderAnimation(&builtinLEDService.previewInstance.plugin,renderNow-builtinLEDService.previewClock);
                builtinLEDService.previewClock=renderNow;
                rendered=LEDTransition_Sample(&builtinLEDService.previewTransition,(LEDFrame){color.r,color.g,color.b,255},renderNow);
            }
        }
        builtinLEDService.renderedFrame=rendered;

        /* Determine next tick timeout. */
        ticks_to_wait =
            (builtinLEDService.previewActive || builtinLEDService.brightnessPreviewActive || builtinLEDService.transition.active || (builtinLEDService.status.is_on &&
             builtinLEDService.status.current_animation != BUILTIN_LED_ANIM_NONE))
                ? pdMS_TO_TICKS(LED_TRANSITION_FRAME_MS)
                : portMAX_DELAY;

        /* The stop checkpoint is outside this state/driver ownership region. */
        uint64_t hardware_start = esp_timer_get_time();
        esp_err_t render_result = ESP_ERR_INVALID_STATE;
        if (builtinLEDService.led_strip) {
            render_result = led_strip_set_pixel(builtinLEDService.led_strip, 0, rendered.r, rendered.g, rendered.b);
            if (render_result == ESP_OK) render_result = led_strip_refresh(builtinLEDService.led_strip);
        }
        esp_err_t previous_error = atomic_exchange(&builtinLEDService.driverError, render_result);
        if (render_result != ESP_OK) {
            /* Retry static output too, without resetting the animation phase. */
            ticks_to_wait = pdMS_TO_TICKS(BUILTIN_LED_FRAME_INTERVAL_MS);
            if (previous_error != render_result) {
                LOGE("BuiltinLED_AnimTask", "LED render failed: 0x%x", (unsigned)render_result);
                PublishLEDEvent(EVENT_BUILTIN_LED_ERROR, EVENT_TYPE_ERROR);
            }
        }
        TraceFrame(now, delta_ms, advanced_ms, cmd_received == pdTRUE ? (int)cmd.type : -1,
            rendered, lock_us, (uint32_t)(esp_timer_get_time() - hardware_start));
#if defined(RACOOL_LED_PHASE_DIAGNOSTIC)
        static uint32_t phase_log_ms;
        static uint64_t previous_output_us;
        static uint32_t maximum_gap_us;
        uint64_t output_us=esp_timer_get_time();
        uint64_t gap_us=previous_output_us ? output_us-previous_output_us : 0;
        previous_output_us=output_us;
        if(gap_us>maximum_gap_us) maximum_gap_us=gap_us>UINT32_MAX ? UINT32_MAX : (uint32_t)gap_us;
        uint32_t phase_now = (uint32_t)(now / 1000ULL);
        if (phase_now < 24000U && (uint32_t)(phase_now - phase_log_ms) >= 200U) {
            phase_log_ms = phase_now;
            ESP_LOGI("LEDPHASE", "builtin t=%lu startup=%u anim=%d remaining=%lu rgb=%u,%u,%u result=%d output=%llu gap_max=%lu lock=%lu hw=%lu",
                (unsigned long)phase_now, (unsigned)builtinLEDService.startupActive,
                (int)builtinLEDService.status.current_animation, (unsigned long)builtinLEDService.status.remaining_ms,
                rendered.r, rendered.g, rendered.b, (int)render_result,
                (unsigned long long)output_us,(unsigned long)maximum_gap_us,
                (unsigned long)lock_us,(unsigned long)(esp_timer_get_time()-hardware_start));
            maximum_gap_us=0;
        }
#endif
        xSemaphoreGive(builtinLEDService.mutex);
    }
}

// ---------------------------------------------------------------------------
// System event handler
// ---------------------------------------------------------------------------

/**
 * @brief EventBus subscriber called when system configuration changes.
 *
 * Reads the new settings from ConfigurationManager and posts a
 * CMD_SETTINGS_CHANGED command to the animation task queue.
 *
 * Args:
 *     event: The bus event received (eventName must be EVENT_CONFIG_CHANGED).
 */
static void SettingsChangedHandler(BusEvent event) {
    if (strcmp(event.eventName, EVENT_CONFIG_CHANGED) != 0) return;

    LOGD("SettingsChangedHandler", "Config changed, updating BuiltinLED default animation");

    IConfigurationManager *configMgr =
        (IConfigurationManager *)ServiceLocator_GetService(SVC_NAME_CONFIGURATION_MANAGER);
    if (!configMgr) return;

    SystemSettings snapshot;
    if (!configMgr->GetSnapshot || configMgr->GetSnapshot(configMgr, &snapshot) != ESP_OK) return;
    const SystemSettings *settings = &snapshot;

    BuiltinLEDAnimationType newDefault;

    if (settings->hardware.builtin_led_anim_mode == 1) {
        /* Automatic mode â€” idle animation is the default condition animation. */
        int raw = settings->hardware.builtin_led_idle_anim;
        newDefault = (raw >= 0 && raw < BUILTIN_LED_ANIM_COUNT)
                         ? (BuiltinLEDAnimationType)raw
                         : BUILTIN_LED_ANIM_NONE;
    } else {
        newDefault = BUILTIN_LED_ANIM_NONE;
    }

    BuiltinLEDCmd cmd = {
        .type                   = CMD_SETTINGS_CHANGED,
        .data.settings.default_anim = newDefault,
        .data.settings.startup_anim = settings->hardware.builtin_led_startup_anim < BUILTIN_LED_ANIM_COUNT
            ? (BuiltinLEDAnimationType)settings->hardware.builtin_led_startup_anim : BUILTIN_LED_ANIM_NONE,
        .data.settings.brightness = settings->hardware.builtin_led_brightness,
        .data.settings.disabled = settings->hardware.builtin_led_anim_mode == 0,
    };
    if (BuiltinLED_PostCommand(&cmd) != ESP_OK)
        LOGE("SettingsChangedHandler", "Settings update queue is full");
}

/**
 * @brief EventBus subscriber called when Zigbee network status changes.
 */
static void ZigbeeEventHandler(BusEvent event) {
    if (!event.eventName) return;
    if (strcmp(event.eventName, "zigbee_pairing_start") == 0) {
        LOGI("BuiltinLED", "Zigbee pairing started -> switching LED to HEARTBEAT_RED (HIGH PRIO)");
        BuiltinLED_SetAnimation(&builtinLEDService.base, HEARTBEAT_RED, BUILTIN_LED_PRIO_HIGH, 0);
    } else if (strcmp(event.eventName, "zigbee_connected") == 0 ||
               strcmp(event.eventName, "zigbee_pairing_stop") == 0) {
        BuiltinLEDCmd cmd = {.type = CMD_CONNECTION_NOTICE};
        esp_err_t result = BuiltinLED_PostCommand(&cmd);
        if (result != ESP_OK) LOGW("ConnectionNotice", "Connection notice admission failed: %d", (int)result);
    } else if (strcmp(event.eventName, "zigbee_disconnected") == 0) {
        BuiltinLEDCmd cmd = {.type = CMD_CANCEL_CONNECTION_NOTICE};
        esp_err_t result = BuiltinLED_PostCommand(&cmd);
        if (result != ESP_OK) LOGW("ConnectionNotice", "Notice cancellation admission failed: %d", (int)result);
    } else if (strcmp(event.eventName, "zigbee_rejoin_exhausted") == 0) {
        LOGW("BuiltinLED", "Zigbee pairing/rejoin exhausted -> HEARTBEAT_RED");
        BuiltinLED_SetAnimation(&builtinLEDService.base, HEARTBEAT_RED, BUILTIN_LED_PRIO_HIGH, 5000);
    } else if (strcmp(event.eventName, "builtin_led_enable") == 0) {
        // HA turned ON the LED switch â€” resume the default condition-driven animation.
        // The UIController will immediately overwrite this with the correct status animation
        // on the next tick, so we just need to exit the forced-off state.
        LOGI("BuiltinLED", "builtin_led_enable received -> resuming default animation");
        BuiltinLED_SetAnimation(&builtinLEDService.base, builtinLEDService.defaultAnimType,
                                BUILTIN_LED_PRIO_LOW, 0);
    }
}

// ---------------------------------------------------------------------------
// IService interface implementations
// ---------------------------------------------------------------------------

/**
 * @brief Initialises the BuiltinLED service and registers all built-in plugins.
 *
 * Resolves the GPIO pin from PinManager and registers all 14 built-in
 * animations synchronously (without the queue) so they are immediately
 * available when Start() creates the animation task.
 *
 * Args:
 *     self:     Pointer to the IService interface.
 *     registry: Dependency injection registry (unused â€” service locator used).
 *
 * Returns:
 *     0 on success, 0 if already initialised (idempotent).
 */
static int32_t BuiltinLED_Initialize(IService *self, IDIRegistry *registry) {
    (void)self;
    (void)registry;

    esp_rom_printf(">> [BLED_INIT] Starting BuiltinLED_Initialize...\n");
    if (builtinLEDService.state >= SERVICE_STATE_INITIALIZED) {
        esp_rom_printf(">> [BLED_INIT] Already initialized (state=%d)\n", (int)builtinLEDService.state);
        return 0;
    }

    if (!builtinLEDService.mutex) {
        esp_rom_printf(">> [BLED_INIT] Creating mutex...\n");
        builtinLEDService.mutex = xSemaphoreCreateMutex();
    }
    if (!builtinLEDService.cmd_queue) {
        esp_rom_printf(">> [BLED_INIT] Creating cmd queue...\n");
        builtinLEDService.cmd_queue = xQueueCreate(BUILTIN_LED_QUEUE_SIZE, sizeof(BuiltinLEDCmd));
    }

    if (!builtinLEDService.controlMutex) builtinLEDService.controlMutex = xSemaphoreCreateMutex();
    if (!builtinLEDService.mutex || !builtinLEDService.cmd_queue || !builtinLEDService.controlMutex ||
        WatchdogWorker_Init(&builtinLEDService.worker) != 0) {
        if (builtinLEDService.controlMutex) vSemaphoreDelete(builtinLEDService.controlMutex);
        builtinLEDService.controlMutex = NULL;
        if (builtinLEDService.cmd_queue) vQueueDelete(builtinLEDService.cmd_queue);
        if (builtinLEDService.mutex) vSemaphoreDelete(builtinLEDService.mutex);
        builtinLEDService.cmd_queue = NULL;
        builtinLEDService.mutex = NULL;
        return -1;
    }
    esp_rom_printf(">> [BLED_INIT] Taking mutex...\n");
    if (xSemaphoreTake(builtinLEDService.mutex, portMAX_DELAY) != pdTRUE) return -1;
    esp_rom_printf(">> [BLED_INIT] Mutex taken\n");

    LEDTransition_Reset(&builtinLEDService.transition, (LEDFrame){0});
    atomic_store(&builtinLEDService.driverError, ESP_OK);
    builtinLEDService.targetFrame = (LEDFrame){0};
    /* Resolve the GPIO pin from PinManager; fall back to ESP32-C6 default. */
    IPinManager *pinMgr =
        (IPinManager *)ServiceLocator_GetService(SVC_NAME_PIN_MANAGER);
    gpio_num_t pin = BUILTIN_LED_PIN;
    if (pinMgr) {
        if (!pinMgr->GetPin) { xSemaphoreGive(builtinLEDService.mutex); return -1; }
        pin = pinMgr->GetPin(pinMgr, PIN_FUNC_BUILTIN_LED);
    }
    if (pin < 0 || pin >= GPIO_NUM_MAX) { xSemaphoreGive(builtinLEDService.mutex); return -1; }
    builtinLEDService.pin = (uint8_t)pin;

    builtinLEDService.max_brightness = DEFAULT_BUILTIN_LED_BRIGHTNESS;
    IConfigurationManager *config = (IConfigurationManager *)ServiceLocator_GetService(SVC_NAME_CONFIGURATION_MANAGER);
    SystemSettings snapshot;
    bool have_settings = config && config->GetSnapshot && config->GetSnapshot(config, &snapshot) == ESP_OK;
    builtinLEDService.disabled = have_settings && snapshot.hardware.builtin_led_anim_mode == 0;
    builtinLEDService.defaultAnimType = BUILTIN_LED_DEFAULT_ANIM;
    /* The splash can already be running before this hardware worker starts.
     * Select its saved startup effect before the first hardware frame, rather
     * than waiting for the UI's later periodic ownership command. */
    builtinLEDService.startupActive = RainbowFlowAnim_IsSynchronizedPhase();
    builtinLEDService.connectionNoticePending = false;
    builtinLEDService.connectionNoticeActive = false;
    builtinLEDService.startupAnimType = DEFAULT_BUILTIN_LED_STARTUP_ANIM;
    if (have_settings) {
        builtinLEDService.max_brightness = snapshot.hardware.builtin_led_brightness;
        builtinLEDService.startupAnimType = snapshot.hardware.builtin_led_startup_anim < BUILTIN_LED_ANIM_COUNT
            ? (BuiltinLEDAnimationType)snapshot.hardware.builtin_led_startup_anim : BUILTIN_LED_ANIM_NONE;
        builtinLEDService.defaultAnimType = snapshot.hardware.builtin_led_idle_anim < BUILTIN_LED_ANIM_COUNT
            ? (BuiltinLEDAnimationType)snapshot.hardware.builtin_led_idle_anim : BUILTIN_LED_ANIM_NONE;
    }
    if (builtinLEDService.disabled) builtinLEDService.defaultAnimType = BUILTIN_LED_ANIM_NONE;
    builtinLEDService.configuredDefaultAnim = builtinLEDService.defaultAnimType;
    builtinLEDService.plugin_count    = 0;

    builtinLEDService.status.current_animation      = BaseAnimation();
    builtinLEDService.status.current_priority       = BUILTIN_LED_PRIO_LOW;
    builtinLEDService.status.remaining_ms           = 0;
    builtinLEDService.status.brightness             = builtinLEDService.max_brightness;
    builtinLEDService.status.enabled                = !builtinLEDService.disabled;
    builtinLEDService.status.is_on                  = BaseAnimation() != BUILTIN_LED_ANIM_NONE;
    builtinLEDService.status.registered_plugins_count = 0;

    builtinLEDService.eventBus = ServiceLocator_GetEventBus();
    builtinLEDService.subscriptions = 0;

    /* Register all 15 built-in animation plugins synchronously. */
    RegisterPlugin_Direct(HeartbeatAnim_Get());
    RegisterPlugin_Direct(BlinkWhiteAnim_Get());
    RegisterPlugin_Direct(SOSRedAnim_Get());
    RegisterPlugin_Direct(BreathingGreenAnim_Get());
    RegisterPlugin_Direct(FadeBlueAnim_Get());
    RegisterPlugin_Direct(StrobeWhiteAnim_Get());
    RegisterPlugin_Direct(RainbowFlowAnim_Get());
    RegisterPlugin_Direct(PoliceStrobeAnim_Get());
    RegisterPlugin_Direct(SolidRedAnim_Get());
    RegisterPlugin_Direct(SolidGreenAnim_Get());
    RegisterPlugin_Direct(SolidBlueAnim_Get());
    RegisterPlugin_Direct(SolidWhiteAnim_Get());
    RegisterPlugin_Direct(BlinkRedAnim_Get());
    RegisterPlugin_Direct(BreathingRedAnim_Get());
    RegisterPlugin_Direct(DoubleBlinkWhiteAnim_Get());
    RegisterPlugin_Direct(GenericBreathingAnim_Get());
    RegisterPlugin_Direct(GenericPulseAnim_Get());
    RegisterPlugin_Direct(GenericCandleAnim_Get());
    RegisterPlugin_Direct(GenericAlertAnim_Get());
    RegisterPlugin_Direct(GenericFadeAnim_Get());
    RegisterPlugin_Direct(GenericColorCycleAnim_Get());

    AnimationPlugin *initial = FindPlugin(BaseAnimation());
    if (initial) { initial->Reset(initial); animationGeneration++; }
    builtinLEDService.state = SERVICE_STATE_INITIALIZED;
    xSemaphoreGive(builtinLEDService.mutex);

    return 0;
}

/**
 * @brief Starts the LED driver, animation task, and subscribes to system events.
 *
 * Args:
 *     self: Pointer to the IService interface.
 *
 * Returns:
 *     0 on success, -1 if already started or driver initialisation fails.
 */
static int32_t BuiltinLED_StopOwned(IService *self);
static const char *builtinEventNames[] = {
    EVENT_CONFIG_CHANGED, "zigbee_pairing_start", "zigbee_connected",
    "zigbee_pairing_stop", "zigbee_rejoin_exhausted", "builtin_led_enable", "zigbee_disconnected"
};
static BusEventHandler builtinEventHandlers[] = {
    SettingsChangedHandler, ZigbeeEventHandler, ZigbeeEventHandler,
    ZigbeeEventHandler, ZigbeeEventHandler, ZigbeeEventHandler, ZigbeeEventHandler
};

static int32_t BuiltinLED_StartOwned(IService *self) {
    (void)self;

    if (builtinLEDService.state == SERVICE_STATE_STARTED)
        return WatchdogWorker_Healthy(&builtinLEDService.worker) ? RegisterConsoleCommands() : -1;
    if (builtinLEDService.state != SERVICE_STATE_INITIALIZED && builtinLEDService.state != SERVICE_STATE_STOPPED) return -1;
    esp_err_t registration_result = RegisterConsoleCommands();
    if (registration_result != ESP_OK) return registration_result;
    if (!builtinLEDService.mutex || xSemaphoreTake(builtinLEDService.mutex, portMAX_DELAY) != pdTRUE) return -1;

    LOGI("BuiltinLED_Start", "Starting BuiltinLED service on pin %d", builtinLEDService.pin);

    /* Initialise the WS2812 LED via the SPI backend (ESP32-C6 preferred path). */
    led_strip_config_t strip_cfg = {
        .strip_gpio_num        = builtinLEDService.pin,
        .max_leds              = 1,
        .led_model             = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
        .flags.invert_out      = false,
    };
    led_strip_spi_config_t spi_cfg = {
        .spi_bus        = SPI2_HOST,
        .flags.with_dma = true,
    };

    esp_err_t err = led_strip_new_spi_device(&strip_cfg, &spi_cfg,
                                              &builtinLEDService.led_strip);
    if (err != ESP_OK) {
        LOGE("BuiltinLED_Start", "led_strip_new_spi_device failed: 0x%x", err);
        atomic_store(&builtinLEDService.driverError, err);
        xSemaphoreGive(builtinLEDService.mutex);
        return -1;
    }

    err = led_strip_clear(builtinLEDService.led_strip);
    atomic_store(&builtinLEDService.driverError, err);
    if (err != ESP_OK) {
        if (led_strip_del(builtinLEDService.led_strip) == ESP_OK) {
            builtinLEDService.led_strip = NULL;
        } else {
            builtinLEDService.driverCleanupFailed = true;
            builtinLEDService.state = SERVICE_STATE_ERROR;
        }
        xSemaphoreGive(builtinLEDService.mutex);
        return -1;
    }
    builtinLEDService.state = SERVICE_STATE_STARTED;

    xSemaphoreGive(builtinLEDService.mutex);

    /* Create animation task AFTER releasing mutex to avoid priority inversion. */
    int32_t task_created = WatchdogWorker_Start(&builtinLEDService.worker,
        BuiltinLED_AnimTask,
        "BuiltinLEDAnim",
        BUILTIN_LED_TASK_STACK_SIZE,
        NULL,
        BUILTIN_LED_TASK_PRIORITY
    );

    builtinLEDService.animTaskHandle = atomic_load(&builtinLEDService.worker.task);
    if (task_created != 0) {
        LOGE("BuiltinLED_Start", "Failed to create animation task");
        /* No worker owns the driver when task creation fails. Do not publish a
         * running service or leak the SPI device on the ordinary rollback path. */
        if (led_strip_del(builtinLEDService.led_strip) == ESP_OK) {
            builtinLEDService.led_strip = NULL;
            builtinLEDService.state = SERVICE_STATE_INITIALIZED;
        } else {
            builtinLEDService.driverCleanupFailed = true;
            builtinLEDService.state = SERVICE_STATE_ERROR;
            LOGE("BuiltinLED_Start", "SPI driver rollback failed; restart required");
        }
        return -1;
    }

    /* Record each successful subscription; rollback never removes an entry
     * this service did not acquire. A busy callback retains its ownership. */
    if (builtinLEDService.eventBus) {
        IEventBus *bus = builtinLEDService.eventBus;
        for (unsigned i = 0; i < sizeof(builtinEventNames) / sizeof(builtinEventNames[0]); ++i) {
            if (!bus->Subscribe || !bus->Unsubscribe ||
                bus->Subscribe(bus, builtinEventNames[i], builtinEventHandlers[i]) != 0) {
                if (BuiltinLED_StopOwned(self) != 0) builtinLEDService.state = SERVICE_STATE_ERROR;
                return -1;
            }
            builtinLEDService.subscriptions |= (1U << i);
        }
    }

    LOGI("BuiltinLED_Start", "Service started successfully (%d plugins registered)",
         builtinLEDService.plugin_count);
    return 0;
}

/**
 * @brief Stops the animation task, releases hardware resources, and unsubscribes.
 *
 * The animation task is deleted before the LED strip is freed so that
 * no task can access a dangling driver handle. EventBus subscription is
 * also cleaned up to prevent dangling callbacks.
 *
 * Args:
 *     self: Pointer to the IService interface.
 *
 * Returns:
 *     0 on success, -1 if the service is not currently started.
 */
static int32_t BuiltinLED_StopOwned(IService *self) {
    (void)self;

    LOGI("BuiltinLED_Stop", "Stopping BuiltinLED service");

    if (builtinLEDService.state != SERVICE_STATE_STARTED &&
        builtinLEDService.state != SERVICE_STATE_ERROR) return -1;
    /* The SPI backend can remove its device before bus deletion fails.
     * Its partially torn-down handle must never be used for clear/delete again. */
    if (builtinLEDService.driverCleanupFailed) return -1;
    builtinLEDService.state = SERVICE_STATE_ERROR;
    if (WatchdogWorker_Stop(&builtinLEDService.worker, pdMS_TO_TICKS(BUILTIN_LED_CONTROL_TIMEOUT_MS)) != 0) return -1;
    builtinLEDService.animTaskHandle = NULL;
    for (unsigned i = 0; i < sizeof(builtinEventNames) / sizeof(builtinEventNames[0]); ++i) {
        if (!(builtinLEDService.subscriptions & (1U << i))) continue;
        IEventBus *bus = builtinLEDService.eventBus;
        if (!bus || !bus->Unsubscribe ||
            bus->Unsubscribe(bus, builtinEventNames[i], builtinEventHandlers[i]) != 0) return -1;
        builtinLEDService.subscriptions &= ~(1U << i);
    }

    if (xSemaphoreTake(builtinLEDService.mutex, pdMS_TO_TICKS(BUILTIN_LED_CONTROL_TIMEOUT_MS)) != pdTRUE) return -1;

    if (builtinLEDService.led_strip) {
        esp_err_t clear_result = led_strip_clear(builtinLEDService.led_strip);
        atomic_store(&builtinLEDService.driverError, clear_result);
        if (clear_result != ESP_OK) {
            xSemaphoreGive(builtinLEDService.mutex);
            return -1;
        }
        esp_err_t delete_result = led_strip_del(builtinLEDService.led_strip);
        if (delete_result != ESP_OK) {
            atomic_store(&builtinLEDService.driverError, delete_result);
            builtinLEDService.driverCleanupFailed = true;
            xSemaphoreGive(builtinLEDService.mutex);
            return -1;
        }
        builtinLEDService.led_strip = NULL;
    }

    BuiltinLEDCmd discarded;
    while (xQueueReceive(builtinLEDService.cmd_queue, &discarded, 0) == pdTRUE) {}
    builtinLEDService.state = SERVICE_STATE_STOPPED;
    xSemaphoreGive(builtinLEDService.mutex);

    LOGI("BuiltinLED_Stop", "Service stopped");
    return 0;
}

static int32_t BuiltinLED_Start(IService *self) {
    if (!builtinLEDService.controlMutex || xSemaphoreTake(builtinLEDService.controlMutex,
        pdMS_TO_TICKS(BUILTIN_LED_CONTROL_TIMEOUT_MS)) != pdTRUE) return -1;
    int32_t result = BuiltinLED_StartOwned(self);
    xSemaphoreGive(builtinLEDService.controlMutex);
    return result;
}

static int32_t BuiltinLED_Stop(IService *self) {
    TaskHandle_t task = atomic_load(&builtinLEDService.worker.task);
    if (task && task == xTaskGetCurrentTaskHandle()) return -1;
    if (!builtinLEDService.controlMutex || xSemaphoreTake(builtinLEDService.controlMutex,
        pdMS_TO_TICKS(BUILTIN_LED_CONTROL_TIMEOUT_MS)) != pdTRUE) return -1;
    int32_t result = BuiltinLED_StopOwned(self);
    xSemaphoreGive(builtinLEDService.controlMutex);
    return result;
}

static ServiceState BuiltinLED_GetState(IService *self) {
    (void)self;
    return builtinLEDService.state;
}

static int32_t BuiltinLED_CheckHealth(IService *self) {
    (void)self;
    return (builtinLEDService.state == SERVICE_STATE_STARTED &&
            WatchdogWorker_Healthy(&builtinLEDService.worker) && builtinLEDService.led_strip != NULL &&
            atomic_load(&builtinLEDService.driverError) == ESP_OK)
               ? 0
               : -1;
}

// ---------------------------------------------------------------------------
// IBuiltinLED interface implementations
// ---------------------------------------------------------------------------

static esp_err_t BuiltinLED_InitModule(IBuiltinLED *self, const BuiltinLEDConfig *config) {
    if (self != &builtinLEDService.base || !config || config->pin < 0) return ESP_ERR_INVALID_ARG;
    /* The active SPI device owns its GPIO; this API cannot silently remap it. */
    if (config->pin != builtinLEDService.pin) return ESP_ERR_NOT_SUPPORTED;

    BuiltinLEDCmd cmd = {
        .type                      = CMD_INIT_MODULE,
        .data.init.pin             = config->pin,
        .data.init.max_brightness  = config->max_brightness,
    };
    return BuiltinLED_PostCommand(&cmd);
}

static esp_err_t BuiltinLED_SetAnimation(IBuiltinLED *self,
                                          BuiltinLEDAnimationType type,
                                          BuiltinLEDPriority priority,
                                          uint32_t duration_ms) {
    if (self != &builtinLEDService.base ||
        (type != BUILTIN_LED_ANIM_NONE && (type < 0 || type == BUILTIN_LED_ANIM_COUNT)) ||
        (unsigned)priority > BUILTIN_LED_PRIO_HIGH)
        return ESP_ERR_INVALID_ARG;
    if (type > BUILTIN_LED_ANIM_COUNT) {
        if (!builtinLEDService.mutex || xSemaphoreTake(builtinLEDService.mutex, 0) != pdTRUE)
            return ESP_ERR_TIMEOUT;
        bool registered = FindPlugin(type) != NULL;
        xSemaphoreGive(builtinLEDService.mutex);
        if (!registered) return ESP_ERR_INVALID_ARG;
    }
    BuiltinLEDCmd cmd = {
        .type                       = CMD_SET_ANIMATION,
        .data.set_anim.anim_type    = type,
        .data.set_anim.priority     = priority,
        .data.set_anim.duration_ms  = duration_ms,
    };
    return BuiltinLED_PostCommand(&cmd);
}

static esp_err_t BuiltinLED_PreviewAnimation(IBuiltinLED *self,BuiltinLEDAnimationType type,uint32_t duration_ms) {
    if (self!=&builtinLEDService.base || !duration_ms || duration_ms>BUILTIN_LED_PREVIEW_MAX_MS ||
        ((unsigned)type>UINT8_MAX && type!=BUILTIN_LED_ANIM_NONE) || !LEDAnimationCatalog_IsValid((uint8_t)type,true)) return ESP_ERR_INVALID_ARG;
    BuiltinLEDCmd cmd={.type=CMD_PREVIEW_ANIMATION,.data.set_anim={type,BUILTIN_LED_PRIO_LOW,duration_ms}};
    return BuiltinLED_PostCommand(&cmd);
}

static esp_err_t BuiltinLED_SetStartupActive(IBuiltinLED *self, bool active) {
    if (self != &builtinLEDService.base) return ESP_ERR_INVALID_ARG;
    BuiltinLEDCmd cmd = {.type = CMD_SET_STARTUP, .data.startup.active = active};
    return BuiltinLED_PostCommand(&cmd);
}

static esp_err_t BuiltinLED_StopAnimation(IBuiltinLED *self) {
    if (self != &builtinLEDService.base) return ESP_ERR_INVALID_ARG;
    BuiltinLEDCmd cmd = {.type = CMD_STOP_ANIMATION};
    return BuiltinLED_PostCommand(&cmd);
}

static esp_err_t BuiltinLED_TurnOn(IBuiltinLED *self) {
    if (self != &builtinLEDService.base) return ESP_ERR_INVALID_ARG;
    BuiltinLEDCmd cmd = {.type = CMD_TURN_ON};
    return BuiltinLED_PostCommand(&cmd);
}

static esp_err_t BuiltinLED_TurnOff(IBuiltinLED *self) {
    if (self != &builtinLEDService.base) return ESP_ERR_INVALID_ARG;
    BuiltinLEDCmd cmd = {.type = CMD_TURN_OFF};
    return BuiltinLED_PostCommand(&cmd);
}

static esp_err_t BuiltinLED_RegisterAnimation(IBuiltinLED *self, AnimationPlugin *plugin) {
    if (self != &builtinLEDService.base || !plugin || !plugin->Reset || !plugin->Update ||
        !plugin->name || !plugin->name[0] || strnlen(plugin->name, BUILTIN_LED_LABEL_CAPACITY) >= BUILTIN_LED_LABEL_CAPACITY ||
        plugin->type < 0 || plugin->type == BUILTIN_LED_ANIM_COUNT) return ESP_ERR_INVALID_ARG;

    BuiltinLEDCmd cmd = {
        .type                  = CMD_REGISTER_PLUGIN,
        .data.reg_plugin.plugin = plugin,
    };
    return BuiltinLED_PostCommand(&cmd);
}

static esp_err_t BuiltinLED_PreviewBrightness(IBuiltinLED *self, uint8_t brightness, uint32_t duration_ms) {
    if (self!=&builtinLEDService.base || !duration_ms || duration_ms>BUILTIN_LED_PREVIEW_MAX_MS) return ESP_ERR_INVALID_ARG;
    BuiltinLEDCmd cmd={.type=CMD_PREVIEW_BRIGHTNESS,.data.brightness_preview={brightness,duration_ms}};
    return BuiltinLED_PostCommand(&cmd);
}

static esp_err_t BuiltinLED_SetBrightness(IBuiltinLED *self, uint8_t brightness) {
    if (self != &builtinLEDService.base) return ESP_ERR_INVALID_ARG;
    BuiltinLEDCmd cmd = {
        .type                          = CMD_SET_BRIGHTNESS,
        .data.set_brightness.brightness = brightness,
    };
    return BuiltinLED_PostCommand(&cmd);
}

static esp_err_t BuiltinLED_GetStatus(IBuiltinLED *self, BuiltinLEDStatus *status) {
    if (self != &builtinLEDService.base || !status) return ESP_ERR_INVALID_ARG;
    if (!builtinLEDService.mutex) return ESP_ERR_INVALID_STATE;
    if (xSemaphoreTake(builtinLEDService.mutex, pdMS_TO_TICKS(BUILTIN_LED_CONTROL_TIMEOUT_MS)) != pdTRUE)
        return ESP_ERR_TIMEOUT;
    memcpy(status, &builtinLEDService.status, sizeof(BuiltinLEDStatus));
    status->enabled = !builtinLEDService.disabled;
    xSemaphoreGive(builtinLEDService.mutex);
    return ESP_OK;
}

// ---------------------------------------------------------------------------
// Debug Console commands
// ---------------------------------------------------------------------------

static esp_err_t BuiltinLED_GetDriverError(IBuiltinLED *self, esp_err_t *error) {
    if (self != &builtinLEDService.base || !error) return ESP_ERR_INVALID_ARG;
    *error = atomic_load(&builtinLEDService.driverError);
    return ESP_OK;
}

static int cmd_builtin_led_info(int argc, char **argv) {
    const LocalizationCatalog *catalog = Localization_Snapshot();
    if (!BuiltinLEDInput_Arguments(argc, argv, 1, 2) ||
        (argc == 2 && strcmp(argv[1], "--machine") != 0)) return 1;
    BuiltinLEDStatus st;
    esp_err_t driver_error;
    IBuiltinLED *led = BuiltinLED_GetModule();
    if (!led->GetDriverError || led->GetDriverError(led, &driver_error) != ESP_OK) return 1;
    if (led->GetStatus(led, &st) != ESP_OK) {
        LOC_PRINTF(catalog, LOC_CLI_BUILTIN_BUILTINLED_FAILED_TO_RETRIEVE_STATUS, "BuiltinLED: failed to retrieve status.\n");
        return 1;
    }
    if (argc == 2) {
        BuiltinLED_PrintMachineStatus(&st, driver_error);
        return 0;
    }
    LOC_PRINTF(catalog, LOC_CLI_BUILTIN_BUILTINLED_STATUS, "BuiltinLED Status:\n");
    LOC_PRINTF(catalog, LOC_CLI_BUILTIN_IS_ON_S, "  Is On               : %s\n",        st.is_on ? Localization_Text(catalog, LOC_CLI_BUILTIN_YES) : Localization_Text(catalog, LOC_CLI_BUILTIN_NO));
    LOC_PRINTF(catalog, LOC_CLI_BUILTIN_GLOBAL_ENABLED_S, "  Global Enabled      : %s\n",        st.enabled ? Localization_Text(catalog, LOC_CLI_BUILTIN_YES) : Localization_Text(catalog, LOC_CLI_BUILTIN_NO));
    LOC_PRINTF(catalog, LOC_CLI_BUILTIN_CURRENT_ANIMATION_D, "  Current Animation   : %d\n",        st.current_animation);
    LOC_PRINTF(catalog, LOC_CLI_BUILTIN_CURRENT_PRIORITY_D, "  Current Priority    : %d\n",        st.current_priority);
    LOC_PRINTF(catalog, LOC_CLI_BUILTIN_REMAINING_DURATION_LU_MS, "  Remaining Duration  : %lu ms\n",    (unsigned long)st.remaining_ms);
    LOC_PRINTF(catalog, LOC_CLI_BUILTIN_MAX_BRIGHTNESS_U_255, "  Max Brightness      : %u / 255\n",  st.brightness);
    LOC_PRINTF(catalog, LOC_CLI_BUILTIN_REGISTERED_PLUGINS_U, "  Registered Plugins  : %u\n",        st.registered_plugins_count);
    LOC_PRINTF(catalog, LOC_CLI_BUILTIN_DRIVER_ERROR_0X_X, "  Driver Error        : 0x%x\n",        (unsigned)driver_error);
    return 0;
}

static int cmd_builtin_led_boot(int argc, char **argv) {
    if (!BuiltinLEDInput_Arguments(argc, argv, 1, 2)) return 1;
    IConfigurationManager *config = ConfigurationManager_GetInstance();
    if (!config || !config->GetSnapshot || !config->UpdateFields) return 1;
    if (argc == 2) {
        uint32_t parsed;
        if (!BuiltinLEDInput_ParseUnsigned(argv[1], UINT8_MAX, &parsed) ||
            !LEDAnimationCatalog_IsValid((uint8_t)parsed, true)) return 1;
        uint8_t animation = (uint8_t)parsed;
        ConfigurationFieldUpdate field = {offsetof(HardwareConfigs, builtin_led_startup_anim), &animation, sizeof(animation)};
        if (config->UpdateFields(config, "hardware", &field, 1, true) != ESP_OK) return 1;
    }
    SystemSettings snapshot;
    if (config->GetSnapshot(config, &snapshot) != ESP_OK) return 1;
    printf("RACOOL_CLI v=1 type=builtin_boot animation=%u\n", (unsigned)snapshot.hardware.builtin_led_startup_anim);
    return 0;
}

static int cmd_builtin_led_list(int argc, char **argv) {
    const LocalizationCatalog *catalog = Localization_Snapshot();
    if (!BuiltinLEDInput_Arguments(argc, argv, 1, 1)) return 1;
    struct { int type; char name[BUILTIN_LED_LABEL_CAPACITY]; } snapshot[BUILTIN_LED_MAX_PLUGINS];
    if (!builtinLEDService.mutex ||
        xSemaphoreTake(builtinLEDService.mutex, pdMS_TO_TICKS(1000)) != pdTRUE) return 1;
    uint8_t count = builtinLEDService.plugin_count;
    for (uint8_t i = 0; i < count; i++) {
        const AnimationPlugin *plugin = builtinLEDService.plugins[i];
        snapshot[i].type = plugin->type;
        const char *name = (unsigned)plugin->type < BUILTIN_LED_ANIM_COUNT
            ? LEDAnimationLabel_Localized((uint8_t)plugin->type, true, catalog) : plugin->name;
        snprintf(snapshot[i].name, sizeof(snapshot[i].name), "%s", name ? name : "?");
    }
    xSemaphoreGive(builtinLEDService.mutex);
    /* UART output must not hold the animation/lifecycle mutex. */
    LOC_PRINTF(catalog, LOC_CLI_BUILTIN_BUILTINLED_REGISTERED_ANIMATIONS_U, "BuiltinLED Registered Animations (%u):\n", count);
    LOC_PRINTF(catalog, LOC_CLI_BUILTIN_OFF_ENTRY, "  [-1] KAPALI\n");
    for (uint8_t i = 0; i < count; i++)
        printf("  [%2d] %s\n", snapshot[i].type, snapshot[i].name);
    return 0;
}

static int dispatch_builtin_led_set(int argc, char **argv) {
    const LocalizationCatalog *catalog = Localization_Snapshot();
    if (!BuiltinLEDInput_Arguments(argc, argv, 2, 4)) return 1;
    uint32_t anim = 0, prio = BUILTIN_LED_PRIO_HIGH, dur = 0;
    bool off = argc >= 2 && argv[1] && strcmp(argv[1], "-1") == 0;
    if (argc < 2 || argc > 4 ||
        (!off && !BuiltinLEDInput_ParseUnsigned(argv[1], INT32_MAX, &anim)) ||
        (argc > 2 && !BuiltinLEDInput_ParseUnsigned(argv[2], BUILTIN_LED_PRIO_HIGH, &prio)) ||
        (argc > 3 && !BuiltinLEDInput_ParseUnsigned(argv[3], UINT32_MAX, &dur))) {
        LOC_PRINTF(catalog, LOC_CLI_BUILTIN_USAGE_BUILTIN_LED_SET_ANIM_ID_PRIORITY_DURATION_MS, "Usage: builtin_led_set <anim_id> [priority] [duration_ms]\n");
        LOC_PRINTF(catalog, LOC_CLI_BUILTIN_ANIM_ID_ANIMATION_TYPE_USE_BUILTIN_LED_LIST_FOR_IDS, "  anim_id   : animation type (use 'builtin_led_list' for IDs)\n");
        LOC_PRINTF(catalog, LOC_CLI_BUILTIN_PRIORITY_0_LOW_1_MEDIUM_2_HIGH_DEFAULT_2, "  priority  : 0=low 1=medium 2=high (default: 2)\n");
        LOC_PRINTF(catalog, LOC_CLI_BUILTIN_DURATION_MS_0_INFINITE_DEFAULT_0, "  duration_ms: 0 = infinite (default: 0)\n");
        return 1;
    }
    esp_err_t err = off ? BuiltinLED_GetModule()->StopAnimation(BuiltinLED_GetModule())
        : BuiltinLED_GetModule()->SetAnimation(
        BuiltinLED_GetModule(), (BuiltinLEDAnimationType)anim,
        (BuiltinLEDPriority)prio, dur);
    printf(err == ESP_OK ? Localization_Text(catalog, LOC_CLI_BUILTIN_ANIMATION_QUEUED) : Localization_Text(catalog, LOC_CLI_BUILTIN_ERROR_0X_X), err);
    return (err == ESP_OK) ? 0 : 1;
}

static int dispatch_builtin_led_stop(int argc, char **argv) {
    const LocalizationCatalog *catalog = Localization_Snapshot();
    if (!BuiltinLEDInput_Arguments(argc, argv, 1, 1)) return 1;
    esp_err_t err = BuiltinLED_GetModule()->StopAnimation(BuiltinLED_GetModule());
    printf(err == ESP_OK ? Localization_Text(catalog, LOC_CLI_BUILTIN_STOP_QUEUED) : Localization_Text(catalog, LOC_CLI_BUILTIN_ERROR_0X_X), err);
    return (err == ESP_OK) ? 0 : 1;
}

static int dispatch_builtin_led_on(int argc, char **argv) {
    const LocalizationCatalog *catalog = Localization_Snapshot();
    if (!BuiltinLEDInput_Arguments(argc, argv, 1, 1)) return 1;
    esp_err_t err = BuiltinLED_SaveEnabled(true);
    printf(err == ESP_OK ? Localization_Text(catalog, LOC_CLI_BUILTIN_AUTOMATIC_MODE_SAVED) : Localization_Text(catalog, LOC_CLI_BUILTIN_ERROR_0X_X), err);
    return (err == ESP_OK) ? 0 : 1;
}

static int dispatch_builtin_led_off(int argc, char **argv) {
    const LocalizationCatalog *catalog = Localization_Snapshot();
    if (!BuiltinLEDInput_Arguments(argc, argv, 1, 1)) return 1;
    esp_err_t err = BuiltinLED_SaveEnabled(false);
    printf(err == ESP_OK ? Localization_Text(catalog, LOC_CLI_BUILTIN_DISABLED_MODE_SAVED) : Localization_Text(catalog, LOC_CLI_BUILTIN_ERROR_0X_X), err);
    return (err == ESP_OK) ? 0 : 1;
}

esp_err_t BuiltinLED_SaveEnabled(bool enabled) {
    IConfigurationManager *config = (IConfigurationManager *)ServiceLocator_GetService(SVC_NAME_CONFIGURATION_MANAGER);
    if (!config || !config->UpdateFields) return ESP_ERR_INVALID_STATE;
    uint8_t mode = enabled ? 1 : 0;
    ConfigurationFieldUpdate field = {offsetof(HardwareConfigs, builtin_led_anim_mode), &mode, sizeof(mode)};
    return config->UpdateFields(config, "hardware", &field, 1, true);
}

esp_err_t BuiltinLED_SaveBrightnessText(const char *value) {
    if (!value || !*value) return ESP_ERR_INVALID_ARG;
    unsigned brightness = 0;
    size_t length = 0;
    for (const char *p = value; *p; ++p) {
        if (++length > 3 || *p < '0' || *p > '9') return ESP_ERR_INVALID_ARG;
        brightness = brightness * 10u + (unsigned)(*p - '0');
    }
    if (brightness > 255) return ESP_ERR_INVALID_ARG;
    IConfigurationManager *config = (IConfigurationManager *)ServiceLocator_GetService(SVC_NAME_CONFIGURATION_MANAGER);
    if (!config || !config->UpdateFields) return ESP_ERR_INVALID_STATE;
    uint8_t setting = (uint8_t)brightness;
    ConfigurationFieldUpdate field = {offsetof(HardwareConfigs, builtin_led_brightness), &setting, sizeof(setting)};
    return config->UpdateFields(config, "hardware", &field, 1, true);
}

static int dispatch_builtin_led_brightness(int argc, char **argv) {
    const LocalizationCatalog *catalog = Localization_Snapshot();
    if (!BuiltinLEDInput_Arguments(argc, argv, 2, 2)) {
        LOC_PRINTF(catalog, LOC_CLI_BUILTIN_USAGE_BUILTIN_LED_BRIGHTNESS_0_255, "Usage: builtin_led_brightness <0-255>\n");
        return 1;
    }
    esp_err_t err = BuiltinLED_SaveBrightnessText(argv[1]);
    printf(err == ESP_OK ? Localization_Text(catalog, LOC_CLI_BUILTIN_BRIGHTNESS_SAVED) : Localization_Text(catalog, LOC_CLI_BUILTIN_ERROR_0X_X), err);
    return err == ESP_OK ? 0 : 1;
}

static int cmd_builtin_led_set(int argc, char **argv) {
    if (!BuiltinLEDInput_Arguments(argc, argv, 1, 5)) return 1;
    bool machine = strcmp(argv[argc - 1], "--machine") == 0;
    int result = dispatch_builtin_led_set(argc - (machine ? 1 : 0), argv);
    if (machine) printf("RACOOL_CLI v=1 type=builtin_command command=set result=%s error=%d\n",
                        result == 0 ? "accepted" : "rejected", result);
    return result;
}

static int cmd_builtin_led_stop(int argc, char **argv) {
    if (!BuiltinLEDInput_Arguments(argc, argv, 1, 5)) return 1;
    bool machine = strcmp(argv[argc - 1], "--machine") == 0;
    int result = dispatch_builtin_led_stop(argc - (machine ? 1 : 0), argv);
    if (machine) printf("RACOOL_CLI v=1 type=builtin_command command=stop result=%s error=%d\n",
                        result == 0 ? "accepted" : "rejected", result);
    return result;
}

static int cmd_builtin_led_on(int argc, char **argv) {
    if (!BuiltinLEDInput_Arguments(argc, argv, 1, 5)) return 1;
    bool machine = strcmp(argv[argc - 1], "--machine") == 0;
    int result = dispatch_builtin_led_on(argc - (machine ? 1 : 0), argv);
    if (machine) printf("RACOOL_CLI v=1 type=builtin_command command=on result=%s error=%d\n",
                        result == 0 ? "accepted" : "rejected", result);
    return result;
}

static int cmd_builtin_led_off(int argc, char **argv) {
    if (!BuiltinLEDInput_Arguments(argc, argv, 1, 5)) return 1;
    bool machine = strcmp(argv[argc - 1], "--machine") == 0;
    int result = dispatch_builtin_led_off(argc - (machine ? 1 : 0), argv);
    if (machine) printf("RACOOL_CLI v=1 type=builtin_command command=off result=%s error=%d\n",
                        result == 0 ? "accepted" : "rejected", result);
    return result;
}

static int cmd_builtin_led_brightness(int argc, char **argv) {
    if (!BuiltinLEDInput_Arguments(argc, argv, 1, 5)) return 1;
    bool machine = strcmp(argv[argc - 1], "--machine") == 0;
    int result = dispatch_builtin_led_brightness(argc - (machine ? 1 : 0), argv);
    if (machine) printf("RACOOL_CLI v=1 type=builtin_command command=brightness result=%s error=%d\n",
                        result == 0 ? "accepted" : "rejected", result);
    return result;
}

/**
 * @brief Registers all BuiltinLED commands with the Debug Console service.
 *
 * Commands registered:
 *   builtin_led_info       â€” show current status
 *   builtin_led_list       â€” list all registered animation plugins
 *   builtin_led_set        â€” start an animation by type ID
 *   builtin_led_stop       â€” stop current animation and clear LED
 *   builtin_led_on         â€” persist automatic mode
 *   builtin_led_off        â€” persist global disabled mode
 *   builtin_led_brightness â€” set max brightness (0â€“255)
 */
static esp_err_t RegisterConsoleCommands(void) {
    IDebugConsole *console =
        (IDebugConsole *)ServiceLocator_GetService(SVC_NAME_DEBUG_CONSOLE);
    if (!console) {
        /* Hardware-only compositions may omit the console. Production startup
         * separately requires its public CLI registrar. */
        return ESP_OK;
    }
    if (!console->RegisterCommandWithCategory) return ESP_ERR_INVALID_STATE;

    static const esp_console_cmd_t cmds[] = {
        {.command = "builtin_led_boot", .help = "Read or persist independent boot animation (255 disables)",
         .hint = "[id]", .func = cmd_builtin_led_boot},
        {
            .command = "builtin_led_trace", .help = "Capture LED timing in memory; dump ends capture",
            .func = cmd_builtin_led_trace,
        },
        {
            .command  = "builtin_led_info",
            .help     = "Show current BuiltinLED state and status",
            .hint     = NULL,
            .func     = cmd_builtin_led_info,
            .argtable = NULL,
        },
        {
            .command  = "builtin_led_list",
            .help     = "List all registered BuiltinLED animation plugins",
            .hint     = NULL,
            .func     = cmd_builtin_led_list,
            .argtable = NULL,
        },
        {
            .command  = "builtin_led_set",
            .help     = "Start a BuiltinLED animation (see builtin_led_list for IDs)",
            .hint     = "<id> [priority] [duration_ms]",
            .func     = cmd_builtin_led_set,
            .argtable = NULL,
        },
        {
            .command  = "builtin_led_stop",
            .help     = "Stop the current BuiltinLED animation and clear the LED",
            .hint     = NULL,
            .func     = cmd_builtin_led_stop,
            .argtable = NULL,
        },
        {
            .command  = "builtin_led_on",
            .help     = "Enable the persisted BuiltinLED automatic mode",
            .hint     = NULL,
            .func     = cmd_builtin_led_on,
            .argtable = NULL,
        },
        {
            .command  = "builtin_led_off",
            .help     = "Disable BuiltinLED globally, including notifications",
            .hint     = NULL,
            .func     = cmd_builtin_led_off,
            .argtable = NULL,
        },
        {
            .command  = "builtin_led_brightness",
            .help     = "Set BuiltinLED maximum brightness (0â€“255)",
            .hint     = "<level>",
            .func     = cmd_builtin_led_brightness,
            .argtable = NULL,
        },
    };

    for (size_t i = 0; i < sizeof(cmds) / sizeof(cmds[0]); i++) {
        esp_err_t result = console->RegisterCommandWithCategory(console, &cmds[i], CMD_CAT_HARDWARE);
        if (result != ESP_OK) return result;
    }

    LOGI("RegisterConsoleCommands", "%zu console commands registered",
         sizeof(cmds) / sizeof(cmds[0]));
    return ESP_OK;
}

// ---------------------------------------------------------------------------
// Module accessor (singleton)
// ---------------------------------------------------------------------------

/**
 * @brief Returns the singleton IBuiltinLED interface instance.
 *
 * Initialises the vtable, mutex, and command queue on the first call.
 * Startup-only initialization must precede concurrent consumers; the RTOS
 * scheduler may already be running when ESP-IDF app_main calls this factory.
 *
 * Returns:
 *     Pointer to the IBuiltinLED interface.
 */
IBuiltinLED *BuiltinLED_GetModule(void) {
    static bool vtable_set = false;
    if (!vtable_set) {
        /* IService vtable */
        builtinLEDService.base.base.Initialize  = BuiltinLED_Initialize;
        builtinLEDService.base.base.Start       = BuiltinLED_Start;
        builtinLEDService.base.base.Stop        = BuiltinLED_Stop;
        builtinLEDService.base.base.GetState    = BuiltinLED_GetState;
        builtinLEDService.base.base.CheckHealth = BuiltinLED_CheckHealth;

        /* IBuiltinLED vtable */
        builtinLEDService.base.InitModule       = BuiltinLED_InitModule;
        builtinLEDService.base.SetAnimation     = BuiltinLED_SetAnimation;
        builtinLEDService.base.SetStartupActive = BuiltinLED_SetStartupActive;
        builtinLEDService.base.PreviewAnimation = BuiltinLED_PreviewAnimation;
        builtinLEDService.base.StopAnimation    = BuiltinLED_StopAnimation;
        builtinLEDService.base.TurnOn           = BuiltinLED_TurnOn;
        builtinLEDService.base.TurnOff          = BuiltinLED_TurnOff;
        builtinLEDService.base.RegisterAnimation = BuiltinLED_RegisterAnimation;
        builtinLEDService.base.SetBrightness    = BuiltinLED_SetBrightness;
        builtinLEDService.base.PreviewBrightness = BuiltinLED_PreviewBrightness;
        builtinLEDService.base.GetStatus        = BuiltinLED_GetStatus;
        builtinLEDService.base.GetDriverError = BuiltinLED_GetDriverError;

        builtinLEDService.state     = SERVICE_STATE_UNINITIALIZED;
        builtinLEDService.mutex     = xSemaphoreCreateMutex();
        builtinLEDService.cmd_queue = xQueueCreate(BUILTIN_LED_QUEUE_SIZE,
                                                    sizeof(BuiltinLEDCmd));
        vtable_set = true;
    }
    return &builtinLEDService.base;
}
