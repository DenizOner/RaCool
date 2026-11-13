#ifndef ROTARYENCODERCONFIG_H
#define ROTARYENCODERCONFIG_H

#include "Management/ConfigurationManager/SystemConfigs.h"

/**
 * @file RotaryEncoderConfig.h
 * @brief Compile-time configuration for the ESP32-C6 PCNT-backed KY-040 rotary encoder.
 *
 * All tuneable knobs live here so that integrators can override them
 * from platformio.ini build_flags without touching source files.
 */

/* ── GPIO Pin Assignment ─────────────────────────────────────────────── */

/** @brief CLK (A-phase) GPIO pin number. */
#define ROTARY_ENCODER_CLK_PIN          ROTARY_CLK_PIN

/** @brief DT (B-phase) GPIO pin number. */
#define ROTARY_ENCODER_DT_PIN           ROTARY_DT_PIN

/** @brief SW (push-button) GPIO pin number. */
#define ROTARY_ENCODER_SW_PIN           ROTARY_SW_PIN

/* ── PCNT Hardware Configuration ─────────────────────────────────────── */

/** @brief Number of PCNT quadrature edges that form one mechanical detent. */
#define RE_PCNT_STEPS_PER_DETENT        DEFAULT_ENCODER_STEPS_PER_DETENT

/** @brief Hardware glitch filter threshold in nanoseconds. */
#define RE_PCNT_GLITCH_FILTER_NS        10000

/** @brief ISR-level software bounce guard: consecutive PCNT watch-point fires
 *         in the same direction within this window are treated as mechanical
 *         bounce and silently dropped.  50 ms allows up to 20 detents/sec,
 *         which is well above the maximum practical menu navigation speed.
 *         Override via platformio.ini build_flags if a different encoder model
 *         requires a shorter (faster) or longer (noisier) window. */
#define RE_PCNT_ISR_DEBOUNCE_US         50000U

/** @brief Invert logical rotation direction when hardware wiring is reversed. */
#define RE_DIRECTION_INVERTED           false

/* ── Button Timing ───────────────────────────────────────────────────── */

/** @brief Minimum press duration to register a valid click (ms). */
#define RE_DEBOUNCE_BUTTON_MS           50

/** @brief Suppress repeated same-level button IRQs within this window (ms). */
#define RE_BUTTON_NOISE_FILTER_MS       20

/** @brief Encoder step deduplication window to filter multi-path or contact bounce duplicates (ms). */
#define RE_ENCODER_DEDUP_WINDOW_MS      15

/** @brief Cooldown window to suppress accidental encoder rotation triggered by physical button press crosstalk (ms). */
#define RE_BUTTON_ROTATION_COOLDOWN_MS  200

/** @brief Duration threshold for a long-press gesture (ms). */
#define RE_LONG_PRESS_MS                5000

/** @brief Maximum gap between clicks for double-click detection (ms). */
#define RE_DOUBLE_CLICK_WINDOW_MS       300

/** @brief Repeat interval while button is held after long-press (ms). */
#define RE_HELD_REPEAT_MS               200

/* ── Acceleration ────────────────────────────────────────────────────── */

/** @brief Enable rotation acceleration by default. */
#define RE_ACCEL_ENABLED                (DEFAULT_ENCODER_ACCEL_ENABLED != 0u)

/** @brief Acceleration threshold between detents (ms). */
#define RE_ACCEL_THRESHOLD_MS           DEFAULT_ENCODER_ACCEL_THRESHOLD_MS

/** @brief Maximum acceleration multiplier. */
#define RE_ACCEL_MAX_MULTIPLIER         5

/* ── FreeRTOS Task ───────────────────────────────────────────────────── */

/** @brief FreeRTOS task name shown in task lists. */
#define RE_TASK_NAME                    "RotaryEnc"

/** @brief Stack size in bytes for the encoder processing task. */
#define RE_TASK_STACK_SIZE              12288

/** @brief Task priority for responsive input handling. */
#define RE_TASK_PRIORITY                8

/** @brief Cooperative worker shutdown deadline (ms). */
#define RE_STOP_TIMEOUT_MS              2000
#define RE_CONTROL_TIMEOUT_MS           2000

/* ── Queues & Callbacks ──────────────────────────────────────────────── */

/** @brief Queue depth for raw and processed encoder events. */
#define RE_EVENT_QUEUE_SIZE             32

/** @brief Maximum direct rotation or button callback subscribers. */
#define RE_MAX_CALLBACK_SUBSCRIBERS     4

/* ── Health & Idle ───────────────────────────────────────────────────── */

/** @brief Encoder task wake interval for gesture checks when idle (ms). */
#define RE_IDLE_WAKE_MS                 50

/** @brief Interval for periodic health-check logging (ms). */
#define RE_HEALTH_CHECK_INTERVAL_MS     5000

#endif /* ROTARYENCODERCONFIG_H */
