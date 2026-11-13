#include "Core/Localization/Localization.h"
#include "Core/ServiceLocator/ServiceLocator.h"
#include "Core/ServiceLocator/ServiceLocatorConsoleCommands.h"
#include "Core/ServiceLocator/ServiceNames.h"
#include "Communication/DebugConsole/IDebugConsole.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


static int cmd_svc_list(int argc, char **argv);
static int cmd_svc_get(int argc, char **argv);

esp_err_t ServiceLocator_RegisterConsoleCommands(void) {

    IDebugConsole *console = (IDebugConsole *)ServiceLocator_GetService(SVC_NAME_DEBUG_CONSOLE);
    if (!console || !console->RegisterCommandWithCategory) return ESP_ERR_INVALID_STATE;

    static const esp_console_cmd_t cmds[] = {
        { .command = "svc_list", .help = "List all registered services with state",         .func = &cmd_svc_list, .hint = NULL           },
        { .command = "svc_get",  .help = "Look up a service by name: svc_get <svc_name>",   .func = &cmd_svc_get,  .hint = "<svc_name>"   },
    };

    for (size_t i = 0; i < sizeof(cmds) / sizeof(cmds[0]); i++) {
        esp_err_t result = console->RegisterCommandWithCategory(console, &cmds[i], CMD_CAT_SYSTEM);
        if (result != ESP_OK) return result;
    }
    return ESP_OK;
}

static int cmd_svc_list(int argc, char **argv) {
    const LocalizationCatalog *catalog = Localization_Snapshot();
    if (argc!=1 || !argv) {LOC_PRINTF(catalog, LOC_CLI_SERVICES_LIST_USAGE, "Usage: svc_list\n");return 1;}
    uint32_t count=0;
    ServiceLocatorBinding *bindings=malloc(sizeof(*bindings)*SERVICE_LOCATOR_MAX_SERVICES);
    if (!bindings) {LOC_PRINTF(catalog, LOC_CLI_SERVICES_ALLOCATION_FAILED, "Service snapshot allocation failed.\n");return 1;}
    if (ServiceLocator_CopyBindings(bindings,SERVICE_LOCATOR_MAX_SERVICES,&count)!=0) {
        free(bindings);LOC_PRINTF(catalog, LOC_CLI_SERVICES_SNAPSHOT_UNAVAILABLE, "Service snapshot unavailable.\n");return 1;
    }
    LOC_PRINTF(catalog, LOC_CLI_SERVICES_TITLE, "\n--- Registered Services (%lu) ---\n",(unsigned long)count);
    for (uint32_t i=0;i<count;i++) {
        IService *service=bindings[i].service;
        ServiceState state=service && service->GetState ? service->GetState(service) : SERVICE_STATE_ERROR;
        printf("  [%2lu] %-32s  state=%d\n",(unsigned long)i,bindings[i].name,(int)state);
    }
    printf("---------------------------------\n");free(bindings);return 0;
}

static int cmd_svc_get(int argc, char **argv) {
    const LocalizationCatalog *catalog = Localization_Snapshot();
    if (argc!=2 || !argv || !argv[1]) {LOC_PRINTF(catalog, LOC_CLI_SERVICES_GET_USAGE, "Usage: svc_get <service_name>\n");return 1;}
    IService *svc = ServiceLocator_GetService(argv[1]);
    if (!svc) {
        LOC_PRINTF(catalog, LOC_CLI_SERVICES_NOT_FOUND, "Service '%s' not found.\n", argv[1]);
        return 1;
    }
    if (!svc->GetState) { LOC_PRINTF(catalog, LOC_CLI_SERVICES_STATE_UNAVAILABLE, "Service state unavailable.\n"); return 1; }
    ServiceState state = svc->GetState(svc);
    LOC_PRINTF(catalog, LOC_CLI_SERVICES_STATE, "Service '%s': state=%d\n", argv[1], (int)state);
    return 0;
}
