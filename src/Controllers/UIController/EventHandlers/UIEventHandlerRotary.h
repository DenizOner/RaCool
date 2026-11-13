#ifndef UI_EVENT_HANDLER_ROTARY_H
#define UI_EVENT_HANDLER_ROTARY_H

#include "Core/EventBus/IEventBus.h"

/**
 * @brief Callback handler triggered when rotary encoder rotates.
 *
 * @param event Event payload containing step difference.
 */
void UIController_OnRotaryRotated(BusEvent event);

/**
 * @brief Callback handler triggered when rotary encoder is clicked.
 *
 * @param event Event payload (unused).
 */
void UIController_OnRotaryClicked(BusEvent event);
void UIController_OnRotaryLongPressed(BusEvent event);
void UIController_OnRotaryHold(BusEvent event);

#endif /* UI_EVENT_HANDLER_ROTARY_H */
