#include "Controllers/UIController/EventHandlers/UIEventHandlerRelay.h"
#include "Controllers/UIController/UIControllerInternal.h"

void UIController_OnRelayStateChanged(BusEvent event) {
  if (!event.data || event.dataSize < sizeof(bool)) {
    return;
  }
  bool isOpen = *(const bool *)event.data;
  UIEvent uiEvent;
  uiEvent.type = UI_EVENT_RELAY_STATE;
  uiEvent.data.relay.isOpen = isOpen;
  UIController_PostEvent(&uiControllerInstance.base, &uiEvent);
}
