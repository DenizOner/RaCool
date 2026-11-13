#ifndef INFRARED_TEMPERATURE_SENSOR_CONFIG_H
#define INFRARED_TEMPERATURE_SENSOR_CONFIG_H

#include <stdint.h>
#include <esp_err.h>

typedef enum {
  IRTEMP_FAILURE_NONE, IRTEMP_FAILURE_I2C, IRTEMP_FAILURE_PEC, IRTEMP_FAILURE_VALUE
} IRTempFailureKind;

/**
 * @file InfraredTemperatureSensorConfig.h
 * @brief Configuration structure for the MLX90614 Infrared Temperature Sensor.
 *
 * Holds all I2C communication parameters, register addresses, temperature
 * validation thresholds, and filtering coefficients for the MLX90614 sensor.
 * Fields are ordered to minimise struct padding (memory alignment).
 */

/**
 * @brief MLX90614 infrared temperature sensor configuration structure.
 *
 * All timing parameters are in milliseconds unless otherwise noted.
 * Register addresses follow the MLX90614 datasheet RAM map.
 */
typedef struct {
  uint32_t i2c_freq_hz;         /**< I2C clock frequency in Hz (max 100 kHz for MLX90614). */
  uint32_t polling_interval_ms; /**< Asynchronous read period in milliseconds. */
  float    error_temperature;   /**< Temperature value returned on error or invalid data. */
  float    temp_range_min;      /**< Minimum valid temperature in degrees Celsius. */
  float    temp_range_max;      /**< Maximum valid temperature in degrees Celsius. */
  float    event_delta_threshold; /**< Minimum temperature change (°C) required to publish an event. */
  uint32_t max_publish_interval_ms; /**< Maximum interval between forced event publishes, regardless of delta
                                     *   threshold. Prevents ClimateController stale detection from firing
                                     *   when temperature is stable. Set to 0 to disable (delta-only). */
  float    ema_alpha;           /**< Exponential Moving Average smoothing factor [0.0, 1.0].
                                 *   Higher value = faster response, less smoothing.
                                 *   Lower value  = slower response, more smoothing. */
  uint8_t  i2c_address;         /**< MLX90614 I2C slave address (default: 0x5A). */
  uint8_t  reg_ambient_temp;    /**< RAM register address for ambient temperature. */
  uint8_t  reg_object_temp;     /**< RAM register address for object (IR) temperature. */
  uint8_t  i2c_port;            /**< I2C port number (I2C_NUM_x). */
} InfraredTemperatureSensorConfig;

/**
 * @brief Telemetry and statistical data container for MLX90614 sensor.
 */
typedef struct {
  float min_object_temp;      /**< Minimum recorded object temperature (°C). */
  float max_object_temp;      /**< Maximum recorded object temperature (°C). */
  float min_ambient_temp;     /**< Minimum recorded ambient temperature (°C). */
  float max_ambient_temp;     /**< Maximum recorded ambient temperature (°C). */
  uint32_t total_reads;       /**< Total read cycles executed. */
  uint32_t pec_errors;        /**< Total PEC validation failures. */
  uint32_t i2c_errors;        /**< Total I2C communication errors. */
    int64_t last_failure_time_us; /**< Boot-local monotonic time; kind NONE means absent. */
    esp_err_t last_failure_error; /**< SDK error for I2C, INVALID_RESPONSE for PEC/value. */
    IRTempFailureKind last_failure_kind; /**< Retained through later successful samples; ResetStats clears. */
} IRTempStats;

/**
 * @brief Returns the default sensor configuration.
 *
 * Values are derived from the MLX90614 datasheet and project-level
 * constants in SystemConfigs.h, providing a single source of truth.
 *
 * @return InfraredTemperatureSensorConfig Fully populated default configuration.
 */
InfraredTemperatureSensorConfig IRTempSensor_GetDefaultConfig(void);

/* ── Conversion Constants ────────────────────────────────────────────────── */

/** @brief Raw-to-Kelvin multiplier (MLX90614 datasheet, section 8.3.3). */
#define IRTEMP_KELVIN_FACTOR 0.02f

/** @brief Kelvin-to-Celsius offset. */
#define IRTEMP_KELVIN_OFFSET 273.15f

/**
 * @brief Sentinel value used to detect an uninitialised temperature cache.
 *
 * Chosen to be well below any physically achievable temperature reading so
 * that the EMA filter can detect "first valid sample" safely without
 * comparing against error_temperature with a magic number offset.
 */
#define IRTEMP_CACHE_UNINITIALIZED (-300.0f)

/** @brief MLX90614 PEC (Packet Error Code) polynomial (CRC-8, x^8+x^2+x+1). */
#define IRTEMP_PEC_POLYNOMIAL 0x07U

/* One blocking peripheral worker; no I2C work runs in the shared timer task. */
#define IRTEMP_WORKER_STACK_SIZE 4096U
#include "Core/TaskPriorities.h"
#define IRTEMP_WORKER_PRIORITY RACOOL_TASK_PRIORITY_THERMAL_SENSOR
#define IRTEMP_WORKER_CHECKPOINT_MS 50U
#define IRTEMP_CONTROL_TIMEOUT_MS 1500U
#define IRTEMP_MEASUREMENT_LOCK_TIMEOUT_MS 750U
#define IRTEMP_CACHE_LOCK_TIMEOUT_MS 100U
#define IRTEMP_POLL_MIN_MS 50U
#define IRTEMP_POLL_MAX_MS 60000U

#endif /* INFRARED_TEMPERATURE_SENSOR_CONFIG_H */
