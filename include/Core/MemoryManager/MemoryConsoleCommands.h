#ifndef MEMORY_CONSOLE_COMMANDS_H
#define MEMORY_CONSOLE_COMMANDS_H

#include "Communication/DebugConsole/IDebugConsole.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Register memory diagnostic commands to the console.
 * 
 * @param console Pointer to console service module.
 * @return The backend error, ESP_ERR_INVALID_ARG for a missing boundary,
 *         or ESP_OK after registration succeeds.
 */
esp_err_t MemoryConsoleCommands_Register(IDebugConsole *console);

#ifdef __cplusplus
}
#endif

#endif // MEMORY_CONSOLE_COMMANDS_H
