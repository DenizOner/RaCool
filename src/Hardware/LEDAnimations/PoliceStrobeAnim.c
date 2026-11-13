/**
 * @file PoliceStrobeAnim.c
 * @brief Animation plugin for police Red/Blue strobe effect on the built-in LED.
 */

#include "Hardware/BuiltinLED/IAnimationPlugin.h"

/**
 * @brief Context structure for the Police Strobe animation.
 */
typedef LEDAnimationClockContext PoliceStrobeContext;

/**
 * @brief Resets the police strobe animation timer.
 * 
 * Args:
 *     self: Pointer to the AnimationPlugin instance.
 */
static void PoliceStrobe_Reset(AnimationPlugin *self) {
    if (self && self->context) {
        PoliceStrobeContext *ctx = (PoliceStrobeContext *)self->context;
        ctx->timer = 0;
    }
}

/**
 * @brief Updates the Police Strobe animation frame.
 * 
 * Args:
 *     self: Pointer to the AnimationPlugin instance.
 *     delta_ms: Milliseconds elapsed since the last update.
 * 
 * Returns:
 *     The computed LEDColor.
 */
static LEDColor PoliceStrobe_Update(AnimationPlugin *self, uint32_t delta_ms) {
    LEDColor color = {0, 0, 0};
    if (self && self->context) {
        PoliceStrobeContext *ctx = (PoliceStrobeContext *)self->context;
        ctx->timer = ((uint64_t)ctx->timer + delta_ms) % 1000;
        
        uint8_t brightness = LEDAnimation_GetScaledBrightness(self, 2);
        
        // 0-500ms: Red strobe, 500-1000ms: Blue strobe
        if (ctx->timer < 500) {
            if ((ctx->timer % 100) < 50) {
                color.r = brightness;
            }
        } else {
            uint32_t b_timer = ctx->timer - 500;
            if ((b_timer % 100) < 50) {
                color.b = brightness;
            }
        }
    }
    return color;
}

static PoliceStrobeContext police_ctx;
static AnimationPlugin police_plugin = {
    .type = POLICE_STROBE,
    .name = "Police Strobe",
    .Reset = PoliceStrobe_Reset,
    .Update = PoliceStrobe_Update,
    .context = &police_ctx,
    .context_size = sizeof(police_ctx),
};

/**
 * @brief Retrieves the Police Strobe animation plugin instance.
 * 
 * Returns:
 *     Pointer to the AnimationPlugin.
 */
AnimationPlugin *PoliceStrobeAnim_Get(void) {
    return &police_plugin;
}
