/**
 * @file RotaryEncoderGesture.c
 * @brief Gesture recognition engine for rotation acceleration and button
 * gestures.
 *
 * Processes raw PCNT detent counts and button press/release edges into
 * high-level events (single click, double click, long press, held repeat)
 * with configurable timing thresholds.  All mutable state lives inside
 * RotaryEncoderServiceImpl.gesture for clean reset semantics and thread safety.
 */

#include "Hardware/RotaryEncoder/RotaryEncoderEvents.h"
#include "Hardware/RotaryEncoder/RotaryEncoderInternal.h"
#include <esp_log.h>
#include <esp_timer.h>
#include <string.h>

#define TAG "RE_Gesture"

/* ── Acceleration Calculator ─────────────────────────────────────────── */

/**
 * @brief Compute the velocity multiplier based on inter-detent timing.
 *
 * @param delta_ms  Milliseconds since the previous detent event.
 * @return Multiplier in [1, RE_ACCEL_MAX_MULTIPLIER].
 */
static uint8_t CalculateVelocity(uint32_t delta_ms) {
  RotaryEncoderRuntimeConfig config;
  portENTER_CRITICAL(&rotaryEncoderService.spinlock);
  config = rotaryEncoderService.runtime_config;
  portEXIT_CRITICAL(&rotaryEncoderService.spinlock);

  if (!config.accel_enabled || delta_ms > config.accel_threshold_ms ||
      config.accel_threshold_ms == 0) {
    return 1;
  }

  if (delta_ms == 0) {
    delta_ms = 1;
  }

  uint8_t accel = config.accel_threshold_ms / delta_ms;
  if (accel > RE_ACCEL_MAX_MULTIPLIER) {
    accel = RE_ACCEL_MAX_MULTIPLIER;
  }
  if (accel < 1) {
    accel = 1;
  }
  return accel;
}

/* ── Rotation Processing ─────────────────────────────────────────────── */

void RotaryEncoder_ProcessRotationDetents(int32_t detents) {
  if (detents == 0) {
    return;
  }

  bool invert_direction;
  portENTER_CRITICAL(&rotaryEncoderService.spinlock);
  invert_direction = rotaryEncoderService.runtime_config.invert_direction;
  portEXIT_CRITICAL(&rotaryEncoderService.spinlock);

  int64_t effective_detents = invert_direction ? -(int64_t)detents : detents;
  if (effective_detents < INT16_MIN || effective_detents > INT16_MAX) {
    portENTER_CRITICAL(&rotaryEncoderService.spinlock);
    if (rotaryEncoderService.status.dropped_rotation_events < UINT32_MAX)
      rotaryEncoderService.status.dropped_rotation_events++;
    portEXIT_CRITICAL(&rotaryEncoderService.spinlock);
    ESP_LOGW(TAG, "Rejected detent count outside the rotation event range");
    return;
  }

  uint32_t now = (uint32_t)(esp_timer_get_time() / 1000ULL);
  RotationDirection dir = effective_detents > 0 ? ROTATION_CW : ROTATION_CCW;
  uint32_t delta_ms = now - rotaryEncoderService.gesture.last_rotation_time;
  uint8_t velocity = CalculateVelocity(delta_ms);
  rotaryEncoderService.gesture.last_rotation_time = now;

  int64_t scaled_steps = effective_detents * velocity;
  uint32_t abs_detents = (uint32_t)(effective_detents > 0 ? effective_detents : -effective_detents);

  portENTER_CRITICAL(&rotaryEncoderService.spinlock);
  int64_t position = (int64_t)rotaryEncoderService.status.absolute_position + scaled_steps;
  rotaryEncoderService.status.absolute_position = position > INT32_MAX ? INT32_MAX :
      position < INT32_MIN ? INT32_MIN : (int32_t)position;
  uint32_t rotations = rotaryEncoderService.status.total_rotations;
  rotaryEncoderService.status.total_rotations = UINT32_MAX - rotations < abs_detents ?
      UINT32_MAX : rotations + abs_detents;
  rotaryEncoderService.status.last_activity_ms = now;
  rotaryEncoderService.diagnostics.last_activity_ms = now;
  rotaryEncoderService.diagnostics.rotation_events++;
  portEXIT_CRITICAL(&rotaryEncoderService.spinlock);

  RotaryEncoderRotationEvent event = {
      .direction = dir,
      .steps = (int16_t)effective_detents,
      .velocity = velocity,
  };

  RotaryEncoderEvent queued_event = {
      .type = ROTARY_ENCODER_EVENT_ROTATION,
      .payload.rotation = event,
  };
  RotaryEncoder_EnqueueEvent(&queued_event);

  ESP_LOGD(TAG, "Rotation: detents=%ld vel=%u pos=%ld", (long)effective_detents,
           velocity, (long)rotaryEncoderService.status.absolute_position);
}

/* ── Gesture State Reset ─────────────────────────────────────────────── */

void RotaryEncoder_ResetGestureState(void) {
  memset(&rotaryEncoderService.gesture, 0, sizeof(RotaryEncoderGestureState));
}

/* ── Button Event Enqueue Helper ─────────────────────────────────────── */

