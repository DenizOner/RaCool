#include "Communication/DebugConsole/Core/ConsoleEventPublisher.h"
#include "Communication/DebugConsole/DebugConsoleConfig.h"
#include <string.h>

#if DEBUG_CONSOLE_ENABLE_EVENTBUS
#include "Core/EventBus/IEventBus.h"
#include "Core/ServiceLocator/ServiceLocator.h"
#include "Core/ServiceLocator/ServiceNames.h"
#endif

/**
 * @brief EventBus instance'ini ServiceLocator uzerinden getirir.
 * EventBus devre disi ise veya bulunamazsa NULL doner.
 */
#if DEBUG_CONSOLE_ENABLE_EVENTBUS
static IEventBus *get_event_bus(void) {
    return (IEventBus *)ServiceLocator_GetService(SVC_NAME_EVENT_BUS);
}
#endif

void ConsoleEventPublisher_CommandExecuted(const char *cmd) {
#if DEBUG_CONSOLE_ENABLE_EVENTBUS
    if (!cmd) return;
    IEventBus *bus = get_event_bus();
    if (bus && bus->Publish) {
        BusEvent evt = {
            .eventName = CONSOLE_EVT_CMD_EXECUTED,
            .type      = EVENT_TYPE_INFO,
            .data      = (void *)cmd,
            .dataSize  = (uint32_t)(strlen(cmd) + 1)
        };
        bus->Publish(evt);
    }
#else
    (void)cmd;
#endif
}

void ConsoleEventPublisher_CommandFailed(const char *cmd, int error_code) {
#if DEBUG_CONSOLE_ENABLE_EVENTBUS
    if (!cmd) return;
    IEventBus *bus = get_event_bus();
    if (bus && bus->Publish) {
        BusEvent evt = {
            .eventName = CONSOLE_EVT_CMD_FAILED,
            .type      = EVENT_TYPE_WARNING,
            .data      = (void *)cmd,
            .dataSize  = (uint32_t)(strlen(cmd) + 1)
        };
        bus->Publish(evt);
    }
#else
    (void)cmd;
    (void)error_code;
#endif
}

void ConsoleEventPublisher_Started(void) {
#if DEBUG_CONSOLE_ENABLE_EVENTBUS
    IEventBus *bus = get_event_bus();
    if (bus && bus->Publish) {
        BusEvent evt = {
            .eventName = CONSOLE_EVT_STARTED,
            .type      = EVENT_TYPE_INFO,
            .data      = NULL,
            .dataSize  = 0
        };
        bus->Publish(evt);
    }
#endif
}

void ConsoleEventPublisher_Stopped(void) {
#if DEBUG_CONSOLE_ENABLE_EVENTBUS
    IEventBus *bus = get_event_bus();
    if (bus && bus->Publish) {
        BusEvent evt = {
            .eventName = CONSOLE_EVT_STOPPED,
            .type      = EVENT_TYPE_INFO,
            .data      = NULL,
            .dataSize  = 0
        };
        bus->Publish(evt);
    }
#endif
}
