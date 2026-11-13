#include "Core/Localization/Localization.h"
#include "Hardware/LEDAnimations/LEDAnimationLabels.h"
/**
 * @file BuiltinLEDConsoleCommands.c
 * @brief Debug Console CLI commands for BuiltinLED Module.
 *
 * Command: bled
 *   bled status              -- GetStatus snapshot
 *   bled on                  -- Persist automatic mode
 *   bled on <ms>             -- Temporary solid-white notification
 *   bled off                 -- Persist global disabled mode
 *   bled anim <type> [ms]     -- Temporary animation override (IDs from builtin_led_list)
 *   bled brightness <0-255>  -- SetBrightness
 */

#include "Hardware/BuiltinLED/BuiltinLEDConsoleCommands.h"
#include "Hardware/BuiltinLED/IBuiltinLED.h"
#include "Hardware/BuiltinLED/BuiltinLEDInput.h"
#include "Hardware/BuiltinLED/BuiltinLEDCLI.h"
#include "Core/ServiceLocator/ServiceLocator.h"
#include "Core/ServiceLocator/ServiceNames.h"
#include "Communication/DebugConsole/IDebugConsole.h"
#include "esp_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef RACOOL_TEST_NATIVE
#include <esp_console.h>
#endif

static const char *BLED_CMD_TAG = "BLEDConsole";

