#include "Core/DI/IDIRegistry.h"
#include "Core/IService.h"
#include "Core/LogManager/ILogManager.h"
#include "Core/LogManager/LogManagerConfig.h"
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <freertos/semphr.h>
#include <string.h>
#include "Communication/SerialMonitor/ISerialMonitor.h"
#include "Communication/SerialMonitor/ISerialMonitorPlugin.h"
#include "Core/ServiceLocator/ServiceLocator.h"
#include "Core/ServiceLocator/ServiceNames.h"

// External plugin getter from SerialLogPlugin.c
extern ISerialMonitorPlugin* LogManager_GetSerialPlugin(void);

// MAX_LOG_ENTRIES is now configurable via LogManagerConfig

typedef struct {
  ILogManager base;
  _Atomic(ServiceState) state;
  LogManagerConfig config;
  SemaphoreHandle_t controlMutex;
  ISerialMonitor *ownedSerial;
  bool captureEnabled;
  bool hookInstalled;
  bool diagnosticCaptureOwned;
  SystemLogEntry *logBuffer;
  uint32_t logHead;
  uint32_t logCount;
  uint32_t logIdCounter;
  vprintf_like_t original_vprintf;
} LogManagerImpl;

static LogManagerImpl logManagerInstance = {.state = SERVICE_STATE_UNINITIALIZED,
                                            .logIdCounter = 1,
                                            .logHead = 0,
                                            .logCount = 0,
                                            .original_vprintf = NULL,
                                            .logBuffer = NULL};

static portMUX_TYPE log_spinlock = portMUX_INITIALIZER_UNLOCKED;
static int LogManager_vprint(const char *fmt, va_list ap);
static int32_t LogManager_BeginCapture(ILogManager *self) {
  if (self != &logManagerInstance.base || !logManagerInstance.controlMutex ||
      xSemaphoreTake(logManagerInstance.controlMutex,pdMS_TO_TICKS(LOG_MANAGER_CONTROL_TIMEOUT_MS))!=pdTRUE) return -1;
  int32_t result=-1;
  if (logManagerInstance.state==SERVICE_STATE_STARTED && !logManagerInstance.hookInstalled && logManagerInstance.logBuffer) {
    vprintf_like_t previous=esp_log_set_vprintf(LogManager_vprint);
    portENTER_CRITICAL(&log_spinlock);
    logManagerInstance.original_vprintf=previous;
    logManagerInstance.captureEnabled=true;
    logManagerInstance.hookInstalled=true;
    logManagerInstance.diagnosticCaptureOwned=true;
    portEXIT_CRITICAL(&log_spinlock);
    result=0;
  }
  xSemaphoreGive(logManagerInstance.controlMutex);return result;
}
static int32_t LogManager_EndCapture(ILogManager *self) {
  if (self != &logManagerInstance.base || !logManagerInstance.controlMutex ||
      xSemaphoreTake(logManagerInstance.controlMutex,pdMS_TO_TICKS(LOG_MANAGER_CONTROL_TIMEOUT_MS))!=pdTRUE) return -1;
  if (!logManagerInstance.diagnosticCaptureOwned) {xSemaphoreGive(logManagerInstance.controlMutex);return -1;}
  portENTER_CRITICAL(&log_spinlock);
  logManagerInstance.captureEnabled=false;
  logManagerInstance.hookInstalled=false;
  logManagerInstance.diagnosticCaptureOwned=false;
  vprintf_like_t previous=logManagerInstance.original_vprintf;
  portEXIT_CRITICAL(&log_spinlock);
  esp_log_set_vprintf(previous ? previous : vprintf);
  xSemaphoreGive(logManagerInstance.controlMutex);return 0;
}

