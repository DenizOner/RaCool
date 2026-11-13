#include "Communication/DebugConsole/Core/CommandRegistry.h"
#include "Communication/DebugConsole/DebugConsoleConfig.h"
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

static CommandEntry s_commands[DEBUG_CONSOLE_MAX_COMMANDS];
static int s_commandCount = 0;
static SemaphoreHandle_t s_registryMutex = NULL;

bool CommandRegistry_Init(void) {
    if (s_registryMutex == NULL) {
        s_registryMutex = xSemaphoreCreateMutex();
    }
    if (!s_registryMutex || xSemaphoreTake(s_registryMutex, pdMS_TO_TICKS(100)) != pdTRUE) return false;
    s_commandCount = 0;
    memset(s_commands, 0, sizeof(s_commands));
    xSemaphoreGive(s_registryMutex);
    return true;
}

esp_err_t CommandRegistry_RegisterWithBackend(const esp_console_cmd_t *cmd,
        CommandCategory category, esp_err_t (*backend)(const esp_console_cmd_t *)) {
    if (!cmd || !cmd->command || !cmd->command[0] || (unsigned)category >= CMD_CAT_MAX ||
        strlen(cmd->command) >= sizeof(s_commands[0].command)) return ESP_ERR_INVALID_ARG;
    if (!s_registryMutex || xSemaphoreTake(s_registryMutex, pdMS_TO_TICKS(100)) != pdTRUE)
        return ESP_ERR_TIMEOUT;
    int slot = -1;
    for (int i = 0; i < s_commandCount; i++) {
        if (s_commands[i].active && strcmp(s_commands[i].command, cmd->command) == 0) {
            slot = i; break;
        }
    }
    if (slot < 0) {
        for (int i = 0; i < s_commandCount; i++) {
            if (!s_commands[i].active) { slot = i; break; }
        }
    }
    if (slot < 0 && s_commandCount >= DEBUG_CONSOLE_MAX_COMMANDS) {
        xSemaphoreGive(s_registryMutex);
        return ESP_ERR_NO_MEM;
    }
    /* SDK registration does not invoke the command. Commit only on success;
     * hold the registry lock so another writer cannot consume the reserved slot. */
    esp_err_t result = backend ? backend(cmd) : ESP_OK;
    if (result == ESP_OK) {
        if (slot < 0) slot = s_commandCount++;
        CommandEntry *entry = &s_commands[slot];
        memset(entry, 0, sizeof(*entry));
        memcpy(entry->command, cmd->command, strlen(cmd->command) + 1);
        if (cmd->help) {
            strncpy(entry->help, cmd->help, sizeof(entry->help) - 1);
        }
        entry->category = category;
        entry->func = cmd->func;
        entry->active = true;
    }
    xSemaphoreGive(s_registryMutex);
    return result;
}

bool CommandRegistry_Register(const esp_console_cmd_t *cmd, CommandCategory category) {
    return CommandRegistry_RegisterWithBackend(cmd, category, NULL) == ESP_OK;
}

bool CommandRegistry_Unregister(const char *name) {
    if (!name || !s_registryMutex) return false;

    if (xSemaphoreTake(s_registryMutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return false;
    }

    for (int i = 0; i < s_commandCount; i++) {
        if (s_commands[i].active &&
            strcmp(s_commands[i].command, name) == 0) {
            s_commands[i].active = false;
            xSemaphoreGive(s_registryMutex);
            return true;
        }
    }

    xSemaphoreGive(s_registryMutex);
    return false;
}

int CommandRegistry_CopyMatches(const char *prefix, CommandCategory category,
        unsigned *cursor, CommandEntry *results, size_t capacity) {
    if (!cursor || !results || !capacity || (unsigned)category > CMD_CAT_MAX ||
        *cursor > DEBUG_CONSOLE_MAX_COMMANDS) return -1;
    if (!s_registryMutex || xSemaphoreTake(s_registryMutex, pdMS_TO_TICKS(100)) != pdTRUE) return -1;
    size_t length = prefix ? strlen(prefix) : 0;
    size_t found = 0;
    while (*cursor < (unsigned)s_commandCount && found < capacity) {
        const CommandEntry *entry = &s_commands[(*cursor)++];
        if (entry->active && (category == CMD_CAT_MAX || entry->category == category) &&
            (!prefix || strncasecmp(entry->command, prefix, length) == 0)) results[found++] = *entry;
    }
    xSemaphoreGive(s_registryMutex);
    return (int)found;
}

int CommandRegistry_GetCount(void) {
    int count = 0;
    if (s_registryMutex && xSemaphoreTake(s_registryMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        for (int i = 0; i < s_commandCount; i++) {
            if (s_commands[i].active) count++;
        }
        xSemaphoreGive(s_registryMutex);
    }
    return count;
}

bool CommandRegistry_CopyByName(const char *name, CommandEntry *result) {
    if (!name || !result || !s_registryMutex) return false;
    bool found = false;
    if (xSemaphoreTake(s_registryMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        for (int i = 0; i < s_commandCount; i++) {
            if (s_commands[i].active && strcmp(s_commands[i].command, name) == 0) {
                *result = s_commands[i];
                found = true;
                break;
            }
        }
        xSemaphoreGive(s_registryMutex);
    }
    return found;
}

void CommandRegistry_Clear(void) {
    if (s_registryMutex && xSemaphoreTake(s_registryMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        s_commandCount = 0;
        memset(s_commands, 0, sizeof(s_commands));
        xSemaphoreGive(s_registryMutex);
    }
}
