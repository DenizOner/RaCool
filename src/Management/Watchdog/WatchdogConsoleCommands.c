/**
 * @file WatchdogConsoleCommands.c
 * @brief Debug Console CLI commands for the Watchdog Management Module.
 *
 * Registers the "watchdog" command with the following sub-commands:
 *
 *  watchdog [status]          – Print full status and statistics.
 *  watchdog feed              – Manually feed the TWDT once.
 *  watchdog selftest          – Execute the built-in self-test suite.
 *  watchdog config [timeout]  – Print or set the TWDT timeout (ms).
 *  watchdog health            – Run CheckHealth and report the result.
 *  watchdog tasks             – Print the number of registered tasks.
 *  watchdog reset             – Deregister all tasks and re-initialise.
 *
 * @author RaCool DeepMind Team
 * @date 2026-08-01
 */

#include "Management/Watchdog/WatchdogConsoleCommands.h"
#include "Management/Watchdog/IWatchdog.h"
#include "Core/ServiceLocator/ServiceLocator.h"
#include "Core/ServiceLocator/ServiceNames.h"
#include "Communication/DebugConsole/IDebugConsole.h"
#include "esp_log.h"
#include "Core/Localization/Localization.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *WD_CMD_TAG = "WatchdogConsole";

/* -----------------------------------------------------------------------
 * Helper: retrieve the watchdog service or print an error and bail out
 * --------------------------------------------------------------------- */

/**
 * @brief Resolves the IWatchdog service from the ServiceLocator.
 *
 * Prints an error to stdout when the service is unavailable so that the
 * CLI gives immediate feedback without crashing.
 *
 * @return Pointer to IWatchdog, or NULL if not registered.
 */
static IWatchdog *WD_GetService(void) {
    const LocalizationCatalog *catalog = Localization_Snapshot();
    IWatchdog *wd = (IWatchdog *)ServiceLocator_GetService(SVC_NAME_WATCHDOG);
    if (wd == NULL) {
        LOC_PRINTF(catalog, LOC_CLI_WATCHDOG_UNAVAILABLE, "ERROR: Watchdog service not registered or available\n");
    }
    return wd;
}

/* -----------------------------------------------------------------------
 * Sub-command handlers
 * --------------------------------------------------------------------- */

/**
 * @brief Prints full watchdog status and statistics.
 *
 * @param wd Pointer to an already-resolved IWatchdog instance.
 * @return   0 on success.
 */
static int WD_CmdStatus(IWatchdog *wd) {
    const LocalizationCatalog *catalog = Localization_Snapshot();
    WatchdogStats_t stats;
    memset(&stats, 0, sizeof(stats));
    if (!wd->GetStats || !wd->IsActive || !wd->GetTimeout || !wd->GetRegisteredTaskCount ||
        wd->GetStats(wd, &stats) != 0) return -1;

    LOC_PRINTF(catalog, LOC_CLI_WATCHDOG_TITLE, "--- Watchdog Module Status ---\n");
    LOC_PRINTF(catalog, LOC_CLI_WATCHDOG_ACTIVE, "  Active                 : %s\n",
           Localization_Text(catalog, wd->IsActive(wd) ? LOC_COMMON_YES : LOC_COMMON_NO));
    LOC_PRINTF(catalog, LOC_CLI_WATCHDOG_TIMEOUT, "  Config Timeout         : %lu ms\n",
           (unsigned long)wd->GetTimeout(wd));
    LOC_PRINTF(catalog, LOC_CLI_WATCHDOG_REGISTERED, "  Registered Tasks       : %lu\n",
           (unsigned long)wd->GetRegisteredTaskCount(wd));
    LOC_PRINTF(catalog, LOC_CLI_WATCHDOG_MANUAL_FEEDS, "  Total Feeds (manual)   : %lu\n",
           (unsigned long)stats.totalFeeds);
    LOC_PRINTF(catalog, LOC_CLI_WATCHDOG_AUTO_FEEDS, "  Total Feeds (auto)     : %lu\n",
           (unsigned long)stats.totalAutoFeeds);
    LOC_PRINTF(catalog, LOC_CLI_WATCHDOG_TASKS_ADDED, "  Tasks Added            : %lu\n",
           (unsigned long)stats.totalTasksAdded);
    LOC_PRINTF(catalog, LOC_CLI_WATCHDOG_TASKS_REMOVED, "  Tasks Removed          : %lu\n",
           (unsigned long)stats.totalTasksRemoved);
    LOC_PRINTF(catalog, LOC_CLI_WATCHDOG_ACTIVE_TASKS, "  Active Task Count      : %lu\n",
           (unsigned long)stats.activeTaskCount);
    LOC_PRINTF(catalog, LOC_CLI_WATCHDOG_WARNINGS, "  Total Warnings         : %lu\n",
           (unsigned long)stats.totalWarnings);
    LOC_PRINTF(catalog, LOC_CLI_WATCHDOG_TIMEOUTS, "  Total Timeouts         : %lu\n",
           (unsigned long)stats.totalTimeouts);
    LOC_PRINTF(catalog, LOC_CLI_WATCHDOG_LAST_FEED, "  Last Feed Time         : %llu ms\n",
           (unsigned long long)stats.lastFeedTimeMs);
    LOC_PRINTF(catalog, LOC_CLI_WATCHDOG_LAST_TIMEOUT, "  Last Timeout Time      : %llu ms\n",
           (unsigned long long)stats.lastTimeoutMs);
    return 0;
}

