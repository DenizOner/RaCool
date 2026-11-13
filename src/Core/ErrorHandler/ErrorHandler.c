#include "Core/ErrorHandler/ErrorHandlerInput.h"
#include "Core/ErrorHandler/ErrorHandlerConsoleCommands.h"
#include "Management/Watchdog/WatchdogWorker.h"
#include "esp_timer.h"
#include "Core/DI/IDIRegistry.h"
#include "Core/ErrorHandler/IErrorHandler.h"
#include "Core/ErrorHandler/ErrorHandlerConfig.h"
#include "Core/ServiceLocator/ServiceLocator.h"
#include "Core/ServiceLocator/ServiceNames.h"
#include "Core/EventBus/IEventBus.h"
#include "Communication/DebugConsole/IDebugConsole.h"
#include "esp_log.h"
#include <esp_rom_sys.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define MAX_CALLBACKS 4

/**
 * @brief Implementation structure of the ErrorHandler service.
 */
typedef struct {
    IErrorHandler base;
    _Atomic(ServiceState) state;
    _Atomic(int32_t) lastErrorCode;
    WatchdogWorker worker;
    SemaphoreHandle_t controlMutex;

    SemaphoreHandle_t stateMutex;
    QueueHandle_t errorQueue;
    _Atomic(TaskHandle_t) queueProcessorTask;

    ErrorRecord *historyBuffer;
    uint32_t historyCount;
    uint32_t historyWriteIdx;
    uint32_t totalErrorCount;

    ErrorHandlerCallback callbacks[MAX_CALLBACKS];
    uint32_t callbackCount;
} ErrorHandlerImpl;

static ErrorHandlerImpl errorHandlerInstance;
static atomic_bool selfTestActive, selfTestCompleted, selfTestCall;

/**
 * @brief Background task to process errors from the queue asynchronously.
 *
 * Args:
 *     pvParameters (void*): Unused task parameters.
 */
static void EH_QueueProcessorTask(void *pvParameters) {
    (void)pvParameters;
    ErrorRecord record = {0};
    const ErrorHandlerConfig *config = ErrorHandlerConfig_GetConfig();

    while (1) {
        WatchdogWorker_Checkpoint(&errorHandlerInstance.worker,NULL,(uint64_t)esp_timer_get_time()/1000ULL);
        TickType_t wait=pdMS_TO_TICKS(ERROR_HANDLER_POLL_MS);
        if (xQueueReceive(errorHandlerInstance.errorQueue, &record, wait ? wait : 1) == pdPASS) {
            if (errorHandlerInstance.state != SERVICE_STATE_STARTED) continue;
            ESP_LOGE("ErrorHandler", "Async error in %s: %d", record.tag, (int)record.errorCode);

            // Access shared state under mutex lock
            if (xSemaphoreTake(errorHandlerInstance.stateMutex, portMAX_DELAY) == pdPASS) {
                errorHandlerInstance.lastErrorCode = record.errorCode;

                if (errorHandlerInstance.historyBuffer != NULL) {
                    errorHandlerInstance.historyBuffer[errorHandlerInstance.historyWriteIdx] = record;
                    errorHandlerInstance.historyWriteIdx = (errorHandlerInstance.historyWriteIdx + 1) % config->historySize;
                    if (errorHandlerInstance.historyCount < config->historySize) {
                        errorHandlerInstance.historyCount++;
                    }
                }
                errorHandlerInstance.totalErrorCount++;

                // Snapshot callbacks to invoke them outside of critical section to avoid deadlocks
                uint32_t cbCount = errorHandlerInstance.callbackCount;
                ErrorHandlerCallback cbSnapshot[MAX_CALLBACKS];
                memcpy(cbSnapshot, errorHandlerInstance.callbacks, sizeof(errorHandlerInstance.callbacks));

                xSemaphoreGive(errorHandlerInstance.stateMutex);

                // Execute registered callbacks
                for (uint32_t i = 0; i < cbCount; i++) {
                    if (cbSnapshot[i] != NULL) {
                        cbSnapshot[i](record.tag, record.errorCode);
                    }
                }
            }

            // Publish the error to the EventBus
            IEventBus *eventBus = ServiceLocator_GetEventBus();
            if (eventBus != NULL && eventBus->Publish) {
                BusEvent event = {
                    .eventName = "error_logged",
                    .type = EVENT_TYPE_ERROR,
                    .dataSize = sizeof(ErrorRecord)
                };
                _Static_assert(sizeof(ErrorRecord) <= EVENT_BUS_PAYLOAD_CAPACITY, "Error record exceeds queue payload capacity");
                event.data = &record;
                eventBus->Publish(event);
            }
            if (record.isSelfTest) atomic_store(&selfTestCompleted, true);
        }
    }
}

