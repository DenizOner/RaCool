#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "Hardware/RGBLED/RGBLEDTypes.h"

static inline bool RGBLED_ArgumentsValid(int argc, char **argv, int maximum) {
    if (argc < 1 || argc > maximum || !argv) return false;
    for (int index = 0; index < argc; ++index)
        if (!argv[index]) return false;
    return true;
}

/* A worker snapshot is not a measurement of physical LED output. */
static inline void RGBLED_PrintMachineState(const RGBLEDState *state) {
    printf("RACOOL_CLI v=1 type=rgb_status brightness=%u mode=%d target_r=%u target_g=%u target_b=%u priority=%d animation=%d on=%u fading=%u current_r=%u current_g=%u current_b=%u preview=%u preview_r=%u preview_g=%u preview_b=%u preview_remaining_ms=%u\n",
           (unsigned)state->brightness, (int)state->colorMode,
           (unsigned)state->targetColor.r, (unsigned)state->targetColor.g,
           (unsigned)state->targetColor.b, (int)state->animPriority,
           (int)state->activeAnim, (unsigned)state->isOn, (unsigned)state->isFading,
           (unsigned)state->currentColor.r, (unsigned)state->currentColor.g,
           (unsigned)state->currentColor.b, (unsigned)state->previewActive,
           (unsigned)state->previewColor.r, (unsigned)state->previewColor.g,
           (unsigned)state->previewColor.b, (unsigned)state->previewRemainingMs);
}

static inline bool RGBLED_ParseBoundedDecimal(const char *text, uint32_t maximum, uint32_t *result) {
    if (!text || !*text || !result) return false;
    uint32_t value = 0;
    for (const char *p = text; *p; ++p) {
        if (*p < '0' || *p > '9') return false;
        uint32_t digit = (uint32_t)(*p - '0');
        if (value > maximum / 10 || (value == maximum / 10 && digit > maximum % 10)) return false;
        value = value * 10 + digit;
    }
    *result = value;
    return true;
}

