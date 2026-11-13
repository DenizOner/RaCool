#include "Controllers/UIController/EventHandlers/UIEventHandlerZigbee.h"
#include "Controllers/UIController/UIControllerInternal.h"
#include "esp_log.h"

static const char *ZB_HANDLER_TAG = "UIEventHandlerZigbee";

void UIController_OnZigbeeMenuAction(BusEvent event) {
  (void)event;
  if (!uiControllerInstance.zbActionSelected) {
    bool isConnected = uiControllerInstance.zigbee &&
                       uiControllerInstance.zigbee->IsConnected(uiControllerInstance.zigbee);
    if (isConnected) {
      ESP_LOGW(ZB_HANDLER_TAG, "[PAIRING] EventBus [YENIDEN] -> Disconnecting from network then re-pairing");
      if (uiControllerInstance.zigbee->DisconnectFromNetwork) {
        uiControllerInstance.zigbee->DisconnectFromNetwork(uiControllerInstance.zigbee);
      }
      vTaskDelay(pdMS_TO_TICKS(300));
    } else {
      ESP_LOGI(ZB_HANDLER_TAG, "[PAIRING] EventBus action -> Triggering Zigbee pairing network steering");
    }
    if (uiControllerInstance.zigbee) {
      uiControllerInstance.zigbee->ConnectToNetwork(uiControllerInstance.zigbee);
    }
  } else {
    ESP_LOGI(ZB_HANDLER_TAG, "[PAIRING] EventBus action -> Exiting pairing screen to CAT_DEVICE");
    uiControllerInstance.zbActionSelected = false;
    if (uiControllerInstance.menu) {
      uiControllerInstance.menu->SetState(uiControllerInstance.menu, UI_STATE_CAT_DEVICE);
    }
  }
}
