#include "Core/Localization/Localization.h"
#include "Core/ErrorHandler/ErrorHandlerInput.h"
#include "Core/ErrorHandler/IErrorHandler.h"
#include "Core/ErrorHandler/ErrorHandlerConfig.h"
#include "Core/ErrorHandler/ErrorHandlerConsoleCommands.h"
#include "Core/ServiceLocator/ServiceLocator.h"
#include "Core/ServiceLocator/ServiceNames.h"
#include "Communication/DebugConsole/IDebugConsole.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


// Forward declarations
static int cmd_error_status(int argc, char **argv);
static int cmd_error_history(int argc, char **argv);
static int cmd_error_trigger(int argc, char **argv);
static int cmd_error_trigger_isr(int argc, char **argv);
static int cmd_error_clear(int argc, char **argv);
static int cmd_error_selftest(int argc, char **argv);

esp_err_t ErrorHandler_RegisterConsoleCommands(void) {

    IDebugConsole *console = (IDebugConsole *)ServiceLocator_GetService(SVC_NAME_DEBUG_CONSOLE);
    if (!console || !console->RegisterCommandWithCategory) return ESP_ERR_INVALID_STATE;

    static const esp_console_cmd_t cmds[] = {
        { .command = "error_status",      .help = "Show ErrorHandler state and statistics",                          .func = &cmd_error_status,      .hint = NULL       },
        { .command = "error_history",     .help = "Display circular error history log",                              .func = &cmd_error_history,     .hint = NULL       },
        { .command = "error_trigger",     .help = "Inject async test error: error_trigger <tag> <code>",             .func = &cmd_error_trigger,     .hint = "<tag> <code>" },
        { .command = "error_trigger_isr", .help = "Queue task-context simulation of ISR-originated error: error_trigger_isr <tag> <code>",  .func = &cmd_error_trigger_isr, .hint = "<tag> <code>" },
        { .command = "error_clear",       .help = "Clear error history and reset counters",                          .func = &cmd_error_clear,       .hint = NULL       },
        { .command = "error_selftest",    .help = "Run ErrorHandler internal self-test suite",                       .func = &cmd_error_selftest,    .hint = NULL       },
    };

    for (size_t i = 0; i < sizeof(cmds) / sizeof(cmds[0]); i++) {
        esp_err_t result = console->RegisterCommandWithCategory(console, &cmds[i], CMD_CAT_SYSTEM);
        if (result != ESP_OK) return result;
    }
    return ESP_OK;
}

static int cmd_error_status(int argc, char **argv) {
    const LocalizationCatalog *catalog = Localization_Snapshot();
    bool machine=argc==2 && argv && argv[0] && argv[1] && strcmp(argv[1],"--machine")==0;
    if (!machine && !ErrorHandler_NoArguments(argc,argv)) {LOC_PRINTF(catalog, LOC_CLI_ERROR_STATUS_USAGE, "Usage: error_status\n");return 1;}
    IErrorHandler *eh=GetErrorHandler();
    ErrorHandlerStatistics statistics;
    if (!eh || !eh->GetStatistics || eh->GetStatistics(&statistics)!=0) {
        LOC_PRINTF(catalog, LOC_CLI_ERROR_STATISTICS_UNAVAILABLE, "Error statistics unavailable.\n");return 1;
    }
    if (machine) {
        printf("RACOOL_CLI v=1 type=error_status state=%d last_error=%ld total_errors=%lu callbacks=%lu\n",
            (int)statistics.state,(long)statistics.lastErrorCode,
            (unsigned long)statistics.totalErrorCount,(unsigned long)statistics.callbackCount);
        return 0;
    }
    LOC_PRINTF(catalog, LOC_CLI_ERROR_TITLE, "\n--- ErrorHandler Status ---\n");
    LOC_PRINTF(catalog, LOC_CLI_ERROR_STATE, "State       : %d\n", (int)statistics.state);
    LOC_PRINTF(catalog, LOC_CLI_ERROR_LAST, "Last Error  : %d\n", (int)statistics.lastErrorCode);
    LOC_PRINTF(catalog, LOC_CLI_ERROR_TOTAL, "Total errors: %lu\n", (unsigned long)statistics.totalErrorCount);
    LOC_PRINTF(catalog, LOC_CLI_ERROR_CALLBACKS, "Callbacks   : %lu\n", (unsigned long)statistics.callbackCount);
    printf("---------------------------\n");
    return 0;
}