static int dispatch_bled(int argc, char **argv) {
    const LocalizationCatalog *catalog = Localization_Snapshot();
    if (!BuiltinLEDInput_Arguments(argc, argv, 1, 4)) return 1;
    IBuiltinLED *led = BuiltinLED_GetModule();
    if (!led) { LOC_PRINTF(catalog, LOC_CLI_BUILTIN_BUILTINLED_SERVICE_UNAVAILABLE, "[BuiltinLED] Service unavailable.\n"); return -1; }

    if (argc == 1 || strcmp(argv[1], "status") == 0) {
        if (argc > 3 || (argc == 3 && strcmp(argv[2], "--machine") != 0) || !led->GetStatus) return 1;
        BuiltinLEDStatus s = {0};
        esp_err_t driver_error = ESP_OK;
        if (argc > 1 && (!led->GetDriverError || led->GetDriverError(led, &driver_error) != ESP_OK)) return 1;
        if (led->GetStatus(led, &s) == ESP_OK) {
            if (argc == 3) {
                BuiltinLED_PrintMachineStatus(&s, driver_error);
                return 0;
            }
            LOC_PRINTF(catalog, LOC_CLI_BUILTIN_BUILTINLED_STATUS, "--- BuiltinLED Status ---\n");
            LOC_PRINTF(catalog, LOC_CLI_BUILTIN_ON_S, " On          : %s\n", s.is_on ? Localization_Text(catalog, LOC_CLI_BUILTIN_YES) : Localization_Text(catalog, LOC_CLI_BUILTIN_NO));
            LOC_PRINTF(catalog, LOC_CLI_BUILTIN_ENABLED_S, " Enabled     : %s\n", s.enabled ? Localization_Text(catalog, LOC_CLI_BUILTIN_YES) : Localization_Text(catalog, LOC_CLI_BUILTIN_NO));
            LOC_PRINTF(catalog, LOC_CLI_BUILTIN_BRIGHTNESS_D, " Brightness  : %d\n", s.brightness);
            LOC_PRINTF(catalog, LOC_CLI_BUILTIN_ANIMATION_D, " Animation   : %d\n", (int)s.current_animation);
            if (argc == 1) { printf("-------------------------\n"); return 0; }
            LOC_PRINTF(catalog, LOC_CLI_BUILTIN_PRIORITY_D, " Priority    : %d\n", (int)s.current_priority);
            LOC_PRINTF(catalog, LOC_CLI_BUILTIN_REMAINING_LU_MS, " Remaining   : %lu ms\n", (unsigned long)s.remaining_ms);
            LOC_PRINTF(catalog, LOC_CLI_BUILTIN_PLUGINS_D, " Plugins     : %d\n", s.registered_plugins_count);
            LOC_PRINTF(catalog, LOC_CLI_BUILTIN_DRIVER_ERROR_0X_X, " Driver Error: 0x%x\n", (unsigned)driver_error);
            printf("-------------------------\n");
        } else {
            LOC_PRINTF(catalog, LOC_CLI_BUILTIN_BUILTINLED_GETSTATUS_FAILED, "[BuiltinLED] GetStatus failed.\n");
            return 1;
        }
        return 0;
    }

    if (strcmp(argv[1], "on") == 0 && argc == 2) {
        esp_err_t e = BuiltinLED_SaveEnabled(true);
        LOC_PRINTF(catalog, LOC_CLI_BUILTIN_BUILTINLED_AUTOMATIC_MODE_SAVE_D, "[BuiltinLED] Automatic mode save -> %d\n", e);
        return e == ESP_OK ? 0 : 1;
    }
    if (strcmp(argv[1], "on") == 0) {
        uint32_t dur;
        if (argc != 3 || !led->SetAnimation || !BuiltinLEDInput_ParseUnsigned(argv[2], UINT32_MAX, &dur)) return 1;
        esp_err_t e = led->SetAnimation(led, SOLID_WHITE, BUILTIN_LED_PRIO_HIGH, dur);
        LOC_PRINTF(catalog, LOC_CLI_BUILTIN_BUILTINLED_TURNON_DUR_LU_MS_D, "[BuiltinLED] TurnOn (dur=%lu ms) -> %d\n", (unsigned long)dur, e);
        return e == ESP_OK ? 0 : 1;
    }

    if (strcmp(argv[1], "off") == 0) {
        if (argc != 2) return 1;
        esp_err_t e = BuiltinLED_SaveEnabled(false);
        LOC_PRINTF(catalog, LOC_CLI_BUILTIN_BUILTINLED_DISABLED_MODE_SAVE_D, "[BuiltinLED] Disabled mode save -> %d\n", e);
        return e == ESP_OK ? 0 : 1;
    }

    if (strcmp(argv[1], "anim") == 0) {
        if (argc < 3 || !led->SetAnimation) return 1;
        uint32_t type = 0, dur = 10000U;
        bool off = strcmp(argv[2], "-1") == 0;
        if ((!off && !BuiltinLEDInput_ParseUnsigned(argv[2], INT32_MAX, &type)) ||
            (argc == 4 && !BuiltinLEDInput_ParseUnsigned(argv[3], UINT32_MAX, &dur))) return 1;
        esp_err_t e = led->SetAnimation(led, off ? BUILTIN_LED_ANIM_NONE : (BuiltinLEDAnimationType)type, BUILTIN_LED_PRIO_HIGH, dur);
        LOC_PRINTF(catalog, LOC_CLI_BUILTIN_BUILTINLED_SETANIMATION_D_DUR_LU_MS_D, "[BuiltinLED] SetAnimation(%d, dur=%lu ms) -> %d\n", off ? -1 : (int)type, (unsigned long)dur, e);
        return e == ESP_OK ? 0 : 1;
    }

    if (strcmp(argv[1], "brightness") == 0) {
        if (argc != 3) { LOC_PRINTF(catalog, LOC_CLI_BUILTIN_USAGE_BLED_BRIGHTNESS_0_255, "Usage: bled brightness <0-255>\n"); return 1; }
        esp_err_t e = BuiltinLED_SaveBrightnessText(argv[2]);
        printf(e == ESP_OK ? Localization_Text(catalog, LOC_CLI_BUILTIN_BUILTINLED_BRIGHTNESS_SAVED) : Localization_Text(catalog, LOC_CLI_BUILTIN_BUILTINLED_SAVE_FAILED_0X_X), e);
        return e == ESP_OK ? 0 : 1;
    }

    if (strcmp(argv[1], "state") == 0) {
        if (argc < 3) {
            LOC_PRINTF(catalog, LOC_CLI_BUILTIN_STATE_USAGE, "Kullanim: bled state <durum_adi> [sure_ms (varsayilan: 10000)]\n");
            LOC_PRINTF(catalog, LOC_CLI_BUILTIN_STATES_TITLE, "Durumlar:\n");
            LOC_PRINTF(catalog, LOC_CLI_BUILTIN_BOOTING_BREATHING_GREEN, "  BOOTING            -> BREATHING_GREEN\n");
            LOC_PRINTF(catalog, LOC_CLI_BUILTIN_IDLE_DOUBLE_BLINK_WHITE, "  IDLE               -> DOUBLE_BLINK_WHITE\n");
            LOC_PRINTF(catalog, LOC_CLI_BUILTIN_HEATING_DOUBLE_BLINK_WHITE, "  HEATING            -> DOUBLE_BLINK_WHITE\n");
            LOC_PRINTF(catalog, LOC_CLI_BUILTIN_PAIRING_ACTIVE_HEARTBEAT_RED, "  PAIRING_ACTIVE     -> HEARTBEAT_RED\n");
            LOC_PRINTF(catalog, LOC_CLI_BUILTIN_CONNECTED_SOLID_GREEN_3S, "  CONNECTED          -> SOLID_GREEN (3s)\n");
            LOC_PRINTF(catalog, LOC_CLI_BUILTIN_REJOIN_HEARTBEAT_RED, "  REJOIN             -> HEARTBEAT_RED\n");
            LOC_PRINTF(catalog, LOC_CLI_BUILTIN_FAULT_POLICE_STROBE, "  FAULT              -> POLICE_STROBE\n");
            LOC_PRINTF(catalog, LOC_CLI_BUILTIN_ALARM_POLICE_STROBE, "  ALARM              -> POLICE_STROBE\n");
            return 1;
        }

        const char *st = argv[2];
        uint32_t dur = 10000U;
        if (!led->SetAnimation || (argc == 4 && !BuiltinLEDInput_ParseUnsigned(argv[3], UINT32_MAX, &dur))) return 1;
        BuiltinLEDAnimationType anim = BUILTIN_LED_ANIM_NONE;

        if (strcasecmp(st, "BOOTING") == 0 || strcasecmp(st, "BOOT") == 0) {
            anim = BREATHING_GREEN;
        } else if (strcasecmp(st, "IDLE") == 0) {
            anim = DOUBLE_BLINK_WHITE;
        } else if (strcasecmp(st, "HEATING") == 0 || strcasecmp(st, "ACTIVE") == 0) {
            anim = DOUBLE_BLINK_WHITE;
        } else if (strcasecmp(st, "PAIRING_ACTIVE") == 0 || strcasecmp(st, "PAIRING") == 0) {
            anim = HEARTBEAT_RED;
        } else if (strcasecmp(st, "CONNECTED") == 0) {
            anim = SOLID_GREEN;
            dur = (argc >= 4) ? dur : 3000U;
        } else if (strcasecmp(st, "REJOIN") == 0 || strcasecmp(st, "CONNECTING") == 0) {
            anim = HEARTBEAT_RED;
        } else if (strcasecmp(st, "FAULT") == 0 || strcasecmp(st, "ERROR") == 0) {
            anim = POLICE_STROBE;
        } else if (strcasecmp(st, "ALARM") == 0) {
            anim = POLICE_STROBE;
        } else {
            LOC_PRINTF(catalog, LOC_CLI_BUILTIN_UNKNOWN_STATE, "[BuiltinLED] Bilinmeyen durum: %s\n", st);
            return -1;
        }

        esp_err_t e = led->SetAnimation(led, anim, BUILTIN_LED_PRIO_HIGH, dur);
        LOC_PRINTF(catalog, LOC_CLI_BUILTIN_STATE_QUEUE_RESULT, "[BuiltinLED] Durum animasyonu kuyruk sonucu: '%s' -> Animasyon: %d, Sure: %lu ms (Sonuc: %d)\n",
               st, (int)anim, (unsigned long)dur, e);
        return e == ESP_OK ? 0 : 1;
    }

    LOC_PRINTF(catalog, LOC_CLI_BUILTIN_USAGE_BLED_STATUS_ON_DUR_MS_OFF_ANIM_TYPE_DUR_MS_STATE_NAME_DUR_MS_BRIGHTNESS, "Usage: bled <status|on [dur_ms]|off|anim <type> [dur_ms]|state <name> [dur_ms]|brightness>\n");
    return -1;
}

