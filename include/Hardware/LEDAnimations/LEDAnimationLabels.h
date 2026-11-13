#pragma once
#include "Core/Localization/Localization.h"
#include "Hardware/LEDAnimations/LEDAnimationCatalog.h"

/* Shared presentation labels; persisted animation identifiers remain unchanged. */
static inline const char *LEDAnimationLabel_Localized(uint8_t id, bool builtin,
                                   const LocalizationCatalog *catalog) {
    static const char *const keys[] = {
        "animation.off",
        "animation.red_heartbeat",
        "animation.white_blink",
        "animation.red_sos",
        "animation.green_breathing",
        "animation.blue_fade",
        "animation.white_strobe",
        "animation.rainbow",
        "animation.police",
        "animation.solid_red",
        "animation.solid_green",
        "animation.solid_blue",
        "animation.solid_white",
        "animation.red_blink",
        "animation.red_breathing",
        "animation.white_double_blink",
        "animation.breathing",
        "animation.pulse",
        "animation.candle",
        "animation.alert",
        "animation.fade_in_out",
        "animation.color_cycle",
    };
    size_t index=LEDAnimationCatalog_Index(id,builtin);
    if (index >= sizeof(keys)/sizeof(keys[0])) return Localization_Text(catalog,LOC_COMMON_UNKNOWN);
    const char *text=Localization_TextKey(catalog,keys[index]);
    return text ? text : Localization_Text(catalog,LOC_COMMON_UNKNOWN);
}
