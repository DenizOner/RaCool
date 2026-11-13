#include "Hardware/RGBLED/LEDBrightnessScale.h"
#include "Controllers/UIController/Widgets/WidgetRGB.h"
#include "Hardware/RGBLED/RGBLEDColorEngine.h"
#include "Controllers/UIController/UITranslation.h"
#include <stdio.h>
#include "Hardware/RGBLED/RGBZoneSelection.h"
#include <stdbool.h>

bool WidgetRGB_FormatLabel(UIState state, const char *label,
                           SystemSettings *settings,
                           char *outBuf, size_t maxLen, const LocalizationCatalog *catalog) {
    if (state>=UI_STATE_RGB_COLD_X && state<=UI_STATE_RGB_FREEZE_Y) {
        unsigned coordinate=(unsigned)(state-UI_STATE_RGB_COLD_X);
        uint16_t x,y;
        if (!SystemConfigs_GetRGBZoneXY(&settings->hardware,coordinate/2,&x,&y)) return false;
        snprintf(outBuf,maxLen,"%s: %u",label,(unsigned)((coordinate&1u) ? y : x));
        return true;
    }
    int zone = RGBZoneSelection_Zone(state);
    if (zone >= 0) {
        uint16_t x,y;
        if (!SystemConfigs_GetRGBZoneXY(&settings->hardware,(unsigned)zone,&x,&y)) return false;
        snprintf(outBuf,maxLen,"%s: %s",label,(RGBLEDColorEngine_XYtoExactPreset(x,y)<0 ? Localization_TextKey(catalog,"color.custom") : UITranslation_Color((PresetColorIndex)RGBLEDColorEngine_XYtoExactPreset(x,y),catalog)));
        return true;
    }
    switch (state) {
    case UI_STATE_SET_RGB_COLD:
        snprintf(outBuf, maxLen, "%s: %s", label,
                 UITranslation_Color(
                     RGBLEDColorEngine_XYtoNearestPreset(
                         settings->hardware.ext_led_color_cold_x,
                         settings->hardware.ext_led_color_cold_y),catalog));
        return true;
    case UI_STATE_SET_RGB_WARM:
        snprintf(outBuf, maxLen, "%s: %s", label,
                 UITranslation_Color(
                     RGBLEDColorEngine_XYtoNearestPreset(
                         settings->hardware.ext_led_color_warm_x,
                         settings->hardware.ext_led_color_warm_y),catalog));
        return true;
    case UI_STATE_SET_RGB_HOT:
        snprintf(outBuf, maxLen, "%s: %s", label,
                 UITranslation_Color(
                     RGBLEDColorEngine_XYtoNearestPreset(
                         settings->hardware.ext_led_color_hot_x,
                         settings->hardware.ext_led_color_hot_y),catalog));
        return true;
    case UI_STATE_SET_RGB_BRIGHTNESS:
        snprintf(outBuf, maxLen, "%s: %u%%", label, LEDBrightness_Percent(settings->hardware.ext_led_brightness, 254));
        return true;
    case UI_STATE_SET_RGB_TEMP_COLD:
        snprintf(outBuf, maxLen, "%s: %.1f-%.1f C", label, settings->hardware.ext_led_temp_cold_min, settings->hardware.ext_led_temp_cold_max);
        return true;
    case UI_STATE_SET_RGB_TEMP_COLD_MAX:
        snprintf(outBuf, maxLen, "%s: %.0f C", label, settings->hardware.ext_led_temp_cold_max);
        return true;
    case UI_STATE_SET_RGB_TEMP_WARM:
        snprintf(outBuf, maxLen, "%s: %.1f-%.1f C", label, settings->hardware.ext_led_temp_warm_min, settings->hardware.ext_led_temp_warm_max);
        return true;
    case UI_STATE_SET_RGB_TEMP_WARM_MAX:
        snprintf(outBuf, maxLen, "%s: %.0f C", label, settings->hardware.ext_led_temp_warm_max);
        return true;
    case UI_STATE_SET_RGB_TEMP_HOT:
        snprintf(outBuf, maxLen, "%s: %.1f-%.1f C", label, settings->hardware.ext_led_temp_hot_min, settings->hardware.ext_led_temp_hot_max);
        return true;
    case UI_STATE_SET_RGB_TEMP_HOT_MAX:
        snprintf(outBuf, maxLen, "%s: %.0f C", label, settings->hardware.ext_led_temp_hot_max);
        return true;
    case UI_STATE_SET_RGB_TEMP_DANGER:
        snprintf(outBuf, maxLen, "%s: %.0f C", label, settings->hardware.ext_led_temp_danger);
        return true;
    case UI_STATE_SET_RGB_TEMP_FREEZE:
        snprintf(outBuf, maxLen, "%s: %.0f C", label, settings->hardware.ext_led_temp_freeze);
        return true;
    default:
        return false;
    }
}

