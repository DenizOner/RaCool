/**
 * @file DiagnosticsManagerConsoleCommands.h
 * @brief Header for DiagnosticsManager CLI console command registration.
 *
 * @author RaCool DeepMind Team
 * @date 2026-08-01
 */

#ifndef DIAGNOSTICS_MANAGER_CONSOLE_COMMANDS_H
#define DIAGNOSTICS_MANAGER_CONSOLE_COMMANDS_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Registers all DiagnosticsManager console commands with the DebugConsole system.
 */
esp_err_t DiagnosticsManager_RegisterConsoleCommands(void);

#ifdef __cplusplus
}
#endif

#endif // DIAGNOSTICS_MANAGER_CONSOLE_COMMANDS_H
