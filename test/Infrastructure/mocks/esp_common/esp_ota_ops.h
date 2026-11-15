#ifndef RACOOL_MOCK_ESP_OTA_OPS_H
#define RACOOL_MOCK_ESP_OTA_OPS_H
#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
typedef struct esp_partition_t { unsigned int size; int sentinel; } esp_partition_t;
typedef unsigned int esp_ota_handle_t;
const esp_partition_t *esp_ota_get_next_update_partition(const esp_partition_t *);
esp_err_t esp_ota_begin(const esp_partition_t *, size_t, esp_ota_handle_t *);
esp_err_t esp_ota_write(esp_ota_handle_t, const void *, size_t);
esp_err_t esp_ota_end(esp_ota_handle_t);
esp_err_t esp_ota_set_boot_partition(const esp_partition_t *);
esp_err_t esp_ota_abort(esp_ota_handle_t);
typedef enum {
    ESP_OTA_IMG_NEW = 0, ESP_OTA_IMG_PENDING_VERIFY,
    ESP_OTA_IMG_VALID, ESP_OTA_IMG_INVALID,
    ESP_OTA_IMG_ABORTED, ESP_OTA_IMG_UNDEFINED
} esp_ota_img_states_t;
const esp_partition_t *esp_ota_get_running_partition(void);
esp_err_t esp_ota_get_state_partition(const esp_partition_t *, esp_ota_img_states_t *);
bool esp_ota_check_rollback_is_possible(void);
esp_err_t esp_ota_mark_app_valid_cancel_rollback(void);
esp_err_t esp_ota_mark_app_invalid_rollback_and_reboot(void);
#endif
