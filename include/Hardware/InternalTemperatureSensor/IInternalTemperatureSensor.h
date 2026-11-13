#ifndef IINTERNAL_TEMPERATURE_SENSOR_H
#define IINTERNAL_TEMPERATURE_SENSOR_H

#include "Core/IService.h"
#include <stdbool.h>
#include <stdint.h>

#include "Hardware/InternalTemperatureSensor/InternalTemperatureSensorConfig.h"

/**
 * @file IInternalTemperatureSensor.h
 * @brief Thread-safe, event-driven interface for ESP32-C6 Internal Temperature Sensor.
 *
 * This module manages hardware sampling, Exponential Moving Average (EMA) filtering,
 * thermal level state machines, rate-of-change calculation, and thermal shock events.
 */

#ifdef __cplusplus
extern "C" {
#endif

struct IDIRegistry;
typedef struct IDIRegistry IDIRegistry;
typedef struct IInternalTemperatureSensor IInternalTemperatureSensor;

/**
 * @brief Thermal level enumeration representing hardware thermal state.
 */
typedef enum {
  THERMAL_LEVEL_NORMAL = 0,    /**< Normal operating temperature */
  THERMAL_LEVEL_WARNING,       /**< High temperature warning */
  THERMAL_LEVEL_CRITICAL,      /**< Critical thermal alert, throttling recommended */
  THERMAL_LEVEL_EMERGENCY      /**< Emergency thermal state, safety action required */
} ThermalLevel;

/**
 * @brief Thermal rate-of-change trend enumeration.
 */
typedef enum {
  THERMAL_TREND_STABLE = 0,    /**< Temperature rate of change is within normal limits */
  THERMAL_TREND_RISING,        /**< Temperature is rising gradually */
  THERMAL_TREND_FALLING,       /**< Temperature is falling gradually */
  THERMAL_TREND_RAPID_RISE     /**< Thermal shock warning: Temperature is rising rapidly */
} ThermalTrend;

/**
 * @brief Thermal statistics snapshot structure.
 */
typedef struct {
  float min_temp;                  /**< Minimum measured temperature in Celsius */
  float max_temp;                  /**< Maximum measured temperature in Celsius */
  float avg_temp;                  /**< Exponential Moving Average (EMA) temperature in Celsius */
  float rate_of_change_c_per_min;  /**< Calculated rate of change in °C per minute */
  uint32_t total_samples;          /**< Total number of successful samples taken */
  uint64_t last_sample_time_ms;    /**< Timestamp of last successful reading in milliseconds */
} ThermalStats;

/* One coherent runtime/configuration snapshot. Cached samples retain their
 * acquisition time; status readback never acquires a new hardware sample. */
typedef struct {
  float last_temperature;
  float offset;
  ThermalLevel level;
  ThermalTrend trend;
  ServiceState state;
  uint64_t last_sample_time_ms;
  bool sample_valid;
} InternalTemperatureStatus;

/**
 * @brief Interface contract for Internal Temperature Sensor service.
 */
struct IInternalTemperatureSensor {
  IService base;

  /**
   * @brief Initializes the sensor with a specific configuration.
   * @param self Pointer to instance
   * @param config The configuration to apply
   * @param registry Dependency Injection registry
   * @return 0 on success, -1 on failure
   */
  int32_t (*InitializeWithConfig)(IInternalTemperatureSensor *self,
                                  InternalTemperatureSensorConfig config,
                                  IDIRegistry *registry);

  /**
   * @brief Reads current temperature from hardware (Thread-safe, applies offset & filtering).
   * @param self Pointer to instance
   * @return float Temperature in Celsius (-273.15f on error)
   */
  float (*ReadTemperature)(IInternalTemperatureSensor *self);

  /**
   * @brief Gets the last recorded temperature without reading hardware.
   * @param self Pointer to instance
   * @return float Cached temperature in Celsius
   */
  float (*GetLastTemperature)(IInternalTemperatureSensor *self);

  /**
   * @brief Starts periodic asynchronous polling.
   * @param self Pointer to instance
   * @param interval_ms Polling interval in milliseconds
   * @return 0 on success, -1 on failure
   */
  int32_t (*StartPolling)(IInternalTemperatureSensor *self, uint32_t interval_ms);

  /**
   * @brief Stops periodic asynchronous polling.
   * @param self Pointer to instance
   * @return 0 on success, -1 on failure
   */
  int32_t (*StopPolling)(IInternalTemperatureSensor *self);

  /**
   * @brief Retrieves current thermal operational level.
   * @param self Pointer to instance
   * @return ThermalLevel Current thermal state
   */
  ThermalLevel (*GetThermalLevel)(IInternalTemperatureSensor *self);

  /**
   * @brief Retrieves current thermal trend (rate of change status).
   * @param self Pointer to instance
   * @return ThermalTrend Current thermal trend
   */
  ThermalTrend (*GetThermalTrend)(IInternalTemperatureSensor *self);

  /**
   * @brief Sets calibration temperature offset.
   * @param self Pointer to instance
   * @param offset_c Offset in Celsius to add to raw readings
   * @return 0 on success, -1 on failure
   */
  int32_t (*SetOffset)(IInternalTemperatureSensor *self, float offset_c);

  /**
   * @brief Retrieves calibration temperature offset.
   * @param self Pointer to instance
   * @return float Offset in Celsius
   */
  float (*GetOffset)(IInternalTemperatureSensor *self);

  /**
   * @brief Gets current thermal statistics snapshot.
   * @param self Pointer to instance
   * @param out_stats Pointer to structure where stats will be copied
   * @return 0 on success, -1 on failure
   */
  int32_t (*GetStats)(IInternalTemperatureSensor *self, ThermalStats *out_stats);

  /**
   * @brief Resets thermal statistics.
   * @param self Pointer to instance
   * @return 0 on success, -1 on failure
   */
  int32_t (*ResetStats)(IInternalTemperatureSensor *self);

  /**
   * @brief Executes self-test hardware and state verification.
   * @param self Pointer to instance
   * @return 0 on success, -1 on failure
   */
  int32_t (*RunSelfTest)(IInternalTemperatureSensor *self);

  /**
   * @brief Dynamically updates sensor range limits.
   * @param self Pointer to instance
   * @param range_min Minimum temperature in Celsius
   * @param range_max Maximum temperature in Celsius
   * @return 0 on success, -1 on failure
   */
  int32_t (*SetLimits)(IInternalTemperatureSensor *self, int16_t range_min, int16_t range_max);
  /* Failed status reads preserve caller output. */
  int32_t (*GetStatus)(IInternalTemperatureSensor *self, InternalTemperatureStatus *status);
};

/**
 * @brief Retrieves singleton instance of Internal Temperature Sensor module.
 * @return IInternalTemperatureSensor* Module instance pointer
 */
IInternalTemperatureSensor *InternalTemperatureSensor_GetModule(void);

#ifdef __cplusplus
}
#endif

#endif // IINTERNAL_TEMPERATURE_SENSOR_H


