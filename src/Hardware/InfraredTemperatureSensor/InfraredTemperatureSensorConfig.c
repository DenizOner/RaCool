#include "Hardware/InfraredTemperatureSensor/InfraredTemperatureSensorConfig.h"
#include <esp_log.h>

static const char *TAG_CFG = "IRTempConfig";

InfraredTemperatureSensorConfig IRTempSensor_GetDefaultConfig(void) {
  InfraredTemperatureSensorConfig config = {
      .i2c_freq_hz = 100000,
      .polling_interval_ms = 100,         /* MLX90614 refresh cycle (~100ms) */
      .error_temperature = -273.15f,
      .temp_range_min = -70.0f,
      .temp_range_max = 380.0f,
      .event_delta_threshold = 0.1f,      /* Publish if temp changes >= 0.1°C */
      .max_publish_interval_ms = 5000,    /* Force-publish every 5s at minimum,
                                           * regardless of delta — prevents
                                           * ClimateController stale detection. */
      .ema_alpha = 0.20f,
      .i2c_address = 0x5A,
      .reg_ambient_temp = 0x06,
      .reg_object_temp = 0x07,
      .i2c_port = 0, /* I2C_NUM_0 */
  };

  ESP_LOGI(TAG_CFG,
           "[IRTempSensor_GetDefaultConfig] addr=0x%02X, port=%d, freq=%lu, "
           "range=[%.1f, %.1f], polling=%lu ms, alpha=%.2f, delta=%.2f",
           config.i2c_address, config.i2c_port,
           (unsigned long)config.i2c_freq_hz,
           config.temp_range_min,
           config.temp_range_max, (unsigned long)config.polling_interval_ms,
           config.ema_alpha, config.event_delta_threshold);

  return config;
}
