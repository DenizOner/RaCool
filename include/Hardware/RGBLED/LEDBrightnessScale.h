#ifndef RACOOL_LED_BRIGHTNESS_SCALE_H
#define RACOOL_LED_BRIGHTNESS_SCALE_H
#include <stdint.h>
/* External ZCL level reserves 255; both hardware maxima display as 100%. */
static inline unsigned LEDBrightness_Percent(uint8_t raw, unsigned maximum) {
    return raw >= maximum ? 100u : ((unsigned)raw * 100u + maximum / 2u) / maximum;
}
static inline uint8_t LEDBrightness_Step(uint8_t raw, int direction, unsigned maximum) {
    unsigned current = LEDBrightness_Percent(raw, maximum);
    int next = (int)current + direction;
    if (next < 0) next = 0;
    if (next > 100) next = 100;
    if ((unsigned)next == current) return raw;
    return next == 100 ? 255 : (uint8_t)((next * maximum + 50u) / 100u);
}
#endif
