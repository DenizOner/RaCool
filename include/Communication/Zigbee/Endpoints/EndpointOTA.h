#ifndef ENDPOINT_OTA_H
#define ENDPOINT_OTA_H

#include "Communication/Zigbee/IZigbeeEndpoint.h"

/**
 * @brief OTA state machine durumları
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
 * @brief OTA Upgrade Client (EP10) nesnesini oluşturur.
 *
 * Zigbee OTA Upgrade Cluster client rolünde çalışır.
 * State machine ile download/verify/apply lifecycle'ı yönetir.
 */
IZigbeeEndpoint *EndpointOTA_Create(void);

#endif // ENDPOINT_OTA_H
