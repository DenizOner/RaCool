/**
 * @file BreathingRedAnim.c
 * @brief Animation plugin for smooth red breathing effect on the built-in LED.
 *
 * Implements a raised-cosine breathing envelope with power-curve perceptual
 * lightness correction, a gentle non-zero baseline floor and stable rounding.
 */

#include "Hardware/BuiltinLED/IAnimationPlugin.h"
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define BREATHING_RED_PERIOD_MS 4000UL

/**
 * @brief Internal state for the Breathing Red animation.
 */
typedef LEDAnimationBreathingContext BreathingRedContext;

/**
 * @brief Resets the breathing cycle to the start of the inhalation phase.
 *
 * Args:
 *     self: Pointer to the AnimationPlugin instance.
 */
static void BreathingRed_Reset(AnimationPlugin *self) {
    if (self && self->context) {
        BreathingRedContext *ctx = (BreathingRedContext *)self->context;
        ctx->timer = 0;
    }
}

/**
 * @brief Advances the breathing timer and returns the current frame color.
 *
 * Uses a smooth raised-cosine wave shaped with an eye-perceptual power curve (gamma 1.6)
 * and stable final-channel rounding.
 *
 * Args:
 *     self:     Pointer to the AnimationPlugin instance.
 *     delta_ms: Milliseconds elapsed since the previous Update() call.
 *
 * Returns:
 *     LEDColor with the red channel modulated by the breathing envelope.
 */
static LEDColor BreathingRed_Update(AnimationPlugin *self, uint32_t delta_ms) {
    LEDColor color = {0, 0, 0};
    if (self && self->context) {
        BreathingRedContext *ctx = (BreathingRedContext *)self->context;
        ctx->timer = ((uint64_t)ctx->timer + delta_ms) % BREATHING_RED_PERIOD_MS;

        /* Raised-cosine smooth breathing wave: [0, 1] */
        float phase    = (float)ctx->timer * (2.0f * (float)M_PI / (float)BREATHING_RED_PERIOD_MS);
        float raw_wave = 0.5f - 0.5f * cosf(phase);

        /* Perceptually-tuned breathing curve with a subtle 1.5% glow floor */
        const float min_val = 0.015f;
        float val = min_val + (1.0f - min_val) * powf(raw_wave, 1.6f);

        /* High-precision target red intensity */
        float target_r = val * (float)(LEDAnimation_GetBrightness(self) * 2);

        /* Stable rounding: frame-rate dithering is visible near darkness. */
        int out_int = (int)roundf(target_r);
        if (out_int < 0) out_int = 0;
        if (out_int > 255) out_int = 255;
        color.r = (uint8_t)out_int;
    }
    return color;
}

static BreathingRedContext breathing_red_ctx;
static AnimationPlugin breathing_red_plugin = {
    .type    = BREATHING_RED,
    .name    = "Breathing Red",
    .Reset   = BreathingRed_Reset,
    .Update  = BreathingRed_Update,
    .context = &breathing_red_ctx,
    .context_size = sizeof(breathing_red_ctx),
};

/**
 * @brief Returns the singleton Breathing Red animation plugin instance.
 *
 * Returns:
 *     Pointer to the statically allocated AnimationPlugin.
 */
AnimationPlugin *BreathingRedAnim_Get(void) {
    return &breathing_red_plugin;
}
