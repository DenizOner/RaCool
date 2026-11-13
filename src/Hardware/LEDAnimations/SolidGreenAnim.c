/**
 * @file SolidGreenAnim.c
 * @brief Animation plugin for constant solid green color on the built-in LED.
 *
 * Outputs a steady green pixel at BUILTIN_LED_DEFAULT_BRIGHTNESS. Global
 * brightness scaling is applied by the BuiltinLED driver after Update() returns.
 */

#include "Hardware/BuiltinLED/IAnimationPlugin.h"
#include <stddef.h>

/**
 * @brief Reset handler — no state to reset for a solid-color plugin.
 *
 * Args:
 *     self: Pointer to the AnimationPlugin instance (unused).
 */
static void SolidGreen_Reset(AnimationPlugin *self) {
    (void)self;
}

/**
 * @brief Returns a constant solid-green color.
 *
 * Args:
 *     self:     Pointer to the AnimationPlugin instance (unused).
 *     delta_ms: Elapsed time since the last frame (unused).
 *
 * Returns:
 *     LEDColor with green channel set to BUILTIN_LED_DEFAULT_BRIGHTNESS.
 */
static LEDColor SolidGreen_Update(AnimationPlugin *self, uint32_t delta_ms) {
    (void)self;
    (void)delta_ms;
    return (LEDColor){0, LEDAnimation_GetBrightness(self), 0};
}

static AnimationPlugin solid_green_plugin = {
    .type    = SOLID_GREEN,
    .name    = "Solid Green",
    .Reset   = SolidGreen_Reset,
    .Update  = SolidGreen_Update,
    .context = NULL,
    .context_size = 0,
};

/**
 * @brief Returns the singleton Solid Green animation plugin instance.
 *
 * Returns:
 *     Pointer to the statically-allocated AnimationPlugin.
 */
AnimationPlugin *SolidGreenAnim_Get(void) {
    return &solid_green_plugin;
}
