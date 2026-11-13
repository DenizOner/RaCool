#include "Core/Localization/Localization.h"
#include "Controllers/UIController/UITranslation.h"
/**
 * @file MenuManagerConsoleCommands.c
 * @brief Debug Console CLI commands for MenuManager Module.
 *
 * Provides sub-commands to inspect status, navigate UI states, simulate rotary
 * encoder rotation, simulate button presses, and run self-test.
 *
 * @author RaCool DeepMind Team
 * @date 2026-08-08
 */

#include "Controllers/MenuManager/MenuManagerConsoleCommands.h"
#include "Controllers/MenuManager/IMenuManager.h"
#include "Controllers/MenuManager/MenuNavigator.h"
#include "Management/ConfigurationManager/IConfigurationManager.h"
#include "Communication/Zigbee/IZigbee.h"
#include "Communication/DebugConsole/IDebugConsole.h"
#include "Communication/DebugConsole/Core/ConsoleColors.h"
#include "Core/ServiceLocator/ServiceLocator.h"
#include "Core/ServiceLocator/ServiceNames.h"
#include "Hardware/RotaryEncoder/RotaryEncoderEvents.h"
#include "Hardware/RotaryEncoder/RotaryEncoderTypes.h"
#include "esp_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <limits.h>
#include <string.h>

#ifndef RACOOL_TEST_NATIVE
#include <esp_console.h>
#endif

static const char *MENU_CMD_TAG = "MenuConsole";

/**
 * @brief Console command handler for MenuManager CLI sub-commands.
 *
 * @param argc Number of command line arguments.
 * @param argv Array of argument strings.
 * @return 0 on success, non-zero error code on failure.
 */
static bool ParseMenuNumber(const char *text, long minimum, long maximum, int *value) {
    if (!text || !*text || !value) return false;
    const char *digits = (*text == '+' || *text == '-') ? text + 1 : text;
    if (!*digits) return false;
    for (const char *c = digits; *c; ++c) if (*c < '0' || *c > '9') return false;
    errno = 0;
    char *end = NULL;
    long parsed = strtol(text, &end, 10);
    if (errno == ERANGE || !end || *end || parsed < minimum || parsed > maximum) return false;
    *value = (int)parsed;
    return true;
}

static int SubmitMenuInput(IMenuManager *menu, bool button, int direction, const LocalizationCatalog *catalog, bool machine) {
    IEventBus *bus = ServiceLocator_GetEventBus();
    int32_t result;
    if (bus) {
        if (!bus->Publish) return -1;
        RotaryEncoderRotationEvent rotation = {
            .direction = direction > 0 ? ROTATION_CW : ROTATION_CCW,
            .steps = (int16_t)direction, .velocity = 1
        };
        BusEvent event = {.eventName = button ? RE_EVENT_BUTTON_CLICK : RE_EVENT_ROTATED,
                          .type = EVENT_TYPE_INFO};
        if (!button) { event.data = &rotation; event.dataSize = sizeof(rotation); }
        result = bus->Publish(event);
    } else {
        if (!menu->PostEvent) return -1;
        MenuEvent event = {.type = button ? MENU_EVENT_BUTTON_PRESSED : MENU_EVENT_ENCODER_ROTATED};
        if (!button) event.data.encoderDirection = (int8_t)direction;
        result = menu->PostEvent(menu, &event);
    }
    if (result != 0) {
        if (machine) { printf("RACOOL_CLI v=1 type=menu_input result=rejected input=%s error=%ld\n", button ? "button" : "encoder", (long)result); return 1; }
        LOC_PRINTF(catalog, LOC_CLI_MENU_INPUT_REJECTED, "[MenuManager] Input request rejected (%ld).\n", (long)result);
        return -1;
    }
    if (menu->UpdateActivity) menu->UpdateActivity(menu);
    if (machine) { printf("RACOOL_CLI v=1 type=menu_input result=accepted input=%s\n", button ? "button" : "encoder"); return 0; }
    LOC_PRINTF(catalog, LOC_CLI_MENU_INPUT_ACCEPTED, "[MenuManager] %s request accepted; use status to inspect worker state.\n",
           Localization_Text(catalog, button ? LOC_CLI_MENU_BUTTON : LOC_CLI_MENU_ENCODER));
    return 0;
}

