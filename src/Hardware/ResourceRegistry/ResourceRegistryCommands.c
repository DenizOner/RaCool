#include "Hardware/ResourceRegistry/ResourceRegistryCommands.h"
#include "Hardware/ResourceRegistry/IResourceRegistry.h"
#include "Core/ServiceLocator/ServiceLocator.h"
#include "Core/ServiceLocator/ServiceNames.h"
#include "Communication/DebugConsole/Core/ConsoleColors.h"
#include "Core/Localization/Localization.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <strings.h>
#include <errno.h>

static bool ResourceCommand_ParseId(const char *text, uint32_t *value) {
    if (!text || !*text || !value) return false;
    for (const char *p=text; *p; ++p) if (*p<'0' || *p>'9') return false;
    errno=0; char *end;
    unsigned long long parsed=strtoull(text,&end,10);
    if (errno || *end || parsed>UINT32_MAX) return false;
    *value=(uint32_t)parsed; return true;
}

/**
 * @file ResourceRegistryCommands.c
 * @brief Implementation of Debug Console commands for ResourceRegistry.
 */

static int cmd_resreg(int argc, char **argv) {
    if (argc<1 || argc>4 || !argv) return 1;
    for (int i=0;i<argc;++i) if (!argv[i] || !*argv[i]) return 1;
    if (argc>=2 && ((strcasecmp(argv[1],"CHECK")==0 && argc!=4) ||
        (strcasecmp(argv[1],"CHECK")!=0 && argc!=2))) return 1;
    const LocalizationCatalog *catalog=Localization_Snapshot();
    IResourceRegistry *resReg = (IResourceRegistry *)ServiceLocator_GetService(SVC_NAME_RESOURCE_REGISTRY);
    if (!resReg) {
        LOC_PRINTF(catalog, LOC_CLI_RESOURCE_UNAVAILABLE, "ResourceRegistry service not found!\n" );
        return 1;
    }

    if (argc < 2) {
        LOC_PRINTF(catalog, LOC_CLI_RESOURCE_USAGE, "Usage: RESREG <subcommand>\n");
        LOC_PRINTF(catalog, LOC_CLI_RESOURCE_SUBCOMMANDS, "Subcommands: DUMP, STATS, CHECK <type> <id>, VALIDATE, RESET\n");
        LOC_PRINTF(catalog, LOC_CLI_RESOURCE_TYPES, "  <type>: 1=GPIO, 2=I2C, 3=PWM, 4=ADC\n");
        return 1;
    }

    if (strcasecmp(argv[1], "DUMP") == 0) {
        return resReg->DumpStatus ? resReg->DumpStatus(resReg) : 1;
    }

    if (strcasecmp(argv[1], "STATS") == 0) {
        ResourceRegistryStats_t stats;
        if (resReg->GetStats && resReg->GetStats(resReg, &stats) == 0) {
            LOC_PRINTF(catalog, LOC_CLI_RESOURCE_TITLE, "\n--- Resource Registry Statistics ---\n" );
            LOC_PRINTF(catalog, LOC_CLI_RESOURCE_RESERVATIONS, "Total Reservations : " "%lu" "\n", (unsigned long)stats.total_reservations);
            LOC_PRINTF(catalog, LOC_CLI_RESOURCE_RELEASES, "Total Releases     : " "%lu" "\n", (unsigned long)stats.total_releases);
            LOC_PRINTF(catalog, LOC_CLI_RESOURCE_CONFLICTS, "Total Conflicts    : " "%lu" "\n", (unsigned long)stats.total_conflicts);
            LOC_PRINTF(catalog, LOC_CLI_RESOURCE_GPIO, "Active GPIOs       : %lu\n", (unsigned long)stats.active_gpio_count);
            LOC_PRINTF(catalog, LOC_CLI_RESOURCE_I2C, "Active I2C Buses   : %lu\n", (unsigned long)stats.active_i2c_count);
            LOC_PRINTF(catalog, LOC_CLI_RESOURCE_PWM, "Active PWM Channels: %lu\n", (unsigned long)stats.active_pwm_count);
            LOC_PRINTF(catalog, LOC_CLI_RESOURCE_ADC, "Active ADC Channels: %lu\n", (unsigned long)stats.active_adc_count);
            LOC_PRINTF(catalog, LOC_CLI_RESOURCE_SEPARATOR, "------------------------------------\n\n");
            return 0;
        }
        LOC_PRINTF(catalog, LOC_CLI_RESOURCE_STATS_FAILED, "Failed to retrieve statistics.\n" );
        return 1;
    }

    if (strcasecmp(argv[1], "VALIDATE") == 0) {
        bool valid = resReg->Validate && resReg->Validate(resReg);
        if (valid) {
            LOC_PRINTF(catalog, LOC_CLI_RESOURCE_VALIDATE_PASSED, "ResourceRegistry validation PASSED (No conflicts detected).\n" );
        } else {
            LOC_PRINTF(catalog, LOC_CLI_RESOURCE_VALIDATE_FAILED, "ResourceRegistry validation FAILED (Conflicts present!).\n" );
        }
        return valid ? 0 : 1;
    }

    if (strcasecmp(argv[1], "RESET") == 0) {
        if (resReg->Reset && resReg->Reset(resReg) == 0) {
            LOC_PRINTF(catalog, LOC_CLI_RESOURCE_RESET_APPLIED, "ResourceRegistry reset successfully.\n" );
            return 0;
        } else {
            LOC_PRINTF(catalog, LOC_CLI_RESOURCE_RESET_FAILED, "Failed to reset ResourceRegistry.\n" );
        }
        return 1;
    }

    if (strcasecmp(argv[1], "CHECK") == 0) {
        if (argc < 4) {
            LOC_PRINTF(catalog, LOC_CLI_RESOURCE_CHECK_USAGE, "Usage: RESREG CHECK <type> <id>\n");
            LOC_PRINTF(catalog, LOC_CLI_RESOURCE_CHECK_TYPES, "  Type: 1=GPIO, 2=I2C, 3=PWM, 4=ADC\n");
            return 1;
        }
        uint32_t typeValue,id;
        if (!ResourceCommand_ParseId(argv[2],&typeValue) || typeValue<=RESOURCE_TYPE_NONE ||
            typeValue>=RESOURCE_TYPE_MAX || !ResourceCommand_ParseId(argv[3],&id) ||
            !resReg->IsAvailable || !resReg->GetOwner) return 1;
        ResourceType_t type = (ResourceType_t)typeValue;
        ResourceId_t res = {.type = type, .id = id};

        bool avail = resReg->IsAvailable(resReg, res);
        char owner[16] = {0};
        int32_t ownerResult=resReg->GetOwner(resReg, res, owner, sizeof(owner));
        if (ownerResult!=0 && ownerResult!=-2) return 1;
        if (ownerResult==-2 && !avail) return 1;
        owner[sizeof(owner)-1]=0;

        LOC_PRINTF(catalog, LOC_CLI_RESOURCE_CHECK_STATUS, "Resource Type %d ID %lu: Status = %s, Owner = %s\n",
               (int)type, (unsigned long)id,
               avail ? Localization_Text(catalog,LOC_CLI_RESOURCE_VALUE_AVAILABLE) : Localization_Text(catalog,LOC_CLI_RESOURCE_VALUE_LOCKED) ,
               ownerResult==-2 ? Localization_Text(catalog,LOC_CLI_RESOURCE_VALUE_FREE) : owner);
        return 0;
    }

    LOC_PRINTF(catalog, LOC_CLI_RESOURCE_UNKNOWN, "Unknown subcommand: %s\n" , argv[1]);
    return 1;
}

esp_err_t ResourceRegistryCommands_Register(IDebugConsole *console) {
    if (!console || !console->RegisterCommandWithCategory) return ESP_ERR_INVALID_ARG;

    static const esp_console_cmd_t cmds[] = {
        {
            .command = "RESREG",
            .help = "Resource Registry management and diagnostic commands",
            .func = &cmd_resreg,
            .hint = "<subcmd>"
        }
    };

    for (size_t i = 0; i < sizeof(cmds) / sizeof(cmds[0]); i++) {
        esp_err_t result = console->RegisterCommandWithCategory(console, &cmds[i], CMD_CAT_HARDWARE);
        if (result != ESP_OK) return result;
    }
    return ESP_OK;
}
