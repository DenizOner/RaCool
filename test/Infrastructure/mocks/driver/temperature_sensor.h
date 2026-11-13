#ifndef MOCK_TEMPERATURE_SENSOR_H
#define MOCK_TEMPERATURE_SENSOR_H
#include "esp_err.h"
typedef struct temperature_sensor_obj_t *temperature_sensor_handle_t;
typedef struct { int range_min; int range_max; int clk_src; } temperature_sensor_config_t;
#define TEMPERATURE_SENSOR_CONFIG_DEFAULT(low, high) { .range_min=(low), .range_max=(high), .clk_src=0 }
esp_err_t temperature_sensor_install(const temperature_sensor_config_t *, temperature_sensor_handle_t *);
esp_err_t temperature_sensor_uninstall(temperature_sensor_handle_t);
esp_err_t temperature_sensor_enable(temperature_sensor_handle_t);
esp_err_t temperature_sensor_disable(temperature_sensor_handle_t);
esp_err_t temperature_sensor_get_celsius(temperature_sensor_handle_t, float *);
#endif
