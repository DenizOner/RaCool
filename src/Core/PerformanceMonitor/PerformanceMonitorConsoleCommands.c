#include "Core/Localization/Localization.h"
#include <string.h>
#include "Core/PerformanceMonitor/IPerformanceMonitor.h"
#include "Core/PerformanceMonitor/PerformanceMonitorConsoleCommands.h"
#include "Core/ServiceLocator/ServiceLocator.h"
#include "Core/ServiceLocator/ServiceNames.h"
#include "Communication/DebugConsole/IDebugConsole.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "esp_timer.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>


static int cmd_perf_status(int argc, char **argv);
static int cmd_perf_start(int argc, char **argv);
static int cmd_perf_stop(int argc, char **argv);

esp_err_t PerformanceMonitor_RegisterConsoleCommands(void) {

    IDebugConsole *console = (IDebugConsole *)ServiceLocator_GetService(SVC_NAME_DEBUG_CONSOLE);
    if (!console || !console->RegisterCommandWithCategory) return ESP_ERR_INVALID_STATE;

    static const esp_console_cmd_t cmds[] = {
        { .command = "perf_status", .help = "Show heap stats and system uptime",          .func = &cmd_perf_status, .hint = NULL },
        { .command = "perf_start",  .help = "Start the performance monitoring task",       .func = &cmd_perf_start,  .hint = NULL },
        { .command = "perf_stop",   .help = "Stop the performance monitoring task",        .func = &cmd_perf_stop,   .hint = NULL },
    };

    for (size_t i = 0; i < sizeof(cmds) / sizeof(cmds[0]); i++) {
        esp_err_t result = console->RegisterCommandWithCategory(console, &cmds[i], CMD_CAT_SYSTEM);
        if (result != ESP_OK) return result;
    }
    return ESP_OK;
}

static int cmd_perf_status(int argc, char **argv) {
    const LocalizationCatalog *catalog = Localization_Snapshot();
    const bool machine = argc == 2 && argv && argv[1] && strcmp(argv[1], "--machine") == 0;
    (void)argv;
    if (argc != 1 && !machine) return 1;
    if (machine) {
        printf("RACOOL_CLI v=1 type=performance_status state=%d\n", (int)PerformanceMonitor.base.GetState(&PerformanceMonitor.base));
        return 0;
    }
    uint32_t free_heap     = esp_get_free_heap_size();
    uint32_t min_free_heap = esp_get_minimum_free_heap_size();
    size_t   internal_free = heap_caps_get_free_size(MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
    uint64_t uptime_ms     = esp_timer_get_time() / 1000;

    LOC_PRINTF(catalog, LOC_CLI_PERF_MONITOR_TITLE, "\n--- PerformanceMonitor Status ---\n");
    LOC_PRINTF(catalog, LOC_CLI_PERF_MONITOR_STATE, "State             : %d\n", (int)PerformanceMonitor.base.GetState(&PerformanceMonitor.base));
    LOC_PRINTF(catalog, LOC_CLI_PERF_MONITOR_HEAP, "Free Heap         : %lu bytes\n",  (unsigned long)free_heap);
    LOC_PRINTF(catalog, LOC_CLI_PERF_MONITOR_MINIMUM, "Min Free Heap     : %lu bytes\n",  (unsigned long)min_free_heap);
    LOC_PRINTF(catalog, LOC_CLI_PERF_MONITOR_INTERNAL, "Internal RAM Free : %u bytes\n",   (unsigned int)internal_free);
    LOC_PRINTF(catalog, LOC_CLI_PERF_MONITOR_UPTIME, "System Uptime     : %llu ms\n",    (unsigned long long)uptime_ms);

    CpuUsageSnapshot cpu;
    if (PerformanceMonitor.GetCpuUsage && PerformanceMonitor.GetCpuUsage(&cpu) == ESP_OK) {
        LOC_PRINTF(catalog, LOC_CLI_PERF_MONITOR_CPU, "CPU Usage         : %u.%02u%%; window=%lu us; measured=%llu us\n",
               (unsigned)(cpu.busy_hundredths_percent / 100), (unsigned)(cpu.busy_hundredths_percent % 100),
               (unsigned long)cpu.window_us, (unsigned long long)cpu.measured_time_us);
    } else LOC_PRINTF(catalog, LOC_CLI_PERF_MONITOR_CPU_UNKNOWN, "CPU Usage         : unavailable\n");
    printf("---------------------------------\n");
    return 0;
}

static int cmd_perf_start(int argc, char **argv) {
    const LocalizationCatalog *catalog = Localization_Snapshot();
    const bool machine = argc == 2 && argv && argv[1] && strcmp(argv[1], "--machine") == 0;
    (void)argv;
    if (argc != 1 && !machine) return 1;
    const int32_t result = PerformanceMonitor.base.Start(&PerformanceMonitor.base);
    if (machine) printf("RACOOL_CLI v=1 type=performance_start result=%s\n", result == 0 ? "applied" : "rejected");
    if (result != 0) return 1;
    if (machine) return 0;
    LOC_PRINTF(catalog, LOC_CLI_PERF_MONITOR_STARTED, "Performance monitoring started.\n");
    return 0;
}

static int cmd_perf_stop(int argc, char **argv) {
    const LocalizationCatalog *catalog = Localization_Snapshot();
    const bool machine = argc == 2 && argv && argv[1] && strcmp(argv[1], "--machine") == 0;
    (void)argv;
    if (argc != 1 && !machine) return 1;
    const int32_t result = PerformanceMonitor.base.Stop(&PerformanceMonitor.base);
    if (machine) printf("RACOOL_CLI v=1 type=performance_stop result=%s\n", result == 0 ? "applied" : "rejected");
    if (result != 0) return 1;
    if (machine) return 0;
    LOC_PRINTF(catalog, LOC_CLI_PERF_MONITOR_STOPPED, "Performance monitoring stopped.\n");
    return 0;
}
