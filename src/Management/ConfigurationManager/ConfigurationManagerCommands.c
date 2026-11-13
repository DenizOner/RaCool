/**
 * @file ConfigurationManagerCommands.c
 * @brief Debug Console command implementations for ConfigurationManager.
 *
 * Each command is implemented as a standalone argtable2-free handler
 * (argc/argv style) so there is no dependency on heap-allocated argtable
 * structures.  Commands print human-readable output via printf which is
 * captured by the DebugConsole driver.
 *
 * @author RaCool Team
 * @date   2026
 */

#include "Management/ConfigurationManager/ConfigurationManagerCommands.h"
#include "Core/Localization/Localization.h"
#include "Management/ConfigurationManager/IConfigurationManager.h"
#include "Management/ConfigurationManager/SystemConfigs.h"
#include "Management/ConfigurationManager/ConfigurationObservableFields.h"
#include "Management/ConfigurationManager/ConfigurationConsoleView.h"
#include "Core/ServiceLocator/ServiceLocator.h"
#include "Core/ServiceLocator/ServiceNames.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>

/** @cond INTERNAL */
#define TAG "ConfigCmd"
/** @endcond */

/* =========================================================================
 * Internal helpers
 * ========================================================================= */

/**
 * @brief Retrieves the ConfigurationManager singleton safely.
 *
 * Attempts ServiceLocator first (preferred path); falls back to
 * ConfigurationManager_GetInstance() if the locator is unavailable.
 *
 * @return Pointer to IConfigurationManager, or NULL on failure.
 */
static IConfigurationManager *get_mgr(void) {
    IService *svc = ServiceLocator_GetService(SVC_NAME_CONFIGURATION_MANAGER);
    if (svc) return (IConfigurationManager *)svc;
    return ConfigurationManager_GetInstance();
}

/* =========================================================================
 * Command: config.show
 * ========================================================================= */

/**
 * @brief Prints all current system settings to the console.
 *
 * @param argc Argument count (unused).
 * @param argv Argument vector (unused).
 * @return 0 on success, 1 if the manager is unavailable.
 */
/* Reject malformed commands before resolving services or changing settings. */
static bool config_command_arguments(int argc, char **argv, bool accepts_block) {
    if (!argv || argc < 1 || argc > (accepts_block ? 2 : 1)) return false;
    for (int i = 0; i < argc; ++i) if (!argv[i] || !argv[i][0]) return false;
    return true;
}

static int dispatch_config_show(int argc, char **argv) {
    const LocalizationCatalog *catalog=Localization_Snapshot();
    if (!config_command_arguments(argc, argv, false)) return 1;
    (void)argc; (void)argv;

    IConfigurationManager *mgr = get_mgr();
    if (!mgr) {
        LOC_PRINTF(catalog,LOC_CLI_CONFIG_UNAVAILABLE,"[ERROR] ConfigurationManager unavailable\n");
        return 1;
    }
    SystemSettings snapshot;
    if (!mgr->GetSnapshot || mgr->GetSnapshot(mgr,&snapshot)!=ESP_OK) {
        LOC_PRINTF(catalog,LOC_CLI_CONFIG_SNAPSHOT_UNAVAILABLE,"[ERROR] Configuration snapshot unavailable\n");
        return 1;
    }
    ConfigurationConsole_PrintSnapshot(&snapshot,catalog);
    return 0;
}

/* =========================================================================
 * Command: config.save [block]
 * ========================================================================= */

/**
 * @brief Saves all or a named configuration block to NVS.
 *
 * Usage: config.save [system|control|hardware|calibration|thermal|zigbee_reporting]
 *
 * @param argc Argument count.
 * @param argv argv[1] = optional block name.
 * @return 0 on success, 1 on error.
 */
