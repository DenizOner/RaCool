#ifndef ICLIMATECONTROLLER_H
#define ICLIMATECONTROLLER_H

#include "Controllers/ClimateController/ClimateControllerConfig.h"
#include "Controllers/ClimateController/TemperatureSources.h"
#include "Core/IService.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ═══════════════════════════════════════════════════════════════════════════
 * Temperature History
 * ═══════════════════════════════════════════════════════════════════════════ */

/**
 * @brief A single temperature history snapshot stored in the ring buffer.
 *
 * Exposed here so that ClimateConsoleCommands.c, diagnostics, and test code
 * can share the same type definition.
 */
typedef struct {
  float    objectTemp;  /**< Object (IR) temperature in °C. */
  float    ambientTemp; /**< Ambient temperature in °C. */
  float    cpuTemp;     /**< CPU temperature in °C. */
  uint32_t tickMs;      /**< Tick-to-ms value at the time of the reading. */
} ClimateTempHistoryEntry;

/**
 * @brief Returns a snapshot of the temperature history ring buffer.
 *
 * Copies up to @p maxEntries entries (oldest first) from the ring buffer
 * into the caller-supplied @p out array. Thread-safe (spinlock-protected).
 *
 * @param out        Caller-allocated array (must hold at least @p maxEntries items).
 * @param maxEntries Maximum number of entries to copy.
 * @return           Number of entries actually copied (0 if disabled or empty).
 */
uint8_t ClimateController_GetHistory(ClimateTempHistoryEntry *out, uint8_t maxEntries);


/**
 * @file IClimateController.h
 * @brief Interface for the ClimateController service.
 *
 * The ClimateController orchestrates temperature sensing, relay (fan) control,
 * and calibration logic. It operates as an event-driven, thread-safe service
 * on the ESP32-C6 FreeRTOS platform.
 *
 * Architecture:
 *  - Implements IService for lifecycle management (Initialize / Start / Stop).
 *  - Subscribes to EventBus events for sensor updates and config changes.
 *  - Exposes thread-safe getters for the latest temperature readings.
 *  - Exposes GetFanMode() / GetConfig() / GetRelayDebounceState() / IsSensorStale().
 *  - Registers debug console commands via ClimateConsoleCommands.
 *  - Registers menu plugins for OLED-based user interaction.
 *
 * Fan Mode:
 *  - CLIMATE_FAN_OBJECT_SENSITIVE — relay follows effective object setpoints.
 *  - CLIMATE_FAN_AMBIENT_DELTA    — relay follows effective temperature difference.
 *  - CLIMATE_FAN_MANUAL_ON  — relay forced open (fan always ON).
 *  - CLIMATE_FAN_MANUAL_OFF — relay forced closed (fan always OFF).
 */

typedef struct IClimateController IClimateController;
typedef enum { FAN_ACTION_PULSE, FAN_ACTION_AUTO_OFF, FAN_ACTION_RELAY_MODE,
  FAN_ACTION_RESET_STATS, FAN_ACTION_SELF_TEST } FanActionType;
typedef struct {
  FanActionType type;
  uint32_t value;
  uint32_t sequence;
  bool pending;
  bool active;
  esp_err_t result;
} FanActionStatus;

struct IClimateController {
  /** @brief Base service interface providing Initialize / Start / Stop. */
  IService base;
  /** Runtime injection only; no persistence. Thread-safe, checked admission. */
  esp_err_t (*SetExternalTemperature)(IClimateController *self, uint8_t slot, float value);
  /** Explicit invalidation; no fabricated numeric measurement. */
  esp_err_t (*ClearExternalTemperature)(IClimateController *self, uint8_t slot);
  esp_err_t (*GetTemperatureSources)(IClimateController *self, TemperatureSourceSnapshot *out);
  /** One copied diagnostic request, owned by the existing control worker. */
  esp_err_t (*RequestFanAction)(IClimateController *self, FanActionType type, uint32_t value);
  esp_err_t (*GetFanActionStatus)(IClimateController *self, FanActionStatus *out);

  /**
   * @brief Initializes the climate controller with a specific configuration.
   *
   * Must be called during startup before concurrent consumers and the first Start().
   * Calls while running, stopped or closing are rejected without changing settings.
   * If not called, Start() will apply the
   * default configuration obtained from ClimateController_GetDefaultConfig().
   *
   * @param self     Pointer to the IClimateController instance.
   * @param config   Configuration values for task, relay, and watchdog timing.
   * @param registry Dependency injection registry for resolving services.
   * @return         0 on success, negative error code on failure.
   */
  int32_t (*InitializeWithConfig)(IClimateController *self,
                                  ClimateControllerConfig config,
                                  struct IDIRegistry *registry);

  /**
   * @brief Returns the latest cached object (infrared) temperature reading.
   *
   * This call is thread-safe and non-blocking. The returned value is refreshed
   * either on every EventBus EVENT_IR_TEMP_UPDATED event (event-driven mode)
   * or on every mainLoopDelayMs polling cycle (fallback mode).
   *
   * @param self Pointer to the IClimateController instance.
   * @return     Object temperature in degrees Celsius. Returns -127.0f if no
   *             valid reading has been received yet.
   */
  float (*GetCurrentObjectTemperature)(IClimateController *self);

