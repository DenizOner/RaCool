#include "Communication/DebugConsole/Core/ConsolePrivacy.h"
#include <esp_timer.h>
#include <esp_console.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>
#include "driver/uart.h"
#include "driver/uart_vfs.h"
#include "esp_task_wdt.h"

#include "Core/DI/IDIRegistry.h"
#include "Core/ServiceLocator/ServiceLocator.h"
#include "Communication/DebugConsole/IDebugConsole.h"
#include "Communication/DebugConsole/DebugConsoleConfig.h"
#include "Communication/DebugConsole/Core/ILogger.h"
#include "Communication/DebugConsole/Core/IDashboard.h"
#include "Communication/DebugConsole/Core/DashboardManager.h"
#include "Communication/DebugConsole/Core/FlashLogger.h"
#include "Communication/DebugConsole/Core/IConsoleIO.h"
#include "Communication/DebugConsole/Core/ConsoleColors.h"
#include "Communication/DebugConsole/Core/CommandRegistry.h"
#include "Communication/DebugConsole/Core/DeveloperMode.h"
#include "Communication/DebugConsole/Core/InputParser.h"
#include "Communication/DebugConsole/Core/HistoryManager.h"
#include "Communication/DebugConsole/Core/ConsoleEventPublisher.h"
#include "Communication/DebugConsole/Drivers/UartConsoleIO.h"
#include "Communication/DebugConsole/Commands/SystemCommands.h"
#include "Communication/DebugConsole/Commands/DiagnosticCommands.h"
#include "Communication/DebugConsole/Commands/HardwareCommands.h"

#if DEBUG_CONSOLE_ENABLE_PERFMON
#include "Communication/DebugConsole/Commands/PerformanceCommands.h"
#endif

#if DEBUG_CONSOLE_ENABLE_NETWORK_CMDS
#include "Communication/DebugConsole/Commands/NetworkCommands.h"
#endif

static const char *TAG = "DebugConsole";

/* ─────────────────────────────────────────────
 * Plugin structure
 * ───────────────────────────────────────────── */
typedef struct {
    char name[32];
    void (*register_func)(IDebugConsole *);
} ConsolePlugin;

/* ─────────────────────────────────────────────
 * Internal implementation
 * ───────────────────────────────────────────── */
typedef struct {
    IDebugConsole  base;
    _Atomic(ServiceState) state;
    TaskHandle_t   consoleTaskHandle;
    IConsoleIO    *io;

    /* Plugin registry */
    ConsolePlugin  plugins[DEBUG_CONSOLE_MAX_PLUGINS];
    int            pluginCount;

    /* Input buffers */
    char           lineBuffer[DEBUG_CONSOLE_LINE_BUFFER_SIZE];
    int            lineIndex;

    /* State flags — protected by the mutex */
    volatile bool  isLiveMode;
    volatile bool  logsMuted;
    bool           (*inputInterceptor)(const char *line);

    /* Synchronization */
    SemaphoreHandle_t mutex;
    SemaphoreHandle_t lifecycleMutex;
    QueueHandle_t stopped;
    atomic_bool closing;
    atomic_bool starting;
    _Atomic(TaskHandle_t) startOwner;
    atomic_bool registeringModule;
    _Atomic(TaskHandle_t) registrationOwner;
    atomic_int registrationError;
    atomic_uint activeCommands;
    bool stopPending;
    bool sdkOwned;
    bool ioOwned;
} DebugConsoleImpl;

/* Externally visible for test_DebugConsole.c */
DebugConsoleImpl consoleService = {0};

/* ─────────────────────────────────────────────
 * Internal helper: thread-safe state access
 * ───────────────────────────────────────────── */
static inline bool dc_take_mutex(TickType_t timeout) {
    return consoleService.mutex &&
           (xSemaphoreTake(consoleService.mutex, timeout) == pdTRUE);
}

static inline void dc_give_mutex(void) {
    if (consoleService.mutex) {
        xSemaphoreGive(consoleService.mutex);
    }
}

static bool dc_admit_operation(void) {
    if (atomic_load(&consoleService.closing) ||
        (atomic_load(&consoleService.starting) &&
         atomic_load(&consoleService.startOwner) != xTaskGetCurrentTaskHandle())) return false;
    atomic_fetch_add(&consoleService.activeCommands, 1);
    if (!atomic_load(&consoleService.closing) &&
        (!atomic_load(&consoleService.starting) ||
         atomic_load(&consoleService.startOwner) == xTaskGetCurrentTaskHandle())) return true;
    atomic_fetch_sub(&consoleService.activeCommands, 1);
    return false;
}

static void dc_finish_operation(void) {
    atomic_fetch_sub(&consoleService.activeCommands, 1);
}

