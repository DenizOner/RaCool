#ifndef LED_ANIMATION_CATALOG_H
#define LED_ANIMATION_CATALOG_H

#include "Hardware/BuiltinLED/BuiltinLEDConfig.h"
#include "Hardware/RGBLED/RGBLEDTypes.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint8_t builtin_id;
    uint8_t external_id;
    const char *label;
} LEDAnimationCatalogEntry;

/* Presentation order is independent of persisted protocol identifiers. */
static inline const LEDAnimationCatalogEntry *LEDAnimationCatalog_Entries(size_t *count) {
    static const LEDAnimationCatalogEntry entries[] = {
        {255, RGBLED_ANIM_NONE, "KAPALI"},
        {HEARTBEAT_RED, RGBLED_ANIM_HEARTBEAT_RED, "KALP KIRMIZI"},
        {BLINK_WHITE, RGBLED_ANIM_BLINK_WHITE, "BEYAZ FLASOR"},
        {SOS_RED, RGBLED_ANIM_SOS_RED, "SOS KIRMIZI"},
        {BREATHING_GREEN, RGBLED_ANIM_BREATHING_GREEN, "YESIL NEFES"},
        {FADE_BLUE, RGBLED_ANIM_FADE_BLUE, "MAVI GECIS"},
        {STROBE_WHITE, RGBLED_ANIM_STROBE_WHITE, "BEYAZ FLAS"},
        {RAINBOW_FLOW, RGBLED_ANIM_RAINBOW, "GOKKUSAGI"},
        {POLICE_STROBE, RGBLED_ANIM_POLICE_STROBE, "POLIS"},
        {SOLID_RED, RGBLED_ANIM_SOLID_RED, "SABIT KIRMIZI"},
        {SOLID_GREEN, RGBLED_ANIM_SOLID_GREEN, "SABIT YESIL"},
        {SOLID_BLUE, RGBLED_ANIM_SOLID_BLUE, "SABIT MAVI"},
        {SOLID_WHITE, RGBLED_ANIM_SOLID_WHITE, "SABIT BEYAZ"},
        {BLINK_RED, RGBLED_ANIM_BLINK_RED, "KIRMIZI FLASOR"},
        {BREATHING_RED, RGBLED_ANIM_BREATHING_RED, "KIRMIZI NEFES"},
        {DOUBLE_BLINK_WHITE, RGBLED_ANIM_DOUBLE_BLINK_WHITE, "CIFT BEYAZ"},
        {GENERIC_BREATHING, RGBLED_ANIM_BREATHING, "NEFES ALMA"},
        {GENERIC_PULSE, RGBLED_ANIM_PULSE, "HIZLI NABIZ"},
        {GENERIC_CANDLE, RGBLED_ANIM_CANDLE, "MUM ISIGI"},
        {GENERIC_ALERT, RGBLED_ANIM_ALERT, "UYARI FLASORU"},
        {GENERIC_FADE, RGBLED_ANIM_FADE_IN_OUT, "ACILIP KAPANMA"},
        {GENERIC_COLOR_CYCLE, RGBLED_ANIM_COLOR_CYCLE, "RENK DONGUSU"},
    };
    if (count) *count = sizeof(entries) / sizeof(entries[0]);
    return entries;
}

static inline size_t LEDAnimationCatalog_Index(uint8_t id, bool builtin) {
    size_t count;
    const LEDAnimationCatalogEntry *entries = LEDAnimationCatalog_Entries(&count);
    for (size_t index = 0; index < count; ++index)
        if ((builtin ? entries[index].builtin_id : entries[index].external_id) == id) return index;
    return count;
}

static inline const char *LEDAnimationCatalog_Label(uint8_t id, bool builtin) {
    size_t count;
    const LEDAnimationCatalogEntry *entries = LEDAnimationCatalog_Entries(&count);
    size_t index = LEDAnimationCatalog_Index(id, builtin);
    return index < count ? entries[index].label : "BILINMIYOR";
}

static inline bool LEDAnimationCatalog_IsValid(uint8_t id, bool builtin) {
    size_t count;
    LEDAnimationCatalog_Entries(&count);
    return LEDAnimationCatalog_Index(id, builtin) < count;
}

static inline uint8_t LEDAnimationCatalog_Next(uint8_t id, bool builtin, int direction) {
    size_t count;
    const LEDAnimationCatalogEntry *entries = LEDAnimationCatalog_Entries(&count);
    size_t index = LEDAnimationCatalog_Index(id, builtin);
    if (index >= count) index = 0;
    if (direction > 0) index = (index + 1) % count;
    else if (direction < 0) index = (index + count - 1) % count;
    return builtin ? entries[index].builtin_id : entries[index].external_id;
}

#endif // LED_ANIMATION_CATALOG_H
