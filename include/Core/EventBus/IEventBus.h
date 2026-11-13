#ifndef IEVENTBUS_H
#define IEVENTBUS_H

#include "Core/EventBus/EventBusConfig.h"
#include "Core/IService.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
  EVENT_TYPE_INFO,
  EVENT_TYPE_WARNING,
  EVENT_TYPE_ERROR
} EventType;

/**
 * @brief Maximum size for inline event data storage.
 * Publishers may use this buffer as temporary source storage. Publish copies
 * flat payloads up to EVENT_BUS_PAYLOAD_CAPACITY and the event name into owned
 * queue storage. Callback data is borrowed only for the duration of dispatch.
 */
#define BUS_EVENT_INLINE_DATA_SIZE 16

typedef struct {
  const char *eventName;
  EventType type;
  void *data;
  uint32_t dataSize;
  uint8_t inlineData[BUS_EVENT_INLINE_DATA_SIZE];
} BusEvent;

typedef void (*BusEventHandler)(BusEvent event);

typedef struct IEventBus IEventBus;

/* Unsubscribe cannot release a handler captured by an in-flight dispatch.
 * Keep callback-owned resources alive and retry after that dispatch finishes. */
#define EVENT_BUS_ERR_BUSY (-2)

struct IEventBus {
  IService base;
  int32_t (*Subscribe)(IEventBus *self, const char *eventName,
                       BusEventHandler handler);
  int32_t (*Unsubscribe)(IEventBus *self, const char *eventName,
                         BusEventHandler handler);
  int32_t (*Publish)(BusEvent event);
  int32_t (*PublishFromISR)(BusEvent event, bool *higherPriorityTaskWoken);
  int32_t (*InitializeWithConfig)(IEventBus *self, const EventBusConfig *config,
                                  struct IDIRegistry *registry);
};

/**
 * @brief Get the global EventBus instance
 */
IEventBus *GetEventBus(void);

#endif // IEVENTBUS_H
