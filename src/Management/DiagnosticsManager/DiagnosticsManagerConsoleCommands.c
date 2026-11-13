#include "Core/Localization/Localization.h"
/**
 * @file DiagnosticsManagerConsoleCommands.c
 * @brief Debug Console CLI commands for DiagnosticsManager Module.
 *
 * @author RaCool DeepMind Team
 * @date 2026-08-01
 */

#include "Management/DiagnosticsManager/DiagnosticsManagerConsoleCommands.h"
#include "Management/DiagnosticsManager/IDiagnosticsManager.h"
#include "Core/ServiceLocator/ServiceLocator.h"
#include "Communication/DebugConsole/IDebugConsole.h"
#include "esp_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef RACOOL_TEST_NATIVE
#include <esp_console.h>
#endif

#ifndef RACOOL_TEST_NATIVE
static const char *DIAG_CMD_TAG = "DiagConsole";
#endif

static int diag_console_cmd_handler(int argc, char **argv) {
    const LocalizationCatalog *catalog = Localization_Snapshot();
  if (!argv || argc < 1 || argc > 2 || !argv[0] || !argv[0][0] ||
      (argc == 2 && (!argv[1] || !argv[1][0]))) return 1;
  IDiagnosticsManager *dm = DiagnosticsManager_GetModule();
  if (!dm) {
    LOC_PRINTF(catalog, LOC_CLI_DIAGNOSTICS_UNAVAILABLE, "[DiagnosticsManager] Service unavailable.\n");
    return -1;
  }

  if (argc == 1 || strcmp(argv[1], "status") == 0) {
    if (!dm->GetDetailedMetrics) return 1;
    DiagnosticsDetailedMetrics m = dm->GetDetailedMetrics(dm);
    LOC_PRINTF(catalog, LOC_CLI_DIAGNOSTICS_TITLE, "--- DiagnosticsManager Status ---\n");
    if (m.health_status == DIAG_HEALTH_UNKNOWN) LOC_PRINTF(catalog, LOC_CLI_DIAGNOSTICS_UNKNOWN, " Health Score : UNKNOWN\n");
    else LOC_PRINTF(catalog, LOC_CLI_DIAGNOSTICS_SCORE, " Health Score : %d%%\n", (int)m.health_score);
    LOC_PRINTF(catalog, LOC_CLI_DIAGNOSTICS_HEAP, " Free Heap    : %lu bytes\n", (unsigned long)m.free_heap);
    LOC_PRINTF(catalog, LOC_CLI_DIAGNOSTICS_HEAP_MIN, " Min Free Heap: %lu bytes\n", (unsigned long)m.minimum_free_heap);
    LOC_PRINTF(catalog, LOC_CLI_DIAGNOSTICS_UPTIME, " Uptime       : %lu s\n", (unsigned long)m.uptime_seconds);
    LOC_PRINTF(catalog, LOC_CLI_DIAGNOSTICS_TASKS, " Active Tasks : %lu\n", (unsigned long)m.task_count);
    return 0;
  }

  if (strcmp(argv[1], "selftest") == 0) {
    if (!dm->StartAsyncSelfTest) return 1;
    LOC_PRINTF(catalog, LOC_CLI_DIAGNOSTICS_START, "Triggering async self-test...\n");
    int32_t res = dm->StartAsyncSelfTest(dm);
    LOC_PRINTF(catalog, LOC_CLI_DIAGNOSTICS_RESULT, "[DiagnosticsManager] Async self-test trigger result: %ld\n", (long)res);
    return res == DIAG_OK ? 0 : 1;
  }

  LOC_PRINTF(catalog, LOC_CLI_UNKNOWN_SUBCOMMAND, "Unknown subcommand: %s\n", argv[1]);
  LOC_PRINTF(catalog, LOC_CLI_DIAGNOSTICS_USAGE, "Valid subcommands: status, selftest\n");
  return -1;
}

esp_err_t DiagnosticsManager_RegisterConsoleCommands(void) {
  IDebugConsole *console = (IDebugConsole *)ServiceLocator_GetService("DebugConsole");
  const esp_console_cmd_t diag_cmd = {
      .command = "diag",
      .help = "DiagnosticsManager commands (status, selftest)",
      .hint = "<status|selftest>",
      .func = &diag_console_cmd_handler,
      .argtable = NULL
  };

  /* Registration belongs to the console registry; do not bypass its ownership
   * and error handling with a second raw SDK registration path. */
  if (!console || !console->RegisterCommandWithCategory) return ESP_ERR_INVALID_STATE;
  esp_err_t result = console->RegisterCommandWithCategory(console, &diag_cmd, CMD_CAT_DIAGNOSTIC);
  if (result != ESP_OK) return result;
#ifndef RACOOL_TEST_NATIVE
  ESP_LOGI(DIAG_CMD_TAG, "DiagnosticsManager console commands registered successfully");
#endif
  return ESP_OK;
}
