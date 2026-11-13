#include "Core/ServiceLocator/ServiceLocator.h"
#include "Core/ServiceLocator/ServiceNames.h"
#include "Core/EventBus/IEventBus.h"
#include "Core/EventBus/EventSubscriptions.h"
#include "Communication/Zigbee/IZigbee.h"
#include "Communication/Zigbee/ZigbeeConfig.h"
#include "Management/ConfigurationManager/SystemConfigs.h"
#include "Management/DiagnosticsManager/IDiagnosticsManager.h"
#include <esp_log.h>
#ifdef RACOOL_STARTUP_SENTINEL_DIAGNOSTIC
#include "esp_timer.h"
#endif
#include <esp_zigbee.h>
#include <esp_heap_caps.h>
#include <math.h>
#include <string.h>

static const char *TAG = "SystemZBPlugin";

/**
 * @brief Publish validated system measurements through Zigbee.
 */

static void PublishAttribute(uint8_t endpoint, uint16_t cluster, uint16_t attribute,
        void *value, uint8_t length) {
#ifdef RACOOL_STARTUP_SENTINEL_DIAGNOSTIC
    /* Diagnostic-only admission gate: preserve SDK startup values for remote
     * reads. Local sensing, health scans and fan safety keep running. */
    if ((endpoint==ZIGBEE_EP_CPU_TEMP || endpoint==ZIGBEE_EP_SYSTEM_HEALTH) &&
        esp_timer_get_time()<60000000LL) return;
#endif
    IZigbee *radio = (IZigbee *)ServiceLocator_GetService(SVC_NAME_ZIGBEE);
    if (!radio || !radio->IsConnected || !radio->QueueAttributeUpdate || radio->IsConnected(radio) <= 0) return;
    int32_t result = radio->QueueAttributeUpdate(radio, endpoint, cluster, attribute, value, length);
    if (result != ZIGBEE_ERROR_OK)
        ESP_LOGE(TAG, "Attribute queue admission failed: endpoint=%u cluster=0x%04x attribute=0x%04x result=%ld",
            (unsigned)endpoint, (unsigned)cluster, (unsigned)attribute, (long)result);
}

static void OnInternalTempUpdated(BusEvent event) {
    if (!event.data || event.dataSize < sizeof(float)) return;
    float temp;
    memcpy(&temp, event.data, sizeof(temp));
    if (!isfinite(temp) || temp < -327.67f || temp > 327.67f) return;
    int16_t zb_temp = (int16_t)(temp * 100);

    PublishAttribute(ZIGBEE_EP_CPU_TEMP, 
                                   EZB_ZCL_CLUSTER_ID_TEMPERATURE_MEASUREMENT,
                                   EZB_ZCL_ATTR_TEMPERATURE_MEASUREMENT_MEASURED_VALUE_ID,
                                   &zb_temp, sizeof(zb_temp));
}

static void OnMemoryUsageUpdated(BusEvent event) {
    if (!event.data || event.dataSize < sizeof(uint32_t)) return;
    uint32_t free_heap;
    memcpy(&free_heap, event.data, sizeof(free_heap));
    size_t total_heap = heap_caps_get_total_size(MALLOC_CAP_DEFAULT);
    if (!total_heap || free_heap > total_heap) return;
    float val = (float)((double)free_heap / (double)total_heap * 100.0);

    PublishAttribute(ZIGBEE_EP_MEMORY, 
                                   EZB_ZCL_CLUSTER_ID_ANALOG_INPUT,
                                   EZB_ZCL_ATTR_ANALOG_INPUT_PRESENT_VALUE_ID,
                                   &val, sizeof(val));
}

static void OnUptimeUpdated(BusEvent event) {
    if (!event.data || event.dataSize < sizeof(uint32_t)) return;
    uint32_t uptime;
    memcpy(&uptime, event.data, sizeof(uptime));
    float val = (float)uptime;

    PublishAttribute(ZIGBEE_EP_UPTIME, 
                                   EZB_ZCL_CLUSTER_ID_ANALOG_INPUT,
                                   EZB_ZCL_ATTR_ANALOG_INPUT_PRESENT_VALUE_ID,
                                   &val, sizeof(val));
}

static void OnHealthUpdated(BusEvent event) {
    if (!event.data || event.dataSize < sizeof(int32_t)) return;
    int32_t health;
    memcpy(&health, event.data, sizeof(health));
    if (health < 0 || health > 100) return;
    float val = (float)health;

    PublishAttribute(ZIGBEE_EP_SYSTEM_HEALTH, 
                                   EZB_ZCL_CLUSTER_ID_ANALOG_INPUT,
                                   EZB_ZCL_ATTR_ANALOG_INPUT_PRESENT_VALUE_ID,
                                   &val, sizeof(val));
}

static EventSubscriptions subscriptions;
static const EventSubscription subscriptionTable[] = {
    {EVENT_INTERNAL_TEMP_UPDATED, OnInternalTempUpdated},
    {"sys.memory.updated", OnMemoryUsageUpdated},
    {"sys.uptime.updated", OnUptimeUpdated},
    {"diag.health.updated", OnHealthUpdated},
    {EVENT_DIAG_HEALTH_CHANGED, OnHealthUpdated}
};

int32_t SystemZigbeePlugin_Init(void) {
#ifdef RACOOL_STARTUP_SENTINEL_DIAGNOSTIC
    ESP_LOGI(TAG,"[DIAG-STARTUP-WIRE] CPU/health publication held until boot+60000ms; local producers remain active");
#endif
    IEventBus *bus = ServiceLocator_GetEventBus();
    int32_t result = EventSubscriptions_Attach(&subscriptions, bus, subscriptionTable,
        sizeof(subscriptionTable)/sizeof(subscriptionTable[0]));
    if (result != 0)
        ESP_LOGE(TAG, "Event subscription failed: result=%ld retained=0x%lx", (long)result, (unsigned long)subscriptions.owned);
    return result;
}
