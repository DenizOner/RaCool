#include "Management/ConfigurationManager/ConfigurationConsoleView.h"
#include "esp_heap_caps.h"
#include "Communication/DebugConsole/Commands/SystemCommands.h"
#include "Communication/DebugConsole/Core/CommandRegistry.h"
#include "Communication/DebugConsole/Core/DeveloperMode.h"
#include "Communication/DebugConsole/Core/ConsoleColors.h"
#include "Communication/DebugConsole/Core/DashboardManager.h"
#include "Communication/DebugConsole/Core/FlashLogger.h"
#include "Communication/DebugConsole/Core/IDashboard.h"
#include "Communication/DebugConsole/DebugConsoleConfig.h"
#include "Communication/DebugConsole/IDebugConsole.h"
#include "Core/ServiceLocator/ServiceLocator.h"
#include "esp_chip_info.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "Management/ConfigurationManager/IConfigurationManager.h"
#include "Management/ApplicationManager/IApplicationManager.h"
#include "Management/PowerManager/IPowerManager.h"
#include "Hardware/RGBLED/RGBLEDColorEngine.h"
#include "Management/ConfigurationManager/SystemConfigs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <esp_flash.h>
#include "Communication/Zigbee/IZigbee.h"
#include "Core/ServiceLocator/ServiceNames.h"
#include "Core/Localization/Localization.h"
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static bool System_RestartPending(void) {
    IPowerManager *power = (IPowerManager *)ServiceLocator_GetService(SVC_NAME_POWER_MANAGER);
    return power && power->IsTransitionPending && power->IsTransitionPending(power);
}

static bool s_resetPending = false;
static bool s_factoryResetPending = false;
static int print_developer_tools(const char *category);

static int cmd_developer(int argc, char **argv) {
    if (!argv || argc < 1 || argc > 3) return 1;
    if (argc >= 2 && argv[1] && !strcasecmp(argv[1], "TOOLS")) {
        if (!DeveloperMode_IsEnabled()) {
            printf("Developer Tools are unavailable. Use DEVELOPER ON.\n");
            return 1;
        }
        return print_developer_tools(argc == 3 ? argv[2] : NULL);
    }
    bool machine = argc > 1 && argv[argc - 1] && strcmp(argv[argc - 1], "--machine") == 0;
    if (machine) --argc;
    if (argc == 2 && argv[1]) {
        if (strcasecmp(argv[1], "ON") == 0 || strcasecmp(argv[1], "OFF") == 0) {
            IConfigurationManager *configuration = (IConfigurationManager *)ServiceLocator_GetService(SVC_NAME_CONFIGURATION_MANAGER);
            uint8_t enabled = strcasecmp(argv[1], "ON") == 0 ? 1u : 0u;
            ConfigurationFieldUpdate field = {offsetof(SystemBaseConfigs, developer_mode_enabled), &enabled, sizeof(enabled)};
            if (!configuration || !configuration->UpdateFields ||
                configuration->UpdateFields(configuration, "system", &field, 1, true) != ESP_OK) {
                printf("Developer Mode preference could not be saved.\n");
                return 1;
            }
        } else if (strcasecmp(argv[1], "STATUS") != 0) return 1;
    } else if (argc != 1) return 1;
    bool enabled = DeveloperMode_IsEnabled();
    if (machine) printf("RACOOL_CLI v=1 type=developer_mode enabled=%u\n", enabled ? 1u : 0u);
    else printf("Developer Mode: %s\n", enabled ? "ON" : "OFF");
    return 0;
}

static const char *LocalizedCommandHelp(const CommandEntry *entry, const LocalizationCatalog *catalog) {
    const char *usage = DeveloperMode_UserUsage(entry->command);
    if (usage) return usage;
    if (!strcasecmp(entry->command, "power")) return "power [mode|sleep|restart]";
    if (!strcasecmp(entry->command, "ir_temp")) return "ir_temp [trigger]";
    if (!strcasecmp(entry->command, "int_temp")) return "int_temp [read]";
    if (!strcasecmp(entry->command, "bled")) return "bled [on|off|anim|brightness]";
    char key[96] = "cli.command_help.";
    size_t prefix = strlen(key);
    size_t index = 0;
    while (index < sizeof(entry->command) && entry->command[index] && prefix + index + 1 < sizeof(key)) {
        unsigned char letter = (unsigned char)entry->command[index];
        key[prefix + index] = (char)tolower(letter);
        ++index;
    }
    key[prefix + index] = '\0';
    const char *translated = Localization_TextKey(catalog, key);
    return translated ? translated : entry->help;
}

static int cmd_language(int argc, char **argv) {
    const LocalizationCatalog *catalog = Localization_Snapshot();
    if (!argv || argc < 1) return 1;
    if (argc == 2 && argv[1] && strcmp(argv[1], "--machine") == 0) {
        /* Stable read-only contract; never translate protocol keys or values. */
        printf("RACOOL_CLI v=1 type=language locale=%s\n", catalog->code);
        return 0;
    }
    if (argc == 3 && argv[1] && argv[2] &&
        strcasecmp(argv[1], "CONFIG") == 0 && strcmp(argv[2], "--machine") == 0) {
        IConfigurationManager *configuration = (IConfigurationManager *)ServiceLocator_GetService(SVC_NAME_CONFIGURATION_MANAGER);
        SystemSettings snapshot;
        esp_err_t result = configuration && configuration->GetSnapshot
            ? configuration->GetSnapshot(configuration, &snapshot) : ESP_ERR_INVALID_STATE;
        if (result != ESP_OK || !SystemConfigs_IsLocalizationValid(&snapshot.localization)) {
            printf("RACOOL_CLI v=1 type=language_config result=unavailable\n");
            return 1;
        }
        const LocalizationCatalog *saved = Localization_Find(snapshot.localization.locale);
        printf("RACOOL_CLI v=1 type=language_config saved=%s active=%s restorable=%u\n",
               snapshot.localization.locale, catalog->code, saved ? 1u : 0u);
        return 0;
    }
    if (argc == 3 && argv[1] && argv[2] &&
        strcasecmp(argv[1], "LIST") == 0 && strcmp(argv[2], "--machine") == 0) {
        for (size_t index = 0; index < Localization_Count(); ++index) {
            printf("RACOOL_CLI v=1 type=language_catalog locale=%s\n", Localization_At(index)->code);
        }
        printf("RACOOL_CLI v=1 type=language_catalog_end count=%zu\n", Localization_Count());
        return 0;
    }
    if (argc == 1) {
        LOC_PRINTF(catalog, LOC_CLI_LANGUAGE_CURRENT, "Language: %s\n", catalog->name);
        return 0;
    }
    if (argc == 2 && argv[1] && strcasecmp(argv[1], "LIST") == 0) {
        for (size_t index = 0; index < Localization_Count(); ++index) {
            const LocalizationCatalog *available = Localization_At(index);
            printf("%s: %s\n", available->code, available->name);
        }
        return 0;
    }
    bool machine = argc == 4 && argv[3] && strcmp(argv[3], "--machine") == 0;
    if ((argc != 3 && !machine) || !argv[1] || !argv[2] || strcasecmp(argv[1], "SET") != 0) {
        printf("%s", Localization_Text(catalog, LOC_CLI_LANGUAGE_USAGE));
        return 1;
    }
    const LocalizationCatalog *selected = Localization_Find(argv[2]);
    if (!selected) {
        if (machine) { printf("RACOOL_CLI v=1 type=language_set result=invalid_locale\n"); return 1; }
        LOC_PRINTF(catalog, LOC_CLI_LANGUAGE_INVALID, "Unknown language: %s\n", argv[2]);
        return 1;
    }
    char code[LOCALIZATION_LOCALE_CAPACITY] = {0};
    size_t length = strlen(selected->code);
    if (length >= sizeof(code)) return 1;
    memcpy(code, selected->code, length);
    IConfigurationManager *configuration = (IConfigurationManager *)ServiceLocator_GetService(SVC_NAME_CONFIGURATION_MANAGER);
    ConfigurationFieldUpdate field = {offsetof(LocalizationConfigs, locale), code, sizeof(code)};
    esp_err_t result = configuration && configuration->UpdateFields
        ? configuration->UpdateFields(configuration, "localization", &field, 1, true) : ESP_ERR_INVALID_STATE;
    if (result != ESP_OK) {
        if (machine) { printf("RACOOL_CLI v=1 type=language_set result=save_failed error=%ld\n", (long)result); return 1; }
        LOC_PRINTF(catalog, LOC_CLI_LANGUAGE_SAVE_FAILED,
                   "Language was not changed; saving failed: %ld\n", (long)result);
        return 1;
    }
    if (machine) { printf("RACOOL_CLI v=1 type=language_set result=applied locale=%s\n", selected->code); return 0; }
    catalog = Localization_Snapshot();
    LOC_PRINTF(catalog, LOC_CLI_LANGUAGE_APPLIED, "Language applied: %s\n", selected->name);
    return 0;
}

