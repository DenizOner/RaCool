#include "Core/Localization/Localization.h"
#include "Core/LogManager/ILogManager.h"
#include "Core/LogManager/LogManagerConsoleCommands.h"
#include "Core/ServiceLocator/ServiceLocator.h"
#include "Core/ServiceLocator/ServiceNames.h"
#include "Communication/DebugConsole/IDebugConsole.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>


static int cmd_log_status(int argc, char **argv);
static int cmd_log_level_set(int argc, char **argv);
static int cmd_log_module_set(int argc, char **argv);
static int cmd_log_recent(int argc, char **argv);

static bool Log_ParseUnsigned(const char *text, unsigned long minimum, unsigned long maximum, unsigned long *out) {
    if (!text || !*text || !out) return false;
    for (const char *p = text; *p; ++p) if (*p < '0' || *p > '9') return false;
    char *end = NULL;
    errno = 0;
    unsigned long value = strtoul(text, &end, 10);
    if (errno == ERANGE || !end || *end || value < minimum || value > maximum) return false;
    *out = value;
    return true;
}

esp_err_t LogManager_RegisterConsoleCommands(void) {

    IDebugConsole *console = (IDebugConsole *)ServiceLocator_GetService(SVC_NAME_DEBUG_CONSOLE);
    if (!console || !console->RegisterCommandWithCategory) return ESP_ERR_INVALID_STATE;

    static const esp_console_cmd_t cmds[] = {
        { .command = "log_status",     .help = "Show LogManager state",                                          .func = &cmd_log_status,     .hint = NULL           },
        { .command = "log_level_set",  .help = "Set global log level (0=None..5=Verbose): log_level_set <level>", .func = &cmd_log_level_set,  .hint = "<level>"      },
        { .command = "log_module_set", .help = "Set per-tag log level: log_module_set <tag> <level>",            .func = &cmd_log_module_set, .hint = "<tag> <level>" },
        { .command = "log_recent",     .help = "Print last N captured log entries: log_recent [n]",              .func = &cmd_log_recent,     .hint = "[n]"          },
    };

    for (size_t i = 0; i < sizeof(cmds) / sizeof(cmds[0]); i++) {
        esp_err_t result = console->RegisterCommandWithCategory(console, &cmds[i], CMD_CAT_SYSTEM);
        if (result != ESP_OK) return result;
    }
    return ESP_OK;
}

static int cmd_log_status(int argc, char **argv) {
    const LocalizationCatalog *catalog = Localization_Snapshot();
    (void)argv;
    if (argc != 1) return 1;
    ILogManager *lm = GetLogManager();
    if (!lm) { LOC_PRINTF(catalog, LOC_CLI_LOG_UNAVAILABLE, "LogManager not available.\n"); return 1; }
    LOC_PRINTF(catalog, LOC_CLI_LOG_TITLE, "\n--- LogManager Status ---\n");
    LOC_PRINTF(catalog, LOC_CLI_LOG_STATE, "State : %d\n", (int)lm->base.GetState((IService *)lm));
    printf("-------------------------\n");
    return 0;
}

static int cmd_log_level_set(int argc, char **argv) {
    const LocalizationCatalog *catalog = Localization_Snapshot();
    unsigned long value;
    if (argc != 2 || !Log_ParseUnsigned(argv[1], ESP_LOG_NONE, ESP_LOG_VERBOSE, &value)) {
        LOC_PRINTF(catalog, LOC_CLI_LOG_LEVEL_USAGE, "Usage: log_level_set <level (0-5)>\n"); return 1;
    }
    ILogManager *lm = GetLogManager();
    if (!lm) { LOC_PRINTF(catalog, LOC_CLI_LOG_UNAVAILABLE, "LogManager not available.\n"); return 1; }
    esp_log_level_t lvl = (esp_log_level_t)value;
    lm->SetGlobalLogLevel(lvl);
    LOC_PRINTF(catalog, LOC_CLI_LOG_LEVEL_SET, "Global log level set to %d.\n", (int)lvl);
    return 0;
}

static int cmd_log_module_set(int argc, char **argv) {
    const LocalizationCatalog *catalog = Localization_Snapshot();
    unsigned long value;
    if (argc != 3 || !argv[1] || !*argv[1] || strlen(argv[1]) > 64 ||
        !Log_ParseUnsigned(argv[2], ESP_LOG_NONE, ESP_LOG_VERBOSE, &value)) {
        LOC_PRINTF(catalog, LOC_CLI_LOG_MODULE_USAGE, "Usage: log_module_set <tag> <level (0-5)>\n"); return 1;
    }
    ILogManager *lm = GetLogManager();
    if (!lm) { LOC_PRINTF(catalog, LOC_CLI_LOG_UNAVAILABLE, "LogManager not available.\n"); return 1; }
    esp_log_level_t lvl = (esp_log_level_t)value;
    lm->SetModuleLogLevel(argv[1], lvl);
    LOC_PRINTF(catalog, LOC_CLI_LOG_MODULE_SET, "Log level for '%s' set to %d.\n", argv[1], (int)lvl);
    return 0;
}

static int cmd_log_recent(int argc, char **argv) {
    const LocalizationCatalog *catalog = Localization_Snapshot();
    unsigned long requested = 10;
    if (argc < 1 || argc > 2 || (argc == 2 && !Log_ParseUnsigned(argv[1], 1, 100, &requested))) return 1;
    ILogManager *lm = GetLogManager();
    if (!lm) { LOC_PRINTF(catalog, LOC_CLI_LOG_UNAVAILABLE, "LogManager not available.\n"); return 1; }
    uint32_t n = (uint32_t)requested;

    SystemLogEntry *entries = malloc(sizeof(SystemLogEntry) * n);
    if (!entries) { LOC_PRINTF(catalog, LOC_CLI_LOG_ALLOCATION_FAILED, "Memory allocation failed.\n"); return 1; }

    uint32_t count = lm->GetRecentLogs(entries, n);
    LOC_PRINTF(catalog, LOC_CLI_LOG_RECENT_TITLE, "\n--- Last %lu Log Entries ---\n", (unsigned long)count);
    for (uint32_t i = 0; i < count; i++) {
        printf("[%4lu] [%s] [%-12s] %s\n",
               (unsigned long)entries[i].id,
               entries[i].level,
               entries[i].module,
               entries[i].message);
    }
    printf("----------------------------\n");
    free(entries);
    return 0;
}
