#ifndef ROTARY_ENCODER_EVENTS_H
#define ROTARY_ENCODER_EVENTS_H

/**
 * @file RotaryEncoderEvents.h
 * @brief EventBus event name constants for the RotaryEncoder module.
 *
 * Each constant is a null-terminated string used as the event identifier
 * when publishing or subscribing through the IEventBus interface.
 */

/** @brief Knob rotated (payload: RotaryEncoderRotationEvent). */
#define RE_EVENT_ROTATED          "RE_Rotated"

/** @brief Single click detected (payload: RotaryEncoderButtonEvent). */
#define RE_EVENT_BUTTON_PRESSED   "RE_ButtonPressed"

#define RE_EVENT_BUTTON_CLICK     "RE_ButtonClick"

/** @brief Double click detected (payload: RotaryEncoderButtonEvent). */
#define RE_EVENT_BUTTON_DBLCLICK  "RE_ButtonDblClick"

/** @brief Long press detected (payload: RotaryEncoderButtonEvent). */
#define RE_EVENT_BUTTON_LONGPRESS "RE_ButtonLongPress"

/** @brief Button is being held after long press (payload: RotaryEncoderButtonEvent). */
#define RE_EVENT_BUTTON_HELD      "RE_ButtonHeld"

/** @brief Button released after a long press / hold (payload: RotaryEncoderButtonEvent). */
#define RE_EVENT_BUTTON_RELEASED  "RE_ButtonReleased"

/** @brief Service state transition (payload: ServiceState). */
#define RE_EVENT_STATE_CHANGED    "RE_StateChanged"

/** @brief Runtime configuration changed (payload: RotaryEncoderRuntimeConfig). */
#define RE_EVENT_CONFIG_CHANGED   "RE_ConfigChanged"

/** @brief Debug console command executed. */
#define RE_EVENT_DEBUG_COMMAND    "RE_DebugCommand"

/** @brief Periodic health check report. */
#define RE_EVENT_HEALTH           "RE_Health"

#endif /* ROTARY_ENCODER_EVENTS_H */
