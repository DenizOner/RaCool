#ifndef COMMAND_REGISTRY_H
#define COMMAND_REGISTRY_H

#include "Communication/DebugConsole/IDebugConsole.h"
#include <esp_console.h>
#include <stdbool.h>

/**
 * @file CommandRegistry.h
 * @brief Central command registry.
 *
 * Thread-safe registration, removal, prefix search and category filtering.
 */

/**
 * @brief Command registry entry
 */
typedef struct {
    char              command[32];
    char              help[64];
    CommandCategory   category;
    esp_console_cmd_func_t func;
    bool              active;
} CommandEntry;

bool CommandRegistry_Init(void);

/**
 * @brief Register a command or update an existing entry.
 */
bool CommandRegistry_Register(const esp_console_cmd_t *cmd, CommandCategory category);
/* The backend must not invoke user callbacks or reenter this registry. */
esp_err_t CommandRegistry_RegisterWithBackend(const esp_console_cmd_t *cmd,
    CommandCategory category, esp_err_t (*backend)(const esp_console_cmd_t *));

/**
 * @brief Remove a command from the registry.
 */
bool CommandRegistry_Unregister(const char *name);

/* Caller-owned results remain valid after concurrent registry changes.
 * CMD_CAT_MAX matches every category. Reset cursor to zero before each scan.
 * Each batch is consistent; separate batches are not a whole-registry snapshot.
 * Returns copied count, zero at end, or -1 on invalid arguments/lock failure. */
int CommandRegistry_CopyMatches(const char *prefix, CommandCategory category,
    unsigned *cursor, CommandEntry *results, size_t capacity);

/**
 * @brief Return the number of active commands.
 */
int CommandRegistry_GetCount(void);

/**
 * @brief Find an entry by command name.
 */
bool CommandRegistry_CopyByName(const char *name, CommandEntry *result);

/**
 * @brief Clear all entries.
 */
void CommandRegistry_Clear(void);

#endif /* COMMAND_REGISTRY_H */
