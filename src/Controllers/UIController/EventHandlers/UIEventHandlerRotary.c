#include "Hardware/RotaryEncoder/RotaryEncoderEvents.h"
#include <string.h>
#include "Controllers/UIController/EventHandlers/UIEventHandlerRotary.h"
#include "Controllers/UIController/UIControllerInternal.h"
#include "Hardware/RotaryEncoder/RotaryEncoderTypes.h"

void UIController_OnRotaryRotated(BusEvent event) {
  if (!event.data || event.dataSize < sizeof(RotaryEncoderRotationEvent)) {
    return;
  }
  const RotaryEncoderRotationEvent *rotEvent = (const RotaryEncoderRotationEvent *)event.data;
  UIEvent uiEvent;
  uiEvent.type = UI_EVENT_ENCODER_MOVE;
  uiEvent.data.encoder.difference = rotEvent->steps;
  UIController_PostEvent(&uiControllerInstance.base, &uiEvent);
}

void UIController_OnRotaryClicked(BusEvent event) {
  (void)event;
  UIEvent uiEvent;
  uiEvent.type = UI_EVENT_ENCODER_CLICK;
  UIController_PostEvent(&uiControllerInstance.base, &uiEvent);
}

void UIController_OnRotaryLongPressed(BusEvent event) {
  if (!event.data || event.dataSize != sizeof(RotaryEncoderButtonEvent)) return;
  const RotaryEncoderButtonEvent *button = event.data;
  if (button->duration_ms > INT32_MAX) return;
  UIEvent uiEvent = {.type = UI_EVENT_ENCODER_LONG_PRESS};
  uiEvent.data.encoder.difference = (int32_t)button->duration_ms;
  UIController_PostEvent(&uiControllerInstance.base, &uiEvent);
}

void UIController_OnRotaryHold(BusEvent event) {
  if (!event.data || event.dataSize != sizeof(RotaryEncoderButtonEvent)) return;
  const RotaryEncoderButtonEvent *button = event.data;
  if (button->duration_ms > INT32_MAX) return;
  UIEvent uiEvent = {.data.encoder.difference = (int32_t)button->duration_ms};
  if (strcmp(event.eventName, RE_EVENT_BUTTON_PRESSED) == 0) uiEvent.type = UI_EVENT_ENCODER_PRESS;
  else if (strcmp(event.eventName, RE_EVENT_BUTTON_HELD) == 0) uiEvent.type = UI_EVENT_ENCODER_HELD;
  else uiEvent.type = UI_EVENT_ENCODER_RELEASE;
  UIController_PostEvent(&uiControllerInstance.base, &uiEvent);
}