static int cmd_factory_reset(int argc, char **argv) {
    const LocalizationCatalog *catalog = Localization_Snapshot();
    if (argc != 1 || !argv || System_RestartPending()) return 1;
    LOC_PRINTF(catalog, LOC_CLI_SYSTEM_FACTORY_PROMPT,  
           "\n!!! DIKKAT: CIHAZ FABRIKA AYARLARINA SIFIRLANACAK !!!\n"
           "Proje ayarlari varsayilana donecek; Zigbee ag sifirlama istegi gonderilecek. Emin misiniz? (y/n)\n" );
    fflush(stdout);
    s_resetPending = false;
    s_factoryResetPending = true;
    return 0;
}

/* ─────────────────────────────────────────────
 * Command implementations
 * ───────────────────────────────────────────── */

static const char *CommandCategoryText(CommandCategory category, const LocalizationCatalog *catalog) {
    static const LocalizationMessage messages[] = {
        LOC_CLI_CATEGORY_SYSTEM, LOC_CLI_CATEGORY_DIAGNOSTIC, LOC_CLI_CATEGORY_PERFORMANCE,
        LOC_CLI_CATEGORY_HARDWARE, LOC_CLI_CATEGORY_NETWORK, LOC_CLI_CATEGORY_DEBUG,
        LOC_CLI_CATEGORY_CUSTOM, LOC_CLI_CATEGORY_UNCATEGORIZED
    };
    _Static_assert(sizeof(messages) / sizeof(messages[0]) == CMD_CAT_MAX, "Category catalog must cover every category");
    return Localization_Text(catalog, messages[category]);
}

static const char *developer_category(const CommandEntry *entry) {
    const char *name = entry->command;
    if (!strncasecmp(name, "LOG", 3) || !strcasecmp(name, "DEBUG") || !strcasecmp(name, "DBG")) return "logging";
    if (!strcasecmp(name, "ZIGBEE") || !strcasecmp(name, "ZB") ||
        !strcasecmp(name, "NET") || !strcasecmp(name, "NETWORK")) return "protocols";
    if (!strcasecmp(name, "I2C") || !strcasecmp(name, "PIN") || !strcasecmp(name, "OLED") ||
        !strncasecmp(name, "ENC", 3) || !strcasecmp(name, "SER_PERF") ||
        !strncasecmp(name, "builtin_led_", 12) || !strncasecmp(name, "rgbled_", 7) ||
        !strcasecmp(name, "relay_info")) return "hardware";
    if (!strcasecmp(name, "PERF") || !strcasecmp(name, "PERFORMANCE") ||
        !strncasecmp(name, "perf_", 5) || !strcasecmp(name, "DASH")) return "metrics";
    return "diagnostics";
}

static bool developer_read_only(const char *name) {
    static const char *const names[] = {
        "MEM", "perf_status", "log_status", "log_recent", "svc_list", "svc_get",
        "error_status", "error_history", "bus_status", "sys_init_status", "ENC", "ENCDIAG",
        "oled_info", "relay_info", "builtin_led_info", "rgbled_info"
    };
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
        if (!strcmp(name, names[i])) return true;
    return false;
}

static int print_developer_tools(const char *category) {
    static const char *const categories[] = {"logging", "diagnostics", "hardware", "protocols", "metrics"};
    bool matched = category == NULL;
    if (category) for (size_t i = 0; i < sizeof(categories) / sizeof(categories[0]); ++i)
        if (!strcasecmp(category, categories[i])) matched = true;
    if (!matched) return 1;
    const LocalizationCatalog *catalog = Localization_Snapshot();
    printf("\nDeveloper Tools (DEVELOPER TOOLS <category>)\n");
    for (size_t group = 0; group < sizeof(categories) / sizeof(categories[0]); ++group) {
        if (category && strcasecmp(category, categories[group])) continue;
        printf("\n%s:\n", categories[group]);
        for (unsigned changes = 0; changes < 2; ++changes) {
            printf("  %s\n", changes ? "Controls (may change state):" : "Read-only:");
            unsigned cursor = 0;
            CommandEntry entry;
            int result;
            while ((result = CommandRegistry_CopyMatches(NULL, CMD_CAT_MAX, &cursor, &entry, 1)) > 0) {
                if (!DeveloperMode_IsUserRoot(entry.command) && !strcasecmp(developer_category(&entry), categories[group]) &&
                    developer_read_only(entry.command) == (changes == 0))
                    printf("    %-15s : %s\n", entry.command, LocalizedCommandHelp(&entry, catalog));
            }
            if (result < 0) return 1;
            if (!strcmp(categories[group], "diagnostics")) {
                if (!changes) printf("    SYSTEM STATUS|UPTIME|MEM|TASKS|SPI|HELP_CAT|HISTORY|LOGSTATS|CONFIG; CLIMATE HIST|ACTION|SOURCES|CPU\n    power status|locks\n");
                else printf("    SYSTEM LIVE; CLIMATE TEST; power selftest\n");
            }
            if (!strcmp(categories[group], "hardware")) {
                if (!changes) printf("    HW GPIO|PINS|OLED|ENC; HW TEMP STATS; menu status|visible\n    ir_temp status|stats|lasterr|raw; int_temp status|stats; bled status; rgb status --machine; UI STATUS|CONFIG; RELAY INFO\n");
                else printf("    HW SCAN|SIM|TEMP RESET|TEST|OFFSET|POLL|LIMITS\n    menu nav|reset|btn|click|enc|fwd|bwd|test; UI EVENT|TEST; bled state\n    ir_temp reset|selftest|poll; int_temp reset_stats|selftest|offset\n    RELAY PULSE|AUTO_OFF|MODE|RESET_STATS|TEST\n");
            }
            if (!strcmp(categories[group], "protocols"))
                fputs(changes ? "    ZB SYNC|RESTART|RESET CONFIRM\n" : "    ZB INFO|DIAG|ROUTERS\n", stdout);
        }
    }
    return 0;
}