bool WidgetRGB_GetValue(UIState state, SystemSettings *settings,
                        char *valBuf, size_t valLen,
                        char *unitBuf, size_t unitLen,
                        int *percent, const LocalizationCatalog *catalog) {
    if (state>=UI_STATE_RGB_COLD_X && state<=UI_STATE_RGB_FREEZE_Y) {
        unsigned coordinate=(unsigned)(state-UI_STATE_RGB_COLD_X);
        uint16_t x,y;
        if (!SystemConfigs_GetRGBZoneXY(&settings->hardware,coordinate/2,&x,&y)) return false;
        unsigned value=(coordinate&1u) ? y : x;
        snprintf(valBuf,valLen,"%u",value);
        snprintf(unitBuf,unitLen,"XY");
        if (percent) *percent=(int)(value*100u/RGB_XY_MAX_COORDINATE);
        return true;
    }
    int zone = RGBZoneSelection_Zone(state);
    if (zone >= 0) {
        uint16_t x,y;
        if (!RGBZoneSelection_Read(state,&x,&y) &&
            !SystemConfigs_GetRGBZoneXY(&settings->hardware,(unsigned)zone,&x,&y)) return false;
        snprintf(valBuf,valLen,"%s",(RGBLEDColorEngine_XYtoExactPreset(x,y)<0 ? Localization_TextKey(catalog,"color.custom") : UITranslation_Color((PresetColorIndex)RGBLEDColorEngine_XYtoExactPreset(x,y),catalog)));
        unitBuf[0]='\0'; if (percent) *percent=0; return true;
    }
    switch (state) {
    case UI_STATE_SET_RGB_COLD: {
        int idx = (int)RGBLEDColorEngine_XYtoNearestPreset(
            settings->hardware.ext_led_color_cold_x,
            settings->hardware.ext_led_color_cold_y);
        snprintf(valBuf, valLen, "%s", UITranslation_Color(idx,catalog));
        unitBuf[0]='\0';
        if (percent) *percent = idx * 100 / (RGB_COLOR_COUNT - 1);
        return true;
    }
    case UI_STATE_SET_RGB_WARM: {
        int idx = (int)RGBLEDColorEngine_XYtoNearestPreset(
            settings->hardware.ext_led_color_warm_x,
            settings->hardware.ext_led_color_warm_y);
        snprintf(valBuf, valLen, "%s", UITranslation_Color(idx,catalog));
        unitBuf[0]='\0';
        if (percent) *percent = idx * 100 / (RGB_COLOR_COUNT - 1);
        return true;
    }
    case UI_STATE_SET_RGB_HOT: {
        int idx = (int)RGBLEDColorEngine_XYtoNearestPreset(
            settings->hardware.ext_led_color_hot_x,
            settings->hardware.ext_led_color_hot_y);
        snprintf(valBuf, valLen, "%s", UITranslation_Color(idx,catalog));
        unitBuf[0]='\0';
        if (percent) *percent = idx * 100 / (RGB_COLOR_COUNT - 1);
        return true;
    }
    case UI_STATE_SET_RGB_BRIGHTNESS:
        snprintf(valBuf, valLen, "%u", LEDBrightness_Percent(settings->hardware.ext_led_brightness, 254));
        snprintf(unitBuf, unitLen, "%%");
        if (percent) *percent = LEDBrightness_Percent(settings->hardware.ext_led_brightness, 254);
        return true;
    case UI_STATE_SET_RGB_TEMP_COLD:
        snprintf(valBuf, valLen, "%.0f-%.0f", settings->hardware.ext_led_temp_cold_min,
                 settings->hardware.ext_led_temp_cold_max);
        snprintf(unitBuf, unitLen, "C");
        if (percent) *percent = (int)((settings->hardware.ext_led_temp_cold_min - RGB_COLD_MIN_LIMIT) * 100.0f / (RGB_COLD_MAX_LIMIT - RGB_COLD_MIN_LIMIT));
        return true;
    case UI_STATE_SET_RGB_TEMP_COLD_MAX:
        snprintf(valBuf, valLen, "%.0f", settings->hardware.ext_led_temp_cold_max);
        snprintf(unitBuf, unitLen, "C");
        if (percent) *percent = (int)((settings->hardware.ext_led_temp_cold_max - RGB_COLD_MIN_LIMIT) * 100.0f / (RGB_COLD_MAX_LIMIT - RGB_COLD_MIN_LIMIT));
        return true;
    case UI_STATE_SET_RGB_TEMP_WARM:
        snprintf(valBuf, valLen, "%.0f-%.0f", settings->hardware.ext_led_temp_warm_min,
                 settings->hardware.ext_led_temp_warm_max);
        snprintf(unitBuf, unitLen, "C");
        if (percent) *percent = (int)((settings->hardware.ext_led_temp_warm_min - RGB_WARM_MIN_LIMIT) * 100.0f / (RGB_WARM_MAX_LIMIT - RGB_WARM_MIN_LIMIT));
        return true;
    case UI_STATE_SET_RGB_TEMP_WARM_MAX:
        snprintf(valBuf, valLen, "%.0f", settings->hardware.ext_led_temp_warm_max);
        snprintf(unitBuf, unitLen, "C");
        if (percent) *percent = (int)((settings->hardware.ext_led_temp_warm_max - RGB_WARM_MIN_LIMIT) * 100.0f / (RGB_WARM_MAX_LIMIT - RGB_WARM_MIN_LIMIT));
        return true;
    case UI_STATE_SET_RGB_TEMP_HOT:
        snprintf(valBuf, valLen, "%.0f-%.0f", settings->hardware.ext_led_temp_hot_min,
                 settings->hardware.ext_led_temp_hot_max);
        snprintf(unitBuf, unitLen, "C");
        if (percent) *percent = (int)((settings->hardware.ext_led_temp_hot_min - RGB_HOT_MIN_LIMIT) * 100.0f / (RGB_HOT_MAX_LIMIT - RGB_HOT_MIN_LIMIT));
        return true;
    case UI_STATE_SET_RGB_TEMP_HOT_MAX:
        snprintf(valBuf, valLen, "%.0f", settings->hardware.ext_led_temp_hot_max);
        snprintf(unitBuf, unitLen, "C");
        if (percent) *percent = (int)((settings->hardware.ext_led_temp_hot_max - RGB_HOT_MIN_LIMIT) * 100.0f / (RGB_HOT_MAX_LIMIT - RGB_HOT_MIN_LIMIT));
        return true;
    case UI_STATE_SET_RGB_TEMP_DANGER:
        snprintf(valBuf, valLen, "%.0f", settings->hardware.ext_led_temp_danger);
        snprintf(unitBuf, unitLen, "C");
        if (percent) *percent = (int)((settings->hardware.ext_led_temp_danger - 60.0f) * 100.0f / 39.0f);
        return true;
    case UI_STATE_SET_RGB_TEMP_FREEZE:
        snprintf(valBuf, valLen, "%.0f", settings->hardware.ext_led_temp_freeze);
        snprintf(unitBuf, unitLen, "C");
        if (percent) *percent = (int)((settings->hardware.ext_led_temp_freeze + 18.0f) * 100.0f / 24.0f);
        return true;
    default:
        return false;
    }
}
