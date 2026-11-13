/**
 * @file InternalTemperatureSensorConsoleCommands.c
 * @brief Debug Console CLI commands for InternalTemperatureSensor Module.
 *
 * Command: int_temp
 *   int_temp status          -- GetLastTemperature + ThermalLevel + ThermalTrend
 *   int_temp read            -- ReadTemperature (live hardware read)
 *   int_temp stats           -- GetStats snapshot
 *   int_temp reset_stats     -- ResetStats
 *   int_temp selftest        -- RunSelfTest
 *   int_temp offset <val>    -- SetOffset
 */

#include "Hardware/InternalTemperatureSensor/InternalTemperatureSensorConsoleCommands.h"
#include "Hardware/InternalTemperatureSensor/IInternalTemperatureSensor.h"
#include "Core/ServiceLocator/ServiceLocator.h"
#include "Core/ServiceLocator/ServiceNames.h"
#include "Communication/DebugConsole/IDebugConsole.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <stdio.h>
#include "Core/Localization/Localization.h"
#include "Hardware/InternalTemperatureSensor/InternalTemperatureSensorInput.h"
#include <stdlib.h>
#include <string.h>

#ifndef RACOOL_TEST_NATIVE
#include <esp_console.h>
#endif

static const char *INT_TEMP_CMD_TAG = "IntTempConsole";

static const char *thermal_level_str(const LocalizationCatalog *catalog, ThermalLevel l) {
    switch (l) {
        case THERMAL_LEVEL_NORMAL:    return Localization_Text(catalog, LOC_CLI_CPU_SENSOR_VALUE_NORMAL);
        case THERMAL_LEVEL_WARNING:   return Localization_Text(catalog, LOC_CLI_CPU_SENSOR_VALUE_WARNING);
        case THERMAL_LEVEL_CRITICAL:  return Localization_Text(catalog, LOC_CLI_CPU_SENSOR_VALUE_CRITICAL);
        case THERMAL_LEVEL_EMERGENCY: return Localization_Text(catalog, LOC_CLI_CPU_SENSOR_VALUE_EMERGENCY);
        default: return Localization_Text(catalog, LOC_CLI_CPU_SENSOR_VALUE_UNKNOWN);
    }
}

static const char *thermal_trend_str(const LocalizationCatalog *catalog, ThermalTrend t) {
    switch (t) {
        case THERMAL_TREND_STABLE:     return Localization_Text(catalog, LOC_CLI_CPU_SENSOR_VALUE_STABLE);
        case THERMAL_TREND_RISING:     return Localization_Text(catalog, LOC_CLI_CPU_SENSOR_VALUE_RISING);
        case THERMAL_TREND_FALLING:    return Localization_Text(catalog, LOC_CLI_CPU_SENSOR_VALUE_FALLING);
        case THERMAL_TREND_RAPID_RISE: return Localization_Text(catalog, LOC_CLI_CPU_SENSOR_VALUE_RAPID_RISE);
        default: return Localization_Text(catalog, LOC_CLI_CPU_SENSOR_VALUE_UNKNOWN);
    }
}

