/**
 * @file MenuManagerConsoleCommands.h
 * @brief Header for MenuManager CLI console command registration.
 *
 * @author RaCool DeepMind Team
 * @date 2026-08-01
 */

#ifndef MENU_MANAGER_CONSOLE_COMMANDS_H
#define MENU_MANAGER_CONSOLE_COMMANDS_H

#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Registers all MenuManager console commands with the DebugConsole system.
 */
/* Return the first registration error; partial entries remain console-owned. */
esp_err_t MenuManager_RegisterConsoleCommands(void);

#ifdef __cplusplus
}
#endif

#endif // MENU_MANAGER_CONSOLE_COMMANDS_H
