#include "Core/Localization/Localization.h"
#include "Communication/DebugConsole/Commands/PerformanceCommands.h"
#include "Communication/DebugConsole/Core/ConsoleColors.h"
#include "Communication/DebugConsole/Core/IDashboard.h"
#include "Communication/DebugConsole/Core/DashboardManager.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "Core/ServiceLocator/ServiceLocator.h"
#include "Core/ServiceLocator/ServiceNames.h"
#include "Hardware/InternalTemperatureSensor/IInternalTemperatureSensor.h"
#include <stdio.h>
#include <stdint.h>
#include <strings.h>

// --- CPU Sicaklik ---

static float read_cpu_temperature(void) {
    IInternalTemperatureSensor *sensor = (IInternalTemperatureSensor *)ServiceLocator_GetService(SVC_NAME_INTERNAL_TEMP_SENSOR);
    if (sensor && sensor->ReadTemperature) {
        return sensor->ReadTemperature(sensor);
    }
    return -999.0f;
}

static int cmd_cputemp(int argc, char **argv) {
    const LocalizationCatalog *catalog = Localization_Snapshot();
    float temp = read_cpu_temperature();
    if (temp > -900.0f) {
        LOC_PRINTF(catalog, LOC_CLI_PERF_TEMPERATURE, "\nCPU Temperature: %.1f °C\n", temp);
    } else {
        LOC_PRINTF(catalog, LOC_CLI_PERF_TEMPERATURE_FAILED, "CPU temperature unavailable.\n");
    }
    return 0;
}

// --- Uptime ---

static int cmd_uptime(int argc, char **argv) {
    const LocalizationCatalog *catalog = Localization_Snapshot();
    uint32_t total_s = (uint32_t)(esp_timer_get_time() / 1000000);
    uint32_t days = total_s / 86400;
    uint32_t hours = (total_s % 86400) / 3600;
    uint32_t mins = (total_s % 3600) / 60;
    uint32_t secs = total_s % 60;

    LOC_PRINTF(catalog, LOC_CLI_PERF_UPTIME_TITLE, "\nSystem Uptime: ");
    if (days > 0) {
        LOC_PRINTF(catalog, LOC_CLI_PERF_UPTIME_DAYS, "%lu days %lu hours %lu min %lu sec", (unsigned long)days, (unsigned long)hours, (unsigned long)mins, (unsigned long)secs);
    } else {
        printf("%02lu:%02lu:%02lu", (unsigned long)hours, (unsigned long)mins, (unsigned long)secs);
    }
    printf("\n" CLI_COLOR_RESET);
    return 0;
}

// --- Heap Watch ---

static int cmd_heapwatch(int argc, char **argv) {
    const LocalizationCatalog *catalog = Localization_Snapshot();
    size_t free_heap = esp_get_free_heap_size();
    size_t min_heap = esp_get_minimum_free_heap_size();
    size_t internal_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    size_t largest_block = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);

    LOC_PRINTF(catalog, LOC_CLI_PERF_HEAP_TITLE, "\n--- Heap Analysis ---\n");
    LOC_PRINTF(catalog, LOC_CLI_PERF_HEAP_FREE, " Total Free Heap:       %7u bytes\n", (unsigned int)free_heap);
    LOC_PRINTF(catalog, LOC_CLI_PERF_HEAP_MIN, " Min. Free Heap:        %7u bytes\n", (unsigned int)min_heap);
    LOC_PRINTF(catalog, LOC_CLI_PERF_HEAP_INTERNAL, " Internal Free Heap:    %7u bytes\n", (unsigned int)internal_free);
    LOC_PRINTF(catalog, LOC_CLI_PERF_HEAP_LARGEST, " Largest Free Block:    %7u bytes\n", (unsigned int)largest_block);

    // Fragmentation ratio
    if (internal_free > 0) {
        float frag = 1.0f - ((float)largest_block / (float)internal_free);
        const char *color = (frag < 0.3f) ? CLI_COLOR_GREEN : (frag < 0.6f) ? CLI_COLOR_YELLOW : CLI_COLOR_RED;
        LOC_PRINTF(catalog, LOC_CLI_PERF_FRAGMENTATION, " Fragmentation:         %s%.1f%%\n", color, frag * 100.0f);
    }

    // Heap exhaustion risk
    if (free_heap < 10000) {
        LOC_PRINTF(catalog, LOC_CLI_PERF_CRITICAL, " WARNING: Critical heap level!\n");
    } else if (free_heap < 30000) {
        LOC_PRINTF(catalog, LOC_CLI_PERF_LOW, " Warning: Low heap level.\n");
    }

    printf(CLI_BOLD "--------------------\n" CLI_COLOR_RESET);
    return 0;
}

// --- Dashboard Provider ---

static void provider_cpu_temp(char *buffer, size_t max_len) {
    float temp = read_cpu_temperature();
    if (temp > -900.0f) {
        snprintf(buffer, max_len, "%.1f C", temp);
    } else {
        snprintf(buffer, max_len, "N/A");
    }
}

// --- Registration ---

static int cmd_perf(int argc, char **argv) {
    const LocalizationCatalog *catalog = Localization_Snapshot();
    if (argc < 2) {
        LOC_PRINTF(catalog, LOC_CLI_PERF_USAGE, "Usage: PERF <subcommand>\n");
        LOC_PRINTF(catalog, LOC_CLI_PERF_COMMANDS, "Subcommands: CPU, UPTIME, HEAP\n");
        return 1;
    }

    if (strcasecmp(argv[1], "CPU") == 0) return cmd_cputemp(argc - 1, &argv[1]);
    if (strcasecmp(argv[1], "UPTIME") == 0) return cmd_uptime(argc - 1, &argv[1]);
    if (strcasecmp(argv[1], "HEAP") == 0) return cmd_heapwatch(argc - 1, &argv[1]);

    LOC_PRINTF(catalog, LOC_CLI_PERF_UNKNOWN, "Unknown subcommand: %s\n", argv[1]);
    return 1;
}

void PerformanceCommands_Register(IDebugConsole* console) {
    if (!console) return;

    static const esp_console_cmd_t perf_cmds[] = {
        {.command = "PERFORMANCE", .help = "Performans izleme komutlari", .func = &cmd_perf, .hint = "<subcmd>"},
        {.command = "PERF",        .help = "Performans izleme komutlari (kisa)", .func = &cmd_perf, .hint = "<subcmd>"},
    };

    for (int i = 0; i < sizeof(perf_cmds) / sizeof(perf_cmds[0]); i++) {
        console->RegisterCommandWithCategory(console, &perf_cmds[i], CMD_CAT_PERFORMANCE);
    }

    // Register the CPU temperature dashboard field
    IDashboard *dash = DashboardManager_GetModule();
    if (dash) {
        dash->RegisterField(dash, "CPU TEMP", provider_cpu_temp);
    }
}