static esp_err_t dc_record_registration_result(esp_err_t result) {
    TaskHandle_t caller = xTaskGetCurrentTaskHandle();
    bool owns_start = atomic_load(&consoleService.starting) &&
        atomic_load(&consoleService.startOwner) == caller;
    bool owns_registration = atomic_load(&consoleService.registeringModule) &&
        atomic_load(&consoleService.registrationOwner) == caller;
    if (result != ESP_OK && (owns_start || owns_registration)) {
        int expected = ESP_OK;
        atomic_compare_exchange_strong(&consoleService.registrationError, &expected, result);
    }
    return result;
}

/* Called only under lifecycleMutex, after all admitted consumers have returned. */
static int32_t dc_cleanup_transport(void) {
    if (consoleService.sdkOwned) {
        esp_err_t result = esp_console_deinit();
        if (result != ESP_OK) return result;
        consoleService.sdkOwned = false;
    }
    if (consoleService.ioOwned) {
        int32_t result = consoleService.io && consoleService.io->Deinitialize ?
            consoleService.io->Deinitialize(consoleService.io) : 0;
        if (result != 0) return result;
        consoleService.ioOwned = false;
    }
    return ESP_OK;
}

/* ─────────────────────────────────────────────
 * IDebugConsole implementation
 * ───────────────────────────────────────────── */

static esp_err_t DebugConsole_RegisterCommand(IDebugConsole *self,
                                              const esp_console_cmd_t *cmd) {
    if (self != &consoleService.base || !cmd || !cmd->command)
        return dc_record_registration_result(ESP_ERR_INVALID_ARG);

    if (!dc_admit_operation()) return dc_record_registration_result(ESP_ERR_INVALID_STATE);
    esp_err_t result = CommandRegistry_RegisterWithBackend(cmd, CMD_CAT_UNCATEGORIZED, esp_console_cmd_register);
    dc_record_registration_result(result);
    dc_finish_operation();
    return result;
}

static esp_err_t DebugConsole_RegisterCommandWithCategory(
    IDebugConsole *self, const esp_console_cmd_t *cmd, CommandCategory category) {
    if (self != &consoleService.base || !cmd || !cmd->command)
        return dc_record_registration_result(ESP_ERR_INVALID_ARG);

    if (!dc_admit_operation()) return dc_record_registration_result(ESP_ERR_INVALID_STATE);
    esp_err_t result = CommandRegistry_RegisterWithBackend(cmd, category, esp_console_cmd_register);
    dc_record_registration_result(result);
    dc_finish_operation();
    return result;
}

static esp_err_t DebugConsole_RegisterModuleOwned(IDebugConsole *self,
                                             const char *moduleName,
                                             void (*register_func)(IDebugConsole *)) {
    if (self != &consoleService.base || !moduleName || !moduleName[0] || !register_func ||
        strlen(moduleName) >= sizeof(consoleService.plugins[0].name)) return ESP_ERR_INVALID_ARG;
    if (!dc_admit_operation()) return ESP_ERR_INVALID_STATE;

    DebugConsoleImpl *impl = (DebugConsoleImpl *)self;
    if (!dc_take_mutex(pdMS_TO_TICKS(200))) {
        dc_finish_operation();
        return ESP_ERR_TIMEOUT;
    }
    if (atomic_load(&impl->registeringModule) &&
        atomic_load(&impl->registrationOwner) != xTaskGetCurrentTaskHandle()) {
        dc_give_mutex();
        dc_finish_operation();
        return ESP_ERR_INVALID_STATE;
    }

    int slot = -1;
    for (int i = 0; i < impl->pluginCount; ++i) {
        if (strcmp(impl->plugins[i].name, moduleName) == 0) { slot = i; break; }
    }
    if (slot >= 0 && (atomic_load(&impl->starting) || atomic_load(&impl->registeringModule))) {
        /* The startup snapshot already owns this callback. Reentrant identical
         * registration is idempotent; replacement waits for startup completion. */
        esp_err_t result = impl->plugins[slot].register_func == register_func ? ESP_OK : ESP_ERR_INVALID_STATE;
        dc_give_mutex();
        dc_finish_operation();
        return result;
    }
    if (slot < 0 && impl->pluginCount >= DEBUG_CONSOLE_MAX_PLUGINS) {
        dc_give_mutex();
        ESP_LOGE(TAG, "Maximum plugin capacity reached! (%d)", DEBUG_CONSOLE_MAX_PLUGINS);
        dc_finish_operation();
        return ESP_ERR_NO_MEM;
    }

    if (slot < 0) slot = impl->pluginCount++;
    strcpy(impl->plugins[slot].name, moduleName);
    impl->plugins[slot].register_func = register_func;

    /* If already started, register immediately */
    if (impl->state == SERVICE_STATE_STARTED) {
        dc_give_mutex();
        TaskHandle_t caller = xTaskGetCurrentTaskHandle();
        bool nested = (atomic_load(&impl->starting) && atomic_load(&impl->startOwner) == caller) ||
            (atomic_load(&impl->registeringModule) && atomic_load(&impl->registrationOwner) == caller);
        if (nested) {
            register_func(self);
            esp_err_t result = atomic_load(&impl->registrationError);
            dc_finish_operation();
            return result;
        }
        if (!impl->lifecycleMutex ||
            xSemaphoreTake(impl->lifecycleMutex, pdMS_TO_TICKS(DEBUG_CONSOLE_CONTROL_TIMEOUT_MS)) != pdTRUE) {
            dc_finish_operation();
            return ESP_ERR_TIMEOUT;
        }
        if (impl->state != SERVICE_STATE_STARTED || atomic_load(&impl->closing)) {
            xSemaphoreGive(impl->lifecycleMutex);
            dc_finish_operation();
            return ESP_ERR_INVALID_STATE;
        }
        atomic_store(&impl->registrationError, ESP_OK);
        atomic_store(&impl->registrationOwner, caller);
        atomic_store(&impl->registeringModule, true);
        register_func(self);
        esp_err_t result = atomic_load(&impl->registrationError);
        atomic_store(&impl->registeringModule, false);
        atomic_store(&impl->registrationOwner, NULL);
        xSemaphoreGive(impl->lifecycleMutex);
        dc_finish_operation();
        return result;
    }

    dc_give_mutex();
    dc_finish_operation();
    return ESP_OK;
}

