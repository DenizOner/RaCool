#ifndef TEST_OLED_SDK_H
#define TEST_OLED_SDK_H
#include "esp_err.h"
#include "driver/i2c_master.h"
#include <stdbool.h>
#include <stddef.h>
typedef void *esp_lcd_panel_handle_t;
typedef void *esp_lcd_panel_io_handle_t;
typedef struct { uint32_t dev_addr; uint32_t scl_speed_hz; size_t control_phase_bytes; int lcd_cmd_bits; int lcd_param_bits; int dc_bit_offset; } esp_lcd_panel_io_i2c_config_t;
typedef struct { uint8_t height; } esp_lcd_panel_ssd1306_config_t;
typedef struct { int bits_per_pixel; int reset_gpio_num; void *vendor_config; } esp_lcd_panel_dev_config_t;
esp_err_t esp_lcd_new_panel_io_i2c(i2c_master_bus_handle_t, const esp_lcd_panel_io_i2c_config_t *, esp_lcd_panel_io_handle_t *);
esp_err_t esp_lcd_new_panel_ssd1306(esp_lcd_panel_io_handle_t, const esp_lcd_panel_dev_config_t *, esp_lcd_panel_handle_t *);
esp_err_t esp_lcd_panel_reset(esp_lcd_panel_handle_t);
esp_err_t esp_lcd_panel_init(esp_lcd_panel_handle_t);
esp_err_t esp_lcd_panel_mirror(esp_lcd_panel_handle_t, bool, bool);
esp_err_t esp_lcd_panel_disp_on_off(esp_lcd_panel_handle_t, bool);
esp_err_t esp_lcd_panel_invert_color(esp_lcd_panel_handle_t, bool);
esp_err_t esp_lcd_panel_io_tx_param(esp_lcd_panel_io_handle_t, int, const void *, size_t);
esp_err_t esp_lcd_panel_draw_bitmap(esp_lcd_panel_handle_t, int, int, int, int, const void *);
esp_err_t esp_lcd_panel_del(esp_lcd_panel_handle_t);
esp_err_t esp_lcd_panel_io_del(esp_lcd_panel_io_handle_t);
#endif
