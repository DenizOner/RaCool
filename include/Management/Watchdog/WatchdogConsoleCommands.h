/**
 * @file WatchdogConsoleCommands.h
 * @brief Header for Watchdog CLI console command registration.
 *
 * @author RaCool DeepMind Team
 * @date 2026-08-01
 */

#ifndef WATCHDOG_CONSOLE_COMMANDS_H
#define WATCHDOG_CONSOLE_COMMANDS_H

#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Registers all Watchdog console commands with the DebugConsole system.
 */
/* Return the first registration error; partial entries remain console-owned. */
esp_err_t Watchdog_RegisterConsoleCommands(void);

#ifdef __cplusplus
}
#endif

#endif // WATCHDOG_CONSOLE_COMMANDS_H
