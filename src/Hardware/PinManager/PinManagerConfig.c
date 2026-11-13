#include "Hardware/PinManager/PinManagerConfig.h"
#include "Management/ConfigurationManager/SystemConfigs.h"

/**
 * @file PinManagerConfig.c
 * @brief PinManager varsayılan yapılandırma uygulaması.
 */

PinManagerConfig PinManagerConfig_GetDefault(void) {
  PinManagerConfig config;

  // Tüm girişleri başlangıçta NC (Bağlı Değil) olarak ayarla
  for (int i = 0; i < PIN_FUNC_MAX_COUNT; i++) {
    config.entries[i].pin = GPIO_NUM_NC;
    config.entries[i].mode = PIN_MODE_INPUT;
    config.entries[i].pull = PIN_PULL_NONE;
    config.entries[i].initialState = false;
    config.entries[i].debounceMs = 0;
  }

  config.defaultDebounceMs = 50; // Varsayılan 50ms debounce

  // Röle Yapılandırması
  config.entries[PIN_FUNC_RELAY].pin = (gpio_num_t)RELAY_IN_PIN;
  config.entries[PIN_FUNC_RELAY].mode = PIN_MODE_OUTPUT;

  // Dahili LED
  config.entries[PIN_FUNC_BUILTIN_LED].pin = (gpio_num_t)BUILTIN_LED_PIN;
  config.entries[PIN_FUNC_BUILTIN_LED].mode = PIN_MODE_OUTPUT;

  // I2C Sensör (MLX90614)
  config.entries[PIN_FUNC_MLX90614_SCL].pin = (gpio_num_t)MLX90614_SCL_PIN;
  config.entries[PIN_FUNC_MLX90614_SDA].pin = (gpio_num_t)MLX90614_SDA_PIN;

  // OLED Ekran
  config.entries[PIN_FUNC_OLED_SCL].pin = (gpio_num_t)OLED_SCL_PIN;
  config.entries[PIN_FUNC_OLED_SCL].mode = PIN_MODE_OPEN_DRAIN;
  config.entries[PIN_FUNC_OLED_SCL].pull = PIN_PULL_UP;
  config.entries[PIN_FUNC_OLED_SDA].pin = (gpio_num_t)OLED_SDA_PIN;
  config.entries[PIN_FUNC_OLED_SDA].mode = PIN_MODE_OPEN_DRAIN;
  config.entries[PIN_FUNC_OLED_SDA].pull = PIN_PULL_UP;

  // Rotary Encoder
  config.entries[PIN_FUNC_ROTARY_CLK].pin = (gpio_num_t)ROTARY_CLK_PIN;
  config.entries[PIN_FUNC_ROTARY_CLK].mode = PIN_MODE_INPUT;
  config.entries[PIN_FUNC_ROTARY_CLK].pull = PIN_PULL_UP;
  config.entries[PIN_FUNC_ROTARY_CLK].debounceMs = 5;

  config.entries[PIN_FUNC_ROTARY_DT].pin = (gpio_num_t)ROTARY_DT_PIN;
  config.entries[PIN_FUNC_ROTARY_DT].mode = PIN_MODE_INPUT;
  config.entries[PIN_FUNC_ROTARY_DT].pull = PIN_PULL_UP;
  config.entries[PIN_FUNC_ROTARY_DT].debounceMs = 5;

  config.entries[PIN_FUNC_ROTARY_SW].pin = (gpio_num_t)ROTARY_SW_PIN;
  config.entries[PIN_FUNC_ROTARY_SW].mode = PIN_MODE_INPUT;
  config.entries[PIN_FUNC_ROTARY_SW].pull = PIN_PULL_UP;
  config.entries[PIN_FUNC_ROTARY_SW].debounceMs = 50;

  // RGB LED
  config.entries[PIN_FUNC_RGB_RED].pin = (gpio_num_t)RGB_RED_PIN;
  config.entries[PIN_FUNC_RGB_RED].mode = PIN_MODE_OUTPUT;
  config.entries[PIN_FUNC_RGB_GREEN].pin = (gpio_num_t)RGB_GREEN_PIN;
  config.entries[PIN_FUNC_RGB_GREEN].mode = PIN_MODE_OUTPUT;
  config.entries[PIN_FUNC_RGB_BLUE].pin = (gpio_num_t)RGB_BLUE_PIN;
  config.entries[PIN_FUNC_RGB_BLUE].mode = PIN_MODE_OUTPUT;

  // I2C0 Genel Eşleme
  config.entries[PIN_FUNC_I2C0_SCL].pin = (gpio_num_t)OLED_SCL_PIN;
  config.entries[PIN_FUNC_I2C0_SCL].mode = PIN_MODE_OPEN_DRAIN;
  config.entries[PIN_FUNC_I2C0_SCL].pull = PIN_PULL_UP;
  config.entries[PIN_FUNC_I2C0_SDA].pin = (gpio_num_t)OLED_SDA_PIN;
  config.entries[PIN_FUNC_I2C0_SDA].mode = PIN_MODE_OPEN_DRAIN;
  config.entries[PIN_FUNC_I2C0_SDA].pull = PIN_PULL_UP;

  return config;
}
