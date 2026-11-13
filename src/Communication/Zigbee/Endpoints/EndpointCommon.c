#if !defined(RACOOL_TARGET_TESTING) || defined(RACOOL_INTERNAL_TEST_ACCESS)
#include "Communication/Zigbee/Endpoints/EndpointCommon.h"
#include "Communication/Zigbee/ZigbeeConfig.h"
#include "Communication/Zigbee/ZigbeeAttributePolicy.h"
#include "esp_zigbee.h"
#include <string.h>
#include "Management/ConfigurationManager/ZigbeeReportingSettings.h"
#include "Communication/Zigbee/IZigbee.h"
#include "Management/PowerManager/IPowerManager.h"
#include "Controllers/SystemController/ISystemController.h"
#include "Core/ServiceLocator/ServiceLocator.h"
#include "Core/ServiceLocator/ServiceNames.h"
#include "esp_log.h"
#include "Management/ConfigurationManager/IConfigurationManager.h"

/* SDK registration creates reporting records only for reportable descriptors. */
static esp_err_t PrepareControlReporting(uint8_t endpoint, ezb_zcl_cluster_desc_t cluster) {
    uint16_t id = ezb_zcl_cluster_desc_get_id(cluster);
    if (id != EZB_ZCL_CLUSTER_ID_ON_OFF && id != EZB_ZCL_CLUSTER_ID_LEVEL &&
        id != EZB_ZCL_CLUSTER_ID_THERMOSTAT && id != EZB_ZCL_CLUSTER_ID_THERMOSTAT_UI_CONFIG)
        return ESP_OK;
    ezb_zcl_attr_desc_t attribute = NULL;
    while ((attribute = ezb_zcl_cluster_get_next_attr_desc(cluster, attribute))) {
        /* Commands are actions, not observable persistent settings. */
        if (!ZigbeeAttribute_IsConfiguration(endpoint, id, ezb_zcl_attr_desc_get_id(attribute))) continue;
        ezb_zcl_attr_access_t access = ezb_zcl_attr_desc_get_access(attribute);
        if ((access & EZB_ZCL_ATTR_ACCESS_READ_WRITE) != EZB_ZCL_ATTR_ACCESS_READ_WRITE ||
            (access & EZB_ZCL_ATTR_ACCESS_REPORTING)) continue;
        ezb_err_t error = ezb_zcl_attr_desc_set_access(attribute, access | EZB_ZCL_ATTR_ACCESS_REPORTING);
        if (error != EZB_ERR_NONE) {
            ESP_LOGE("EndpointCommon", "Reporting descriptor rejected: cluster=0x%04x attribute=0x%04x access=0x%02x sdk_error=%d",
                id, ezb_zcl_attr_desc_get_id(attribute), (unsigned)access, (int)error);
            return ESP_FAIL;
        }
    }
    return ESP_OK;
}

