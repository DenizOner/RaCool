#ifndef ENDPOINT_RGB_LED_H
#define ENDPOINT_RGB_LED_H

#include "Communication/Zigbee/IZigbeeEndpoint.h"

/**
 * @brief RGB LED (EP3) nesnesini oluşturur.
 */
IZigbeeEndpoint *EndpointRGBLED_Create(void);

#endif // ENDPOINT_RGB_LED_H
