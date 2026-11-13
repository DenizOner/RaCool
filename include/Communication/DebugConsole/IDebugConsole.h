#ifndef IDEBUG_CONSOLE_H
#define IDEBUG_CONSOLE_H

#include <stdbool.h>
#include <stdint.h>
#include "Core/IService.h"
#include "Communication/DebugConsole/DebugConsoleConfig.h"
#include <esp_console.h>
#include <esp_err.h>

/**
 * @file IDebugConsole.h
 * @brief Debug Console service interface.
 *
 * Registration and display/log controls report admission/backend errors.
 * The interceptor setter retains its void contract. Legacy history views are
 * borrowed; new consumers must use CopyCommandHistory.
 */

/**
 * @brief Command category classifications.
 */
typedef enum {
    CMD_CAT_SYSTEM = 0,
    CMD_CAT_DIAGNOSTIC,
    CMD_CAT_PERFORMANCE,
    CMD_CAT_HARDWARE,
    CMD_CAT_NETWORK,
    CMD_CAT_DEBUG,
    CMD_CAT_CUSTOM,
    CMD_CAT_UNCATEGORIZED,
    CMD_CAT_MAX
} CommandCategory;

/**
 * @brief Debug Console service contract.
 */
typedef struct IDebugConsole {
    IService base;

    /**
     * @brief Processes and executes a raw CLI command string.
     * @param self Pointer to IDebugConsole instance.
     * @param cmd Command string to execute.
     * @return ESP_OK on success, ESP_ERR_NOT_FOUND if command is unrecognized.
     */
    esp_err_t (*ProcessCommand)(struct IDebugConsole *self, const char *cmd);

    /**
     * @brief Registers a command with the console engine.
     * @param self Pointer to IDebugConsole instance.
     * @param cmd Pointer to ESP console command structure.
     * @return ESP_OK on success, ESP_ERR_NO_MEM if capacity is full.
     */
    esp_err_t (*RegisterCommand)(struct IDebugConsole *self,
                                 const esp_console_cmd_t *cmd);

    /**
     * @brief Registers a command under a specific category.
     * @param self Pointer to IDebugConsole instance.
     * @param cmd Pointer to ESP console command structure.
     * @param category Command category enumeration value.
     * @return ESP_OK on success, ESP_ERR_NO_MEM if capacity is full.
     */
    esp_err_t (*RegisterCommandWithCategory)(struct IDebugConsole *self,
                                             const esp_console_cmd_t *cmd,
                                             CommandCategory category);

    /**
     * @brief Registers a group of commands (plugin/module).
     * @param self Pointer to IDebugConsole instance.
     * @param moduleName Name of the plugin module.
     * @param register_func Function pointer to register module commands.
     * @return The first registration API error from the owning callback task,
     *         or ESP_OK after the callback completes successfully. Successful
     *         partial entries remain registry-owned and may be retried.
     * Runtime callbacks are serialized with Start/Stop without pausing console IO.
     */
    esp_err_t (*RegisterModule)(struct IDebugConsole *self,
                                const char *moduleName,
                                void (*register_func)(struct IDebugConsole *));

    /**
     * @brief Enables or disables the Live Dashboard view.
     * @param self Pointer to IDebugConsole instance.
     * @param enabled True to enable live dashboard, false to disable.
     */
    esp_err_t (*SetLiveMode)(struct IDebugConsole *self, bool enabled);

    /**
     * @brief Registers an input interceptor callback (e.g. for Y/N prompts).
     * @param self Pointer to IDebugConsole instance.
     * @param interceptor Callback function receiving input line.
     */
    void (*SetInterceptor)(struct IDebugConsole *self,
                           bool (*interceptor)(const char *));

    /**
     * @brief Gets recent command history.
     * @param self Pointer to IDebugConsole instance.
     * @param count Output parameter for number of returned history items.
     * @return Const array of command strings.
     */
    const char *const *(*GetCommandHistory)(struct IDebugConsole *self,
                                            int *count);
    esp_err_t (*CopyCommandHistory)(struct IDebugConsole *self,
        char (*output)[DEBUG_CONSOLE_LINE_BUFFER_SIZE], size_t capacity, int *count);

    /**
     * @brief Mutes or unmutes debug log output to stdout.
     * @param self Pointer to IDebugConsole instance.
     * @param muted True to silence info logs, false to enable full logging.
     */
    esp_err_t (*SetMuteLog)(struct IDebugConsole *self, bool muted);

    /**
     * @brief Returns total count of registered commands.
     * @param self Pointer to IDebugConsole instance.
     * @return Number of active commands.
     */
    int (*GetRegisteredCommandCount)(struct IDebugConsole *self);

    /**
     * @brief Executes a batch of commands sequentially.
     * @param self Pointer to IDebugConsole instance.
     * @param commands Array of command strings.
     * @param count Number of commands in batch.
     */
    void (*ExecuteBatch)(struct IDebugConsole *self,
                         const char *commands[], int count);

} IDebugConsole;

/**
 * @brief Returns the singleton instance of Debug Console.
 * @return Pointer to IDebugConsole instance.
 */
IDebugConsole *DebugConsole_GetModule(void);

#endif /* IDEBUG_CONSOLE_H */