static int cmd_bled_handler(int argc, char **argv) {
    if (!BuiltinLEDInput_Arguments(argc, argv, 1, 5)) return 1;
    bool machine = strcmp(argv[argc - 1], "--machine") == 0;
    if (machine && argc >= 3 && strcmp(argv[1], "status") != 0) {
        if (strcmp(argv[1], "on") != 0 && strcmp(argv[1], "off") != 0 &&
            strcmp(argv[1], "anim") != 0 && strcmp(argv[1], "brightness") != 0 &&
            strcmp(argv[1], "state") != 0) return 1;
        int result = dispatch_bled(argc - 1, argv);
        printf("RACOOL_CLI v=1 type=builtin_command command=%s result=%s error=%d\n",
               argv[1], result == 0 ? "accepted" : "rejected", result);
        return result;
    }
    return dispatch_bled(argc, argv);
}

esp_err_t BuiltinLED_RegisterConsoleCommands(void) {
    IDebugConsole *console = (IDebugConsole *)ServiceLocator_GetService(SVC_NAME_DEBUG_CONSOLE);
    if (!console || !console->RegisterCommandWithCategory) return ESP_ERR_INVALID_STATE;
    const esp_console_cmd_t cmd = {
        .command  = "bled",
        .help     = "BuiltinLED control (status, on, off, anim, brightness)",
        .hint     = "<status|on|off|anim|brightness>",
        .func     = &cmd_bled_handler,
        .argtable = NULL
    };
    esp_err_t result = console->RegisterCommandWithCategory(console, &cmd, CMD_CAT_HARDWARE);
    if (result != ESP_OK) return result;
    ESP_LOGI(BLED_CMD_TAG, "BuiltinLED console commands registered ('bled')");
    return ESP_OK;
}
