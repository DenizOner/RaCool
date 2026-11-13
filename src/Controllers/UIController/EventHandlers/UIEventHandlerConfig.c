#include "Controllers/UIController/EventHandlers/UIEventHandlerConfig.h"
#include "Controllers/UIController/UIControllerInternal.h"

void UIController_OnConfigChanged(BusEvent event) {
  (void)event;
  UIEvent uiEvent = { .type = UI_EVENT_CONFIG_CHANGED };
  UIController_PostEvent(&uiControllerInstance.base, &uiEvent);
}
