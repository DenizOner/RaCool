#ifndef UI_CONSOLE_COMMANDS_H
#define UI_CONSOLE_COMMANDS_H

#include "Communication/DebugConsole/IDebugConsole.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Registers all UI debug console commands with the given IDebugConsole.
 *
 * Registers the top-level 'UI' command and its short alias 'UC'.
 *
 * Args:
 *     console (IDebugConsole*): Pointer to the debug console module.
 */
/* Return the first registration error; successful partial entries remain owned
 * by the console registry and may be registered again on retry. */
esp_err_t UIConsoleCommands_Register(IDebugConsole *console);

#ifdef __cplusplus
}
#endif

#endif // UI_CONSOLE_COMMANDS_H