static int dispatch_config_save(int argc, char **argv) {
    const LocalizationCatalog *catalog=Localization_Snapshot();
    if (!config_command_arguments(argc, argv, true)) return 1;
    IConfigurationManager *mgr = get_mgr();
    if (!mgr) {
        LOC_PRINTF(catalog,LOC_CLI_CONFIG_UNAVAILABLE,"[ERROR] ConfigurationManager unavailable\n");
        return 1;
    }

    const char *block = (argc >= 2) ? argv[1] : NULL;
    if (!mgr->Save) return 1;
    esp_err_t err = mgr->Save(mgr, block);

    if (err == ESP_OK) {
        LOC_PRINTF(catalog,LOC_CLI_CONFIG_SAVED,"[OK] Config saved: %s\n", block ? block : Localization_Text(catalog,LOC_CLI_CONFIG_ALL));
    } else {
        LOC_PRINTF(catalog,LOC_CLI_CONFIG_SAVE_FAILED,"[FAIL] Config save failed (block=%s, err=0x%x)\n",
               block ? block : Localization_Text(catalog,LOC_CLI_CONFIG_ALL), (unsigned)err);
    }
    return (err == ESP_OK) ? 0 : 1;
}

/* =========================================================================
 * Command: config.load [block]
 * ========================================================================= */

/**
 * @brief Reloads all or a named configuration block from NVS.
 *
 * Usage: config.load [block]
 *
 * @param argc Argument count.
 * @param argv argv[1] = optional block name.
 * @return 0 on success, 1 on error.
 */
static int dispatch_config_load(int argc, char **argv) {
    const LocalizationCatalog *catalog=Localization_Snapshot();
    if (!config_command_arguments(argc, argv, true)) return 1;
    IConfigurationManager *mgr = get_mgr();
    if (!mgr) {
        LOC_PRINTF(catalog,LOC_CLI_CONFIG_UNAVAILABLE,"[ERROR] ConfigurationManager unavailable\n");
        return 1;
    }

    const char *block = (argc >= 2) ? argv[1] : NULL;
    if (!mgr->Load) return 1;
    esp_err_t err = mgr->Load(mgr, block);

    if (err == ESP_OK) {
        LOC_PRINTF(catalog,LOC_CLI_CONFIG_LOADED,"[OK] Config loaded: %s\n", block ? block : Localization_Text(catalog,LOC_CLI_CONFIG_ALL));
    } else {
        LOC_PRINTF(catalog,LOC_CLI_CONFIG_LOAD_FAILED,"[FAIL] Config load failed (block=%s, err=0x%x)\n",
               block ? block : Localization_Text(catalog,LOC_CLI_CONFIG_ALL), (unsigned)err);
    }
    return (err == ESP_OK) ? 0 : 1;
}

/* =========================================================================
 * Command: config.reset [block] CONFIRM
 * ========================================================================= */

/**
 * @brief Resets all or a named configuration block to factory defaults.
 *
 * Usage: config.reset [block] CONFIRM
 *
 * WARNING: This overwrites NVS immediately.
 *
 * @param argc Argument count.
 * @param argv argv[1] = optional block name.
 * @return 0 on success, 1 on error.
 */
static int dispatch_config_reset(int argc, char **argv) {
    const LocalizationCatalog *catalog=Localization_Snapshot();
    if (!config_command_arguments(argc, argv, true)) return 1;
    IConfigurationManager *mgr = get_mgr();
    if (!mgr) {
        LOC_PRINTF(catalog,LOC_CLI_CONFIG_UNAVAILABLE,"[ERROR] ConfigurationManager unavailable\n");
        return 1;
    }

    const char *block = (argc >= 2) ? argv[1] : NULL;
    if (!mgr->ResetToDefaults) return 1;
    esp_err_t err = mgr->ResetToDefaults(mgr, block);

    if (err == ESP_OK) {
        LOC_PRINTF(catalog,LOC_CLI_CONFIG_RESET,"[OK] Config reset to defaults: %s\n", block ? block : Localization_Text(catalog,LOC_CLI_CONFIG_ALL));
    } else {
        LOC_PRINTF(catalog,LOC_CLI_CONFIG_RESET_FAILED,"[FAIL] Config reset failed (block=%s, err=0x%x)\n",
               block ? block : Localization_Text(catalog,LOC_CLI_CONFIG_ALL), (unsigned)err);
    }
    return (err == ESP_OK) ? 0 : 1;
}

/* =========================================================================
 * Command: config.validate
 * ========================================================================= */

/**
 * @brief Validates the current in-memory settings against defined bounds.
 *
 * @param argc Argument count (unused).
 * @param argv Argument vector (unused).
 * @return 0 if valid, 1 if validation fails.
 */
