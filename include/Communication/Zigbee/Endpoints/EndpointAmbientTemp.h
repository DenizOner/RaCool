#ifndef ENDPOINT_AMBIENT_TEMP_H
#define ENDPOINT_AMBIENT_TEMP_H

#include "Communication/Zigbee/IZigbeeEndpoint.h"

/**
 * @brief Ortam Sıcaklığı (EP4) nesnesini oluşturur.
 */
IZigbeeEndpoint *EndpointAmbientTemp_Create(void);

#endif // ENDPOINT_AMBIENT_TEMP_H
