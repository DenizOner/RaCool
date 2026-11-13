#include "Controllers/UIController/EventHandlers/UIEventHandlerTemp.h"
#include "Controllers/UIController/UIControllerInternal.h"

void UIController_OnIRTempUpdated(BusEvent event) {
  if (!event.data || event.dataSize < sizeof(float) * 2) {
    return;
  }
  const float *temps = (const float *)event.data;
  UIEvent uiEvent;
  uiEvent.type = UI_EVENT_TEMP_UPDATE;
  uiEvent.data.temp.objectTemp = temps[0];
  uiEvent.data.temp.ambientTemp = temps[1];
  uiEvent.data.temp.cpuTemp = -127.0f; // Invalid/ignore
  UIController_PostEvent(&uiControllerInstance.base, &uiEvent);
}

void UIController_OnInternalTempUpdated(BusEvent event) {
  if (!event.data || event.dataSize < sizeof(float)) {
    return;
  }
  float cpuTemp = *(const float *)event.data;
  UIEvent uiEvent;
  uiEvent.type = UI_EVENT_TEMP_UPDATE;
  uiEvent.data.temp.objectTemp = -127.0f; // Invalid/ignore
  uiEvent.data.temp.ambientTemp = -127.0f; // Invalid/ignore
  uiEvent.data.temp.cpuTemp = cpuTemp;
  UIController_PostEvent(&uiControllerInstance.base, &uiEvent);
}