static int cmd_help_cat(int argc, char **argv) {
    const LocalizationCatalog *catalog = Localization_Snapshot();
    if (argc != 2 || !argv || !argv[1]) {
        printf("%s", Localization_Text(catalog, LOC_CLI_HELP_CATEGORY_USAGE));
        for (int category = 0; category < CMD_CAT_MAX; ++category)
            printf("%d: %s\n", category, CommandCategoryText((CommandCategory)category, catalog));
        return 1;
    }
    char *end = NULL;
    errno = 0;
    long category = strtol(argv[1], &end, 10);
    if (errno || end == argv[1] || *end || category < 0 || category >= CMD_CAT_MAX) {
        LOC_PRINTF(catalog, LOC_CLI_HELP_INVALID_CATEGORY, "Invalid category: %s\n", argv[1]);
        return 1;
    }

    CommandCategory cat = (CommandCategory)category;
    CommandEntry *results = malloc(DEBUG_CONSOLE_MAX_COMMANDS * sizeof(*results));
    if (!results) return 1;
    unsigned cursor = 0;
    int count = CommandRegistry_CopyMatches(NULL, cat, &cursor, results, DEBUG_CONSOLE_MAX_COMMANDS);
    if (count < 0) { free(results); return 1; }

    LOC_PRINTF(catalog, LOC_CLI_HELP_CATEGORY_TITLE, "\n--- %s Commands (%d) ---\n",
        CommandCategoryText(cat, catalog), count);
    for (int i = 0; i < count; i++) {
        if (DeveloperMode_IsEnabled() || DeveloperMode_IsVisibleUserRoot(results[i].command))
            printf("  %-15s : %s\n", results[i].command, LocalizedCommandHelp(&results[i], catalog));
    }
    free(results);
    return 0;
}

static int cmd_help(int argc, char **argv) {
    if (!argv || argc < 1 || argc > 2) return 1;
    if (argc == 2) {
        if (!argv[1] || !DeveloperMode_IsEnabled()) return 1;
        return print_developer_tools(!strcasecmp(argv[1], "developer") ? NULL : argv[1]);
    }
    const LocalizationCatalog *catalog = Localization_Snapshot();
    printf("%s", Localization_Text(catalog, LOC_CLI_HELP_TITLE));
    CommandEntry *results = malloc(DEBUG_CONSOLE_MAX_COMMANDS * sizeof(*results));
    if (!results) return 1;
    for (int category = 0; category < CMD_CAT_MAX; ++category) {
        unsigned cursor = 0;
        int count = CommandRegistry_CopyMatches(NULL, (CommandCategory)category, &cursor, results, DEBUG_CONSOLE_MAX_COMMANDS);
        if (count < 0) { free(results); return 1; }
        int visible = 0;
        for (int i = 0; i < count; ++i) if (DeveloperMode_IsVisibleUserRoot(results[i].command)) ++visible;
        if (!visible) continue;
        LOC_PRINTF(catalog, LOC_CLI_HELP_CATEGORY_TITLE, "\n--- %s Commands (%d) ---\n",
            CommandCategoryText((CommandCategory)category, catalog), visible);
        /* Print full UTF-8 descriptions; byte truncation can split a glyph. */
        for (int index = 0; index < count; ++index)
            if (DeveloperMode_IsVisibleUserRoot(results[index].command))
                printf("  %-15s : %s\n", results[index].command, LocalizedCommandHelp(&results[index], catalog));
    }
    if (DeveloperMode_IsEnabled()) printf("\nDeveloper Tools: DEVELOPER TOOLS\n");
    free(results);
    return 0;
}

static int cmd_find(int argc, char **argv) {
    const LocalizationCatalog *catalog = Localization_Snapshot();
    if (argc != 2 || !argv || !argv[1]) {
        printf("%s", Localization_Text(catalog, LOC_CLI_FIND_USAGE));
        return 1;
    }

    LOC_PRINTF(catalog, LOC_CLI_FIND_TITLE, "\n--- Search Results for '%s' ---\n", argv[1]);
    CommandEntry matches[10];
    unsigned cursor = 0;
    int count = CommandRegistry_CopyMatches(argv[1], CMD_CAT_MAX, &cursor, matches, 10);
    if (count < 0) return 1;
    if (count == 0) {
        printf("%s", Localization_Text(catalog, LOC_CLI_FIND_EMPTY));
    }
    for (int i = 0; i < count; i++) {
        const CommandEntry *e = &matches[i];
        if (e && (DeveloperMode_IsEnabled() || DeveloperMode_IsUserRoot(e->command))) {
            printf("  %-15s : %s\n", e->command, LocalizedCommandHelp(e, catalog));
        }
    }
    return 0;
}

static int cmd_logstats(int argc, char **argv) {
    const LocalizationCatalog *catalog=Localization_Snapshot();
    if (argc != 1 || !argv) return 1;
    FlashLoggerStats stats;
    if (FlashLogger_GetStats(&stats) != ESP_OK) {LOC_PRINTF(catalog,LOC_CLI_SYSTEM_FLASH_STATS_UNAVAILABLE,"Flash logger statistics unavailable.\n");return 1;}
    LOC_PRINTF(catalog,LOC_CLI_SYSTEM_FLASH_STATS_TITLE,"\n--- Flash Logger Statistics ---\n");
    LOC_PRINTF(catalog,LOC_CLI_SYSTEM_FLASH_WRITTEN," Written logs: %lu\n", (unsigned long)stats.totalLogsWritten);
    LOC_PRINTF(catalog,LOC_CLI_SYSTEM_FLASH_DROPPED," Dropped logs: %lu\n", (unsigned long)stats.droppedLogs);
    LOC_PRINTF(catalog,LOC_CLI_SYSTEM_FLASH_TIMESTAMP," Last log timestamp: %lu ms\n", (unsigned long)stats.lastLogTimestamp);
    LOC_PRINTF(catalog,LOC_CLI_SYSTEM_FLASH_FILE," File index: %lu\n", (unsigned long)stats.currentFileIndex);
    LOC_PRINTF(catalog,LOC_CLI_SYSTEM_LOG_FILE_SIZE," File size: %lu bytes\n", (unsigned long)stats.currentFileSize);
    printf("---------------------------------\n");
    return 0;
}

static int cmd_cls(int argc, char **argv) {
    printf(CLI_CLR_SCR);
    return 0;
}

static int cmd_reset(int argc, char **argv) {
    const LocalizationCatalog *catalog = Localization_Snapshot();
    if (argc != 1 || !argv || System_RestartPending()) return 1;
    s_factoryResetPending = false;
    LOC_PRINTF(catalog, LOC_CLI_SYSTEM_RESTART_PROMPT,  
           "\n!!! DIKKAT: Cihaz yeniden baslatilacak. "
           "Emin misiniz? (y/n) !!!\n" );
    LOC_PRINTF(catalog, LOC_CLI_SYSTEM_CONFIRM_USAGE, "Onaylamak icin 'y' veya 'yes' yazin. Vazgecmek icin 'n' yazin.\n");
    fflush(stdout);
    s_resetPending = true;
    return 0;
}

