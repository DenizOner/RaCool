#ifndef ERROR_HANDLER_INPUT_H
#define ERROR_HANDLER_INPUT_H
#include <errno.h>
#include "Core/ErrorHandler/IErrorHandler.h"
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
static inline bool ErrorHandler_IsTagValid(const char *tag) {
    if (!tag || !*tag) return false;
    unsigned length=0;
    while (length<ERROR_HANDLER_TAG_CAPACITY && tag[length]) ++length;
    return length<ERROR_HANDLER_TAG_CAPACITY;
}
/* Shared by both historical CLI registrars; reject truncation and atoi fallbacks. */
static inline bool ErrorHandler_ParseInput(int argc, char **argv, int32_t *code) {
    if (argc != 3 || !argv || !argv[1] || !argv[2] || !code ||
        !ErrorHandler_IsTagValid(argv[1])) return false;
    const char *number = argv[2];
    if (*number == '-' || *number == '+') ++number;
    if (!*number) return false;
    for (const char *p=number; *p; ++p) if (*p < '0' || *p > '9') return false;
    errno=0; char *end;
    long value=strtol(argv[2],&end,10);
    if (errno || *end || value < INT32_MIN || value > INT32_MAX) return false;
    *code=(int32_t)value;
    return true;
}
static inline bool ErrorHandler_NoArguments(int argc,char **argv) {
    return argc==1 && argv && argv[0];
}
#endif
