#ifndef SERVICE_LOCATOR_CONSOLE_COMMANDS_H
#define SERVICE_LOCATOR_CONSOLE_COMMANDS_H

#include <esp_err.h>

/**
 * @brief Register all ServiceLocator CLI commands into the Debug Console.
 *
 * Commands registered (CMD_CAT_SYSTEM):
 *   svc_list   - List all registered services with their names and states
 *   svc_get    - Look up a service by name: svc_get <service_name>
 *
 * Safe to call multiple times — registration is idempotent.
 */
/* Return the first registration error; partial entries remain console-owned. */
esp_err_t ServiceLocator_RegisterConsoleCommands(void);

#endif // SERVICE_LOCATOR_CONSOLE_COMMANDS_H
