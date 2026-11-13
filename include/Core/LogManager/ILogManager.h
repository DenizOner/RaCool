#ifndef ILOGMANAGER_H
#define ILOGMANAGER_H

#include "Core/IService.h"
#include "Core/LogManager/LogManagerConfig.h"
#include <esp_log.h>
#include <stdbool.h>
#include <stdint.h>

typedef enum {
  LOG_LEVEL_DEBUG = ESP_LOG_DEBUG,
  LOG_LEVEL_INFO = ESP_LOG_INFO,
  LOG_LEVEL_WARNING = ESP_LOG_WARN,
  LOG_LEVEL_ERROR = ESP_LOG_ERROR,
  LOG_LEVEL_CRITICAL = ESP_LOG_ERROR
} LogLevel;

typedef struct {
  uint32_t id;
  uint32_t timestamp;
  char level[8];
  char module[16];
  char message[80];
} SystemLogEntry;

typedef struct ILogManager ILogManager;

struct ILogManager {
  IService base;
  /** Exclusive temporary RAM capture; rejects another active interception owner. */
  int32_t (*BeginCapture)(ILogManager *self);
  int32_t (*EndCapture)(ILogManager *self);

  /**
   * @brief Emit a validated formatted message through the ESP-IDF log sink
   */
  int32_t (*Log)(LogLevel level, const char *tag, const char *format, ...)
      __attribute__((format(printf, 3, 4)));

  /**
   * @brief Initialize the LogManager with a specific configuration
   */
  int32_t (*InitializeWithConfig)(ILogManager *self,
                                  const LogManagerConfig *config,
                                  struct IDIRegistry *registry);

  /**
   * @brief Configure the log level for a specific TAG
   */
  void (*SetModuleLogLevel)(const char *tag, esp_log_level_t level);

  /**
   * @brief Get the current log level for a specific TAG
   */
  esp_log_level_t (*GetModuleLogLevel)(const char *tag);

  /**
   * @brief Set the global default log level
   */
  void (*SetGlobalLogLevel)(esp_log_level_t level);

  /**
   * @brief Copy the newest requested captured entries in chronological order
   */
  uint32_t (*GetRecentLogs)(SystemLogEntry *logs, uint32_t max_logs);
};

/**
 * @brief Get the LogManager singleton instance
 */
ILogManager *GetLogManager(void);

#endif // ILOGMANAGER_H
