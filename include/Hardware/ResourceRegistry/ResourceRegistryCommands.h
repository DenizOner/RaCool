#ifndef RESOURCE_REGISTRY_COMMANDS_H
#define RESOURCE_REGISTRY_COMMANDS_H

#include "Communication/DebugConsole/IDebugConsole.h"

/**
 * @file ResourceRegistryCommands.h
 * @brief Console commands registration header for ResourceRegistry.
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Registers ResourceRegistry console commands (`RESREG`) into DebugConsole.
 *
 * @param console Pointer to IDebugConsole instance.
 */
/* Missing registration boundaries and backend failures are reported to callers. */
esp_err_t ResourceRegistryCommands_Register(IDebugConsole *console);

#ifdef __cplusplus
}
#endif

#endif // RESOURCE_REGISTRY_COMMANDS_H
