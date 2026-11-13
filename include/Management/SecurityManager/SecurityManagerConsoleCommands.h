/**
 * @file SecurityManagerConsoleCommands.h
 * @brief Header for SecurityManager CLI console command registration.
 *
 * @author RaCool DeepMind Team
 * @date 2026-08-01
 */

#ifndef SECURITY_MANAGER_CONSOLE_COMMANDS_H
#define SECURITY_MANAGER_CONSOLE_COMMANDS_H

#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Registers all SecurityManager console commands with the DebugConsole system.
 */
/* Return the first registration error; partial entries remain console-owned. */
esp_err_t SecurityManager_RegisterConsoleCommands(void);

#ifdef __cplusplus
}
#endif

#endif // SECURITY_MANAGER_CONSOLE_COMMANDS_H
