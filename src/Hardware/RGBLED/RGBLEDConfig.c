#include "Hardware/RGBLED/RGBLEDConfig.h"
#include "Core/DI/IDIRegistry.h"
#include "Hardware/PinManager/IPinManager.h"
#include "Core/ServiceLocator/ServiceLocator.h"
#include "Core/ServiceLocator/ServiceNames.h"
#include "Management/ConfigurationManager/SystemConfigs.h"
#include <esp_log.h>

static const char *TAG = "RGBLEDConfig";

RGBLEDConfig RGBLED_GetDefaultConfig(void) {
    // Start with hardcoded defaults from SystemConfigs as a safe fallback.
    // These match the physical board wiring (GPIO 2 / 10 / 11).
    RGBLEDConfig config = {
        .red_pin   = (gpio_num_t)RGB_RED_PIN,
        .green_pin = (gpio_num_t)RGB_GREEN_PIN,
        .blue_pin  = (gpio_num_t)RGB_BLUE_PIN,
        .frequency = RGBLED_PWM_FREQ_HZ,
        .resolution = RGBLED_PWM_RESOLUTION_BITS
    };

    // Attempt to override with PinManager values (runtime-resolved).
    IPinManager *pinManager = (IPinManager*)ServiceLocator_GetService(SVC_NAME_PIN_MANAGER);
    if (pinManager) {
        int r_pin = pinManager->GetPin(pinManager, PIN_FUNC_RGB_RED);
        int g_pin = pinManager->GetPin(pinManager, PIN_FUNC_RGB_GREEN);
        int b_pin = pinManager->GetPin(pinManager, PIN_FUNC_RGB_BLUE);

        // Only override if PinManager returned valid (non-NC) values
        if (r_pin >= 0) config.red_pin   = (gpio_num_t)r_pin;
        if (g_pin >= 0) config.green_pin = (gpio_num_t)g_pin;
        if (b_pin >= 0) config.blue_pin  = (gpio_num_t)b_pin;

        ESP_LOGI(TAG, "Resolved Pins (PinManager) -> R:%d, G:%d, B:%d",
                 config.red_pin, config.green_pin, config.blue_pin);
    } else {
        ESP_LOGW(TAG, "PinManager not found -- using hardcoded fallback R:%d, G:%d, B:%d",
                 config.red_pin, config.green_pin, config.blue_pin);
    }

    return config;
}