/**
 * @brief Handle an error asynchronously by sending it to the processing queue.
 *
 * Args:
 *     tag (const char*): Source tag/identifier.
 *     errorCode (int32_t): Numeric error code.
 *
 * Returns:
 *     int32_t: 0 on success, negative error code on failure.
 */
static int32_t EH_HandleErrorAsync(const char *tag, int32_t errorCode) {
    if (!ErrorHandler_IsTagValid(tag)) {
        return -1;
    }

    ErrorRecord record = {0};
    strncpy(record.tag, tag, sizeof(record.tag) - 1);
    record.tag[sizeof(record.tag) - 1] = '\0';
    record.errorCode = errorCode;
    record.timestamp = xTaskGetTickCount();
    record.isAsync = true;

    if (errorHandlerInstance.state != SERVICE_STATE_STARTED || errorHandlerInstance.errorQueue == NULL) {
        ESP_LOGE("ErrorHandler", "Fallback sync error in %s: %d", record.tag, (int)errorCode);
        
        atomic_store(&errorHandlerInstance.lastErrorCode,errorCode);
        return -2;
    }

    // Try posting to queue without blocking
    if (xQueueSend(errorHandlerInstance.errorQueue, &record, 0) != pdPASS) {
        ESP_LOGW("ErrorHandler", "Queue full! Dropping async error in %s: %d", record.tag, (int)errorCode);
        return -3;
    }

    return 0;
}

/**
 * @brief Handle an error from Interrupt Service Routine (ISR) context.
 *
 * Args:
 *     tag (const char*): Source tag/identifier.
 *     errorCode (int32_t): Numeric error code.
 *     pxHigherPriorityTaskWoken (BaseType_t*): Pointer to context switch flag.
 *
 * Returns:
 *     int32_t: 0 on success, negative error code on failure.
 */
static int32_t EH_HandleErrorFromISR(const char *tag, int32_t errorCode, BaseType_t *pxHigherPriorityTaskWoken) {
    if (!ErrorHandler_IsTagValid(tag)) {
        return -1;
    }

    ErrorRecord record = {0};
    size_t i;
    for (i = 0; i < sizeof(record.tag) - 1 && tag[i] != '\0'; i++) {
        record.tag[i] = tag[i];
    }
    record.tag[i] = '\0';
    record.errorCode = errorCode;
    record.timestamp = xTaskGetTickCountFromISR();
    record.isAsync = true;

    if (errorHandlerInstance.state != SERVICE_STATE_STARTED || errorHandlerInstance.errorQueue == NULL) {
        errorHandlerInstance.lastErrorCode = errorCode;
        return -2;
    }

    if (xQueueSendFromISR(errorHandlerInstance.errorQueue, &record, pxHigherPriorityTaskWoken) != pdPASS) {
        return -3;
    }

    return 0;
}

/**
 * @brief Legacy synchronous error handling wrapper.
 *
 * Args:
 *     tag (const char*): Source tag/identifier.
 *     errorCode (int32_t): Numeric error code.
 */
static void EH_HandleError(const char *tag, int32_t errorCode) {
    (void)EH_HandleErrorAsync(tag, errorCode);
}

/**
 * @brief Retrieve the last recorded error code.
 *
 * Returns:
 *     int32_t: Last error code.
 */
static int32_t EH_GetLastErrorCode(void) {
    return atomic_load(&errorHandlerInstance.lastErrorCode);
}

static int32_t EH_GetStatistics(ErrorHandlerStatistics *output) {
    if (!output) return -1;
    if (!errorHandlerInstance.stateMutex ||
        xSemaphoreTake(errorHandlerInstance.stateMutex,pdMS_TO_TICKS(ERROR_HANDLER_CONTROL_MS)) != pdPASS) return -2;
    ErrorHandlerStatistics snapshot = {
        .state=atomic_load(&errorHandlerInstance.state),
        .lastErrorCode=atomic_load(&errorHandlerInstance.lastErrorCode),
        .totalErrorCount=errorHandlerInstance.totalErrorCount,
        .callbackCount=errorHandlerInstance.callbackCount,
    };
    xSemaphoreGive(errorHandlerInstance.stateMutex);
    *output=snapshot;
    return 0;
}

