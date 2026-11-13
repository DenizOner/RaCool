/**
 * @file SolidWhiteAnim.c
 * @brief Animation plugin for constant solid white color on the built-in LED.
 *
 * Outputs equal R/G/B at BUILTIN_LED_DEFAULT_BRIGHTNESS to produce white.
 * Global brightness scaling is applied by the BuiltinLED driver after
 * Update() returns.
 */

#include "Hardware/BuiltinLED/IAnimationPlugin.h"
#include <stddef.h>

/**
 * @brief Reset handler — no state to reset for a solid-color plugin.
 *
 * Args:
 *     self: Pointer to the AnimationPlugin instance (unused).
 */
static void SolidWhite_Reset(AnimationPlugin *self) {
    (void)self;
}

/**
 * @brief Returns a constant solid-white color.
 *
 * Args:
 *     self:     Pointer to the AnimationPlugin instance (unused).
 *     delta_ms: Elapsed time since the last frame (unused).
 *
 * Returns:
 *     LEDColor with all channels set to BUILTIN_LED_DEFAULT_BRIGHTNESS.
 */
static LEDColor SolidWhite_Update(AnimationPlugin *self, uint32_t delta_ms) {
    (void)self;
    (void)delta_ms;
    return (LEDColor){
        LEDAnimation_GetBrightness(self),
        LEDAnimation_GetBrightness(self),
        LEDAnimation_GetBrightness(self),
    };
}

static AnimationPlugin solid_white_plugin = {
    .type    = SOLID_WHITE,
    .name    = "Solid White",
    .Reset   = SolidWhite_Reset,
    .Update  = SolidWhite_Update,
    .context = NULL,
    .context_size = 0,
};

/**
 * @brief Returns the singleton Solid White animation plugin instance.
 *
 * Returns:
 *     Pointer to the statically-allocated AnimationPlugin.
 */
AnimationPlugin *SolidWhiteAnim_Get(void) {
    return &solid_white_plugin;
}
