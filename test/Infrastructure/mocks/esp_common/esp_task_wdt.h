#ifndef ESP_TASK_WDT_H
#define ESP_TASK_WDT_H

#include <stdint.h>
#include <stdbool.h>

typedef struct esp_task_wdt_user_handle_s *esp_task_wdt_user_handle_t;

typedef struct {
    uint32_t timeout_ms;
    uint32_t idle_core_mask;
    bool trigger_panic;
} esp_task_wdt_config_t;

#endif /* ESP_TASK_WDT_H */
