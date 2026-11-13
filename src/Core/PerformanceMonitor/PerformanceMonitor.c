#include "Core/DI/IDIRegistry.h"
#include "Core/PerformanceMonitor/IPerformanceMonitor.h"
#include "Core/PerformanceMonitor/PerformanceMonitorConfig.h"
#include <esp_err.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_rom_sys.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdatomic.h>
#include "Management/Watchdog/WatchdogWorker.h"
#include "Management/Watchdog/WatchdogConfig.h"

static const char *TAG = "PerformanceMonitor";
static PerformanceMonitorConfig currentConfig;
static WatchdogWorker monitorWorker;
static SemaphoreHandle_t controlMutex;
static _Atomic(ServiceState) monitorState = SERVICE_STATE_UNINITIALIZED;
static portMUX_TYPE cpuMux = portMUX_INITIALIZER_UNLOCKED;
static CpuUsageSnapshot cpuSnapshot;
static bool cpuValid, cpuBaseline;
static uint32_t previousIdle;
static uint64_t previousTime;

/* The existing performance worker is the only runtime-sample writer. Unsigned
 * idle deltas cover the SDK counter wrap; full-width elapsed time rejects gaps
 * spanning an entire 32-bit microsecond counter period. */
static void Perf_UpdateCpuCounters(uint32_t idle, uint64_t now) {
  if (!cpuBaseline || now <= previousTime || now - previousTime > UINT32_MAX) {
    previousIdle = idle; previousTime = now; cpuBaseline = true;
    portENTER_CRITICAL(&cpuMux); cpuValid = false; portEXIT_CRITICAL(&cpuMux);
    return;
  }
  uint64_t elapsed = now - previousTime;
  if (elapsed < PERF_MONITOR_CPU_WINDOW_US) return;
  uint32_t idleDelta = idle - previousIdle;
  previousIdle = idle; previousTime = now;
  portENTER_CRITICAL(&cpuMux);
  cpuValid = idleDelta <= elapsed;
  if (cpuValid) cpuSnapshot = (CpuUsageSnapshot){
      .busy_hundredths_percent = (uint16_t)(((elapsed - idleDelta) * 10000ULL) / elapsed),
      .measured_time_us = now, .window_us = (uint32_t)elapsed};
  portEXIT_CRITICAL(&cpuMux);
}

static void Perf_SampleCpu(void) {
#if CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS && CONFIG_FREERTOS_RUN_TIME_STATS_USING_ESP_TIMER
  uint32_t idle = (uint32_t)ulTaskGetIdleRunTimeCounter();
  Perf_UpdateCpuCounters(idle, (uint64_t)esp_timer_get_time());
#else
  /* The SDK runtime counter must use the same microsecond clock. */
  (void)Perf_UpdateCpuCounters;
#endif
}

static esp_err_t Perf_GetCpuUsage(CpuUsageSnapshot *out) {
  if (!out) return ESP_ERR_INVALID_ARG;
  portENTER_CRITICAL(&cpuMux);
  bool valid = cpuValid && monitorState == SERVICE_STATE_STARTED;
  if (valid) *out = cpuSnapshot;
  portEXIT_CRITICAL(&cpuMux);
  return valid ? ESP_OK : ESP_ERR_NOT_FINISHED;
}


static TickType_t Perf_DelayTicks(uint32_t milliseconds) {
  TickType_t ticks = pdMS_TO_TICKS(milliseconds);
  return ticks ? ticks : 1;
}

static void Perf_ReportTaskStats(void) {
#if CONFIG_FREERTOS_USE_TRACE_FACILITY
  TaskStatus_t *tasks = calloc(PERF_MONITOR_MAX_TASK_SNAPSHOT, sizeof(*tasks));
  if (!tasks) { ESP_LOGE(TAG, "Task snapshot allocation failed"); return; }
  UBaseType_t count = uxTaskGetSystemState(tasks, PERF_MONITOR_MAX_TASK_SNAPSHOT, NULL);
  if (!count) ESP_LOGE(TAG, "Task snapshot exceeds capacity or is unavailable");
  for (UBaseType_t i = 0; i < count; ++i) {
    ESP_LOGI(TAG, "Task %lu state=%u priority=%lu stack=%lu",
             (unsigned long)tasks[i].xTaskNumber, (unsigned)tasks[i].eCurrentState,
             (unsigned long)tasks[i].uxCurrentPriority, (unsigned long)tasks[i].usStackHighWaterMark);
  }
  free(tasks);
#else
  ESP_LOGW(TAG, "Task snapshots require FreeRTOS trace facility");
#endif
}

