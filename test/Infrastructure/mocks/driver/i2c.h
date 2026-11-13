#ifndef MOCK_DRIVER_I2C_H
#define MOCK_DRIVER_I2C_H

#include <esp_err.h>
#include <stdint.h>

typedef enum {
    I2C_NUM_0 = 0,
    I2C_NUM_1 = 1,
    I2C_NUM_MAX
} i2c_port_t;

typedef enum {
    I2C_MODE_MASTER = 0,
    I2C_MODE_SLAVE = 1,
} i2c_mode_t;

typedef struct {
    i2c_mode_t mode;
    int sda_io_num;
    int scl_io_num;
    bool sda_pullup_en;
    bool scl_pullup_en;
    struct {
        uint32_t clk_speed;
    } master;
} i2c_config_t;

esp_err_t i2c_param_config(i2c_port_t i2c_num, const i2c_config_t *i2c_conf);
esp_err_t i2c_driver_install(i2c_port_t i2c_num, i2c_mode_t mode, size_t slv_rx_buf_len, size_t slv_tx_buf_len, int intr_alloc_flags);

#endif
