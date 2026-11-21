#ifndef ENDPOINT_OTA_H
#define ENDPOINT_OTA_H

#include "Communication/Zigbee/IZigbeeEndpoint.h"

/**
 * @brief Observable OTA progress states.
 *
 * The current endpoint does not have a verified image writer: progress
 * callbacks reject transfer/verification/application until the complete
 * flash, integrity and rollback owner is implemented.
 */
typedef enum {
    OTA_STATE_IDLE = 0,
    OTA_STATE_QUERYING,
    OTA_STATE_DOWNLOADING,
    OTA_STATE_VERIFYING,
    OTA_STATE_APPLYING,
    OTA_STATE_ERROR
} OtaUpgradeState;

/**
 * @brief Register the existing EP10 Zigbee OTA client descriptor.
 *
 * This registers discovery attributes, not a working firmware installer.
 * Until an image writer is implemented, transfer callbacks return ABORT.
 */
IZigbeeEndpoint *EndpointOTA_Create(void);

#endif // ENDPOINT_OTA_H
