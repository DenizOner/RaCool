#ifndef IERROR_HANDLER_H
#define IERROR_HANDLER_H

#include "Core/IService.h"
#include "freertos/FreeRTOS.h"
#include <stdint.h>
#include <stdbool.h>

#define ERROR_HANDLER_TAG_CAPACITY 32u

/**
 * @brief Structure representing a single recorded error log entry.
 */
typedef struct {
    char tag[ERROR_HANDLER_TAG_CAPACITY];         ///< Component tag where error occurred
    int32_t errorCode;    ///< Numeric error code
    uint32_t timestamp;   ///< System tick timestamp of the error
    bool isSelfTest;      ///< Internal marked self-test record; ordinary producers leave this false
    bool isAsync;         ///< True if error was processed asynchronously
} ErrorRecord;

/**
 * @brief Callback function type for custom error events.
 *
 * Args:
 *     tag (const char*): Source component identifier tag.
 *     errorCode (int32_t): Numeric error code.
 */
typedef void (*ErrorHandlerCallback)(const char *tag, int32_t errorCode);

/**
 * @brief Interface for the central Error Handler system.
 *
 * This service manages logging, queueing, callbacks, and history logs
 * for system-wide exceptions and failures in a thread-safe manner.
 */
typedef struct {
    ServiceState state;
    int32_t lastErrorCode;
    uint32_t totalErrorCount;
    uint32_t callbackCount;
} ErrorHandlerStatistics;

typedef struct IErrorHandler {
    /**
     * @brief Base service lifecycle interface.
     */
    IService base;

    /** Checked, coherent statistics snapshot; output is unchanged on failure. */
    int32_t (*GetStatistics)(ErrorHandlerStatistics *output);

    /**
     * @brief Handle error synchronously (legacy fallback).
     *
     * Args:
     *     tag (const char*): Source component identifier tag.
     *     errorCode (int32_t): Numeric code of the occurred error.
     */
    void (*HandleError)(const char *tag, int32_t errorCode);

    /**
     * @brief Get the last error code recorded.
     *
     * Returns:
     *     int32_t: The last recorded error code.
     */
    int32_t (*GetLastErrorCode)(void);

    /**
     * @brief Handle error asynchronously by queueing it.
     *
     * This function is thread-safe and non-blocking under normal operation.
     *
     * Args:
     *     tag (const char*): Source component identifier tag.
     *     errorCode (int32_t): Numeric code of the occurred error.
     *
     * Returns:
     *     int32_t: 0 on success, negative error code on failure.
     */
    int32_t (*HandleErrorAsync)(const char *tag, int32_t errorCode);

    /**
     * @brief Handle error from Interrupt Service Routine (ISR) context.
     *
     * This function is non-blocking in ordinary ISR context. It does not promise
     * cache-disabled/IRAM-safe execution; tag storage must remain accessible.
     * Tags must contain 1 to ERROR_HANDLER_TAG_CAPACITY-1 bytes.
     *
     * Args:
     *     tag (const char*): Source component identifier tag.
     *     errorCode (int32_t): Numeric code of the occurred error.
     *     pxHigherPriorityTaskWoken (BaseType_t*): Pointer to flag indicating context switch.
     *
     * Returns:
     *     int32_t: 0 on success, negative error code on failure.
     */
    int32_t (*HandleErrorFromISR)(const char *tag, int32_t errorCode, BaseType_t *pxHigherPriorityTaskWoken);

    /**
     * @brief Register a callback to be executed on error occurrences.
     *
     * Callback code and referenced storage must remain valid for the process.
     * Callbacks execute on the worker outside the history mutex. They must not
     * synchronously Start/Stop this worker; those reentrant calls are rejected.
     *
     * Args:
     *     callback (ErrorHandlerCallback): Callback function pointer.
     *
     * Returns:
     *     int32_t: 0 on success, negative error code on failure.
     */
    int32_t (*RegisterCallback)(ErrorHandlerCallback callback);

    /**
     * @brief Retrieve error logs history.
     *
     * Args:
     *     outRecords (ErrorRecord*): Array to populate with error records.
     *     maxRecords (uint32_t): Maximum capacity of the output array.
     *     outCount (uint32_t*): Pointer to populate with actual number of records written.
     *
     * Returns:
     *     int32_t: 0 on success, negative error code on failure.
     */
    int32_t (*GetErrorHistory)(ErrorRecord *outRecords, uint32_t maxRecords, uint32_t *outCount);

    /**
     * @brief Clear all stored error history and counters.
     * @return 0 on success; negative on missing resources or lock failure.
     */
    int32_t (*ClearHistory)(void);

    /**
     * @brief Run self-diagnostics on the ErrorHandler service.
     *
     * Returns:
     *     int32_t: 0 if all tests passed, non-zero error code on failure.
     */
    int32_t (*PerformSelfTest)(void);
} IErrorHandler;

/**
 * @brief Retrieve the global ErrorHandler singleton instance.
 *
 * Returns:
 *     IErrorHandler*: Pointer to the IErrorHandler interface.
 */
IErrorHandler *GetErrorHandler(void);

#endif // IERROR_HANDLER_H

