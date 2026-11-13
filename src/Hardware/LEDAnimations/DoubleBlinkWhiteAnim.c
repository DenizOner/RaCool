/**
 * @file DoubleBlinkWhiteAnim.c
 * @brief Animation plugin for double-pulse white blink (long pause, two quick blinks).
 */

#include <stdbool.h>
#include "Hardware/BuiltinLED/IAnimationPlugin.h"

#define DOUBLE_BLINK_TOTAL_PERIOD_MS  2000U
#define PULSE_ON_MS                    80U
#define PULSE_GAP_MS                   120U

typedef LEDAnimationClockContext DoubleBlinkContext;

static DoubleBlinkContext s_ctx = {0};

static void DoubleBlinkWhite_Reset(AnimationPlugin *self) {
    if (self && self->context) {
        DoubleBlinkContext *ctx = (DoubleBlinkContext *)self->context;
        ctx->timer = 0;
    }
}

static LEDColor DoubleBlinkWhite_Update(AnimationPlugin *self, uint32_t delta_ms) {
    LEDColor color = {0, 0, 0};
    if (self && self->context) {
        DoubleBlinkContext *ctx = (DoubleBlinkContext *)self->context;
        ctx->timer = ((uint64_t)ctx->timer + delta_ms) % DOUBLE_BLINK_TOTAL_PERIOD_MS;
        
        uint32_t t = ctx->timer;
        // First pulse: 0 to PULSE_ON_MS
        // Gap: PULSE_ON_MS to PULSE_ON_MS + PULSE_GAP_MS
        // Second pulse: (PULSE_ON_MS + PULSE_GAP_MS) to (2*PULSE_ON_MS + PULSE_GAP_MS)
        // Long pause: remainder until 2000ms
        bool on = (t < PULSE_ON_MS) || 
                  (t >= (PULSE_ON_MS + PULSE_GAP_MS) && t < (2U * PULSE_ON_MS + PULSE_GAP_MS));
        
        if (on) {
            uint8_t bri = LEDAnimation_GetBrightness(self);
            color.r = bri;
            color.g = bri;
            color.b = bri;
        }
    }
    return color;
}

static AnimationPlugin s_plugin = {
    .type    = DOUBLE_BLINK_WHITE,
    .name    = "Double Blink White",
    .Reset   = DoubleBlinkWhite_Reset,
    .Update  = DoubleBlinkWhite_Update,
    .context = &s_ctx,
    .context_size = sizeof(s_ctx),
};

AnimationPlugin *DoubleBlinkWhiteAnim_Get(void) {
    return &s_plugin;
}
