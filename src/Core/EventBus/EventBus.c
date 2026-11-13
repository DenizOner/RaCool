#include "Core/DI/IDIRegistry.h"
#include "Core/EventBus/EventBusConfig.h"
#include "Core/EventBus/IEventBus.h"
#include "Core/IService.h"
#include "Management/Watchdog/WatchdogWorker.h"
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <stdbool.h>
#include <stdalign.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"

static const char *TAG = "EventBus";

static bool EventBus_ShouldIgnoreMissingSubscriber(const char *eventName) {
  if (!eventName) {
    return true;
  }
  return strcmp(eventName, "RE_Rotated") == 0 ||
         strcmp(eventName, "menu.state.changed") == 0 ||
         strcmp(eventName, "menu.activity") == 0 ||
         strcmp(eventName, "CMD_EXECUTED") == 0 ||
         strcmp(eventName, "diag.selftest.started") == 0 ||
         strcmp(eventName, "diag.selftest.step") == 0 ||
         strcmp(eventName, "diag.selftest.finished") == 0 ||
         strcmp(eventName, "diag.health.critical") == 0;
}

typedef struct {
  char *eventName;
  BusEventHandler handler;
  uint32_t dispatchReservations;
} Subscriber;

typedef struct {
  IEventBus base;
  _Atomic(ServiceState) state;
  atomic_uint generation;
  Subscriber *subscribers;
  uint32_t subscriberCount;
  uint32_t maxSubscribers;
  QueueHandle_t eventQueue;
  SemaphoreHandle_t busMutex;
  TaskHandle_t workerTask;
  WatchdogWorker worker;
  SemaphoreHandle_t lifecycleMutex;
  EventBusConfig ownedConfig;
  char taskName[EVENT_BUS_NAME_CAPACITY];
  const EventBusConfig *config;
} EventBusImpl;

typedef struct {
  BusEvent event;
  unsigned generation;
  char name[EVENT_BUS_NAME_CAPACITY];
  alignas(max_align_t) uint8_t payload[EVENT_BUS_PAYLOAD_CAPACITY];
} QueuedEvent;

static EventBusImpl busInstance = {0};

/**
 * @brief Worker task that processes events from the queue
 * @param pvParameters Pointer to task parameters (unused)
 */
/**
 * @brief Snapshot buffer for mutex-free handler dispatch.
 * Handlers may call Subscribe/Publish, so we must NOT hold
 * busMutex while invoking them.
 */
static BusEventHandler dispatchSnapshot[64];
static int32_t findSubscriberIndex(const char *eventName, BusEventHandler handler);

static void EventBus_Dispatch(BusEvent *event) {
  if (!event || !event->eventName || !busInstance.busMutex) return;
      ESP_LOGD(TAG, "Worker dequeued: '%s', subs=%lu", event->eventName ? event->eventName : "NULL", (unsigned long)busInstance.subscriberCount);

      /* Take a snapshot of matching handlers under mutex */
      uint32_t snapshotCount = 0;
      if (xSemaphoreTake(busInstance.busMutex, portMAX_DELAY) != pdTRUE) return;
      for (uint32_t i = 0; i < busInstance.subscriberCount && snapshotCount < 64; i++) {
        if (strcmp(busInstance.subscribers[i].eventName, event->eventName) == 0) {
          dispatchSnapshot[snapshotCount++] = busInstance.subscribers[i].handler;
          busInstance.subscribers[i].dispatchReservations++;
        }
      }
      xSemaphoreGive(busInstance.busMutex);

      /* Dispatch WITHOUT holding mutex — prevents deadlock */
      for (uint32_t i = 0; i < snapshotCount; i++) {
        ESP_LOGD(TAG, "Dispatching '%s' to handler[%lu]", event->eventName, (unsigned long)i);
        dispatchSnapshot[i](*event);
        /* A reservation includes queued snapshot callbacks, not just the current
         * invocation. Removal must not authorize freeing their owner early. */
        if (xSemaphoreTake(busInstance.busMutex, portMAX_DELAY) == pdTRUE) {
          int32_t index = findSubscriberIndex(event->eventName, dispatchSnapshot[i]);
          if (index >= 0 && busInstance.subscribers[index].dispatchReservations)
            busInstance.subscribers[index].dispatchReservations--;
          xSemaphoreGive(busInstance.busMutex);
        }
      }

      if (snapshotCount == 0 && !EventBus_ShouldIgnoreMissingSubscriber(event->eventName)) {
        ESP_LOGD(TAG, "No subscriber matched for '%s'", event->eventName);
      }
}

static void EventBus_DispatchQueued(QueuedEvent *queued) {
  if (busInstance.state != SERVICE_STATE_STARTED || queued->generation != atomic_load(&busInstance.generation)) return;
  queued->event.eventName = queued->name;
  queued->event.data = queued->event.dataSize ? queued->payload : NULL;
  EventBus_Dispatch(&queued->event);
}