static int cmd_uptime(int argc, char **argv) {
    const LocalizationCatalog *catalog = Localization_Snapshot();
    uint32_t total_s = (uint32_t)(esp_timer_get_time() / 1000000);
    uint32_t days = total_s / 86400;
    uint32_t hours = (total_s % 86400) / 3600;
    uint32_t mins = (total_s % 3600) / 60;
    uint32_t secs = total_s % 60;

    LOC_PRINTF(catalog, LOC_CLI_SYSTEM_UPTIME_TITLE, "\n"  "Sistem Calisma Suresi: " );
    if (days > 0) {
        LOC_PRINTF(catalog, LOC_CLI_SYSTEM_UPTIME_DAYS, "%lu gun %lu saat %lu dk %lu sn", (unsigned long)days, (unsigned long)hours, (unsigned long)mins, (unsigned long)secs);
    } else {
        printf("%02lu:%02lu:%02lu", (unsigned long)hours, (unsigned long)mins, (unsigned long)secs);
    }
    printf("\n" CLI_COLOR_RESET);
    return 0;
}

static int cmd_tasks(int argc, char **argv) {
    const LocalizationCatalog *catalog = Localization_Snapshot();
#if CONFIG_FREERTOS_USE_STATS_FORMATTING_FUNCTIONS
    char *buf = malloc(2048);
    if (buf == NULL) {
        LOC_PRINTF(catalog, LOC_CLI_SYSTEM_ALLOCATION_FAILED, "Bellek ayirma hatasi!\n");
        return 1;
    }
    LOC_PRINTF(catalog, LOC_CLI_SYSTEM_TASKS_TITLE, "\nTask Ismi       Durum   Pri     Stack   No      Core\n");
    printf("----------------------------------------------------\n");
    vTaskList(buf);
    printf("%s", buf);
    free(buf);
#else
    LOC_PRINTF(catalog, LOC_CLI_SYSTEM_TASKS_UNAVAILABLE, "TASKS komutu icin CONFIG_FREERTOS_USE_STATS_FORMATTING_FUNCTIONS sdkconfig icinde aktif olmalidir.\n");
#endif
    return 0;
}

