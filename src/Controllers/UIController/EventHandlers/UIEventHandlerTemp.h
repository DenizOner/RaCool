#ifndef UI_EVENT_HANDLER_TEMP_H
#define UI_EVENT_HANDLER_TEMP_H

#include "Core/EventBus/IEventBus.h"

/**
 * @brief Callback handler triggered when infrared temperature is updated.
 *
 * @param event Event payload containing temperature array [objectTemp, ambientTemp].
 */
void UIController_OnIRTempUpdated(BusEvent event);

/**
 * @brief Callback handler triggered when internal CPU temperature is updated.
 *
 * @param event Event payload containing temperature float.
 */
void UIController_OnInternalTempUpdated(BusEvent event);

#endif /* UI_EVENT_HANDLER_TEMP_H */
