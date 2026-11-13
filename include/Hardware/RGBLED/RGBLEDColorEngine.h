#ifndef RGBLED_COLOR_ENGINE_H
#define RGBLED_COLOR_ENGINE_H

#include "Hardware/RGBLED/RGBLEDTypes.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Convert HSV to RGB. */
RGBColor RGBLEDColorEngine_HSVtoRGB(HSVColor hsv);

/** @brief Convert RGB to HSV. */
HSVColor RGBLEDColorEngine_RGBtoHSV(RGBColor rgb);

typedef enum {
    RGB_COLOR_BLUE = 0,
    RGB_COLOR_CYAN,
    RGB_COLOR_GREEN,
    RGB_COLOR_YELLOW,
    RGB_COLOR_ORANGE,
    RGB_COLOR_RED,
    RGB_COLOR_PURPLE,
    RGB_COLOR_WHITE,
    RGB_COLOR_COUNT
} PresetColorIndex;

RGBColor        RGBLEDColorEngine_GetPresetColor(PresetColorIndex index);
const char*     RGBLEDColorEngine_GetPresetColorName(PresetColorIndex index);

/**
 * @brief Convert CIE 1931 XY chromaticity (ZCL format, 0-65279) to RGB.
 *
 * Uses the standard XYZ D65 white-point to sRGB matrix.
 * Brightness (Y luminance) is assumed to be 1.0 (full brightness).
 *
 * @param zcl_x  ZCL currentX value (0-65279).
 * @param zcl_y  ZCL currentY value (0-65279).
 * @return       Corresponding RGBColor (gamma-corrected sRGB, 0-255 each).
 */
RGBColor RGBLEDColorEngine_XYtoRGB(uint16_t zcl_x, uint16_t zcl_y);

/**
 * @brief Find the nearest preset color index for a given CIE XY color.
 *
 * Compares the XY input against every preset's known XY and returns the
 * index with the smallest Euclidean distance in CIE xy space.
 *
 * @param zcl_x  ZCL currentX value.
 * @param zcl_y  ZCL currentY value.
 * @return       Closest PresetColorIndex.
 */
PresetColorIndex RGBLEDColorEngine_XYtoNearestPreset(uint16_t zcl_x, uint16_t zcl_y);

/**
 * @brief Get the canonical CIE XY values (ZCL format) for a preset color.
 *
 * @param index   Preset color index.
 * @param out_x   Output ZCL X value.
 * @param out_y   Output ZCL Y value.
 */
void RGBLEDColorEngine_PresetToXY(PresetColorIndex index,
                                   uint16_t *out_x, uint16_t *out_y);

static inline int RGBLEDColorEngine_XYtoExactPreset(uint16_t x, uint16_t y) {
    for (int i=0;i<RGB_COLOR_COUNT;++i) {
        uint16_t px,py;
        RGBLEDColorEngine_PresetToXY((PresetColorIndex)i,&px,&py);
        if (x==px && y==py) return i;
    }
    return -1;
}

/**
 * @brief Map temperature to interpolated RGB using CIE XY zone colors.
 */
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
                                              RGBColor color_freeze);

#ifdef __cplusplus
}
#endif

#endif // RGBLED_COLOR_ENGINE_H
