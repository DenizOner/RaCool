#ifndef SYSTEM_INITIALIZER_CONSOLE_COMMANDS_H
#define SYSTEM_INITIALIZER_CONSOLE_COMMANDS_H

#include <esp_err.h>

/**
 * @brief Register all SystemInitializer CLI commands into the Debug Console.
 *
 * Commands registered (CMD_CAT_SYSTEM):
 *   sys_init_status - Show SystemInitializer service state
 *
 * Safe to call multiple times — registration is idempotent.
 */
/* Return the first registration error; partial entries remain console-owned. */
esp_err_t SystemInitializer_RegisterConsoleCommands(void);

#endif // SYSTEM_INITIALIZER_CONSOLE_COMMANDS_H
