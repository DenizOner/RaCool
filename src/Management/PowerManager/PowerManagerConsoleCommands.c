#include "Management/PowerManager/PowerManagerConsoleCommands.h"
#include "Management/PowerManager/IPowerManager.h"
#include "Core/ServiceLocator/ServiceLocator.h"
#include "Core/ServiceLocator/ServiceNames.h"
#include "Communication/DebugConsole/IDebugConsole.h"
#include "esp_log.h"
#include "Core/Localization/Localization.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

static const char *PWR_CONSOLE_TAG = "PowerManagerConsole";



static bool ParseMilliseconds(const char *text, uint32_t *value) {
    if (!text || !*text || !value) return false;
    for (const char *p = text; *p; ++p) if (*p < '0' || *p > '9') return false;
    errno = 0;
    char *end;
    unsigned long long parsed = strtoull(text, &end, 10);
    if (errno || *end || parsed > UINT32_MAX) return false;
    *value = (uint32_t)parsed;
    return true;
}

static int cmd_power(int argc, char **argv) {
    const LocalizationCatalog *catalog = Localization_Snapshot();
    if (argc < 1 || argc > 4 || !argv) return -1;
    for (int i=0; i<argc; ++i) if (!argv[i] || !*argv[i]) return -1;
    bool machine = argc >= 3 && strcmp(argv[argc-1], "--machine") == 0;
    if (machine) {
        --argc;
        if (strcmp(argv[1],"status") && strcmp(argv[1],"mode") &&
            strcmp(argv[1],"restart") && strcmp(argv[1],"selftest")) return -1;
    }
    if (argc >= 2 && (!strcmp(argv[1],"status") || !strcmp(argv[1],"selftest")) && argc != 2) return -1;
    IPowerManager *pm = (IPowerManager *)ServiceLocator_GetService(SVC_NAME_POWER_MANAGER);
    if (!pm) {
        LOC_PRINTF(catalog, LOC_CLI_POWER_UNAVAILABLE, "ERROR: PowerManager service not registered or available\n");
        return -1;
    }

    if (argc < 2 || strcmp(argv[1], "status") == 0) {
        PowerStats_t stats;
        memset(&stats, 0, sizeof(stats));
        if (!pm->GetStats || pm->GetStats(pm, &stats) != POWER_OK) return -1;
        if (machine) {
            printf("RACOOL_CLI v=1 type=power_status mode=%d locks=%lu sleeps=%lu wakeups=%lu restarts=%lu uptime_s=%lu\n",
                (int)stats.currentMode, (unsigned long)stats.activePowerLocks,
                (unsigned long)stats.totalSleeps, (unsigned long)stats.totalWakeups,
                (unsigned long)stats.totalRestarts, (unsigned long)stats.uptimeSeconds);
            return 0;
        }
        const char *mode_str = Localization_Text(catalog,LOC_CLI_POWER_VALUE_0);
        switch (stats.currentMode) {
            case POWER_MODE_UNKNOWN:    mode_str = Localization_Text(catalog,LOC_CLI_POWER_VALUE_1); break;
            case POWER_MODE_PERFORMANCE: mode_str = Localization_Text(catalog,LOC_CLI_POWER_VALUE_2); break;
            case POWER_MODE_BALANCED:    mode_str = Localization_Text(catalog,LOC_CLI_POWER_VALUE_3); break;
            case POWER_MODE_LOW_POWER:   mode_str = Localization_Text(catalog,LOC_CLI_POWER_VALUE_4); break;
            case POWER_MODE_LIGHT_SLEEP: mode_str = Localization_Text(catalog,LOC_CLI_POWER_VALUE_5); break;
            case POWER_MODE_DEEP_SLEEP:  mode_str = Localization_Text(catalog,LOC_CLI_POWER_VALUE_6); break;
        }

        LOC_PRINTF(catalog, LOC_CLI_POWER_TITLE, "--- Power Manager Status ---\n");
        LOC_PRINTF(catalog, LOC_CLI_POWER_MODE, " Current Mode      : %s (%d)\n", mode_str, stats.currentMode);
        if (argc < 2) return 0;
        if (!pm->GetResetReasonString || !pm->GetWakeupCause) return -1;
        LOC_PRINTF(catalog, LOC_CLI_POWER_RESET_REASON, " Reset Reason      : %s\n", pm->GetResetReasonString(pm));
        LOC_PRINTF(catalog, LOC_CLI_POWER_WAKE_CAUSE, " Wakeup Cause Code : %d\n", pm->GetWakeupCause(pm));
        LOC_PRINTF(catalog, LOC_CLI_POWER_LOCK_COUNT, " Active Power Locks: %lu\n", (unsigned long)stats.activePowerLocks);
        LOC_PRINTF(catalog, LOC_CLI_POWER_SLEEP_COUNT, " Total Sleep Count : %lu\n", (unsigned long)stats.totalSleeps);
        LOC_PRINTF(catalog, LOC_CLI_POWER_WAKE_COUNT, " Total Wakeups     : %lu\n", (unsigned long)stats.totalWakeups);
        LOC_PRINTF(catalog, LOC_CLI_POWER_RESTART_COUNT, " Total Restarts    : %lu\n", (unsigned long)stats.totalRestarts);
        LOC_PRINTF(catalog, LOC_CLI_POWER_UPTIME, " Uptime (seconds)  : %lu\n", (unsigned long)stats.uptimeSeconds);
        return 0;
    }

    if (strcmp(argv[1], "locks") == 0) {
        if (argc != 2 || !pm->GetLockSnapshot) return -1;
        PowerLockSnapshot entries[POWER_MAX_LOCK_OWNERS];
        size_t count = 0;
        if (pm->GetLockSnapshot(pm, entries, POWER_MAX_LOCK_OWNERS, &count) != POWER_OK || count > POWER_MAX_LOCK_OWNERS) return -1;
        LOC_PRINTF(catalog, LOC_CLI_POWER_LOCK_OWNERS, "Application power lock owners: %u\n", (unsigned)count);
        const char *kinds[] = {"CPU_MAX", "APB_MAX", "NO_LIGHT_SLEEP"};
        for (size_t i = 0; i < count; ++i)
            LOC_PRINTF(catalog, LOC_CLI_POWER_LOCK_ENTRY, " %s: %s, references=%lu\n", entries[i].name,
                (unsigned)entries[i].kind < 3 ? kinds[entries[i].kind] : Localization_Text(catalog,LOC_CLI_POWER_VALUE_0),
                (unsigned long)entries[i].references);
        LOC_PRINTF(catalog, LOC_CLI_POWER_SDK_LOCK_SCOPE, "SDK peripheral-owned locks are not included.\n");
        return 0;
    }

    if (strcmp(argv[1], "mode") == 0) {
        if (argc != 3 || !pm->SetPowerMode) {
            LOC_PRINTF(catalog, LOC_CLI_POWER_MODE_USAGE, "Usage: power mode <perf|balanced|lowpower>\n");
            return -1;
        }
        PowerMode_t mode = POWER_MODE_BALANCED;
        if (strcmp(argv[2], "perf") == 0 || strcmp(argv[2], "performance") == 0) {
            mode = POWER_MODE_PERFORMANCE;
        } else if (strcmp(argv[2], "balanced") == 0) {
            mode = POWER_MODE_BALANCED;
        } else if (strcmp(argv[2], "lowpower") == 0 || strcmp(argv[2], "low_power") == 0) {
            mode = POWER_MODE_LOW_POWER;
        } else {
            LOC_PRINTF(catalog, LOC_CLI_POWER_INVALID_MODE, "ERROR: Invalid mode '%s'. Options: perf, balanced, lowpower\n", argv[2]);
            return -1;
        }

        int32_t res = pm->SetPowerMode(pm, mode);
        if (machine) {
            printf("RACOOL_CLI v=1 type=power_mode result=%s mode=%d error=%ld\n",res == POWER_OK ? "applied" : "rejected",(int)mode,(long)res);
            return res == POWER_OK ? 0 : -1;
        }
        if (res == POWER_OK) {
            LOC_PRINTF(catalog, LOC_CLI_POWER_MODE_APPLIED, "SUCCESS: Power mode updated to %s\n", argv[2]);
            return 0;
        } else {
            LOC_PRINTF(catalog, LOC_CLI_POWER_MODE_FAILED, "ERROR: Failed to set power mode, code=%ld\n", (long)res);
            return -1;
        }
    }

    if (strcmp(argv[1], "sleep") == 0) {
        uint32_t duration_ms = POWER_DEFAULT_CONSOLE_SLEEP_MS;
        bool is_deep = false;
        if (argc >= 3) {
            if (!ParseMilliseconds(argv[2], &duration_ms)) return -1;
        }
        if (argc > 4 || (argc == 4 && strcmp(argv[3], "deep") != 0)) return -1;
        is_deep = argc == 4;
        if (!is_deep && !duration_ms) {
            LOC_PRINTF(catalog, LOC_CLI_POWER_LIGHT_DURATION, "Light sleep requires a positive timer duration.\n");
            return -1;
        }

        LOC_PRINTF(catalog, LOC_CLI_POWER_SLEEP_REQUESTED, "Requesting %s sleep for %lu ms...\n", is_deep ? Localization_Text(catalog,LOC_CLI_POWER_VALUE_7) : Localization_Text(catalog,LOC_CLI_POWER_VALUE_8), (unsigned long)duration_ms);
        uint64_t duration_us = (uint64_t)duration_ms * 1000ULL;
        if (is_deep) {
            if (!pm->EnterDeepSleep) return -1;
            int32_t result = pm->EnterDeepSleep(pm, duration_us);
            if (result != POWER_OK) LOC_PRINTF(catalog, LOC_CLI_POWER_DEEP_FAILED, "Deep sleep preparation failed (%ld).\n", (long)result);
            return result == POWER_OK ? 0 : -1;
        } else {
            return pm->EnterLightSleep && pm->EnterLightSleep(pm, duration_us) == POWER_OK ? 0 : -1;
        }
        return 0;
    }

    if (strcmp(argv[1], "restart") == 0) {
        uint32_t delay_ms = POWER_DEFAULT_RESTART_DELAY_MS;
        if (argc >= 3) {
            if (!ParseMilliseconds(argv[2], &delay_ms)) return -1;
        }
        if (argc > 3) return -1;
        if (!pm->RestartAsync) return -1;
        int32_t result = pm->RestartAsync(pm, delay_ms);
        if (machine) {
            printf("RACOOL_CLI v=1 type=power_restart result=%s delay_ms=%lu error=%ld\n", result == POWER_OK ? "queued" : "rejected",(unsigned long)delay_ms,(long)result);
            return result == POWER_OK ? 0 : -1;
        }
        if (result != POWER_OK) {
            LOC_PRINTF(catalog, LOC_CLI_POWER_RESTART_FAILED, "Restart scheduling failed: %ld\n", (long)result);
            return -1;
        }
        LOC_PRINTF(catalog, LOC_CLI_POWER_RESTART_QUEUED, "System restart scheduled in %lu ms...\n", (unsigned long)delay_ms);
        return 0;
    }

    if (strcmp(argv[1], "selftest") == 0) {
        if (!pm->SelfTest) return -1;
        if (!machine) LOC_PRINTF(catalog, LOC_CLI_POWER_SELFTEST_START, "Executing PowerManager self-test...\n");
        int32_t res = pm->SelfTest(pm);
        if (machine) {
            printf("RACOOL_CLI v=1 type=power_selftest result=%s error=%ld\n",res == POWER_OK ? "passed" : "failed",(long)res);
            return res == POWER_OK ? 0 : -1;
        }
        if (res == POWER_OK) {
            LOC_PRINTF(catalog, LOC_CLI_POWER_SELFTEST_PASSED, "PASSED: PowerManager self-test OK\n");
            return 0;
        } else {
            LOC_PRINTF(catalog, LOC_CLI_POWER_SELFTEST_FAILED, "FAILED: PowerManager self-test failed, code=%ld\n", (long)res);
            return -1;
        }
    }

    LOC_PRINTF(catalog, LOC_CLI_POWER_UNKNOWN, "Unknown power subcommand: %s\n", argv[1]);
    LOC_PRINTF(catalog, LOC_CLI_POWER_USAGE, "Valid subcommands: status, locks, mode, sleep, restart, selftest\n");
    return -1;
}

esp_err_t PowerManager_RegisterConsoleCommands(void) {
    IDebugConsole *console = (IDebugConsole *)ServiceLocator_GetService(SVC_NAME_DEBUG_CONSOLE);
    if (!console || !console->RegisterCommandWithCategory) return ESP_ERR_INVALID_STATE;

    esp_console_cmd_t power_cmd = {
        .command = "power",
        .help = "Power management commands (status, locks, mode, sleep, restart, selftest)",
        .hint = "<status|locks|mode|sleep|restart|selftest>",
        .func = &cmd_power,
        .argtable = NULL
    };

    esp_err_t result = console->RegisterCommandWithCategory(console, &power_cmd, CMD_CAT_SYSTEM);
    if (result != ESP_OK) return result;
    ESP_LOGI(PWR_CONSOLE_TAG, "Power console commands registered successfully");

    return ESP_OK;
}
