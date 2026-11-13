/**
 * @file BreathingGreenAnim.c
 * @brief Animation plugin for green breathing on the built-in LED.
 *
 * Implements a raised-cosine breathing envelope with power-curve perceptual
 * lightness correction, a gentle non-zero baseline floor and stable rounding.
 */

#include "Hardware/BuiltinLED/IAnimationPlugin.h"
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define BREATHING_GREEN_PERIOD_MS 4000UL

/**
 * @brief Context structure for the Breathing Green animation.
 */
typedef LEDAnimationBreathingContext BreathingGreenContext;

/**
 * @brief Resets the green breathing animation timer state.
 * 
 * Args:
 *     self: Pointer to the AnimationPlugin instance.
 */
static void BreathingGreen_Reset(AnimationPlugin *self) {
    if (self && self->context) {
        BreathingGreenContext *ctx = (BreathingGreenContext *)self->context;
        ctx->timer = 0;
    }
}

/**
 * @brief Updates the Breathing Green animation frame.
 * 
 * Uses a smooth raised-cosine wave shaped with an eye-perceptual power curve (gamma 1.6)
 * and stable final-channel rounding. This ensures continuous fluid transitions
 * without visible stepping or dead-black pauses at low brightness.
 * 
 * Args:
 *     self: Pointer to the AnimationPlugin instance.
 *     delta_ms: Milliseconds elapsed since the last update.
 * 
 * Returns:
 *     The computed LEDColor.
 */
static LEDColor BreathingGreen_Update(AnimationPlugin *self, uint32_t delta_ms) {
    LEDColor color = {0, 0, 0};
    if (self && self->context) {
        BreathingGreenContext *ctx = (BreathingGreenContext *)self->context;
        ctx->timer = ((uint64_t)ctx->timer + delta_ms) % BREATHING_GREEN_PERIOD_MS;
        
        /* Raised-cosine smooth breathing wave: [0, 1] */
        float phase    = (float)ctx->timer * (2.0f * (float)M_PI / (float)BREATHING_GREEN_PERIOD_MS);
        float raw_wave = 0.5f - 0.5f * cosf(phase);

        /* Perceptually-tuned breathing curve with a subtle 1.5% glow floor */
        const float min_val = 0.015f;
        float val = min_val + (1.0f - min_val) * powf(raw_wave, 1.6f);

        /* High-precision target green intensity */
        float target_g = val * (float)(LEDAnimation_GetBrightness(self) * 2);

        /* Stable rounding: frame-rate dithering is visible near darkness. */
        int out_int = (int)roundf(target_g);
        if (out_int < 0) out_int = 0;
        if (out_int > 255) out_int = 255;
        color.g = (uint8_t)out_int;
    }
    return color;
}

static BreathingGreenContext breathing_ctx;
static AnimationPlugin breathing_plugin = {
    .type = BREATHING_GREEN,
    .name = "Breathing Green",
    .Reset = BreathingGreen_Reset,
    .Update = BreathingGreen_Update,
    .context = &breathing_ctx,
    .context_size = sizeof(breathing_ctx),
};

/**
 * @brief Retrieves the Breathing Green animation plugin instance.
 * 
 * Returns:
 *     Pointer to the AnimationPlugin.
 */
AnimationPlugin *BreathingGreenAnim_Get(void) {
    return &breathing_plugin;
}