static esp_err_t DebugConsole_RegisterModule(IDebugConsole *self, const char *moduleName,
                                            void (*register_func)(IDebugConsole *)) {
    return dc_record_registration_result(DebugConsole_RegisterModuleOwned(self, moduleName, register_func));
}

static esp_err_t DebugConsole_SetLiveMode(IDebugConsole *self, bool enabled) {
    if (!self || !dc_take_mutex(pdMS_TO_TICKS(100))) return ESP_ERR_INVALID_STATE;
    consoleService.isLiveMode = enabled;
    dc_give_mutex();
    if (enabled) {
        printf("\n" CLI_COLOR_CYAN "Live Dashboard enabled. Type 'LIVE stop' to exit.\n" CLI_COLOR_RESET);
    } else {
        printf("\n" CLI_COLOR_YELLOW "Live Dashboard stopped.\n" CLI_COLOR_RESET);
    }
    return ESP_OK;
}

static void DebugConsole_SetInterceptor(IDebugConsole *self,
                                        bool (*interceptor)(const char *)) {
    if (dc_take_mutex(pdMS_TO_TICKS(100))) {
        consoleService.inputInterceptor = interceptor;
        dc_give_mutex();
    }
}

static const char *const *DebugConsole_GetCommandHistory(IDebugConsole *self,
                                                         int *count) {
    return HistoryManager_GetAll(count);
}

static esp_err_t DebugConsole_CopyCommandHistory(IDebugConsole *self,
        char (*output)[DEBUG_CONSOLE_LINE_BUFFER_SIZE],size_t capacity,int *count) {
    return self && HistoryManager_CopyAll(output,capacity,count) ? ESP_OK : ESP_FAIL;
}

static int DebugConsole_GetRegisteredCommandCount(IDebugConsole *self) {
    return CommandRegistry_GetCount();
}

static void DebugConsole_ExecuteBatch(IDebugConsole *self,
                                      const char *commands[], int count) {
    if (!commands) return;
    for (int i = 0; i < count; i++) {
        if (commands[i]) {
            self->ProcessCommand(self, commands[i]);
        }
    }
}

static esp_err_t DebugConsole_SetMuteLog(IDebugConsole *self, bool muted) {
    if (!self || !dc_take_mutex(pdMS_TO_TICKS(100))) return ESP_ERR_INVALID_STATE;
    esp_log_level_t requested=muted ? ESP_LOG_WARN : ESP_LOG_INFO;
    esp_log_level_set("*",requested);
    if (esp_log_level_get("*")!=requested) {dc_give_mutex();return ESP_FAIL;}
    consoleService.logsMuted = muted;
    dc_give_mutex();
    if (muted) {
        printf("\n" CLI_COLOR_YELLOW "Logs muted. Only Error/Warning level shown.\n" CLI_COLOR_RESET);
    } else {
        printf("\n" CLI_COLOR_GREEN "Log output resumed.\n" CLI_COLOR_RESET);
    }
    return ESP_OK;
}

/* ─────────────────────────────────────────────
 * Command Execution
 * ───────────────────────────────────────────── */

