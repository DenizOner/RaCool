#include "Core/Localization/Localization.h"
/**
 * @file SystemControllerConsoleCommands.c
 * @brief Debug Console CLI commands for SystemController Module.
 *
 * Provides sub-commands to inspect system status, change system states,
 * trigger recovery/reset, and run self-tests.
 *
 * @author RaCool DeepMind Team
 * @date 2026-08-08
 */

#include "Controllers/SystemController/SystemControllerConsoleCommands.h"
#include "Controllers/SystemController/ISystemController.h"
#include "Communication/DebugConsole/IDebugConsole.h"
#include "Communication/DebugConsole/Core/ConsoleColors.h"
#include "Core/ServiceLocator/ServiceLocator.h"
#include "esp_log.h"
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#ifndef RACOOL_TEST_NATIVE
#include <esp_console.h>
#endif

static const char *SYS_CMD_TAG = "SystemConsole";

/**
 * @brief Console command handler for SystemController CLI sub-commands.
 *
 * @param argc Number of command line arguments.
 * @param argv Array of argument strings.
 * @return 0 on success, non-zero error code on failure.
 */
static int sys_console_cmd_handler(int argc, char **argv) {
  const LocalizationCatalog *catalog = Localization_Snapshot();
  if (!argv || argc < 1 || argc > 4) return 1;
  for (int i = 0; i < argc; ++i) if (!argv[i]) return 1;
  const bool machine = argc > 1 && strcmp(argv[argc-1], "--machine") == 0;
  if (machine) --argc;
  if (machine && argc > 1 && strcmp(argv[1], "status") != 0 && strcmp(argv[1], "setstate") != 0) {
    printf("RACOOL_CLI v=1 type=system_request result=rejected reason=unsupported\n");
    return 1;
  }

  ISystemController *sc = GetSystemController();
  if (!sc) {
    if (machine) printf("RACOOL_CLI v=1 type=system_status state=-1\n");
    else LOC_PRINTF(catalog, LOC_CLI_SYS_UNAVAILABLE, "System controller unavailable.\n");
    return -1;
  }

  if (argc < 2 || strcmp(argv[1], "status") == 0) {
    if (argc > 2 || !sc->GetSystemState) return 1;
    SystemState st = sc->GetSystemState(sc);
    if (machine) { printf("RACOOL_CLI v=1 type=system_status state=%d\n", (int)st); return 0; }
    LOC_PRINTF(catalog, LOC_CLI_SYS_TITLE, "=== SystemController Status ===\n");
    LOC_PRINTF(catalog, LOC_CLI_SYS_STATE, " Current System State: %d\n", (int)st);
    return 0;
  }

  if (strcmp(argv[1], "setstate") == 0) {
    if (argc != 3) {
      if (machine) { printf("RACOOL_CLI v=1 type=system_state result=rejected state=-1 reason=invalid\n"); return 1; }
      LOC_PRINTF(catalog, LOC_CLI_SYS_USAGE, "Usage: sys setstate <state_id>\n");
      return -1;
    }
    char *end;
    errno = 0;
    long state_id = strtol(argv[2], &end, 10);
    if (errno || end == argv[2] || *end || state_id < 0 || state_id > SYSTEM_STATE_ERROR) {
      if (machine) { printf("RACOOL_CLI v=1 type=system_state result=rejected state=-1 reason=invalid\n"); return 1; }
      LOC_PRINTF(catalog, LOC_CLI_SYS_INVALID, "Invalid system state.\n");
      return -1;
    }
    if (!sc->base.GetState || !sc->SetSystemState || !sc->GetSystemState || sc->base.GetState(&sc->base) != SERVICE_STATE_STARTED) return -1;
    sc->SetSystemState(sc, (SystemState)state_id);
    SystemState expected = state_id == SYSTEM_STATE_ERROR ? SYSTEM_STATE_RECOVERING : (SystemState)state_id;
    if (sc->GetSystemState(sc) != expected) {
      if (machine) { printf("RACOOL_CLI v=1 type=system_state result=rejected state=%d reason=unapplied\n", (int)sc->GetSystemState(sc)); return 1; }
      LOC_PRINTF(catalog, LOC_CLI_SYS_NOT_APPLIED, "System state request was not applied.\n");
      return -1;
    }
    if (machine) { printf("RACOOL_CLI v=1 type=system_state result=applied state=%d\n", (int)expected); return 0; }
    LOC_PRINTF(catalog, LOC_CLI_SYS_APPLIED, "System state is %d\n", (int)expected);
    return 0;
  }

  if (strcmp(argv[1], "reset") == 0) {
    if (argc != 2 || sc->base.GetState(&sc->base) != SERVICE_STATE_STARTED) return -1;
    sc->SetSystemState(sc, SYSTEM_STATE_INITIALIZING);
    if (sc->GetSystemState(sc) != SYSTEM_STATE_INITIALIZING) return -1;
    LOC_PRINTF(catalog, LOC_CLI_SYS_RESET, "System state reset to default (INITIALIZING).\n");
    return 0;
  }

  if (strcmp(argv[1], "test") == 0) {
    if (argc != 2) return -1;
    int32_t res = sc->base.CheckHealth((IService *)sc);
    if (res == 0) {
      LOC_PRINTF(catalog, LOC_CLI_SYS_PASSED, "System controller self-test PASSED.\n");
    } else {
      LOC_PRINTF(catalog, LOC_CLI_SYS_FAILED, "System controller self-test FAILED (%ld).\n", (long)res);
    }
    return res;
  }

  LOC_PRINTF(catalog, LOC_CLI_SYS_UNKNOWN, "Unknown subcommand: %s\n", argv[1]);
  LOC_PRINTF(catalog, LOC_CLI_SYS_SUBCOMMANDS, "Valid subcommands: status, setstate <id>, reset, test\n");
  return -1;
}

esp_err_t SystemController_RegisterConsoleCommands(void) {
  IDebugConsole *console = (IDebugConsole *)ServiceLocator_GetService("DebugConsole");
  if (!console || !console->RegisterCommandWithCategory) return ESP_ERR_INVALID_STATE;
  const esp_console_cmd_t sys_cmd = {
      .command = "sys",
      .help = "SystemController commands (status, setstate, reset, test)",
      .hint = "<status|setstate|reset|test>",
      .func = &sys_console_cmd_handler,
      .argtable = NULL
  };

  esp_err_t result = console->RegisterCommandWithCategory(console, &sys_cmd, CMD_CAT_SYSTEM);
  if (result != ESP_OK) return result;
  ESP_LOGI(SYS_CMD_TAG, "SystemController console commands registered successfully");
  return ESP_OK;
}