/**
 * @brief Register a callback for error events.
 *
 * Args:
 *     callback (ErrorHandlerCallback): Callback function pointer.
 *
 * Returns:
 *     int32_t: 0 on success, negative error code on failure.
 */
static int32_t EH_RegisterCallback(ErrorHandlerCallback callback) {
    if (callback == NULL || !errorHandlerInstance.stateMutex) {
        return -1;
    }

    int32_t result = -2;
    if (xSemaphoreTake(errorHandlerInstance.stateMutex, portMAX_DELAY) == pdPASS) {
        if (errorHandlerInstance.callbackCount < MAX_CALLBACKS) {
            errorHandlerInstance.callbacks[errorHandlerInstance.callbackCount++] = callback;
            result = 0;
        }
        xSemaphoreGive(errorHandlerInstance.stateMutex);
    }
    return result;
}

/**
 * @brief Retrieve historical error records in reverse chronological order (newest first).
 *
 * Args:
 *     outRecords (ErrorRecord*): Buffer to receive records.
 *     maxRecords (uint32_t): Size of output buffer.
 *     outCount (uint32_t*): Actual number of retrieved records.
 *
 * Returns:
 *     int32_t: 0 on success, negative error code on failure.
 */
static int32_t EH_GetErrorHistory(ErrorRecord *outRecords, uint32_t maxRecords, uint32_t *outCount) {
    if (outRecords == NULL || outCount == NULL || maxRecords == 0 || !errorHandlerInstance.stateMutex || !errorHandlerInstance.historyBuffer) {
        return -1;
    }

    const ErrorHandlerConfig *config = ErrorHandlerConfig_GetConfig();
    int32_t result = -2;

    if (xSemaphoreTake(errorHandlerInstance.stateMutex, portMAX_DELAY) == pdPASS) {
        uint32_t countToCopy = (errorHandlerInstance.historyCount < maxRecords) ? errorHandlerInstance.historyCount : maxRecords;
        for (uint32_t i = 0; i < countToCopy; i++) {
            uint32_t idx = (errorHandlerInstance.historyWriteIdx - 1 - i + config->historySize) % config->historySize;
            outRecords[i] = errorHandlerInstance.historyBuffer[idx];
        }
        *outCount = countToCopy;
        result = 0;
        xSemaphoreGive(errorHandlerInstance.stateMutex);
    }
    return result;
}

/**
 * @brief Reset stats, error codes and clear history buffer.
 */
static int32_t EH_ClearHistory(void) {
    if (!errorHandlerInstance.stateMutex) return -1;
    if (xSemaphoreTake(errorHandlerInstance.stateMutex,pdMS_TO_TICKS(ERROR_HANDLER_CONTROL_MS)) != pdPASS) return -2;
    errorHandlerInstance.historyCount = 0;
    errorHandlerInstance.historyWriteIdx = 0;
    errorHandlerInstance.totalErrorCount = 0;
    errorHandlerInstance.lastErrorCode = 0;
    xSemaphoreGive(errorHandlerInstance.stateMutex);
    return 0;
}

/* A self-test owns a marked queue record, never a temporary registry callback.
 * A timed-out record may finish later; the next test is rejected until it does. */
static int32_t EH_RunSelfTest(void) {
    if (errorHandlerInstance.state != SERVICE_STATE_STARTED ||
        errorHandlerInstance.queueProcessorTask == xTaskGetCurrentTaskHandle()) return -1;
    bool expected = false;
    if (!atomic_compare_exchange_strong(&selfTestActive, &expected, true)) {
        if (!atomic_load(&selfTestCompleted)) return -2;
        atomic_store(&selfTestActive, false);
        return -2;
    }
    atomic_store(&selfTestCompleted, false);
    ErrorRecord record = {.errorCode = 9999, .timestamp = xTaskGetTickCount(), .isAsync = true, .isSelfTest = true};
    strcpy(record.tag, "SelfTestTag");
    if (errorHandlerInstance.state != SERVICE_STATE_STARTED ||
        xQueueSend(errorHandlerInstance.errorQueue, &record, 0) != pdTRUE) {
        atomic_store(&selfTestActive, false); return -3;
    }
    TickType_t start = xTaskGetTickCount();
    TickType_t timeout = pdMS_TO_TICKS(ERROR_HANDLER_SELFTEST_MS);
    while (!atomic_load(&selfTestCompleted)) {
        if (errorHandlerInstance.state != SERVICE_STATE_STARTED || xTaskGetTickCount() - start >= timeout) return -4;
        TickType_t wait = pdMS_TO_TICKS(ERROR_HANDLER_POLL_MS);
        vTaskDelay(wait ? wait : 1);
    }
    atomic_store(&selfTestActive, false);
    return 0;
}

