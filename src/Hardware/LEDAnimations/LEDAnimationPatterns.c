#include "Hardware/LEDAnimations/LEDAnimationPatterns.h"
#include "Hardware/LEDAnimations/LEDAnimationPatternsConfig.h"
#include <math.h>

_Static_assert(LED_PATTERN_BREATHING_PERIOD_MS > 0, "Breathing period must be positive");
_Static_assert(LED_PATTERN_PULSE_PERIOD_MS >= 5, "Pulse must have an on interval");
_Static_assert(LED_PATTERN_ALERT_PERIOD_MS >= 2, "Alert must have two phases");
_Static_assert(LED_PATTERN_FADE_PERIOD_MS >= 2 && LED_PATTERN_FADE_PERIOD_MS % 2 == 0,
               "Fade period must contain two equal phases");
_Static_assert(LED_PATTERN_COLOR_CYCLE_PERIOD_MS >= 6, "Color cycle must have six phases");

uint32_t LEDAnimation_PatternPeriod(LEDAnimationPattern pattern) {
    switch (pattern) {
        case LED_PATTERN_BREATHING: return LED_PATTERN_BREATHING_PERIOD_MS;
        case LED_PATTERN_PULSE: return LED_PATTERN_PULSE_PERIOD_MS;
        case LED_PATTERN_ALERT: return LED_PATTERN_ALERT_PERIOD_MS;
        case LED_PATTERN_FADE: return LED_PATTERN_FADE_PERIOD_MS;
        case LED_PATTERN_COLOR_CYCLE: return LED_PATTERN_COLOR_CYCLE_PERIOD_MS;
        default: return 0;
    }
}

static LEDColor ScaleColor(LEDColor base, float intensity) {
    return (LEDColor){(uint8_t)(base.r * intensity + 0.5f), (uint8_t)(base.g * intensity + 0.5f),
                      (uint8_t)(base.b * intensity + 0.5f)};
}

/* Stateless hashed knots avoid frame-rate aliasing and shared RNG ownership.
 * Smooth interpolation makes irregular flame motion independent of call rate. */
static float CandleNoise(uint32_t knot) {
    knot ^= knot >> 16;
    knot *= 0x7feb352dU;
    knot ^= knot >> 15;
    knot *= 0x846ca68bU;
    knot ^= knot >> 16;
    return (float)(knot & 0xffffU) / 65535.0f;
}
static float CandleWave(uint32_t time_ms, uint32_t interval, uint32_t salt) {
    uint32_t index = time_ms / interval;
    float weight = (float)(time_ms % interval) / interval;
    weight = weight * weight * (3.0f - 2.0f * weight);
    float first = CandleNoise(index ^ salt);
    return first + (CandleNoise((index + 1U) ^ salt) - first) * weight;
}

LEDColor LEDAnimation_RenderPattern(LEDAnimationPattern pattern, uint32_t time_ms,
                                     LEDColor base) {
    if ((unsigned)pattern >= LED_PATTERN_COUNT) return (LEDColor){0};
    uint32_t period = LEDAnimation_PatternPeriod(pattern);
    uint32_t phase = period ? time_ms % period : time_ms;
    switch (pattern) {
        case LED_PATTERN_BREATHING:
            return ScaleColor(base, 0.5f * (1.0f - cosf(6.2831853071795864769f * phase / period)));
        case LED_PATTERN_PULSE:
            return phase < period / 5 ? base : (LEDColor){0};
        case LED_PATTERN_ALERT:
            return phase < period / 2 ? base : (LEDColor){0};
        case LED_PATTERN_CANDLE: {
            uint8_t peak = base.r > base.g ? base.r : base.g;
            if (base.b > peak) peak = base.b;
            float intensity = 0.65f + 0.12f * CandleWave(time_ms, LED_PATTERN_CANDLE_FAST_MS, 0x53acU)
                                   + 0.23f * CandleWave(time_ms, LED_PATTERN_CANDLE_SLOW_MS, 0xa19fU);
            return (LEDColor){(uint8_t)(peak * intensity * LED_PATTERN_CANDLE_RED / 255.0f + 0.5f),
                              (uint8_t)(peak * intensity * LED_PATTERN_CANDLE_GREEN / 255.0f + 0.5f),
                              (uint8_t)(peak * intensity * LED_PATTERN_CANDLE_BLUE / 255.0f + 0.5f)};
        }
        case LED_PATTERN_FADE: {
            uint32_t amount = phase < period / 2 ? phase : period - phase;
            return (LEDColor){(uint8_t)((uint64_t)base.r * amount / (period / 2)),
                              (uint8_t)((uint64_t)base.g * amount / (period / 2)),
                              (uint8_t)((uint64_t)base.b * amount / (period / 2))};
        }
        case LED_PATTERN_COLOR_CYCLE: {
            static const LEDColor colors[] = {{1,0,0},{1,1,0},{0,1,0},{0,1,1},{0,0,1},{1,0,1}};
            uint32_t index = (uint32_t)((uint64_t)phase * 6U / period);
            uint8_t brightness = base.r > base.g ? base.r : base.g;
            if (base.b > brightness) brightness = base.b;
            LEDColor color = colors[index];
            return (LEDColor){color.r * brightness, color.g * brightness, color.b * brightness};
        }
        default: return (LEDColor){0};
    }
}
