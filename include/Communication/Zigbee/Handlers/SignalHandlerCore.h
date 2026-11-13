#ifndef SIGNAL_HANDLER_CORE_H
#define SIGNAL_HANDLER_CORE_H

#include "Communication/Zigbee/IZigbeeSignalHandler.h"

/**
 * @brief Core Zigbee sinyal işleyicisi oluşturur.
 * 
 * Ağ bağlantısı, steering ve discovery gibi temel Zigbee sinyallerini yönetir.
 */
IZigbeeSignalHandler* SignalHandlerCore_Create(void);

#endif // SIGNAL_HANDLER_CORE_H