static int dispatch_config_validate(int argc, char **argv) {
    const LocalizationCatalog *catalog=Localization_Snapshot();
    if (!config_command_arguments(argc, argv, false)) return 1;
    (void)argc; (void)argv;

    IConfigurationManager *mgr = get_mgr();
    if (!mgr) {
        LOC_PRINTF(catalog,LOC_CLI_CONFIG_UNAVAILABLE,"[ERROR] ConfigurationManager unavailable\n");
        return 1;
    }

    if (!mgr->ValidateSettings) return 1;
    esp_err_t err = mgr->ValidateSettings(mgr);
    if (err == ESP_OK) {
        LOC_PRINTF(catalog,LOC_CLI_CONFIG_VALID,"[OK] All settings are within valid bounds\n");
    } else {
        LOC_PRINTF(catalog,LOC_CLI_CONFIG_INVALID,"[FAIL] Settings validation failed (err=0x%x) – check logs\n",
               (unsigned)err);
    }
    return (err == ESP_OK) ? 0 : 1;
}

/* =========================================================================
 * Command: config.version
 * ========================================================================= */

/**
 * @brief Prints the NVS schema version of each configuration block.
 *
 * @param argc Argument count (unused).
 * @param argv Argument vector (unused).
 * @return 0 on successful observation, nonzero when metadata is unavailable.
 */
static int dispatch_config_version(int argc, char **argv) {
    const LocalizationCatalog *catalog=Localization_Snapshot();
    if (!config_command_arguments(argc, argv, false)) return 1;
    (void)argc; (void)argv;

    IConfigurationManager *mgr = get_mgr();
    if (!mgr) {
        LOC_PRINTF(catalog,LOC_CLI_CONFIG_UNAVAILABLE,"[ERROR] ConfigurationManager unavailable\n");
        return 1;
    }

    ConfigurationStatus status;
    if (!mgr->GetStatus || mgr->GetStatus(mgr, &status) != ESP_OK ||
        status.block_count != CONFIG_STATUS_BLOCK_CAPACITY) {
        LOC_PRINTF(catalog,LOC_CLI_CONFIG_METADATA_UNAVAILABLE,"[ERROR] Configuration metadata unavailable\n");
        return 1;
    }
    LOC_PRINTF(catalog,LOC_CLI_CONFIG_VERSIONS,"Config block versions:\n");
    for (size_t i = 0; i < status.block_count; ++i)
        printf("  %-20s v%u\n", status.blocks[i].name, (unsigned)status.blocks[i].version);
    return 0;
}

/* =========================================================================
 * Command: config.status
 * ========================================================================= */

/**
 * @brief Displays ConfigurationManager service status.
 *
 * Prints service state, mutex health, async task state, per-block dirty flags,
 * and whether a system restart is pending.
 *
 * @param argc Argument count (unused).
 * @param argv Argument vector (unused).
 * @return 0 on successful observation, nonzero when metadata is unavailable.
 */
