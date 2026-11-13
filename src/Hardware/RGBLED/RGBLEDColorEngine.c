#include "Hardware/RGBLED/RGBLEDColorEngine.h"
#include <math.h>
#include "Hardware/RGBLED/RGBLEDConfig.h"

#ifndef MIN
#define MIN(a,b) (((a)<(b))?(a):(b))
#endif
#ifndef MAX
#define MAX(a,b) (((a)>(b))?(a):(b))
#endif

RGBColor RGBLEDColorEngine_HSVtoRGB(HSVColor hsv) {
    RGBColor rgb = {0, 0, 0};
    
    if (hsv.s == 0) {
        rgb.r = rgb.g = rgb.b = hsv.v;
        return rgb;
    }

    // Keep hue in degrees so exact primary hues do not acquire stray channels.
    const uint16_t hue = hsv.h % 360u;
    const uint8_t region = hue / 60u;
    const uint16_t fraction = hue % 60u;
    const uint8_t p = ((uint32_t)hsv.v * (255u - hsv.s) + 127u) / 255u;
    const uint8_t q = ((uint32_t)hsv.v * (15300u - hsv.s * fraction) + 7650u) / 15300u;
    const uint8_t t = ((uint32_t)hsv.v * (15300u - hsv.s * (60u - fraction)) + 7650u) / 15300u;

    switch (region) {
        case 0:  rgb.r = hsv.v; rgb.g = t; rgb.b = p; break;
        case 1:  rgb.r = q; rgb.g = hsv.v; rgb.b = p; break;
        case 2:  rgb.r = p; rgb.g = hsv.v; rgb.b = t; break;
        case 3:  rgb.r = p; rgb.g = q; rgb.b = hsv.v; break;
        case 4:  rgb.r = t; rgb.g = p; rgb.b = hsv.v; break;
        default: rgb.r = hsv.v; rgb.g = p; rgb.b = q; break;
    }

    return rgb;
}

HSVColor RGBLEDColorEngine_RGBtoHSV(RGBColor rgb) {
    HSVColor hsv;
    uint8_t min_val = MIN(MIN(rgb.r, rgb.g), rgb.b);
    uint8_t max_val = MAX(MAX(rgb.r, rgb.g), rgb.b);
    
    hsv.v = max_val;
    
    if (max_val == 0) {
        hsv.s = 0;
        hsv.h = 0;
        return hsv;
    }
    
    uint8_t delta = max_val - min_val;
    hsv.s = (uint8_t)(((uint16_t)delta * 255) / max_val);
    
    if (delta == 0) {
        hsv.h = 0;
        return hsv;
    }
    
    int32_t h = 0;
    if (rgb.r == max_val) {
        h = (int32_t)(rgb.g - rgb.b) * 60 / delta;
        if (rgb.g < rgb.b) h += 360;
    } else if (rgb.g == max_val) {
        h = (int32_t)(rgb.b - rgb.r) * 60 / delta + 120;
    } else {
        h = (int32_t)(rgb.r - rgb.g) * 60 / delta + 240;
    }
    
    hsv.h = h % 360;
    return hsv;
}

RGBColor RGBLEDColorEngine_GetPresetColor(PresetColorIndex index) {
    switch (index) {
        case RGB_COLOR_BLUE:    return (RGBColor){0, 0, 255};
        case RGB_COLOR_CYAN:    return (RGBColor){0, 255, 255};
        case RGB_COLOR_GREEN:   return (RGBColor){0, 255, 0};
        case RGB_COLOR_YELLOW:  return (RGBColor){255, 255, 0};
        case RGB_COLOR_ORANGE:  return (RGBColor){255, 128, 0};
        case RGB_COLOR_RED:     return (RGBColor){255, 0, 0};
        case RGB_COLOR_PURPLE:  return (RGBColor){128, 0, 128};
        case RGB_COLOR_WHITE:   return (RGBColor){255, 255, 255};
        default:                return (RGBColor){0, 0, 0};
    }
}

const char* RGBLEDColorEngine_GetPresetColorName(PresetColorIndex index) {
    switch (index) {
        case RGB_COLOR_BLUE:    return "Mavi";
        case RGB_COLOR_CYAN:    return "Turkuaz";
        case RGB_COLOR_GREEN:   return "Yesil";
        case RGB_COLOR_YELLOW:  return "Sari";
        case RGB_COLOR_ORANGE:  return "Turuncu";
        case RGB_COLOR_RED:     return "Kirmizi";
        case RGB_COLOR_PURPLE:  return "Mor";
        case RGB_COLOR_WHITE:   return "Beyaz";
        default:                return "Bilinmeyen";
    }
}

/* Canonical CIE xy chromaticities (ZCL 0-65279 scale) for each preset. */
static const uint16_t s_preset_xy[RGB_COLOR_COUNT][2] = {
    {RGBLED_PRESET_BLUE_X, RGBLED_PRESET_BLUE_Y},
    {RGBLED_PRESET_CYAN_X, RGBLED_PRESET_CYAN_Y},
    {RGBLED_PRESET_GREEN_X, RGBLED_PRESET_GREEN_Y},
    {RGBLED_PRESET_YELLOW_X, RGBLED_PRESET_YELLOW_Y},
    {RGBLED_PRESET_ORANGE_X, RGBLED_PRESET_ORANGE_Y},
    {RGBLED_PRESET_RED_X, RGBLED_PRESET_RED_Y},
    {RGBLED_PRESET_PURPLE_X, RGBLED_PRESET_PURPLE_Y},
    {RGBLED_PRESET_WHITE_X, RGBLED_PRESET_WHITE_Y},
};

