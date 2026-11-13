#include "Hardware/RGBLED/RGBLEDHardware.h"
#include "esp_log.h"
#include <driver/ledc.h>
#include <driver/gpio.h>
#include <math.h>
#include "Hardware/LEDAnimations/LEDColorTransfer.h"
#include <limits.h>
#include <esp_rom_gpio.h>

static RGBLEDConfig current_config;
static bool hardware_initialized = false;
static uint8_t prepared_pins, owned_channels, active_fades;
static bool fade_service_owned;
static const ledc_channel_t channel_order[] = {
    RGBLED_LEDC_CHANNEL_G, RGBLED_LEDC_CHANNEL_R, RGBLED_LEDC_CHANNEL_B
};
static int8_t configured_pin(unsigned index) {
    return index == 0 ? current_config.red_pin : index == 1 ? current_config.green_pin : current_config.blue_pin;
}
static void keep_first_error(esp_err_t *result, esp_err_t error) {
    if (*result == ESP_OK) *result = error;
}

/* ──────────────────────────────────────────
 * Private Functions
 * ────────────────────────────────────────── */

/**
 * @brief Calculates PWM duty cycle based on raw color value and master brightness.
 * 
 * @param color_val Raw color component (0-255).
 * @param brightness Master brightness (0-255).
 * @return uint32_t Target duty cycle.
 */
static uint32_t RGBLEDHardware_CalculateDuty(uint8_t color_val, uint8_t brightness) {
#if RGBLED_GAMMA_ENABLED
    uint32_t val = LEDColor_ApplyOutputCurve(color_val);
#else
    uint32_t val = color_val;
#endif

    // Apply linear scaling of brightness
    val = (val * brightness) / 255;

    // Scale to duty resolution
    uint32_t duty = (val * RGBLED_PWM_MAX_DUTY) / 255;

#if RGBLED_COMMON_ANODE
    // Common Anode: Active LOW. Invert the duty cycle.
    // 0 brightness (duty=0) becomes duty=MAX_DUTY (LED off).
    // Max brightness (duty=MAX_DUTY) becomes duty=0 (LED fully on).
    duty = RGBLED_PWM_MAX_DUTY - duty;
#endif

    return duty;
}

/**
 * @brief Configures a single LEDC channel.
 * 
 * @param index Pin/channel slot in the established physical mapping.
 * @return esp_err_t ESP_OK on success, error code otherwise.
 */
static esp_err_t config_ledc_channel(unsigned index) {
    int8_t pin = configured_pin(index);
    if (pin == GPIO_NUM_NC) return ESP_OK;
    esp_err_t err = gpio_reset_pin((gpio_num_t)pin);
    if (err != ESP_OK) return err;
    prepared_pins |= 1U << index;
    esp_rom_gpio_pad_select_gpio((gpio_num_t)pin);
    err = gpio_set_direction((gpio_num_t)pin, GPIO_MODE_OUTPUT);
    if (err != ESP_OK) return err;
    ledc_channel_config_t channel = {
        .channel = channel_order[index], .duty = 0, .gpio_num = pin,
        .speed_mode = LEDC_LOW_SPEED_MODE, .hpoint = 0,
        .timer_sel = RGBLED_LEDC_TIMER, .flags.output_invert = 0
    };
    /* Retain cleanup ownership even if SDK channel setup only partly succeeds. */
    owned_channels |= 1U << index;
    return ledc_channel_config(&channel);
}

/* ──────────────────────────────────────────
 * Public Functions
 * ────────────────────────────────────────── */

esp_err_t RGBLEDHardware_Init(const RGBLEDConfig *config) {
    if (!config || !config->frequency || config->resolution != RGBLED_PWM_RESOLUTION_BITS ||
        config->red_pin < GPIO_NUM_NC || config->green_pin < GPIO_NUM_NC || config->blue_pin < GPIO_NUM_NC)
        return ESP_ERR_INVALID_ARG;
    if ((config->red_pin >= 0 && (config->red_pin == config->green_pin || config->red_pin == config->blue_pin)) ||
        (config->green_pin >= 0 && config->green_pin == config->blue_pin)) return ESP_ERR_INVALID_ARG;
    if (hardware_initialized) {
        return config->red_pin == current_config.red_pin && config->green_pin == current_config.green_pin &&
               config->blue_pin == current_config.blue_pin && config->frequency == current_config.frequency &&
               config->resolution == current_config.resolution ? ESP_OK : ESP_ERR_INVALID_STATE;
    }
    if (prepared_pins || owned_channels || active_fades || fade_service_owned) return ESP_ERR_INVALID_STATE;
    current_config = *config;
    ledc_timer_config_t timer = {
        .duty_resolution = LEDC_TIMER_8_BIT, .freq_hz = config->frequency,
        .speed_mode = LEDC_LOW_SPEED_MODE, .timer_num = RGBLED_LEDC_TIMER, .clk_cfg = LEDC_AUTO_CLK
    };
    esp_err_t err = ledc_timer_config(&timer);
    if (err != ESP_OK) return err;
    /* Existing physical mapping: red-labelled pin drives G; green-labelled pin drives R. */
    for (unsigned i = 0; i < 3; ++i) {
        err = config_ledc_channel(i);
        if (err != ESP_OK) goto rollback;
    }
    err = ledc_fade_func_install(0);
    if (err == ESP_OK) fade_service_owned = true;
    else if (err != ESP_ERR_INVALID_STATE) goto rollback;
    hardware_initialized = true;
    err = RGBLEDHardware_TurnOff();
    if (err == ESP_OK) return ESP_OK;
rollback:
    (void)RGBLEDHardware_Deinit();
    return err;
}