static int dispatch_config_status(int argc, char **argv) {
    const LocalizationCatalog *catalog=Localization_Snapshot();
    if (!config_command_arguments(argc, argv, false)) return 1;
    (void)argc; (void)argv;

    IConfigurationManager *mgr = get_mgr();
    if (!mgr) {
        LOC_PRINTF(catalog,LOC_CLI_CONFIG_UNAVAILABLE,"[ERROR] ConfigurationManager unavailable\n");
        return 1;
    }

    ConfigurationStatus status;
    if (!mgr->GetStatus || mgr->GetStatus(mgr, &status) != ESP_OK ||
        status.block_count != CONFIG_STATUS_BLOCK_CAPACITY) {
        LOC_PRINTF(catalog,LOC_CLI_CONFIG_METADATA_UNAVAILABLE,"[ERROR] Configuration metadata unavailable\n");
        return 1;
    }

    /* Service state string. */
    IService *svc = (IService *)mgr;
    const char *state_str = Localization_Text(catalog,LOC_CLI_CONFIG_UNKNOWN);
    if (svc->GetState) {
        switch (svc->GetState(svc)) {
            case SERVICE_STATE_UNINITIALIZED: state_str = Localization_Text(catalog,LOC_CLI_CONFIG_UNINITIALIZED); break;
            case SERVICE_STATE_INITIALIZED:   state_str = Localization_Text(catalog,LOC_CLI_CONFIG_INITIALIZED);   break;
            case SERVICE_STATE_STARTED:       state_str = Localization_Text(catalog,LOC_CLI_CONFIG_STARTED);       break;
            case SERVICE_STATE_STOPPED:       state_str = Localization_Text(catalog,LOC_CLI_CONFIG_STOPPED);       break;
            case SERVICE_STATE_ERROR:         state_str = Localization_Text(catalog,LOC_CLI_CONFIG_ERROR);         break;
            default:                          state_str = Localization_Text(catalog,LOC_CLI_CONFIG_UNKNOWN);       break;
        }
    }

    LOC_PRINTF(catalog,LOC_CLI_CONFIG_STATUS_TITLE,"--- ConfigurationManager Status ---\n");
    LOC_PRINTF(catalog,LOC_CLI_CONFIG_STATE,"  Service state : %s\n", state_str);
    LOC_PRINTF(catalog,LOC_CLI_CONFIG_HEALTH,"  Health        : %s\n",
           (svc->CheckHealth && svc->CheckHealth(svc) == 0) ? Localization_Text(catalog,LOC_CLI_CONFIG_OK) : Localization_Text(catalog,LOC_CLI_CONFIG_FAULT));
    LOC_PRINTF(catalog,LOC_CLI_CONFIG_RESTART,"  Restart req.  : %s\n",
           status.restart_required ? Localization_Text(catalog,LOC_CLI_CONFIG_YES) : Localization_Text(catalog,LOC_CLI_CONFIG_NO));
    LOC_PRINTF(catalog,LOC_CLI_CONFIG_RETRY,"  Notify retry  : %s\n", status.notification_pending ? Localization_Text(catalog,LOC_CLI_CONFIG_PENDING) : Localization_Text(catalog,LOC_CLI_CONFIG_NONE));

    bool any_dirty = false;
    LOC_PRINTF(catalog,LOC_CLI_CONFIG_DIRTY,"  Dirty blocks  :");
    for (size_t i = 0; i < status.block_count; ++i) {
        if (status.blocks[i].dirty) {
            printf(" %s", status.blocks[i].name);
            any_dirty = true;
        }
    }
    printf("%s\n", any_dirty ? "" : Localization_Text(catalog,LOC_CLI_CONFIG_EMPTY));
    printf("-----------------------------------\n");
    return 0;
}

/* =========================================================================
 * Command: config.diff
 * ========================================================================= */

/**
 * @brief Shows differences between current settings and compile-time defaults.
 *
 * Compares every comparable numeric/bool field against its DEFAULT_* macro
 * value and prints only fields that differ.  Useful for auditing persistent
 * customisations.
 *
 * @param argc Argument count (unused).
 * @param argv Argument vector (unused).
 * @return 0 on successful observation, nonzero when metadata is unavailable.
 */
