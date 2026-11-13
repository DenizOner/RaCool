#include "Communication/DebugConsole/Commands/DiagnosticCommands.h"
#include "Communication/DebugConsole/Core/ConsoleColors.h"
#include "Communication/DebugConsole/Core/IDashboard.h"
#include "Communication/DebugConsole/Core/DashboardManager.h"
#include "Management/DiagnosticsManager/IDiagnosticsManager.h"
#include "Core/ServiceLocator/ServiceLocator.h"
#include "Core/ServiceLocator/ServiceNames.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "Core/Localization/Localization.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool DiagnosticArguments(int argc, char **argv, int minimum, int maximum) {
    if (!argv || argc < minimum || argc > maximum) return false;
    for (int i = 0; i < argc; ++i) if (!argv[i] || !argv[i][0]) return false;
    return true;
}

static int cmd_diag_run(int argc, char **argv, const LocalizationCatalog *catalog) {
    if (!DiagnosticArguments(argc, argv, 1, 2) || (argc == 2 && strcmp(argv[1], "--machine") != 0)) return 1;
    IDiagnosticsManager *dm = (IDiagnosticsManager *)ServiceLocator_GetService(SVC_NAME_DIAGNOSTICS_MANAGER);
    if (!dm) {
        LOC_PRINTF(catalog, LOC_CLI_DIAG_MISSING_SERVICE,  "Error: DiagnosticsManager service not found!\n" );
        return 1;
    }

    if (argc == 1) LOC_PRINTF(catalog, LOC_CLI_DIAG_STARTING,  "Starting asynchronous self-test...\n" );
    if (!dm->StartAsyncSelfTest) return 1;
    if (dm->StartAsyncSelfTest(dm) == DIAG_OK) {
        if (argc == 2) printf("RACOOL_CLI v=1 type=diagnostic_run result=accepted\n");
        else LOC_PRINTF(catalog, LOC_CLI_DIAG_STARTED,  "Self-test triggered successfully. Results logged to console.\n" );
    } else {
        if (argc == 2) printf("RACOOL_CLI v=1 type=diagnostic_run result=rejected\n");
        else LOC_PRINTF(catalog, LOC_CLI_DIAG_START_FAILED,  "Error: Could not start self-test (Busy or service not started).\n" );
        return 1;
    }
    return 0;
}

static int cmd_diag_status(int argc, char **argv, const LocalizationCatalog *catalog) {
    if (!DiagnosticArguments(argc, argv, 1, 2) || (argc == 2 && strcmp(argv[1], "--machine") != 0)) return 1;
    IDiagnosticsManager *dm = (IDiagnosticsManager *)ServiceLocator_GetService(SVC_NAME_DIAGNOSTICS_MANAGER);
    if (!dm) {
        LOC_PRINTF(catalog, LOC_CLI_DIAG_MISSING_SERVICE,  "Error: DiagnosticsManager service not found!\n" );
        return 1;
    }

    if (!dm->GetLastTestResult || !dm->GetSystemUptime || !dm->GetFreeHeap) return 1;
    DiagnosticsTestResult res = dm->GetLastTestResult(dm);
    int32_t score = res.health_score;
    if (argc == 2) {
        const bool known = res.completed && res.services_checked && score >= 0 && score <= 100;
        printf("RACOOL_CLI v=1 type=diagnostic_status known=%u score=%ld running=%u checked=%lu healthy=%lu\n",
            known ? 1u : 0u, (long)score, res.is_running ? 1u : 0u,
            (unsigned long)res.services_checked, (unsigned long)res.services_healthy);
        return 0;
    }

    LOC_PRINTF(catalog, LOC_CLI_DIAG_STATUS_TITLE, "\n"  "--- System Health Status ---\n" );
    
    const char *score_color = (score < 50) ? CLI_COLOR_RED : (score < 80) ? CLI_COLOR_YELLOW : CLI_COLOR_GREEN;
    if (res.completed && res.services_checked) LOC_PRINTF(catalog, LOC_CLI_DIAG_HEALTH_SCORE, " Health Score:       %s%ld/100%s\n", score_color, (long)score, CLI_COLOR_RESET);
    else LOC_PRINTF(catalog, LOC_CLI_DIAG_HEALTH_UNKNOWN, " Health Score:       UNKNOWN (no completed checks)\n");
    LOC_PRINTF(catalog, LOC_CLI_DIAG_STATE, " Status:             %s\n", Localization_Text(catalog, res.is_running ? LOC_CLI_DIAG_RUNNING : LOC_CLI_DIAG_IDLE));
    LOC_PRINTF(catalog, LOC_CLI_DIAG_LAST_TEST, " Last Test Time:     %lu sec ago\n", (unsigned long)(dm->GetSystemUptime(dm) - res.last_test_time));
    LOC_PRINTF(catalog, LOC_CLI_DIAG_SERVICES, " Services Checked:   %lu Total / %lu Healthy\n", (unsigned long)res.services_checked, (unsigned long)res.services_healthy);
    
    if (res.last_failed_service[0]) {
        LOC_PRINTF(catalog, LOC_CLI_DIAG_LAST_FAILURE, " Last Failure:       "  "%s"  "\n", res.last_failed_service);
    }
    
    LOC_PRINTF(catalog, LOC_CLI_DIAG_FREE_HEAP, " Free Heap:          %lu bytes\n", (unsigned long)dm->GetFreeHeap(dm));
    printf("---------------------------\n");
    
    return 0;
}