RGBColor RGBLEDColorEngine_XYtoRGB(uint16_t zcl_x, uint16_t zcl_y) {
    float cx = (float)zcl_x / 65536.0f;
    float cy = (float)zcl_y / 65536.0f;
    if (cy < 1e-6f) return (RGBColor){0, 0, 0};
    float Y = 1.0f;
    float X = (Y / cy) * cx;
    float Z = (Y / cy) * (1.0f - cx - cy);
    float r_lin =  3.2406f * X - 1.5372f * Y - 0.4986f * Z;
    float g_lin = -0.9689f * X + 1.8758f * Y + 0.0415f * Z;
    float b_lin =  0.0557f * X - 0.2040f * Y + 1.0570f * Z;
    if (r_lin < 0.0f) r_lin = 0.0f;
    if (g_lin < 0.0f) g_lin = 0.0f;
    if (b_lin < 0.0f) b_lin = 0.0f;
    float max_ch = r_lin > g_lin ? r_lin : g_lin;
    if (b_lin > max_ch) max_ch = b_lin;
    if (max_ch > 1e-6f) { r_lin /= max_ch; g_lin /= max_ch; b_lin /= max_ch; }
#define _GAMMA(v) ((v) <= 0.0031308f ? (12.92f*(v)) : (1.055f*powf((v),1.0f/2.4f)-0.055f))
    RGBColor rgb;
    rgb.r = (uint8_t)(_GAMMA(r_lin) * 255.0f + 0.5f);
    rgb.g = (uint8_t)(_GAMMA(g_lin) * 255.0f + 0.5f);
    rgb.b = (uint8_t)(_GAMMA(b_lin) * 255.0f + 0.5f);
#undef _GAMMA
    return rgb;
}

void RGBLEDColorEngine_PresetToXY(PresetColorIndex index,
                                   uint16_t *out_x, uint16_t *out_y) {
    if (!out_x || !out_y) return;
    uint8_t i = (uint8_t)index < (uint8_t)RGB_COLOR_COUNT ? (uint8_t)index : (uint8_t)RGB_COLOR_WHITE;
    *out_x = s_preset_xy[i][0];
    *out_y = s_preset_xy[i][1];
}

PresetColorIndex RGBLEDColorEngine_XYtoNearestPreset(uint16_t zcl_x, uint16_t zcl_y) {
    float cx = (float)zcl_x / 65536.0f;
    float cy = (float)zcl_y / 65536.0f;
    PresetColorIndex best = RGB_COLOR_WHITE;
    float best_dist = 1e9f;
    for (uint8_t i = 0; i < (uint8_t)RGB_COLOR_COUNT; i++) {
        float dx = cx - (float)s_preset_xy[i][0] / 65536.0f;
        float dy = cy - (float)s_preset_xy[i][1] / 65536.0f;
        float dist = dx * dx + dy * dy;
        if (dist < best_dist) { best_dist = dist; best = (PresetColorIndex)i; }
    }
    return best;
}

static RGBColor interpolate(RGBColor from, RGBColor to, float value, float start, float end) {
    float ratio = (value - start) / (end - start);
    if (ratio < 0.0f) ratio = 0.0f;
    if (ratio > 1.0f) ratio = 1.0f;
    RGBColor result = {
        (uint8_t)(from.r + (to.r - from.r) * ratio),
        (uint8_t)(from.g + (to.g - from.g) * ratio),
        (uint8_t)(from.b + (to.b - from.b) * ratio)
    };
    return result;
}

RGBColor RGBLEDColorEngine_TemperatureToColor(float temp_c, 
                                             float cold_min,
                                             float cold_max,
                                             float warm_min,
                                             float warm_max,
                                             float hot_min,
                                             float hot_max,
                                             float danger_threshold,
                                             float freeze_threshold,
                                             RGBColor color_cold,
                                             RGBColor color_warm,
                                             RGBColor color_hot,
                                             RGBColor color_danger,
                                             RGBColor color_freeze) {
    if (temp_c <= freeze_threshold) return color_freeze;
    if (temp_c < cold_min) return interpolate(color_freeze, color_cold, temp_c, freeze_threshold, cold_min);
    if (temp_c <= cold_max) return color_cold;
    if (temp_c < warm_min) return interpolate(color_cold, color_warm, temp_c, cold_max, warm_min);
    if (temp_c <= warm_max) return color_warm;
    if (temp_c < hot_min) return interpolate(color_warm, color_hot, temp_c, warm_max, hot_min);
    if (temp_c <= hot_max) return color_hot;
    if (temp_c < danger_threshold) return interpolate(color_hot, color_danger, temp_c, hot_max, danger_threshold);
    return color_danger;
}
