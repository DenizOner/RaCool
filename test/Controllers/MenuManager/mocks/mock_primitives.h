#ifndef MOCK_ESP_PRIMITIVES_H
#define MOCK_ESP_PRIMITIVES_H

#include <stdint.h>
#include <stdbool.h>

// Mock FreeRTOS
typedef void* QueueHandle_t;
typedef void* TaskHandle_t;
typedef void* SemaphoreHandle_t;
#define pdPASS (1)
#define pdFAIL (0)
#define portMAX_DELAY (0xFFFFFFFF)
#define pdMS_TO_TICKS(x) (x)

// Mock ESP Error
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1

#endif 