static int menu_console_cmd_handler(int argc, char **argv) {
    const LocalizationCatalog *catalog = Localization_Snapshot();
    if (!argv || argc < 1 || argc > 4) return 1;
    for (int i=0; i<argc; ++i) if (!argv[i]) return 1;
    const bool machine = argc > 1 && strcmp(argv[argc-1], "--machine") == 0;
    if (machine) --argc;

    if (argc < 1 || !argv || !argv[0] || (argc > 1 && !argv[1])) return -1;
    IMenuManager *menu = MenuManager_GetModule();
    if (!menu) return -1;
    const char *command = argc == 1 ? "status" : argv[1];
    if (strcmp(command, "auto-exit") == 0) {
        if (argc != 3 || (strcmp(argv[2], "on") && strcmp(argv[2], "off") && strcmp(argv[2], "status"))) {
            LOC_PRINTF(catalog, LOC_CLI_MENU_AUTO_EXIT_USAGE, "Usage: menu auto-exit <on|off|status>\n");
            return 1;
        }
        IConfigurationManager *owner = (IConfigurationManager *)ServiceLocator_GetService(SVC_NAME_CONFIGURATION_MANAGER);
        SystemSettings snapshot;
        if (!owner || !owner->GetSnapshot) return 1;
        esp_err_t result = ESP_OK;
        const bool change = strcmp(argv[2], "status") != 0;
        uint8_t enabled = strcmp(argv[2], "on") == 0 ? 1u : 0u;
        if (change) {
            if (!owner->UpdateFields) return 1;
            ConfigurationFieldUpdate field = {offsetof(SystemBaseConfigs, menu_auto_exit_enabled), &enabled, sizeof(enabled)};
            result = owner->UpdateFields(owner, "system", &field, 1, true);
        }
        if (result == ESP_OK) result = owner->GetSnapshot(owner, &snapshot);
        if (result == ESP_OK && (snapshot.system.menu_auto_exit_enabled > 1 ||
            (change && snapshot.system.menu_auto_exit_enabled != enabled))) result = ESP_ERR_INVALID_STATE;
        if (result != ESP_OK) {
            if (machine) printf("RACOOL_CLI v=1 type=menu_auto_exit result=failed error=%ld\n", (long)result);
            else LOC_PRINTF(catalog, LOC_CLI_MENU_AUTO_EXIT_FAILED, "Menu auto-exit could not be read or applied (%ld).\n", (long)result);
            return 1;
        }
        enabled = snapshot.system.menu_auto_exit_enabled;
        if (machine) printf("RACOOL_CLI v=1 type=menu_auto_exit result=ok enabled=%u\n", (unsigned)enabled);
        else LOC_PRINTF(catalog, LOC_CLI_MENU_AUTO_EXIT_STATUS, "Menu auto-exit: %s\n",
            Localization_Text(catalog, enabled ? LOC_COMMON_ON : LOC_COMMON_OFF));
        return 0;
    }
    if (strcmp(command, "visible") == 0) {
        int state;
        if (!machine || argc!=3 || !ParseMenuNumber(argv[2],0,UI_STATE_COUNT-1,&state)) return 1;
        IConfigurationManager *owner=(IConfigurationManager *)ServiceLocator_GetService(SVC_NAME_CONFIGURATION_MANAGER);
        SystemSettings settings;
        if (!owner || !owner->GetSnapshot || owner->GetSnapshot(owner,&settings)!=ESP_OK) return 1;
        IZigbee *radio=(IZigbee *)ServiceLocator_GetService(SVC_NAME_ZIGBEE);
        bool connected=radio && radio->IsConnected && radio->IsConnected(radio)>0;
        MenuVisibilityContext context=MenuNavigator_Context(&settings,connected);
        printf("RACOOL_CLI v=1 type=menu_visibility state=%d visible=%u\n",state,
               MenuNavigator_IsVisible((UIState)state,&context) ? 1u : 0u);
        return 0;
    }
    if (strcmp(command, "status") == 0) {
        if (argc > 2 || !menu->GetCurrentState || !menu->CopyLabel) return -1;
        UIState state = menu->GetCurrentState(menu);
        char label[MENU_LABEL_CAPACITY] = "---";
        if (!menu->CopyLabel(menu, state, label, sizeof(label))) return -1;
        if (machine) {
            printf("RACOOL_CLI v=1 type=menu_status state=%d registry_label_hex=", (int)state);
            for (size_t i=0; i<sizeof(label) && label[i]; ++i) printf("%02x", (unsigned)(unsigned char)label[i]);
            printf("\n"); return 0;
        }
        LOC_PRINTF(catalog, LOC_CLI_MENU_TITLE, "=== MenuManager Status ===\n Current UI State : %d (%s)\n", (int)state, UITranslation_MenuLabel(state,label,catalog));
        return 0;
    }
    if (strcmp(command, "nav") == 0 || strcmp(command, "reset") == 0) {
        bool reset = strcmp(command, "reset") == 0;
        int state = UI_STATE_MONITOR;
        if (argc != (reset ? 2 : 3) || (!reset && !ParseMenuNumber(argv[2],0,UI_STATE_COUNT-1,&state)) ||
            !menu->PostEvent) {
            LOC_PRINTF(catalog, LOC_CLI_MENU_NAV_USAGE, "Usage: menu nav <valid_state_id> | menu reset\n");
            return -1;
        }
        MenuEvent event = {.type = MENU_EVENT_NAVIGATE_TO, .data.targetState = (UIState)state};
        MenuManagerError result = menu->PostEvent(menu, &event);
        if (result != MENU_OK) {
            if (machine) { printf("RACOOL_CLI v=1 type=menu_navigation result=rejected state=%d error=%d\n", state, result); return 1; }
            LOC_PRINTF(catalog, LOC_CLI_MENU_NAV_REJECTED, "[MenuManager] Navigation request rejected (%d).\n", result);
            return -1;
        }
        if (machine) { printf("RACOOL_CLI v=1 type=menu_navigation result=queued state=%d\n", state); return 0; }
        LOC_PRINTF(catalog, LOC_CLI_MENU_NAV_QUEUED, "[MenuManager] Navigation request queued for state %d; use status to inspect worker state.\n",state);
        return 0;
    }
    if (strcmp(command,"btn") == 0 || strcmp(command,"click") == 0) {
        return argc == 2 ? SubmitMenuInput(menu,true,0,catalog,machine) : -1;
    }
    if (strcmp(command,"enc") == 0 || strcmp(command,"fwd") == 0 || strcmp(command,"bwd") == 0) {
        bool explicit_direction = strcmp(command,"enc") == 0;
        int direction = strcmp(command,"bwd") == 0 ? -1 : 1;
        if ((!explicit_direction && argc != 2) ||
            (explicit_direction && (argc < 2 || argc > 3 ||
             (argc == 3 && !ParseMenuNumber(argv[2],INT8_MIN,INT8_MAX,&direction)))) || !direction) {
            LOC_PRINTF(catalog, LOC_CLI_MENU_ENCODER_USAGE, "Usage: menu enc <nonzero_signed_8bit_direction> | menu fwd | menu bwd\n");
            return -1;
        }
        return SubmitMenuInput(menu,false,direction,catalog,machine);
    }
    if (strcmp(command,"test") == 0) {
        if (machine) return 1;
        if (argc != 2 || !menu->base.CheckHealth) return -1;
        int32_t result = menu->base.CheckHealth(&menu->base);
        LOC_PRINTF(catalog, LOC_CLI_MENU_TEST, "[MenuManager] Self-test %s (%ld).\n",Localization_Text(catalog, result == 0 ? LOC_CLI_MENU_PASSED : LOC_CLI_MENU_FAILED),(long)result);
        return result;
    }
    LOC_PRINTF(catalog, LOC_CLI_MENU_USAGE, "Unknown subcommand. Valid: status, nav, btn, click, enc, fwd, bwd, reset, test, auto-exit\n");
    return -1;
}

esp_err_t MenuManager_RegisterConsoleCommands(void) {
  IDebugConsole *console = (IDebugConsole *)ServiceLocator_GetService("DebugConsole");
  if (!console || !console->RegisterCommandWithCategory) return ESP_ERR_INVALID_STATE;
  const esp_console_cmd_t menu_cmd = {
      .command = "menu",
      .help = "MenuManager commands (status, nav, btn, click, fwd, bwd, enc, reset, test, auto-exit)",
      .hint = "<status|nav|btn|click|fwd|bwd|enc|reset|test|auto-exit>",
      .func = &menu_console_cmd_handler,
      .argtable = NULL
  };

  esp_err_t result = console->RegisterCommandWithCategory(console, &menu_cmd, CMD_CAT_HARDWARE);
  if (result != ESP_OK) return result;
  ESP_LOGI(MENU_CMD_TAG, "MenuManager console commands registered successfully");
  return ESP_OK;
}

