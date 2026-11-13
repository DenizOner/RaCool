/**
 * @file BuiltinLEDConsoleCommands.h
 * @brief Debug Console CLI commands for BuiltinLED Module.
 */
#ifndef BUILTIN_LED_CONSOLE_COMMANDS_H
#define BUILTIN_LED_CONSOLE_COMMANDS_H

#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Return the first registration error; partial entries remain console-owned. */
esp_err_t BuiltinLED_RegisterConsoleCommands(void);

#ifdef __cplusplus
}
#endif

#endif // BUILTIN_LED_CONSOLE_COMMANDS_H