static int LogManager_vprint(const char *fmt, va_list ap) {
  portENTER_CRITICAL(&log_spinlock);
  vprintf_like_t sink = logManagerInstance.original_vprintf;
  bool capture = logManagerInstance.captureEnabled;
  uint32_t formatSize = logManagerInstance.config.vprintfBufferSize;
  portEXIT_CRITICAL(&log_spinlock);
  /* A callback can run between SDK hook installation and publication of the
   * previous sink. Preserve console output during that short window. */
  if (!sink) sink = vprintf;

  // 1. Original vprintf (Handle with a copy directly without holding spinlocks)
  va_list aq;
  va_copy(aq, ap);
  int ret = sink(fmt, aq);
  va_end(aq);

  // 2. Capture log into circular buffer safely
  if (capture) {
      char local_log_buf[LOG_MANAGER_MAX_FORMAT_SIZE];
      va_list ar;
      va_copy(ar, ap);
      vsnprintf(local_log_buf, formatSize, fmt, ar);
      va_end(ar);

      char level_str[8] = "INFO";
      if (local_log_buf[0] == 'E') strcpy(level_str, "ERROR");
      else if (local_log_buf[0] == 'W') strcpy(level_str, "WARN");
      else if (local_log_buf[0] == 'I') strcpy(level_str, "INFO");
      else if (local_log_buf[0] == 'D') strcpy(level_str, "DEBUG");
      else if (local_log_buf[0] == 'V') strcpy(level_str, "VERB");

      uint32_t now_sec = (uint32_t)(esp_timer_get_time() / 1000000);

      const char *msg_start = strstr(local_log_buf, ": ");
      if (msg_start) {
        msg_start += 2;
      } else {
        msg_start = local_log_buf;
      }

      portENTER_CRITICAL(&log_spinlock);
      if (!logManagerInstance.captureEnabled || !logManagerInstance.logBuffer) {
        portEXIT_CRITICAL(&log_spinlock);
        return ret;
      }
      uint32_t idx = (logManagerInstance.logHead + logManagerInstance.logCount) % logManagerInstance.config.maxLogEntries;
      logManagerInstance.logBuffer[idx].id = logManagerInstance.logIdCounter++;
      logManagerInstance.logBuffer[idx].timestamp = now_sec;
      strncpy(logManagerInstance.logBuffer[idx].level, level_str, sizeof(logManagerInstance.logBuffer[idx].level) - 1);
      logManagerInstance.logBuffer[idx].level[sizeof(logManagerInstance.logBuffer[idx].level) - 1] = '\0';
      strncpy(logManagerInstance.logBuffer[idx].module, "System", sizeof(logManagerInstance.logBuffer[idx].module) - 1);
      logManagerInstance.logBuffer[idx].module[sizeof(logManagerInstance.logBuffer[idx].module) - 1] = '\0';

      strncpy(logManagerInstance.logBuffer[idx].message, msg_start, sizeof(logManagerInstance.logBuffer[idx].message) - 1);
      logManagerInstance.logBuffer[idx].message[sizeof(logManagerInstance.logBuffer[idx].message) - 1] = '\0';

      size_t len = strlen(logManagerInstance.logBuffer[idx].message);
      if (len > 0 && logManagerInstance.logBuffer[idx].message[len - 1] == '\n') {
        logManagerInstance.logBuffer[idx].message[len - 1] = '\0';
      }

      if (logManagerInstance.logCount < logManagerInstance.config.maxLogEntries) {
        logManagerInstance.logCount++;
      } else {
        logManagerInstance.logHead = (logManagerInstance.logHead + 1) % logManagerInstance.config.maxLogEntries;
      }
      portEXIT_CRITICAL(&log_spinlock);
  }

  return ret;
}

static uint32_t LogManager_GetRecentLogs(SystemLogEntry *logs,
                                         uint32_t max_logs) {
  if (!logs || !max_logs) return 0;
  portENTER_CRITICAL(&log_spinlock);
  if (!logManagerInstance.logBuffer) { portEXIT_CRITICAL(&log_spinlock); return 0; }
  uint32_t copy_count = (logManagerInstance.logCount < max_logs)
                            ? logManagerInstance.logCount
                            : max_logs;
  for (uint32_t i = 0; i < copy_count; i++) {
    uint32_t idx = (logManagerInstance.logHead + logManagerInstance.logCount - copy_count + i) %
              logManagerInstance.config.maxLogEntries;
    logs[i] = logManagerInstance.logBuffer[idx];
  }
  portEXIT_CRITICAL(&log_spinlock);
  return copy_count;
}

static void LogManager_SetModuleLogLevel(const char *tag,
                                         esp_log_level_t level) {
  if (tag && *tag && level >= ESP_LOG_NONE && level <= ESP_LOG_VERBOSE) esp_log_level_set(tag, level);
}

static esp_log_level_t LogManager_GetModuleLogLevel(const char *tag) {
  return tag && *tag ? esp_log_level_get(tag) : ESP_LOG_NONE;
}

