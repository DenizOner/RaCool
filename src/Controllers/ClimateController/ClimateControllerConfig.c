/**
 * @file ClimateControllerConfig.c
 * @brief Default configuration factory for ClimateController.
 *
 * All default values are sourced from SystemConfigs.h macros to maintain
 * a single source of truth for system-wide constants.
 */

#include "Controllers/ClimateController/ClimateControllerConfig.h"
#include "Management/ConfigurationManager/SystemConfigs.h"

ClimateControllerConfig ClimateController_GetDefaultConfig(void) {
  ClimateControllerConfig config = {
      .taskStackSize             = CLIMATE_CTRL_TASK_STACK_SIZE,
      .taskPriority              = CLIMATE_CTRL_TASK_PRIORITY,
      .mainLoopDelayMs           = APP_MAIN_LOOP_DELAY_MS,
      .relayDebounceMs           = RELAY_DEBOUNCE_MS,
      .relayMinSwitchMs          = RELAY_MIN_SWITCH_MS,
      .watchdogTimeoutMs         = CLIMATE_CTRL_WATCHDOG_TIMEOUT_MS,
      .sensorStaleTimeoutMs      = CLIMATE_SENSOR_STALE_TIMEOUT_MS,
      .historyBufferSize         = CLIMATE_HISTORY_BUFFER_SIZE,
      .enableEventDrivenSensors  = true,
      .hysteresisDeadband        = 0.0f,
      .cpuTempProtectionEnabled  = true,
  };
  return config;
}
