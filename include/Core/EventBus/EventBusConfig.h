#ifndef EVENT_BUS_CONFIG_H
#define EVENT_BUS_CONFIG_H

#include <stdint.h>

/* Fixed queue storage bounds; include the terminating NUL in name capacity. */
#define EVENT_BUS_NAME_CAPACITY 64U
#define EVENT_BUS_PAYLOAD_CAPACITY 256U
#define EVENT_BUS_WORKER_WAIT_MS 100U
#define EVENT_BUS_STOP_TIMEOUT_MS 2000U


typedef struct {
  uint32_t maxSubscribers;
  uint32_t eventQueueLen;
  uint32_t taskStackSize;
  uint32_t taskPriority;
  const char *taskName;
} EventBusConfig;

/**
 * @brief Get the default configuration for the EventBus
 * @return Constant pointer to default configuration
 */
const EventBusConfig *EventBusConfig_GetDefault(void);

#endif // EVENT_BUS_CONFIG_H
