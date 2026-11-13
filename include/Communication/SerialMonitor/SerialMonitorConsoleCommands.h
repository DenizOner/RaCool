/**
 * @file SerialMonitorConsoleCommands.h
 * @brief Header for SerialMonitor CLI console command registration.
 *
 * @author RaCool DeepMind Team
 * @date 2026-08-01
 */

#ifndef SERIAL_MONITOR_CONSOLE_COMMANDS_H
#define SERIAL_MONITOR_CONSOLE_COMMANDS_H
#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Registers all SerialMonitor console commands with the DebugConsole system.
 * @return The registration result. A failed managed registration is never
 * retried through the raw SDK, which would bypass the console registry.
 */
esp_err_t SerialMonitor_RegisterConsoleCommands(void);
int SerialMonitor_HandlePerfCommand(int argc, char **argv);

#ifdef __cplusplus
}
#endif

#endif // SERIAL_MONITOR_CONSOLE_COMMANDS_H