/**
 * @brief Manually feeds the TWDT once.
 *
 * @param wd Pointer to an already-resolved IWatchdog instance.
 * @return   0 on success.
 */
static int WD_CmdFeed(IWatchdog *wd) {
    const LocalizationCatalog *catalog = Localization_Snapshot();
    if (!wd->FeedChecked || wd->FeedChecked(wd) != 0) {
        LOC_PRINTF(catalog, LOC_CLI_WATCHDOG_FEED_FAILED, "ERROR: Watchdog feed failed.\n");
        return -1;
    }
    LOC_PRINTF(catalog, LOC_CLI_WATCHDOG_FEED_APPLIED, "SUCCESS: Watchdog fed manually.\n");
    return 0;
}

/**
 * @brief Runs the built-in self-test and reports the result.
 *
 * @param wd Pointer to an already-resolved IWatchdog instance.
 * @return   0 on pass, -1 on failure.
 */
static int WD_CmdSelfTest(IWatchdog *wd) {
    const LocalizationCatalog *catalog = Localization_Snapshot();
    if (!wd->SelfTest) return -1;
    LOC_PRINTF(catalog, LOC_CLI_WATCHDOG_SELFTEST_START, "Executing Watchdog self-test...\n");
    int32_t res = wd->SelfTest(wd);
    if (res == 0) {
        LOC_PRINTF(catalog, LOC_CLI_WATCHDOG_SELFTEST_PASSED, "PASSED: Watchdog self-test completed successfully.\n");
        return 0;
    }
    LOC_PRINTF(catalog, LOC_CLI_WATCHDOG_SELFTEST_FAILED, "FAILED: Watchdog self-test failed with code: %ld\n", (long)res);
    return -1;
}

/**
 * @brief Prints or updates the TWDT timeout.
 *
 * When argc > 0 the first element of argv is parsed as the new timeout in
 * milliseconds; otherwise the current value is printed.
 *
 * @param wd   Pointer to an already-resolved IWatchdog instance.
 * @param argc Number of additional arguments following "config".
 * @param argv Pointer to the argument array starting after "config".
 * @return     0 on success, -1 on validation or apply failure.
 */
