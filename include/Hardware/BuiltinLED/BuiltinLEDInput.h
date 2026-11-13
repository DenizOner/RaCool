#ifndef BUILTIN_LED_INPUT_H
#define BUILTIN_LED_INPUT_H

#include <stdbool.h>
#include <stdint.h>

/* Shared strict parsing for the two built-in LED console spellings. */
static inline bool BuiltinLEDInput_Arguments(int argc, char **argv, int minimum, int maximum) {
    if (!argv || argc < minimum || argc > maximum) return false;
    for (int i = 0; i < argc; ++i) if (!argv[i] || !argv[i][0]) return false;
    return true;
}

static inline bool BuiltinLEDInput_ParseUnsigned(const char *text, uint32_t maximum, uint32_t *out) {
    if (!text || !*text || !out) return false;
    uint32_t value = 0;
    for (const char *cursor = text; *cursor; ++cursor) {
        if (*cursor < '0' || *cursor > '9') return false;
        uint32_t digit = (uint32_t)(*cursor - '0');
        if (digit > maximum || value > (maximum - digit) / 10) return false;
        value = value * 10 + digit;
    }
    *out = value;
    return true;
}

#endif
