#ifndef I2C_MASTER_H
#define I2C_MASTER_H

#include "esp_err.h"

typedef int i2c_port_t;
#define I2C_CLK_SRC_DEFAULT 0
#define I2C_NUM_0 0
#define I2C_NUM_1 1
#define I2C_NUM_MAX 2

typedef struct {
    int i2c_port;
    int sda_io_num;
    int scl_io_num;
    uint32_t clk_source;
    uint8_t glitch_ignore_cnt;
    struct {
        unsigned int enable_internal_pullup: 1;
    } flags;
} i2c_master_bus_config_t;

typedef struct {
    uint16_t dev_addr_length;
    uint16_t device_address;
    uint32_t scl_speed_hz;
    struct {
        unsigned int disable_ack_check: 1;
    } flags;
} i2c_device_config_t;

typedef void *i2c_master_bus_handle_t;
typedef void *i2c_master_dev_handle_t;

#endif /* I2C_MASTER_H */
