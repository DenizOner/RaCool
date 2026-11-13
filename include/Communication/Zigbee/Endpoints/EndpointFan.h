#ifndef ENDPOINT_FAN_H
#define ENDPOINT_FAN_H

#include "Communication/Zigbee/IZigbeeEndpoint.h"

/**
 * @brief Fan ve Ana Termostat özelliklerini barındıran Endpoint 1 nesnesini oluşturur.
 * 
 * Bu endpoint; Sıcaklık Ölçümü, On/Off (Fan/Röle), Termostat ve Termostat UI kümelerini içerir.
 */
IZigbeeEndpoint *EndpointFan_Create(void);

#endif // ENDPOINT_FAN_H
