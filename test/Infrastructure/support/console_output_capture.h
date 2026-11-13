#ifndef RACOOL_CONSOLE_OUTPUT_CAPTURE_H
#define RACOOL_CONSOLE_OUTPUT_CAPTURE_H
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
static char console_output[8192];
static inline void ConsoleCapture_Clear(void) { console_output[0] = '\0'; }
#if defined(__GNUC__)
__attribute__((format(printf, 1, 2)))
#endif
static int ConsoleCapture_Printf(const char *format, ...) {
    size_t used = strlen(console_output);
    va_list arguments; va_start(arguments, format);
    int result = vsnprintf(console_output + used, sizeof(console_output) - used, format, arguments);
    va_end(arguments);
    return result;
}
#endif
