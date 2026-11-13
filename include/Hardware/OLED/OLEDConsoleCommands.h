/**
 * @file OLEDConsoleCommands.h
 * @brief Debug Console CLI commands for OLED Module.
 */
#ifndef OLED_CONSOLE_COMMANDS_H
#define OLED_CONSOLE_COMMANDS_H

#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Return the first registration error; partial entries remain console-owned. */
esp_err_t OLED_RegisterConsoleCommands(void);

#ifdef __cplusplus
}
#endif

#endif // OLED_CONSOLE_COMMANDS_H
