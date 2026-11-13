#ifndef ENDPOINT_INTERNAL_LED_H
#define ENDPOINT_INTERNAL_LED_H

#include "Communication/Zigbee/IZigbeeEndpoint.h"

/**
 * @brief Dahili LED (EP2) nesnesini oluşturur.
 */
IZigbeeEndpoint *EndpointInternalLED_Create(void);

#endif // ENDPOINT_INTERNAL_LED_H
