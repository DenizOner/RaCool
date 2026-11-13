#ifndef IINFRARED_TEMPERATURE_SENSOR_H
#define IINFRARED_TEMPERATURE_SENSOR_H

#include "Core/IService.h"
#include "Hardware/InfraredTemperatureSensor/InfraredTemperatureSensorConfig.h"
#include <esp_err.h>
#include <stdbool.h>
#include <stdint.h>

/**
 * @file IInfraredTemperatureSensor.h
 * @brief Interface for the MLX90614 Infrared Temperature Sensor service.
 *
 * Provides cached, event-driven access to MLX90614 measurements. A dedicated worker
 * owns periodic blocking I2C work; cached getters use bounded locks. Stop waits for
 * the worker outside resource locks, detaches its I2C device, and retains cache/locks
 * for restart. Start reattaches the device before starting work. A failed Stop
 * keeps ownership and rejects new measurements until Stop is retried successfully.
 */

typedef struct IInfraredTemperatureSensor IInfraredTemperatureSensor;

/* EVENT_IR_TEMP_UPDATED payload. Keep the temperature pair first for consumers
 * that only display values. Timestamps belong to the completed measurement,
 * never to a subsequent cache read or EventBus delivery. */
typedef struct {
  float temperatures[2];
  int64_t measured_time_us;
  uint32_t measured_tick;
} IRTempSample;

/* Diagnostic bytes, including received PEC; these are not validated temperatures. */
typedef struct {
  uint8_t raw_ir1[3], raw_ir2[3], ambient[3], object1[3], object2[3], config1[3], emissivity[3];
} IRTempRawRegisters;

struct IInfraredTemperatureSensor {
  /** @brief Base service interface for lifecycle management (Initialize/Start/Stop). */
  IService base;

  /**
   * @brief Reads the cached object (target) temperature.
   *
   * Returns the most recent smoothed temperature sample stored in memory without performing
   * a blocking I2C transaction. Safe to call from any thread.
   *
   * @param self Pointer to the IInfraredTemperatureSensor instance.
   * @return float Object temperature in degrees Celsius (°C).
   */
  float (*ReadObjectTemperature)(IInfraredTemperatureSensor *self);

  /**
   * @brief Reads the cached ambient (package) temperature.
   *
   * Returns the most recent ambient temperature sample stored in memory. Safe to call
   * from any thread.
   *
   * @param self Pointer to the IInfraredTemperatureSensor instance.
   * @return float Ambient temperature in degrees Celsius (°C).
   */
  float (*ReadAmbientTemperature)(IInfraredTemperatureSensor *self);

  /**
   * @brief Runs a measurement cycle synchronously on the caller.
   *
   * Forces an immediate reading of object and ambient registers over I2C rather
   * than waiting for the periodic timer. May block on the I2C bus and cache locks.
   * Publishes EVENT_IR_TEMP_UPDATED only when both readings in this cycle are valid
   * and either the delta threshold or heartbeat interval is reached. Cached values
   * remain readable after a failed measurement but are not republished as fresh data.
   *
   * @param self Pointer to the IInfraredTemperatureSensor instance.
   */
  void (*TriggerMeasurement)(IInfraredTemperatureSensor *self);

  /**
   * @brief Performs a hardware self-test to verify sensor presence and communication.
   *
   * Reads the object register and verifies its PEC and sensor error flag.
   * This does not exercise the ambient register or prove measurement accuracy.
   *
   * @param self Pointer to the IInfraredTemperatureSensor instance.
   * @return esp_err_t ESP_OK if sensor passes self-test, or error code on failure.
   */
  esp_err_t (*SelfTest)(IInfraredTemperatureSensor *self);

  /**
   * @brief Returns the last recorded I2C communication error code.
   *
   * @param self Pointer to the IInfraredTemperatureSensor instance.
   * @return esp_err_t Last ESP_OK or error code encountered during I2C transactions.
   */
  esp_err_t (*GetLastError)(IInfraredTemperatureSensor *self);

  /**
   * @brief Retrieves detailed telemetry statistics for the sensor.
   *
   * @param self Pointer to the IInfraredTemperatureSensor instance.
   * @param stats Pointer to output IRTempStats structure.
   * @return esp_err_t ESP_OK on success, ESP_ERR_INVALID_ARG if stats is NULL.
   */
  esp_err_t (*GetStats)(IInfraredTemperatureSensor *self, IRTempStats *stats);

  /**
   * @brief Resets accumulated telemetry statistics to zero/defaults.
   *
   * @param self Pointer to the IInfraredTemperatureSensor instance.
   * @return esp_err_t ESP_OK on success.
   */
  esp_err_t (*ResetStats)(IInfraredTemperatureSensor *self);

  /**
   * @brief Dynamic update of valid temperature range limits.
   *
   * @param self Pointer to the IInfraredTemperatureSensor instance.
   * @param min_temp Minimum valid temperature (°C).
   * @param max_temp Maximum valid temperature (°C).
   * @return esp_err_t ESP_OK on success, ESP_ERR_INVALID_ARG if min >= max.
   */
  esp_err_t (*SetLimits)(IInfraredTemperatureSensor *self, float min_temp, float max_temp);

  /**
   * @brief Dynamically configures sensor polling interval.
   *
   * @param self Pointer to the IInfraredTemperatureSensor instance.
   * @param interval_ms New polling period in milliseconds.
   * @return esp_err_t ESP_OK on success, or ESP_ERR_INVALID_ARG if invalid.
   */
  esp_err_t (*SetPollingInterval)(IInfraredTemperatureSensor *self, uint32_t interval_ms);
  /** @brief Returns the active local sampling interval; zero means uninitialized. */
  uint32_t (*GetPollingInterval)(IInfraredTemperatureSensor *self);
  /** @brief Reads the diagnostic registers through the owned device under the measurement lock.
   * Output is unchanged on error. Bytes are raw and their PEC is not validated here. */
  esp_err_t (*ReadRawRegisters)(IInfraredTemperatureSensor *self, IRTempRawRegisters *out);
  /** Copies the last complete valid pair under a bounded cache lock. Returns
   * INVALID_STATE before the first pair; output remains unchanged on error.
   * A successful cache read does not imply the measurement is still fresh. */
  esp_err_t (*ReadSample)(IInfraredTemperatureSensor *self, IRTempSample *out);
};

/**
 * @brief Singleton module accessor function.
 *
 * Returns a reference to the global InfraredTemperatureSensor instance.
 *
 * @return IInfraredTemperatureSensor* Pointer to the module interface.
 */
IInfraredTemperatureSensor *InfraredTemperatureSensor_GetModule(void);

#endif // IINFRARED_TEMPERATURE_SENSOR_H
