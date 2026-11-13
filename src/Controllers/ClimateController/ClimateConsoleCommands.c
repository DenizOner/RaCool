#include "Controllers/ClimateController/FanCommands.h"
/**
 * @file ClimateConsoleCommands.c
 * @brief Debug console command implementations for the ClimateController module.
 *
 * Provides the CLIMATE command family (and short alias CC) for runtime
 * inspection and control of the ClimateController from the serial debug
 * console (via IDebugConsole).
 *
 * All commands are registered under the CMD_CAT_HARDWARE category and are
 * accessible as sub-commands of the top-level CLIMATE command.
 *
 * Available sub-commands:
 *   STATUS             — Temperatures, relay state, mode, setpoints, stale flag.
 *   INFO               — Active configuration parameters.
 *   HIST               — Temperature history ring buffer.
 *   SET HIGH|LOW <val> — Relay setpoint configuration.
 *   MODE OBJECT_SENSITIVE|AMBIENT_DELTA|ON|OFF   — Fan mode control.
 *   CAL OBJ|AMB <val>  — Temperature calibration offset.
 *   SAVE               — Persist climate settings to NVS.
 *   CPU                — CPU thermal thresholds and protection status.
 *   RESET              — Restore climate defaults.
 *   TEST               — Run ClimateController self-test.
 */

#include "Controllers/ClimateController/ClimateConsoleCommands.h"
#include "Controllers/ClimateController/IClimateController.h"
#include "Communication/DebugConsole/IDebugConsole.h"
#include "Communication/DebugConsole/Core/ConsoleColors.h"
#include "Core/EventBus/IEventBus.h"
#include "Core/ServiceLocator/ServiceLocator.h"
#include "Core/ServiceLocator/ServiceNames.h"
#include "Management/ConfigurationManager/IConfigurationManager.h"
#include "Management/ConfigurationManager/SystemConfigs.h"
#include <stdio.h>
#include "Core/Localization/Localization.h"
#include <errno.h>
#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <esp_log.h>

static const char *TAG_CLI = "ClimateCmd";

/* ─────────────────────────────────────────────────────────────────────────────
 * Internal Helpers
 * ───────────────────────────────────────────────────────────────────────────── */

/**
 * @brief Retrieves and validates the IClimateController singleton.
 *
 * @return Pointer to the running IClimateController, or NULL if unavailable.
 */
static IClimateController *get_climate_ctrl(void) {
  const LocalizationCatalog *catalog = Localization_Snapshot();
  IClimateController *cc = (IClimateController *)ServiceLocator_GetService(
      SVC_NAME_CLIMATE_CONTROLLER);
  if (!cc) {
    LOC_PRINTF(catalog, LOC_CLI_CLIMATE_SERVICE_MISSING, "ClimateController: service not found\n");
  }
  return cc;
}

/**
 * @brief Retrieves and validates the IConfigurationManager singleton.
 *
 * @param output Caller-owned storage for one consistent snapshot.
 * @return True on success; false when the service or snapshot is unavailable.
 */
static bool get_settings(SystemSettings *output) {
  const LocalizationCatalog *catalog = Localization_Snapshot();
  IConfigurationManager *cfg = ConfigurationManager_GetInstance();
  if (!cfg || !cfg->GetSnapshot || cfg->GetSnapshot(cfg, output) != ESP_OK) {
    LOC_PRINTF(catalog, LOC_CLI_CLIMATE_SNAPSHOT_MISSING, "Configuration snapshot unavailable\n");
    return false;
  }
  return true;
}

static int update_fields(const char *block, const ConfigurationFieldUpdate *fields, size_t count) {
  const LocalizationCatalog *catalog = Localization_Snapshot();
  IConfigurationManager *cfg = ConfigurationManager_GetInstance();
  esp_err_t result = cfg && cfg->UpdateFields ? cfg->UpdateFields(cfg, block, fields, count, false) : ESP_FAIL;
  if (result != ESP_OK) {
    LOC_PRINTF(catalog, LOC_CLI_CLIMATE_UPDATE_FAILED, "Configuration update failed (0x%x)\n", (unsigned)result);
    return 1;
  }
  return 0;
}

static bool parse_float(const char *text, float *value) {
  char *end = NULL;
  errno = 0;
  *value = strtof(text, &end);
  return end != text && !*end && !errno && isfinite(*value);
}

/**
 * @brief Returns a human-readable string for a ClimateFanMode value.
 *
 * @param mode ClimateFanMode enum value.
 * @return     Constant string for either sensing strategy or manual selection.
 */
