#ifndef TEST_FREERTOS_TIMERS_H
#define TEST_FREERTOS_TIMERS_H
#include "FreeRTOS.h"
typedef void *TimerHandle_t;
typedef void (*TimerCallbackFunction_t)(TimerHandle_t);
TimerHandle_t xTimerCreate(const char *, TickType_t, BaseType_t, void *, TimerCallbackFunction_t);
BaseType_t xTimerStart(TimerHandle_t, TickType_t);
BaseType_t xTimerStop(TimerHandle_t, TickType_t);
#endif
