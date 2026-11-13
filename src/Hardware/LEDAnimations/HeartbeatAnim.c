/**
 * @file HeartbeatAnim.c
 * @brief Animation plugin for double-pulse heartbeat red color effect on the built-in LED.
 */

#include "Hardware/BuiltinLED/IAnimationPlugin.h"

/**
 * @brief Context structure for the Heartbeat Red animation.
 */
typedef LEDAnimationClockContext HeartbeatContext;

/**
 * @brief Resets the heartbeat animation timer.
 * 
 * Args:
 *     self: Pointer to the AnimationPlugin instance.
 */
static void Heartbeat_Reset(AnimationPlugin *self) {
    if (self && self->context) {
        HeartbeatContext *ctx = (HeartbeatContext *)self->context;
        ctx->timer = 0;
    }
}

/**
 * @brief Updates the Heartbeat Red animation frame.
 * 
 * Args:
 *     self: Pointer to the AnimationPlugin instance.
 *     delta_ms: Milliseconds elapsed since the last update.
 * 
 * Returns:
 *     The computed LEDColor.
 */
static LEDColor Heartbeat_Update(AnimationPlugin *self, uint32_t delta_ms) {
    LEDColor color = {0, 0, 0};
    if (self && self->context) {
        HeartbeatContext *ctx = (HeartbeatContext *)self->context;
        ctx->timer = ((uint64_t)ctx->timer + delta_ms) % 1000;
        
        // Double pulse: 0-100ms On, 100-200ms Off, 200-300ms On, 300-1000ms Off
        if (ctx->timer < 100 || (ctx->timer >= 200 && ctx->timer < 300)) {
            color.r = LEDAnimation_GetBrightness(self);
        }
    }
    return color;
}

static HeartbeatContext heartbeat_ctx;
static AnimationPlugin heartbeat_plugin = {
    .type = HEARTBEAT_RED,
    .name = "Heartbeat Red",
    .Reset = Heartbeat_Reset,
    .Update = Heartbeat_Update,
    .context = &heartbeat_ctx,
    .context_size = sizeof(heartbeat_ctx),
};

/**
 * @brief Retrieves the Heartbeat Red animation plugin instance.
 * 
 * Returns:
 *     Pointer to the AnimationPlugin.
 */
AnimationPlugin *HeartbeatAnim_Get(void) {
    return &heartbeat_plugin;
}
