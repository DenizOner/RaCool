#include "Core/Localization/Localization.h"
/**
 * @file OLEDConsoleCommands.c
 * @brief Debug Console CLI commands for OLED Module.
 *
 * Command: oled
 *   oled status              -- Service state
 *   oled on                  -- SetPower(true)
 *   oled off                 -- SetPower(false)
 *   oled contrast <0-255>    -- SetContrast
 *   oled invert <0|1>        -- InvertDisplay
 */

#include "Hardware/OLED/OLEDConsoleCommands.h"
#include "Hardware/OLED/IOLED.h"
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

static const char *OLED_CMD_TAG = "OLEDConsole";

static bool parse_byte(const char *text, unsigned maximum, uint8_t *value) {
    if (!text || !*text) return false;
    unsigned parsed = 0;
    for (const char *p = text; *p; ++p) {
        if (*p < '0' || *p > '9') return false;
        parsed = parsed * 10U + (unsigned)(*p - '0');
        if (parsed > maximum) return false;
    }
    *value = (uint8_t)parsed;
    return true;
}

static int cmd_oled_handler(int argc, char **argv) {
    const LocalizationCatalog *catalog = Localization_Snapshot();
    if (argc < 1 || argc > 3 || !argv) return 1;
    for (int i = 0; i < argc; ++i) if (!argv[i] || !*argv[i]) return 1;
    IOLED *oled = OLED_GetModule();
    if (!oled) { LOC_PRINTF(catalog, LOC_CLI_OLED_UNAVAILABLE, "[OLED] Service unavailable.\n"); return -1; }

    if (argc==3 && !strcmp(argv[1],"frame") && !strcmp(argv[2],"--machine")) {
        OLEDFrameSnapshot *frame=malloc(sizeof(*frame));
        if (!frame) return 1;
        esp_err_t error=oled->CopyFrame ? oled->CopyFrame(oled,frame) : ESP_ERR_INVALID_STATE;
        if (error!=ESP_OK) {
            free(frame);
            printf("RACOOL_CLI v=1 type=oled_frame_error error=%d\n",error);
            return 1;
        }
        uint32_t hash=2166136261U;
        printf("RACOOL_CLI v=1 type=oled_frame_begin seq=%lu t_ms=%lu width=%u height=%u bytes=%u powered=%u inverted=%u flipped=%u\n",
            (unsigned long)frame->sequence,(unsigned long)frame->timestamp_ms,OLED_WIDTH,OLED_HEIGHT,
            (unsigned)sizeof(frame->pixels),frame->powered ? 1U : 0U,frame->inverted ? 1U : 0U,frame->flipped ? 1U : 0U);
        for (unsigned offset=0;offset<sizeof(frame->pixels);offset+=64) {
            char hex[129];
            for (unsigned i=0;i<64;++i) {
                uint8_t value=frame->pixels[offset+i];
                snprintf(hex+i*2,3,"%02x",value);
                hash=(hash^value)*16777619U;
            }
            printf("RACOOL_CLI v=1 type=oled_frame_data seq=%lu offset=%u hex=%s\n",(unsigned long)frame->sequence,offset,hex);
        }
        printf("RACOOL_CLI v=1 type=oled_frame_end seq=%lu fnv=%08lx\n",(unsigned long)frame->sequence,(unsigned long)hash);
        free(frame);
        return 0;
    }

    if (argc == 1 || (argc == 2 && strcmp(argv[1], "status") == 0)) {
        if (!oled->base.GetState) return 1;
        ServiceState st = oled->base.GetState((IService *)oled);
        LOC_PRINTF(catalog, LOC_CLI_OLED_TITLE, "--- OLED Status ---\n");
        LOC_PRINTF(catalog, LOC_CLI_OLED_STATE, " State: %d\n", (int)st);
        printf("-------------------\n");
        return 0;
    }

    if (argc == 2 && strcmp(argv[1], "on") == 0) {
        if (!oled->SetPower || oled->SetPower(oled, true) != ESP_OK) { LOC_PRINTF(catalog, LOC_CLI_OLED_ON_FAILED, "[OLED] Power ON failed\n"); return 1; }
        LOC_PRINTF(catalog, LOC_CLI_OLED_ON, "[OLED] Power ON\n");
        return 0;
    }

    if (argc == 2 && strcmp(argv[1], "off") == 0) {
        if (!oled->SetPower || oled->SetPower(oled, false) != ESP_OK) { LOC_PRINTF(catalog, LOC_CLI_OLED_OFF_FAILED, "[OLED] Power OFF failed\n"); return 1; }
        LOC_PRINTF(catalog, LOC_CLI_OLED_OFF, "[OLED] Power OFF\n");
        return 0;
    }

    if (strcmp(argv[1], "contrast") == 0) {
        uint8_t c;
        if (argc != 3 || !parse_byte(argv[2], 255, &c)) { LOC_PRINTF(catalog, LOC_CLI_OLED_CONTRAST_USAGE, "Usage: oled contrast <0-255>\n"); return 1; }
        if (!oled->SetContrast || oled->SetContrast(oled, c) != ESP_OK) { LOC_PRINTF(catalog, LOC_CLI_OLED_CONTRAST_FAILED, "[OLED] Contrast failed\n"); return 1; }
        LOC_PRINTF(catalog, LOC_CLI_OLED_CONTRAST, "[OLED] Contrast set to %d\n", c);
        return 0;
    }

    if (strcmp(argv[1], "invert") == 0) {
        uint8_t inv;
        if (argc != 3 || !parse_byte(argv[2], 1, &inv)) { LOC_PRINTF(catalog, LOC_CLI_OLED_INVERT_USAGE, "Usage: oled invert <0|1>\n"); return 1; }
        if (!oled->InvertDisplay || oled->InvertDisplay(oled, inv) != ESP_OK) { LOC_PRINTF(catalog, LOC_CLI_OLED_INVERT_FAILED, "[OLED] Invert failed\n"); return 1; }
        LOC_PRINTF(catalog, LOC_CLI_OLED_INVERT, "[OLED] Invert: %s\n", Localization_Text(catalog, inv ? LOC_CLI_OLED_ENABLED : LOC_CLI_OLED_DISABLED));
        return 0;
    }

    LOC_PRINTF(catalog, LOC_CLI_OLED_USAGE, "Usage: oled <status|on|off|contrast|invert>\n");
    return -1;
}

esp_err_t OLED_RegisterConsoleCommands(void) {
    IDebugConsole *console = (IDebugConsole *)ServiceLocator_GetService(SVC_NAME_DEBUG_CONSOLE);
    if (!console || !console->RegisterCommandWithCategory) return ESP_ERR_INVALID_STATE;
    const esp_console_cmd_t cmd = {
        .command  = "oled",
        .help     = "OLED display control (status, on, off, contrast, invert)",
        .hint     = "<status|on|off|contrast|invert>",
        .func     = &cmd_oled_handler,
        .argtable = NULL
    };
    esp_err_t result = console->RegisterCommandWithCategory(console, &cmd, CMD_CAT_HARDWARE);
    if (result != ESP_OK) return result;
    ESP_LOGI(OLED_CMD_TAG, "OLED console commands registered ('oled')");
    return ESP_OK;
}
