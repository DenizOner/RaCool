#ifndef ZIGBEE_RADIO_BOUNDARY_H
#define ZIGBEE_RADIO_BOUNDARY_H
#include "esp_err.h"

/* The sole SDK owner begins an operation after external leases have drained.
 * Link wrappers preserve the first native radio error that SDK 2.0.4 discards. */
void ZigbeeRadioBoundary_Begin(void);
esp_err_t ZigbeeRadioBoundary_Result(void);
#endif
