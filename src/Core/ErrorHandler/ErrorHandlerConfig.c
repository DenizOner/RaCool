#include "Core/ErrorHandler/ErrorHandlerConfig.h"

/**
 * @brief Default static configuration settings for the Error Handler system.
 *
 * Configured specifically for the ESP32-C6 environment with 8MB flash and 512KB SRAM.
 */
static const ErrorHandlerConfig defaultConfig = {
    .enableRecovery = true,
    .maxRetries = 3,
    .queueSize = 10,
    .historySize = 16,
    .taskStackSize = 3072,
    .taskPriority = 3,
};

/**
 * @brief Retrieve current ErrorHandler configuration.
 *
 * Returns:
 *     const ErrorHandlerConfig*: Read-only pointer to configuration structure.
 */
const ErrorHandlerConfig *ErrorHandlerConfig_GetConfig(void)
{
    return &defaultConfig;
}
