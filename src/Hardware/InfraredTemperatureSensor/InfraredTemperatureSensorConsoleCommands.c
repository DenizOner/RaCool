/**
 * @file InfraredTemperatureSensorConsoleCommands.c
 * @brief Debug Console CLI commands for InfraredTemperatureSensor (MLX90614) Module.
 *
 * Command: ir_temp
 *   ir_temp status    -- Complete cached temperature pair and acquisition age
 *   ir_temp trigger   -- TriggerMeasurement
 *   ir_temp stats     -- GetStats
 *   ir_temp reset     -- ResetStats
 *   ir_temp selftest  -- SelfTest
 *   ir_temp lasterr   -- GetLastError
 *   ir_temp poll <ms> -- SetPollingInterval
 */

#include "Hardware/InfraredTemperatureSensor/InfraredTemperatureSensorConsoleCommands.h"
#include "Hardware/InfraredTemperatureSensor/IInfraredTemperatureSensor.h"
#include "Core/ServiceLocator/ServiceLocator.h"
#include "Core/ServiceLocator/ServiceNames.h"
#include "Communication/DebugConsole/IDebugConsole.h"
#include "Communication/DebugConsole/Core/ConsoleColors.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <stdio.h>
#include "Core/Localization/Localization.h"
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <esp_console.h>

static const char *IR_TEMP_CMD_TAG = "IrTempConsole";

