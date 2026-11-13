/**
 * @file StrobeWhiteAnim.c
 * @brief Animation plugin for fast high-intensity white strobe effect on the built-in LED.
 */

#include "Hardware/BuiltinLED/IAnimationPlugin.h"

/**
 * @brief Context structure for the Strobe White animation.
 */
typedef LEDAnimationClockContext StrobeWhiteContext;

/**
 * @brief Resets the white strobe animation timer.
 * 
 * Args:
 *     self: Pointer to the AnimationPlugin instance.
 */
static void StrobeWhite_Reset(AnimationPlugin *self) {
    if (self && self->context) {
        StrobeWhiteContext *ctx = (StrobeWhiteContext *)self->context;
        ctx->timer = 0;
    }
}

/**
 * @brief Updates the Strobe White animation frame.
 * 
 * Args:
 *     self: Pointer to the AnimationPlugin instance.
 *     delta_ms: Milliseconds elapsed since the last update.
 * 
 * Returns:
 *     The computed LEDColor.
 */
static LEDColor StrobeWhite_Update(AnimationPlugin *self, uint32_t delta_ms) {
    LEDColor color = {0, 0, 0};
    if (self && self->context) {
        StrobeWhiteContext *ctx = (StrobeWhiteContext *)self->context;
        ctx->timer = ((uint64_t)ctx->timer + delta_ms) % 100;
        
        if (ctx->timer < 50) {
            uint32_t brightness = LEDAnimation_GetBrightness(self) * 4;
            color.r = (brightness > 255) ? 255 : brightness;
            color.g = (brightness > 255) ? 255 : brightness;
            color.b = (brightness > 255) ? 255 : brightness;
        }
    }
    return color;
}

static StrobeWhiteContext strobe_ctx;
static AnimationPlugin strobe_plugin = {
    .type = STROBE_WHITE,
    .name = "Strobe White",
    .Reset = StrobeWhite_Reset,
    .Update = StrobeWhite_Update,
    .context = &strobe_ctx,
    .context_size = sizeof(strobe_ctx),
};

/**
 * @brief Retrieves the Strobe White animation plugin instance.
 * 
 * Returns:
 *     Pointer to the AnimationPlugin.
 */
AnimationPlugin *StrobeWhiteAnim_Get(void) {
    return &strobe_plugin;
}
