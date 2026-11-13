#ifndef ENDPOINT_COMMON_H
#define ENDPOINT_COMMON_H

#include "esp_zigbee.h"
#include "Communication/Zigbee/ZigbeeConfig.h"
#include <stdint.h>

/**
 * @brief Adds standard Basic + Identify clusters to an endpoint.
 *        Modernized for Zigbee v2.x architecture.
 *
 * @param ep_desc Target endpoint descriptor
 * @param power_source Power source value (e.g. EZB_ZCL_BASIC_POWER_SOURCE_MAINS)
 */
esp_err_t EndpointCommon_RegisterSharedClusters(ezb_af_ep_desc_t ep_desc, uint8_t power_source);

/* Takes cluster ownership; frees an unattached cluster on failed admission. */
esp_err_t ZigbeeEndpoint_AddCluster(ezb_af_ep_desc_t endpoint, ezb_zcl_cluster_desc_t cluster);

/**
 * @brief Configures standard ZCL reporting for a single attribute.
 *        Uses ezb_zcl_reporting_info_update for modern v2.x logic.
 *
 * @param endpoint_id  Target endpoint ID
 * @param cluster_id   Target cluster ID
 * @param attr_id      Attribute ID
 * @param attr_type    ZCL attribute type
 * @param reportable_change Pointer to reportable change value (type matched to attr_type)
 * @param min_interval Minimum reporting interval (seconds)
 * @param max_interval Maximum reporting interval (seconds)
 */
esp_err_t ZigbeeEndpoint_ConfigureSimpleReporting(uint8_t endpoint_id,
                                             uint16_t cluster_id,
                                             uint16_t attr_id,
                                             uint8_t attr_type,
                                             void *reportable_change,
                                             uint16_t min_interval,
                                             uint16_t max_interval);

/* Caller owns the SDK lock (or runs in an SDK callback). Zero stops all unsolicited reports. */
esp_err_t ZigbeeEndpoint_ApplyReporting(ezb_zcl_reporting_info_t info, uint16_t minimum,
    uint16_t maximum, const ezb_zcl_attr_variable_t *delta);

/** Shared display setting validation for EP9 and legacy EP1 writes. */
bool ZigbeeEndpoint_HandleDisplaySetting(const ezb_zcl_set_attr_value_message_t *message, esp_err_t *result);
esp_err_t ZigbeeEndpoint_RegisterDisplayCluster(ezb_af_ep_desc_t endpoint);

/** Shared system command routing for EP10 and legacy EP1 writes. */
bool ZigbeeEndpoint_HandleSystemCommand(const ezb_zcl_set_attr_value_message_t *message, esp_err_t *result);

esp_err_t ZigbeeEndpoint_RegisterReportingCluster(ezb_af_ep_desc_t endpoint, uint8_t endpoint_id);
bool ZigbeeEndpoint_HandleReportingSetting(const ezb_zcl_set_attr_value_message_t *message, esp_err_t *result);

#endif // ENDPOINT_COMMON_H
