#ifndef UI_EVENT_HANDLER_ZIGBEE_H
#define UI_EVENT_HANDLER_ZIGBEE_H

#include "Core/EventBus/IEventBus.h"

/**
 * @brief Callback handler triggered on Zigbee menu pairing/reset action.
 *
 * @param event Event payload (unused).
 */
void UIController_OnZigbeeMenuAction(BusEvent event);

#endif /* UI_EVENT_HANDLER_ZIGBEE_H */
