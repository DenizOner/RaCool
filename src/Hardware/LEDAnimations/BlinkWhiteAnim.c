/**
 * @file BlinkWhiteAnim.c
 * @brief Animation plugin for blinking the built-in LED in white color.
 * 
 * Part of the BuiltinLED animation subsystem.
 */

#include "Hardware/BuiltinLED/IAnimationPlugin.h"

/**
 * @brief Context structure for the Blink White animation.
 */
typedef LEDAnimationClockContext BlinkWhiteContext;

/**
 * @brief Resets the animation timer to 0.
 * 
 * Args:
 *     self: Pointer to the AnimationPlugin instance.
 */
static void BlinkWhite_Reset(AnimationPlugin *self) {
    if (self && self->context) {
        BlinkWhiteContext *ctx = (BlinkWhiteContext *)self->context;
        ctx->timer = 0;
    }
}

/**
 * @brief Computes the next frame of the Blink White animation.
 * 
 * Args:
 *     self: Pointer to the AnimationPlugin instance.
 *     delta_ms: Milliseconds elapsed since the last update.
 * 
 * Returns:
 *     The computed LEDColor for the current frame.
 */
static LEDColor BlinkWhite_Update(AnimationPlugin *self, uint32_t delta_ms) {
    LEDColor color = {0, 0, 0};
    if (self && self->context) {
        BlinkWhiteContext *ctx = (BlinkWhiteContext *)self->context;
        ctx->timer = ((uint64_t)ctx->timer + delta_ms) % 1000;
        
        if (ctx->timer < 500) {
            color.r = LEDAnimation_GetBrightness(self) / 2;
            color.g = LEDAnimation_GetBrightness(self) / 2;
            color.b = LEDAnimation_GetBrightness(self) / 2;
        }
    }
    return color;
}

static BlinkWhiteContext blink_ctx;
static AnimationPlugin blink_plugin = {
    .type = BLINK_WHITE,
    .name = "Blink White",
    .Reset = BlinkWhite_Reset,
    .Update = BlinkWhite_Update,
    .context = &blink_ctx,
    .context_size = sizeof(blink_ctx),
};

/**
 * @brief Retrieves the Blink White animation plugin instance.
 * 
 * Returns:
 *     Pointer to the AnimationPlugin.
 */
AnimationPlugin *BlinkWhiteAnim_Get(void) {
    return &blink_plugin;
}
