/**
 * @file I2CManagerConsoleCommands.h
 * @brief Debug Console CLI commands for I2CManager Module.
 */
#ifndef I2C_MANAGER_CONSOLE_COMMANDS_H
#define I2C_MANAGER_CONSOLE_COMMANDS_H

#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Return the first registration error; partial entries remain console-owned. */
esp_err_t I2CManager_RegisterConsoleCommands(void);

#ifdef __cplusplus
}
#endif

#endif // I2C_MANAGER_CONSOLE_COMMANDS_H
