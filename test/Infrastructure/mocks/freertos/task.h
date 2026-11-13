#ifndef TASK_H
#define TASK_H

#include "FreeRTOS.h"

#define tskIDLE_PRIORITY 0

typedef enum { eNoAction, eSetBits, eIncrement, eSetValueWithOverwrite, eSetValueWithoutOverwrite } eNotifyAction;

typedef enum {
    eRunning = 0,
    eReady,
    eBlocked,
    eSuspended,
    eDeleted,
    eInvalid
} eTaskState;

BaseType_t xTaskCreate(TaskFunction_t pvTaskCode, const char * const pcName, uint32_t usStackDepth, void *pvParameters, uint32_t uxPriority, TaskHandle_t *pxCreatedTask);
void vTaskDelete(TaskHandle_t xTaskToDelete);
void vTaskSuspend(TaskHandle_t xTaskToSuspend);
void vTaskDelay(const TickType_t xTicksToDelay);
TickType_t xTaskGetTickCount(void);
TickType_t xTaskGetTickCountFromISR(void);
eTaskState eTaskGetState(TaskHandle_t xTask);

#endif /* TASK_H */
