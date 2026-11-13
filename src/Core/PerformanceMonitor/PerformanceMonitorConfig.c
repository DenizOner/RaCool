#include "Core/PerformanceMonitor/PerformanceMonitorConfig.h"

PerformanceMonitorConfig PerformanceMonitorConfig_GetDefault(void) {
  PerformanceMonitorConfig config = {
      .reportIntervalMs = 5000, // 5 seconds
      .enableCpuUsage = true,
      .enableMemUsage = true,
      .enableTaskStats =
          false, // Disabled by default as it's more resource-intensive
      .taskStackSize = 4096,
      .taskPriority = 1 // Low priority
  };
  return config;
}
