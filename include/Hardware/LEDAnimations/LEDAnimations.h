#ifndef LED_ANIMATIONS_H
#define LED_ANIMATIONS_H

#include "Hardware/BuiltinLED/IAnimationPlugin.h"
#include <esp_err.h>

/**
 * @file LEDAnimations.h
 * @brief Factory accessors for all LED animation plugins.
 *
 * Templates retain the legacy BuiltinLED state. Independent players must use
 * caller-owned instances; sharing a template context between workers is unsafe.
 */

/* Bounded catalog state storage; no heap allocation or worker is introduced. */
typedef union {
    LEDAnimationClockContext clock;
    LEDAnimationBreathingContext breathing;
} LEDAnimationContextState;

#define LED_ANIMATION_CONTEXT_CAPACITY sizeof(LEDAnimationContextState)

typedef struct {
    AnimationPlugin plugin;
    LEDAnimationContextState state;
} LEDAnimationInstance;

/* Metadata access does not reset the template's existing playback state. */
AnimationPlugin *LEDAnimations_GetTemplate(BuiltinLEDAnimationType type);

/* Storage belongs to the caller and must outlive every Reset/Update call.
 * Create before admission to a worker, or while that player's lock is held.
 * Invalid input leaves the destination unchanged. */
esp_err_t LEDAnimations_CreateInstance(BuiltinLEDAnimationType type,
                                       LEDAnimationInstance *instance);

// --- Complex animated plugins ---
AnimationPlugin *HeartbeatAnim_Get(void);
AnimationPlugin *BlinkWhiteAnim_Get(void);
AnimationPlugin *DoubleBlinkWhiteAnim_Get(void);
AnimationPlugin *SOSRedAnim_Get(void);
AnimationPlugin *BreathingGreenAnim_Get(void);
AnimationPlugin *FadeBlueAnim_Get(void);
AnimationPlugin *StrobeWhiteAnim_Get(void);
AnimationPlugin *RainbowFlowAnim_Get(void);
AnimationPlugin *PoliceStrobeAnim_Get(void);

// --- Static solid-color plugins ---
AnimationPlugin *SolidRedAnim_Get(void);
AnimationPlugin *SolidGreenAnim_Get(void);
AnimationPlugin *SolidBlueAnim_Get(void);
AnimationPlugin *SolidWhiteAnim_Get(void);

// --- Simple blink / breathing plugins ---
AnimationPlugin *BlinkRedAnim_Get(void);
AnimationPlugin *BreathingRedAnim_Get(void);

AnimationPlugin *GenericBreathingAnim_Get(void);
AnimationPlugin *GenericPulseAnim_Get(void);
AnimationPlugin *GenericCandleAnim_Get(void);
AnimationPlugin *GenericAlertAnim_Get(void);
AnimationPlugin *GenericFadeAnim_Get(void);
AnimationPlugin *GenericColorCycleAnim_Get(void);

#endif // LED_ANIMATIONS_H
