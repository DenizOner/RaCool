#include "Hardware/RGBLED/LEDBrightnessScale.h"
#include "Controllers/UIController/Widgets/WidgetLED.h"
#include "Hardware/BuiltinLED/IBuiltinLED.h"
#include "Hardware/BuiltinLED/BuiltinLEDConfig.h"
#include "Hardware/RGBLED/IRGBLED.h"
#include "Hardware/LEDAnimations/LEDAnimationCatalog.h"
#include "Controllers/UIController/UITranslation.h"
#include <stdio.h>
#include <stdbool.h>

/* Resolve the complete built-in animation registry to a display string. */
static const char *BuiltinAnimToStr(uint8_t anim, const LocalizationCatalog *catalog) {
    return UITranslation_Animation(anim,true,catalog);
}

/* Resolve ext_led_anim index to display string. */
static const char *ExtAnimToStr(uint8_t anim, const LocalizationCatalog *catalog) {
    return UITranslation_Animation(anim,false,catalog);
}

static const char *ExternalModeLabel(uint8_t mode, const LocalizationCatalog *catalog) {
    static const LocalizationMessage labels[] = {LOC_MENU_TEMPERATURE_DRIVEN, LOC_COMMON_ANIMATION, LOC_COMMON_OFF};
    return Localization_Text(catalog,mode < EXT_LED_MODE_COUNT ? labels[mode] : LOC_COMMON_UNKNOWN);
}

/* Resolve built-in LED animation for a given state. */
static uint8_t GetBuiltinAnimForState(UIState state, SystemSettings *settings) {
    if (state == UI_STATE_SET_BUILTIN_STARTUP)    return settings->hardware.builtin_led_startup_anim;
    if (state == UI_STATE_SET_BUILTIN_IDLE)       return settings->hardware.builtin_led_idle_anim;
    if (state == UI_STATE_SET_BUILTIN_CONNECTING) return settings->hardware.builtin_led_connecting_anim;
    if (state == UI_STATE_SET_BUILTIN_FAULT)      return settings->hardware.builtin_led_fault_anim;
    return 255; /* Unsupported states have no configured animation. */
}

bool WidgetLED_FormatLabel(UIState state, const char *label,
                           SystemSettings *settings,
                           char *outBuf, size_t maxLen, const LocalizationCatalog *catalog) {
    switch (state) {
    case UI_STATE_SET_BUILTIN_MODE:
        snprintf(outBuf,maxLen,"%s: %s",label,Localization_Text(catalog,
            settings->hardware.builtin_led_anim_mode ? LOC_COMMON_ON : LOC_COMMON_OFF));
        return true;
    case UI_STATE_SET_BUILTIN_BRIGHTNESS:
        snprintf(outBuf, maxLen, "%s: %u%%", label, LEDBrightness_Percent(settings->hardware.builtin_led_brightness, 255));
        return true;
    case UI_STATE_SET_LED_MODE: {
        const char *modeStr = ExternalModeLabel(settings->hardware.ext_led_mode,catalog);
        snprintf(outBuf, maxLen, "%s: %s", label, modeStr);
        return true;
    }
    case UI_STATE_SET_BUILTIN_STARTUP:
    case UI_STATE_SET_BUILTIN_IDLE:
    case UI_STATE_SET_BUILTIN_CONNECTING:
    case UI_STATE_SET_BUILTIN_FAULT:
        snprintf(outBuf, maxLen, "%s: %s", label,
             BuiltinAnimToStr(GetBuiltinAnimForState(state, settings),catalog));
        return true;
    case UI_STATE_SET_LED_ANIM:
        snprintf(outBuf, maxLen, "%s: %s", label,
                 ExtAnimToStr(settings->hardware.ext_led_startup_anim,catalog));
        return true;
    default:
        return false;
    }
}

bool WidgetLED_GetValue(UIState state, SystemSettings *settings,
                        char *valBuf, size_t valLen,
                        char *unitBuf, size_t unitLen,
                        int *percent, const LocalizationCatalog *catalog) {
    switch (state) {
    case UI_STATE_SET_BUILTIN_MODE:
        snprintf(valBuf,valLen,"%s",Localization_Text(catalog,
            settings->hardware.builtin_led_anim_mode ? LOC_COMMON_ON : LOC_COMMON_OFF));
        unitBuf[0]='\0';
        if(percent) *percent=settings->hardware.builtin_led_anim_mode ? 100 : 0;
        return true;
    case UI_STATE_SET_BUILTIN_BRIGHTNESS:
        snprintf(valBuf, valLen, "%u", LEDBrightness_Percent(settings->hardware.builtin_led_brightness, 255));
        snprintf(unitBuf, unitLen, "%%");
        if (percent) *percent = LEDBrightness_Percent(settings->hardware.builtin_led_brightness, 255);
        return true;
    case UI_STATE_SET_LED_MODE: {
        const char *modeStr = ExternalModeLabel(settings->hardware.ext_led_mode,catalog);
        snprintf(valBuf, valLen, "%s", modeStr);
        unitBuf[0]='\0';
        if (percent) *percent = settings->hardware.ext_led_mode < EXT_LED_MODE_COUNT
            ? settings->hardware.ext_led_mode * 100 / (EXT_LED_MODE_COUNT - 1) : 0;
        return true;
    }
    case UI_STATE_SET_BUILTIN_STARTUP:
    case UI_STATE_SET_BUILTIN_IDLE:
    case UI_STATE_SET_BUILTIN_CONNECTING:
    case UI_STATE_SET_BUILTIN_FAULT: {
        uint8_t act = GetBuiltinAnimForState(state, settings);
        snprintf(valBuf, valLen, "%s", BuiltinAnimToStr(act,catalog));
        unitBuf[0]='\0';
        size_t count;
        LEDAnimationCatalog_Entries(&count);
        size_t index = LEDAnimationCatalog_Index(act, true);
        if (percent) *percent = index < count ? (int)(index * 100 / (count - 1)) : 0;
        return true;
    }
    case UI_STATE_SET_LED_ANIM: {
        uint8_t anim = settings->hardware.ext_led_startup_anim;
        snprintf(valBuf, valLen, "%s", ExtAnimToStr(anim,catalog));
        unitBuf[0]='\0';
        size_t count;
        LEDAnimationCatalog_Entries(&count);
        size_t index = LEDAnimationCatalog_Index(anim, false);
        if (percent) *percent = index < count ? (int)(index * 100 / (count - 1)) : 0;
        return true;
    }
    default:
        return false;
    }
}