static int WD_CmdConfig(IWatchdog *wd, int argc, char **argv) {
    const LocalizationCatalog *catalog = Localization_Snapshot();
    if (argc > 0) {
        if (argc != 1 || !argv || !argv[0] || !*argv[0] || !wd->UpdateTimeout) return -1;
        uint32_t new_timeout = 0;
        for (const unsigned char *p = (const unsigned char *)argv[0]; *p; ++p) {
            if (*p < '0' || *p > '9' || new_timeout > (WATCHDOG_MAX_TIMEOUT_MS - (*p - '0')) / 10u) return -1;
            new_timeout = new_timeout * 10u + (*p - '0');
        }
        if (new_timeout < WATCHDOG_MIN_TIMEOUT_MS || new_timeout > WATCHDOG_MAX_TIMEOUT_MS) return -1;
        int32_t res = wd->UpdateTimeout(wd, new_timeout);
        if (res == 0) {
            LOC_PRINTF(catalog, LOC_CLI_WATCHDOG_TIMEOUT_APPLIED, "SUCCESS: Watchdog timeout set to %lu ms\n",
                   (unsigned long)new_timeout);
            return 0;
        }
        LOC_PRINTF(catalog, LOC_CLI_WATCHDOG_CONFIG_FAILED, "ERROR: Failed to update Watchdog config (code=%ld)\n", (long)res);
        return -1;
    }

    if (!wd->GetTimeout) return -1;
    uint32_t timeout = wd->GetTimeout(wd);
    if (!timeout) return -1;
    LOC_PRINTF(catalog, LOC_CLI_WATCHDOG_CURRENT_TIMEOUT, "Current Watchdog timeout: %lu ms\n",
           (unsigned long)timeout);
    return 0;
}

/**
 * @brief Runs CheckHealth and prints a human-readable result.
 *
 * @param wd Pointer to an already-resolved IWatchdog instance.
 * @return   0 if healthy, -1 otherwise.
 */
static int WD_CmdHealth(IWatchdog *wd) {
    const LocalizationCatalog *catalog = Localization_Snapshot();
    if (!wd->base.CheckHealth) return -1;
    int32_t health = wd->base.CheckHealth((IService *)wd);
    if (health == 0) {
        LOC_PRINTF(catalog, LOC_CLI_WATCHDOG_HEALTHY, "OK: Watchdog is healthy.\n");
        return 0;
    }
    LOC_PRINTF(catalog, LOC_CLI_WATCHDOG_UNHEALTHY, "FAULT: Watchdog health check failed (code=%ld).\n", (long)health);
    return -1;
}

/**
 * @brief Prints the number of tasks currently registered with the TWDT.
 *
 * @param wd Pointer to an already-resolved IWatchdog instance.
 * @return   0 on success.
 */
static int WD_CmdTasks(IWatchdog *wd) {
    const LocalizationCatalog *catalog = Localization_Snapshot();
    if (!wd->GetRegisteredTaskCount) return -1;
    uint32_t count = wd->GetRegisteredTaskCount(wd);
    LOC_PRINTF(catalog, LOC_CLI_WATCHDOG_TASK_COUNT, "Registered Task Count: %lu\n", (unsigned long)count);
    return 0;
}

/**
 * @brief Triggers a soft watchdog reset by stopping and re-starting the service.
 *
 * This exercises the full Stop → Start lifecycle path and is intended for
 * diagnostic use only.
 *
 * @param wd Pointer to an already-resolved IWatchdog instance.
 * @return   0 on success, -1 on lifecycle error.
 */
static int WD_CmdReset(IWatchdog *wd) {
    const LocalizationCatalog *catalog = Localization_Snapshot();
    if (!wd->base.Stop || !wd->base.Start) return -1;
    LOC_PRINTF(catalog, LOC_CLI_WATCHDOG_RESET_START, "Performing Watchdog service reset...\n");

    int32_t stop_res = wd->base.Stop((IService *)wd);
    if (stop_res != 0) {
        LOC_PRINTF(catalog, LOC_CLI_WATCHDOG_STOP_FAILED, "ERROR: Watchdog stop failed (code=%ld); restart aborted.\n",
               (long)stop_res);
        return -1;
    }

    int32_t start_res = wd->base.Start((IService *)wd);
    if (start_res == 0) {
        LOC_PRINTF(catalog, LOC_CLI_WATCHDOG_RESET_APPLIED, "SUCCESS: Watchdog service reset and restarted.\n");
        return 0;
    }
    LOC_PRINTF(catalog, LOC_CLI_WATCHDOG_RESTART_FAILED, "ERROR: Failed to restart Watchdog service (code=%ld).\n",
           (long)start_res);
    return -1;
}

