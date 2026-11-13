#if !defined(RACOOL_TARGET_TESTING) || defined(RACOOL_INTERNAL_TEST_ACCESS)
#include "Communication/Zigbee/Handlers/SignalHandlerCore.h"
#include "Core/EventBus/IEventBus.h"
#include "Core/ServiceLocator/ServiceLocator.h"
#include "Core/LogManager/ILogManager.h"
#include "esp_zigbee.h"
#include "Communication/Zigbee/ZigbeeConfig.h"

static const char *TAG_SH = "SignalHandlerCore";

typedef struct {
    IZigbeeSignalHandler base;
    uint8_t rejoinAttempt;
    bool isCommissioning;
} SignalHandlerCore;

static SignalHandlerCore handlerInstance;

// --- Event Publishing Helper ---

static void PublishBusEvent(const char *name, EventType type) {
    IEventBus *bus = ServiceLocator_GetEventBus();
    if (bus) {
        BusEvent event = {.eventName = name, .type = type};
        bus->Publish(event);
    }
}

// --- Signal Handler ---

static void HandleSignal(IZigbeeSignalHandler *self, esp_zb_app_signal_type_t sig_type, esp_err_t err_status, const void *params) {
    SignalHandlerCore *handler = (SignalHandlerCore *)self;
    sig_type = (esp_zb_app_signal_type_t)(sig_type & 0xFFFF);
    const char *sigName = ezb_app_signal_to_string(sig_type);

    ESP_LOGI(TAG_SH, "Processing signal 0x%04X (%s), status: %s",
             (unsigned int)sig_type, sigName ? sigName : "UNKNOWN", esp_err_to_name(err_status));

    switch (sig_type) {
    case EZB_ZDO_SIGNAL_SKIP_STARTUP:
        ESP_LOGI(TAG_SH, "Zigbee stack initializing (SKIP_STARTUP received)");
        handlerInstance.isCommissioning = true;
        ezb_bdb_start_top_level_commissioning(EZB_BDB_MODE_INITIALIZATION);
        break;

    case EZB_BDB_SIGNAL_DEVICE_FIRST_START:
    case EZB_BDB_SIGNAL_DEVICE_REBOOT: {
        handlerInstance.isCommissioning = false;
        if (err_status == ESP_OK) {
            bool isFactoryNew = ezb_bdb_is_factory_new();
            ESP_LOGI(TAG_SH, "Device started, factory_new=%s", isFactoryNew ? "Yes" : "No");

            if (isFactoryNew) {
                ESP_LOGI(TAG_SH, "Device started (factory new). Waiting for user pairing command from menu.");
                handler->rejoinAttempt = 0;
            } else {
                ESP_LOGI(TAG_SH, "Device rejoined successfully. PAN=0x%04x, Ch=%d, Addr=0x%04x",
                       ezb_get_panid(), ezb_get_current_channel(), ezb_get_short_address());
                handler->rejoinAttempt = 0;
                PublishBusEvent("zigbee_connected", EVENT_TYPE_INFO);
            }
        } else {
            ESP_LOGE(TAG_SH, "Failed to initialize Zigbee stack: %s", esp_err_to_name(err_status));
        }
        break;
    }

    case EZB_BDB_SIGNAL_STEERING: {
        handlerInstance.isCommissioning = false;
        if (err_status == ESP_OK) {
            ezb_extpanid_t extended_pan_id;
            ezb_get_extended_panid(&extended_pan_id);
            ESP_LOGI(TAG_SH, "Joined network: PAN=0x%04x, Ch=%d, ExtPAN=%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x",
                     ezb_get_panid(), ezb_get_current_channel(),
                     extended_pan_id.u8[7], extended_pan_id.u8[6], extended_pan_id.u8[5], extended_pan_id.u8[4],
                     extended_pan_id.u8[3], extended_pan_id.u8[2], extended_pan_id.u8[1], extended_pan_id.u8[0]);

            handler->rejoinAttempt = 0;
            PublishBusEvent("zigbee_connected", EVENT_TYPE_INFO);
            PublishBusEvent("zigbee_pairing_stop", EVENT_TYPE_INFO);
        } else {
            ESP_LOGW(TAG_SH, "Steering pass finished: %s", esp_err_to_name(err_status));
            handler->rejoinAttempt++;
        }
        break;
    }

    case EZB_ZDO_SIGNAL_LEAVE:
        ESP_LOGW(TAG_SH, "Left network");
        PublishBusEvent("zigbee_disconnected", EVENT_TYPE_WARNING);
        handler->rejoinAttempt = 0;
        break;

    case EZB_ZDO_SIGNAL_DEVICE_ANNCE:
        ESP_LOGI(TAG_SH, "Child device announced (Router active)");
        break;

    case EZB_NWK_SIGNAL_PERMIT_JOIN_STATUS: {
        ezb_nwk_signal_permit_join_status_params_t *pj_params = (ezb_nwk_signal_permit_join_status_params_t *)params;
        ESP_LOGI(TAG_SH, "Permit join status updated: duration=%d", pj_params ? pj_params->duration : 0);
        break;
    }

    default: {
        ESP_LOGD(TAG_SH, "Unhandled signal: 0x%x (%s)", (unsigned int)sig_type, sigName ? sigName : "UNKNOWN");
        break;
    }
    }
}

IZigbeeSignalHandler *SignalHandlerCore_Create(void) {
    handlerInstance.base.HandleSignal = HandleSignal;
    handlerInstance.base.next = NULL;
    handlerInstance.rejoinAttempt = 0;
    handlerInstance.isCommissioning = false;
    return &handlerInstance.base;
}

#endif // RACOOL_TARGET_TESTING) && !defined(RACOOL_INTERNAL_TEST_ACCESS
