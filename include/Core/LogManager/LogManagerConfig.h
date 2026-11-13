#ifndef LOG_MANAGER_CONFIG_H
#define LOG_MANAGER_CONFIG_H

#include <esp_log.h>
#include <stdbool.h>
#include <stdint.h>

#define LOG_MANAGER_MAX_ENTRIES 256U
#define LOG_MANAGER_MIN_FORMAT_SIZE 32U
#define LOG_MANAGER_MAX_FORMAT_SIZE 256U
#define LOG_MANAGER_CONTROL_TIMEOUT_MS 1000U

/**
 * @brief Configuration for the LogManager
 */
typedef struct {
  uint32_t maxLogEntries;     /**< Maximum number of log entries to store in the
                                 circular buffer */
  uint32_t vprintfBufferSize; /**< Size of the temporary buffer used for
                                 formatting log messages */
  esp_log_level_t defaultLogLevel; /**< Initial global log level */
  bool enableInterception; /**< Whether to intercept esp_log output using
                              esp_log_set_vprintf */
} LogManagerConfig;

/**
 * @brief Get the default configuration for the LogManager
 * @return Constant pointer to default configuration
 */
const LogManagerConfig *LogManagerConfig_GetDefault(void);

#endif // LOG_MANAGER_CONFIG_H