static int cmd_meminfo(int argc, char **argv) {
    const LocalizationCatalog *catalog = Localization_Snapshot();
    LOC_PRINTF(catalog, LOC_CLI_SYSTEM_MEMORY_TITLE, "\n--- Bellek Bilgileri ---\n");
    LOC_PRINTF(catalog, LOC_CLI_SYSTEM_MEMORY_FREE, "Toplam Bos Heap:   %7u byte\n", (unsigned int)esp_get_free_heap_size());
    LOC_PRINTF(catalog, LOC_CLI_SYSTEM_MEMORY_MIN, "Min Bos Heap:      %7u byte\n", (unsigned int)esp_get_minimum_free_heap_size());
    LOC_PRINTF(catalog, LOC_CLI_SYSTEM_MEMORY_INTERNAL, "Dahili Bos Heap:   %7u byte\n", (unsigned int)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
#ifdef MALLOC_CAP_SPIRAM
    LOC_PRINTF(catalog, LOC_CLI_SYSTEM_MEMORY_SPI, "SPI RAM Bos:       %7u byte\n", (unsigned int)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
#endif
    printf("-------------------------\n");
    return 0;
}

static int cmd_spiinfo(int argc, char **argv) {
    const LocalizationCatalog *catalog = Localization_Snapshot();
    uint32_t flash_id = 0;
    uint32_t flash_size = 0;
    esp_chip_info_t chip_info;
    
    esp_chip_info(&chip_info);
    esp_flash_read_id(NULL, &flash_id);
    esp_flash_get_physical_size(NULL, &flash_size);

    LOC_PRINTF(catalog, LOC_CLI_SYSTEM_HARDWARE_TITLE, "\n--- Donanim ve Flash Bilgileri ---\n");
    LOC_PRINTF(catalog, LOC_CLI_SYSTEM_CHIP_MODEL, "- Chip Model: %s (Revision %d)\n", (chip_info.model == CHIP_ESP32C6 ? "ESP32-C6" : Localization_Text(catalog, LOC_CLI_SYSTEM_UNKNOWN_MODEL)), chip_info.revision);
    LOC_PRINTF(catalog, LOC_CLI_SYSTEM_CPU_CORES, "- CPU Cores: %d\n", chip_info.cores);
    LOC_PRINTF(catalog, LOC_CLI_SYSTEM_FLASH_SIZE, "- Flash Fiziksel Boyut: %u MB\n", (unsigned int)(flash_size / (1024 * 1024)));
    LOC_PRINTF(catalog,LOC_CLI_SYSTEM_FLASH_ID,"- Flash ID: 0x%06lx\n", (unsigned long)flash_id);
    LOC_PRINTF(catalog, LOC_CLI_SYSTEM_FEATURES, "- Özellikler: %s %s %s\n", 
           (chip_info.features & CHIP_FEATURE_WIFI_BGN ? "WiFi" : ""),
           (chip_info.features & CHIP_FEATURE_BLE ? "BLE" : ""),
           (chip_info.features & CHIP_FEATURE_IEEE802154 ? "Zigbee/Thread" : ""));
    printf("----------------------------------\n");
    
    return 0;
}

static int cmd_status(int argc, char **argv) {
    const LocalizationCatalog *catalog = Localization_Snapshot();
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);

    LOC_PRINTF(catalog, LOC_CLI_SYSTEM_STATUS_TITLE, "\n"  "--- Sistem Ozeti ---\n" );
    LOC_PRINTF(catalog, LOC_CLI_SYSTEM_STATUS_MODEL, " Model:    ESP32-C6 | Revision: v%d.%d\n",
           chip_info.revision / 100, chip_info.revision % 100);

    uint32_t uptime_s = (uint32_t)(esp_timer_get_time() / 1000000);
    uint32_t hours = uptime_s / 3600;
    uint32_t mins  = (uptime_s % 3600) / 60;
    uint32_t secs  = uptime_s % 60;
    LOC_PRINTF(catalog, LOC_CLI_SYSTEM_STATUS_UPTIME, " Uptime:   %02lu:%02lu:%02lu\n",
           (unsigned long)hours, (unsigned long)mins, (unsigned long)secs);

    uint32_t free_heap = esp_get_free_heap_size();
    uint32_t min_heap  = esp_get_minimum_free_heap_size();
    const char *heap_color = (free_heap < 10000) ? CLI_COLOR_RED :
                             (free_heap < 30000) ? CLI_COLOR_YELLOW : CLI_COLOR_GREEN;
    LOC_PRINTF(catalog, LOC_CLI_SYSTEM_STATUS_HEAP, " Heap:     %s%u%s / %u (Min) byte\n",
           heap_color, (unsigned int)free_heap, CLI_COLOR_RESET, (unsigned int)min_heap);

    LOC_PRINTF(catalog, LOC_CLI_SYSTEM_STATUS_COMMANDS, " Komutlar: %d kayitli\n", CommandRegistry_GetCount());
    LOC_PRINTF(catalog, LOC_CLI_SYSTEM_STATUS_TASKS, " Tasks:    %u aktif\n", (unsigned int)uxTaskGetNumberOfTasks());
    printf("--------------------\n\n");
    return 0;
}


static int cmd_dashboard(int argc, char **argv) {
    if (argc!=1 || !argv) return 1;
    IDashboard *dashboard = DashboardManager_GetModule();
    if (!dashboard || !dashboard->Render) return 1;
    dashboard->Render(dashboard);
    return 0;
}

static int dispatch_logs(int argc, char **argv) {
    const LocalizationCatalog *catalog=Localization_Snapshot();
    if (argc < 1 || !argv || argc > 4) return 1;
    if (argc>1 && strcasecmp(argv[1],"capture")==0) {
        if (argc!=4) return 1;
        uint32_t values[2];
        for (unsigned i=0; i<2; ++i) {
            const char *text=argv[i+2];
            if (!text || !*text) return 1;
            for (const char *p=text; *p; ++p) if (*p<'0' || *p>'9') return 1;
            char *end; errno=0; unsigned long value=strtoul(text,&end,10);
            if (errno || *end || value>UINT32_MAX) return 1;
            values[i]=(uint32_t)value;
        }
        ILogManager *source=(ILogManager *)ServiceLocator_GetService(SVC_NAME_LOG_MANAGER);
        esp_err_t result=FlashLogger_StartCapture(source,values[0],values[1]);
        LOC_PRINTF(catalog,LOC_CLI_SYSTEM_CAPTURE_RESULT,"Flash capture: %s (0x%x).\n",Localization_Text(catalog,result==ESP_OK ? LOC_CLI_SYSTEM_COMPLETED : LOC_CLI_SYSTEM_REJECTED),result);
        return result==ESP_OK ? 0 : 1;
    }
    if (argc==2 && strcasecmp(argv[1],"capture-stop")==0) {
        esp_err_t result=FlashLogger_StopCapture();
        LOC_PRINTF(catalog,LOC_CLI_SYSTEM_CAPTURE_STOP,"Flash capture stop: %s.\n",Localization_Text(catalog,result==ESP_OK ? LOC_CLI_SYSTEM_COMPLETED : LOC_CLI_SYSTEM_FAILED));
        return result==ESP_OK ? 0 : 1;
    }
    if (argc==2 && strcasecmp(argv[1],"capture-status")==0) {
        FlashCaptureStatus status;
        if (FlashLogger_GetCaptureStatus(&status)!=ESP_OK) return 1;
        LOC_PRINTF(catalog,LOC_CLI_SYSTEM_CAPTURE_STATUS,"Flash capture active=%u admitted=%lu rejected=%lu remaining=%lu\n",(unsigned)status.active,
               (unsigned long)status.admitted,(unsigned long)status.rejected,(unsigned long)status.remaining);
        printf("RACOOL_CLI v=1 type=flash_capture active=%u admitted=%lu rejected=%lu remaining=%lu\n",(unsigned)status.active,(unsigned long)status.admitted,(unsigned long)status.rejected,(unsigned long)status.remaining);
        return 0;
    }
    if (argc>3) return 1;
    /* LOGS clear */
    if (argc > 1 && strcasecmp(argv[1], "clear") == 0) {
        if (argc != 3 || strcmp(argv[2], "CONFIRM") != 0) {
            printf("Log deletion requires: LOGS clear CONFIRM\n");
            return 1;
        }
        if (FlashLogger_Clear() != ESP_OK) {
            LOC_PRINTF(catalog,LOC_CLI_SYSTEM_LOGS_CLEAR_FAILED,"Log clear failed or timed out.\n");return 1;
        }
        LOC_PRINTF(catalog,LOC_CLI_SYSTEM_LOGS_CLEARED,"All log files cleared.\n");
        return 0;
    }

    /* LOGS flush */
    if (argc > 1 && strcasecmp(argv[1], "flush") == 0) {
        if (argc != 2 || FlashLogger_Flush() != ESP_OK) {LOC_PRINTF(catalog,LOC_CLI_SYSTEM_LOGS_FLUSH_FAILED,"Log flush failed or timed out.\n");return 1;}
        LOC_PRINTF(catalog,LOC_CLI_SYSTEM_LOGS_FLUSHED,"Log queue flushed.\n");
        return 0;
    }

    unsigned tailCount=DEBUG_CONSOLE_LOGGER_TAIL_LINES;
    if (argc>1) {
        if (strcasecmp(argv[1],"tail")!=0) return 1;
        if (argc==3) {
            char *end=NULL;errno=0;long parsed=strtol(argv[2],&end,10);
            if (errno || !argv[2][0] || !end || *end || parsed<1 || (unsigned long)parsed>DEBUG_CONSOLE_LOGGER_TAIL_LINES) return 1;
            tailCount=(unsigned)parsed;
        }
    }
    size_t capacity=(size_t)tailCount*DEBUG_CONSOLE_LOGGER_LINE_CAPACITY+1;
    char *snapshot=malloc(capacity);if (!snapshot) return 1;
    unsigned copied=0;esp_err_t result=FlashLogger_CopyTail(snapshot,capacity,tailCount,&copied);
    if (result==ESP_OK) LOC_PRINTF(catalog,LOC_CLI_SYSTEM_LOGS_TAIL,"--- Last %u Error Records ---\n%s",copied,snapshot);
    else LOC_PRINTF(catalog,LOC_CLI_SYSTEM_LOGS_SNAPSHOT_FAILED,"Log snapshot failed: %d.\n",result);
    free(snapshot);return result==ESP_OK ? 0 : 1;
}

static int cmd_logs(int argc,char **argv) {
    if (!argv || argc<1 || argc>5) return 1;
    for (int i=0;i<argc;++i) if (!argv[i] || !argv[i][0]) return 1;
    bool machine=strcmp(argv[argc-1],"--machine")==0;
    if (machine) --argc;
    int result=dispatch_logs(argc,argv);
    if (machine) printf("RACOOL_CLI v=1 type=logs_command result=%s\n",result==0 ? "completed" : "rejected");
    return result;
}

static int cmd_live(int argc, char **argv) {
    const LocalizationCatalog *catalog = Localization_Snapshot();
    if (!argv || argc<1 || argc>2 || (argc==2 && strcasecmp(argv[1],"stop")!=0)) {
        LOC_PRINTF(catalog, LOC_CLI_SYSTEM_LIVE_USAGE, "Usage: SYSTEM LIVE [stop]\n");return 1;
    }
    IDebugConsole *console=DebugConsole_GetModule();
    return console && console->SetLiveMode && console->SetLiveMode(console,argc==1)==ESP_OK ? 0 : 1;
}

static int cmd_history(int argc, char **argv) {
    const LocalizationCatalog *catalog = Localization_Snapshot();
    if (argc!=1 || !argv) return 1;
    IDebugConsole *console=DebugConsole_GetModule();
    if (!console || !console->CopyCommandHistory) return 1;
    char (*history)[DEBUG_CONSOLE_LINE_BUFFER_SIZE]=malloc(sizeof(*history)*DEBUG_CONSOLE_HISTORY_SIZE);
    if (!history) return 1;
    int count=0;
    esp_err_t result=console->CopyCommandHistory(console,history,DEBUG_CONSOLE_HISTORY_SIZE,&count);
    if (result==ESP_OK) {
        LOC_PRINTF(catalog, LOC_CLI_SYSTEM_HISTORY_TITLE, "\n--- Komut Gecmisi (%d) ---\n",count);
        for (int i=0;i<count;i++) printf(" %2d. %s\n",i+1,history[i]);
        printf("-------------------------\n");
    }
    free(history);return result==ESP_OK ? 0 : 1;
}

static int cmd_log_monitor(int argc, char **argv) {
    if (!argv || argc<1 || argc>2 || (argc==2 && strcasecmp(argv[1],"stop")!=0 && strcasecmp(argv[1],"monitor")!=0)) return 1;
    IDebugConsole *console=DebugConsole_GetModule();
    return console && console->SetMuteLog && console->SetMuteLog(console,argc==2 && strcasecmp(argv[1],"stop")==0)==ESP_OK ? 0 : 1;
}

static int cmd_loglevel(int argc, char **argv) {
    const LocalizationCatalog *catalog = Localization_Snapshot();
    if (argc != 3 || !argv || !argv[1][0] || strnlen(argv[1],DEBUG_CONSOLE_LOG_TAG_CAPACITY)>=DEBUG_CONSOLE_LOG_TAG_CAPACITY) {
        LOC_PRINTF(catalog, LOC_CLI_SYSTEM_LOG_LEVEL_USAGE, 
               "Kullanim: LOGLEVEL <TAG|*> <LEVEL>\n" );
        printf("LEVEL: ERROR, WARN, INFO, DEBUG, VERBOSE, NONE\n");
        return 1;
    }

    const char *tag = argv[1];
    const char *level_str = argv[2];
    esp_log_level_t level = ESP_LOG_INFO;

    if (strcasecmp(level_str, "ERROR") == 0)        level = ESP_LOG_ERROR;
    else if (strcasecmp(level_str, "WARN") == 0)    level = ESP_LOG_WARN;
    else if (strcasecmp(level_str, "INFO") == 0)    level = ESP_LOG_INFO;
    else if (strcasecmp(level_str, "DEBUG") == 0)   level = ESP_LOG_DEBUG;
    else if (strcasecmp(level_str, "VERBOSE") == 0) level = ESP_LOG_VERBOSE;
    else if (strcasecmp(level_str, "NONE") == 0)    level = ESP_LOG_NONE;
    else {
        LOC_PRINTF(catalog, LOC_CLI_SYSTEM_LOG_LEVEL_INVALID,  "Gecersiz log seviyesi: %s\n" ,
               level_str);
        return 1;
    }

    esp_log_level_set(tag, level);
    LOC_PRINTF(catalog, LOC_CLI_SYSTEM_LOG_LEVEL_SUBMITTED,  "Log level request submitted: [%s] -> %s\n" ,
           tag, level_str);
    return 0;
}

static int cmd_logfilter(int argc, char **argv) {
    const LocalizationCatalog *catalog = Localization_Snapshot();
    if (argc < 2) {
        LOC_PRINTF(catalog, LOC_CLI_SYSTEM_LOG_FILTER_USAGE, 
               "Kullanim: LOGFILTER add <TAG> | LOGFILTER clear\n" );
        return 1;
    }

    if (strcasecmp(argv[1], "clear") == 0) {
        if (argc != 2 || FlashLogger_ClearFilters() != ESP_OK) return 1;
        LOC_PRINTF(catalog, LOC_CLI_SYSTEM_LOG_FILTER_CLEARED, 
               "Log filtreleri temizlendi. Tum loglar acik.\n" );
    } else if (strcasecmp(argv[1], "add") == 0) {
        if (argc >= 3) {
            if (argc != 3 || FlashLogger_AddFilter(argv[2]) != ESP_OK) return 1;
            LOC_PRINTF(catalog, LOC_CLI_SYSTEM_LOG_FILTER_ADDED,  "Log filtresi eklendi: %s\n" ,
                   argv[2]);
        } else {
            LOC_PRINTF(catalog, LOC_CLI_SYSTEM_LOG_FILTER_MISSING, 
                   "Hata: Filtrelenecek TAG belirtilmedi.\n" );
            return 1;
        }
    } else {
        LOC_PRINTF(catalog, LOC_CLI_SYSTEM_UNKNOWN_COMMAND,  "Bilinmeyen alt komut: %s\n" ,
               argv[1]);
        return 1;
    }
    return 0;
}

static int cmd_config(int argc, char **argv) {
    const LocalizationCatalog *catalog = Localization_Snapshot();
    IConfigurationManager *mgr = ConfigurationManager_GetInstance();
    if (!mgr) return 1;
    if ((argc==3 || argc==5) && argv && argv[1] && strcasecmp(argv[1],"XY")==0) {
        static const char *zones[]={"cold","warm","hot","danger","freeze"};
        unsigned zone;
        for (zone=0;zone<RGB_ZONE_COUNT;++zone) if (argv[2] && strcasecmp(argv[2],zones[zone])==0) break;
        size_t offsetX,offsetY;
        if (!SystemConfigs_RGBZoneOffsets(zone,&offsetX,&offsetY)) return 1;
        if (argc==3) {
            SystemSettings settings;
            uint16_t x,y;
            if (!mgr->GetSnapshot || mgr->GetSnapshot(mgr,&settings)!=ESP_OK ||
                !SystemConfigs_GetRGBZoneXY(&settings.hardware,zone,&x,&y)) return 1;
            LOC_PRINTF(catalog,LOC_CLI_SYSTEM_ZONE_XY,"Zone XY %s: x=%u y=%u\n",zones[zone],(unsigned)x,(unsigned)y);
            printf("RACOOL_CLI v=1 type=zone_xy zone=%s x=%u y=%u\n",zones[zone],(unsigned)x,(unsigned)y);
            return 0;
        }
        if (!mgr->UpdateFields) return 1;
        uint16_t coordinates[2];
        for (unsigned i=0;i<2;++i) {
            const char *text=argv[i+3];if (!text || !*text) return 1;
            for (const char *p=text;*p;++p) if (*p<'0' || *p>'9') return 1;
            char *end;errno=0;unsigned long value=strtoul(text,&end,10);
            if (errno || *end || value>RGB_XY_MAX_COORDINATE || (i==1 && !value)) return 1;
            coordinates[i]=(uint16_t)value;
        }
        ConfigurationFieldUpdate fields[]={{offsetX,&coordinates[0],sizeof(uint16_t)},{offsetY,&coordinates[1],sizeof(uint16_t)}};
        esp_err_t result=mgr->UpdateFields(mgr,"hardware",fields,2,true);
        LOC_PRINTF(catalog,LOC_CLI_SYSTEM_ZONE_SAVED,"Zone XY %s: %s (0x%x)\n",zones[zone],Localization_Text(catalog,result==ESP_OK ? LOC_CLI_SYSTEM_SAVED : LOC_CLI_SYSTEM_FAILED),result);
        return result==ESP_OK ? 0 : 1;
    }
    if (argc == 1) {
        SystemSettings snapshot;
        if (!mgr->GetSnapshot || mgr->GetSnapshot(mgr,&snapshot)!=ESP_OK) return 1;
        ConfigurationConsole_PrintSnapshot(&snapshot,catalog);
        LOC_PRINTF(catalog, LOC_CLI_SYSTEM_CONFIG_USAGE, "Usage: SYSTEM CONFIG SET <key> <value>\n");
        return 0;
    }
    if (argc != 4 || strcasecmp(argv[1], "SET") != 0 || !mgr->UpdateFields) {
        LOC_PRINTF(catalog, LOC_CLI_SYSTEM_CONFIG_INVALID, "Invalid configuration command\n");
        return 1;
    }
    enum { CONFIG_FLOAT, CONFIG_BYTE, CONFIG_BOOL, CONFIG_COLOR };
    static const struct { const char *key; const char *block; size_t offset; int type; } keys[] = {
        {"cold_min", "hardware", offsetof(HardwareConfigs, ext_led_temp_cold_min), CONFIG_FLOAT},
        {"cold_max", "hardware", offsetof(HardwareConfigs, ext_led_temp_cold_max), CONFIG_FLOAT},
        {"warm_min", "hardware", offsetof(HardwareConfigs, ext_led_temp_warm_min), CONFIG_FLOAT},
        {"warm_max", "hardware", offsetof(HardwareConfigs, ext_led_temp_warm_max), CONFIG_FLOAT},
        {"hot_min", "hardware", offsetof(HardwareConfigs, ext_led_temp_hot_min), CONFIG_FLOAT},
        {"hot_max", "hardware", offsetof(HardwareConfigs, ext_led_temp_hot_max), CONFIG_FLOAT},
        {"danger", "hardware", offsetof(HardwareConfigs, ext_led_temp_danger), CONFIG_FLOAT},
        {"freeze", "hardware", offsetof(HardwareConfigs, ext_led_temp_freeze), CONFIG_FLOAT},
        {"relay_open", "control", offsetof(ControlConfigs, relay_open_temp), CONFIG_FLOAT},
        {"relay_close", "control", offsetof(ControlConfigs, relay_close_temp), CONFIG_FLOAT},
        {"fan_high", "control", offsetof(ControlConfigs, threshold_high), CONFIG_FLOAT},
        {"fan_low", "control", offsetof(ControlConfigs, threshold_low), CONFIG_FLOAT},
        {"brightness", "hardware", offsetof(HardwareConfigs, ext_led_brightness), CONFIG_BYTE},
        {"system_enabled", "system", offsetof(SystemBaseConfigs, system_enabled), CONFIG_BOOL},
        {"cold_color", "hardware", offsetof(HardwareConfigs, ext_led_color_cold_x), CONFIG_COLOR},
        {"warm_color", "hardware", offsetof(HardwareConfigs, ext_led_color_warm_x), CONFIG_COLOR},
        {"hot_color", "hardware", offsetof(HardwareConfigs, ext_led_color_hot_x), CONFIG_COLOR},
        {"danger_color", "hardware", offsetof(HardwareConfigs, ext_led_color_danger_x), CONFIG_COLOR},
        {"freeze_color", "hardware", offsetof(HardwareConfigs, ext_led_color_freeze_x), CONFIG_COLOR},
    };
    size_t index;
    for (index = 0; index < sizeof(keys) / sizeof(keys[0]); ++index) {
        if (strcasecmp(argv[2], keys[index].key) == 0) break;
    }
    if (index == sizeof(keys) / sizeof(keys[0])) {
        LOC_PRINTF(catalog,LOC_CLI_SYSTEM_CONFIG_UNKNOWN,"Unknown configuration key: %s\n", argv[2]);
        return 1;
    }
    char *end = NULL;
    errno = 0;
    float number = 0;
    uint8_t byte = 0;
    bool boolean = false;
    uint16_t x = 0, y = 0;
    ConfigurationFieldUpdate fields[2] = {{.offset = keys[index].offset}};
    size_t count = 1;
    if (keys[index].type == CONFIG_FLOAT) {
        number = strtof(argv[3], &end);
        if (errno || end == argv[3] || *end || !isfinite(number)) return 1;
        fields[0].data = &number;
        fields[0].size = sizeof(number);
    } else {
        /* Reject signs, whitespace and truncation before converting bounded integers. */
        if (!argv[3][0]) return 1;
        for (const char *c = argv[3]; *c; ++c) if (*c < '0' || *c > '9') return 1;
        unsigned long value = strtoul(argv[3], &end, 10);
        unsigned long limit = keys[index].type == CONFIG_BOOL ? 1 :
                              keys[index].type == CONFIG_COLOR ? RGB_COLOR_COUNT - 1 : UINT8_MAX;
        if (errno || *end || value > limit) return 1;
        if (keys[index].type == CONFIG_COLOR) {
            RGBLEDColorEngine_PresetToXY((PresetColorIndex)value, &x, &y);
            fields[0].data = &x;
            fields[0].size = sizeof(x);
            fields[1] = (ConfigurationFieldUpdate){keys[index].offset + sizeof(x), &y, sizeof(y)};
            count = 2;
        } else if (keys[index].type == CONFIG_BOOL) {
            boolean = value != 0;
            fields[0].data = &boolean;
            fields[0].size = sizeof(boolean);
        } else {
            byte = (uint8_t)value;
            fields[0].data = &byte;
            fields[0].size = sizeof(byte);
        }
    }
    esp_err_t result = mgr->UpdateFields(mgr, keys[index].block, fields, count, true);
    if (result != ESP_OK) {
        LOC_PRINTF(catalog, LOC_CLI_SYSTEM_CONFIG_FAILED, "Configuration update failed (0x%x)\n", (unsigned)result);
        return 1;
    }
    LOC_PRINTF(catalog, LOC_CLI_SYSTEM_CONFIG_SAVED,  "%s saved to flash.\n" , keys[index].key);
    return 0;
}

static int cmd_version(int argc, char **argv) {
    const LocalizationCatalog *catalog = Localization_Snapshot();
    LOC_PRINTF(catalog, LOC_CLI_SYSTEM_VERSION_TITLE,  "\n--- RaCool Firmware Bilgisi ---\n" );
    LOC_PRINTF(catalog,LOC_CLI_SYSTEM_PLATFORM," Platform: ESP32-C6\n");
    LOC_PRINTF(catalog, LOC_CLI_SYSTEM_VERSION_FRAMEWORK, " Framework:  ESP-IDF v%s\n", esp_get_idf_version());
    LOC_PRINTF(catalog, LOC_CLI_SYSTEM_VERSION_CONSOLE, " Konsol:     DebugConsole v2.0\n");
    LOC_PRINTF(catalog,LOC_CLI_SYSTEM_COMMANDS," Commands: %d registered\n", CommandRegistry_GetCount());
    printf("-------------------------------\n");
    return 0;
}

/* ─────────────────────────────────────────────
 * Registration
 * ───────────────────────────────────────────── */

static int dispatch_system(int argc, char **argv) {
    const LocalizationCatalog *catalog = Localization_Snapshot();
    if (argc < 2) {
        LOC_PRINTF(catalog, LOC_CLI_SYSTEM_USAGE, "Kullanim: SYSTEM <subcommand>\n");
        LOC_PRINTF(catalog, LOC_CLI_SYSTEM_SUBCOMMANDS, "Alt komutlar: STATUS, RESET, FACTORY_RESET, MEM, TASKS, SPI, UPTIME, VERSION, CONFIG, HELP_CAT, HISTORY, LOGSTATS, LIVE\n");
        return 1;
    }

    if (strcasecmp(argv[1], "STATUS") == 0) return cmd_status(argc - 1, &argv[1]);
    if (strcasecmp(argv[1], "RESET") == 0) return cmd_reset(argc - 1, &argv[1]);
    if (strcasecmp(argv[1], "FACTORY_RESET") == 0) return cmd_factory_reset(argc - 1, &argv[1]);
    if (strcasecmp(argv[1], "MEM") == 0) return cmd_meminfo(argc - 1, &argv[1]);
    if (strcasecmp(argv[1], "TASKS") == 0) return cmd_tasks(argc - 1, &argv[1]);
    if (strcasecmp(argv[1], "SPI") == 0) return cmd_spiinfo(argc - 1, &argv[1]);
    if (strcasecmp(argv[1], "UPTIME") == 0) return cmd_uptime(argc - 1, &argv[1]);
    if (strcasecmp(argv[1], "VERSION") == 0) return cmd_version(argc - 1, &argv[1]);
    if (strcasecmp(argv[1], "CONFIG") == 0) return cmd_config(argc - 1, &argv[1]);
    if (strcasecmp(argv[1], "HELP_CAT") == 0) return cmd_help_cat(argc - 1, &argv[1]);
    if (strcasecmp(argv[1], "HISTORY") == 0) return cmd_history(argc - 1, &argv[1]);
    if (strcasecmp(argv[1], "LOGSTATS") == 0) return cmd_logstats(argc - 1, &argv[1]);
    if (strcasecmp(argv[1], "LIVE") == 0) return cmd_live(argc - 1, &argv[1]);

    LOC_PRINTF(catalog, LOC_CLI_SYSTEM_UNKNOWN_COMMAND,  "Bilinmeyen alt komut: %s\n" , argv[1]);
    return 1;
}

static int cmd_system(int argc,char **argv) {
    if (!argv || argc<1 || argc>7) return 1;
    for (int i=0;i<argc;++i) if (!argv[i] || !argv[i][0]) return 1;
    bool machine=strcmp(argv[argc-1],"--machine")==0;
    if (machine) --argc;
    int result=dispatch_system(argc,argv);
    if (machine) printf("RACOOL_CLI v=1 type=system_command result=%s\n",result==0 ? "completed" : "rejected");
    return result;
}

static int cmd_debug(int argc, char **argv) {
    const LocalizationCatalog *catalog = Localization_Snapshot();
    if (argc < 2) {
        LOC_PRINTF(catalog, LOC_CLI_SYSTEM_DEBUG_USAGE, "Kullanim: DBG <subcommand>\n");
        LOC_PRINTF(catalog, LOC_CLI_SYSTEM_DEBUG_SUBCOMMANDS, "Alt komutlar: LEVEL, FILTER, LOGSTATS, MONITOR, STOP\n");
        return 1;
    }

    if (strcasecmp(argv[1], "LEVEL") == 0) return cmd_loglevel(argc - 1, &argv[1]);
    if (strcasecmp(argv[1], "FILTER") == 0) return cmd_logfilter(argc - 1, &argv[1]);
    if (strcasecmp(argv[1], "LOGSTATS") == 0) return cmd_logstats(argc - 1, &argv[1]);
    if (strcasecmp(argv[1], "MONITOR") == 0) {
        if (argc!=2) {LOC_PRINTF(catalog, LOC_CLI_SYSTEM_DEBUG_MONITOR_USAGE, "Usage: DEBUG MONITOR\n");return 1;}
        char *monitor_argv[] = {"LOG", "monitor"};
        return cmd_log_monitor(2, monitor_argv);
    }
    if (strcasecmp(argv[1], "STOP") == 0) {
        if (argc!=2) {LOC_PRINTF(catalog, LOC_CLI_SYSTEM_DEBUG_STOP_USAGE, "Usage: DEBUG STOP\n");return 1;}
        char *stop_argv[] = {"LOG", "stop"};
        return cmd_log_monitor(2, stop_argv);
    }

    LOC_PRINTF(catalog, LOC_CLI_SYSTEM_UNKNOWN_COMMAND,  "Bilinmeyen alt komut: %s\n" , argv[1]);
    return 1;
}

void SystemCommands_Register(IDebugConsole *console) {
    if (!console) return;

    if (console->SetInterceptor != NULL) {
        console->SetInterceptor(console, SystemCommands_InterceptInput);
    }

    static const esp_console_cmd_t core_cmds[] = {
        {.command = "DEVELOPER", .help = "Developer Mode: ON, OFF or STATUS", .func = &cmd_developer},
        {.command = "LANGUAGE", .help = "LANGUAGE [LIST [--machine] | SET <locale> [--machine] | CONFIG --machine | --machine]", .func = &cmd_language},
        {.command = "HELP",     .help = "Yardim menusunu goster",             .func = &cmd_help},
        {.command = "help",     .help = "Yardim menusunu goster",             .func = &cmd_help},
        {.command = "SYSTEM",   .help = "Sistem yonetim komutlari",           .func = &cmd_system, .hint = "<subcmd>"},
        {.command = "SYS",      .help = "Sistem yonetim komutlari (kisa)",    .func = &cmd_system, .hint = "<subcmd>"},
        {.command = "DEBUG",    .help = "Hata ayiklama ve log yonetimi",      .func = &cmd_debug,  .hint = "<subcmd>"},
        {.command = "DBG",      .help = "Hata ayiklama (kisa)",                .func = &cmd_debug,  .hint = "<subcmd>"},
        {.command = "DASH",     .help = "Dinamik Dashboard",                  .func = &cmd_dashboard},
        {.command = "LOGS",     .help = "Flash logs: tail, clear CONFIRM, flush, capture",  .func = &cmd_logs},
        {.command = "CLS",      .help = "Ekrani temizle",                     .func = &cmd_cls},
        {.command = "FIND",     .help = "Komut ara",                          .func = &cmd_find}
    };

    for (int i = 0; i < (int)(sizeof(core_cmds) / sizeof(core_cmds[0])); i++) {
        console->RegisterCommandWithCategory(console, &core_cmds[i], CMD_CAT_SYSTEM);
    }
}

bool SystemCommands_InterceptInput(const char *line) {
    if (!line) return false;
    const LocalizationCatalog *catalog = Localization_Snapshot();
    if (s_factoryResetPending) {
        s_factoryResetPending = false;
        if (strcasecmp(line, "y") == 0 || strcasecmp(line, "yes") == 0) {
            IApplicationManager *app = (IApplicationManager *)ServiceLocator_GetService(SVC_NAME_APPLICATION_MANAGER);
            if (!app || !app->TriggerFactoryReset) {
                LOC_PRINTF(catalog, LOC_CLI_SYSTEM_RESET_UNAVAILABLE,  "Sifirlama reddedildi: servisler hazir degil.\n" );
                return true;
            }
            esp_err_t result = app->TriggerFactoryReset(app);
            if (result != ESP_OK) {
                LOC_PRINTF(catalog, LOC_CLI_SYSTEM_FACTORY_RESET_REJECTED,  "Fabrika reset istegi reddedildi: %d.\n" , result);
                return true;
            }
            LOC_PRINTF(catalog, LOC_CLI_SYSTEM_FACTORY_RESET_QUEUED,  "Fabrika reset istegi siraya alindi; dogrulama basariliysa otomatik restart yapilacak.\n" );
            return true;
        }
        LOC_PRINTF(catalog, LOC_CLI_SYSTEM_FACTORY_RESET_CANCELLED,  "Fabrika ayarlarina sifirlama iptal edildi.\n" );
        return true;
    }

    if (!s_resetPending) return false;

    s_resetPending = false;
    if (strcasecmp(line, "y") == 0 || strcasecmp(line, "yes") == 0) {
        IPowerManager *power = (IPowerManager *)ServiceLocator_GetService(SVC_NAME_POWER_MANAGER);
        int32_t result = power && power->RestartAsync
            ? power->RestartAsync(power, DEBUG_CONSOLE_RESTART_DELAY_MS) : POWER_ERR_INVALID_STATE;
        if (result != POWER_OK) {
            LOC_PRINTF(catalog, LOC_CLI_SYSTEM_RESTART_REJECTED, "Restart request rejected: %ld.\n", (long)result);
            return true;
        }
        LOC_PRINTF(catalog, LOC_CLI_SYSTEM_RESTART_SCHEDULED, "Restart request scheduled in %u ms.\n",(unsigned)DEBUG_CONSOLE_RESTART_DELAY_MS);
        return true;
    }

    LOC_PRINTF(catalog, LOC_CLI_SYSTEM_RESTART_CANCELLED,  "Reset islemi iptal edildi.\n" );
    return true;
}