static int dispatch_config_diff(int argc, char **argv) {
    const LocalizationCatalog *catalog=Localization_Snapshot();
    if (!config_command_arguments(argc, argv, false)) return 1;
    (void)argc; (void)argv;

    IConfigurationManager *mgr = get_mgr();
    if (!mgr) {
        LOC_PRINTF(catalog,LOC_CLI_CONFIG_UNAVAILABLE,"[ERROR] ConfigurationManager unavailable\n");
        return 1;
    }

    SystemSettings snapshot;
    if (!mgr->GetSnapshot || mgr->GetSnapshot(mgr, &snapshot) != ESP_OK) {
        LOC_PRINTF(catalog,LOC_CLI_CONFIG_SNAPSHOT_UNAVAILABLE,"[ERROR] Configuration snapshot unavailable\n");
        return 1;
    }
    SystemSettings defaults;
    if (!mgr->GetDefaults || mgr->GetDefaults(mgr,&defaults)!=ESP_OK) return 1;
    int changes=0;
    LOC_PRINTF(catalog,LOC_CLI_CONFIG_DIFF_TITLE,"--- Config Diff (current vs default) ---\n");
#define CONFIG_DIFF_NUMBER(section,field) \
    if (snapshot.section.field!=defaults.section.field) { \
        LOC_PRINTF(catalog,LOC_CLI_CONFIG_DIFF_FIELD,"  %s: %.9g -> %.9g [MODIFIED]\n", \
            #section "." #field,(double)defaults.section.field,(double)snapshot.section.field); \
        ++changes; \
    }
    CONFIGURATION_OBSERVABLE_NUMBERS(CONFIG_DIFF_NUMBER)
    if (strcmp(snapshot.localization.locale,defaults.localization.locale)!=0) {
        LOC_PRINTF(catalog,LOC_CLI_CONFIG_DIFF_TEXT,"  %s: %s -> %s [MODIFIED]\n","localization.locale",defaults.localization.locale,snapshot.localization.locale);
        ++changes;
    }
#undef CONFIG_DIFF_NUMBER
    if (snapshot.restart_required!=defaults.restart_required) {
        LOC_PRINTF(catalog,LOC_CLI_CONFIG_DIFF_FIELD,"  %s: %.9g -> %.9g [MODIFIED]\n","restart_required",(double)defaults.restart_required,(double)snapshot.restart_required);
        ++changes;
    }
    LOC_PRINTF(catalog,LOC_CLI_CONFIG_DIFF_COUNT,"  Total modified fields: %d\n",changes);
    printf("RACOOL_CLI v=1 type=config_diff fields=%d\n",changes);
    return 0;
}

/* =========================================================================
 * Public registration
 * ========================================================================= */

/**
 * @brief Registers all ConfigurationManager commands with the DebugConsole.
 */
static int dispatch_config_verify_defaults(int argc, char **argv) {
    const LocalizationCatalog *catalog=Localization_Snapshot();
    if (!config_command_arguments(argc, argv, false)) return 1;
    IConfigurationManager *manager = get_mgr();
    SystemSettings verified;
    esp_err_t result = manager && manager->VerifyDefaults
        ? manager->VerifyDefaults(manager, &verified) : ESP_ERR_INVALID_STATE;
    if (result != ESP_OK) {
        LOC_PRINTF(catalog,LOC_CLI_CONFIG_DEFAULT_READ_FAILED,"[ERROR] Default readback verification failed: %ld\n", (long)result);
        return 1;
    }
    LOC_PRINTF(catalog,LOC_CLI_CONFIG_DEFAULTS_VERIFIED,"All %u persisted and runtime configuration blocks match defaults.\n",
           (unsigned)CONFIG_STATUS_BLOCK_CAPACITY);
    return 0;
}

static bool config_machine_arguments(int argc,char **argv) {
    if (argc<2 || argc>3 || !argv) return false;
    for (int i=0;i<argc;++i) if (!argv[i] || !argv[i][0]) return false;
    return strcmp(argv[argc-1],"--machine")==0;
}
static int cmd_config_show(int argc,char **argv) {
    bool machine=config_machine_arguments(argc,argv);
    int result=dispatch_config_show(machine ? argc-1 : argc,argv);
    if (machine) printf("RACOOL_CLI v=1 type=config_command command=show result=%s\n",result==0 ? "completed" : "rejected");
    return result;
}
static int cmd_config_save(int argc,char **argv) {
    bool machine=config_machine_arguments(argc,argv);
    int result=dispatch_config_save(machine ? argc-1 : argc,argv);
    if (machine) printf("RACOOL_CLI v=1 type=config_command command=save result=%s\n",result==0 ? "completed" : "rejected");
    return result;
}
static int cmd_config_load(int argc,char **argv) {
    bool machine=config_machine_arguments(argc,argv);
    int result=dispatch_config_load(machine ? argc-1 : argc,argv);
    if (machine) printf("RACOOL_CLI v=1 type=config_command command=load result=%s\n",result==0 ? "completed" : "rejected");
    return result;
}
static int cmd_config_reset(int argc,char **argv) {
    bool machine=argv && argc>=2 && argc<=4 && argv[argc-1] && strcmp(argv[argc-1],"--machine")==0;
    int tokens=argc-(machine ? 1 : 0);
    int result=1;
    if (!argv || tokens<2 || tokens>3 || !argv[0] || !argv[0][0] ||
        !argv[tokens-1] || strcmp(argv[tokens-1],"CONFIRM")!=0 ||
        (tokens==3 && (!argv[1] || !argv[1][0]))) {
        printf("Configuration reset requires: config.reset [block] CONFIRM\n");
    } else {
        char *arguments[] = {argv[0], tokens==3 ? argv[1] : NULL};
        result=dispatch_config_reset(tokens-1,arguments);
    }
    if (machine) printf("RACOOL_CLI v=1 type=config_command command=reset result=%s\n",result==0 ? "completed" : "rejected");
    return result;
}
static int cmd_config_validate(int argc,char **argv) {
    bool machine=config_machine_arguments(argc,argv);
    int result=dispatch_config_validate(machine ? argc-1 : argc,argv);
    if (machine) printf("RACOOL_CLI v=1 type=config_command command=validate result=%s\n",result==0 ? "completed" : "rejected");
    return result;
}
static int cmd_config_version(int argc,char **argv) {
    bool machine=config_machine_arguments(argc,argv);
    int result=dispatch_config_version(machine ? argc-1 : argc,argv);
    if (machine) printf("RACOOL_CLI v=1 type=config_command command=version result=%s\n",result==0 ? "completed" : "rejected");
    return result;
}
static int cmd_config_status(int argc,char **argv) {
    bool machine=config_machine_arguments(argc,argv);
    int result=dispatch_config_status(machine ? argc-1 : argc,argv);
    if (machine) printf("RACOOL_CLI v=1 type=config_command command=status result=%s\n",result==0 ? "completed" : "rejected");
    return result;
}
static int cmd_config_diff(int argc,char **argv) {
    bool machine=config_machine_arguments(argc,argv);
    int result=dispatch_config_diff(machine ? argc-1 : argc,argv);
    if (machine) printf("RACOOL_CLI v=1 type=config_command command=diff result=%s\n",result==0 ? "completed" : "rejected");
    return result;
}
static int cmd_config_verify_defaults(int argc,char **argv) {
    bool machine=config_machine_arguments(argc,argv);
    int result=dispatch_config_verify_defaults(machine ? argc-1 : argc,argv);
    if (machine) printf("RACOOL_CLI v=1 type=config_command command=verify_defaults result=%s\n",result==0 ? "completed" : "rejected");
    return result;
}

esp_err_t ConfigurationManagerCommands_Register(IDebugConsole *console) {
    if (!console || !console->RegisterCommandWithCategory) return ESP_ERR_INVALID_ARG;

    static const esp_console_cmd_t commands[] = {
        {
            .command = "config.verify_defaults",
            .help = "Read-only verification of all six live and persisted default blocks",
            .func = cmd_config_verify_defaults
        },
        {
            .command = "config.show",
            .help    = "Print all current system settings",
            .hint    = NULL,
            .func    = cmd_config_show,
            .argtable = NULL
        },
        {
            .command = "config.save",
            .help    = "Save config to NVS. Usage: config.save [block]",
            .hint    = "[block]",
            .func    = cmd_config_save,
            .argtable = NULL
        },
        {
            .command = "config.load",
            .help    = "Reload config from NVS. Usage: config.load [block]",
            .hint    = "[block]",
            .func    = cmd_config_load,
            .argtable = NULL
        },
        {
            .command = "config.reset",
            .help    = "Reset config to factory defaults. Usage: config.reset [block] CONFIRM",
            .hint    = "[block] CONFIRM",
            .func    = cmd_config_reset,
            .argtable = NULL
        },
        {
            .command = "config.validate",
            .help    = "Validate current in-memory settings",
            .hint    = NULL,
            .func    = cmd_config_validate,
            .argtable = NULL
        },
        {
            .command = "config.version",
            .help    = "Show NVS schema version of each config block",
            .hint    = NULL,
            .func    = cmd_config_version,
            .argtable = NULL
        },
        {
            .command = "config.status",
            .help    = "Show ConfigurationManager service status (state, dirty flags, restart)",
            .hint    = NULL,
            .func    = cmd_config_status,
            .argtable = NULL
        },
        {
            .command = "config.diff",
            .help    = "Show diff between current settings and compile-time defaults",
            .hint    = NULL,
            .func    = cmd_config_diff,
            .argtable = NULL
        },
    };

    static const size_t cmd_count = sizeof(commands) / sizeof(commands[0]);

    for (size_t i = 0; i < cmd_count; i++) {
        esp_err_t err = console->RegisterCommandWithCategory(
            console, &commands[i], CMD_CAT_SYSTEM);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to register '%s' (err=0x%x)",
                     commands[i].command, (unsigned)err);
            return err;
        }
    }

    ESP_LOGI(TAG, "%u ConfigurationManager commands registered",
             (unsigned)cmd_count);
    return ESP_OK;
}
