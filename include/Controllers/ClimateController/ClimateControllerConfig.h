#ifndef CLIMATE_CONTROLLER_CONFIG_H
#define CLIMATE_CONTROLLER_CONFIG_H

#include <freertos/FreeRTOS.h>
#include <stdbool.h>
#include <stdint.h>
#define FAN_SELF_TEST_PHASE_MS 100u

/**
 * @file ClimateControllerConfig.h
 * @brief Configuration structure and runtime mode types for the ClimateController service.
 *
 * All timing, relay protection, feature-flag, and diagnostic parameters that govern
 * ClimateController behaviour are defined here. Runtime defaults are provided by
 * ClimateController_GetDefaultConfig() and are sourced from macros in SystemConfigs.h
 * so there is a single source of truth.
 */

/* ═══════════════════════════════════════════════════════════════════════════
 * Fan Mode Enum
 * ═══════════════════════════════════════════════════════════════════════════ */

/**
 * @brief Runtime fan/relay operating mode.
 *
 * Returned by IClimateController::GetFanMode() to allow callers to inspect
 * the current mode without parsing raw SystemSettings fields.
 */
typedef enum {
  CLIMATE_FAN_UNKNOWN = -1, /**< Configuration could not be confirmed. */
  CLIMATE_FAN_OBJECT_SENSITIVE        = 0, /**< Object-sensitive temperature-controlled relay. */
  CLIMATE_FAN_MANUAL_ON   = 1, /**< Manual override — relay forced open (fan ON). */
  CLIMATE_FAN_MANUAL_OFF  = 2, /**< Manual override — relay forced closed (fan OFF). */
  CLIMATE_FAN_AMBIENT_DELTA = 3, /**< Object minus ambient hysteresis. */
} ClimateFanMode;

/* ═══════════════════════════════════════════════════════════════════════════
 * Configuration Structure
 * ═══════════════════════════════════════════════════════════════════════════ */

/**
 * @brief Full configuration for the ClimateController task, relay logic, and diagnostics.
 */
typedef struct {
  /** @brief FreeRTOS task stack size in bytes (default: CLIMATE_CTRL_TASK_STACK_SIZE). */
  uint32_t taskStackSize;

  /** @brief FreeRTOS task priority (default: CLIMATE_CTRL_TASK_PRIORITY). */
  UBaseType_t taskPriority;

  /**
   * @brief Main control loop delay in milliseconds.
   *
   * Sets how often the relay logic is evaluated. Lower values give faster
   * response but consume more CPU. Has no effect when event-driven sensor
   * mode is enabled — relay evaluation is still periodic, but sensor data
   * arrives via EventBus callbacks.
   *
   * Default: APP_MAIN_LOOP_DELAY_MS (1000 ms).
   */
  uint32_t mainLoopDelayMs;

  /**
   * @brief Relay debounce period in milliseconds.
   *
   * A relay state change is only executed after the triggering temperature
   * condition has remained stable for this many milliseconds. Prevents
   * chattering near the setpoint threshold.
   *
   * Default: RELAY_DEBOUNCE_MS (50 ms).
   */
  uint32_t relayDebounceMs;

  /**
   * @brief Minimum time in milliseconds that must elapse between two relay
   *        state changes.
   *
   * Protects the relay from rapid on/off cycling which can damage mechanical
   * contacts. Enforced independently of debounce.
   *
   * Default: RELAY_MIN_SWITCH_MS (1000 ms).
   */
  uint32_t relayMinSwitchMs;

  /**
   * @brief Watchdog timeout for the climate control task in milliseconds.
   *
   * If the task does not feed the watchdog within this period the system
   * will trigger a watchdog reset.
   *
   * Default: CLIMATE_CTRL_WATCHDOG_TIMEOUT_MS (5000 ms).
   */
  uint32_t watchdogTimeoutMs;

  /**
   * @brief Sensor stale timeout in milliseconds.
   *
   * If the last IR sensor reading is older than this value, the sensor is
   * considered stale. EVENT_CLIMATE_SENSOR_STALE is published on the EventBus,
   * and IsSensorStale() returns true.
   *
   * Set to 0 to disable stale detection.
   *
   * Default: CLIMATE_SENSOR_STALE_TIMEOUT_MS (10000 ms).
   */
  uint32_t sensorStaleTimeoutMs;

  /**
   * @brief Size of the temperature history ring buffer (number of samples).
   *
   * The ClimateController stores the last N object/ambient/CPU temperature
   * triplets in a ring buffer accessible via CLIMATE HIST console command.
   * Set to 0 to disable history tracking.
   *
   * Default: CLIMATE_HISTORY_BUFFER_SIZE (10 samples).
   */
  uint8_t historyBufferSize;

  /**
   * @brief When true, sensor readings are consumed via EventBus subscriptions
   *        (EVENT_IR_TEMP_UPDATED / EVENT_INTERNAL_TEMP_UPDATED) rather than
   *        being polled inside the main task loop.
   *
   * Event-driven mode reduces latency and decouples the ClimateController
   * from the sensor polling schedule. Set to false for legacy behaviour.
   *
   * Default: true.
   */
  bool enableEventDrivenSensors;

  /**
   * @brief Hysteresis dead-band in degrees Celsius applied around relay
   *        open/close setpoints.
   *
   * Legacy object-sensitive control opens at openSetpoint - hysteresisDeadband
   * and closes at closeSetpoint + hysteresisDeadband, with the existing debounce
   * and minimum switch interval. Ambient-delta control uses its exact signed
   * threshold pair instead. Set to 0.0f to use unmodified object thresholds.
   *
   * Default: 0.0f (disabled — SystemConfigs already has separate open/close
   * thresholds which implement natural hysteresis).
   */
  float hysteresisDeadband;

  /**
   * @brief When true, the relay is forced ON if the CPU temperature exceeds
   *        the cpu_temp_emergency threshold defined in ThermalConfigs.
   *
   * This provides a last-resort cooling measure independently of the normal
   * temperature setpoints and fan mode. The override is removed automatically
   * once the CPU temperature drops below the warning threshold.
   *
   * Default: true.
   */
  bool cpuTempProtectionEnabled;

} ClimateControllerConfig;

/**
 * @brief Returns the default configuration for ClimateController.
 *
 * Values are derived from the macros defined in SystemConfigs.h so there is a
 * single source of truth for system-wide constants.
 *
 * @return A fully initialised ClimateControllerConfig with default values.
 */
ClimateControllerConfig ClimateController_GetDefaultConfig(void);

#endif // CLIMATE_CONTROLLER_CONFIG_H
