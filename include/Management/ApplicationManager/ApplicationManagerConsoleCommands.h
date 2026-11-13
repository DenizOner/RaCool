/**
 * @file ApplicationManagerConsoleCommands.h
 * @brief Header for ApplicationManager CLI console command registration.
 *
 * @author RaCool DeepMind Team
 * @date 2026-08-01
 */

#ifndef APPLICATION_MANAGER_CONSOLE_COMMANDS_H
#define APPLICATION_MANAGER_CONSOLE_COMMANDS_H

#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Registers all ApplicationManager console commands with the DebugConsole system.
 */
/* Return the first registration error; partial entries remain console-owned. */
esp_err_t ApplicationManager_RegisterConsoleCommands(void);

#ifdef __cplusplus
}
#endif

#endif // APPLICATION_MANAGER_CONSOLE_COMMANDS_H
