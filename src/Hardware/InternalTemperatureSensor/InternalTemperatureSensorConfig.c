#include "Hardware/InternalTemperatureSensor/InternalTemperatureSensorConfig.h"
#include "Management/ConfigurationManager/SystemConfigs.h"
#include <math.h>
#include "soc/temperature_sensor_periph.h"

InternalTemperatureSensorConfig GetDefaultInternalTemperatureSensorConfig(void) {
  InternalTemperatureSensorConfig config = {
      .range_min = -10,
      .range_max = 80,
      .polling_interval_ms = 5000,
      .enable_events = true,
      .temp_offset = 0.0f,
      .warning_threshold = 70.0f,
      .critical_threshold = 80.0f,
      .emergency_threshold = 90.0f,
      .ema_alpha = 0.2f,
      .rate_alert_threshold_c_per_min = 10.0f,
  };
  return config;
}

bool InternalTemperatureSensorConfig_IsValid(const InternalTemperatureSensorConfig *config) {
  return config && InternalTemperatureSensorConfig_IsRangeValid(config->range_min,config->range_max) &&
      config->polling_interval_ms >= CPU_POLL_MIN_MS && config->polling_interval_ms <= CPU_POLL_MAX_MS &&
      InternalTemperatureSensorConfig_IsOffsetValid(config->temp_offset) && isfinite(config->ema_alpha) && config->ema_alpha >= 0.0f && config->ema_alpha <= 1.0f &&
      isfinite(config->warning_threshold) && isfinite(config->critical_threshold) && isfinite(config->emergency_threshold) &&
      config->warning_threshold < config->critical_threshold && config->critical_threshold < config->emergency_threshold &&
      isfinite(config->rate_alert_threshold_c_per_min) && config->rate_alert_threshold_c_per_min > 0.0f;
}

bool InternalTemperatureSensorConfig_IsRangeValid(int16_t minimum,int16_t maximum) {
  if (minimum>=maximum) return false;
  for (unsigned i=0;i<TEMPERATURE_SENSOR_ATTR_RANGE_NUM;++i) {
    if (minimum>=temperature_sensor_attributes[i].range_min && maximum<=temperature_sensor_attributes[i].range_max) return true;
  }
  return false;
}

bool InternalTemperatureSensorConfig_IsOffsetValid(float offset) {
  return isfinite(offset) && offset >= CALIBRATION_MIN && offset <= CALIBRATION_MAX;
}
