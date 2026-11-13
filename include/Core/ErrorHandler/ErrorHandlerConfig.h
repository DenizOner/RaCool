#ifndef ERROR_HANDLER_CONFIG_H
#define ERROR_HANDLER_CONFIG_H

#include <stdbool.h>
#include <stdint.h>
#define ERROR_HANDLER_POLL_MS 20U
#define ERROR_HANDLER_CONTROL_MS 1000U
#define ERROR_HANDLER_STOP_MS 500U
#define ERROR_HANDLER_SELFTEST_MS 1000U

/**
 * @brief Structure containing configuration settings for the Error Handler.
 *
 * These settings define the runtime behavior of the Error Handler queue,
 * circular logs history buffer, background task stack, and priorities.
 */
typedef struct {
    bool enableRecovery;        ///< Legacy reserved field; does not initiate recovery
    uint32_t maxRetries;        ///< Legacy reserved field; no recovery retry policy is implemented here
    uint32_t queueSize;         ///< FreeRTOS queue size for async errors
    uint32_t historySize;       ///< Size of the circular buffer for error logs history
    uint32_t taskStackSize;     ///< Stack size for background worker task
    uint32_t taskPriority;      ///< Priority of background worker task
} ErrorHandlerConfig;

/**
 * @brief Retrieve current global ErrorHandler configuration.
 *
 * This function returns a read-only pointer to the static configuration,
 * making it thread-safe for concurrent read access without locks.
 *
 * Returns:
 *     const ErrorHandlerConfig*: Read-only pointer to configuration structure.
 */
const ErrorHandlerConfig *ErrorHandlerConfig_GetConfig(void);

#endif // ERROR_HANDLER_CONFIG_H


