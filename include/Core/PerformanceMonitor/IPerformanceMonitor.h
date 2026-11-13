#ifndef IPERFORMANCE_MONITOR_H
#define IPERFORMANCE_MONITOR_H

#include "Core/IService.h"
#include "Core/PerformanceMonitor/PerformanceMonitorConfig.h"
#include <esp_err.h>

typedef struct {
  uint16_t busy_hundredths_percent;
  uint64_t measured_time_us;
  uint32_t window_us;
} CpuUsageSnapshot;

typedef struct {
  IService base;
  esp_err_t (*Init)(void);
  esp_err_t (*InitializeWithConfig)(PerformanceMonitorConfig config);
  void (*StartMonitoring)(void);
  /** Returns NOT_FINISHED until two coherent runtime samples; preserves output on error. */
  esp_err_t (*GetCpuUsage)(CpuUsageSnapshot *out);
} PerformanceMonitorProxy;

extern PerformanceMonitorProxy PerformanceMonitor;

#endif // IPERFORMANCE_MONITOR_H