esp_err_t ZigbeeEndpoint_AddCluster(ezb_af_ep_desc_t endpoint, ezb_zcl_cluster_desc_t cluster) {
    if (!cluster) return ESP_ERR_NO_MEM;
    if (!endpoint || PrepareControlReporting(ezb_af_ep_desc_get_ep_id(endpoint), cluster) != ESP_OK ||
        ezb_af_endpoint_add_cluster_desc(endpoint, cluster) != EZB_ERR_NONE) {
        ezb_zcl_free_cluster_desc(cluster);
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t EndpointCommon_RegisterSharedClusters(ezb_af_ep_desc_t endpoint, uint8_t power_source) {
    if (!endpoint) return ESP_ERR_INVALID_ARG;
    ezb_zcl_basic_cluster_server_config_t basic_cfg = {
        .zcl_version = EZB_ZCL_BASIC_ZCL_VERSION_DEFAULT_VALUE, .power_source = power_source
    };
    ezb_zcl_cluster_desc_t basic = ezb_zcl_basic_create_cluster_desc(&basic_cfg, EZB_ZCL_CLUSTER_SERVER);
    if (!basic) return ESP_ERR_NO_MEM;
    if (ezb_zcl_basic_cluster_desc_add_attr(basic, EZB_ZCL_ATTR_BASIC_MANUFACTURER_NAME_ID, ZIGBEE_MANUFACTURER_NAME) != EZB_ERR_NONE ||
        ezb_zcl_basic_cluster_desc_add_attr(basic, EZB_ZCL_ATTR_BASIC_MODEL_IDENTIFIER_ID, ZIGBEE_MODEL_IDENTIFIER) != EZB_ERR_NONE ||
        ezb_zcl_basic_cluster_desc_add_attr(basic, EZB_ZCL_ATTR_BASIC_DATE_CODE_ID, ZIGBEE_DATE_CODE) != EZB_ERR_NONE ||
        ezb_zcl_basic_cluster_desc_add_attr(basic, EZB_ZCL_ATTR_BASIC_SW_BUILD_ID_ID, RACOOL_PRODUCT_ZCL_VERSION) != EZB_ERR_NONE) {
        ezb_zcl_free_cluster_desc(basic);
        return ESP_FAIL;
    }
    esp_err_t result = ZigbeeEndpoint_AddCluster(endpoint, basic);
    if (result != ESP_OK) return result;
    ezb_zcl_identify_cluster_server_config_t identify_cfg = {.identify_time = 0};
    return ZigbeeEndpoint_AddCluster(endpoint,
        ezb_zcl_identify_create_cluster_desc(&identify_cfg, EZB_ZCL_CLUSTER_SERVER));
}

esp_err_t ZigbeeEndpoint_ConfigureSimpleReporting(uint8_t endpoint_id,
                                             uint16_t cluster_id,
                                             uint16_t attr_id,
                                             uint8_t attr_type,
                                             void *reportable_change,
                                             uint16_t min_interval,
                                             uint16_t max_interval) {
    ezb_zcl_reporting_info_t info = ezb_zcl_reporting_info_find(
        endpoint_id, cluster_id, EZB_ZCL_CLUSTER_SERVER, attr_id, EZB_ZCL_STD_MANUF_CODE);
    
    if (info) {
        ezb_zcl_attr_variable_t delta = {0};
        if (reportable_change) {
            if (attr_type == EZB_ZCL_ATTR_TYPE_BOOL || attr_type == EZB_ZCL_ATTR_TYPE_UINT8 || attr_type == EZB_ZCL_ATTR_TYPE_INT8 || attr_type == EZB_ZCL_ATTR_TYPE_ENUM8 || attr_type == EZB_ZCL_ATTR_TYPE_DATA8) {
                delta.u8 = *(uint8_t*)reportable_change;
            } else if (attr_type == EZB_ZCL_ATTR_TYPE_UINT16 || attr_type == EZB_ZCL_ATTR_TYPE_INT16 || attr_type == EZB_ZCL_ATTR_TYPE_ENUM16 || attr_type == EZB_ZCL_ATTR_TYPE_DATA16) {
                delta.u16 = *(uint16_t*)reportable_change;
            } else if (attr_type == EZB_ZCL_ATTR_TYPE_SINGLE || attr_type == EZB_ZCL_ATTR_TYPE_UINT32 || attr_type == EZB_ZCL_ATTR_TYPE_INT32 || attr_type == EZB_ZCL_ATTR_TYPE_DATA32) {
                delta.u32 = *(uint32_t*)reportable_change;
            }
        }
        
        return ZigbeeEndpoint_ApplyReporting(info, min_interval, max_interval, &delta);
    }
    return ESP_ERR_NOT_FOUND;
}

esp_err_t ZigbeeEndpoint_ApplyReporting(ezb_zcl_reporting_info_t info, uint16_t minimum,
    uint16_t maximum, const ezb_zcl_attr_variable_t *delta) {
    if (!info) return ESP_ERR_NOT_FOUND;
    ezb_err_t result;
    if (maximum == 0) {
        /* Keep the ZCL readback consistent with the disabled scheduler. */
        result = ezb_zcl_reporting_info_update(info, minimum, UINT16_MAX, delta);
        if (result != EZB_ERR_NONE) ESP_LOGE("ZigbeeReporting", "Disabled reporting metadata update failed: %d", result);
        /* Stop still runs if metadata update failed; preserve the first failure. */
        ezb_err_t stopped = ezb_zcl_reporting_stop_attr_report(info);
        if (result == EZB_ERR_NONE) result = stopped;
    } else {
        if (minimum > maximum) minimum = maximum;
        result = ezb_zcl_reporting_info_update(info, minimum, maximum, delta);
        /* info_update already processes/reschedules the attribute. Starting it again
         * resets that state in the installed SDK and can suppress periodic reports. */
    }
    if (result != EZB_ERR_NONE) ESP_LOGE("ZigbeeReporting", "Reporting update failed: %d", result);
    return result == EZB_ERR_NONE ? ESP_OK : ESP_FAIL;
}
bool ZigbeeEndpoint_HandleDisplaySetting(const ezb_zcl_set_attr_value_message_t *message, esp_err_t *result) {
    if (!message || !result || message->info.cluster_id != EZB_ZCL_CLUSTER_ID_THERMOSTAT_UI_CONFIG) return false;
    uint16_t id = message->in.attribute.id;
    if (id != 0x8020 && id != 0x8021 && id != EZB_ZCL_ATTR_THERMOSTAT_UI_CONFIG_TEMPERATURE_DISPLAY_MODE_ID) return false;
    *result = ESP_ERR_INVALID_ARG;
    if (!message->in.attribute.data.value) return true;
    uint8_t mode = 0; uint32_t timeout = 0;
    ConfigurationFieldUpdate field;
    if (id == EZB_ZCL_ATTR_THERMOSTAT_UI_CONFIG_TEMPERATURE_DISPLAY_MODE_ID) {
        if (message->in.attribute.data.type != EZB_ZCL_ATTR_TYPE_ENUM8 || message->in.attribute.data.size != 1) return true;
        mode = *(const uint8_t *)message->in.attribute.data.value;
        if (mode > 1) return true;
        field = (ConfigurationFieldUpdate){offsetof(SystemBaseConfigs, temp_unit), &mode, sizeof(mode)};
    } else if (id == 0x8020) {
        if (message->in.attribute.data.type != EZB_ZCL_ATTR_TYPE_UINT8 || message->in.attribute.data.size != 1) return true;
        mode = *(const uint8_t *)message->in.attribute.data.value;
        if (!SystemConfigs_IsValidDisplayMode(mode)) return true;
        field = (ConfigurationFieldUpdate){offsetof(SystemBaseConfigs, display_mode), &mode, sizeof(mode)};
    } else {
        if (message->in.attribute.data.type != EZB_ZCL_ATTR_TYPE_UINT16 || message->in.attribute.data.size != 2) return true;
        uint16_t raw; memcpy(&raw, message->in.attribute.data.value, sizeof(raw));
        if (raw < OLED_SLEEP_ZB_MIN_SEC || raw > OLED_SLEEP_ZB_MAX_SEC) return true;
        timeout = raw;
        field = (ConfigurationFieldUpdate){offsetof(SystemBaseConfigs, oled_timeout_sec), &timeout, sizeof(timeout)};
    }
    IConfigurationManager *config = ConfigurationManager_GetInstance();
    if (!config || !config->UpdateFields) { *result = ESP_ERR_INVALID_STATE; return true; }
    *result = config->UpdateFields(config, "system", &field, 1, true);
    return true;
}

esp_err_t ZigbeeEndpoint_RegisterDisplayCluster(ezb_af_ep_desc_t endpoint) {
    if (!endpoint) return ESP_ERR_INVALID_ARG;
    IConfigurationManager *config = ConfigurationManager_GetInstance();
    SystemSettings settings;
    if (!config || !config->GetSnapshot || config->GetSnapshot(config, &settings) != ESP_OK) return ESP_ERR_INVALID_STATE;
    uint8_t unit = settings.system.temp_unit;
    uint8_t mode = settings.system.display_mode;
    uint16_t timeout = (uint16_t)settings.system.oled_timeout_sec;
    /* The factory creates mandatory attributes; initialize them through its config. */
    ezb_zcl_thermostat_ui_config_cluster_server_config_t cluster_config = {
        .temperature_display_mode = unit,
        .keypad_lockout = EZB_ZCL_THERMOSTAT_UI_CONFIG_KEYPAD_LOCKOUT_DEFAULT_VALUE
    };
    ezb_zcl_cluster_desc_t cluster = ezb_zcl_thermostat_ui_config_create_cluster_desc(&cluster_config, EZB_ZCL_CLUSTER_SERVER);
    if (!cluster) return ESP_ERR_NO_MEM;
    if (ezb_zcl_cluster_desc_add_manuf_attr(cluster, 0x8020, EZB_ZCL_ATTR_TYPE_UINT8,
            EZB_ZCL_ATTR_ACCESS_READ_WRITE, EZB_ZCL_STD_MANUF_CODE, &mode) != EZB_ERR_NONE ||
        ezb_zcl_cluster_desc_add_manuf_attr(cluster, 0x8021, EZB_ZCL_ATTR_TYPE_UINT16,
            EZB_ZCL_ATTR_ACCESS_READ_WRITE, EZB_ZCL_STD_MANUF_CODE, &timeout) != EZB_ERR_NONE) {
        ezb_zcl_free_cluster_desc(cluster); return ESP_FAIL;
    }
    return ZigbeeEndpoint_AddCluster(endpoint, cluster);
}

bool ZigbeeEndpoint_HandleSystemCommand(const ezb_zcl_set_attr_value_message_t *message, esp_err_t *result) {
    if (!message || !result ||
        (message->info.dst_ep != ZIGBEE_EP_SYSTEM && message->info.dst_ep != ZIGBEE_EP_SENSOR_FAN) ||
        message->info.cluster_id != EZB_ZCL_CLUSTER_ID_THERMOSTAT_UI_CONFIG ||
        message->in.attribute.id != ZIGBEE_ATTR_SYSTEM_COMMAND) return false;
    *result = ESP_ERR_INVALID_ARG;
    if (!message->in.attribute.data.value || message->in.attribute.data.size != sizeof(uint8_t) ||
        message->in.attribute.data.type != EZB_ZCL_ATTR_TYPE_UINT8) return true;
    uint8_t command;
    memcpy(&command, message->in.attribute.data.value, sizeof(command));
    if (command < 1 || command > 3) return true;
    if (command == 1) {
        IPowerManager *power = (IPowerManager *)ServiceLocator_GetService(SVC_NAME_POWER_MANAGER);
        if (!power || !power->RestartAsync) { *result = ESP_ERR_INVALID_STATE; return true; }
        *result = power->RestartAsync(power, ZIGBEE_POWER_COMMAND_DELAY_MS);
    } else {
        ISystemController *system = (ISystemController *)ServiceLocator_GetService(SVC_NAME_SYSTEM_CONTROLLER);
        if (!system || !system->RequestShutdown) { *result = ESP_ERR_INVALID_STATE; return true; }
        /* Admission precedes the worker's actual sleep preparation. */
        *result = system->RequestShutdown(system, ZIGBEE_POWER_COMMAND_DELAY_MS);
    }
    return true;
}

esp_err_t ZigbeeEndpoint_RegisterReportingCluster(ezb_af_ep_desc_t endpoint, uint8_t endpoint_id) {
    static const uint8_t endpoints[] = {ZIGBEE_EP_SENSOR_FAN, ZIGBEE_EP_AMBIENT_TEMP,
        ZIGBEE_EP_CPU_TEMP, ZIGBEE_EP_SYSTEM_HEALTH, ZIGBEE_EP_MEMORY, ZIGBEE_EP_UPTIME};
    unsigned kind = 0;
    while (kind < ZIGBEE_REPORT_COUNT && endpoints[kind] != endpoint_id) ++kind;
    if (!endpoint || kind == ZIGBEE_REPORT_COUNT) return ESP_ERR_INVALID_ARG;
    uint16_t interval;
    esp_err_t result = ZigbeeReporting_GetInterval(ConfigurationManager_GetInstance(), (ZigbeeReportKind)kind, &interval);
    if (result != ESP_OK) return result;
    ezb_zcl_cluster_desc_t cluster = ezb_zcl_thermostat_ui_config_create_cluster_desc(NULL, EZB_ZCL_CLUSTER_SERVER);
    if (!cluster) return ESP_ERR_NO_MEM;
    if (ezb_zcl_cluster_desc_add_manuf_attr(cluster, 0x8030 + kind, EZB_ZCL_ATTR_TYPE_UINT16,
            EZB_ZCL_ATTR_ACCESS_READ_WRITE, EZB_ZCL_STD_MANUF_CODE, &interval) != EZB_ERR_NONE ||
        ezb_af_endpoint_add_cluster_desc(endpoint, cluster) != EZB_ERR_NONE) {
        ezb_zcl_free_cluster_desc(cluster);
        return ESP_FAIL;
    }
    return ESP_OK;
}

bool ZigbeeEndpoint_HandleReportingSetting(const ezb_zcl_set_attr_value_message_t *message, esp_err_t *result) {
    static const uint8_t endpoints[] = {ZIGBEE_EP_SENSOR_FAN, ZIGBEE_EP_AMBIENT_TEMP,
        ZIGBEE_EP_CPU_TEMP, ZIGBEE_EP_SYSTEM_HEALTH, ZIGBEE_EP_MEMORY, ZIGBEE_EP_UPTIME};
    if (!message || !result || message->info.cluster_id != EZB_ZCL_CLUSTER_ID_THERMOSTAT_UI_CONFIG ||
        message->in.attribute.id < 0x8030 || message->in.attribute.id > 0x8035) return false;
    unsigned kind = message->in.attribute.id - 0x8030;
    if (message->info.dst_ep != ZIGBEE_EP_SENSOR_FAN && message->info.dst_ep != endpoints[kind]) return false;
    *result = ESP_ERR_INVALID_ARG;
    if (!message->in.attribute.data.value || message->in.attribute.data.size != sizeof(uint16_t) ||
        message->in.attribute.data.type != EZB_ZCL_ATTR_TYPE_UINT16) return true;
    uint16_t seconds;
    memcpy(&seconds, message->in.attribute.data.value, sizeof(seconds));
    *result = ZigbeeReporting_SetInterval(ConfigurationManager_GetInstance(), (ZigbeeReportKind)kind, seconds);
    if (*result != ESP_OK) return true;
    IZigbee *zigbee = (IZigbee *)ServiceLocator_GetService(SVC_NAME_ZIGBEE);
    if (!zigbee || !zigbee->ReconfigureAllReporting || zigbee->ReconfigureAllReporting(zigbee) != 0) {
        ESP_LOGE("ZigbeeReporting", "Interval persisted but runtime refresh was not admitted");
        *result = ESP_FAIL;
    }
    return true;
}

#endif