static void EventBus_WorkerTask(void *pvParameters) {
  (void)pvParameters;
  QueuedEvent queued;
  while (1) {
    WatchdogWorker_Checkpoint(&busInstance.worker, NULL, 0);
    if (xQueueReceive(busInstance.eventQueue, &queued, pdMS_TO_TICKS(EVENT_BUS_WORKER_WAIT_MS)) == pdPASS)
      EventBus_DispatchQueued(&queued);
  }
}

/**
 * @brief Find subscriber index by event name and handler
 * @param eventName Name of the event
 * @param handler Callback handler function
 * @return Index of subscriber or -1 if not found
 */
static int32_t findSubscriberIndex(const char *eventName,
                                   BusEventHandler handler) {
  for (uint32_t i = 0; i < busInstance.subscriberCount; i++) {
    if (strcmp(busInstance.subscribers[i].eventName, eventName) == 0 &&
        busInstance.subscribers[i].handler == handler) {
      return i;
    }
  }
  return -1;
}

/**
 * @brief Subscribe to an event
 * @param self Pointer to EventBus instance (unused)
 * @param eventName Name of the event to subscribe to
 * @param handler Callback handler for the event
 * @return 0 on success, -1 on failure (max subscribers or already subscribed)
 */
static int32_t EventBus_Subscribe(IEventBus *self, const char *eventName,
                                  BusEventHandler handler) {
  if (!self || !eventName || !handler || !busInstance.busMutex || !busInstance.subscribers) {
    return -1;
  }
  if (xSemaphoreTake(busInstance.busMutex, portMAX_DELAY) != pdTRUE) return -1;

  // Duplicate check: already subscribed?
  if (findSubscriberIndex(eventName, handler) != -1) {
    ESP_LOGD(TAG, "Already subscribed to '%s'", eventName);
    xSemaphoreGive(busInstance.busMutex);
    return 0;
  }

  // Preserve existing safety-critical subscriptions when capacity is exhausted.
  if (busInstance.subscriberCount >= busInstance.maxSubscribers) {
    xSemaphoreGive(busInstance.busMutex);
    return -1;
  }

  busInstance.subscribers[busInstance.subscriberCount].eventName =
      strdup(eventName);
  if (!busInstance.subscribers[busInstance.subscriberCount].eventName) {
    xSemaphoreGive(busInstance.busMutex);
    return -1;
  }
  busInstance.subscribers[busInstance.subscriberCount].handler = handler;
  busInstance.subscribers[busInstance.subscriberCount].dispatchReservations = 0;
  ESP_LOGD(TAG, "Subscribe OK: '%s' -> sub[%lu] handler=%p",
           eventName, (unsigned long)busInstance.subscriberCount, (void*)handler);
  busInstance.subscriberCount++;
  xSemaphoreGive(busInstance.busMutex);
  return 0;
}

/**
 * @brief Unsubscribe from an event
 * @param self Pointer to EventBus instance (unused)
 * @param eventName Name of the event to unsubscribe from
 * @param handler Callback handler to remove
 * @return 0 on success, EVENT_BUS_ERR_BUSY while dispatch owns the callback, -1 on failure
 */
static int32_t EventBus_Unsubscribe(IEventBus *self, const char *eventName,
                                    BusEventHandler handler) {
  if (!self || !eventName || !handler || !busInstance.busMutex ||
      xSemaphoreTake(busInstance.busMutex, portMAX_DELAY) != pdTRUE) return -1;
  int32_t idx = findSubscriberIndex(eventName, handler);
  if (idx == -1) {
    xSemaphoreGive(busInstance.busMutex);
    return -1;
  }

  if (busInstance.subscribers[idx].dispatchReservations) {
    xSemaphoreGive(busInstance.busMutex);
    return EVENT_BUS_ERR_BUSY;
  }
  free(busInstance.subscribers[idx].eventName);
  for (uint32_t i = idx; i < busInstance.subscriberCount - 1; i++) {
    busInstance.subscribers[i] = busInstance.subscribers[i + 1];
  }
  busInstance.subscriberCount--;
  xSemaphoreGive(busInstance.busMutex);
  return 0;
}

/**
 * @brief Publish an event to the bus
 * @param event Event to publish
 * @return 0 on success, -1 if bus not started or queue full
 */
/* Both publication paths own complete flat payload bytes and the event name.
 * Nested pointers are not deep-copied; handlers may only borrow data during dispatch. */