static esp_err_t DebugConsole_RunCommand(IDebugConsole *self, const char *cmd, bool internal) {
    if (!cmd || cmd[0] == '\0') return ESP_ERR_INVALID_ARG;

    const bool privateCommand = ConsolePrivacy_IsPrivate(cmd);
    const bool developerEnabled = DeveloperMode_IsEnabled();
    const char *usage = !internal && !developerEnabled ? DeveloperMode_UserUsage(cmd) : NULL;
    if (usage) { printf("%s\n", usage); return ESP_OK; }
    if (!internal && !developerEnabled && !DeveloperMode_IsUserLine(cmd)) {
        printf("Developer Tools are unavailable. Use DEVELOPER ON.\n");
        return ESP_ERR_INVALID_STATE;
    }

    const char *auditCommand = privateCommand ? "[private]" : cmd;
    if (!privateCommand) HistoryManager_Add(cmd);
    ESP_LOGI(TAG, "Executing: %s", auditCommand);
    ConsoleEventPublisher_CommandExecuted(auditCommand);

    int ret = 0;
    esp_err_t err = esp_console_run(cmd, &ret);
    if (err == ESP_ERR_NOT_FOUND) {
        printf("\nUnknown command: '%s'. Type 'help' for available commands.\n", auditCommand);
        ConsoleEventPublisher_CommandFailed(auditCommand, (int)err);
        return ESP_ERR_NOT_FOUND;
    }
    if (err != ESP_OK) {
        ConsoleEventPublisher_CommandFailed(auditCommand, (int)err);
        return err;
    }
    if (ret != 0) {
        ConsoleEventPublisher_CommandFailed(auditCommand, ret);
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t DebugConsole_ProcessCommand(IDebugConsole *self, const char *cmd) {
    if (!self || atomic_load(&consoleService.starting) || !dc_admit_operation()) return ESP_ERR_INVALID_STATE;
    if (atomic_load(&consoleService.starting)) {
        dc_finish_operation();
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t result = DebugConsole_RunCommand(self, cmd, false);
    dc_finish_operation();
    return result;
}

/* ─────────────────────────────────────────────
 * Asynchronous input processing
 * ───────────────────────────────────────────── */

#if DEBUG_CONSOLE_ENABLE_HISTORY_NAV
static bool dc_copy_visible_history(bool previous, char *output, size_t capacity) {
    for (unsigned i = 0; i < DEBUG_CONSOLE_HISTORY_SIZE; ++i) {
        if (!HistoryManager_CopyNavigation(previous, output, capacity)) return false;
        if (!output[0] || DeveloperMode_IsEnabled() || DeveloperMode_IsUserLine(output) ||
            DeveloperMode_UserUsage(output)) return true;
    }
    return false;
}
#endif

static void handle_char(char c) {
    char shared_line[DEBUG_CONSOLE_LINE_BUFFER_SIZE] = {0};
    InputParserEvent evt = InputParser_ProcessChar(c, shared_line,
                                                   DEBUG_CONSOLE_LINE_BUFFER_SIZE);
    switch (evt) {
        case INPUT_EVENT_LINE_READY: {
            /* Check the input interceptor. */
            bool intercepted = false;
            if (dc_take_mutex(pdMS_TO_TICKS(50))) {
                if (consoleService.inputInterceptor != NULL) {
                    bool (*fn)(const char *) = consoleService.inputInterceptor;
                    dc_give_mutex();
                    intercepted = fn(shared_line);
                } else {
                    dc_give_mutex();
                }
            }
            if (intercepted) {
                ConsolePrivacy_Clear(shared_line, sizeof(shared_line));
                printf("\n" CLI_PROMPT);
                fflush(stdout);
                return;
            }
            DebugConsole_ProcessCommand(&consoleService.base, shared_line);
            printf("\n" CLI_PROMPT);
            fflush(stdout);
            break;
        }

        case INPUT_EVENT_TAB:
#if DEBUG_CONSOLE_ENABLE_TAB_COMPLETION
        {
            CommandEntry matches[5];
            unsigned cursor = 0;
            int match_count = 0;
            CommandEntry candidate;
            int fetched;
            while (match_count < 5 &&
                   (fetched = CommandRegistry_CopyMatches(shared_line, CMD_CAT_MAX, &cursor, &candidate, 1)) > 0) {
                if (DeveloperMode_IsEnabled() || DeveloperMode_IsVisibleUserRoot(candidate.command)) matches[match_count++] = candidate;
            }
            if (match_count == 1) {
                InputParser_SetLine(matches[0].command, shared_line,
                                    DEBUG_CONSOLE_LINE_BUFFER_SIZE);
            } else if (match_count > 1) {
                printf("\n");
                for (int i = 0; i < match_count; i++) {
                    printf("%-15s ", matches[i].command);
                }
                printf("\n" CLI_PROMPT);
                InputParser_SetLine(shared_line, shared_line,
                                    DEBUG_CONSOLE_LINE_BUFFER_SIZE);
            }
        }
#endif
            break;

        case INPUT_EVENT_HISTORY_PREV:
#if DEBUG_CONSOLE_ENABLE_HISTORY_NAV
        {
            char previous[DEBUG_CONSOLE_LINE_BUFFER_SIZE];
            if (dc_copy_visible_history(true,previous,sizeof(previous)) && previous[0]) {
                InputParser_SetLine(previous, shared_line,
                                    DEBUG_CONSOLE_LINE_BUFFER_SIZE);
            }
        }
#endif
            break;

        case INPUT_EVENT_HISTORY_NEXT:
#if DEBUG_CONSOLE_ENABLE_HISTORY_NAV
        {
            char next[DEBUG_CONSOLE_LINE_BUFFER_SIZE];
            if (dc_copy_visible_history(false,next,sizeof(next)))
                InputParser_SetLine(next, shared_line,DEBUG_CONSOLE_LINE_BUFFER_SIZE);
        }
#endif
            break;

        case INPUT_EVENT_CANCEL:
            HistoryManager_ResetNavigation();
            printf("^C\n" CLI_PROMPT);
            fflush(stdout);
            break;

        default:
            break;
    }
    ConsolePrivacy_Clear(shared_line, sizeof(shared_line));
}

/* ─────────────────────────────────────────────
 * Console main task
 * ───────────────────────────────────────────── */

static void console_task(void *pvParameters) {
    DebugConsoleImpl *impl = (DebugConsoleImpl *)pvParameters;
    if (!impl) {
        esp_rom_printf("[DebugConsole] Critical Error: Task parameter is NULL!\n");
        vTaskDelete(NULL);
        return;
    }

    /* Watchdog registration */
    esp_err_t wdt_err = esp_task_wdt_add(NULL);
    if (wdt_err != ESP_OK) {
        esp_rom_printf("[DebugConsole] WDT Add failed: 0x%x\n", wdt_err);
    }

    ESP_LOGI(TAG, "Console task started. IO: %p", impl->io);
    esp_rom_printf("\n" CLI_COLOR_CYAN
        "RaCool Debug Console Started. Type 'help' to see available commands.\n"
        CLI_COLOR_RESET);
    printf(CLI_PROMPT);
    fflush(stdout);

    uint32_t last_heartbeat = xTaskGetTickCount();
    uint32_t last_dashboard_update = 0;

#if DEBUG_CONSOLE_ENABLE_SELF_TEST
    bool self_test_done = false;
#endif

    for (;;) {
        if (atomic_load(&impl->starting)) { vTaskDelay(1); continue; }
        if (atomic_load(&impl->closing)) {
            esp_err_t result = wdt_err == ESP_OK ? esp_task_wdt_delete(NULL) : ESP_OK;
            if (result != ESP_OK) atomic_store(&impl->closing, false);
            if (xQueueSend(impl->stopped, &result, 0) == pdTRUE && result == ESP_OK) {
                /* No IO, registry or callback remains borrowed at this boundary. */
                vTaskSuspend(NULL);
                return;
            }
            if (result == ESP_OK) wdt_err = ESP_FAIL;
        }
        if (wdt_err == ESP_OK) esp_task_wdt_reset();
        uint32_t now = xTaskGetTickCount();

#if DEBUG_CONSOLE_ENABLE_SELF_TEST
        /* Startup verification (one-shot) */
        if (!self_test_done) {
            vTaskDelay(pdMS_TO_TICKS(2000));
            esp_rom_printf("[DebugConsole] SELF-TEST: Verification Suite Starting...\n");
            static const char *const checks[] = {"SYS TASKS", "SYS STATUS", "SYS MEM", "HW PINS", "I2C SCAN"};
            for (size_t i = 0; i < sizeof(checks) / sizeof(checks[0]); ++i) {
                if (!dc_admit_operation()) break;
                (void)DebugConsole_RunCommand(&consoleService.base, checks[i], true);
                dc_finish_operation();
            }
            self_test_done = true;
        }
#endif

        /* Heartbeat */
        if ((now - last_heartbeat) > pdMS_TO_TICKS(DEBUG_CONSOLE_HEARTBEAT_INTERVAL_MS)) {
            ESP_LOGD(TAG, "Heartbeat - task running, state: %d", (int)impl->state);

            if (!impl->logsMuted && !impl->isLiveMode && impl->lineIndex == 0) {
                static const char spinner[] = "|/-\\";
                static int s_idx = 0;
                esp_rom_printf("\033[s\r%s %c\033[u", CLI_PROMPT, spinner[s_idx]);
                s_idx = (s_idx + 1) % 4;
            }
            last_heartbeat = now;
        }

        /* Periodic dashboard update */
#if DEBUG_CONSOLE_ENABLE_DASHBOARD
        if (impl->isLiveMode &&
            (now - last_dashboard_update) > pdMS_TO_TICKS(DEBUG_CONSOLE_DASHBOARD_REFRESH_MS)) {
            IDashboard *dash = DashboardManager_GetModule();
            if (dash) dash->Render(dash);
            printf(CLI_PROMPT "%s", impl->lineBuffer);
            fflush(stdout);
            last_dashboard_update = now;
        }
#endif

        /* Input reading */
        if (impl->io && impl->io->IsDataAvailable(impl->io, 10)) {
            char c;
            if (impl->io->Read(impl->io, &c, 1) > 0) {
                handle_char(c);
            } else {
                /* If read failed, delay to avoid spinning */
                vTaskDelay(pdMS_TO_TICKS(10));
            }
        } else {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }

}

/* ─────────────────────────────────────────────
 * Service Lifecycle
 * ───────────────────────────────────────────── */

static int32_t DebugConsole_Initialize(IService *self, IDIRegistry *registry) {
    if (!self) return ESP_ERR_INVALID_ARG;
    esp_rom_printf("[DebugConsole] Initializing DebugConsole...\n");
    if (consoleService.state >= SERVICE_STATE_INITIALIZED) {
        return 0;
    }

    /* DIP: IO driver injection */
    consoleService.io = StdioConsoleIO_GetDriver();
    esp_rom_printf("[DebugConsole] IO driver obtained: %p\n", consoleService.io);

    if (consoleService.mutex == NULL) {
        consoleService.mutex = xSemaphoreCreateMutex();
        if (!consoleService.mutex) return ESP_ERR_NO_MEM;
    }
    if (!consoleService.lifecycleMutex) consoleService.lifecycleMutex = xSemaphoreCreateMutex();
    if (!consoleService.lifecycleMutex) return ESP_ERR_NO_MEM;
    if (!consoleService.stopped) consoleService.stopped = xQueueCreate(1, sizeof(esp_err_t));
    if (!consoleService.stopped) return ESP_ERR_NO_MEM;

    if (!CommandRegistry_Init()) return ESP_ERR_NO_MEM;
    InputParser_Init();
    if (!HistoryManager_Init()) return ESP_ERR_NO_MEM;

#if DEBUG_CONSOLE_ENABLE_LOGGING
    if (!FlashLogger_GetModule()) return ESP_FAIL;
#endif

#if DEBUG_CONSOLE_ENABLE_DASHBOARD
    DashboardManager_GetModule();
#endif

    consoleService.state = SERVICE_STATE_INITIALIZED;
    esp_rom_printf("[DebugConsole] DebugConsole initialized (Mutex: %p)\n",
                   consoleService.mutex);
    return 0;
}

static int32_t DebugConsole_StartOwned(IService *self) {
    if (!self) return ESP_ERR_INVALID_ARG;
    esp_rom_printf("[DebugConsole] Starting DebugConsole...\n");
    if (atomic_load(&consoleService.closing) || consoleService.stopPending) return ESP_ERR_INVALID_STATE;
    if (consoleService.state == SERVICE_STATE_STARTED) {
        return 0;
    }
    if (consoleService.sdkOwned || consoleService.ioOwned || consoleService.consoleTaskHandle)
        return ESP_ERR_INVALID_STATE;

    if (!consoleService.io || !consoleService.io->Initialize || consoleService.io->Initialize(consoleService.io) != 0) return -1;
    consoleService.ioOwned = true;

    ESP_LOGI(TAG, "Initializing esp_console...");
    esp_console_config_t console_config = ESP_CONSOLE_CONFIG_DEFAULT();
    esp_err_t console_result = esp_console_init(&console_config);
    if (console_result != ESP_OK) {
        if (!consoleService.io->Deinitialize || consoleService.io->Deinitialize(consoleService.io) == 0)
            consoleService.ioOwned = false;
        else ESP_LOGE(TAG, "Console IO rollback failed");
        return console_result;
    }
    consoleService.sdkOwned = true;

    /* Core command modules */
    esp_err_t diagnostics_result = DiagnosticCommands_Register(&consoleService.base);
    if (diagnostics_result != ESP_OK) {
        if (dc_cleanup_transport() != ESP_OK) ESP_LOGE(TAG, "Diagnostic registration rollback failed");
        return diagnostics_result;
    }
    SystemCommands_Register(&consoleService.base);

#if DEBUG_CONSOLE_ENABLE_PERFMON
    PerformanceCommands_Register(&consoleService.base);
#endif

    HardwareCommands_Register(&consoleService.base);

#if DEBUG_CONSOLE_ENABLE_NETWORK_CMDS
    NetworkCommands_Register(&consoleService.base);
#endif

    /* Plugins */
    ConsolePlugin plugins[DEBUG_CONSOLE_MAX_PLUGINS];
    int pluginCount = 0;
    if (dc_take_mutex(pdMS_TO_TICKS(DEBUG_CONSOLE_CONTROL_TIMEOUT_MS))) {
        pluginCount = consoleService.pluginCount;
        memcpy(plugins, consoleService.plugins, (size_t)pluginCount * sizeof(*plugins));
        /* Later plugins register immediately, including reentrant additions
         * from these callbacks. The worker is admitted only after Start returns. */
        consoleService.state = SERVICE_STATE_STARTED;
        dc_give_mutex();
    } else {
        if (dc_cleanup_transport() != ESP_OK) ESP_LOGE(TAG, "Plugin admission rollback failed");
        return ESP_ERR_TIMEOUT;
    }
    for (int i = 0; i < pluginCount; i++) plugins[i].register_func(&consoleService.base);
    int registrationError = atomic_load(&consoleService.registrationError);
    if (registrationError != ESP_OK) {
        consoleService.state = SERVICE_STATE_INITIALIZED;
        if (dc_cleanup_transport() != ESP_OK) ESP_LOGE(TAG, "Command registration rollback failed");
        return registrationError;
    }

    BaseType_t task_result = xTaskCreate(console_task, "console_task",
                DEBUG_CONSOLE_TASK_STACK_SIZE,
                (void *)&consoleService,
                DEBUG_CONSOLE_TASK_PRIORITY,
                &consoleService.consoleTaskHandle);
    if (task_result != pdPASS) {
        consoleService.state = SERVICE_STATE_INITIALIZED;
        consoleService.consoleTaskHandle = NULL;
        if (esp_console_deinit() == ESP_OK) consoleService.sdkOwned = false;
        else ESP_LOGE(TAG, "Console SDK rollback failed");
        if (!consoleService.sdkOwned) {
            if (!consoleService.io->Deinitialize || consoleService.io->Deinitialize(consoleService.io) == 0)
                consoleService.ioOwned = false;
            else ESP_LOGE(TAG, "Console IO rollback failed");
        }
        return ESP_ERR_NO_MEM;
    }

    /* Default log level */
    if (consoleService.logsMuted) {
        esp_log_level_set("*", ESP_LOG_WARN);
    } else {
        esp_log_level_set("*", ESP_LOG_INFO);
    }

    ConsoleEventPublisher_Started();

    esp_rom_printf("[DebugConsole] DebugConsole started task: %p\n",
                   consoleService.consoleTaskHandle);
    ESP_LOGI(TAG, "DebugConsole started in %s mode.",
             consoleService.logsMuted ? "Muted" : "Monitor");
    return 0;
}

static int32_t DebugConsole_Start(IService *self) {
    if (!self || !consoleService.lifecycleMutex) return ESP_ERR_INVALID_STATE;
    if ((atomic_load(&consoleService.registeringModule) &&
         atomic_load(&consoleService.registrationOwner) == xTaskGetCurrentTaskHandle()) ||
        (atomic_load(&consoleService.starting) &&
         atomic_load(&consoleService.startOwner) == xTaskGetCurrentTaskHandle())) return ESP_ERR_INVALID_STATE;
    if (xSemaphoreTake(consoleService.lifecycleMutex, pdMS_TO_TICKS(DEBUG_CONSOLE_CONTROL_TIMEOUT_MS)) != pdTRUE)
        return ESP_ERR_TIMEOUT;
    bool starting = consoleService.state != SERVICE_STATE_STARTED;
    if (starting) {
        atomic_store(&consoleService.startOwner, xTaskGetCurrentTaskHandle());
        atomic_store(&consoleService.starting, true);
        if (atomic_load(&consoleService.activeCommands)) {
            atomic_store(&consoleService.starting, false);
            atomic_store(&consoleService.startOwner, NULL);
            xSemaphoreGive(consoleService.lifecycleMutex);
            return ESP_ERR_INVALID_STATE;
        }
        atomic_store(&consoleService.registrationError, ESP_OK);
    }
    int32_t result = DebugConsole_StartOwned(self);
    if (starting) {
        atomic_store(&consoleService.starting, false);
        atomic_store(&consoleService.startOwner, NULL);
    }
    xSemaphoreGive(consoleService.lifecycleMutex);
    return result;
}

static int32_t DebugConsole_Stop(IService *self) {
    if (!self || !consoleService.lifecycleMutex) return ESP_ERR_INVALID_STATE;
    if ((atomic_load(&consoleService.registeringModule) &&
         atomic_load(&consoleService.registrationOwner) == xTaskGetCurrentTaskHandle()) ||
        (atomic_load(&consoleService.starting) &&
         atomic_load(&consoleService.startOwner) == xTaskGetCurrentTaskHandle())) return ESP_ERR_INVALID_STATE;
    if (consoleService.consoleTaskHandle && consoleService.consoleTaskHandle == xTaskGetCurrentTaskHandle())
        return ESP_ERR_INVALID_STATE;
    if (xSemaphoreTake(consoleService.lifecycleMutex, pdMS_TO_TICKS(DEBUG_CONSOLE_CONTROL_TIMEOUT_MS)) != pdTRUE)
        return ESP_ERR_TIMEOUT;
    atomic_store(&consoleService.closing, true);
    int32_t result = ESP_OK;
    if (consoleService.consoleTaskHandle) {
        consoleService.stopPending = true;
        esp_err_t acknowledgment = ESP_FAIL;
        if (xQueueReceive(consoleService.stopped, &acknowledgment,
                pdMS_TO_TICKS(DEBUG_CONSOLE_CONTROL_TIMEOUT_MS)) != pdTRUE) {
            result = ESP_ERR_TIMEOUT;
            goto done;
        }
        consoleService.stopPending = false;
        if (acknowledgment != ESP_OK) { result = acknowledgment; goto done; }
        vTaskDelete(consoleService.consoleTaskHandle);
        consoleService.consoleTaskHandle = NULL;
    }
    TickType_t waitStart = xTaskGetTickCount();
    while (atomic_load(&consoleService.activeCommands)) {
        if ((TickType_t)(xTaskGetTickCount() - waitStart) >= pdMS_TO_TICKS(DEBUG_CONSOLE_CONTROL_TIMEOUT_MS)) {
            result = ESP_ERR_TIMEOUT;
            goto done;
        }
        vTaskDelay(1);
    }
    result = dc_cleanup_transport();
    if (result != ESP_OK) goto done;
    consoleService.state = SERVICE_STATE_STOPPED;
    atomic_store(&consoleService.closing, false);
    ConsoleEventPublisher_Stopped();
done:
    xSemaphoreGive(consoleService.lifecycleMutex);
    return result;
}

static ServiceState DebugConsole_GetState(IService *self) {
    return consoleService.state;
}

static int32_t DebugConsole_CheckHealth(IService *self) {
    if (atomic_load(&consoleService.closing)) return ESP_ERR_INVALID_STATE;
    if (consoleService.state != SERVICE_STATE_STARTED) return -1;
    if (consoleService.consoleTaskHandle == NULL) return -2;
    return 0;
}

/* ─────────────────────────────────────────────
 * Singleton factory
 * ───────────────────────────────────────────── */

IDebugConsole *DebugConsole_GetModule(void) {
    static bool init = false;
    if (!init) {
        consoleService.base.base.Initialize  = DebugConsole_Initialize;
        consoleService.base.base.Start       = DebugConsole_Start;
        consoleService.base.base.Stop        = DebugConsole_Stop;
        consoleService.base.base.GetState    = DebugConsole_GetState;
        consoleService.base.base.CheckHealth = DebugConsole_CheckHealth;

        consoleService.base.ProcessCommand            = DebugConsole_ProcessCommand;
        consoleService.base.RegisterCommand            = DebugConsole_RegisterCommand;
        consoleService.base.RegisterModule             = DebugConsole_RegisterModule;
        consoleService.base.SetLiveMode                = DebugConsole_SetLiveMode;
        consoleService.base.SetInterceptor             = DebugConsole_SetInterceptor;
        consoleService.base.GetCommandHistory           = DebugConsole_GetCommandHistory;
        consoleService.base.CopyCommandHistory          = DebugConsole_CopyCommandHistory;
        consoleService.base.SetMuteLog                 = DebugConsole_SetMuteLog;
        consoleService.base.GetRegisteredCommandCount   = DebugConsole_GetRegisteredCommandCount;
        consoleService.base.ExecuteBatch               = DebugConsole_ExecuteBatch;
        consoleService.base.RegisterCommandWithCategory = DebugConsole_RegisterCommandWithCategory;

        consoleService.state     = SERVICE_STATE_UNINITIALIZED;
        consoleService.logsMuted = true;

        if (consoleService.mutex == NULL) {
            consoleService.mutex = xSemaphoreCreateMutex();
        }

        init = true;
    }
    return &consoleService.base;
}
