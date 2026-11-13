#include "Core/Localization/Localization.h"
/**
 * @file ApplicationManagerConsoleCommands.c
 * @brief Debug Console CLI commands for ApplicationManager Module.
 *
 * @author RaCool DeepMind Team
 * @date 2026-08-01
 */

#include "Management/ApplicationManager/ApplicationManagerConsoleCommands.h"
#include "Management/ApplicationManager/IApplicationManager.h"
#include "Core/ServiceLocator/ServiceLocator.h"
#include "Core/ServiceLocator/ServiceNames.h"
#include "Communication/DebugConsole/IDebugConsole.h"
#include "esp_log.h"
#include <stdio.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#ifndef RACOOL_TEST_NATIVE
#include <esp_console.h>
#endif

static const char *APP_CMD_TAG = "AppConsole";

static bool App_ParseInteger(const char *text, int64_t minimum, int64_t maximum, int64_t *out) {
    if (!text || !*text || !out) return false;
    char *end = NULL;
    errno = 0;
    long long value = strtoll(text, &end, 10);
    if (errno == ERANGE || end == text || *end || value < minimum || value > maximum) return false;
    *out = value;
    return true;
}
static bool App_ParseTemperature(const char *text, float *out) {
    if (!text || !*text || !out) return false;
    char *end = NULL;
    errno = 0;
    float value = strtof(text, &end);
    if (errno == ERANGE || end == text || *end || !isfinite(value)) return false;
    *out = value;
    return true;
}
static int App_QueueResult(const LocalizationCatalog *catalog, esp_err_t result) {
    if (result == ESP_ERR_NOT_SUPPORTED) {
        LOC_PRINTF(catalog, LOC_CLI_APP_UNSUPPORTED, "[AppCmd] Requested action is not implemented.\n");
        return 1;
    }
    LOC_PRINTF(catalog, LOC_CLI_APP_QUEUE_RESULT, "[AppCmd] %s (result: %d)\n", Localization_Text(catalog, result == ESP_OK ? LOC_CLI_APP_QUEUED : LOC_CLI_APP_REJECTED), result);
    return result == ESP_OK ? 0 : 1;
}

