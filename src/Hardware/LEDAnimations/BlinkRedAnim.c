/**
 * @file BlinkRedAnim.c
 * @brief Animation plugin for simple 1 Hz red blinking on the built-in LED.
 *
 * Produces a 500 ms on / 500 ms off red blink cycle. Global brightness
 * scaling is applied by the BuiltinLED driver after Update() returns.
 */

#include "Hardware/BuiltinLED/IAnimationPlugin.h"

/** @brief Period of one full blink cycle in milliseconds. */
#define BLINK_RED_PERIOD_MS 1000U

/** @brief Duration the LED is on within each cycle in milliseconds. */
#define BLINK_RED_ON_MS     500U

/**
 * @brief Internal state for the Blink Red animation.
 */
typedef LEDAnimationClockContext BlinkRedContext;

/**
 * @brief Resets the blink cycle timer to the start of a new cycle.
 *
 * Args:
 *     self: Pointer to the AnimationPlugin instance.
 */
static void BlinkRed_Reset(AnimationPlugin *self) {
    if (self && self->context) {
        BlinkRedContext *ctx = (BlinkRedContext *)self->context;
        ctx->timer = 0;
    }
}

/**
 * @brief Advances the blink timer and returns the current frame color.
 *
 * Args:
 *     self:     Pointer to the AnimationPlugin instance.
 *     delta_ms: Milliseconds elapsed since the previous Update() call.
 *
 * Returns:
 *     Red pixel during the ON phase, black during the OFF phase.
 */
static LEDColor BlinkRed_Update(AnimationPlugin *self, uint32_t delta_ms) {
    LEDColor color = {0, 0, 0};
    if (self && self->context) {
        BlinkRedContext *ctx = (BlinkRedContext *)self->context;
        ctx->timer = ((uint64_t)ctx->timer + delta_ms) % BLINK_RED_PERIOD_MS;
        if (ctx->timer < BLINK_RED_ON_MS) {
            color.r = LEDAnimation_GetBrightness(self);
        }
    }
    return color;
}

static BlinkRedContext blink_red_ctx;
static AnimationPlugin blink_red_plugin = {
    .type    = BLINK_RED,
    .name    = "Blink Red",
    .Reset   = BlinkRed_Reset,
    .Update  = BlinkRed_Update,
    .context = &blink_red_ctx,
    .context_size = sizeof(blink_red_ctx),
};

/**
 * @brief Returns the singleton Blink Red animation plugin instance.
 *
 * Returns:
 *     Pointer to the statically-allocated AnimationPlugin.
 */
AnimationPlugin *BlinkRedAnim_Get(void) {
    return &blink_red_plugin;
}
