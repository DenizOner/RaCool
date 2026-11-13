/**
 * @file InternalTemperatureSensorConsoleCommands.h
 * @brief Debug Console CLI commands for InternalTemperatureSensor Module.
 */
#ifndef INTERNAL_TEMP_SENSOR_CONSOLE_COMMANDS_H
#define INTERNAL_TEMP_SENSOR_CONSOLE_COMMANDS_H

#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Return the first registration error; partial entries remain console-owned. */
esp_err_t InternalTemperatureSensor_RegisterConsoleCommands(void);

#ifdef __cplusplus
}
#endif

#endif // INTERNAL_TEMP_SENSOR_CONSOLE_COMMANDS_H
