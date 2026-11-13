/**
 * @file I2CManagerConsoleCommands.c
 * @brief Debug Console CLI commands for I2CManager Module.
 *
 * Command: i2c
 *   i2c status   -- Service state
 *   i2c scan     -- Scan I2C bus (port 0)
 */

#include "Hardware/I2CManager/I2CManagerConsoleCommands.h"
#include "Hardware/I2CManager/II2CManager.h"
#include "Core/ServiceLocator/ServiceLocator.h"
#include "Core/ServiceLocator/ServiceNames.h"
#include "Communication/DebugConsole/IDebugConsole.h"
#include "esp_log.h"
#include <stdio.h>
#include "Core/Localization/Localization.h"
#include <string.h>

#ifndef RACOOL_TEST_NATIVE
#include <esp_console.h>
#endif

static const char *I2C_CMD_TAG = "I2CConsole";

static int cmd_i2c_handler(int argc, char **argv) {
    const LocalizationCatalog *catalog=Localization_Snapshot();
    if (argc < 1 || argc > 3 || !argv) return -1;
    for (int i = 0; i < argc; ++i) if (!argv[i] || !*argv[i]) return -1;
    bool machine=argc>=2 && !strcmp(argv[argc-1],"--machine");
    if (machine) --argc;
    if (argc>2) return -1;
    II2CManager *i2c = I2CManager_GetInstance();
    if (!i2c) { LOC_PRINTF(catalog, LOC_CLI_I2C_MISSING, "[I2CManager] Service unavailable.\n"); return -1; }

    if (argc < 2 || strcmp(argv[1], "status") == 0) {
        if (!i2c->base.GetState) return -1;
        ServiceState st = i2c->base.GetState((IService *)i2c);
        I2CDiagnostics diagnostics;
        if (!i2c->GetDiagnostics || i2c->GetDiagnostics(i2c, &diagnostics) != ESP_OK) {
            LOC_PRINTF(catalog, LOC_CLI_I2C_SUMMARY_MISSING, " Resource/transfer summary unavailable\n"); return -1;
        }
        if (machine) {
            printf("RACOOL_CLI v=1 type=i2c_status state=%d ready=%u devices=%lu owners=%lu reads=%lu writes=%lu external=%lu failures=%lu lock_timeouts=%lu last_result=%d last_failure=%d\n",
                (int)st,diagnostics.bus_ready ? 1u : 0u,(unsigned long)diagnostics.owned_devices,
                (unsigned long)diagnostics.external_owners,(unsigned long)diagnostics.reads,
                (unsigned long)diagnostics.writes,(unsigned long)diagnostics.external_operations,
                (unsigned long)diagnostics.transfer_failures,(unsigned long)diagnostics.lock_timeouts,
                (int)diagnostics.last_transfer_result,(int)diagnostics.last_transfer_failure);
            return 0;
        }
        LOC_PRINTF(catalog, LOC_CLI_I2C_TITLE, "--- I2CManager Status ---\n");
        LOC_PRINTF(catalog, LOC_CLI_I2C_STATE, " State: %d\n", (int)st);
        LOC_PRINTF(catalog, LOC_CLI_I2C_RESOURCES, " Bus Ready: %s; Owned Devices: %lu; External Owners: %lu\n",
               Localization_Text(catalog, diagnostics.bus_ready ? LOC_CLI_I2C_YES : LOC_CLI_I2C_NO), (unsigned long)diagnostics.owned_devices,
               (unsigned long)diagnostics.external_owners);
        LOC_PRINTF(catalog, LOC_CLI_I2C_OPERATIONS, " Reads: %lu; Writes: %lu; External Operations: %lu\n",
               (unsigned long)diagnostics.reads, (unsigned long)diagnostics.writes,
               (unsigned long)diagnostics.external_operations);
        LOC_PRINTF(catalog, LOC_CLI_I2C_FAILURES, " Transfer Failures: %lu; Lock Timeouts: %lu; Last Result: %d; Last Failure: %d\n",
               (unsigned long)diagnostics.transfer_failures, (unsigned long)diagnostics.lock_timeouts,
               (int)diagnostics.last_transfer_result, (int)diagnostics.last_transfer_failure);
        printf("-------------------------\n");
        return 0;
    }

    if (strcmp(argv[1], "scan") == 0) {
        if (!i2c->Scan) return -1;
        if (!machine) LOC_PRINTF(catalog, LOC_CLI_I2C_SCAN_START, "[I2CManager] Scanning I2C bus (port 0)...\n");
        esp_err_t e = i2c->Scan(i2c, 0);
        if (machine) {
            printf("RACOOL_CLI v=1 type=i2c_command command=scan result=%s error=%ld\n",e==ESP_OK ? "completed" : "rejected",(long)e);
            return e==ESP_OK ? 0 : -1;
        }
        LOC_PRINTF(catalog, LOC_CLI_I2C_SCAN_RESULT, "[I2CManager] Scan complete -> %d\n", e);
        return e == ESP_OK ? 0 : -1;
    }

    if (strcmp(argv[1], "recover") == 0) {
        esp_err_t result = i2c->RecoverBus ? i2c->RecoverBus(i2c) : ESP_ERR_INVALID_STATE;
        if (machine) {
            printf("RACOOL_CLI v=1 type=i2c_command command=recover result=%s error=%ld\n",result==ESP_OK ? "completed" : "rejected",(long)result);
            return result==ESP_OK ? 0 : -1;
        }
        LOC_PRINTF(catalog, LOC_CLI_I2C_RECOVER_RESULT, "[I2CManager] Recovery result: %d; device/lease owners must be detached first.\n", result);
        return result == ESP_OK ? 0 : -1;
    }
    LOC_PRINTF(catalog, LOC_CLI_I2C_USAGE, "Usage: i2c <status|scan|recover>\n");
    return -1;
}

esp_err_t I2CManager_RegisterConsoleCommands(void) {
    IDebugConsole *console = (IDebugConsole *)ServiceLocator_GetService(SVC_NAME_DEBUG_CONSOLE);
    if (!console || !console->RegisterCommandWithCategory) return ESP_ERR_INVALID_STATE;
    const esp_console_cmd_t cmd = {
        .command  = "i2c",
        .help     = "I2CManager diagnostics (status, scan, recover)",
        .hint     = "<status|scan|recover>",
        .func     = &cmd_i2c_handler,
        .argtable = NULL
    };
    esp_err_t result = console->RegisterCommandWithCategory(console, &cmd, CMD_CAT_HARDWARE);
    if (result != ESP_OK) return result;
    ESP_LOGI(I2C_CMD_TAG, "I2CManager console commands registered ('i2c')");
    return ESP_OK;
}
