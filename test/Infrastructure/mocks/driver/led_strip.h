/**
 * @file led_strip.h
 * @brief Native-test mock for the ESP-IDF led_strip driver.
 *
 * Provides type stubs and function declarations so that BuiltinLED.c can be
 * compiled with gcc in the native test environment without the full ESP-IDF.
 * The actual function implementations are supplied by test_BuiltinLED.c.
 */

#ifndef LED_STRIP_H
#define LED_STRIP_H

#include <stdint.h>
#include "esp_err.h"

/* Opaque handle used by the driver. */
typedef void *led_strip_handle_t;

/* LED model selection. */
typedef enum {
    LED_MODEL_WS2812 = 0,
    LED_MODEL_SK6812 = 1,
} led_strip_model_t;

/* Color component byte order. */
typedef enum {
    LED_STRIP_COLOR_COMPONENT_FMT_GRB = 0,
    LED_STRIP_COLOR_COMPONENT_FMT_RGB = 1,
} led_strip_color_format_t;

/* SPI host identifier (matches the ESP-IDF spi_host_device_t values). */
typedef enum {
    SPI1_HOST = 0,
    SPI2_HOST = 1,
    SPI3_HOST = 2,
} spi_host_device_t;

/* Strip configuration passed to led_strip_new_spi_device(). */
typedef struct {
    uint32_t             strip_gpio_num;
    uint32_t             max_leds;
    led_strip_model_t    led_model;
    led_strip_color_format_t color_component_format;
    struct { uint32_t invert_out : 1; } flags;
} led_strip_config_t;

/* SPI-specific configuration. */
typedef struct {
    spi_host_device_t spi_bus;
    struct { uint32_t with_dma : 1; } flags;
} led_strip_spi_config_t;

/* Function declarations — implementations provided by the test fixture. */
esp_err_t led_strip_new_spi_device(const led_strip_config_t   *config,
                                    const led_strip_spi_config_t *spi_config,
                                    led_strip_handle_t           *ret_strip);

esp_err_t led_strip_set_pixel(led_strip_handle_t strip, uint32_t index,
                               uint32_t red, uint32_t green, uint32_t blue);

esp_err_t led_strip_refresh(led_strip_handle_t strip);
esp_err_t led_strip_clear(led_strip_handle_t strip);
esp_err_t led_strip_del(led_strip_handle_t strip);

#endif /* LED_STRIP_H */
