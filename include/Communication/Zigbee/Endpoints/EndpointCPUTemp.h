#ifndef ENDPOINT_CPU_TEMP_H
#define ENDPOINT_CPU_TEMP_H

#include "Communication/Zigbee/IZigbeeEndpoint.h"

/**
 * @brief CPU Sıcaklığı (EP5) nesnesini oluşturur.
 */
IZigbeeEndpoint *EndpointCPUTemp_Create(void);

#endif // ENDPOINT_CPU_TEMP_H