static void LogManager_SetGlobalLogLevel(esp_log_level_t level) {
  if (level >= ESP_LOG_NONE && level <= ESP_LOG_VERBOSE) esp_log_level_set("*", level);
}

static bool LogManager_ConfigValid(const LogManagerConfig *config) {
  return config && config->maxLogEntries > 0 && config->maxLogEntries <= LOG_MANAGER_MAX_ENTRIES &&
         config->vprintfBufferSize >= LOG_MANAGER_MIN_FORMAT_SIZE &&
         config->vprintfBufferSize <= LOG_MANAGER_MAX_FORMAT_SIZE &&
         config->defaultLogLevel >= ESP_LOG_NONE && config->defaultLogLevel <= ESP_LOG_VERBOSE;
}

/* Lifecycle control is initialized before service publication. The log hook
 * never takes this mutex and never invokes a sink inside log_spinlock. */
static int32_t LogManager_InitializeWithConfig(ILogManager *self, const LogManagerConfig *config,
                                               IDIRegistry *registry) {
  (void)self; (void)registry;
  if (!config) config = LogManagerConfig_GetDefault();
  if (!LogManager_ConfigValid(config)) return -1;
  if (!logManagerInstance.controlMutex) logManagerInstance.controlMutex = xSemaphoreCreateMutex();
  if (!logManagerInstance.controlMutex ||
      xSemaphoreTake(logManagerInstance.controlMutex, pdMS_TO_TICKS(LOG_MANAGER_CONTROL_TIMEOUT_MS)) != pdTRUE) return -1;
  int32_t result = -1;
  ServiceState state = logManagerInstance.state;
  if (state == SERVICE_STATE_INITIALIZED || state == SERVICE_STATE_STARTED) {
    /* Initialization is idempotent only for the same configuration. It is not
     * a live reconfiguration API and must not acknowledge ignored changes. */
    const LogManagerConfig *current = &logManagerInstance.config;
    if (current->maxLogEntries == config->maxLogEntries &&
        current->vprintfBufferSize == config->vprintfBufferSize &&
        current->defaultLogLevel == config->defaultLogLevel &&
        current->enableInterception == config->enableInterception) result = 0;
  }
  else if (state != SERVICE_STATE_ERROR) {
    SystemLogEntry *buffer = calloc(config->maxLogEntries, sizeof(*buffer));
    if (buffer) {
      portENTER_CRITICAL(&log_spinlock);
      logManagerInstance.config = *config;
      logManagerInstance.logBuffer = buffer;
      logManagerInstance.logHead = logManagerInstance.logCount = 0;
      portEXIT_CRITICAL(&log_spinlock);
      logManagerInstance.state = SERVICE_STATE_INITIALIZED;
      result = 0;
    }
  }
  xSemaphoreGive(logManagerInstance.controlMutex);
  return result;
}

static int32_t LogManager_Initialize(IService *self, IDIRegistry *registry) {
  return LogManager_InitializeWithConfig((ILogManager *)self, NULL, registry);
}

static int32_t LogManager_Start(IService *self) {
  (void)self;
  if (!logManagerInstance.controlMutex ||
      xSemaphoreTake(logManagerInstance.controlMutex, pdMS_TO_TICKS(LOG_MANAGER_CONTROL_TIMEOUT_MS)) != pdTRUE) return -1;
  int32_t result = -1;
  if (logManagerInstance.state == SERVICE_STATE_STARTED) result = 0;
  else if (logManagerInstance.state == SERVICE_STATE_INITIALIZED || logManagerInstance.state == SERVICE_STATE_STOPPED) {
    if (!logManagerInstance.logBuffer) {
      SystemLogEntry *buffer = calloc(logManagerInstance.config.maxLogEntries, sizeof(*buffer));
      portENTER_CRITICAL(&log_spinlock);
      logManagerInstance.logBuffer = buffer;
      logManagerInstance.logHead = logManagerInstance.logCount = 0;
      portEXIT_CRITICAL(&log_spinlock);
    }
    ISerialMonitor *serial = (ISerialMonitor *)ServiceLocator_GetService(SVC_NAME_SERIAL_MONITOR);
    if (logManagerInstance.logBuffer && (!serial ||
        (serial->AddPlugin && serial->RemovePlugin && serial->AddPlugin(serial, LogManager_GetSerialPlugin()) == 0))) {
      logManagerInstance.ownedSerial = serial;
      if (logManagerInstance.config.enableInterception) {
        vprintf_like_t previous = esp_log_set_vprintf(LogManager_vprint);
        portENTER_CRITICAL(&log_spinlock);
        logManagerInstance.original_vprintf = previous;
        logManagerInstance.captureEnabled = true;
        logManagerInstance.hookInstalled = true;
        portEXIT_CRITICAL(&log_spinlock);
      }
      esp_log_level_set("*", logManagerInstance.config.defaultLogLevel);
      logManagerInstance.state = SERVICE_STATE_STARTED;
      result = 0;
    }
  }
  xSemaphoreGive(logManagerInstance.controlMutex);
  return result;
}

