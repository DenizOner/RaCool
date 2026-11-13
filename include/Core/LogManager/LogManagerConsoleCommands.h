#ifndef LOG_MANAGER_CONSOLE_COMMANDS_H
#define LOG_MANAGER_CONSOLE_COMMANDS_H

#include <esp_err.h>

/**
 * @brief Register all LogManager CLI commands into the Debug Console.
 *
 * Commands registered (CMD_CAT_SYSTEM):
 *   log_status      - Show LogManager state
 *   log_level_set   - Set global log level: log_level_set <0-5>
 *   log_module_set  - Set per-tag log level: log_module_set <tag> <0-5>
 *   log_recent      - Print the last N captured log entries: log_recent [n]
 *
 * Safe to call multiple times — registration is idempotent.
 */
/* Return the first registration error; partial entries remain console-owned. */
esp_err_t LogManager_RegisterConsoleCommands(void);

#endif // LOG_MANAGER_CONSOLE_COMMANDS_H
