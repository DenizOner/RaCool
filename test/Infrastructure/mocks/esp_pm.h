#ifndef TEST_ESP_PM_H
#define TEST_ESP_PM_H
#include "esp_err.h"
#include <stdbool.h>
typedef struct {
    int max_freq_mhz;
    int min_freq_mhz;
    bool light_sleep_enable;
} esp_pm_config_t;
esp_err_t esp_pm_configure(const void *config);
esp_err_t esp_pm_get_configuration(void *config);
typedef enum { ESP_PM_CPU_FREQ_MAX, ESP_PM_APB_FREQ_MAX, ESP_PM_NO_LIGHT_SLEEP } esp_pm_lock_type_t;
typedef void *esp_pm_lock_handle_t;
esp_err_t esp_pm_lock_create(esp_pm_lock_type_t type, int arg, const char *name, esp_pm_lock_handle_t *handle);
esp_err_t esp_pm_lock_acquire(esp_pm_lock_handle_t handle);
esp_err_t esp_pm_lock_release(esp_pm_lock_handle_t handle);
esp_err_t esp_pm_lock_delete(esp_pm_lock_handle_t handle);
#endif
