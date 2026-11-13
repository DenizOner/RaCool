#ifndef PERFORMANCE_MONITOR_CONSOLE_COMMANDS_H
#define PERFORMANCE_MONITOR_CONSOLE_COMMANDS_H

#include <esp_err.h>

/**
 * @brief Register all PerformanceMonitor CLI commands into the Debug Console.
 *
 * Commands registered (CMD_CAT_SYSTEM):
 *   perf_status  - Show PerformanceMonitor state and heap statistics
 *   perf_start   - Start the performance monitoring task
 *   perf_stop    - Stop the performance monitoring task
 *
 * Safe to call multiple times — registration is idempotent.
 */
/* Return the first registration error; partial entries remain console-owned. */
esp_err_t PerformanceMonitor_RegisterConsoleCommands(void);

#endif // PERFORMANCE_MONITOR_CONSOLE_COMMANDS_H