static const char *fan_mode_str(const LocalizationCatalog *catalog, ClimateFanMode mode) {
  switch (mode) {
    case CLIMATE_FAN_OBJECT_SENSITIVE: return Localization_Text(catalog, LOC_CLI_CLIMATE_VALUE_OBJECT_MODE);
    case CLIMATE_FAN_AMBIENT_DELTA: return Localization_Text(catalog, LOC_CLI_CLIMATE_VALUE_AMBIENT_MODE);
    case CLIMATE_FAN_MANUAL_ON:  return Localization_Text(catalog, LOC_CLI_CLIMATE_VALUE_MANUAL_ON);
    case CLIMATE_FAN_MANUAL_OFF: return Localization_Text(catalog, LOC_CLI_CLIMATE_VALUE_MANUAL_OFF);
    default:                     return Localization_Text(catalog, LOC_CLI_CLIMATE_VALUE_UNKNOWN);
  }
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Sub-command Handlers
 * ───────────────────────────────────────────────────────────────────────────── */

/**
 * @brief CLIMATE STATUS — display current climate controller state.
 *
 * Prints all temperature readings, relay state, fan mode, setpoints,
 * calibration offsets, stale sensor flag, and system enable status.
 */
static int do_climate_status(int argc, char **argv) {
  const LocalizationCatalog *catalog = Localization_Snapshot();
  bool machine=argc==2 && argv && argv[1] && !strcmp(argv[1],"--machine");
  if (argc != 1 && !machine) return 1;

  IClimateController *cc = get_climate_ctrl();
  SystemSettings snapshot;
  const SystemSettings *s = get_settings(&snapshot) ? &snapshot : NULL;
  if (!cc || !s || !cc->GetCurrentObjectTemperature || !cc->GetCurrentAmbientTemperature ||
      !cc->GetCurrentCpuTemperature || !cc->GetRelayState || !cc->GetLastSensorReadTime ||
      !cc->IsSensorStale || !cc->GetRelayDebounceState || !cc->GetFanMode) return 1;

  if (!machine) LOC_PRINTF(catalog, LOC_CLI_CLIMATE_STATUS_TITLE, "\n=== ClimateController Status ===\n");

  /* Temperature readings. */
  if (cc) {
    float    obj      = cc->GetCurrentObjectTemperature(cc);
    float    amb      = cc->GetCurrentAmbientTemperature(cc);
    float    cpu      = cc->GetCurrentCpuTemperature(cc);
    bool     relay    = cc->GetRelayState(cc);
    uint32_t lastRead = cc->GetLastSensorReadTime(cc);
    bool     stale    = cc->IsSensorStale(cc);
    bool     debounce = cc->GetRelayDebounceState(cc);
    ClimateFanMode mode = cc->GetFanMode(cc);
    if (machine) {
      printf("RACOOL_CLI v=1 type=climate_status mode=%d on=%u stale=%u debouncing=%u ir_tick=%lu object_c=%.2f ambient_c=%.2f cpu_c=%.2f open_c=%.2f close_c=%.2f object_cal_c=%.2f ambient_cal_c=%.2f enabled=%u manual=%u manual_state=%u strategy=%u object_source=%u ambient_source=%u delta_on=%d delta_off=%d\n",
          (int)mode,relay ? 1u : 0u,stale ? 1u : 0u,debounce ? 1u : 0u,
          (unsigned long)lastRead,obj,amb,cpu,s->control.relay_open_temp,s->control.relay_close_temp,
          s->calibration.ir_object_cal,s->calibration.ir_ambient_cal,s->system.system_enabled ? 1u : 0u,
          s->control.fan_manual_mode ? 1u : 0u,s->control.fan_manual_state ? 1u : 0u,
          (unsigned)s->control.fan_strategy,(unsigned)s->control.object_temperature_source,
          (unsigned)s->control.ambient_temperature_source,(int)s->control.fan_delta_on_decic,(int)s->control.fan_delta_off_decic);
      return 0;
    }

    bool fahrenheit = s && s->system.temp_unit == 1;
    const char *unit = fahrenheit ? "F" : "C";
    if (fahrenheit) { obj = obj * 1.8f + 32.0f; amb = amb * 1.8f + 32.0f; cpu = cpu * 1.8f + 32.0f; }
    LOC_PRINTF(catalog, LOC_CLI_CLIMATE_OBJECT_TEMPERATURE, "Radiator Temp: %.2f °%s\n", obj, unit);
    LOC_PRINTF(catalog, LOC_CLI_CLIMATE_AMBIENT_TEMPERATURE, "Ambient Temp : %.2f °%s\n", amb, unit);
    LOC_PRINTF(catalog, LOC_CLI_CLIMATE_CPU_TEMPERATURE, "CPU Temp     : %.2f °%s\n", cpu, unit);
    LOC_PRINTF(catalog, LOC_CLI_CLIMATE_FAN_STATE, "Relay (Fan)  : %s\n",
           relay ? Localization_Text(catalog, LOC_CLI_CLIMATE_VALUE_ON)
                 : Localization_Text(catalog, LOC_CLI_CLIMATE_VALUE_OFF));
    LOC_PRINTF(catalog, LOC_CLI_CLIMATE_LAST_IR_READ, "Last IR Read : tick %lu\n", (unsigned long)lastRead);
    LOC_PRINTF(catalog, LOC_CLI_CLIMATE_SENSOR_STALE, "Sensor Stale : %s\n",
           stale ? Localization_Text(catalog, LOC_CLI_CLIMATE_VALUE_YES)
                 : Localization_Text(catalog, LOC_CLI_CLIMATE_VALUE_NO));
    LOC_PRINTF(catalog, LOC_CLI_CLIMATE_DEBOUNCING, "Debouncing   : %s\n",
           debounce ? Localization_Text(catalog, LOC_CLI_CLIMATE_VALUE_YES) : Localization_Text(catalog, LOC_CLI_CLIMATE_VALUE_NO));
    LOC_PRINTF(catalog, LOC_CLI_CLIMATE_FAN_MODE, "Fan Mode     : %s\n",
           fan_mode_str(catalog, mode));
  } else {
    LOC_PRINTF(catalog, LOC_CLI_CLIMATE_UNAVAILABLE, "Temperatures : ClimateController unavailable\n");
  }

  /* Settings. */
  if (s) {
    LOC_PRINTF(catalog, LOC_CLI_CLIMATE_OPEN_THRESHOLD, "Open Setpt   : %.2f °C\n", s->control.relay_open_temp);
    LOC_PRINTF(catalog, LOC_CLI_CLIMATE_CLOSE_THRESHOLD, "Close Setpt  : %.2f °C\n", s->control.relay_close_temp);
    LOC_PRINTF(catalog, LOC_CLI_CLIMATE_OBJECT_CALIBRATION, "Radiator Cal : %.2f °C\n", s->calibration.ir_object_cal);
    LOC_PRINTF(catalog, LOC_CLI_CLIMATE_AMBIENT_CALIBRATION, "Amb Cal      : %.2f °C\n", s->calibration.ir_ambient_cal);
    LOC_PRINTF(catalog, LOC_CLI_CLIMATE_SYSTEM, "System       : %s\n",
           s->system.system_enabled
               ? Localization_Text(catalog, LOC_CLI_CLIMATE_VALUE_ENABLED)
               : Localization_Text(catalog, LOC_CLI_CLIMATE_VALUE_DISABLED));
  }

  printf(CLI_COLOR_CYAN "================================\n" CLI_COLOR_RESET);
  return 0;
}

/**
 * @brief CLIMATE INFO — display active configuration parameters.
 *
 * Prints the runtime configuration snapshot: task parameters, debounce,
 * min-switch, watchdog timeout, stale timeout, history capacity,
 * hysteresis dead-band, and feature flags.
 */
static int do_climate_info(int argc, char **argv) {
  const LocalizationCatalog *catalog = Localization_Snapshot();
  bool machine=argc==2 && argv && argv[1] && !strcmp(argv[1],"--machine");
  if (argc != 1 && !machine) return 1;

  IClimateController *cc = get_climate_ctrl();
  if (!cc || !cc->GetConfig) return 1;

  ClimateControllerConfig cfg = cc->GetConfig(cc);
  if (machine) {
    printf("RACOOL_CLI v=1 type=climate_info stale_timeout_ms=%lu cpu_protection=%u\n",
        (unsigned long)cfg.sensorStaleTimeoutMs,cfg.cpuTempProtectionEnabled ? 1u : 0u);
    return 0;
  }

  LOC_PRINTF(catalog, LOC_CLI_CLIMATE_CONFIG_TITLE, "\n=== ClimateController Config ===\n");
  LOC_PRINTF(catalog, LOC_CLI_CLIMATE_TASK_STACK, "Task Stack Size   : %lu bytes\n",   (unsigned long)cfg.taskStackSize);
  LOC_PRINTF(catalog, LOC_CLI_CLIMATE_TASK_PRIORITY, "Task Priority     : %u\n",           (unsigned)cfg.taskPriority);
  LOC_PRINTF(catalog, LOC_CLI_CLIMATE_LOOP_DELAY, "Main Loop Delay   : %lu ms\n",       (unsigned long)cfg.mainLoopDelayMs);
  LOC_PRINTF(catalog, LOC_CLI_CLIMATE_FAN_DEBOUNCE, "Relay Debounce    : %lu ms\n",       (unsigned long)cfg.relayDebounceMs);
  LOC_PRINTF(catalog, LOC_CLI_CLIMATE_FAN_MIN_SWITCH, "Relay Min Switch  : %lu ms\n",       (unsigned long)cfg.relayMinSwitchMs);
  LOC_PRINTF(catalog, LOC_CLI_CLIMATE_WATCHDOG_TIMEOUT, "Watchdog Timeout  : %lu ms\n",       (unsigned long)cfg.watchdogTimeoutMs);
  LOC_PRINTF(catalog, LOC_CLI_CLIMATE_STALE_TIMEOUT, "Sensor Stale TO   : %lu ms%s\n",
         (unsigned long)cfg.sensorStaleTimeoutMs,
         cfg.sensorStaleTimeoutMs == 0 ? Localization_Text(catalog, LOC_CLI_CLIMATE_VALUE_DISABLED_SUFFIX) : "");
  LOC_PRINTF(catalog, LOC_CLI_CLIMATE_HISTORY_CAPACITY, "History Buf Size  : %u samples\n",   (unsigned)cfg.historyBufferSize);
  LOC_PRINTF(catalog, LOC_CLI_CLIMATE_DEADBAND, "Hysteresis DB     : %.2f °C%s\n",
         cfg.hysteresisDeadband,
         cfg.hysteresisDeadband == 0.0f ? Localization_Text(catalog, LOC_CLI_CLIMATE_VALUE_DISABLED_SUFFIX) : "");
  LOC_PRINTF(catalog, LOC_CLI_CLIMATE_EVENT_DRIVEN, "Event-Driven Sens : %s\n",
         cfg.enableEventDrivenSensors
             ? Localization_Text(catalog, LOC_CLI_CLIMATE_VALUE_YES)
             : Localization_Text(catalog, LOC_CLI_CLIMATE_VALUE_POLLING));
  LOC_PRINTF(catalog, LOC_CLI_CLIMATE_CPU_PROTECTION, "CPU Temp Protect  : %s\n",
         cfg.cpuTempProtectionEnabled
             ? Localization_Text(catalog, LOC_CLI_CLIMATE_VALUE_ENABLED)
             : Localization_Text(catalog, LOC_CLI_CLIMATE_VALUE_DISABLED));
  printf(CLI_COLOR_CYAN "================================\n" CLI_COLOR_RESET);
  return 0;
}

/**
 * @brief CLIMATE HIST — display temperature history ring buffer.
 *
 * Prints the last N object/ambient/CPU temperature readings stored in the
 * ring buffer (oldest first). Shows how many ms ago each reading was taken.
 */
static int do_climate_hist(int argc, char **argv) {
  const LocalizationCatalog *catalog = Localization_Snapshot();
  (void)argv;
  if (argc != 1) return 1;

  /* Use a local buffer matching CLIMATE_HISTORY_MAX. */
  ClimateTempHistoryEntry entries[CLIMATE_HISTORY_BUFFER_SIZE];
  uint8_t count = ClimateController_GetHistory(entries, CLIMATE_HISTORY_BUFFER_SIZE);
  if (count > CLIMATE_HISTORY_BUFFER_SIZE) return 1;

  LOC_PRINTF(catalog, LOC_CLI_CLIMATE_HISTORY_TITLE, "\n=== Temperature History (%u entries) ===\n",
         (unsigned)count);

  if (count == 0) {
    LOC_PRINTF(catalog, LOC_CLI_CLIMATE_HISTORY_EMPTY, "  (no history available yet)\n");
    printf(CLI_COLOR_CYAN "================================\n" CLI_COLOR_RESET);
    return 0;
  }

  printf("  %-5s  %-10s  %-10s  %-10s  %s\n",
         "#", Localization_Text(catalog, LOC_CLI_CLIMATE_VALUE_HISTORY_OBJECT), Localization_Text(catalog, LOC_CLI_CLIMATE_VALUE_HISTORY_AMBIENT), "CPU (°C)", Localization_Text(catalog, LOC_CLI_CLIMATE_VALUE_HISTORY_AGE));
  printf("  %-5s  %-10s  %-10s  %-10s  %s\n",
         "─────", "──────────", "──────────", "──────────", "────────");

  /* Approximate "now" for age calculation. */
  uint32_t nowMs = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);

  for (uint8_t i = 0; i < count; i++) {
    uint32_t ageMs = nowMs - entries[i].tickMs;
    printf("  %-5u  %-10.2f  %-10.2f  %-10.2f  %lu\n",
           (unsigned)(i + 1),
           entries[i].objectTemp,
           entries[i].ambientTemp,
           entries[i].cpuTemp,
           (unsigned long)ageMs);
  }
  printf(CLI_COLOR_CYAN "================================\n" CLI_COLOR_RESET);
  return 0;
}

