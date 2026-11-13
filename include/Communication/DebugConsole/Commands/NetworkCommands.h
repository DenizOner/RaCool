#ifndef NETWORK_COMMANDS_H
#define NETWORK_COMMANDS_H

#include "Communication/DebugConsole/IDebugConsole.h"

/**
 * @file NetworkCommands.h
 * @brief Ag ve Zigbee teshis komutlari.
 *
 * Home Assistant / Zigbee ag durumu, baglanti teshis komutlari.
 */
void NetworkCommands_Register(IDebugConsole *console);

#endif /* NETWORK_COMMANDS_H */