static int cmd_app_handler(int argc, char **argv) {
    const LocalizationCatalog *catalog = Localization_Snapshot();
    if (argc < 1 || argc > 4 || !argv) return 1;
    for (int i = 0; i < argc; ++i) if (!argv[i] || !*argv[i]) return 1;
    IApplicationManager *appMgr = (IApplicationManager *)ServiceLocator_GetService(SVC_NAME_APPLICATION_MANAGER);
    if (!appMgr) {
        LOC_PRINTF(catalog, LOC_CLI_APP_UNAVAILABLE, "[AppCmd] Error: ApplicationManager service not found!\n");
        return 1;
    }
    if (argc == 1 || (argc == 2 && strcmp(argv[1], "status") == 0)) {
        if (!appMgr->GetStatus) return 1;
        AppManagerStatus status = {0};
        esp_err_t err = appMgr->GetStatus(appMgr, &status);
        if (err != ESP_OK) {
            LOC_PRINTF(catalog, LOC_CLI_APP_STATUS_FAILED, "[AppCmd] Failed to get status (err: %d)\n", err);
            return 1;
        }
        LOC_PRINTF(catalog, LOC_CLI_APP_TITLE, "\n=== ApplicationManager Status ===\n");
        LOC_PRINTF(catalog, LOC_CLI_APP_STATE, "  Service State     : %d\n", status.state);
        LOC_PRINTF(catalog, LOC_CLI_APP_PROCESSED, "  Processed Events  : %lu\n", (unsigned long)status.processedEventsCount);
        LOC_PRINTF(catalog, LOC_CLI_APP_DROPPED, "  Dropped Events    : %lu\n", (unsigned long)status.droppedEventsCount);
        LOC_PRINTF(catalog, LOC_CLI_APP_QUEUE, "  Queue Usage       : %lu / %lu\n", (unsigned long)status.queueCurrentUsage, (unsigned long)status.queueCapacity);
        LOC_PRINTF(catalog, LOC_CLI_APP_UPTIME, "  Uptime            : %lu s\n", (unsigned long)(status.uptimeMs / 1000));
        LOC_PRINTF(catalog, LOC_CLI_APP_WATCHDOG, "  Watchdog Monitored: %s\n", Localization_Text(catalog, status.isWatchdogRegistered ? LOC_CLI_APP_YES : LOC_CLI_APP_NO));
        LOC_PRINTF(catalog, LOC_CLI_APP_RESET_STATUS, "  Factory Reset     : id=%lu phase=%lu error=%ld busy=%s start=%lu ms\n",
               (unsigned long)status.factoryResetId, (unsigned long)status.factoryResetPhase,
               (long)status.factoryResetError, Localization_Text(catalog, status.factoryResetBusy ? LOC_CLI_APP_YES : LOC_CLI_APP_NO),
               (unsigned long)status.factoryResetStartedMs);
        printf("=================================\n\n");
        return 0;
    }
    if (argc == 2 && strcmp(argv[1], "reset_factory") == 0)
        return appMgr->TriggerFactoryReset ? App_QueueResult(catalog, appMgr->TriggerFactoryReset(appMgr)) : 1;

    AppManagerEvent evt = {0};
    int64_t value;
    if (argc == 3 && strcmp(argv[1], "set_temp") == 0) {
        if (!App_ParseTemperature(argv[2], &evt.payload.targetTemperature)) return 1;
        evt.id = APP_EVENT_SET_TARGET_TEMP;
        return appMgr->PostEvent ? App_QueueResult(catalog, appMgr->PostEvent(appMgr, &evt)) : 1;
    }
    if (argc == 3 && strcmp(argv[1], "toggle_relay") == 0) {
        if (!App_ParseInteger(argv[2], 0, 1, &value)) return 1;
        evt.id = APP_EVENT_TOGGLE_RELAY;
        evt.payload.relayState = value != 0;
        return appMgr->PostEvent ? App_QueueResult(catalog, appMgr->PostEvent(appMgr, &evt)) : 1;
    }
    if ((argc == 3 || argc == 4) && strcmp(argv[1], "post_event") == 0) {
        if (!App_ParseInteger(argv[2], APP_EVENT_ROTATION, APP_EVENT_FACTORY_RESET, &value)) return 1;
        evt.id = (AppManagerEventId)value;
        switch (evt.id) {
            case APP_EVENT_ROTATION:
                if (argc != 4 || !App_ParseInteger(argv[3], INT32_MIN, INT32_MAX, &value)) return 1;
                evt.payload.rawValue = (int32_t)value;
                break;
            case APP_EVENT_BUTTON_LONGPRESS:
                if (argc != 4 || !App_ParseInteger(argv[3], 0, UINT32_MAX, &value)) return 1;
                evt.payload.durationMs = (uint32_t)value;
                break;
            case APP_EVENT_TOGGLE_RELAY:
                if (argc != 4 || !App_ParseInteger(argv[3], 0, 1, &value)) return 1;
                evt.payload.relayState = value != 0;
                break;
            case APP_EVENT_SET_TARGET_TEMP:
                if (argc != 4 || !App_ParseTemperature(argv[3], &evt.payload.targetTemperature)) return 1;
                break;
            default:
                if (argc != 3) return 1;
                break;
        }
        return appMgr->PostEvent ? App_QueueResult(catalog, appMgr->PostEvent(appMgr, &evt)) : 1;
    }
    LOC_PRINTF(catalog, LOC_CLI_APP_USAGE, "[AppCmd] Usage: app [status | reset_factory | set_temp <deg> | toggle_relay <0|1> | post_event <id> [val]]\n");
    return 1;
}

esp_err_t ApplicationManager_RegisterConsoleCommands(void) {
    IDebugConsole *console = (IDebugConsole *)ServiceLocator_GetService(SVC_NAME_DEBUG_CONSOLE);
    if (!console || !console->RegisterCommandWithCategory) return ESP_ERR_INVALID_STATE;
    const esp_console_cmd_t app_cmd = {
        .command = "app",
        .help = "ApplicationManager commands (status, reset_factory, set_temp, toggle_relay, post_event)",
        .hint = "<status|reset_factory|set_temp|toggle_relay|post_event>",
        .func = &cmd_app_handler,
        .argtable = NULL
    };

    esp_err_t result = console->RegisterCommandWithCategory(console, &app_cmd, CMD_CAT_SYSTEM);
    if (result != ESP_OK) return result;
    ESP_LOGI(APP_CMD_TAG, "ApplicationManager Debug Console commands registered ('app')");
    return ESP_OK;
}
