#ifndef CLIMATE_CONSOLE_COMMANDS_H
#define CLIMATE_CONSOLE_COMMANDS_H

#include "Communication/DebugConsole/IDebugConsole.h"

/**
 * @file ClimateConsoleCommands.h
 * @brief Debug console command registration for the ClimateController module.
 *
 * Registers the CLIMATE command family (and its short alias CC), providing
 * runtime inspection and control of the ClimateController from the serial
 * debug console (via IDebugConsole).
 *
 * Commands:
 *   CLIMATE STATUS              — Current temperatures, relay state, fan mode, setpoints.
 *   CLIMATE INFO                — Active configuration parameters (task, debounce, watchdog, etc.).
 *   CLIMATE HIST                — Temperature history ring buffer (last N readings).
 *   CLIMATE SET HIGH <value>    — Set the relay-open temperature setpoint (°C).
 *   CLIMATE SET LOW  <value>    — Set the relay-close temperature setpoint (°C).
 *   CLIMATE MODE OBJECT_SENSITIVE — Absolute object-temperature hysteresis.
 *   CLIMATE MODE AMBIENT_DELTA — Effective object minus ambient hysteresis.
 *   CLIMATE SOURCE OBJECT|AMBIENT <0..3> — Choose the independent source.
 *   CLIMATE DELTA <on> <off>   — Atomically set signed difference thresholds.
 *   CLIMATE EXTERNAL <1..3> <C|CLEAR> — Debug RAM injection/invalidation.
 *   CLIMATE SOURCES            — Freshness/fallback diagnostics.
 *   CLIMATE ACTION             — Diagnostic admission/completion status.
 *   CLIMATE MODE ON             — Force fan ON (manual override).
 *   CLIMATE MODE OFF            — Force fan OFF (manual override).
 *   CLIMATE CAL OBJ  <value>    — Set object temperature calibration offset (°C).
 *   CLIMATE CAL AMB  <value>    — Set ambient temperature calibration offset (°C).
 *   CLIMATE SAVE                — Persist all climate settings to NVS.
 *   CLIMATE CPU                 — Display CPU thermal thresholds and protection status.
 *   CLIMATE RESET               — Restore all climate settings to compiled-in defaults.
 *   CLIMATE TEST                — Run ClimateController self-test.
 */

/**
 * @brief Registers all CLIMATE debug console commands with the given console.
 *
 * Must be called after DebugConsole is initialised and ClimateController is
 * in at least SERVICE_STATE_INITIALIZED.
 *
 * @param console Pointer to the active IDebugConsole instance.
 */
esp_err_t ClimateCommands_Register(IDebugConsole *console);

#endif // CLIMATE_CONSOLE_COMMANDS_H