/**
 * @brief Task for periodic performance monitoring
 */
static void PerfMonitorTask(void *pvParameters) {
  (void)pvParameters;
  ESP_LOGI(TAG, "Performance monitoring task started");

  while (1) {
    WatchdogWorker_Checkpoint(&monitorWorker, NULL, (uint64_t)esp_timer_get_time() / 1000ULL);
    if (monitorState != SERVICE_STATE_STARTED) { vTaskDelay(Perf_DelayTicks(10)); continue; }
    if (currentConfig.enableMemUsage) {
      uint32_t free_heap = esp_get_free_heap_size();
      uint32_t min_free_heap = esp_get_minimum_free_heap_size();
      ESP_LOGI(TAG, "Memory: Free Heap: %lu bytes, Min Free Heap: %lu bytes",
               free_heap, min_free_heap);

      size_t internal_free =
          heap_caps_get_free_size(MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
      ESP_LOGI(TAG, "Internal RAM Free: %u bytes", internal_free);
    }

    if (currentConfig.enableCpuUsage) {
      Perf_SampleCpu();
      CpuUsageSnapshot usage;
      if (Perf_GetCpuUsage(&usage) == ESP_OK)
        ESP_LOGI(TAG, "CPU Usage: %u.%02u%%; window=%lu us",
                 (unsigned)(usage.busy_hundredths_percent / 100),
                 (unsigned)(usage.busy_hundredths_percent % 100), (unsigned long)usage.window_us);
      ESP_LOGI(TAG, "System Uptime: %llu ms", esp_timer_get_time() / 1000);
    }

    if (currentConfig.enableTaskStats) Perf_ReportTaskStats();

    uint32_t delayRemainingMs = currentConfig.reportIntervalMs;
    while (monitorState == SERVICE_STATE_STARTED && delayRemainingMs > 0) {
      uint32_t chunk = delayRemainingMs > 100 ? 100 : delayRemainingMs;
      WatchdogWorker_Checkpoint(&monitorWorker, NULL, (uint64_t)esp_timer_get_time() / 1000ULL);
      if (currentConfig.enableCpuUsage) Perf_SampleCpu();
      vTaskDelay(Perf_DelayTicks(chunk));
      delayRemainingMs -= chunk;
    }
  }

}

/**
 * @brief Initialize performance monitor with config
 */
static bool Perf_ConfigValid(const PerformanceMonitorConfig *config) {
  return config->reportIntervalMs >= PERF_MONITOR_MIN_INTERVAL_MS &&
         config->reportIntervalMs <= PERF_MONITOR_MAX_INTERVAL_MS &&
         config->taskStackSize >= PERF_MONITOR_MIN_STACK_SIZE &&
         config->taskStackSize <= PERF_MONITOR_MAX_STACK_SIZE &&
         config->taskPriority < configMAX_PRIORITIES;
}

/* Initialization occurs before publication. Lifecycle/configuration operations
 * then serialize on controlMutex; the worker never takes that mutex. */
static esp_err_t Perf_InitializeWithConfig(PerformanceMonitorConfig config) {
  if (!Perf_ConfigValid(&config)) return ESP_ERR_INVALID_ARG;
  if (!controlMutex) controlMutex = xSemaphoreCreateMutex();
  if (!controlMutex) return ESP_ERR_NO_MEM;
  if (xSemaphoreTake(controlMutex, pdMS_TO_TICKS(WATCHDOG_WORKER_CONTROL_TIMEOUT_MS)) != pdTRUE)
    return ESP_ERR_TIMEOUT;
  esp_err_t result = ESP_OK;
  if (monitorState == SERVICE_STATE_STARTED || monitorState == SERVICE_STATE_ERROR)
    result = ESP_ERR_INVALID_STATE;
  else if (WatchdogWorker_Init(&monitorWorker) != 0) result = ESP_ERR_NO_MEM;
  else { currentConfig = config; monitorState = SERVICE_STATE_INITIALIZED; }
  xSemaphoreGive(controlMutex);
  return result;
}

static esp_err_t Perf_Init(void) {
  if (monitorState == SERVICE_STATE_ERROR) return ESP_ERR_INVALID_STATE;
  return monitorState != SERVICE_STATE_UNINITIALIZED ? ESP_OK :
         Perf_InitializeWithConfig(PerformanceMonitorConfig_GetDefault());
}

static int32_t Perf_Service_Start(IService *self) {
  (void)self;
  if (!controlMutex || xSemaphoreTake(controlMutex, pdMS_TO_TICKS(WATCHDOG_WORKER_CONTROL_TIMEOUT_MS)) != pdTRUE)
    return -1;
  int32_t result = -1;
  ServiceState previous = monitorState;
  if (previous == SERVICE_STATE_STARTED) result = WatchdogWorker_Healthy(&monitorWorker) ? 0 : -1;
  else if (previous == SERVICE_STATE_INITIALIZED || previous == SERVICE_STATE_STOPPED) {
    cpuBaseline = false;
    portENTER_CRITICAL(&cpuMux); cpuValid = false; portEXIT_CRITICAL(&cpuMux);
    monitorState = SERVICE_STATE_STARTED;
    result = WatchdogWorker_Start(&monitorWorker, PerfMonitorTask, "PerfMon",
                                 currentConfig.taskStackSize, NULL, currentConfig.taskPriority);
    if (result != 0) monitorState = previous;
  }
  xSemaphoreGive(controlMutex);
  return result;
}

static void Perf_StartMonitoring(void) {
  if (Perf_Service_Start(NULL) != 0) ESP_LOGE(TAG, "Performance monitoring start failed");
}

static int32_t Perf_Service_Initialize(IService *self, IDIRegistry *registry) {
  (void)self; (void)registry;
  return Perf_Init() == ESP_OK ? 0 : -1;
}

static ServiceState Perf_Service_GetState(IService *self) { (void)self; return monitorState; }
static int32_t Perf_Service_CheckHealth(IService *self) {
  (void)self;
  return monitorState == SERVICE_STATE_STARTED && WatchdogWorker_Healthy(&monitorWorker) ? 0 : -1;
}

static int32_t Perf_Service_Stop(IService *self) {
  (void)self;
  TaskHandle_t task = atomic_load(&monitorWorker.task);
  if (task && task == xTaskGetCurrentTaskHandle()) return -1;
  if (!controlMutex || xSemaphoreTake(controlMutex, pdMS_TO_TICKS(WATCHDOG_WORKER_CONTROL_TIMEOUT_MS)) != pdTRUE)
    return -1;
  monitorState = SERVICE_STATE_ERROR;
  int32_t result = WatchdogWorker_Stop(&monitorWorker, pdMS_TO_TICKS(WATCHDOG_WORKER_CONTROL_TIMEOUT_MS));
  if (result == 0) monitorState = SERVICE_STATE_STOPPED;
  xSemaphoreGive(controlMutex);
  return result;
}

PerformanceMonitorProxy PerformanceMonitor = {
    .base = {.Initialize = Perf_Service_Initialize,
             .Start = Perf_Service_Start,
             .Stop = Perf_Service_Stop,
             .GetState = Perf_Service_GetState,
             .CheckHealth = Perf_Service_CheckHealth},
    .Init = Perf_Init,
    .InitializeWithConfig = Perf_InitializeWithConfig,
    .StartMonitoring = Perf_StartMonitoring,
    .GetCpuUsage = Perf_GetCpuUsage};