static int32_t EH_PerformSelfTest(void) {
    if (atomic_exchange(&selfTestCall,true)) return -2;
    int32_t result=EH_RunSelfTest();
    atomic_store(&selfTestCall,false);
    return result;
}

/**
 * @brief Register CLI commands into the Debug Console.
 */
static esp_err_t EH_RegisterConsoleCommands(void) {
    IDebugConsole *console = (IDebugConsole *)ServiceLocator_GetService(SVC_NAME_DEBUG_CONSOLE);
    /* A core-only composition may omit the console; production startup checks
     * the public CLI registrar separately. */
    if (!console) return ESP_OK;
    return ErrorHandler_RegisterConsoleCommands();
}

/**
 * @brief Initialize the error handler service.
 *
 * Args:
 *     self (IService*): Pointer to IService instance.
 *     registry (IDIRegistry*): Pointer to dependency injection registry.
 *
 * Returns:
 *     int32_t: 0 on success, negative error code on failure.
 */
static int32_t EH_Initialize(IService *self, IDIRegistry *registry) {
    (void)self;(void)registry;
    if (!errorHandlerInstance.controlMutex) errorHandlerInstance.controlMutex=xSemaphoreCreateMutex();
    if (!errorHandlerInstance.controlMutex) return -2;
    if (xSemaphoreTake(errorHandlerInstance.controlMutex,pdMS_TO_TICKS(ERROR_HANDLER_CONTROL_MS))!=pdTRUE) return -2;
    int32_t result=0;
    if (errorHandlerInstance.state==SERVICE_STATE_STARTED || errorHandlerInstance.state==SERVICE_STATE_ERROR) result=-1;
    else if (!errorHandlerInstance.historyBuffer) {
        const ErrorHandlerConfig *config=ErrorHandlerConfig_GetConfig();
        errorHandlerInstance.historyBuffer=calloc(config->historySize,sizeof(ErrorRecord));
        if (!errorHandlerInstance.historyBuffer) result=-1;
        else if (!(errorHandlerInstance.stateMutex=xSemaphoreCreateMutex())) result=-2;
        else if (!(errorHandlerInstance.errorQueue=xQueueCreate(config->queueSize,sizeof(ErrorRecord)))) result=-3;
        else if (WatchdogWorker_Init(&errorHandlerInstance.worker)!=0) result=-3;
        if (result) {
            if (errorHandlerInstance.errorQueue) {vQueueDelete(errorHandlerInstance.errorQueue);errorHandlerInstance.errorQueue=NULL;}
            if (errorHandlerInstance.stateMutex) {vSemaphoreDelete(errorHandlerInstance.stateMutex);errorHandlerInstance.stateMutex=NULL;}
            free(errorHandlerInstance.historyBuffer);errorHandlerInstance.historyBuffer=NULL;
        } else errorHandlerInstance.state=SERVICE_STATE_INITIALIZED;
    }
    xSemaphoreGive(errorHandlerInstance.controlMutex);return result;
}

/**
 * @brief Start the error handler service.
 *
 * Args:
 *     self (IService*): Pointer to IService instance.
 *
 * Returns:
 *     int32_t: 0 on success, negative error code on failure.
 */