/**
 * @brief CLIMATE SET <HIGH|LOW> <value> — update relay setpoint.
 *
 * @param argc Argument count (must be exactly 3).
 * @param argv argv[0]="SET", argv[1]="HIGH"|"LOW", argv[2]=float value.
 */
static int do_climate_set(int argc, char **argv) {
  const LocalizationCatalog *catalog = Localization_Snapshot();
  float value;
  if (argc != 3 || !parse_float(argv[2], &value)) return 1;
  bool high = strcasecmp(argv[1], "HIGH") == 0;
  if (!high && strcasecmp(argv[1], "LOW") != 0) return 1;
  IConfigurationManager *owner = ConfigurationManager_GetInstance();
  if (FanCommands_Dispatch(owner, (FanCommand){high ? FAN_COMMAND_OBJECT_ON : FAN_COMMAND_OBJECT_OFF, value, 0}, false) != ESP_OK) {
    LOC_PRINTF(catalog, LOC_CLI_CLIMATE_SETTING_REJECTED, "Fan setting rejected: temperature bounds and ON > OFF required.\n"); return 1;
  }
  LOC_PRINTF(catalog, LOC_CLI_CLIMATE_SETTING_APPLIED, "Relay %s setpoint updated to %.2f C; use CLIMATE SAVE to persist.\n", high ? Localization_Text(catalog, LOC_CLI_CLIMATE_VALUE_OPENING) : Localization_Text(catalog, LOC_CLI_CLIMATE_VALUE_CLOSING), value);
  return 0;
}

