#include "Core/Localization/Localization.h"
#include "Core/EventBus/IEventBus.h"
#include "Core/EventBus/EventBusConsoleCommands.h"
#include "Core/ServiceLocator/ServiceLocator.h"
#include "Core/ServiceLocator/ServiceNames.h"
#include "Communication/DebugConsole/IDebugConsole.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>


static int cmd_bus_status(int argc, char **argv);
static int cmd_bus_publish(int argc, char **argv);

esp_err_t EventBus_RegisterConsoleCommands(void) {

    IDebugConsole *console = (IDebugConsole *)ServiceLocator_GetService(SVC_NAME_DEBUG_CONSOLE);
    if (!console || !console->RegisterCommandWithCategory) return ESP_ERR_INVALID_STATE;

    static const esp_console_cmd_t cmds[] = {
        { .command = "bus_status",  .help = "Show EventBus state and subscriber count",              .func = &cmd_bus_status,  .hint = NULL          },
        { .command = "bus_publish", .help = "Publish a test event to EventBus: bus_publish <name>",  .func = &cmd_bus_publish, .hint = "<event_name>" },
    };

    for (size_t i = 0; i < sizeof(cmds) / sizeof(cmds[0]); i++) {
        esp_err_t result = console->RegisterCommandWithCategory(console, &cmds[i], CMD_CAT_SYSTEM);
        if (result != ESP_OK) return result;
    }
    return ESP_OK;
}

static int cmd_bus_status(int argc, char **argv) {
    const LocalizationCatalog *catalog = Localization_Snapshot();
    if (argc != 1 || !argv || !argv[0]) return 1;
    IEventBus *bus = GetEventBus();
    if (!bus) { LOC_PRINTF(catalog, LOC_CLI_BUS_UNAVAILABLE, "EventBus not available.\n"); return 1; }
    if (!bus->base.GetState) return 1;
    LOC_PRINTF(catalog, LOC_CLI_BUS_TITLE, "\n--- EventBus Status ---\n");
    LOC_PRINTF(catalog, LOC_CLI_SERVICE_STATE, "State : %d\n", (int)bus->base.GetState((IService *)bus));
    printf("-----------------------\n");
    return 0;
}

static int cmd_bus_publish(int argc, char **argv) {
    const LocalizationCatalog *catalog = Localization_Snapshot();
    if (argc != 2 || !argv || !argv[0] || !argv[1] || !argv[1][0]) { LOC_PRINTF(catalog, LOC_CLI_BUS_USAGE, "Usage: bus_publish <event_name>\n"); return 1; }
    IEventBus *bus = GetEventBus();
    if (!bus) { LOC_PRINTF(catalog, LOC_CLI_BUS_UNAVAILABLE, "EventBus not available.\n"); return 1; }
    if (!bus->Publish) return 1;
    BusEvent ev = {
        .eventName = argv[1],
        .type      = EVENT_TYPE_INFO,
        .data      = NULL,
        .dataSize  = 0,
    };
    int32_t res = bus->Publish(ev);
    if (res == 0) {
        LOC_PRINTF(catalog, LOC_CLI_BUS_PUBLISHED, "Event '%s' published.\n", argv[1]);
    } else {
        LOC_PRINTF(catalog, LOC_CLI_BUS_PUBLISH_FAILED, "Publish failed (err=%d).\n", (int)res);
    }
    return res == 0 ? 0 : 1;
}
