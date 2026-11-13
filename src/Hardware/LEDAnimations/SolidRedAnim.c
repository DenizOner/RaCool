/**
 * @file SolidRedAnim.c
 * @brief Animation plugin for constant solid red color on the built-in LED.
 *
 * Outputs a steady red pixel at BUILTIN_LED_DEFAULT_BRIGHTNESS. Global
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
static void SolidRed_Reset(AnimationPlugin *self) {
    (void)self;
}

/**
 * @brief Returns a constant solid-red color.
 *
 * Args:
 *     self:     Pointer to the AnimationPlugin instance (unused).
 *     delta_ms: Elapsed time since the last frame (unused).
 *
 * Returns:
 *     LEDColor with red channel set to BUILTIN_LED_DEFAULT_BRIGHTNESS.
 */
static LEDColor SolidRed_Update(AnimationPlugin *self, uint32_t delta_ms) {
    (void)self;
    (void)delta_ms;
    return (LEDColor){LEDAnimation_GetBrightness(self), 0, 0};
}

static AnimationPlugin solid_red_plugin = {
    .type    = SOLID_RED,
    .name    = "Solid Red",
    .Reset   = SolidRed_Reset,
    .Update  = SolidRed_Update,
    .context = NULL,
    .context_size = 0,
};

/**
 * @brief Returns the singleton Solid Red animation plugin instance.
 *
 * Returns:
 *     Pointer to the statically-allocated AnimationPlugin.
 */
AnimationPlugin *SolidRedAnim_Get(void) {
    return &solid_red_plugin;
}
