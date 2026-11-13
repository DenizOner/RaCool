#ifndef ROTARY_ENCODER_TYPES_H
#define ROTARY_ENCODER_TYPES_H

/**
 * @file RotaryEncoderTypes.h
 * @brief Type definitions for the KY-040 Rotary Encoder module.
 *
 * Provides all data types, event structures, callback signatures, and
 * diagnostic/status containers used throughout the RotaryEncoder subsystem.
 */

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Rotation direction enumeration.
 *
 * ROTATION_CW (clockwise) maps to +1 and ROTATION_CCW (counter-clockwise)
 * maps to -1 so the value can be used directly as a signed multiplier.
 */
typedef enum {
    ROTATION_NONE = 0,
    ROTATION_CW   = 1,
    ROTATION_CCW   = -1
} RotationDirection;

/**
 * @brief Button gesture types detected by the gesture engine.
 */
typedef enum {
    GESTURE_NONE = 0,
    GESTURE_SINGLE_CLICK,
    GESTURE_DOUBLE_CLICK,
    GESTURE_LONG_PRESS,
    GESTURE_HELD,
    GESTURE_RELEASED,
    GESTURE_PRESSED
} ButtonGesture;

/**
 * @brief Discriminator for the RotaryEncoderEvent union.
 */
typedef enum {
    ROTARY_ENCODER_EVENT_ROTATION = 0,
    ROTARY_ENCODER_EVENT_BUTTON
} RotaryEncoderEventType;

/**
 * @brief Payload for a processed rotation event.
 *
 * @param direction  CW or CCW after optional direction inversion.
 * @param steps      Signed detent count (negative = CCW).
 * @param velocity   Acceleration multiplier (1 = no acceleration).
 */
typedef struct {
    RotationDirection direction;
    int16_t steps;
    uint8_t velocity;
} RotaryEncoderRotationEvent;

/**
 * @brief Payload for a processed button event.
 *
 * @param gesture      Detected gesture type.
 * @param duration_ms  Press duration in milliseconds.
 * @param click_count  Number of clicks in the gesture sequence.
 */
typedef struct {
    ButtonGesture gesture;
    uint32_t duration_ms;
    uint8_t click_count;
} RotaryEncoderButtonEvent;

/**
 * @brief Tagged union for queued encoder events (rotation or button).
 */
typedef struct {
    RotaryEncoderEventType type;
    union {
        RotaryEncoderRotationEvent rotation;
        RotaryEncoderButtonEvent button;
    } payload;
} RotaryEncoderEvent;

/**
 * @brief Raw button edge captured by the ISR.
 *
 * @param pressed       true = falling edge (button down), false = rising edge.
 * @param timestamp_ms  Millisecond timestamp from esp_timer.
 */
typedef struct {
    bool pressed;
    uint32_t timestamp_ms;
} RotaryEncoderRawEvent;

/**
 * @brief Public status snapshot of the encoder module.
 */
typedef struct {
    int32_t  absolute_position;
    bool     button_held;
    uint32_t last_activity_ms;
    uint32_t total_rotations;
    uint32_t total_clicks;
    uint32_t dropped_rotation_events;
    uint32_t dropped_button_events;
} RotaryEncoderStatus;

/**
 * @brief Internal diagnostics counters for health monitoring.
 */
typedef struct {
    uint32_t isr_count;
    uint32_t queue_overflows;
    uint32_t last_activity_ms;
    uint32_t rotation_events;
    uint32_t button_events;
    uint32_t callback_failures;
    uint32_t pcnt_overflow_count;
} RotaryEncoderDiagnostics;

/**
 * @brief Runtime-tunable configuration parameters.
 *
 * All fields can be modified at runtime through the IRotaryEncoder API
 * and are protected by the service spinlock.
 */
typedef struct {
    bool    invert_direction;
    uint8_t steps_per_detent;
    bool    accel_enabled;
    uint8_t accel_threshold_ms;
    uint16_t debounce_rotation_ms;
    uint16_t debounce_button_ms;
    uint16_t long_press_ms;
    uint16_t double_click_ms;
    uint16_t held_repeat_ms;
} RotaryEncoderRuntimeConfig;

/**
 * @brief Callback signature for rotation events.
 *
 * @param event  Rotation event payload.
 * @param ctx    User-supplied context pointer.
 */
typedef void (*RotaryEncoderRotationCallback)(RotaryEncoderRotationEvent event, void *ctx);

/**
 * @brief Callback signature for button events.
 *
 * @param event  Button event payload.
 * @param ctx    User-supplied context pointer.
 */
typedef void (*RotaryEncoderButtonCallback)(RotaryEncoderButtonEvent event, void *ctx);

#ifdef __cplusplus
}
#endif

#endif /* ROTARY_ENCODER_TYPES_H */
