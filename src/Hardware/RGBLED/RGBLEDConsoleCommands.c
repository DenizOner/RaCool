#include "Hardware/LEDAnimations/LEDAnimationLabels.h"
#include "Core/Localization/Localization.h"
/**
 * @file RGBLEDConsoleCommands.c
 * @brief Debug Console CLI commands for RGBLED Module.
 *
 * Command: rgb
 *   rgb status              -- GetLEDState snapshot
 *   rgb set <R> <G> <B>     -- SetColorRGB
 *   rgb brightness <0-255>  -- Persist brightness setting
 *   rgb anim <type>         -- StartAnimation (IDs follow RGBLEDAnimType)
 *   rgb off                 -- TurnOff
 */

#include "Hardware/RGBLED/RGBLEDConsoleCommands.h"
#include "Hardware/RGBLED/IRGBLED.h"
#include "Hardware/RGBLED/RGBLEDTypes.h"
#include "Hardware/LEDAnimations/LEDAnimationCatalog.h"
#include "Core/ServiceLocator/ServiceLocator.h"
#include "Core/ServiceLocator/ServiceNames.h"
#include "Communication/DebugConsole/IDebugConsole.h"
#include "esp_log.h"
#include <stdio.h>
#include "Hardware/RGBLED/RGBLEDCLIParsing.h"
#include <string.h>

#ifndef RACOOL_TEST_NATIVE
#include <esp_console.h>
#endif