/* -----------------------------------------------------------------------
 * Dispatcher
 * --------------------------------------------------------------------- */

/**
 * @brief Main command dispatcher for the "watchdog" CLI entry.
 */
static int cmd_watchdog(int argc, char **argv) {
    const LocalizationCatalog *catalog = Localization_Snapshot();
    if (argc < 1 || argc > 3 || !argv) return -1;
    for (int i = 0; i < argc; ++i) if (!argv[i] || !*argv[i]) return -1;
    bool machine = argc == 3 && strcmp(argv[1], "status") == 0 && strcmp(argv[2], "--machine") == 0;
    if (argc == 3 && strcmp(argv[1], "config") != 0 && !machine) return -1;
    IWatchdog *wd = WD_GetService();
    if (wd == NULL) {
        return -1;
    }
    if (machine) {
        WatchdogStats_t stats = {0};
        if (!wd->GetStats || !wd->IsActive || !wd->GetTimeout || !wd->GetRegisteredTaskCount ||
            wd->GetStats(wd, &stats) != 0) return -1;
        printf("RACOOL_CLI v=1 type=watchdog_status active=%u timeout_ms=%lu registered=%lu warnings=%lu timeouts=%lu last_timeout_ms=%llu\n",
               wd->IsActive(wd) ? 1u : 0u, (unsigned long)wd->GetTimeout(wd),
               (unsigned long)wd->GetRegisteredTaskCount(wd), (unsigned long)stats.totalWarnings,
               (unsigned long)stats.totalTimeouts, (unsigned long long)stats.lastTimeoutMs);
        return 0;
    }

    /* Default sub-command: "status" */
    if (argc < 2 || strcmp(argv[1], "status") == 0) {
        return WD_CmdStatus(wd);
    }

    if (strcmp(argv[1], "feed") == 0) {
        return WD_CmdFeed(wd);
    }

    if (strcmp(argv[1], "selftest") == 0) {
        return WD_CmdSelfTest(wd);
    }

    if (strcmp(argv[1], "config") == 0) {
        /* Pass any arguments that follow "config" to the handler. */
        return WD_CmdConfig(wd, argc - 2, argv + 2);
    }

    if (strcmp(argv[1], "health") == 0) {
        return WD_CmdHealth(wd);
    }

    if (strcmp(argv[1], "tasks") == 0) {
        return WD_CmdTasks(wd);
    }

    if (strcmp(argv[1], "reset") == 0) {
        return WD_CmdReset(wd);
    }

    LOC_PRINTF(catalog, LOC_CLI_WATCHDOG_UNKNOWN, "Unknown watchdog subcommand: '%s'\n", argv[1]);
    LOC_PRINTF(catalog, LOC_CLI_WATCHDOG_USAGE, "Valid subcommands: status, feed, selftest, config [ms], "
           "health, tasks, reset\n");
    return -1;
}

/* -----------------------------------------------------------------------
 * Registration
 * --------------------------------------------------------------------- */

/**
 * @brief Registers all Watchdog console commands with the DebugConsole system.
 *
 * Called once during system initialisation (SystemInitializer).  Safe to
 * call when the DebugConsole is not yet available — the call is silently
 * skipped and a warning is emitted.
 */
esp_err_t Watchdog_RegisterConsoleCommands(void) {
    IDebugConsole *console =
        (IDebugConsole *)ServiceLocator_GetService(SVC_NAME_DEBUG_CONSOLE);
    if (!console || !console->RegisterCommandWithCategory) return ESP_ERR_INVALID_STATE;

    esp_console_cmd_t watchdog_cmd = {
        .command  = "watchdog",
        .help     = "Watchdog management (status|feed|selftest|config [ms]"
                    "|health|tasks|reset)",
        .hint     = "<status|feed|selftest|config|health|tasks|reset>",
        .func     = &cmd_watchdog,
        .argtable = NULL,
    };

    esp_err_t result = console->RegisterCommandWithCategory(console, &watchdog_cmd,
                                             CMD_CAT_SYSTEM);
    if (result != ESP_OK) return result;
    ESP_LOGI(WD_CMD_TAG, "Watchdog console commands registered successfully");
    return ESP_OK;
}
