#ifndef UI_EVENT_HANDLER_RELAY_H
#define UI_EVENT_HANDLER_RELAY_H

#include "Core/EventBus/IEventBus.h"

/**
 * @brief Callback handler triggered when relay state is changed.
 *
 * @param event Event payload containing the new relay state (bool).
 */
void UIController_OnRelayStateChanged(BusEvent event);

#endif /* UI_EVENT_HANDLER_RELAY_H */
