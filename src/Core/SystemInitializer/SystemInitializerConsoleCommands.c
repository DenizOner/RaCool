#include "Core/Localization/Localization.h"
#include "Core/DI/IDIRegistry.h"
#include "Core/SystemInitializer/ISystemInitializer.h"
#include "Core/SystemInitializer/SystemInitializerConsoleCommands.h"
#include "Core/ServiceLocator/ServiceLocator.h"
#include "Core/ServiceLocator/ServiceNames.h"
#include "Communication/DebugConsole/IDebugConsole.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>


static int cmd_sys_init_status(int argc, char **argv);

esp_err_t SystemInitializer_RegisterConsoleCommands(void) {

    IDebugConsole *console = (IDebugConsole *)ServiceLocator_GetService(SVC_NAME_DEBUG_CONSOLE);
    if (!console || !console->RegisterCommandWithCategory) return ESP_ERR_INVALID_STATE;

    static const esp_console_cmd_t cmds[] = {
        { .command = "sys_init_status", .help = "Show SystemInitializer service state", .func = &cmd_sys_init_status, .hint = NULL },
    };

    for (size_t i = 0; i < sizeof(cmds) / sizeof(cmds[0]); i++) {
        esp_err_t result = console->RegisterCommandWithCategory(console, &cmds[i], CMD_CAT_SYSTEM);
        if (result != ESP_OK) return result;
    }
    return ESP_OK;
}

static int cmd_sys_init_status(int argc, char **argv) {
    const LocalizationCatalog *catalog = Localization_Snapshot();
    if (argc != 1 || !argv || !argv[0]) return 1;
    SystemInitializer *si = GetSystemInitializer();
    if (!si) { LOC_PRINTF(catalog, LOC_CLI_INITIALIZER_UNAVAILABLE, "SystemInitializer not available.\n"); return 1; }
    LOC_PRINTF(catalog, LOC_CLI_INITIALIZER_TITLE, "\n--- SystemInitializer Status ---\n");
    if (si->base.GetState != NULL) {
        ServiceState state = si->base.GetState((IService *)si);
        LOC_PRINTF(catalog, LOC_CLI_SERVICE_STATE, "State : %d\n", (int)state);
    } else {
        LOC_PRINTF(catalog, LOC_CLI_INITIALIZER_STATE_UNAVAILABLE, "State : (GetState not implemented)\n");
        return 1;
    }
    printf("--------------------------------\n");
    return 0;
}