static bool EventBus_PrepareQueued(const BusEvent *event, QueuedEvent *queued) {
  if (!event->eventName || event->type < EVENT_TYPE_INFO || event->type > EVENT_TYPE_ERROR ||
      event->dataSize > EVENT_BUS_PAYLOAD_CAPACITY || (event->dataSize && !event->data)) return false;
  size_t length = strnlen(event->eventName, EVENT_BUS_NAME_CAPACITY);
  if (length == 0 || length == EVENT_BUS_NAME_CAPACITY) return false;
  memset(queued, 0, sizeof(*queued));
  queued->event.type = event->type;
  queued->event.dataSize = event->dataSize;
  memcpy(queued->name, event->eventName, length + 1);
  if (event->dataSize) memcpy(queued->payload, event->data, event->dataSize);
  return true;
}

static int32_t EventBus_PublishEvent(BusEvent event) {
  unsigned generation = atomic_load(&busInstance.generation);
  if (busInstance.state != SERVICE_STATE_STARTED) return -1;
  QueuedEvent queued;
  if (!EventBus_PrepareQueued(&event, &queued)) return -1;
  queued.generation = generation;
  return xQueueSend(busInstance.eventQueue, &queued, 0) == pdPASS ? 0 : -1;
}

static int32_t EventBus_PublishEventFromISR(BusEvent event, bool *higherPriorityTaskWoken) {
  if (higherPriorityTaskWoken) *higherPriorityTaskWoken = false;
  unsigned generation = atomic_load(&busInstance.generation);
  if (busInstance.state != SERVICE_STATE_STARTED) return -1;
  QueuedEvent queued;
  if (!EventBus_PrepareQueued(&event, &queued)) return -1;
  queued.generation = generation;
  BaseType_t woken = pdFALSE;
  if (xQueueSendFromISR(busInstance.eventQueue, &queued, &woken) != pdPASS) return -1;
  if (higherPriorityTaskWoken) *higherPriorityTaskWoken = woken == pdTRUE;
  return 0;
}

/**
 * @brief Initialize the event bus with explicit configuration
 * @param self Pointer to IEventBus instance
 * @param config Pointer to configuration struct
 * @return 0 on success, -1 on allocation failure
 */
static int32_t EventBus_InitializeWithConfig(IEventBus *self,
                                             const EventBusConfig *config,
                                             IDIRegistry *registry) {
  if (busInstance.state == SERVICE_STATE_INITIALIZED || busInstance.state == SERVICE_STATE_STARTED || busInstance.state == SERVICE_STATE_STOPPED)
    return 0;

  if (config == NULL) {
    config = EventBusConfig_GetDefault();
  }
  if (!self || config->maxSubscribers == 0 || config->maxSubscribers > 64 ||
      config->eventQueueLen == 0 || !config->taskName || !config->taskStackSize ||
      strnlen(config->taskName, EVENT_BUS_NAME_CAPACITY) >= EVENT_BUS_NAME_CAPACITY) return -1;
  busInstance.ownedConfig = *config;
  strcpy(busInstance.taskName, config->taskName);
  busInstance.ownedConfig.taskName = busInstance.taskName;
  busInstance.config = &busInstance.ownedConfig;
  busInstance.maxSubscribers = config->maxSubscribers;

  busInstance.subscribers =
      calloc(busInstance.maxSubscribers, sizeof(Subscriber));
  if (busInstance.subscribers == NULL) {
    return -1;
  }

  busInstance.eventQueue =
      xQueueCreate(config->eventQueueLen, sizeof(QueuedEvent));
  busInstance.busMutex = xSemaphoreCreateMutex();
  busInstance.subscriberCount = 0;

  if (busInstance.eventQueue == NULL || busInstance.busMutex == NULL) {
    free(busInstance.subscribers);
    busInstance.subscribers = NULL;
    if (busInstance.eventQueue) vQueueDelete(busInstance.eventQueue);
    if (busInstance.busMutex) vSemaphoreDelete(busInstance.busMutex);
    busInstance.eventQueue = NULL;
    busInstance.busMutex = NULL;
    return -1;
  }

  busInstance.lifecycleMutex = xSemaphoreCreateMutex();
  if (!busInstance.lifecycleMutex || WatchdogWorker_Init(&busInstance.worker) != 0) {
    if (busInstance.lifecycleMutex) vSemaphoreDelete(busInstance.lifecycleMutex);
    busInstance.lifecycleMutex = NULL;
    vSemaphoreDelete(busInstance.busMutex); busInstance.busMutex = NULL;
    vQueueDelete(busInstance.eventQueue); busInstance.eventQueue = NULL;
    free(busInstance.subscribers); busInstance.subscribers = NULL;
    return -1;
  }
  busInstance.state = SERVICE_STATE_INITIALIZED;
  return 0;
}

/**
 * @brief Initialize the event bus (default config)
 * @param self Pointer to IService instance
 * @return 0 on success, -1 on allocation failure
 */
