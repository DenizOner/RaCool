#ifndef MOCK_ESP_ZIGBEE_H
#define MOCK_ESP_ZIGBEE_H

#include "esp_common/esp_err.h"
#include <stdbool.h>
#include <stdint.h>

// Basic types for esp_zigbee stubs
typedef void* esp_zb_zcl_status_t;

typedef struct {
    int dummy;
} ezb_app_signal_t;

static inline bool esp_zigbee_lock_acquire(uint32_t block_ticks) { (void)block_ticks; return true; }
static inline void esp_zigbee_lock_release(void) {}
static inline esp_err_t esp_zigbee_launch_mainloop(void) { return ESP_OK; }
static inline esp_err_t ezb_bdb_start_top_level_commissioning(uint8_t mode) { (void)mode; return ESP_OK; }
static inline esp_err_t ezb_bdb_reset_via_local_action(void) { return ESP_OK; }

#endif // MOCK_ESP_ZIGBEE_H

