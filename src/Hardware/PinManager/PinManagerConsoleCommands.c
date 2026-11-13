#include "Core/Localization/Localization.h"
#include <errno.h>
/**
 * @file PinManagerConsoleCommands.c
 * @brief Debug Console CLI commands for PinManager Module.
 *
 * Command: pin
 *   pin status      -- Validate + DumpPinMap
 *   pin dump        -- DumpPinMap
 *   pin get <func>  -- GetPin by function index
 */

#include "Hardware/PinManager/PinManagerConsoleCommands.h"
#include "Hardware/PinManager/IPinManager.h"
#include "Core/ServiceLocator/ServiceLocator.h"
#include "Core/ServiceLocator/ServiceNames.h"
#include "Communication/DebugConsole/IDebugConsole.h"
#include "esp_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef RACOOL_TEST_NATIVE
#include <esp_console.h>
#endif

static const char *PIN_CMD_TAG = "PinConsole";

static int cmd_pin_handler(int argc, char **argv) {
    const LocalizationCatalog *catalog = Localization_Snapshot();
    if (!argv || argc < 1 || argc > 3 || !argv[0]) return 1;
    const bool status = argc == 1 || (argv[1] && strcmp(argv[1], "status") == 0);
    const bool dump = argc > 1 && argv[1] && strcmp(argv[1], "dump") == 0;
    const bool get = argc > 1 && argv[1] && strcmp(argv[1], "get") == 0;
    unsigned long function = 0;
    if (get && argc == 3 && argv[2] && argv[2][0]) {
        for (const char *digit = argv[2]; *digit; ++digit) {
            if (*digit < '0' || *digit > '9') goto invalid;
        }
        char *end; errno = 0; function = strtoul(argv[2], &end, 10);
        if (errno || *end || function <= PIN_FUNC_NONE || function >= PIN_FUNC_MAX_COUNT) goto invalid;
    } else if ((!status && !dump) || (status && argc > 2) || (dump && argc != 2)) goto invalid;
    IPinManager *pm = PinManager_GetInstance();
    if (!pm) {
        LOC_PRINTF(catalog, LOC_CLI_PIN_UNAVAILABLE, "Pin manager unavailable.\n");
        return 1;
    }
    if (status) {
        if (!pm->Validate || !pm->DumpPinMap) return 1;
        const bool valid = pm->Validate(pm);
        LOC_PRINTF(catalog, LOC_CLI_PIN_TITLE, "--- Pin Mapping Status ---\n");
        LOC_PRINTF(catalog, LOC_CLI_PIN_VALIDATION, "Validation: %s\n",
            Localization_Text(catalog, valid ? LOC_CLI_PIN_PASSED : LOC_CLI_PIN_FAILED));
        const int32_t result = pm->DumpPinMap(pm);
        return valid && result == 0 ? 0 : 1;
    }
    if (dump) return pm->DumpPinMap && pm->DumpPinMap(pm) == 0 ? 0 : 1;
    if (!pm->GetPin) return 1;
    const int pin = (int)pm->GetPin(pm, (PinFunction_t)function);
    LOC_PRINTF(catalog, LOC_CLI_PIN_MAPPING, "Function %lu -> GPIO %d\n", function, pin);
    return 0;
invalid:
    LOC_PRINTF(catalog, LOC_CLI_PIN_USAGE, "Usage: pin <status|dump|get <function_index>>\n");
    return 1;
}

esp_err_t PinManager_RegisterConsoleCommands(void) {
    IDebugConsole *console = (IDebugConsole *)ServiceLocator_GetService(SVC_NAME_DEBUG_CONSOLE);
    if (!console || !console->RegisterCommandWithCategory) return ESP_ERR_INVALID_STATE;
    const esp_console_cmd_t cmd = {
        .command  = "pin",
        .help     = "PinManager diagnostics (status, dump, get)",
        .hint     = "<status|dump|get>",
        .func     = &cmd_pin_handler,
        .argtable = NULL
    };
    esp_err_t result = console->RegisterCommandWithCategory(console, &cmd, CMD_CAT_HARDWARE);
    if (result != ESP_OK) return result;
    ESP_LOGI(PIN_CMD_TAG, "PinManager console commands registered ('pin')");
    return ESP_OK;
}
