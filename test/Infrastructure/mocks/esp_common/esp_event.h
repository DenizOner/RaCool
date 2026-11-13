#ifndef ESP_EVENT_H
#define ESP_EVENT_H

#include <stdint.h>
#include "esp_err.h"

typedef void* esp_event_base_t;
typedef void (*esp_event_handler_t)(void* event_handler_arg, esp_event_base_t event_base, int32_t event_id, void* event_data);

#endif /* ESP_EVENT_H */
