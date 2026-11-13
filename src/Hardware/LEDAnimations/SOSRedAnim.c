/**
 * @file SOSRedAnim.c
 * @brief Animation plugin for SOS Morse code red color blinking on the built-in LED.
 */

#include "Hardware/BuiltinLED/IAnimationPlugin.h"

/**
 * @brief Context structure for the SOS Red animation.
 */
typedef LEDAnimationClockContext SOSRedContext;

/**
 * @brief Resets the SOS Red animation timer.
 * 
 * Args:
 *     self: Pointer to the AnimationPlugin instance.
 */
static void SOSRed_Reset(AnimationPlugin *self) {
    if (self && self->context) {
        SOSRedContext *ctx = (SOSRedContext *)self->context;
        ctx->timer = 0;
    }
}

/**
 * @brief Updates the SOS Red animation frame.
 * 
 * Args:
 *     self: Pointer to the AnimationPlugin instance.
 *     delta_ms: Milliseconds elapsed since the last update.
 * 
 * Returns:
 *     The computed LEDColor.
 */
static LEDColor SOSRed_Update(AnimationPlugin *self, uint32_t delta_ms) {
    LEDColor color = {0, 0, 0};
    if (self && self->context) {
        SOSRedContext *ctx = (SOSRedContext *)self->context;
        ctx->timer = ((uint64_t)ctx->timer + delta_ms) % 4800;
        
        uint8_t brightness = LEDAnimation_GetBrightness(self);
        
        // S (...)  : 3x 200ms on, 200ms off -> 0ms to 1200ms
        // O (---)  : 3x 600ms on, 200ms off -> 1200ms to 3600ms
        // S (...)  : 3x 200ms on, 200ms off -> 3600ms to 4800ms
        if (ctx->timer < 1200) {
            // S
            if ((ctx->timer % 400) < 200) {
                color.r = brightness;
            }
        } else if (ctx->timer < 3600) {
            // O
            uint32_t o_timer = ctx->timer - 1200;
            if ((o_timer % 800) < 600) {
                color.r = brightness;
            }
        } else if (ctx->timer < 4800) {
            // S
            uint32_t s2_timer = ctx->timer - 3600;
            if ((s2_timer % 400) < 200) {
                color.r = brightness;
            }
        }
    }
    return color;
}

static SOSRedContext sos_ctx;
static AnimationPlugin sos_plugin = {
    .type = SOS_RED,
    .name = "SOS Red",
    .Reset = SOSRed_Reset,
    .Update = SOSRed_Update,
    .context = &sos_ctx,
    .context_size = sizeof(sos_ctx),
};

/**
 * @brief Retrieves the SOS Red animation plugin instance.
 * 
 * Returns:
 *     Pointer to the AnimationPlugin.
 */
AnimationPlugin *SOSRedAnim_Get(void) {
    return &sos_plugin;
}
