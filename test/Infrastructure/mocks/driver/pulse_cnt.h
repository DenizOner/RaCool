#ifndef TEST_PULSE_CNT_H
#define TEST_PULSE_CNT_H
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
typedef void *pcnt_unit_handle_t;
typedef void *pcnt_channel_handle_t;
typedef enum { PCNT_CHANNEL_EDGE_ACTION_HOLD, PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_DECREASE } pcnt_channel_edge_action_t;
typedef enum { PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE, PCNT_CHANNEL_LEVEL_ACTION_HOLD } pcnt_channel_level_action_t;
typedef struct { int watch_point_value; int zero_cross_mode; } pcnt_watch_event_data_t;
typedef bool (*pcnt_watch_cb_t)(pcnt_unit_handle_t, const pcnt_watch_event_data_t *, void *);
typedef struct { pcnt_watch_cb_t on_reach; } pcnt_event_callbacks_t;
typedef struct { int low_limit; int high_limit; int intr_priority; struct { uint32_t accum_count:1; } flags; } pcnt_unit_config_t;
typedef struct { int edge_gpio_num; int level_gpio_num; } pcnt_chan_config_t;
typedef struct { uint32_t max_glitch_ns; } pcnt_glitch_filter_config_t;
esp_err_t pcnt_new_unit(const pcnt_unit_config_t *, pcnt_unit_handle_t *);
esp_err_t pcnt_new_channel(pcnt_unit_handle_t, const pcnt_chan_config_t *, pcnt_channel_handle_t *);
esp_err_t pcnt_unit_set_glitch_filter(pcnt_unit_handle_t, const pcnt_glitch_filter_config_t *);
esp_err_t pcnt_channel_set_edge_action(pcnt_channel_handle_t, pcnt_channel_edge_action_t, pcnt_channel_edge_action_t);
esp_err_t pcnt_channel_set_level_action(pcnt_channel_handle_t, pcnt_channel_level_action_t, pcnt_channel_level_action_t);
esp_err_t pcnt_unit_add_watch_point(pcnt_unit_handle_t, int);
esp_err_t pcnt_unit_register_event_callbacks(pcnt_unit_handle_t, const pcnt_event_callbacks_t *, void *);
esp_err_t pcnt_unit_enable(pcnt_unit_handle_t);
esp_err_t pcnt_unit_clear_count(pcnt_unit_handle_t);
esp_err_t pcnt_unit_start(pcnt_unit_handle_t);
esp_err_t pcnt_unit_stop(pcnt_unit_handle_t);
esp_err_t pcnt_unit_disable(pcnt_unit_handle_t);
esp_err_t pcnt_del_channel(pcnt_channel_handle_t);
esp_err_t pcnt_del_unit(pcnt_unit_handle_t);
#endif
