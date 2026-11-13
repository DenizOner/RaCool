#ifndef POWER_MANAGER_CONSOLE_COMMANDS_H
#define POWER_MANAGER_CONSOLE_COMMANDS_H

#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file PowerManagerConsoleCommands.h
 * @brief Registers power management commands to DebugConsole.
 */

/**
 * @brief Registers all power management console commands with the Debug Console.
 */
/* Return the first registration error; partial entries remain console-owned. */
esp_err_t PowerManager_RegisterConsoleCommands(void);

#ifdef __cplusplus
}
#endif

#endif // POWER_MANAGER_CONSOLE_COMMANDS_H
