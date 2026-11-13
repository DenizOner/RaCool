#ifndef RACOOL_ZIGBEE_ATTRIBUTE_POLICY_H
#define RACOOL_ZIGBEE_ATTRIBUTE_POLICY_H
#include "esp_zigbee.h"
#include "Communication/Zigbee/ZigbeeConfig.h"
#include <stdbool.h>

/* Active settings only; legacy aliases and command attributes are not settings reports. */
static inline bool ZigbeeAttribute_IsConfiguration(uint8_t endpoint, uint16_t cluster, uint16_t attribute) {
    if (cluster == EZB_ZCL_CLUSTER_ID_THERMOSTAT_UI_CONFIG) {
        if (endpoint == ZIGBEE_EP_SENSOR_FAN)
            return attribute == 0x8030 || (attribute >= 0x8050 && attribute <= 0x8054);
        if (endpoint >= ZIGBEE_EP_AMBIENT_TEMP && endpoint <= ZIGBEE_EP_UPTIME)
            return attribute == 0x8030 + endpoint - 3;
        if (endpoint == ZIGBEE_EP_DISPLAY) return attribute >= 0x8020 && attribute <= 0x8022;
        if (endpoint == ZIGBEE_EP_SYSTEM)
            return attribute == ZIGBEE_ATTR_POWER_PROFILE || attribute == ZIGBEE_ATTR_TX_PROFILE;
        return false;
    }
    if (cluster == EZB_ZCL_CLUSTER_ID_THERMOSTAT)
        return ((endpoint == ZIGBEE_EP_SENSOR_FAN || endpoint == ZIGBEE_EP_AMBIENT_TEMP) &&
            attribute == EZB_ZCL_ATTR_THERMOSTAT_LOCAL_TEMPERATURE_CALIBRATION_ID) ||
            (endpoint == ZIGBEE_EP_SENSOR_FAN &&
            (attribute == EZB_ZCL_ATTR_THERMOSTAT_OCCUPIED_COOLING_SETPOINT_ID ||
             attribute == EZB_ZCL_ATTR_THERMOSTAT_OCCUPIED_HEATING_SETPOINT_ID ||
             attribute == EZB_ZCL_ATTR_THERMOSTAT_SYSTEM_MODE_ID));
    if (cluster == EZB_ZCL_CLUSTER_ID_LEVEL)
        return endpoint == ZIGBEE_EP_RGB_LED && attribute == EZB_ZCL_ATTR_LEVEL_CURRENT_LEVEL_ID;
    return cluster == EZB_ZCL_CLUSTER_ID_ON_OFF &&
        ((endpoint == ZIGBEE_EP_INTERNAL_LED && attribute >= 0x8000 && attribute <= 0x8005) ||
         (endpoint == ZIGBEE_EP_RGB_LED &&
          ((attribute >= 0x8013 && attribute <= 0x8014) ||
           (attribute >= 0x8020 && attribute <= 0x8027) ||
           (attribute >= 0x8030 && attribute <= 0x8034))));
}
#endif