static int32_t EventBus_Initialize(IService *self, IDIRegistry *registry) {
  if (busInstance.state == SERVICE_STATE_INITIALIZED || busInstance.state == SERVICE_STATE_STARTED || busInstance.state == SERVICE_STATE_STOPPED) return 0;
  return EventBus_InitializeWithConfig((IEventBus *)self, NULL, registry);
}

/**
 * @brief Start the event bus (creates worker task)
 * @param self Pointer to IService instance
 * @return 0 on success, -1 if not initialized or task creation failed
 */
/* Lifecycle calls serialize separately from subscriptions. No worker callback
 * holds this gate, and no resources accessible to publishers/ISR are freed. */
static int32_t EventBus_Start(IService *self) {
  if (!self || !busInstance.lifecycleMutex ||
      xSemaphoreTake(busInstance.lifecycleMutex, pdMS_TO_TICKS(EVENT_BUS_STOP_TIMEOUT_MS)) != pdTRUE) return -1;
  int32_t result = -1;
  if (busInstance.state == SERVICE_STATE_STARTED && !atomic_load(&busInstance.worker.stopRequested)) result = 0;
  else if (busInstance.state == SERVICE_STATE_INITIALIZED || busInstance.state == SERVICE_STATE_STOPPED) {
    result = WatchdogWorker_Start(&busInstance.worker, EventBus_WorkerTask,
      busInstance.config->taskName, busInstance.config->taskStackSize, NULL, busInstance.config->taskPriority);
    if (result == 0) {
      busInstance.workerTask = atomic_load(&busInstance.worker.task);
      busInstance.state = SERVICE_STATE_STARTED;
    }
  }
  xSemaphoreGive(busInstance.lifecycleMutex);
  return result;
}

static int32_t EventBus_Stop(IService *self) {
  if (!self || !busInstance.lifecycleMutex ||
      (atomic_load(&busInstance.worker.task) && atomic_load(&busInstance.worker.task) == xTaskGetCurrentTaskHandle())) return -1;
  if (xSemaphoreTake(busInstance.lifecycleMutex, pdMS_TO_TICKS(EVENT_BUS_STOP_TIMEOUT_MS)) != pdTRUE) return -1;
  if (busInstance.state == SERVICE_STATE_STARTED) {
    busInstance.state = SERVICE_STATE_STOPPED;
    atomic_fetch_add(&busInstance.generation, 1U);
  }
  int32_t result = WatchdogWorker_Stop(&busInstance.worker, pdMS_TO_TICKS(EVENT_BUS_STOP_TIMEOUT_MS));
  if (result == 0) busInstance.workerTask = NULL;
  xSemaphoreGive(busInstance.lifecycleMutex);
  return result;
}

/**
 * @brief Get the current service state
 * @param self Pointer to IService instance
 * @return Current service state
 */
static ServiceState EventBus_GetState(IService *self) {
  return busInstance.state;
}

/**
 * @brief Check the health of the event bus
 * @param self Pointer to IService instance
 * @return 0 if healthy (started), -1 otherwise
 */
static int32_t EventBus_CheckHealth(IService *self) {
  return (busInstance.state == SERVICE_STATE_STARTED && WatchdogWorker_Healthy(&busInstance.worker)) ? 0 : -1;
}

/**
 * @brief Get the global EventBus instance
 * @return Pointer to EventBus instance
 */
IEventBus *GetEventBus(void) {
  static bool initialized = false;
  if (!initialized) {
    busInstance.base.base.Initialize = EventBus_Initialize;
    busInstance.base.base.Start = EventBus_Start;
    busInstance.base.base.Stop = EventBus_Stop;
    busInstance.base.base.GetState = EventBus_GetState;
    busInstance.base.base.CheckHealth = EventBus_CheckHealth;
    busInstance.base.Subscribe = EventBus_Subscribe;
    busInstance.base.Unsubscribe = EventBus_Unsubscribe;
    busInstance.base.Publish = EventBus_PublishEvent;
    busInstance.base.PublishFromISR = EventBus_PublishEventFromISR;
    busInstance.base.InitializeWithConfig = EventBus_InitializeWithConfig;
    busInstance.state = SERVICE_STATE_UNINITIALIZED;
    initialized = true;
  }
  return &busInstance.base;
}

/**
 * @brief Publish an event by name
 * @param eventName Name of the event
 * @param data Pointer to event data
 * @param dataSize Size of event data
 * @return Result of EventBus_PublishEvent
 */
int32_t EventBus_Publish(const char *eventName, void *data, uint32_t dataSize) {
  BusEvent event = {.eventName = eventName,
                    .type = EVENT_TYPE_INFO,
                    .data = data,
                    .dataSize = dataSize};
  return EventBus_PublishEvent(event);
}