static int32_t EH_Start(IService *self) {
    (void)self;
    if (errorHandlerInstance.queueProcessorTask && errorHandlerInstance.queueProcessorTask==xTaskGetCurrentTaskHandle()) return -1;
    if (!errorHandlerInstance.controlMutex || xSemaphoreTake(errorHandlerInstance.controlMutex,pdMS_TO_TICKS(ERROR_HANDLER_CONTROL_MS))!=pdTRUE) return -1;
    int32_t result=-1;ServiceState previous=errorHandlerInstance.state;
    if (previous==SERVICE_STATE_STARTED) result=0;
    else if ((previous==SERVICE_STATE_INITIALIZED || previous==SERVICE_STATE_STOPPED) && errorHandlerInstance.historyBuffer) {
        result=EH_RegisterConsoleCommands();
        if (!result) {
            const ErrorHandlerConfig *config=ErrorHandlerConfig_GetConfig();
            result=WatchdogWorker_Start(&errorHandlerInstance.worker,EH_QueueProcessorTask,"EH_Worker",config->taskStackSize,NULL,config->taskPriority);
            errorHandlerInstance.queueProcessorTask=atomic_load(&errorHandlerInstance.worker.task);
            errorHandlerInstance.state=result ? previous : SERVICE_STATE_STARTED;
        }
    }
    xSemaphoreGive(errorHandlerInstance.controlMutex);return result;
}

/**
 * @brief Stop the error handler service.
 *
 * Args:
 *     self (IService*): Pointer to IService instance.
 *
 * Returns:
 *     int32_t: 0 on success.
 */
static int32_t EH_Stop(IService *self) {
    (void)self;
    if (errorHandlerInstance.queueProcessorTask && errorHandlerInstance.queueProcessorTask==xTaskGetCurrentTaskHandle()) return -1;
    if (!errorHandlerInstance.controlMutex) return 0;
    if (xSemaphoreTake(errorHandlerInstance.controlMutex,pdMS_TO_TICKS(ERROR_HANDLER_CONTROL_MS))!=pdTRUE) return -1;
    errorHandlerInstance.state=SERVICE_STATE_STOPPED;
    int32_t result=WatchdogWorker_Stop(&errorHandlerInstance.worker,pdMS_TO_TICKS(ERROR_HANDLER_STOP_MS));
    if (result) errorHandlerInstance.state=SERVICE_STATE_ERROR;
    else {
        errorHandlerInstance.queueProcessorTask=NULL;
        ErrorRecord discarded;
        while (errorHandlerInstance.errorQueue && xQueueReceive(errorHandlerInstance.errorQueue,&discarded,0)==pdTRUE) {}
        atomic_store(&selfTestActive,false);atomic_store(&selfTestCompleted,false);
    }
    /* Retain queue/history/state lock across Stop: late ISR producers cannot
     * dereference a freed queue. Initialize precedes concurrent consumers. */
    xSemaphoreGive(errorHandlerInstance.controlMutex);return result;
}

/**
 * @brief Get the current service state.
 *
 * Args:
 *     self (IService*): Pointer to IService instance.
 *
 * Returns:
 *     ServiceState: Current service state.
 */
static ServiceState EH_GetState(IService *self) {
    (void)self;
    return errorHandlerInstance.state;
}

/**
 * @brief Check the health of the error handler service.
 *
 * Args:
 *     self (IService*): Pointer to IService instance.
 *
 * Returns:
 *     int32_t: 0 if healthy (started), -1 otherwise.
 */
static int32_t EH_CheckHealth(IService *self) {
    (void)self;
    return (errorHandlerInstance.state == SERVICE_STATE_STARTED) ? 0 : -1;
}

/**
 * @brief Get the global ErrorHandler instance.
 *
 * Returns:
 *     IErrorHandler*: Pointer to IErrorHandler interface.
 */
static ErrorHandlerImpl errorHandlerInstance = {
    .base = {
        .base = {.Initialize=EH_Initialize,.Start=EH_Start,.Stop=EH_Stop,.GetState=EH_GetState,.CheckHealth=EH_CheckHealth},
        .GetStatistics=EH_GetStatistics,.HandleError=EH_HandleError,.GetLastErrorCode=EH_GetLastErrorCode,
        .HandleErrorAsync=EH_HandleErrorAsync,.HandleErrorFromISR=EH_HandleErrorFromISR,
        .RegisterCallback=EH_RegisterCallback,.GetErrorHistory=EH_GetErrorHistory,
        .ClearHistory=EH_ClearHistory,.PerformSelfTest=EH_PerformSelfTest
    },
    .state=SERVICE_STATE_UNINITIALIZED
};
IErrorHandler *GetErrorHandler(void) {
    return &errorHandlerInstance.base;
}