/**
 * @brief CLIMATE MODE <OBJECT_SENSITIVE|AMBIENT_DELTA|ON|OFF> — set fan operating mode.
 *
 * @param argc Argument count (must be exactly 2).
 * @param argv argv[0]="MODE", argv[1]="OBJECT_SENSITIVE"|"ON"|"OFF".
 */
static int do_climate_mode(int argc, char **argv) {
  const LocalizationCatalog *catalog = Localization_Snapshot();
  if (argc != 2) return 1;
  ClimateFanMode mode;
  if (!strcasecmp(argv[1], "OBJECT_SENSITIVE")) mode = CLIMATE_FAN_OBJECT_SENSITIVE;
  else if (!strcasecmp(argv[1], "AMBIENT_DELTA")) mode = CLIMATE_FAN_AMBIENT_DELTA;
  else if (!strcasecmp(argv[1], "ON")) mode = CLIMATE_FAN_MANUAL_ON;
  else if (!strcasecmp(argv[1], "OFF")) mode = CLIMATE_FAN_MANUAL_OFF;
  else return 1;
  if (FanCommands_Dispatch(ConfigurationManager_GetInstance(), (FanCommand){FAN_COMMAND_MODE, mode, 0}, false) != ESP_OK) return 1;
  LOC_PRINTF(catalog, LOC_CLI_CLIMATE_MODE_APPLIED, "Fan mode updated to %s; use CLIMATE SAVE to persist.\n", fan_mode_str(catalog, mode));
  return 0;
}

/**
 * @brief CLIMATE CAL <OBJ|AMB> <value> — set calibration offset.
 *
 * @param argc Argument count (must be exactly 3).
 * @param argv argv[0]="CAL", argv[1]="OBJ"|"AMB", argv[2]=float offset.
 */
static int do_climate_cal(int argc, char **argv) {
  const LocalizationCatalog *catalog = Localization_Snapshot();
  float value;
  if (argc != 3 || !parse_float(argv[2], &value) || value < CALIBRATION_MIN || value > CALIBRATION_MAX) return 1;
  bool object = !strcasecmp(argv[1], "OBJ");
  if (!object && strcasecmp(argv[1], "AMB")) return 1;
  ConfigurationFieldUpdate field = {object ? offsetof(CalibrationConfigs, ir_object_cal) :
                                            offsetof(CalibrationConfigs, ir_ambient_cal), &value, sizeof(value)};
  if (update_fields("calibration", &field, 1)) return 1;
  LOC_PRINTF(catalog, LOC_CLI_CLIMATE_CALIBRATION_APPLIED, "%s calibration updated to %.2f C; use CLIMATE SAVE to persist.\n", object ? Localization_Text(catalog, LOC_CLI_CLIMATE_VALUE_RADIATOR) : Localization_Text(catalog, LOC_CLI_CLIMATE_VALUE_AMBIENT), value);
  return 0;
}

/**
 * @brief CLIMATE SAVE — persist current climate settings to NVS.
 *
 * Triggers a synchronous save of the control and calibration configuration
 * blocks via the ConfigurationManager.
 */
