#ifndef INTERNAL_TEMPERATURE_SENSOR_INPUT_H
#define INTERNAL_TEMPERATURE_SENSOR_INPUT_H
#include <errno.h>
#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

static inline bool InternalTemp_ParseFloat(const char *text,float *out) {
    if (!text || !out || !*text || isspace((unsigned char)*text) || strnlen(text,65)>64) return false;
    errno=0;char *end;float value=strtof(text,&end);
    if (errno || end==text || *end || !isfinite(value)) return false;
    *out=value;return true;
}
static inline bool InternalTemp_ParseInteger(const char *text,long minimum,long maximum,long *out) {
    if (!text || !out || !*text || isspace((unsigned char)*text) || strnlen(text,65)>64) return false;
    errno=0;char *end;long value=strtol(text,&end,10);
    if (errno || end==text || *end || value<minimum || value>maximum) return false;
    *out=value;return true;
}
#endif
