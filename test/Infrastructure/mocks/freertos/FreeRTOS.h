#ifndef FREERTOS_H
#define FREERTOS_H

#include <stddef.h>
#include <stdint.h>

typedef void *TaskHandle_t;
typedef uint32_t TickType_t;
typedef uint32_t BaseType_t;
typedef void (*TaskFunction_t)(void *);

#define pdMS_TO_TICKS(ms) (ms)
#define pdTRUE 1
#define pdFALSE 0
#define pdPASS 1
#define pdFAIL 0

#define portMAX_DELAY (TickType_t)0xffffffff
#define portTICK_PERIOD_MS 1
#define configMAX_PRIORITIES 25
#define configTICK_RATE_HZ 1000

#define portYIELD_FROM_ISR()
#define portENTER_CRITICAL(m)
#define portEXIT_CRITICAL(m)
#define portENTER_CRITICAL_ISR(m)
#define portEXIT_CRITICAL_ISR(m)
#define portENTER_CRITICAL_FROM_ISR(m) 0
#define portEXIT_CRITICAL_FROM_ISR(m, x) (void)x
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
typedef uint32_t UBaseType_t;

#endif