esp_err_t RGBLEDHardware_Deinit(void) {
    hardware_initialized = false;
    esp_err_t result = ESP_OK;
    for (unsigned i = 0; i < 3; ++i) {
        uint8_t bit = 1U << i;
        esp_err_t err;
        if (active_fades & bit) {
            err = ledc_fade_stop(LEDC_LOW_SPEED_MODE, channel_order[i]);
            keep_first_error(&result, err);
            if (err != ESP_OK) continue;
            active_fades &= (uint8_t)~bit;
        }
        if (owned_channels & bit) {
            err = ledc_stop(LEDC_LOW_SPEED_MODE, channel_order[i], RGBLED_COMMON_ANODE ? 1 : 0);
            keep_first_error(&result, err);
            if (err != ESP_OK) continue;
            owned_channels &= (uint8_t)~bit;
        }
        if (prepared_pins & bit) {
            err = gpio_reset_pin((gpio_num_t)configured_pin(i));
            keep_first_error(&result, err);
            if (err == ESP_OK) prepared_pins &= (uint8_t)~bit;
        }
    }
    if (!prepared_pins && !owned_channels && !active_fades && fade_service_owned) {
        ledc_fade_func_uninstall();
        fade_service_owned = false;
    }
    return result;
}

static esp_err_t apply_channel(unsigned i, uint32_t duty, uint32_t fade_ms) {
    uint8_t bit = 1U << i;
    if (!(owned_channels & bit)) return ESP_OK;
    ledc_channel_t channel = channel_order[i];
    esp_err_t err;
    if (active_fades & bit) {
        err = ledc_fade_stop(LEDC_LOW_SPEED_MODE, channel);
        if (err != ESP_OK) return err;
        active_fades &= (uint8_t)~bit;
    }
    if (fade_ms) {
        err = ledc_set_fade_with_time(LEDC_LOW_SPEED_MODE, channel, duty, (int)fade_ms);
        if (err != ESP_OK) return err;
        active_fades |= bit;
        return ledc_fade_start(LEDC_LOW_SPEED_MODE, channel, LEDC_FADE_NO_WAIT);
    }
    err = ledc_set_duty(LEDC_LOW_SPEED_MODE, channel, duty);
    return err == ESP_OK ? ledc_update_duty(LEDC_LOW_SPEED_MODE, channel) : err;
}

static esp_err_t apply_color(RGBColor color, uint8_t brightness, uint32_t fade_ms) {
    if (!hardware_initialized) return ESP_ERR_INVALID_STATE;
    if (fade_ms > INT_MAX) return ESP_ERR_INVALID_ARG;
    /* Preserve the established logical R/G/B write ordering. */
    const unsigned order[] = {1, 0, 2};
    const uint8_t components[] = {color.g, color.r, color.b};
    esp_err_t result = ESP_OK;
    for (unsigned n = 0; n < 3; ++n) {
        unsigned i = order[n];
        keep_first_error(&result, apply_channel(i, RGBLEDHardware_CalculateDuty(components[i], brightness), fade_ms));
    }
    return result;
}

esp_err_t RGBLEDHardware_SetColorImmediate(RGBColor color, uint8_t brightness) {
    return apply_color(color, brightness, 0);
}
esp_err_t RGBLEDHardware_FadeToColor(RGBColor color, uint8_t brightness, uint32_t fade_ms) {
    return apply_color(color, brightness, fade_ms);
}
esp_err_t RGBLEDHardware_TurnOff(void) {
    return RGBLEDHardware_SetColorImmediate((RGBColor){0, 0, 0}, 0);
}
