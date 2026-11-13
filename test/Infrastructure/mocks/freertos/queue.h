#ifndef QUEUE_H
#define QUEUE_H

#include "FreeRTOS.h"

typedef void* QueueHandle_t;

QueueHandle_t xQueueCreate(uint32_t uxQueueLength, uint32_t uxItemSize);
BaseType_t xQueueSend(QueueHandle_t xQueue, const void * pvItemToQueue, TickType_t xTicksToWait);
BaseType_t xQueueReceive(QueueHandle_t xQueue, void * pvBuffer, TickType_t xTicksToWait);
uint32_t uxQueueMessagesWaiting(QueueHandle_t xQueue);
void vQueueDelete(QueueHandle_t xQueue);
BaseType_t xQueueSendFromISR(QueueHandle_t xQueue, const void * pvItemToQueue, BaseType_t * pxHigherPriorityTaskWoken);

#endif /* QUEUE_H */
