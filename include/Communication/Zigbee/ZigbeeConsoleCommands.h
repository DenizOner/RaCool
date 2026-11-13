#ifndef ZIGBEE_CONSOLE_COMMANDS_H
#define ZIGBEE_CONSOLE_COMMANDS_H
#include "esp_err.h"

/**
 * @brief Register Zigbee commands and dashboard fields during service startup.
 */
esp_err_t Zigbee_RegisterConsoleCommands(void);

/* Owner worker only, after successful SDK deinit has retired every callback. */
void Zigbee_ConsoleSdkStopped(void);

#endif // ZIGBEE_CONSOLE_COMMANDS_H