static int cmd_int_temp_handler(int argc, char **argv) {
    const LocalizationCatalog *catalog=Localization_Snapshot();
    if (argc < 1 || argc > 4 || !argv) return 1;
    for (int i=0; i<argc; ++i) if (!argv[i] || !*argv[i]) return 1;
    bool machine=argc>=2 && !strcmp(argv[argc-1],"--machine");
    if (machine) --argc;
    if (argc>3) return 1;
    if (argc > 2 && strcmp(argv[1], "offset") != 0) return 1;
    IInternalTemperatureSensor *s = InternalTemperatureSensor_GetModule();
    if (!s) { LOC_PRINTF(catalog, LOC_CLI_CPU_SENSOR_SERVICE_MISSING, "[IntTemp] Service unavailable.\n"); return -1; }

    if (argc < 2 || strcmp(argv[1], "status") == 0) {
        InternalTemperatureStatus status;
        if (!s->GetStatus || s->GetStatus(s,&status)!=0) {LOC_PRINTF(catalog, LOC_CLI_CPU_SENSOR_STATUS_MISSING, "[IntTemp] Status unavailable.\n");return 1;}
        float temp=status.last_temperature,offset=status.offset;
        ThermalLevel level=status.level;ThermalTrend trend=status.trend;
        int64_t now_us = esp_timer_get_time();
        int64_t age_ms=status.sample_valid && now_us>=0 && status.last_sample_time_ms<=(uint64_t)now_us/1000ULL
            ? (int64_t)((uint64_t)now_us/1000ULL-status.last_sample_time_ms) : -1;
        if (machine) {
            printf("RACOOL_CLI v=1 type=cpu_sensor_status valid=%u temperature_c=%.9g offset_c=%.9g level=%u trend=%u state=%u sample_time_ms=%llu age_ms=%lld\n",
                status.sample_valid ? 1u : 0u,temp,offset,(unsigned)level,(unsigned)trend,(unsigned)status.state,
                (unsigned long long)status.last_sample_time_ms,(long long)age_ms);
            return 0;
        }
        LOC_PRINTF(catalog, LOC_CLI_CPU_SENSOR_TITLE, "--- InternalTemperatureSensor Status ---\n");
        if (status.sample_valid) LOC_PRINTF(catalog, LOC_CLI_CPU_SENSOR_TEMPERATURE, " Last Temp  : %.2f C\n", temp);
        else LOC_PRINTF(catalog, LOC_CLI_CPU_SENSOR_TEMPERATURE_MISSING, " Last Temp  : unavailable\n");
        if (argc < 2) return 0;
        if (status.sample_valid && now_us >= 0 &&
            status.last_sample_time_ms <= (uint64_t)now_us / 1000ULL)
            LOC_PRINTF(catalog, LOC_CLI_CPU_SENSOR_AGE, " Sample Age : %llu ms\n", (unsigned long long)((uint64_t)now_us / 1000ULL - status.last_sample_time_ms));
        else LOC_PRINTF(catalog, LOC_CLI_CPU_SENSOR_AGE_MISSING, " Sample Age : unavailable\n");
        LOC_PRINTF(catalog, LOC_CLI_CPU_SENSOR_OFFSET, " Offset     : %.9g C\n", offset);
        LOC_PRINTF(catalog, LOC_CLI_CPU_SENSOR_LEVEL, " Level      : %s\n", thermal_level_str(catalog, level));
        LOC_PRINTF(catalog, LOC_CLI_CPU_SENSOR_TREND, " Trend      : %s\n", thermal_trend_str(catalog, trend));
        printf("----------------------------------------\n");
        return 0;
    }

    if (strcmp(argv[1], "read") == 0) {
        if (!s->ReadTemperature) return 1;
        float t = s->ReadTemperature(s);
        if (!isfinite(t) || t<=-273.0f) {
            if (machine) {
                printf("RACOOL_CLI v=1 type=cpu_sensor_read result=rejected\n");
            } else {
                LOC_PRINTF(catalog, LOC_CLI_CPU_SENSOR_READ_FAILED, "[IntTemp] Live read failed.\n");
            }
            return 1;
        }
        if (machine) {printf("RACOOL_CLI v=1 type=cpu_sensor_read result=read temperature_c=%.9g\n",t);return 0;}
        LOC_PRINTF(catalog, LOC_CLI_CPU_SENSOR_READ, "[IntTemp] Live read: %.2f C\n", t);
        return 0;
    }

    if (strcmp(argv[1], "stats") == 0) {
        ThermalStats st = {0};
        if (s->GetStats && s->GetStats(s, &st) == 0) {
            if (machine) {
                printf("RACOOL_CLI v=1 type=cpu_sensor_stats min_c=%.9g max_c=%.9g avg_c=%.9g rate_c_min=%.9g samples=%lu sample_time_ms=%llu\n",
                    st.min_temp,st.max_temp,st.avg_temp,st.rate_of_change_c_per_min,
                    (unsigned long)st.total_samples,(unsigned long long)st.last_sample_time_ms);
                return 0;
            }
            LOC_PRINTF(catalog, LOC_CLI_CPU_SENSOR_STATS_TITLE, "--- Thermal Stats ---\n");
            LOC_PRINTF(catalog, LOC_CLI_CPU_SENSOR_MINIMUM, " Min     : %.2f C\n", st.min_temp);
            LOC_PRINTF(catalog, LOC_CLI_CPU_SENSOR_MAXIMUM, " Max     : %.2f C\n", st.max_temp);
            LOC_PRINTF(catalog, LOC_CLI_CPU_SENSOR_AVERAGE, " Avg EMA : %.2f C\n", st.avg_temp);
            LOC_PRINTF(catalog, LOC_CLI_CPU_SENSOR_RATE, " Rate    : %.2f C/min\n", st.rate_of_change_c_per_min);
            LOC_PRINTF(catalog, LOC_CLI_CPU_SENSOR_SAMPLES, " Samples : %lu\n", (unsigned long)st.total_samples);
            printf("---------------------\n");
        } else {
            LOC_PRINTF(catalog, LOC_CLI_CPU_SENSOR_STATS_FAILED, "[IntTemp] GetStats failed.\n");
            return 1;
        }
        return 0;
    }

    if (strcmp(argv[1], "reset_stats") == 0) {
        if (!s->ResetStats) return 1;
        int32_t r = s->ResetStats(s);
        if (machine) {
            printf("RACOOL_CLI v=1 type=cpu_sensor_command command=reset_stats result=%s error=%ld\n",r==0 ? "applied" : "rejected",(long)r);
            return r==0 ? 0 : 1;
        }
        LOC_PRINTF(catalog, LOC_CLI_CPU_SENSOR_RESET_RESULT, "[IntTemp] ResetStats -> %ld\n", (long)r);
        return r == 0 ? 0 : 1;
    }

    if (strcmp(argv[1], "selftest") == 0) {
        if (!s->RunSelfTest) return 1;
        int32_t r = s->RunSelfTest(s);
        if (machine) {
            printf("RACOOL_CLI v=1 type=cpu_sensor_command command=selftest result=%s error=%ld\n",r==0 ? "applied" : "rejected",(long)r);
            return r==0 ? 0 : 1;
        }
        LOC_PRINTF(catalog, LOC_CLI_CPU_SENSOR_SELFTEST_RESULT, "[IntTemp] SelfTest -> %s (%ld)\n", r == 0 ? Localization_Text(catalog, LOC_CLI_CPU_SENSOR_VALUE_PASSED) : Localization_Text(catalog, LOC_CLI_CPU_SENSOR_VALUE_FAILED), (long)r);
        return r == 0 ? 0 : 1;
    }

    if (strcmp(argv[1], "offset") == 0) {
        float off;
        if (argc != 3 || !InternalTemp_ParseFloat(argv[2], &off)) { LOC_PRINTF(catalog, LOC_CLI_CPU_SENSOR_OFFSET_USAGE, "Usage: int_temp offset <finite_value_in_C>\n"); return 1; }
        if (!s->SetOffset) return 1;
        int32_t r = s->SetOffset(s, off);
        if (machine) {
            printf("RACOOL_CLI v=1 type=cpu_sensor_command command=offset result=%s error=%ld\n",r==0 ? "applied" : "rejected",(long)r);
            return r==0 ? 0 : 1;
        }
        LOC_PRINTF(catalog, LOC_CLI_CPU_SENSOR_OFFSET_RESULT, "[IntTemp] SetOffset(%.9g) -> %ld\n", off, (long)r);
        return r == 0 ? 0 : 1;
    }

    LOC_PRINTF(catalog, LOC_CLI_CPU_SENSOR_USAGE, "Usage: int_temp <status|read|stats|reset_stats|selftest|offset>\n");
    return -1;
}

esp_err_t InternalTemperatureSensor_RegisterConsoleCommands(void) {
    IDebugConsole *console = (IDebugConsole *)ServiceLocator_GetService(SVC_NAME_DEBUG_CONSOLE);
    if (!console || !console->RegisterCommandWithCategory) return ESP_ERR_INVALID_STATE;
    const esp_console_cmd_t cmd = {
        .command  = "int_temp",
        .help     = "InternalTemperatureSensor (status, read, stats, reset_stats, selftest, offset)",
        .hint     = "<status|read|stats|reset_stats|selftest|offset>",
        .func     = &cmd_int_temp_handler,
        .argtable = NULL
    };
    esp_err_t result = console->RegisterCommandWithCategory(console, &cmd, CMD_CAT_HARDWARE);
    if (result != ESP_OK) return result;
    ESP_LOGI(INT_TEMP_CMD_TAG, "InternalTemperatureSensor console commands registered ('int_temp')");
    return ESP_OK;
}
