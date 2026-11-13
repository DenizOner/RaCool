/**
 * @file SolidBlueAnim.c
 * @brief Animation plugin for constant solid blue color on the built-in LED.
 *
 * Outputs a steady blue pixel at BUILTIN_LED_DEFAULT_BRIGHTNESS. Global
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
static void SolidBlue_Reset(AnimationPlugin *self) {
    (void)self;
}

/**
 * @brief Returns a constant solid-blue color.
 *
 * Args:
 *     self:     Pointer to the AnimationPlugin instance (unused).
 *     delta_ms: Elapsed time since the last frame (unused).
 *
 * Returns:
 *     LEDColor with blue channel set to BUILTIN_LED_DEFAULT_BRIGHTNESS.
 */
static LEDColor SolidBlue_Update(AnimationPlugin *self, uint32_t delta_ms) {
    (void)self;
    (void)delta_ms;
    return (LEDColor){0, 0, LEDAnimation_GetBrightness(self)};
}

static AnimationPlugin solid_blue_plugin = {
    .type    = SOLID_BLUE,
    .name    = "Solid Blue",
    .Reset   = SolidBlue_Reset,
    .Update  = SolidBlue_Update,
    .context = NULL,
    .context_size = 0,
};

/**
 * @brief Returns the singleton Solid Blue animation plugin instance.
 *
 * Returns:
 *     Pointer to the statically-allocated AnimationPlugin.
 */
AnimationPlugin *SolidBlueAnim_Get(void) {
    return &solid_blue_plugin;
}
