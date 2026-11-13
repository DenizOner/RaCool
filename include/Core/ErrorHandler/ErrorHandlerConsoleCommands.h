#ifndef ERROR_HANDLER_CONSOLE_COMMANDS_H
#define ERROR_HANDLER_CONSOLE_COMMANDS_H

#include <esp_err.h>

/**
 * @brief Register all ErrorHandler CLI commands into the Debug Console.
 *
 * Commands registered (CMD_CAT_SYSTEM):
 *   error_status       - Show ErrorHandler state and statistics
 *   error_history      - Display circular error history log
 *   error_trigger      - Inject an async test error: error_trigger <tag> <code>
 *   error_trigger_isr  - Inject a test error from simulated ISR context
 *   error_clear        - Clear error history and reset counters
 *   error_selftest     - Run ErrorHandler internal self-test suite
 *
 * Safe to call multiple times — registration is idempotent.
 */
/* Return the first registration error; partial entries remain console-owned. */
esp_err_t ErrorHandler_RegisterConsoleCommands(void);

#endif // ERROR_HANDLER_CONSOLE_COMMANDS_H
