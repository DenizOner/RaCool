#ifndef LED_ANIMATION_PATTERNS_H
#define LED_ANIMATION_PATTERNS_H
#include "Hardware/BuiltinLED/IAnimationPlugin.h"

typedef enum {
    LED_PATTERN_BREATHING,
    LED_PATTERN_PULSE,
    LED_PATTERN_CANDLE,
    LED_PATTERN_ALERT,
    LED_PATTERN_FADE,
    LED_PATTERN_COLOR_CYCLE,
    LED_PATTERN_COUNT
} LEDAnimationPattern;

/* Candle uses an unsigned elapsed clock instead of a short repeating period. */
uint32_t LEDAnimation_PatternPeriod(LEDAnimationPattern pattern);
LEDColor LEDAnimation_RenderPattern(LEDAnimationPattern pattern, uint32_t time_ms,
                                     LEDColor base);
#endif
