#ifndef EVENT_SUBSCRIPTIONS_H
#define EVENT_SUBSCRIPTIONS_H

#include "Core/EventBus/IEventBus.h"
#include <stddef.h>
#include <stdint.h>

typedef struct {
    const char *name;
    BusEventHandler handler;
} EventSubscription;

typedef struct {
    IEventBus *bus;
    uint32_t owned;
    const EventSubscription *table;
    size_t count;
} EventSubscriptions;

/* Caller serializes lifecycle calls and keeps table/callback resources alive.
 * Partial rollback retains BUSY subscriptions for retry; do not clear this
 * ownership record or switch buses until Detach succeeds. */
int32_t EventSubscriptions_Attach(EventSubscriptions *state, IEventBus *bus,
    const EventSubscription *table, size_t count);
int32_t EventSubscriptions_Detach(EventSubscriptions *state,
    const EventSubscription *table, size_t count);

#endif