static int32_t LogManager_Stop(IService *self) {
  (void)self;
  if (!logManagerInstance.controlMutex ||
      xSemaphoreTake(logManagerInstance.controlMutex, pdMS_TO_TICKS(LOG_MANAGER_CONTROL_TIMEOUT_MS)) != pdTRUE) return -1;
  if (logManagerInstance.state == SERVICE_STATE_UNINITIALIZED) {
    xSemaphoreGive(logManagerInstance.controlMutex);
    return 0;
  }
  if (logManagerInstance.diagnosticCaptureOwned) {
    xSemaphoreGive(logManagerInstance.controlMutex);
    return -1; /* The capture owner must release its callback lifetime first. */
  }
  logManagerInstance.state = SERVICE_STATE_ERROR;
  ISerialMonitor *serial = logManagerInstance.ownedSerial;
  if (serial && (!serial->RemovePlugin || serial->RemovePlugin(serial, LogManager_GetSerialPlugin()) != 0)) {
    xSemaphoreGive(logManagerInstance.controlMutex);
    return -1;
  }
  logManagerInstance.ownedSerial = NULL;
  portENTER_CRITICAL(&log_spinlock);
  logManagerInstance.captureEnabled = false;
  bool installed = logManagerInstance.hookInstalled;
  vprintf_like_t sink = logManagerInstance.original_vprintf;
  logManagerInstance.hookInstalled = false;
  SystemLogEntry *buffer = logManagerInstance.logBuffer;
  logManagerInstance.logBuffer = NULL;
  logManagerInstance.logHead = logManagerInstance.logCount = 0;
  portEXIT_CRITICAL(&log_spinlock);
  if (installed) esp_log_set_vprintf(sink);
  free(buffer);
  logManagerInstance.state = SERVICE_STATE_STOPPED;
  xSemaphoreGive(logManagerInstance.controlMutex);
  return 0;
}

static ServiceState LogManager_GetState(IService *self) {
  return logManagerInstance.state;
}

static int32_t LogManager_CheckHealth(IService *self) {
  return (logManagerInstance.state == SERVICE_STATE_STARTED) ? 0 : -1;
}

static int32_t LogManager_Log(LogLevel level, const char *tag,
                              const char *format, ...) {
  if (!tag || !*tag || !format || (int)level < ESP_LOG_ERROR || (int)level > ESP_LOG_VERBOSE) return -1;
  va_list args;
  va_start(args, format);
  esp_log_writev((esp_log_level_t)level, tag, format, args);
  va_end(args);
  return 0;
}

ILogManager *GetLogManager(void) {
  static bool initialized = false;
  if (!initialized) {
    logManagerInstance.base.base.Initialize = LogManager_Initialize;
    logManagerInstance.base.base.Start = LogManager_Start;
    logManagerInstance.base.base.Stop = LogManager_Stop;
    logManagerInstance.base.base.GetState = LogManager_GetState;
    logManagerInstance.base.base.CheckHealth = LogManager_CheckHealth;

    logManagerInstance.base.Log = LogManager_Log;
    logManagerInstance.base.SetModuleLogLevel = LogManager_SetModuleLogLevel;
    logManagerInstance.base.GetModuleLogLevel = LogManager_GetModuleLogLevel;
    logManagerInstance.base.SetGlobalLogLevel = LogManager_SetGlobalLogLevel;
    logManagerInstance.base.GetRecentLogs = LogManager_GetRecentLogs;
    logManagerInstance.base.BeginCapture = LogManager_BeginCapture;
    logManagerInstance.base.EndCapture = LogManager_EndCapture;
    logManagerInstance.base.InitializeWithConfig =
        LogManager_InitializeWithConfig;

    logManagerInstance.state = SERVICE_STATE_UNINITIALIZED;
    initialized = true;
  }
  return &logManagerInstance.base;
}