/**
 * @brief Create a button event on the stack and enqueue it.
 */
static void EnqueueButtonEvent(ButtonGesture gesture, uint32_t duration_ms,
                               uint8_t click_count) {
  RotaryEncoderButtonEvent event = {
      .gesture = gesture,
      .duration_ms = duration_ms,
      .click_count = click_count,
  };

  RotaryEncoderEvent queued_event = {
      .type = ROTARY_ENCODER_EVENT_BUTTON,
      .payload.button = event,
  };
  RotaryEncoder_EnqueueEvent(&queued_event);
}

/* ── Button Event Processing ─────────────────────────────────────────── */

void RotaryEncoder_ProcessButtonEvent(RotaryEncoderRawEvent *raw) {
  if (!raw) {
    return;
  }

  RotaryEncoderGestureState *gs = &rotaryEncoderService.gesture;
  uint32_t now = raw->timestamp_ms;

  // Ignore unpaired releases and repeated edges without restarting hold timing.
  portENTER_CRITICAL(&rotaryEncoderService.spinlock);
  bool was_held = rotaryEncoderService.status.button_held;
  portEXIT_CRITICAL(&rotaryEncoderService.spinlock);
  if (raw->pressed == was_held) return;

  if (raw->pressed) {
    gs->press_start_time = now;
    EnqueueButtonEvent(GESTURE_PRESSED, 0, 0);
    gs->long_press_reported = false;
    gs->last_held_trigger_time = 0;

    portENTER_CRITICAL(&rotaryEncoderService.spinlock);
    rotaryEncoderService.status.button_held = true;
    rotaryEncoderService.status.last_activity_ms = now;
    rotaryEncoderService.diagnostics.last_activity_ms = now;
    rotaryEncoderService.diagnostics.button_events++;
    portEXIT_CRITICAL(&rotaryEncoderService.spinlock);
    return;
  }

  /* Release edge */
  uint16_t debounce_ms;
  portENTER_CRITICAL(&rotaryEncoderService.spinlock);
  rotaryEncoderService.status.button_held = false;
  debounce_ms = rotaryEncoderService.runtime_config.debounce_button_ms;
  portEXIT_CRITICAL(&rotaryEncoderService.spinlock);

  uint32_t duration = now - gs->press_start_time;
  if (duration < debounce_ms) {
    return;
  }

  portENTER_CRITICAL(&rotaryEncoderService.spinlock);
  rotaryEncoderService.status.total_clicks++;
  rotaryEncoderService.status.last_activity_ms = now;
  rotaryEncoderService.diagnostics.last_activity_ms = now;
  rotaryEncoderService.diagnostics.button_events++;
  portEXIT_CRITICAL(&rotaryEncoderService.spinlock);

  uint16_t double_click_ms;
  portENTER_CRITICAL(&rotaryEncoderService.spinlock);
  double_click_ms = rotaryEncoderService.runtime_config.double_click_ms;
  portEXIT_CRITICAL(&rotaryEncoderService.spinlock);

  if (gs->long_press_reported) {
    EnqueueButtonEvent(GESTURE_RELEASED, duration, 1);
    gs->click_count = 0;
    gs->last_click_time = now;
    return;
  }

  gs->click_count++;
  if (gs->click_count == 2 && (now - gs->last_click_time) <= double_click_ms) {
    EnqueueButtonEvent(GESTURE_DOUBLE_CLICK, duration, 2);
    gs->click_count = 0;
  } else if (gs->click_count >= 2) {
    gs->click_count = 1;
    EnqueueButtonEvent(GESTURE_SINGLE_CLICK, duration, 1);
  } else {
    EnqueueButtonEvent(GESTURE_SINGLE_CLICK, duration, 1);
  }

  gs->last_click_time = now;
}

/* ── Periodic Gesture Check ──────────────────────────────────────────── */

void RotaryEncoder_CheckGestures(void) {
  uint32_t now = (uint32_t)(esp_timer_get_time() / 1000ULL);
  bool is_held = false;
  uint16_t long_press_ms;
  uint16_t held_repeat_ms;

  portENTER_CRITICAL(&rotaryEncoderService.spinlock);
  is_held = rotaryEncoderService.status.button_held;
  long_press_ms = rotaryEncoderService.runtime_config.long_press_ms;
  held_repeat_ms = rotaryEncoderService.runtime_config.held_repeat_ms;
  portEXIT_CRITICAL(&rotaryEncoderService.spinlock);

  if (!is_held) {
    return;
  }

  RotaryEncoderGestureState *gs = &rotaryEncoderService.gesture;
  uint32_t duration = now - gs->press_start_time;

  if (!gs->long_press_reported && duration >= long_press_ms) {
    gs->long_press_reported = true;
    gs->last_held_trigger_time = now;
    EnqueueButtonEvent(GESTURE_LONG_PRESS, duration, 1);
    ESP_LOGD(TAG, "Long Press Detected (%lu ms)", (unsigned long)duration);
  }

  if (gs->long_press_reported &&
      now - gs->last_held_trigger_time >= held_repeat_ms) {
    gs->last_held_trigger_time = now;
    EnqueueButtonEvent(GESTURE_HELD, duration, 1);
  }
}

#undef TAG
