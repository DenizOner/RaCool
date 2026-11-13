/**
 * @file FadeBlueAnim.c
 * @brief Animation plugin for linear blue fading effect on the built-in LED.
 */

#include "Hardware/BuiltinLED/IAnimationPlugin.h"

/**
 * @brief Context structure for the Fade Blue animation.
 */
typedef LEDAnimationClockContext FadeBlueContext;

/**
 * @brief Resets the blue fade animation timer.
 * 
 * Args:
 *     self: Pointer to the AnimationPlugin instance.
 */
static void FadeBlue_Reset(AnimationPlugin *self) {
    if (self && self->context) {
        FadeBlueContext *ctx = (FadeBlueContext *)self->context;
        ctx->timer = 0;
    }
}

/**
 * @brief Updates the Fade Blue animation frame.
 * 
 * Args:
 *     self: Pointer to the AnimationPlugin instance.
 *     delta_ms: Milliseconds elapsed since the last update.
 * 
 * Returns:
 *     The computed LEDColor.
 */
static LEDColor FadeBlue_Update(AnimationPlugin *self, uint32_t delta_ms) {
    LEDColor color = {0, 0, 0};
    if (self && self->context) {
        FadeBlueContext *ctx = (FadeBlueContext *)self->context;
        ctx->timer = ((uint64_t)ctx->timer + delta_ms) % 3000;
        
        // Linear fade in and out
        uint8_t val;
        if (ctx->timer < 1500) {
            val = (uint8_t)(ctx->timer * LEDAnimation_GetBrightness(self) / 1500);
        } else {
            val = (uint8_t)((3000 - ctx->timer) * LEDAnimation_GetBrightness(self) / 1500);
        }
        color.b = val;
    }
    return color;
}

static FadeBlueContext fade_ctx;
static AnimationPlugin fade_plugin = {
    .type = FADE_BLUE,
    .name = "Fade Blue",
    .Reset = FadeBlue_Reset,
    .Update = FadeBlue_Update,
    .context = &fade_ctx,
    .context_size = sizeof(fade_ctx),
};

/**
 * @brief Retrieves the Fade Blue animation plugin instance.
 * 
 * Returns:
 *     Pointer to the AnimationPlugin.
 */
AnimationPlugin *FadeBlueAnim_Get(void) {
    return &fade_plugin;
}
