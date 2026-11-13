/**
 * @file InfraredTemperatureSensorConsoleCommands.h
 * @brief Debug Console CLI commands for InfraredTemperatureSensor Module.
 */
#ifndef INFRARED_TEMP_SENSOR_CONSOLE_COMMANDS_H
#define INFRARED_TEMP_SENSOR_CONSOLE_COMMANDS_H

#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t InfraredTemperatureSensor_RegisterConsoleCommands(void);

#ifdef __cplusplus
}
#endif

#endif // INFRARED_TEMP_SENSOR_CONSOLE_COMMANDS_H
