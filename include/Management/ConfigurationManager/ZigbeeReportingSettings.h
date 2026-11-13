#ifndef ZIGBEE_REPORTING_SETTINGS_H
#define ZIGBEE_REPORTING_SETTINGS_H
#include "Management/ConfigurationManager/IConfigurationManager.h"

typedef enum {
    ZIGBEE_REPORT_FAN, ZIGBEE_REPORT_AMBIENT, ZIGBEE_REPORT_CPU,
    ZIGBEE_REPORT_HEALTH, ZIGBEE_REPORT_MEMORY, ZIGBEE_REPORT_UPTIME,
    ZIGBEE_REPORT_COUNT
} ZigbeeReportKind;

/* Shared by HA, OLED and CLI. Zero disables unsolicited reports. */
esp_err_t ZigbeeReporting_SetInterval(IConfigurationManager *config, ZigbeeReportKind kind, uint32_t seconds);
esp_err_t ZigbeeReporting_GetInterval(IConfigurationManager *config, ZigbeeReportKind kind, uint16_t *seconds);
#endif
