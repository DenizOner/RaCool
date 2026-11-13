#include "Hardware/OLED/OLEDConfig.h"
#include "Core/ServiceLocator/ServiceLocator.h"
#include "Core/ServiceLocator/ServiceNames.h"
#include "Hardware/PinManager/IPinManager.h"
#include "esp_log.h"


static const char *TAG = "OLEDConfig";

OLEDConfig OLED_GetDefaultConfig() {
  OLEDConfig config = {.address = 0x3C, // Common I2C address for SSD1306
                       .width = OLED_WIDTH,    // Common width for SSD1306
                       .height = OLED_HEIGHT,    // Common height for SSD1306
                       .sda_pin = GPIO_NUM_NC,
                       .scl_pin = GPIO_NUM_NC,
                       .contrast = 0x7F,
                       .flip_screen = false};

  IPinManager *pinMgr =
      (IPinManager *)ServiceLocator_GetService(SVC_NAME_PIN_MANAGER);
  if (pinMgr) {
    config.scl_pin = pinMgr->GetPin(pinMgr, PIN_FUNC_OLED_SCL);
    config.sda_pin = pinMgr->GetPin(pinMgr, PIN_FUNC_OLED_SDA);
  }

  ESP_LOGI(TAG,
           "[OLED_GetDefaultConfig] Default OLED config: address=0x%02X, "
           "width=%d, height=%d, sda=%d, scl=%d",
           config.address, config.width, config.height, config.sda_pin,
           config.scl_pin);

  return config;
}