static int do_climate_save(int argc, char **argv) {
  const LocalizationCatalog *catalog = Localization_Snapshot();
  (void)argv;
  if (argc != 1) return 1;

  IConfigurationManager *cfg = ConfigurationManager_GetInstance();
  if (!cfg || !cfg->Save) {
    LOC_PRINTF(catalog, LOC_CLI_CLIMATE_CONFIG_MISSING, "ConfigurationManager: not available\n");
    return 1;
  }

  LOC_PRINTF(catalog, LOC_CLI_CLIMATE_SAVING, "Saving climate settings to NVS...\n");
  esp_err_t rc = cfg->Save(cfg, "control");
  bool control_saved = rc == ESP_OK;
  if (rc == ESP_OK) {
    rc = cfg->Save(cfg, "calibration");
  }

  if (rc == ESP_OK) {
    LOC_PRINTF(catalog, LOC_CLI_CLIMATE_SAVED, "Settings saved successfully.\n");
    ESP_LOGI(TAG_CLI, "Climate settings saved to NVS via console");
  } else {
    if (control_saved) LOC_PRINTF(catalog, LOC_CLI_CLIMATE_PARTIAL_SAVE, "Control saved; calibration save failed. No complete climate save.\n");
    LOC_PRINTF(catalog, LOC_CLI_CLIMATE_SAVE_FAILED, "Save failed (err=0x%x)\n", (unsigned)rc);
    ESP_LOGE(TAG_CLI, "NVS save failed: 0x%x", (unsigned)rc);
    return 1;
  }
  return 0;
}

/**
 * @brief CLIMATE CPU — display CPU thermal thresholds and protection status.
 *
 * Prints the current CPU temperature alongside the warning, critical, and
 * emergency thresholds from ThermalConfigs. Shows whether the thermal
 * protection override is active.
 */
static int do_climate_cpu(int argc, char **argv) {
  const LocalizationCatalog *catalog = Localization_Snapshot();
  bool machine=argc==2 && argv && argv[1] && !strcmp(argv[1],"--machine");
  if (argc != 1 && !machine) return 1;

  IClimateController *cc = get_climate_ctrl();
  SystemSettings snapshot;
  const SystemSettings *s = get_settings(&snapshot) ? &snapshot : NULL;
  if (!cc || !s || !cc->GetCurrentCpuTemperature || !cc->GetConfig ||
      !cc->IsCpuThermalOverrideActive) return 1;
  if (machine) {
    printf("RACOOL_CLI v=1 type=climate_cpu override=%u cpu_c=%.2f warning_c=%.2f critical_c=%.2f emergency_c=%.2f polling_ms=%lu\n",
        cc->IsCpuThermalOverrideActive(cc) ? 1u : 0u,cc->GetCurrentCpuTemperature(cc),
        s->thermal.cpu_temp_warning,s->thermal.cpu_temp_critical,s->thermal.cpu_temp_emergency,
        (unsigned long)s->thermal.cpu_polling_interval_ms);
    return 0;
  }

  LOC_PRINTF(catalog, LOC_CLI_CLIMATE_CPU_TITLE, "\n=== CPU Thermal Status ===\n");

  if (cc) {
    float cpu = cc->GetCurrentCpuTemperature(cc);
    ClimateControllerConfig cfg = cc->GetConfig(cc);

    LOC_PRINTF(catalog, LOC_CLI_CLIMATE_CPU_TEMPERATURE_C, "CPU Temperature   : %.2f °C\n", cpu);
    LOC_PRINTF(catalog, LOC_CLI_CLIMATE_THERMAL_OVERRIDE, "Thermal Override  : %s\n",
           (cc->IsCpuThermalOverrideActive && cc->IsCpuThermalOverrideActive(cc))
               ? Localization_Text(catalog, LOC_CLI_CLIMATE_VALUE_EMERGENCY)
               : Localization_Text(catalog, LOC_CLI_CLIMATE_VALUE_INACTIVE));
    LOC_PRINTF(catalog, LOC_CLI_CLIMATE_CPU_PROTECTION_STATE, "CPU Protection    : %s\n",
           cfg.cpuTempProtectionEnabled
               ? Localization_Text(catalog, LOC_CLI_CLIMATE_VALUE_ENABLED)
               : Localization_Text(catalog, LOC_CLI_CLIMATE_VALUE_DISABLED));
  }

  if (s) {
    LOC_PRINTF(catalog, LOC_CLI_CLIMATE_WARNING_THRESHOLD, "Warning Thresh    : %.2f °C\n",   s->thermal.cpu_temp_warning);
    LOC_PRINTF(catalog, LOC_CLI_CLIMATE_CRITICAL_THRESHOLD, "Critical Thresh   : %.2f °C\n",   s->thermal.cpu_temp_critical);
    LOC_PRINTF(catalog, LOC_CLI_CLIMATE_EMERGENCY_THRESHOLD, "Emergency Thresh  : %.2f °C\n",   s->thermal.cpu_temp_emergency);
    LOC_PRINTF(catalog, LOC_CLI_CLIMATE_POLLING_INTERVAL, "Polling Interval  : %lu ms\n",
           (unsigned long)s->thermal.cpu_polling_interval_ms);
  }

  printf(CLI_COLOR_CYAN "==========================\n" CLI_COLOR_RESET);
  return 0;
}

/**
 * @brief CLIMATE RESET — restore climate settings to compiled-in defaults.
 */