static int cmd_diag_metrics(int argc, char **argv, const LocalizationCatalog *catalog) {
    if (!DiagnosticArguments(argc, argv, 1, 2) || (argc==2 && strcmp(argv[1],"--machine"))) return 1;
    IDiagnosticsManager *dm = (IDiagnosticsManager *)ServiceLocator_GetService(SVC_NAME_DIAGNOSTICS_MANAGER);
    if (!dm) {
        LOC_PRINTF(catalog, LOC_CLI_DIAG_MISSING_SERVICE,  "Error: DiagnosticsManager service not found!\n" );
        return 1;
    }

    if (!dm->GetDetailedMetrics) return 1;
    DiagnosticsDetailedMetrics m = dm->GetDetailedMetrics(dm);
    if (argc==2) {
        printf("RACOOL_CLI v=1 type=diagnostic_metrics free_heap=%lu min_free_heap=%lu largest_block=%lu uptime_s=%lu tasks=%lu score=%ld known=%u\n",
            (unsigned long)m.free_heap,(unsigned long)m.minimum_free_heap,(unsigned long)m.largest_free_block,
            (unsigned long)m.uptime_seconds,(unsigned long)m.task_count,(long)m.health_score,
            m.health_status!=DIAG_HEALTH_UNKNOWN && m.health_score>=0 && m.health_score<=100 ? 1u : 0u);
        return 0;
    }

    LOC_PRINTF(catalog, LOC_CLI_DIAG_METRICS_TITLE, "\n"  "--- System Detailed Metrics ---\n" );
    LOC_PRINTF(catalog, LOC_CLI_DIAG_METRICS_FREE, " Free Heap:            %lu bytes\n", (unsigned long)m.free_heap);
    LOC_PRINTF(catalog, LOC_CLI_DIAG_METRICS_MIN, " Min Free Heap:        %lu bytes\n", (unsigned long)m.minimum_free_heap);
    LOC_PRINTF(catalog, LOC_CLI_DIAG_METRICS_BLOCK, " Max Free Block:       %lu bytes\n", (unsigned long)m.largest_free_block);
    LOC_PRINTF(catalog, LOC_CLI_DIAG_TASKS, " Active Tasks Count:   %lu\n", (unsigned long)m.task_count);
    LOC_PRINTF(catalog, LOC_CLI_DIAG_UPTIME, " System Uptime:        %lu seconds\n", (unsigned long)m.uptime_seconds);
    if (m.health_status == DIAG_HEALTH_UNKNOWN) LOC_PRINTF(catalog, LOC_CLI_DIAG_METRICS_UNKNOWN, " Health Score:         UNKNOWN\n");
    else LOC_PRINTF(catalog, LOC_CLI_DIAG_METRICS_HEALTH, " Health Score:         %ld/100\n", (long)m.health_score);
    printf("-------------------------------\n");
    return 0;
}

static int cmd_diag_periodic(int argc, char **argv, const LocalizationCatalog *catalog) {
    if (!DiagnosticArguments(argc, argv, 2, 3)) return 1;
    IDiagnosticsManager *dm = (IDiagnosticsManager *)ServiceLocator_GetService(SVC_NAME_DIAGNOSTICS_MANAGER);
    if (!dm) {
        LOC_PRINTF(catalog, LOC_CLI_DIAG_MISSING_SERVICE,  "Error: DiagnosticsManager service not found!\n" );
        return 1;
    }

    if (!argv || argc < 2 || !dm->SetPeriodicTest) return 1;
    bool enable;
    uint32_t ms = 0;
    if (argc == 2 && strcasecmp(argv[1], "ENABLE") == 0) enable = true;
    else if (argc == 2 && strcasecmp(argv[1], "DISABLE") == 0) enable = false;
    else if (argc == 3 && strcasecmp(argv[1], "SET_INTERVAL") == 0) {
        if (!argv[2] || !argv[2][0]) return 1;
        for (const unsigned char *p = (const unsigned char *)argv[2]; *p; ++p) {
            if (*p < '0' || *p > '9' || ms > (DIAGNOSTICS_MAX_PERIOD_MS - (*p - '0')) / 10) {
                LOC_PRINTF(catalog, LOC_CLI_DIAG_INTERVAL_RANGE, "Interval must be a whole number from %u to %u ms.\n",
                       (unsigned)DIAGNOSTICS_MIN_PERIOD_MS, (unsigned)DIAGNOSTICS_MAX_PERIOD_MS);
                return 1;
            }
            ms = ms * 10 + (*p - '0');
        }
        if (ms < DIAGNOSTICS_MIN_PERIOD_MS) return 1;
        enable = true;
    } else {
        LOC_PRINTF(catalog, LOC_CLI_DIAG_PERIODIC_USAGE, "Usage: DIAG PERIODIC <ENABLE|DISABLE|SET_INTERVAL <MS>>\n");
        return 1;
    }
    int32_t result = dm->SetPeriodicTest(dm, enable, ms);
    if (result != DIAG_OK) {
        LOC_PRINTF(catalog, LOC_CLI_DIAG_PERIODIC_FAILED, "Periodic diagnostic setting was not applied: %ld\n", (long)result);
        return 1;
    }
    LOC_PRINTF(catalog, LOC_CLI_DIAG_PERIODIC_RESULT, "Periodic diagnostic checks %s%s\n", Localization_Text(catalog, enable ? LOC_CLI_DIAG_ENABLED : LOC_CLI_DIAG_DISABLED),
           ms ? Localization_Text(catalog, LOC_CLI_DIAG_REQUESTED_INTERVAL) : "");

    return 0;
}

