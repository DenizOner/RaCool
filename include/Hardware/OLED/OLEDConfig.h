#ifndef OLED_CONFIG_H
#define OLED_CONFIG_H

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief OLED Configuration Structure
 *
 * This structure holds the configuration parameters for the OLED display.
 */
typedef struct {
  uint8_t address;     /**< I2C address of the OLED display */
  uint8_t width;       /**< Width of the OLED display in pixels */
  uint8_t height;      /**< Height of the OLED display in pixels */
  uint8_t sda_pin;     /**< SDA pin for I2C communication */
  uint8_t scl_pin;     /**< SCL pin for I2C communication */
  uint8_t contrast;    /**< Initial contrast level (0-255) */
  bool flip_screen;    /**< True if screen should be flipped 180 degrees */
} OLEDConfig;

/**
 * @brief Default OLED Configuration
 *
 * This function returns the default configuration for the OLED display.
 *
 * @return OLEDConfig Default configuration
 */
OLEDConfig OLED_GetDefaultConfig();

// --- OLED Constants ---
#define OLED_WIDTH 128
#define OLED_HEIGHT 64
#define OLED_I2C_CLOCK_HZ 100000
#define OLED_DEFAULT_CONTRAST 0x7F
#define OLED_I2C_TIMEOUT_MS 100
#define OLED_CONTROL_TIMEOUT_MS 2000
#define OLED_TASK_STACK_SIZE 8192
#include "Core/TaskPriorities.h"
#define OLED_TASK_PRIORITY RACOOL_TASK_PRIORITY_DISPLAY

#endif // OLED_CONFIG_H
