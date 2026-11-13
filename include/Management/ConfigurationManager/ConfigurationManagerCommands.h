/**
 * @file ConfigurationManagerCommands.h
 * @brief Debug Console command registration for ConfigurationManager.
 *
 * Registers the following commands with the system DebugConsole:
 *
 *   | Command             | Description                                      |
 *   |---------------------|--------------------------------------------------|
 *   | config.show         | Print all current settings to the console        |
 *   | config.save [block] | Save all or a named block to NVS                 |
 *   | config.load [block] | Reload all or a named block from NVS             |
 *   | config.reset [block]| Reset all or a named block to factory defaults   |
 *   | config.validate     | Validate current in-memory settings              |
 *   | config.version      | Show NVS schema version of each config block     |
 *
 * Usage example:
 * @code
 *   IDebugConsole *console = DebugConsole_GetModule();
 *   ConfigurationManagerCommands_Register(console);
 * @endcode
 *
 * @author RaCool Team
 * @date   2026
 */

#ifndef CONFIGURATION_MANAGER_COMMANDS_H
#define CONFIGURATION_MANAGER_COMMANDS_H

#include "Communication/DebugConsole/IDebugConsole.h"

/**
 * @brief Registers all ConfigurationManager commands with the DebugConsole.
 *
 * Existing command names are updated by the console registry. A partial failure
 * preserves successful entries and can be retried; this is not an atomic batch.
 *
 * @param console Pointer to the active IDebugConsole instance.
 * @return ESP_ERR_INVALID_ARG for a missing console/registration method,
 *         the first backend error, or ESP_OK after every command is registered.
 */
esp_err_t ConfigurationManagerCommands_Register(IDebugConsole *console);

#endif /* CONFIGURATION_MANAGER_COMMANDS_H */
