#include "Core/EventBus/EventBusConfig.h"
#include <stddef.h>

static const EventBusConfig defaultEventBusConfig = {.maxSubscribers = 64,
                                                     .eventQueueLen = 32,
                                                     .taskStackSize = 4096,
                                                     .taskPriority = 5,
                                                     .taskName = "EB_Worker"};

const EventBusConfig *EventBusConfig_GetDefault(void) {
  return &defaultEventBusConfig;
}
