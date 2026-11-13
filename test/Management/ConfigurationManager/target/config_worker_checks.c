#include "unity.h"
#include "nvs.h"
#include <stdatomic.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
static SemaphoreHandle_t save_release;
static atomic_bool save_entered;
static atomic_uint saved_blocks;
static atomic_bool fail_save;
static esp_err_t ConfigTarget_Open(const char *name, nvs_open_mode_t mode, nvs_handle_t *handle) {
    (void)name;
    if (mode == NVS_READONLY) return ESP_ERR_NVS_NOT_FOUND;
    *handle = 1; return ESP_OK;
}
static esp_err_t ConfigTarget_SetBlob(nvs_handle_t handle, const char *key, const void *data, size_t size) {
    (void)handle; (void)key; (void)data; (void)size;
    atomic_store(&save_entered, true);
    if (save_release) {
        xSemaphoreTake(save_release, portMAX_DELAY);
        xSemaphoreGive(save_release);
    }
    atomic_fetch_add(&saved_blocks, 1);
    return atomic_load(&fail_save) ? ESP_FAIL : ESP_OK;
}
static esp_err_t ConfigTarget_Commit(nvs_handle_t handle) { (void)handle; return ESP_OK; }
static void ConfigTarget_Close(nvs_handle_t handle) { (void)handle; }
#define nvs_open ConfigTarget_Open
#define nvs_set_blob ConfigTarget_SetBlob
#define nvs_commit ConfigTarget_Commit
#define nvs_close ConfigTarget_Close
#define GetEventBus ConfigTarget_GetEventBus
#define DeveloperMode_SetEnabled ConfigTarget_DeveloperSetEnabled
#include "../../../../src/Management/ConfigurationManager/ConfigurationManager.c"
#undef DeveloperMode_SetEnabled
#undef GetEventBus
#undef nvs_open
#undef nvs_set_blob
#undef nvs_commit
#undef nvs_close
IEventBus *ConfigTarget_GetEventBus(void) { return NULL; }
void ConfigTarget_DeveloperSetEnabled(bool enabled) { (void)enabled; }
static bool initialized;
void target_config_worker_cleanup(void) {
    if (save_release) xSemaphoreGive(save_release);
    if (initialized) TEST_ASSERT_EQUAL(0, config_stop(&s_mgr.base.base));
    if (save_release) { vSemaphoreDelete(save_release); save_release = NULL; }
}
void target_config_worker_verify_shutdown(void) {
    IConfigurationManager *config = ConfigurationManager_GetInstance();
    TEST_ASSERT_NOT_NULL(config);
    initialized = true;
    save_release = xSemaphoreCreateBinary();
    TEST_ASSERT_NOT_NULL(save_release);
    atomic_store(&save_entered, false);
    atomic_store(&saved_blocks, 0);
    atomic_store(&fail_save, false);
    TEST_ASSERT_EQUAL(0, config_start(&config->base));
    char block[] = "control";
    TEST_ASSERT_EQUAL(ESP_OK, config->SaveAsync(config, block));
    memset(block, 'x', sizeof(block) - 1);
    for (unsigned i = 0; i < 100 && !atomic_load(&save_entered); ++i) vTaskDelay(pdMS_TO_TICKS(10));
    TEST_ASSERT_TRUE(atomic_load(&save_entered));
    for (unsigned i = 0; i < CONFIG_ASYNC_QUEUE_LENGTH; ++i)
        TEST_ASSERT_EQUAL(ESP_OK, config->SaveAsync(config, "system"));
    TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT, config->SaveAsync(config, "system"));
    TaskHandle_t task = atomic_load(&s_mgr.worker.task);
    QueueHandle_t queue = s_mgr.requests;
    TEST_ASSERT_NOT_EQUAL(0, config_stop(&config->base));
    TEST_ASSERT_EQUAL_PTR(task, atomic_load(&s_mgr.worker.task));
    TEST_ASSERT_EQUAL_PTR(queue, s_mgr.requests);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, config->SaveAsync(config, NULL));
    TEST_ASSERT_NOT_EQUAL(0, config_start(&config->base));
    TEST_ASSERT_NOT_EQUAL(0, config_initialize(&config->base, NULL));
    xSemaphoreGive(save_release);
    TEST_ASSERT_EQUAL(0, config_stop(&config->base));
    TEST_ASSERT_NULL(atomic_load(&s_mgr.worker.task));
    TEST_ASSERT_EQUAL(1 + CONFIG_ASYNC_QUEUE_LENGTH, atomic_load(&saved_blocks));
    TEST_ASSERT_EQUAL(0, config_start(&config->base));
    on_restart_req_event((BusEvent){0});
    TEST_ASSERT_EQUAL(0, config_stop(&config->base));
    TEST_ASSERT_TRUE(s_mgr.settings.restart_required);
    TEST_ASSERT_EQUAL(1 + CONFIG_ASYNC_QUEUE_LENGTH, atomic_load(&saved_blocks));
    TEST_ASSERT_EQUAL(0, config_start(&config->base));
    atomic_store(&fail_save, true);
    TEST_ASSERT_EQUAL(ESP_OK, config->SaveAsync(config, "system"));
    for (unsigned i = 0; i < 100 && atomic_load(&s_mgr.asyncError) == ESP_OK; ++i) vTaskDelay(pdMS_TO_TICKS(10));
    TEST_ASSERT_NOT_EQUAL(0, config_check_health(&config->base));
    atomic_store(&fail_save, false);
    TEST_ASSERT_EQUAL(ESP_OK, config->SaveAsync(config, "system"));
    TEST_ASSERT_EQUAL(0, config_stop(&config->base));
    TEST_ASSERT_EQUAL(ESP_OK, atomic_load(&s_mgr.asyncError));
}