static int cmd_ir_temp_handler(int argc, char **argv) {
    const LocalizationCatalog *catalog=Localization_Snapshot();
    if (!argv || argc < 1 || argc > 4) return -1;
    for (int i=0;i<argc;++i) if (!argv[i] || !*argv[i]) return -1;
    bool machine=argc>=2 && !strcmp(argv[argc-1],"--machine");
    if (machine) --argc;
    if (argc>3 || (machine && argc>=2 && !strcmp(argv[1],"raw"))) return -1;
    if (argc == 3 && (strcmp(argv[1], "poll") != 0 || !argv[2])) return -1;
    IInfraredTemperatureSensor *s = InfraredTemperatureSensor_GetModule();
    if (!s) {
        LOC_PRINTF(catalog, LOC_CLI_IR_SENSOR_SERVICE_MISSING, "[IrTemp] Service unavailable.\n");
        return -1;
    }

    if (argc < 2 || strcmp(argv[1], "status") == 0) {
        if ((argc >= 2 || machine) && !s->GetPollingInterval) return -1;
        IRTempSample sample;
        if (!s->ReadSample || s->ReadSample(s, &sample) != ESP_OK) {
            LOC_PRINTF(catalog, LOC_CLI_IR_SENSOR_SAMPLE_MISSING, "[IrTemp] Complete sample unavailable.\n"); return -1;
        }
        float obj = sample.temperatures[0];
        float amb = sample.temperatures[1];
        int64_t now_us = esp_timer_get_time();
        int64_t age_ms=sample.measured_time_us>=0 && now_us>=sample.measured_time_us ? (now_us-sample.measured_time_us)/1000 : -1;
        if (machine) {
            printf("RACOOL_CLI v=1 type=ir_sensor_status object_c=%.9g ambient_c=%.9g measured_time_us=%lld age_ms=%lld polling_ms=%lu\n",
                obj,amb,(long long)sample.measured_time_us,(long long)age_ms,(unsigned long)s->GetPollingInterval(s));
            return 0;
        }
        LOC_PRINTF(catalog, LOC_CLI_IR_SENSOR_TITLE, "\n--- IR Temperature Sensor Status (MLX90614) ---\n");
        LOC_PRINTF(catalog, LOC_CLI_IR_SENSOR_OBJECT, " Object Temp: %.2f C\n",obj);
        LOC_PRINTF(catalog, LOC_CLI_IR_SENSOR_AMBIENT, " Ambient Temp: %.2f C\n",amb);
        if (argc < 2) return 0;
        if (sample.measured_time_us >= 0 && now_us >= sample.measured_time_us)
            LOC_PRINTF(catalog, LOC_CLI_IR_SENSOR_AGE, " Sample Age: %llu ms\n", (unsigned long long)((uint64_t)(now_us - sample.measured_time_us) / 1000ULL));
        else LOC_PRINTF(catalog, LOC_CLI_IR_SENSOR_AGE_MISSING, " Sample Age: unavailable\n");
        LOC_PRINTF(catalog, LOC_CLI_IR_SENSOR_INTERVAL, " Local sampling interval: %lu ms\n", (unsigned long)s->GetPollingInterval(s));
        return 0;
    }

    if (strcmp(argv[1], "trigger") == 0) {
        if (!s->TriggerMeasurement) return -1;
        s->TriggerMeasurement(s);
        if (machine) {printf("RACOOL_CLI v=1 type=ir_sensor_command command=trigger result=requested\n");return 0;}
        LOC_PRINTF(catalog, LOC_CLI_IR_SENSOR_TRIGGER_REQUESTED, "[IrTemp] Measurement requested; inspect statistics for the result.\n");
        return 0;
    }

    if (strcmp(argv[1], "stats") == 0) {
        IRTempStats stats;
        if (s->GetStats && s->GetStats(s, &stats) == ESP_OK) {
            if (machine) {
                printf("RACOOL_CLI v=1 type=ir_sensor_stats reads=%lu pec_errors=%lu i2c_errors=%lu failure_kind=%u failure_error=%ld failure_time_us=%lld object_min_c=%.9g object_max_c=%.9g ambient_min_c=%.9g ambient_max_c=%.9g\n",
                    (unsigned long)stats.total_reads,(unsigned long)stats.pec_errors,(unsigned long)stats.i2c_errors,
                    (unsigned)stats.last_failure_kind,(long)stats.last_failure_error,(long long)stats.last_failure_time_us,
                    stats.min_object_temp,stats.max_object_temp,stats.min_ambient_temp,stats.max_ambient_temp);
                return 0;
            }
            LOC_PRINTF(catalog, LOC_CLI_IR_SENSOR_STATS_TITLE, "\n--- IR Sensor Telemetry Statistics ---\n");
            LOC_PRINTF(catalog, LOC_CLI_IR_SENSOR_READS, " Total Reads   : %lu\n", (unsigned long)stats.total_reads);
            LOC_PRINTF(catalog, LOC_CLI_IR_SENSOR_PEC_ERRORS, " PEC Errors    : %lu\n", (unsigned long)stats.pec_errors);
            LOC_PRINTF(catalog, LOC_CLI_IR_SENSOR_I2C_ERRORS, " I2C Errors    : %lu\n", (unsigned long)stats.i2c_errors);
            if (stats.last_failure_kind == IRTEMP_FAILURE_NONE) {
                LOC_PRINTF(catalog, LOC_CLI_IR_SENSOR_NO_FAILURE, " Last Failure  : none\n");
            } else {
                int64_t now = esp_timer_get_time();
                LOC_PRINTF(catalog, LOC_CLI_IR_SENSOR_FAILURE, " Last Failure  : kind=%u error=%ld time_us=%lld\n",
                       (unsigned)stats.last_failure_kind, (long)stats.last_failure_error,
                       (long long)stats.last_failure_time_us);
                if (now >= stats.last_failure_time_us)
                    LOC_PRINTF(catalog, LOC_CLI_IR_SENSOR_FAILURE_AGE, " Failure Age   : %llu ms\n", (unsigned long long)((now - stats.last_failure_time_us) / 1000));
                else LOC_PRINTF(catalog, LOC_CLI_IR_SENSOR_FAILURE_AGE_MISSING, " Failure Age   : unavailable\n");
            }
            LOC_PRINTF(catalog, LOC_CLI_IR_SENSOR_OBJECT_RANGE, " Obj Temp Range: [%.2f °C - %.2f °C]\n", stats.min_object_temp, stats.max_object_temp);
            LOC_PRINTF(catalog, LOC_CLI_IR_SENSOR_AMBIENT_RANGE, " Amb Temp Range: [%.2f °C - %.2f °C]\n", stats.min_ambient_temp, stats.max_ambient_temp);
            printf(CLI_COLOR_CYAN "-------------------------------------\n" CLI_COLOR_RESET);
            return 0;
        }
        LOC_PRINTF(catalog, LOC_CLI_IR_SENSOR_STATS_FAILED, "[IrTemp] Failed to read statistics.\n");
        return -1;
    }

    if (strcmp(argv[1], "reset") == 0) {
        esp_err_t result=s->ResetStats ? s->ResetStats(s) : ESP_ERR_INVALID_STATE;
        if (machine) {
            printf("RACOOL_CLI v=1 type=ir_sensor_command command=reset result=%s error=%ld\n",result==ESP_OK ? "applied" : "rejected",(long)result);
            return result==ESP_OK ? 0 : -1;
        }
        if (result == ESP_OK) {
            LOC_PRINTF(catalog, LOC_CLI_IR_SENSOR_RESET_APPLIED, "[IrTemp] Telemetry statistics reset.\n");
            return 0;
        }
        LOC_PRINTF(catalog, LOC_CLI_IR_SENSOR_RESET_FAILED, "[IrTemp] Failed to reset statistics.\n");
        return -1;
    }

    if (strcmp(argv[1], "selftest") == 0) {
        if (!s->SelfTest) return -1;
        esp_err_t e = s->SelfTest(s);
        if (machine) {
            printf("RACOOL_CLI v=1 type=ir_sensor_test result=%s error=%ld\n",e==ESP_OK ? "passed" : "failed",(long)e);
            return e==ESP_OK ? 0 : -1;
        }
        if (e == ESP_OK) {
            LOC_PRINTF(catalog, LOC_CLI_IR_SENSOR_SELFTEST_PASSED, "[IrTemp] SelfTest -> PASSED\n");
        } else {
            LOC_PRINTF(catalog, LOC_CLI_IR_SENSOR_SELFTEST_FAILED, "[IrTemp] SelfTest -> FAILED (Error code: %d)\n", e);
        }
        return e == ESP_OK ? 0 : -1;
    }

    if (strcmp(argv[1], "lasterr") == 0) {
        if (!s->GetLastError) return -1;
        esp_err_t e = s->GetLastError(s);
        if (machine) {printf("RACOOL_CLI v=1 type=ir_sensor_error code=%ld\n",(long)e);return 0;}
        LOC_PRINTF(catalog, LOC_CLI_IR_SENSOR_LAST_ERROR, "[IrTemp] Last Error Code -> %d (%s)\n", e, esp_err_to_name(e));
        return 0;
    }

    if (strcmp(argv[1], "poll") == 0 && argc == 3) {
        const char *value = argv[2];
        if (!*value) return -1;
        for (const char *p = value; *p; ++p) if (*p < '0' || *p > '9') return -1;
        errno = 0;
        char *end = NULL;
        unsigned long parsed = strtoul(value, &end, 10);
        if (errno || *end || parsed > UINT32_MAX) return -1;
        uint32_t interval = (uint32_t)parsed;
        esp_err_t result=s->SetPollingInterval ? s->SetPollingInterval(s,interval) : ESP_ERR_INVALID_STATE;
        if (machine) {
            printf("RACOOL_CLI v=1 type=ir_sensor_command command=poll result=%s error=%ld interval_ms=%lu\n",
                result==ESP_OK ? "applied" : "rejected",(long)result,(unsigned long)interval);
            return result==ESP_OK ? 0 : -1;
        }
        if (result == ESP_OK) {
            LOC_PRINTF(catalog, LOC_CLI_IR_SENSOR_POLL_APPLIED, "[IrTemp] Polling interval updated to %lu ms.\n", (unsigned long)interval);
            return 0;
        } else {
            LOC_PRINTF(catalog, LOC_CLI_IR_SENSOR_POLL_FAILED, "[IrTemp] Failed to update polling interval.\n");
            return -1;
        }
    }

    if (strcmp(argv[1], "raw") == 0) {
        if (!s->ReadRawRegisters) return -1;
        IRTempRawRegisters raw;
        esp_err_t result = s->ReadRawRegisters(s, &raw);
        if (result != ESP_OK) {
            LOC_PRINTF(catalog, LOC_CLI_IR_SENSOR_RAW_FAILED, "[IrTemp] Register read failed: %s\n", esp_err_to_name(result));
            return -1;
        }
        const uint8_t *d_raw1 = raw.raw_ir1, *d_raw2 = raw.raw_ir2;
        const uint8_t *d_ta = raw.ambient, *d_tobj1 = raw.object1, *d_tobj2 = raw.object2;
        const uint8_t *d_cfg1 = raw.config1, *d_emiss = raw.emissivity;
        uint16_t raw_ir1 = (d_raw1[1] << 8) | d_raw1[0];
        uint16_t raw_ir2 = (d_raw2[1] << 8) | d_raw2[0];
        uint16_t raw_ta = (d_ta[1] << 8) | d_ta[0];
        uint16_t raw_tobj1 = (d_tobj1[1] << 8) | d_tobj1[0];
        uint16_t raw_tobj2 = (d_tobj2[1] << 8) | d_tobj2[0];
        uint16_t raw_cfg1 = (d_cfg1[1] << 8) | d_cfg1[0];
        uint16_t raw_emiss = (d_emiss[1] << 8) | d_emiss[0];

        LOC_PRINTF(catalog, LOC_CLI_IR_SENSOR_RAW_TITLE, "\n=== MLX90614 Raw Register Dump (PEC not validated) ===\n");
        LOC_PRINTF(catalog, LOC_CLI_IR_SENSOR_RAW_IR1, " RAM 0x00 (Raw IR1): 0x%04X (%d)\n", raw_ir1, (int16_t)raw_ir1);
        LOC_PRINTF(catalog, LOC_CLI_IR_SENSOR_RAW_IR2, " RAM 0x01 (Raw IR2): 0x%04X (%d)\n", raw_ir2, (int16_t)raw_ir2);
        LOC_PRINTF(catalog, LOC_CLI_IR_SENSOR_RAW_AMBIENT, " RAM 0x06 (Ta)     : Low=0x%02X High=0x%02X PEC=0x%02X -> Raw=0x%04X (%.2f °C)\n",
               d_ta[0], d_ta[1], d_ta[2], raw_ta, (float)raw_ta * 0.02f - 273.15f);
        LOC_PRINTF(catalog, LOC_CLI_IR_SENSOR_RAW_OBJECT1, " RAM 0x07 (TOBJ1)  : Low=0x%02X High=0x%02X PEC=0x%02X -> Raw=0x%04X (%.2f °C)\n",
               d_tobj1[0], d_tobj1[1], d_tobj1[2], raw_tobj1, (float)(raw_tobj1 & 0x7FFF) * 0.02f - 273.15f);
        LOC_PRINTF(catalog, LOC_CLI_IR_SENSOR_RAW_OBJECT2, " RAM 0x08 (TOBJ2)  : Low=0x%02X High=0x%02X PEC=0x%02X -> Raw=0x%04X (%.2f °C)\n",
               d_tobj2[0], d_tobj2[1], d_tobj2[2], raw_tobj2, (float)(raw_tobj2 & 0x7FFF) * 0.02f - 273.15f);
        printf(" EEP 0x03 (Config1): 0x%04X\n", raw_cfg1);
        LOC_PRINTF(catalog, LOC_CLI_IR_SENSOR_RAW_EMISSIVITY, " EEP 0x04 (Emiss)  : Low=0x%02X High=0x%02X PEC=0x%02X -> Raw=0x%04X (Factor=%.4f)\n",
               d_emiss[0], d_emiss[1], d_emiss[2], raw_emiss, (float)raw_emiss / 65535.0f);
        printf(CLI_COLOR_CYAN "===================================\n\n" CLI_COLOR_RESET);
        return 0;
    }

    LOC_PRINTF(catalog, LOC_CLI_IR_SENSOR_USAGE, "Usage: ir_temp <status|trigger|stats|reset|selftest|lasterr|poll <ms>|raw>\n");
    return -1;
}

esp_err_t InfraredTemperatureSensor_RegisterConsoleCommands(void) {
    IDebugConsole *console = (IDebugConsole *)ServiceLocator_GetService(SVC_NAME_DEBUG_CONSOLE);
    static const esp_console_cmd_t cmd = {
        .command = "ir_temp",
        .help = "InfraredTemperatureSensor MLX90614 diagnostics and local sampling interval",
        .hint = "<status|trigger|stats|reset|selftest|lasterr|poll <ms>|raw>",
        .func = &cmd_ir_temp_handler,
        .argtable = NULL
    };
    esp_err_t result;
    if (console && console->RegisterCommandWithCategory) {
        result = console->RegisterCommandWithCategory(console, &cmd, CMD_CAT_HARDWARE);
    } else if (console && console->RegisterCommand) {
        result = console->RegisterCommand(console, &cmd);
    } else {
        result = ESP_ERR_INVALID_STATE;
    }
    if (result == ESP_OK) {
        ESP_LOGI(IR_TEMP_CMD_TAG, "InfraredTemperatureSensor console command registered");
    } else {
        ESP_LOGE(IR_TEMP_CMD_TAG, "ir_temp registration failed: %ld", (long)result);
    }
    return result;
}
