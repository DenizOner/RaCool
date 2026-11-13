#include "Management/ConfigurationManager/ZigbeeReportingSettings.h"
#include <string.h>

static const size_t field_offsets[ZIGBEE_REPORT_COUNT] = {
    offsetof(ZigbeeReportingConfigs, fan_s), offsetof(ZigbeeReportingConfigs, ambient_s),
    offsetof(ZigbeeReportingConfigs, cpu_s), offsetof(ZigbeeReportingConfigs, health_s),
    offsetof(ZigbeeReportingConfigs, memory_s), offsetof(ZigbeeReportingConfigs, uptime_s)
};

esp_err_t ZigbeeReporting_SetInterval(IConfigurationManager *config, ZigbeeReportKind kind, uint32_t seconds) {
    if ((unsigned)kind >= ZIGBEE_REPORT_COUNT || seconds > ZB_REPORT_MAX_S) return ESP_ERR_INVALID_ARG;
    if (!config || !config->UpdateFields) return ESP_ERR_INVALID_STATE;
    uint16_t value = (uint16_t)seconds;
    ConfigurationFieldUpdate field = {.offset = field_offsets[kind], .data = &value, .size = sizeof(value)};
    return config->UpdateFields(config, "zigbee_reporting", &field, 1, true);
}
esp_err_t ZigbeeReporting_GetInterval(IConfigurationManager *config, ZigbeeReportKind kind, uint16_t *seconds) {
    if ((unsigned)kind >= ZIGBEE_REPORT_COUNT || !seconds) return ESP_ERR_INVALID_ARG;
    if (!config || !config->GetSnapshot) return ESP_ERR_INVALID_STATE;
    SystemSettings snapshot;
    esp_err_t result = config->GetSnapshot(config, &snapshot);
    if (result == ESP_OK)
        memcpy(seconds, (const uint8_t *)&snapshot.zigbee_reporting + field_offsets[kind], sizeof(*seconds));
    return result;
}
