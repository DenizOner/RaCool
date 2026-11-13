/**
 * @file RGBLEDConsoleCommands.h
 * @brief Debug Console CLI commands for RGBLED Module.
 */
#ifndef RGBLED_CONSOLE_COMMANDS_H
#define RGBLED_CONSOLE_COMMANDS_H

#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Return the first registration error; partial entries remain console-owned. */
esp_err_t RGBLED_RegisterConsoleCommands(void);

#ifdef __cplusplus
}
#endif

#endif // RGBLED_CONSOLE_COMMANDS_H