static int cmd_error_history(int argc, char **argv) {
    const LocalizationCatalog *catalog = Localization_Snapshot();
    if (!ErrorHandler_NoArguments(argc,argv)) {LOC_PRINTF(catalog, LOC_CLI_ERROR_HISTORY_USAGE, "Usage: error_history\n");return 1;}
    (void)argc; (void)argv;
    IErrorHandler *eh = GetErrorHandler();
    if (!eh || !eh->GetErrorHistory) { LOC_PRINTF(catalog, LOC_CLI_ERROR_UNAVAILABLE, "ErrorHandler not available.\n"); return 1; }

    const ErrorHandlerConfig *cfg = ErrorHandlerConfig_GetConfig();
    ErrorRecord *recs = malloc(sizeof(ErrorRecord) * cfg->historySize);
    if (!recs) { LOC_PRINTF(catalog, LOC_CLI_ERROR_ALLOCATION, "Memory allocation failed.\n"); return 1; }

    uint32_t count = 0;
    int32_t result=eh->GetErrorHistory(recs,cfg->historySize,&count);
    if (result == 0) {
        LOC_PRINTF(catalog, LOC_CLI_ERROR_HISTORY_TITLE, "\n--- Error History (Total: %lu) ---\n", (unsigned long)count);
        for (uint32_t i = 0; i < count; i++) {
            LOC_PRINTF(catalog, LOC_CLI_ERROR_RECORD, "[%3lu] Tag: %-16s | Code: %-6d | Tick: %lu | Async: %s\n",
                   (unsigned long)i, recs[i].tag, (int)recs[i].errorCode,
                   (unsigned long)recs[i].timestamp, Localization_Text(catalog, recs[i].isAsync ? LOC_CLI_ERROR_YES : LOC_CLI_ERROR_NO));
        }
        printf("----------------------------------\n");
    } else {
        LOC_PRINTF(catalog, LOC_CLI_ERROR_HISTORY_FAILED, "Failed to retrieve error history.\n");
    }
    free(recs);
    return result==0 ? 0 : 1;
}

static int cmd_error_trigger(int argc, char **argv) {
    const LocalizationCatalog *catalog = Localization_Snapshot();
    int32_t code;
    if (!ErrorHandler_ParseInput(argc, argv, &code)) { LOC_PRINTF(catalog, LOC_CLI_ERROR_TRIGGER_USAGE, "Usage: error_trigger <tag> <code>\n"); return 1; }
    IErrorHandler *eh = GetErrorHandler();
    if (!eh || !eh->HandleErrorAsync) { LOC_PRINTF(catalog, LOC_CLI_ERROR_UNAVAILABLE, "ErrorHandler not available.\n"); return 1; }
    int32_t err = eh->HandleErrorAsync(argv[1], code);
    if (err == 0) LOC_PRINTF(catalog, LOC_CLI_ERROR_QUEUED, "Async error queued.\n");
    else LOC_PRINTF(catalog, LOC_CLI_ERROR_QUEUE_FAILED, "Failed to queue error (err=%d).\n", (int)err);
    return err == 0 ? 0 : 1;
}

static int cmd_error_trigger_isr(int argc, char **argv) {
    const LocalizationCatalog *catalog = Localization_Snapshot();
    int32_t code;
    if (!ErrorHandler_ParseInput(argc, argv, &code)) { LOC_PRINTF(catalog, LOC_CLI_ERROR_ISR_USAGE, "Usage: error_trigger_isr <tag> <code>\n"); return 1; }
    IErrorHandler *eh = GetErrorHandler();
    if (!eh || !eh->HandleErrorAsync) { LOC_PRINTF(catalog, LOC_CLI_ERROR_UNAVAILABLE, "ErrorHandler not available.\n"); return 1; }
    int32_t err = eh->HandleErrorAsync(argv[1], code);
    if (err == 0) {
        LOC_PRINTF(catalog, LOC_CLI_ERROR_ISR_QUEUED, "Task-context ISR simulation queued.\n");
    } else {
        LOC_PRINTF(catalog, LOC_CLI_ERROR_ISR_FAILED, "Failed to queue ISR error (err=%d).\n", (int)err);
    }
    return err == 0 ? 0 : 1;
}

static int cmd_error_clear(int argc, char **argv) {
    const LocalizationCatalog *catalog = Localization_Snapshot();
    if (!ErrorHandler_NoArguments(argc,argv)) {LOC_PRINTF(catalog, LOC_CLI_ERROR_CLEAR_USAGE, "Usage: error_clear\n");return 1;}
    (void)argc; (void)argv;
    IErrorHandler *eh = GetErrorHandler();
    if (!eh || !eh->ClearHistory) { LOC_PRINTF(catalog, LOC_CLI_ERROR_UNAVAILABLE, "ErrorHandler not available.\n"); return 1; }
    int32_t result=eh->ClearHistory();
    if (result == 0) LOC_PRINTF(catalog, LOC_CLI_ERROR_CLEARED, "Error history cleared.\n");
    else LOC_PRINTF(catalog, LOC_CLI_ERROR_CLEAR_FAILED, "Failed to clear error history (%ld).\n", (long)result);
    return result==0 ? 0 : 1;
}

static int cmd_error_selftest(int argc, char **argv) {
    const LocalizationCatalog *catalog = Localization_Snapshot();
    if (!ErrorHandler_NoArguments(argc,argv)) {LOC_PRINTF(catalog, LOC_CLI_ERROR_TEST_USAGE, "Usage: error_selftest\n");return 1;}
    (void)argc; (void)argv;
    IErrorHandler *eh = GetErrorHandler();
    if (!eh || !eh->PerformSelfTest) { LOC_PRINTF(catalog, LOC_CLI_ERROR_UNAVAILABLE, "ErrorHandler not available.\n"); return 1; }
    int32_t res = eh->PerformSelfTest();
    if (res == 0) LOC_PRINTF(catalog, LOC_CLI_ERROR_PASSED, "Self test PASSED.\n");
    else LOC_PRINTF(catalog, LOC_CLI_ERROR_FAILED, "Self test FAILED (code=%d).\n", (int)res);
    return res == 0 ? 0 : 1;
}
