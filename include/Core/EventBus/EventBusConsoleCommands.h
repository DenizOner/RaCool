#ifndef EVENTBUS_CONSOLE_COMMANDS_H
#define EVENTBUS_CONSOLE_COMMANDS_H

#include <esp_err.h>

/**
 * @brief Register all EventBus CLI commands into the Debug Console.
 *
 * Commands registered (CMD_CAT_SYSTEM):
 *   bus_status   - Show EventBus state, subscriber count and queue depth
 *   bus_publish  - Publish a test info event: bus_publish <event_name>
 *
 * Safe to call multiple times — registration is idempotent.
 */
/* Return the first registration error; partial entries remain console-owned. */
esp_err_t EventBus_RegisterConsoleCommands(void);

#endif // EVENTBUS_CONSOLE_COMMANDS_H
