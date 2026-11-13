#ifndef UI_EVENT_HANDLER_CONFIG_H
#define UI_EVENT_HANDLER_CONFIG_H

#include "Core/EventBus/IEventBus.h"

/**
 * @brief Callback handler triggered when system configuration changes.
 *
 * Wakes up the screen explicitly to reflect setting changes.
 *
 * @param event Event payload (unused).
 */
void UIController_OnConfigChanged(BusEvent event);

#endif /* UI_EVENT_HANDLER_CONFIG_H */
