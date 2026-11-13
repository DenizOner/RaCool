/**
 * @file RGBLEDConfig.h
 * @brief Configuration constants and structures for the RGB LED module.
 */

#ifndef RGBLED_CONFIG_H
#define RGBLED_CONFIG_H

#include <stdint.h>

/* sRGB D65 preset chromaticities, ZCL coordinates scaled by 65536. */
#define RGBLED_PRESET_BLUE_X 9831u
#define RGBLED_PRESET_BLUE_Y 3933u
#define RGBLED_PRESET_CYAN_X 14723u
#define RGBLED_PRESET_CYAN_Y 21544u
#define RGBLED_PRESET_GREEN_X 19661u
#define RGBLED_PRESET_GREEN_Y 39322u
#define RGBLED_PRESET_YELLOW_X 27481u
#define RGBLED_PRESET_YELLOW_Y 33112u
#define RGBLED_PRESET_ORANGE_X 35587u
#define RGBLED_PRESET_ORANGE_Y 26675u
#define RGBLED_PRESET_RED_X 41948u
#define RGBLED_PRESET_RED_Y 21625u
#define RGBLED_PRESET_PURPLE_X 21032u
#define RGBLED_PRESET_PURPLE_Y 10103u
#define RGBLED_PRESET_WHITE_X 20494u
#define RGBLED_PRESET_WHITE_Y 21561u

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief RGB LED hardware configuration structure.
 */
typedef struct {
  int8_t red_pin;        /**< GPIO pin for the Red LED channel. */
  int8_t green_pin;      /**< GPIO pin for the Green LED channel. */
  int8_t blue_pin;       /**< GPIO pin for the Blue LED channel. */
  uint32_t frequency;    /**< PWM frequency in Hz. */
  uint8_t resolution;    /**< PWM duty resolution in bits. */
} RGBLEDConfig;

/* ──────────────────────────────────────────
 * PWM Constants
 * ────────────────────────────────────────── */

#define RGBLED_PWM_FREQ_HZ          5000    /**< Default PWM frequency. */
#define RGBLED_PWM_RESOLUTION_BITS  8       /**< Default PWM resolution. */
#define RGBLED_PWM_MAX_DUTY         ((1 << RGBLED_PWM_RESOLUTION_BITS) - 1)

/* ──────────────────────────────────────────
 * LEDC Channel/Timer Assignment
 * ────────────────────────────────────────── */

#define RGBLED_LEDC_TIMER           0       /**< LEDC_TIMER_0 (Shared with BuiltinLED for compatibility) */
#define RGBLED_LEDC_CHANNEL_R       0       /**< LEDC_CHANNEL_0 */
#define RGBLED_LEDC_CHANNEL_G       1       /**< LEDC_CHANNEL_1 */
#define RGBLED_LEDC_CHANNEL_B       2       /**< LEDC_CHANNEL_2 */

/* ──────────────────────────────────────────
 * Temperature Threshold Constants (Default)
 * ────────────────────────────────────────── */

#define RGBLED_TEMP_COLD_DEFAULT    30.0f   /**< Radiator cold/off threshold (°C) */
#define RGBLED_TEMP_WARM_DEFAULT    50.0f   /**< Radiator warm-zone threshold (°C) */
#define RGBLED_TEMP_HOT_DEFAULT     70.0f   /**< Radiator hot threshold (°C) */

/* ──────────────────────────────────────────
 * Fade & Animation Constants
 * ────────────────────────────────────────── */

#define RGBLED_FADE_DEFAULT_MS      300     /**< Default transition duration (ms) */
#define RGBLED_FADE_FAST_MS         100     /**< Fast transition duration (ms) */
#define RGBLED_FADE_SLOW_MS         1000    /**< Slow transition duration (ms) */
#define RGBLED_ANIM_TICK_MS         20      /**< Period of animation engine updates (ms) */
#define RGBLED_BREATHING_PERIOD_MS  LED_PATTERN_BREATHING_PERIOD_MS    /**< Period of breathing animation (ms) */
#define RGBLED_PULSE_PERIOD_MS      LED_PATTERN_PULSE_PERIOD_MS     /**< Period of pulse cycle (ms) */
#define RGBLED_ALERT_PERIOD_MS      LED_PATTERN_ALERT_PERIOD_MS     /**< Period of alert blinking (ms) */

/* ──────────────────────────────────────────
 * FreeRTOS Task Constants
 * ────────────────────────────────────────── */

#define RGBLED_TASK_STACK_SIZE      12288    /**< Stack size for the main RGBLED task (bytes) */
#define RGBLED_COLOR_PREVIEW_MS 3000u
#define RGBLED_COLOR_PREVIEW_MAX_MS 10000u

#define RGBLED_TASK_PRIORITY        5       /**< Priority of the RGBLED task */
#define RGBLED_CMD_QUEUE_LENGTH     32      /**< Command queue capacity increased to prevent packet drops */

/* ──────────────────────────────────────────
 * Gamma Correction
 * ────────────────────────────────────────── */

#define RGBLED_GAMMA_ENABLED        1       /**< Enable/Disable gamma correction */
#define RGBLED_GAMMA_VALUE          2.2f    /**< Gamma exponent */

/* ──────────────────────────────────────────
 * LED Type Configuration
 * ────────────────────────────────────────── */

/**
 * @brief Set to 1 if the RGB LED module is Common Anode type.
 *
 * Common Anode: shared pin connects to VCC; R/G/B pins sink current (active LOW).
 * PWM output must be inverted: higher duty = brighter LED.
 *
 * Set to 0 for Common Cathode (active HIGH, no inversion needed).
 */
#define RGBLED_COMMON_ANODE         0

/* ──────────────────────────────────────────
 * Public Function Declarations
 * ────────────────────────────────────────── */

/**
 * @brief Resolves default pins and settings via PinManager.
 * 
 * @return RGBLEDConfig Default hardware configuration.
 */
RGBLEDConfig RGBLED_GetDefaultConfig(void);

#ifdef __cplusplus
}
#endif

#endif // RGBLED_CONFIG_H
