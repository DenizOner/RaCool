#ifndef INTERNAL_TEMPERATURE_SENSOR_CONFIG_H
#define INTERNAL_TEMPERATURE_SENSOR_CONFIG_H

#include <stdbool.h>
#include <stdint.h>

#define INTERNAL_TEMP_LOCK_MS 20u
#define INTERNAL_TEMP_STOP_MS 500u
#define INTERNAL_TEMP_POLL_JOIN_MS 20u

/**
 * @file InternalTemperatureSensorConfig.h
 * @brief Configuration definitions for ESP32-C6 Internal Temperature Sensor.
 *
 * Provides parameters for sampling intervals, thermal thresholds, filtering coefficients,
 * and rate-of-change alert thresholds.
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Configuration structure for the Internal Temperature Sensor.
 */
typedef struct {
  int16_t range_min;                    /**< Minimum measurable temperature in Celsius (-10°C) */
  int16_t range_max;                    /**< Maximum measurable temperature in Celsius (80°C) */
  uint32_t polling_interval_ms;         /**< Asynchronous polling interval in milliseconds */
  bool enable_events;                   /**< Flag indicating if thermal event publishing is enabled */
  float temp_offset;                    /**< Temperature calibration offset in Celsius */
  float warning_threshold;              /**< Thermal warning threshold in Celsius */
  float critical_threshold;             /**< Thermal critical threshold in Celsius */
  float emergency_threshold;            /**< Thermal emergency threshold in Celsius */
  float ema_alpha;                      /**< Exponential Moving Average alpha filter coefficient (0.0f - 1.0f) */
  float rate_alert_threshold_c_per_min; /**< Threshold for thermal shock warning in °C per minute */
} InternalTemperatureSensorConfig;

/**
 * @brief Retrieves the default configuration for the internal temperature sensor.
 * 
 * @return InternalTemperatureSensorConfig Structure containing default values.
 */
InternalTemperatureSensorConfig GetDefaultInternalTemperatureSensorConfig(void);
bool InternalTemperatureSensorConfig_IsOffsetValid(float offset);
bool InternalTemperatureSensorConfig_IsRangeValid(int16_t minimum,int16_t maximum);
bool InternalTemperatureSensorConfig_IsValid(const InternalTemperatureSensorConfig *config);

#ifdef __cplusplus
}
#endif

#endif // INTERNAL_TEMPERATURE_SENSOR_CONFIG_H


