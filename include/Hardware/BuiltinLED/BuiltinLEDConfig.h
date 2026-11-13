#ifndef BUILTIN_LED_CONFIG_H
#define BUILTIN_LED_CONFIG_H

#include <stdint.h>

#define BUILTIN_LED_PREVIEW_MAX_MS 10000u

/**
 * @brief Built-in LED animation type identifiers.
 *
 * Each value maps to exactly one AnimationPlugin registered in the plugin
 * registry. BUILTIN_LED_ANIM_NONE indicates the LED is off or in static
 * (non-animated) mode. BUILTIN_LED_ANIM_COUNT is a sentinel for range checks.
 */
typedef enum {
    BUILTIN_LED_ANIM_NONE  = -1,  ///< No animation active (LED off or static).
    HEARTBEAT_RED          =  0,  ///< Double-pulse red heartbeat pattern.
    BLINK_WHITE            =  1,  ///< 1 Hz on/off white blink.
    SOS_RED                =  2,  ///< SOS Morse code in red.
    BREATHING_GREEN        =  3,  ///< Exponential-sine green breathing cycle.
    FADE_BLUE              =  4,  ///< Linear blue fade-in / fade-out loop.
    STROBE_WHITE           =  5,  ///< High-speed white strobe (10 Hz).
    RAINBOW_FLOW           =  6,  ///< Smooth 6-sector HSV rainbow sweep.
    POLICE_STROBE          =  7,  ///< Red/Blue alternating police strobe.
    SOLID_RED              =  8,  ///< Constant solid red.
    SOLID_GREEN            =  9,  ///< Constant solid green.
    SOLID_BLUE             = 10,  ///< Constant solid blue.
    SOLID_WHITE            = 11,  ///< Constant solid white.
    BLINK_RED              = 12,  ///< 1 Hz on/off red blink.
    BREATHING_RED          = 13,  ///< Exponential-sine red breathing cycle.
    DOUBLE_BLINK_WHITE     = 14,  ///< Double-pulse white blink (long pause, two quick flashes).
    GENERIC_BREATHING      = 15,  ///< Legacy breathing pattern with a white base.
    GENERIC_PULSE          = 16,  ///< Quick pulse pattern.
    GENERIC_CANDLE         = 17,  ///< Elapsed-time candle flicker.
    GENERIC_ALERT          = 18,  ///< Alert blink.
    GENERIC_FADE           = 19,  ///< Linear fade-in/out.
    GENERIC_COLOR_CYCLE    = 20,  ///< Six-color cycle.
    BUILTIN_LED_ANIM_COUNT        ///< Sentinel — total number of animation types.
} BuiltinLEDAnimationType;

/**
 * @brief Priority levels for animation preemption.
 *
 * A new animation request is accepted only if its priority is greater than
 * or equal to the currently active animation's priority.
 */
typedef enum {
    BUILTIN_LED_PRIO_LOW    = 0,  ///< Idle / manual (lowest priority).
    BUILTIN_LED_PRIO_MEDIUM = 1,  ///< Network / state-change notifications.
    BUILTIN_LED_PRIO_HIGH   = 2   ///< Critical errors and alarms (highest).
} BuiltinLEDPriority;

/**
 * @brief Runtime configuration passed to BuiltinLED_InitModule().
 */
typedef struct {
    int8_t  pin;            ///< GPIO pin number of the WS2812 LED.
    uint8_t max_brightness; ///< Maximum brightness level (0–255).
} BuiltinLEDConfig;

// ---------------------------------------------------------------------------
// Task & render parameters
// ---------------------------------------------------------------------------

/** @brief ESP-IDF FreeRTOS stack size for the animation task (bytes). */
#define BUILTIN_LED_TASK_STACK_SIZE   3072U

/** @brief FreeRTOS priority of the animation task. */
/* Share the application scheduling band with RGBLED and Zigbee; a sustained
 * SDK mainloop burst must not starve the short animation worker. */
#define BUILTIN_LED_TASK_PRIORITY     5U

/** @brief Depth of the command queue (number of pending commands). */
#define BUILTIN_LED_QUEUE_SIZE        16U
#define BUILTIN_LED_CONTROL_TIMEOUT_MS 1000U
#define BUILTIN_LED_IDLE_POLL_MS       50U

/** @brief Target frame interval in milliseconds (~50 FPS). */
#define BUILTIN_LED_FRAME_INTERVAL_MS 20U
#define BUILTIN_LED_CONNECTED_NOTICE_MS 3000U

/* Optional in-memory timing capture; no serial output in the animation worker. */
#define BUILTIN_LED_LABEL_CAPACITY 64U
#define BUILTIN_LED_TRACE_CAPACITY 512U
#define BUILTIN_LED_TRACE_PAGE_ROWS 64U
#define BUILTIN_LED_TRACE_PAGE_COUNT ((BUILTIN_LED_TRACE_CAPACITY + BUILTIN_LED_TRACE_PAGE_ROWS - 1U) / BUILTIN_LED_TRACE_PAGE_ROWS)
#define BUILTIN_LED_TRACE_GAP_US 40000U

// ---------------------------------------------------------------------------
// Animation defaults
// ---------------------------------------------------------------------------

/** @brief Animation shown at startup and after high-priority animations expire. */
#define BUILTIN_LED_DEFAULT_ANIM       BLINK_WHITE
/** Independent boot selection; existing status animation remains the runtime default. */
#define DEFAULT_BUILTIN_LED_STARTUP_ANIM RAINBOW_FLOW

/** @brief Default brightness value used by animation plugins as their nominal output. */
#define BUILTIN_LED_DEFAULT_BRIGHTNESS 20U

/** @brief Maximum number of simultaneously registered animation plugins. */
#define BUILTIN_LED_MAX_PLUGINS        24U

// ---------------------------------------------------------------------------
// EventBus event names published by this module
// ---------------------------------------------------------------------------

/** @brief Published whenever the LED on/off state changes. */
#define EVENT_BUILTIN_LED_STATE_CHANGED      "builtin_led.state_changed"

/** @brief Published whenever the maximum brightness is updated. */
#define EVENT_BUILTIN_LED_BRIGHTNESS_CHANGED "builtin_led.brightness_changed"

/** @brief Published whenever the active animation changes. */
#define EVENT_BUILTIN_LED_ANIM_CHANGED       "builtin_led.anim_changed"

/** @brief Published when a non-recoverable LED driver error occurs. */
#define EVENT_BUILTIN_LED_ERROR              "builtin_led.error"

#endif // BUILTIN_LED_CONFIG_H
