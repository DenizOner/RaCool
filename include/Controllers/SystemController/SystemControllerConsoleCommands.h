/**
 * @file SystemControllerConsoleCommands.h
 * @brief Header for SystemController CLI console command registration.
 *
 * @author RaCool DeepMind Team
 * @date 2026-08-01
 */

#ifndef SYSTEM_CONTROLLER_CONSOLE_COMMANDS_H
#define SYSTEM_CONTROLLER_CONSOLE_COMMANDS_H

#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Registers all SystemController console commands with the DebugConsole system.
 */
/* Return the first registration error; partial entries remain console-owned. */
esp_err_t SystemController_RegisterConsoleCommands(void);

#ifdef __cplusplus
}
#endif

#endif // SYSTEM_CONTROLLER_CONSOLE_COMMANDS_H
