#ifndef PERFORMANCE_MONITOR_CONFIG_H
#define PERFORMANCE_MONITOR_CONFIG_H

#include <stdbool.h>
#include <stdint.h>

#define PERF_MONITOR_MIN_INTERVAL_MS 1U
#define PERF_MONITOR_MAX_INTERVAL_MS 86400000U
#define PERF_MONITOR_MIN_STACK_SIZE 2048U
#define PERF_MONITOR_MAX_STACK_SIZE 65536U
#define PERF_MONITOR_MAX_TASK_SNAPSHOT 64U
#define PERF_MONITOR_CPU_WINDOW_US 1000000ULL

/**
 * @brief Configuration structure for the PerformanceMonitor
 */
typedef struct {
  uint32_t reportIntervalMs; ///< Interval between performance reports in
                             ///< milliseconds
  bool enableCpuUsage;       ///< Enable measured CPU utilization and system uptime output
  bool enableMemUsage;       ///< Whether to monitor and report Memory usage
  bool enableTaskStats;      ///< Whether to monitor and report Task statistics
  uint32_t taskStackSize;    ///< Stack size for the performance monitor task
  uint32_t taskPriority;     ///< Priority for the performance monitor task
} PerformanceMonitorConfig;

/**
 * @brief Get the default configuration for the PerformanceMonitor
 * @return Constant PerformanceMonitorConfig struct with default values
 */
PerformanceMonitorConfig PerformanceMonitorConfig_GetDefault(void);

#endif // PERFORMANCE_MONITOR_CONFIG_H
