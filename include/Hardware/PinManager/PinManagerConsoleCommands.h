/**
 * @file PinManagerConsoleCommands.h
 * @brief Debug Console CLI commands for PinManager Module.
 */
#ifndef PIN_MANAGER_CONSOLE_COMMANDS_H
#define PIN_MANAGER_CONSOLE_COMMANDS_H

#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Return the first registration error; partial entries remain console-owned. */
esp_err_t PinManager_RegisterConsoleCommands(void);

#ifdef __cplusplus
}
#endif

#endif // PIN_MANAGER_CONSOLE_COMMANDS_H