  /**
   * @brief Returns the latest cached ambient (sensor package) temperature.
   *
   * Thread-safe and non-blocking. Updated alongside object temperature.
   *
   * @param self Pointer to the IClimateController instance.
   * @return     Ambient temperature in degrees Celsius. Returns -127.0f if no
   *             valid reading has been received yet.
   */
  float (*GetCurrentAmbientTemperature)(IClimateController *self);

  /**
   * @brief Returns the latest cached internal CPU temperature reading.
   *
   * Thread-safe and non-blocking. Updated on every EventBus
   * EVENT_INTERNAL_TEMP_UPDATED event.
   *
   * @param self Pointer to the IClimateController instance.
   * @return     CPU temperature in degrees Celsius. Returns -127.0f if no
   *             valid reading has been received yet.
   */
  float (*GetCurrentCpuTemperature)(IClimateController *self);

  /**
   * @brief Returns the current relay (fan) open/closed state.
   *
   * Thread-safe. Reflects the physical relay state as managed by the
   * relay control logic.
   *
   * @param self Pointer to the IClimateController instance.
   * @return     true if the relay is open (fan ON), false if closed (fan OFF).
   */
  bool (*GetRelayState)(IClimateController *self);

  /**
   * @brief Returns the FreeRTOS tick time of the last successful sensor read.
   *
   * Can be used by watchdog or diagnostic code to detect sensor stalls.
   *
   * @param self Pointer to the IClimateController instance.
   * @return     Tick count (xTaskGetTickCount()) at the time of the last read.
   *             Returns 0 if no reading has occurred yet.
   */
  uint32_t (*GetLastSensorReadTime)(IClimateController *self);

  /**
   * @brief Returns the current fan/relay operating mode.
   *
   * Thread-safe. Derived from the live SystemSettings without requiring the
   * caller to access ConfigurationManager directly.
   *
   * @param self Pointer to the IClimateController instance.
   * @return     ClimateFanMode enum value:
   *               CLIMATE_FAN_OBJECT_SENSITIVE — effective object temperature thresholds.
   *               CLIMATE_FAN_AMBIENT_DELTA    — effective object-minus-ambient thresholds.
   *               CLIMATE_FAN_MANUAL_ON   — relay forced ON.
   *               CLIMATE_FAN_MANUAL_OFF  — relay forced OFF.
   *               CLIMATE_FAN_UNKNOWN     — configuration snapshot unavailable;
   *                                         no operating mode is confirmed.
   */
  ClimateFanMode (*GetFanMode)(IClimateController *self);

  /** @brief True only while CPU protection owns a forced-cooling override.
   * Manual fan ON is independent of this state. */
  bool (*IsCpuThermalOverrideActive)(IClimateController *self);

  /**
   * @brief Returns a snapshot of the active ClimateController configuration.
   *
   * Returns the config currently in use by the running service. This is
   * either the config passed to InitializeWithConfig() or the default config
   * applied automatically by Start().
   *
   * @param self Pointer to the IClimateController instance.
   * @return     Copy of the active ClimateControllerConfig.
   */
  ClimateControllerConfig (*GetConfig)(IClimateController *self);

  /**
   * @brief Returns whether the relay debounce countdown is currently active.
   *
   * Returns true between the moment a temperature condition is first detected
   * and the moment the debounce period elapses and the relay action is applied.
   * Useful for diagnostic UIs and tests.
   *
   * @param self Pointer to the IClimateController instance.
   * @return     true if a debounce countdown is in progress.
   */
  bool (*GetRelayDebounceState)(IClimateController *self);

  /**
   * @brief Returns whether the IR sensor reading is considered stale.
   *
   * A reading is stale when the elapsed time since the last successful
   * EVENT_IR_TEMP_UPDATED exceeds config.sensorStaleTimeoutMs.
   * Stale detection is disabled if sensorStaleTimeoutMs == 0.
   *
   * @param self Pointer to the IClimateController instance.
   * @return     true if the sensor reading is stale or no reading has occurred.
   */
  bool (*IsSensorStale)(IClimateController *self);

  /**
   * @brief Runs an in-place self-test of the ClimateController subsystem.
   *
   * Checks that all required dependencies (sensor, relay, config manager) are
   * reachable, that temperature values are within plausible ranges, that
   * relay state is consistent with configuration, and that the sensor is not
   * stale. Results are logged via ESP_LOG at INFO level.
   *
   * @param self Pointer to the IClimateController instance.
   * @return     0 if all checks pass, negative count of failed checks otherwise.
   */
  int32_t (*RunSelfTest)(IClimateController *self);
};

/**
 * @brief Returns the singleton IClimateController instance.
 *
 * The first call initialises the vtable. Subsequent calls return the same
 * pointer. This function is thread-safe by virtue of the static initialisation
 * guard (called before tasks are created during system boot).
 *
 * @return Pointer to the global ClimateController instance.
 */
IClimateController *ClimateController_GetModule(void);

#ifdef __cplusplus
}
#endif

#endif // ICLIMATECONTROLLER_H