static int do_climate_reset(int argc, char **argv) {
  const LocalizationCatalog *catalog = Localization_Snapshot();
  (void)argv;
  if (argc != 1) return 1;
  float object = DEFAULT_IR_OBJECT_CAL, ambient = DEFAULT_IR_AMBIENT_CAL;
  ConfigurationFieldUpdate calibration[] = {
    {offsetof(CalibrationConfigs, ir_object_cal), &object, sizeof(object)},
    {offsetof(CalibrationConfigs, ir_ambient_cal), &ambient, sizeof(ambient)}
  };
  if (FanCommands_Dispatch(ConfigurationManager_GetInstance(),(FanCommand){FAN_COMMAND_RESET,0,0},false)!=ESP_OK) {
    LOC_PRINTF(catalog, LOC_CLI_CLIMATE_RESET_REJECTED, "Climate control defaults rejected.\n");return 1;
  }
  if (update_fields("calibration", calibration, 2)) {
    LOC_PRINTF(catalog, LOC_CLI_CLIMATE_PARTIAL_RESET, "Control defaults applied; calibration reset failed. No complete reset.\n");
    return 1;
  }
  LOC_PRINTF(catalog, LOC_CLI_CLIMATE_RESET_APPLIED, "Climate defaults applied; use CLIMATE SAVE to persist.\n");
  return 0;
}

/**
 * @brief CLIMATE TEST — run ClimateController built-in self-test.
 */