static int cmd_diag_heap(int argc, char **argv, const LocalizationCatalog *catalog) {
    if (!DiagnosticArguments(argc, argv, 1, 1)) return 1;
    LOC_PRINTF(catalog, LOC_CLI_DIAG_HEAP_TITLE, "\n"  "--- Heap Memory Diagnostics ---\n" );
    LOC_PRINTF(catalog, LOC_CLI_DIAG_HEAP_TOTAL, " Total Free Heap:      %lu bytes\n", (unsigned long)esp_get_free_heap_size());
    LOC_PRINTF(catalog, LOC_CLI_DIAG_HEAP_MIN, " Minimum Free Heap:    %lu bytes\n", (unsigned long)esp_get_minimum_free_heap_size());
    LOC_PRINTF(catalog, LOC_CLI_DIAG_HEAP_BLOCK, " Largest Free Block:   %lu bytes\n", (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    LOC_PRINTF(catalog, LOC_CLI_DIAG_HEAP_INTERNAL, " Internal Free Heap:   %lu bytes\n", (unsigned long)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    printf("-------------------------------\n");
    return 0;
}

static int cmd_diag(int argc, char **argv) {
    const LocalizationCatalog *catalog = Localization_Snapshot();
    if (!DiagnosticArguments(argc, argv, 2, 4)) {
        LOC_PRINTF(catalog, LOC_CLI_DIAG_USAGE, "Usage: DIAG <RUN|STATUS|METRICS|PERIODIC|HEAP>\n");
        return 1;
    }

    if (strcasecmp(argv[1], "RUN") == 0) return cmd_diag_run(argc - 1, &argv[1], catalog);
    if (strcasecmp(argv[1], "STATUS") == 0) return cmd_diag_status(argc - 1, &argv[1], catalog);
    if (strcasecmp(argv[1], "METRICS") == 0) return cmd_diag_metrics(argc - 1, &argv[1], catalog);
    if (strcasecmp(argv[1], "PERIODIC") == 0) return cmd_diag_periodic(argc - 1, &argv[1], catalog);
    if (strcasecmp(argv[1], "HEAP") == 0) return cmd_diag_heap(argc - 1, &argv[1], catalog);

    LOC_PRINTF(catalog, LOC_CLI_DIAG_UNKNOWN_COMMAND,  "Unknown subcommand: %s\n" , argv[1]);
    return 1;
}

static void provider_health(char *buffer, size_t max_len) {
    IDiagnosticsManager *dm = (IDiagnosticsManager *)ServiceLocator_GetService(SVC_NAME_DIAGNOSTICS_MANAGER);
    if (!dm || !dm->GetLastTestResult) {
        snprintf(buffer, max_len, "N/A");
        return;
    }
    DiagnosticsTestResult result = dm->GetLastTestResult(dm);
    if (!result.completed || !result.services_checked) { snprintf(buffer, max_len, "N/A"); return; }
    int32_t score = result.health_score;
    const char *score_color = (score < 50) ? CLI_COLOR_RED : (score < 80) ? CLI_COLOR_YELLOW : CLI_COLOR_GREEN;
    snprintf(buffer, max_len, "%s%ld/100%s", score_color, (long)score, CLI_COLOR_RESET);
}

esp_err_t DiagnosticCommands_Register(IDebugConsole *console) {
    if (!console || !console->RegisterCommandWithCategory) return ESP_ERR_INVALID_ARG;

    static const esp_console_cmd_t diag_cmds[] = {
        {.command = "DIAG", .help = "System diagnostic and telemetry commands", .func = &cmd_diag, .hint = "<RUN|STATUS|METRICS|PERIODIC|HEAP>"}
    };

    for (int i = 0; i < (int)(sizeof(diag_cmds) / sizeof(diag_cmds[0])); i++) {
        esp_err_t result = console->RegisterCommandWithCategory(console, &diag_cmds[i], CMD_CAT_DIAGNOSTIC);
        if (result != ESP_OK) return result;
    }
    
    IDashboard *dash = DashboardManager_GetModule();
    if (dash) {
        if (!dash->RegisterField || !dash->RegisterField(dash, "HEALTH", provider_health)) return ESP_FAIL;
    }
    return ESP_OK;
}