static int cmd_rgb_handler(int argc, char **argv) {
    const LocalizationCatalog *catalog = Localization_Snapshot();
    if (!RGBLED_ArgumentsValid(argc, argv, 6)) return -1;
    IRGBLED *rgb = GetRGBLEDService();
    if (!rgb) { LOC_PRINTF(catalog, LOC_CLI_RGB_RGBLED_SERVICE_UNAVAILABLE, "[RGBLED] Service unavailable.\n"); return -1; }

    if (argc == 3 && strcmp(argv[1], "status") == 0 && strcmp(argv[2], "--machine") == 0) {
        RGBLEDState state = {0};
        if (!rgb->GetLEDState || rgb->GetLEDState(rgb, &state) != ESP_OK) return -1;
        RGBLED_PrintMachineState(&state);
        return 0;
    }

    if (argc == 1 || (argc == 2 && strcmp(argv[1], "status") == 0)) {
        if (!rgb->GetLEDState) return -1;
        RGBLEDState state = {0};
        if (rgb->GetLEDState(rgb, &state) == ESP_OK) {
            LOC_PRINTF(catalog, LOC_CLI_RGB_RGBLED_STATUS, "--- RGBLED Status ---\n");
            LOC_PRINTF(catalog, LOC_CLI_RGB_CURRENT_RGB_R_D_G_D_B_D, " Current RGB : R:%d G:%d B:%d\n",
                   state.currentColor.r, state.currentColor.g, state.currentColor.b);
            LOC_PRINTF(catalog, LOC_CLI_RGB_CURRENT_HSV_H_D_S_D_V_D, " Current HSV : H:%d S:%d V:%d\n",
                   state.currentHSV.h, state.currentHSV.s, state.currentHSV.v);
            LOC_PRINTF(catalog, LOC_CLI_RGB_ANIMATION_D, " Animation   : %d\n", (int)state.activeAnim);
            printf("---------------------\n");
        } else {
            LOC_PRINTF(catalog, LOC_CLI_RGB_RGBLED_GETLEDSTATE_FAILED, "[RGBLED] GetLEDState failed.\n");
            return -1;
        }
        return 0;
    }

    if (argc < 2) return -1;
    if (strcmp(argv[1], "list") == 0 && argc == 2) {
        size_t count;
        const LEDAnimationCatalogEntry *entries = LEDAnimationCatalog_Entries(&count);
        for (size_t index = 0; index < count; ++index)
            printf("  [%2u] %s\n", entries[index].external_id, LEDAnimationLabel_Localized(entries[index].external_id, false, catalog));
        return 0;
    }
    if (strcmp(argv[1], "set") == 0) {
        if (!rgb->SetColorRGB) return -1;
        uint32_t r, g, b, duration = 10000U;
        if ((argc != 5 && argc != 6) ||
            !RGBLED_ParseBoundedDecimal(argv[2], 255, &r) ||
            !RGBLED_ParseBoundedDecimal(argv[3], 255, &g) ||
            !RGBLED_ParseBoundedDecimal(argv[4], 255, &b) ||
            (argc == 6 && !RGBLED_ParseBoundedDecimal(argv[5], UINT32_MAX, &duration))) {
            LOC_PRINTF(catalog, LOC_CLI_RGB_USAGE_RGB_SET_R_0_255_G_0_255_B_0_255_DURATION_MS, "Usage: rgb set <R:0-255> <G:0-255> <B:0-255> [duration_ms]\n"); return -1;
        }
        RGBColor color = {.r=(uint8_t)r, .g=(uint8_t)g, .b=(uint8_t)b};
        if (duration > 0 && !rgb->StartAnimation) return -1;
        esp_err_t result = rgb->SetColorRGB(rgb, color);
        if (result != ESP_OK) { LOC_PRINTF(catalog, LOC_CLI_RGB_RGBLED_COLOR_COMMAND_REJECTED_D, "[RGBLED] Color command rejected: %d\n", (int)result); return -1; }
        if (duration > 0) {
            result = rgb->StartAnimation(rgb, RGBLED_ANIM_ALERT, RGBLED_PRIO_HIGH, duration);
            if (result != ESP_OK) {
                LOC_PRINTF(catalog, LOC_CLI_RGB_RGBLED_COLOR_QUEUED_BUT_TIMED_ANIMATION_REJECTED_D, "[RGBLED] Color queued, but timed animation rejected: %d\n", (int)result);
                return -1;
            }
        }
        LOC_PRINTF(catalog, LOC_CLI_RGB_RGBLED_COMMAND_QUEUED_WORKER_APPLICATION_IS_ASYNCHRONOUS, "[RGBLED] Command queued; worker application is asynchronous.\n");
        return 0;
    }
    if (strcmp(argv[1], "brightness") == 0) {
        uint32_t brightness;
        if (argc != 3 || !RGBLED_ParseBoundedDecimal(argv[2], 255, &brightness)) {
            LOC_PRINTF(catalog, LOC_CLI_RGB_USAGE_RGB_BRIGHTNESS_0_255, "Usage: rgb brightness <0-255>\n"); return -1;
        }
        esp_err_t result = RGBLED_SaveBrightnessText(argv[2]);
        LOC_PRINTF(catalog, LOC_CLI_RGB_RGBLED_BRIGHTNESS_QUEUE_RESULT_D, "[RGBLED] Brightness persistence result: %d; worker application is asynchronous.\n", (int)result);
        return result == ESP_OK ? 0 : -1;
    }
    if (strcmp(argv[1], "anim") == 0) {
        if (!rgb->StartAnimation) return -1;
        uint32_t type, duration = 10000U;
        if ((argc != 3 && argc != 4) ||
            !RGBLED_ParseBoundedDecimal(argv[2], RGBLED_ANIM_COUNT - 1, &type) ||
            (argc == 4 && !RGBLED_ParseBoundedDecimal(argv[3], UINT32_MAX, &duration))) {
            LOC_PRINTF(catalog, LOC_CLI_RGB_USAGE_RGB_ANIM_TYPE_DURATION_MS, "Usage: rgb anim <type> [duration_ms]\n"); return -1;
        }
        esp_err_t result = rgb->StartAnimation(rgb, (RGBLEDAnimType)type, RGBLED_PRIO_HIGH, duration);
        LOC_PRINTF(catalog, LOC_CLI_RGB_RGBLED_ANIMATION_QUEUE_RESULT_D, "[RGBLED] Animation queue result: %d\n", (int)result);
        return result == ESP_OK ? 0 : -1;
    }
    if (strcmp(argv[1], "off") == 0 && argc == 2) {
        if (!rgb->TurnOff) return -1;
        esp_err_t result = rgb->TurnOff(rgb);
        LOC_PRINTF(catalog, LOC_CLI_RGB_RGBLED_OFF_QUEUE_RESULT_D, "[RGBLED] Off queue result: %d\n", (int)result);
        return result == ESP_OK ? 0 : -1;
    }

    LOC_PRINTF(catalog, LOC_CLI_RGB_USAGE_RGB_STATUS_LIST_SET_BRIGHTNESS_ANIM_OFF, "Usage: rgb <status|list|set|brightness|anim|off>\n");
    return -1;
}

esp_err_t RGBLED_RegisterConsoleCommands(void) {
    IDebugConsole *console = (IDebugConsole *)ServiceLocator_GetService(SVC_NAME_DEBUG_CONSOLE);
    if (!console || !console->RegisterCommandWithCategory) return ESP_ERR_INVALID_STATE;
    const esp_console_cmd_t cmd = {
        .command  = "rgb",
        .help     = "RGBLED control (status, list, set R G B, brightness, anim, off)",
        .hint     = "<status|list|set|brightness|anim|off>",
        .func     = &cmd_rgb_handler,
        .argtable = NULL
    };
    esp_err_t result = console->RegisterCommandWithCategory(console, &cmd, CMD_CAT_HARDWARE);
    if (result != ESP_OK) return result;
    ESP_LOGI("RGBConsole", "RGBLED console commands registered ('rgb')");
    return ESP_OK;
}