static int do_climate_test(int argc, char **argv) {
  const LocalizationCatalog *catalog = Localization_Snapshot();
  (void)argv;
  if (argc != 1) return 1;

  IClimateController *cc = get_climate_ctrl();
  if (!cc || !cc->RunSelfTest) return 1;

  LOC_PRINTF(catalog, LOC_CLI_CLIMATE_SELFTEST_RUNNING, "Running ClimateController self-test...\n");
  int32_t result = cc->RunSelfTest(cc);

  if (result == 0) {
    LOC_PRINTF(catalog, LOC_CLI_CLIMATE_SELFTEST_PASSED, "Self-test PASSED\n");
  } else {
    LOC_PRINTF(catalog, LOC_CLI_CLIMATE_SELFTEST_FAILED, "Self-test FAILED (%ld check(s) failed — see log)\n", (long)(-result));
  }
  return (result == 0) ? 0 : 1;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Top-level Dispatcher
 * ───────────────────────────────────────────────────────────────────────────── */

/**
 * @brief Top-level CLIMATE command dispatcher.
 *
 * Routes to the appropriate sub-command handler based on argv[1].
 * Prints the full usage summary when called with no arguments.
 */
static int do_climate_unit(int argc, char **argv) {
  const LocalizationCatalog *catalog = Localization_Snapshot();
  if (argc < 1 || argc > 2) return 1;
  IConfigurationManager *config = ConfigurationManager_GetInstance();
  if (!config || !config->GetSnapshot || !config->UpdateFields) return 1;
  if (argc == 2) {
    uint8_t unit;
    if (!strcasecmp(argv[1], "C") || !strcasecmp(argv[1], "Celsius")) unit = 0;
    else if (!strcasecmp(argv[1], "F") || !strcasecmp(argv[1], "Fahrenheit")) unit = 1;
    else { LOC_PRINTF(catalog, LOC_CLI_CLIMATE_UNIT_USAGE, "Usage: CLIMATE UNIT [C|F]\n"); return 1; }
    ConfigurationFieldUpdate field = {offsetof(SystemBaseConfigs, temp_unit), &unit, sizeof(unit)};
    if (config->UpdateFields(config, "system", &field, 1, true) != ESP_OK) {
      LOC_PRINTF(catalog, LOC_CLI_CLIMATE_UNIT_FAILED, "Temperature unit save failed\n"); return 1;
    }
  }
  SystemSettings snapshot;
  if (config->GetSnapshot(config, &snapshot) != ESP_OK) return 1;
  LOC_PRINTF(catalog, LOC_CLI_CLIMATE_UNIT_CURRENT, "Temperature display unit: %s\n", snapshot.system.temp_unit == 1 ? "Fahrenheit" : "Celsius");
  LOC_PRINTF(catalog, LOC_CLI_CLIMATE_UNIT_SCOPE, "Zigbee temperatures and explicit Celsius diagnostic fields remain in Celsius.\n");
  return 0;
}

static int do_climate_source(int argc, char **argv) {
  const LocalizationCatalog *catalog = Localization_Snapshot();
  if (argc != 3) return 1;
  bool object = !strcasecmp(argv[1], "OBJECT");
  if (!object && strcasecmp(argv[1], "AMBIENT")) return 1;
  float source;
  if (!parse_float(argv[2], &source)) return 1;
  esp_err_t result = FanCommands_Dispatch(ConfigurationManager_GetInstance(),
      (FanCommand){object ? FAN_COMMAND_OBJECT_SOURCE : FAN_COMMAND_AMBIENT_SOURCE, source, 0}, false);
  LOC_PRINTF(catalog, LOC_CLI_CLIMATE_SOURCE_RESULT, "Temperature source %s; use CLIMATE SAVE to persist.\n", result == ESP_OK ? Localization_Text(catalog, LOC_CLI_CLIMATE_VALUE_UPDATED) : Localization_Text(catalog, LOC_CLI_CLIMATE_VALUE_REJECTED_SOURCE));
  return result == ESP_OK ? 0 : 1;
}
static int do_climate_delta(int argc, char **argv) {
  const LocalizationCatalog *catalog = Localization_Snapshot();
  if (argc != 3) return 1;
  float on, off;
  if (!parse_float(argv[1], &on) || !parse_float(argv[2], &off)) return 1;
  esp_err_t result = FanCommands_Dispatch(ConfigurationManager_GetInstance(), (FanCommand){FAN_COMMAND_DELTA_PAIR, on, off}, false);
  LOC_PRINTF(catalog, LOC_CLI_CLIMATE_DELTA_RESULT, "Fan delta %s: -9..+9 C, step 0.1, ON > OFF; use CLIMATE SAVE to persist.\n", result == ESP_OK ? Localization_Text(catalog, LOC_CLI_CLIMATE_VALUE_UPDATED) : Localization_Text(catalog, LOC_CLI_CLIMATE_VALUE_REJECTED));
  return result == ESP_OK ? 0 : 1;
}
static int do_climate_external(int argc, char **argv) {
  const LocalizationCatalog *catalog = Localization_Snapshot();
  if (argc != 3) return 1;
  float slot, value;
  if (!parse_float(argv[1],&slot) || slot<1 || slot>3 || floorf(slot)!=slot) return 1;
  IClimateController *controller=get_climate_ctrl();
  if (!controller) return 1;
  esp_err_t result;
  if (!strcasecmp(argv[2],"CLEAR")) result=controller->ClearExternalTemperature ? controller->ClearExternalTemperature(controller,(uint8_t)slot) : ESP_ERR_INVALID_STATE;
  else {
    if (!parse_float(argv[2],&value) || !controller->SetExternalTemperature) return 1;
    result=controller->SetExternalTemperature(controller,(uint8_t)slot,value);
  }
  LOC_PRINTF(catalog, LOC_CLI_CLIMATE_EXTERNAL_RESULT, "External temperature RAM update %s.\n", result == ESP_OK ? Localization_Text(catalog, LOC_CLI_CLIMATE_VALUE_ACCEPTED) : Localization_Text(catalog, LOC_CLI_CLIMATE_VALUE_REJECTED));
  return result == ESP_OK ? 0 : 1;
}
static int do_climate_sources(int argc, char **argv) {
  (void)argv;
  if (argc != 1) return 1;
  IClimateController *controller = get_climate_ctrl();
  TemperatureSourceSnapshot snapshot;
  if (!controller || !controller->GetTemperatureSources || controller->GetTemperatureSources(controller, &snapshot) != ESP_OK) return 1;
  const ControlConfigs *config=&snapshot.config;
  unsigned mode=config->fan_manual_mode ? (config->fan_manual_state ? 1 : 2) : (config->fan_strategy==1 ? 3 : 0);
  printf("FAN_CONFIG mode=%u object_on=%.2f object_off=%.2f delta_on=%.1f delta_off=%.1f\n",mode,
      config->relay_open_temp,config->relay_close_temp,config->fan_delta_on_decic/10.0f,config->fan_delta_off_decic/10.0f);
  const EffectiveTemperature *values[] = {&snapshot.object, &snapshot.ambient};
  const char *names[] = {"object", "ambient"};
  for (unsigned i = 0; i < 2; ++i) {
    const EffectiveTemperature *value = values[i];
    printf("SOURCE %s configured=%u effective=%u status=%u valid=%u temperature=%.2f age_ms=%lu\n", names[i],
        value->configured_source, value->effective_source, (unsigned)value->status, value->valid, value->value, (unsigned long)value->age_ms);
  }
  printf("DIFFERENCE valid=%u temperature=%.2f\n", snapshot.difference_valid, snapshot.temperature_difference);
  for (unsigned i = 0; i < 3; ++i) printf("EXT%u state=%u value=%.2f last_update_ms=%lu age_ms=%lu\n", i+1,
      (unsigned)TemperatureSources_SlotState(&snapshot.slots[i], snapshot.now_ms), snapshot.slots[i].value,
      (unsigned long)snapshot.slots[i].last_update_ms,
      (unsigned long)(snapshot.slots[i].valid ? (uint32_t)(snapshot.now_ms-snapshot.slots[i].last_update_ms) : 0));
  return 0;
}

static int do_climate_action(int argc,char **argv) {
  (void)argv;
  IClimateController *controller=get_climate_ctrl();FanActionStatus status;
  if (argc!=1 || !controller || !controller->GetFanActionStatus || controller->GetFanActionStatus(controller,&status)!=ESP_OK) return 1;
  printf("FAN_ACTION sequence=%lu type=%u pending=%u active=%u result=%d\n",(unsigned long)status.sequence,
      (unsigned)status.type,status.pending,status.active,status.result);return 0;
}

static int dispatch_climate(int argc, char **argv) {
  const LocalizationCatalog *catalog = Localization_Snapshot();
  if (argc<1 || argc>4 || !argv) return 1;
  for (int i=0;i<argc;++i) if (!argv[i] || !*argv[i]) return 1;
  if (argc < 2) {
    LOC_PRINTF(catalog, LOC_CLI_CLIMATE_USAGE, "Usage: CLIMATE <subcommand>\n");
    LOC_PRINTF(catalog, LOC_CLI_CLIMATE_SUBCOMMANDS, "Sub-commands:\n");
    LOC_PRINTF(catalog, LOC_CLI_CLIMATE_HELP_UNIT, "  UNIT [C|F]          — Read or persist temperature display unit\n");
    LOC_PRINTF(catalog, LOC_CLI_CLIMATE_HELP_STATUS, "  STATUS              — Temperatures, relay, mode, setpoints\n");
    LOC_PRINTF(catalog, LOC_CLI_CLIMATE_HELP_INFO, "  INFO                — Active configuration parameters\n");
    LOC_PRINTF(catalog, LOC_CLI_CLIMATE_HELP_HIST, "  HIST                — Temperature history ring buffer\n");
    LOC_PRINTF(catalog, LOC_CLI_CLIMATE_HELP_SET, "  SET HIGH|LOW <val>  — Relay setpoint (°C)\n");
    LOC_PRINTF(catalog, LOC_CLI_CLIMATE_HELP_MODE, "  MODE OBJECT_SENSITIVE|AMBIENT_DELTA|ON|OFF    — Fan mode control\n");
    LOC_PRINTF(catalog, LOC_CLI_CLIMATE_HELP_SOURCE, "  SOURCE OBJECT|AMBIENT <0..3> — Select internal or external source\n");
    LOC_PRINTF(catalog, LOC_CLI_CLIMATE_HELP_DELTA, "  DELTA <on> <off>     — Signed difference thresholds, step 0.1 C\n");
    LOC_PRINTF(catalog, LOC_CLI_CLIMATE_HELP_EXTERNAL, "  EXTERNAL <1..3> <value|CLEAR> — Update or clear RAM measurement\n");
    LOC_PRINTF(catalog, LOC_CLI_CLIMATE_HELP_SOURCES, "  SOURCES             — Configured/effective sources and freshness\n");
    LOC_PRINTF(catalog, LOC_CLI_CLIMATE_HELP_ACTION, "  ACTION              — Latest admitted diagnostic action status\n");
    LOC_PRINTF(catalog, LOC_CLI_CLIMATE_HELP_CAL, "  CAL OBJ|AMB <val>   — Calibration offset (°C)\n");
    LOC_PRINTF(catalog, LOC_CLI_CLIMATE_HELP_SAVE, "  SAVE                — Persist settings to NVS\n");
    LOC_PRINTF(catalog, LOC_CLI_CLIMATE_HELP_CPU, "  CPU                 — CPU thermal status and thresholds\n");
    LOC_PRINTF(catalog, LOC_CLI_CLIMATE_HELP_RESET, "  RESET               — Restore climate defaults\n");
    LOC_PRINTF(catalog, LOC_CLI_CLIMATE_HELP_TEST, "  TEST                — Run self-test\n");
    return 0;
  }

  if (strcasecmp(argv[1], "UNIT") == 0) return do_climate_unit(argc - 1, &argv[1]);
  if (strcasecmp(argv[1], "STATUS") == 0) return do_climate_status(argc - 1, &argv[1]);
  if (strcasecmp(argv[1], "INFO")   == 0) return do_climate_info(argc - 1, &argv[1]);
  if (strcasecmp(argv[1], "HIST")   == 0) return do_climate_hist(argc - 1, &argv[1]);
  if (strcasecmp(argv[1], "SET")    == 0) return do_climate_set(argc - 1, &argv[1]);
  if (strcasecmp(argv[1], "MODE")   == 0) return do_climate_mode(argc - 1, &argv[1]);
  if (strcasecmp(argv[1], "ACTION")==0) return do_climate_action(argc-1,&argv[1]);
  if (strcasecmp(argv[1], "SOURCE") == 0) return do_climate_source(argc - 1, &argv[1]);
  if (strcasecmp(argv[1], "DELTA") == 0) return do_climate_delta(argc - 1, &argv[1]);
  if (strcasecmp(argv[1], "EXTERNAL") == 0) return do_climate_external(argc - 1, &argv[1]);
  if (strcasecmp(argv[1], "SOURCES") == 0) return do_climate_sources(argc - 1, &argv[1]);
  if (strcasecmp(argv[1], "CAL")    == 0) return do_climate_cal(argc - 1, &argv[1]);
  if (strcasecmp(argv[1], "SAVE")   == 0) return do_climate_save(argc - 1, &argv[1]);
  if (strcasecmp(argv[1], "CPU")    == 0) return do_climate_cpu(argc - 1, &argv[1]);
  if (strcasecmp(argv[1], "RESET")  == 0) return do_climate_reset(argc - 1, &argv[1]);
  if (strcasecmp(argv[1], "TEST")   == 0) return do_climate_test(argc - 1, &argv[1]);

  LOC_PRINTF(catalog, LOC_CLI_CLIMATE_UNKNOWN_COMMAND, "Unknown sub-command: %s\n", argv[1]);
  LOC_PRINTF(catalog, LOC_CLI_CLIMATE_USAGE_HINT, "Run 'CLIMATE' without arguments for usage.\n");
  return 1;
}

static int cmd_climate(int argc,char **argv) {
  if (argc<1 || argc>5 || !argv) return 1;
  for (int i=0;i<argc;++i) if (!argv[i] || !*argv[i]) return 1;
  bool machine=argc>=3 && !strcmp(argv[argc-1],"--machine");
  if (!machine) return dispatch_climate(argc,argv);
  if (!strcasecmp(argv[1],"STATUS") || !strcasecmp(argv[1],"INFO") || !strcasecmp(argv[1],"CPU"))
    return dispatch_climate(argc,argv);
  const char *commands[]={"SET","MODE","CAL","SAVE","RESET","UNIT","SOURCE","DELTA","EXTERNAL","TEST"};
  for (unsigned i=0;i<sizeof(commands)/sizeof(commands[0]);++i) {
    if (strcasecmp(argv[1],commands[i])) continue;
    int result=dispatch_climate(argc-1,argv);
    printf("RACOOL_CLI v=1 type=climate_command command=%s result=%s error=%d\n",
        commands[i],result==0 ? "completed" : "rejected",result);
    return result;
  }
  return 1;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Registration
 * ───────────────────────────────────────────────────────────────────────────── */

/**
 * @brief Registers all CLIMATE commands with the given IDebugConsole instance.
 *
 * Registers both the full name (CLIMATE) and a short alias (CC) for convenience.
 *
 * @param console Pointer to the active IDebugConsole.
 */
esp_err_t ClimateCommands_Register(IDebugConsole *console) {
  if (!console || !console->RegisterCommandWithCategory) return ESP_ERR_INVALID_ARG;

  static const esp_console_cmd_t climate_cmds[] = {
      {
          .command = "CLIMATE",
            .help    = "Climate control (STATUS/SOURCES/SOURCE/DELTA/EXTERNAL/ACTION/INFO/HIST/SET/MODE/CAL/SAVE/CPU/RESET/TEST)",
          .func    = &cmd_climate,
          .hint    = "<subcommand>",
      },
      {
          .command = "CC",
          .help    = "ClimateController commands (short alias for CLIMATE)",
          .func    = &cmd_climate,
          .hint    = "<subcommand>",
      },
  };

  for (int i = 0; i < (int)(sizeof(climate_cmds) / sizeof(climate_cmds[0])); i++) {
    esp_err_t result = console->RegisterCommandWithCategory(console, &climate_cmds[i], CMD_CAT_HARDWARE);
    if (result != ESP_OK) return result;
  }
  return ESP_OK;
}
