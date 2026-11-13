#define RACOOL_MOCKS_IMPLEMENTATION
#include "test_mocks.h"

// ---------------------------------------------------------
// 0. FFF GLOBALS
// ---------------------------------------------------------
DEFINE_FFF_GLOBALS;

// ---------------------------------------------------------
// 1. STANDARD LIBS MOCKS
// ---------------------------------------------------------
DEFINE_FAKE_VALUE_FUNC1(char *, mocked_strdup, const char *);

DEFINE_FAKE_VALUE_FUNC2(esp_err_t, mocked_i2c_new_master_bus, const i2c_master_bus_config_t *, i2c_master_bus_handle_t *);
DEFINE_FAKE_VALUE_FUNC1(esp_err_t, mocked_i2c_del_master_bus, i2c_master_bus_handle_t);
DEFINE_FAKE_VALUE_FUNC1(esp_err_t, mocked_i2c_master_bus_reset, i2c_master_bus_handle_t);
DEFINE_FAKE_VALUE_FUNC3(esp_err_t, mocked_i2c_master_bus_add_device, i2c_master_bus_handle_t, const i2c_device_config_t *, i2c_master_dev_handle_t *);
DEFINE_FAKE_VALUE_FUNC1(esp_err_t, mocked_i2c_master_bus_rm_device, i2c_master_dev_handle_t);
DEFINE_FAKE_VALUE_FUNC4(esp_err_t, mocked_i2c_master_transmit, i2c_master_dev_handle_t, const uint8_t *, size_t, int);
DEFINE_FAKE_VALUE_FUNC4(esp_err_t, mocked_i2c_master_receive, i2c_master_dev_handle_t, uint8_t *, size_t, int);
DEFINE_FAKE_VALUE_FUNC6(esp_err_t, mocked_i2c_master_transmit_receive, i2c_master_dev_handle_t, const uint8_t *, size_t, uint8_t *, size_t, int);
DEFINE_FAKE_VALUE_FUNC3(esp_err_t, mocked_i2c_master_probe, i2c_master_bus_handle_t, uint16_t, int);

// ---------------------------------------------------------
// 2. SYSTEM & ROM MOCKS
// ---------------------------------------------------------
// Manual variadic implementation for esp_rom_printf
void mocked_esp_rom_printf(const char *fmt, ...) {
    (void)fmt;
    // Optional: add vprintf if console output is needed during tests
}

DEFINE_FAKE_VALUE_FUNC0(int64_t, mocked_esp_timer_get_time);
DEFINE_FAKE_VALUE_FUNC1(eTaskState, mocked_eTaskGetState, TaskHandle_t);
DEFINE_FAKE_VOID_FUNC1(mocked_esp_chip_info, esp_chip_info_t *);
DEFINE_FAKE_VALUE_FUNC1(const char *, mocked_esp_err_to_name, esp_err_t);
DEFINE_FAKE_VOID_FUNC1(mocked_esp_rom_delay_us, uint32_t);
DEFINE_FAKE_VALUE_FUNC0(uint32_t, mocked_esp_get_free_heap_size);
DEFINE_FAKE_VALUE_FUNC0(uint32_t, mocked_esp_get_minimum_free_heap_size);
DEFINE_FAKE_VALUE_FUNC0(uint32_t, mocked_esp_cpu_get_cycle_count);
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wreturn-type"
DEFINE_FAKE_VOID_FUNC0(mocked_esp_restart);
#pragma GCC diagnostic pop

// --- Core Services ---
DEFINE_FAKE_VALUE_FUNC0(IConfigurationManagerPtr, mocked_ConfigurationManager_GetInstance);
DEFINE_FAKE_VALUE_FUNC1(IServicePtr, mocked_ServiceLocator_GetService, const char *);
DEFINE_FAKE_VALUE_FUNC0(IEventBusPtr, mocked_ServiceLocator_GetEventBus);
DEFINE_FAKE_VALUE_FUNC0(IEventBusPtr, mocked_GetEventBus);
DEFINE_FAKE_VALUE_FUNC1(esp_err_t, mocked_esp_console_cmd_register, const esp_console_cmd_t *);
DEFINE_FAKE_VOID_FUNC1(mocked_vQueueDelete, QueueHandle_t);


// ---------------------------------------------------------
// 3. DRIVER MOCKS (I2C, LEDC, etc.)
// ---------------------------------------------------------

DEFINE_FAKE_VALUE_FUNC1(esp_err_t, mocked_ledc_timer_config, ledc_timer_config_t *);
DEFINE_FAKE_VALUE_FUNC1(esp_err_t, mocked_ledc_channel_config, ledc_channel_config_t *);
DEFINE_FAKE_VALUE_FUNC3(esp_err_t, mocked_ledc_set_duty, ledc_mode_t, ledc_channel_t, uint32_t);
DEFINE_FAKE_VALUE_FUNC2(esp_err_t, mocked_ledc_update_duty, ledc_mode_t, ledc_channel_t);
DEFINE_FAKE_VALUE_FUNC4(esp_err_t, mocked_ledc_set_fade_with_time, ledc_mode_t, ledc_channel_t, uint32_t, int);
DEFINE_FAKE_VALUE_FUNC3(esp_err_t, mocked_ledc_fade_start, ledc_mode_t, ledc_channel_t, ledc_fade_mode_t);
DEFINE_FAKE_VALUE_FUNC1(esp_err_t, mocked_ledc_fade_func_install, int);
DEFINE_FAKE_VALUE_FUNC1(esp_err_t, mocked_esp_vfs_littlefs_register, const esp_vfs_littlefs_conf_t *);
DEFINE_FAKE_VALUE_FUNC1(bool, mocked_esp_littlefs_mounted, const char *);
DEFINE_FAKE_VALUE_FUNC1(size_t, mocked_heap_caps_get_free_size, uint32_t);
DEFINE_FAKE_VALUE_FUNC1(size_t, mocked_heap_caps_get_total_size, uint32_t);
DEFINE_FAKE_VALUE_FUNC1(size_t, mocked_heap_caps_get_largest_free_block, uint32_t);
DEFINE_FAKE_VALUE_FUNC2(esp_err_t, mocked_esp_timer_create, const esp_timer_create_args_t *, esp_timer_handle_t *);
DEFINE_FAKE_VALUE_FUNC2(esp_err_t, mocked_esp_timer_start_once, esp_timer_handle_t, uint64_t);
DEFINE_FAKE_VALUE_FUNC2(esp_err_t, mocked_esp_timer_start_periodic, esp_timer_handle_t, uint64_t);
DEFINE_FAKE_VALUE_FUNC1(esp_err_t, mocked_esp_timer_stop, esp_timer_handle_t);
DEFINE_FAKE_VALUE_FUNC1(esp_err_t, mocked_esp_timer_delete, esp_timer_handle_t);
DEFINE_FAKE_VALUE_FUNC2(esp_err_t, mocked_esp_flash_read_id, void *, uint32_t *);
DEFINE_FAKE_VALUE_FUNC2(esp_err_t, mocked_esp_flash_get_physical_size, void *, uint32_t *);

// Watchdog
DEFINE_FAKE_VALUE_FUNC1(esp_err_t, mocked_esp_task_wdt_init, const esp_task_wdt_config_t *);
DEFINE_FAKE_VALUE_FUNC0(esp_err_t, mocked_esp_task_wdt_reset);
DEFINE_FAKE_VALUE_FUNC2(esp_err_t, mocked_esp_task_wdt_add_user, const char *, esp_task_wdt_user_handle_t *);
DEFINE_FAKE_VALUE_FUNC1(esp_err_t, mocked_esp_task_wdt_reset_user, esp_task_wdt_user_handle_t);
DEFINE_FAKE_VALUE_FUNC1(esp_err_t, mocked_esp_task_wdt_delete_user, esp_task_wdt_user_handle_t);
DEFINE_FAKE_VALUE_FUNC1(esp_err_t, mocked_esp_task_wdt_add, TaskHandle_t);
DEFINE_FAKE_VALUE_FUNC1(esp_err_t, mocked_esp_task_wdt_delete, TaskHandle_t);
DEFINE_FAKE_VALUE_FUNC1(esp_err_t, mocked_esp_task_wdt_reconfigure, const esp_task_wdt_config_t *);

// NVS
DEFINE_FAKE_VALUE_FUNC3(esp_err_t, mocked_nvs_open, const char *, nvs_open_mode_t, nvs_handle_t *);
DEFINE_FAKE_VOID_FUNC1(mocked_nvs_close, nvs_handle_t);
DEFINE_FAKE_VALUE_FUNC4(esp_err_t, mocked_nvs_set_blob, nvs_handle_t, const char *, const void *, size_t);
DEFINE_FAKE_VALUE_FUNC4(esp_err_t, mocked_nvs_get_blob, nvs_handle_t, const char *, void *, size_t *);
DEFINE_FAKE_VALUE_FUNC1(esp_err_t, mocked_nvs_commit, nvs_handle_t);
DEFINE_FAKE_VALUE_FUNC3(esp_err_t, mocked_nvs_get_u8, nvs_handle_t, const char *, uint8_t *);
DEFINE_FAKE_VALUE_FUNC3(esp_err_t, mocked_nvs_set_u8, nvs_handle_t, const char *, uint8_t);
DEFINE_FAKE_VALUE_FUNC3(esp_err_t, mocked_nvs_get_u32, nvs_handle_t, const char *, uint32_t *);
DEFINE_FAKE_VALUE_FUNC3(esp_err_t, mocked_nvs_set_u32, nvs_handle_t, const char *, uint32_t);
DEFINE_FAKE_VALUE_FUNC3(esp_err_t, mocked_nvs_get_i32, nvs_handle_t, const char *, int32_t *);
DEFINE_FAKE_VALUE_FUNC3(esp_err_t, mocked_nvs_set_i32, nvs_handle_t, const char *, int32_t);
DEFINE_FAKE_VALUE_FUNC4(esp_err_t, mocked_nvs_get_str, nvs_handle_t, const char *, char *, size_t *);
DEFINE_FAKE_VALUE_FUNC3(esp_err_t, mocked_nvs_set_str, nvs_handle_t, const char *, const char *);
DEFINE_FAKE_VALUE_FUNC1(esp_err_t, mocked_nvs_erase_all, nvs_handle_t);

// GPIO
DEFINE_FAKE_VALUE_FUNC1(esp_err_t, mocked_gpio_config, const gpio_config_t *);
DEFINE_FAKE_VALUE_FUNC1(esp_err_t, mocked_gpio_reset_pin, gpio_num_t);
DEFINE_FAKE_VALUE_FUNC2(esp_err_t, mocked_gpio_set_level, gpio_num_t, uint32_t);
DEFINE_FAKE_VALUE_FUNC1(int, mocked_gpio_get_level, gpio_num_t);
DEFINE_FAKE_VALUE_FUNC2(esp_err_t, mocked_gpio_set_direction, gpio_num_t, gpio_mode_t);
DEFINE_FAKE_VALUE_FUNC2(esp_err_t, mocked_gpio_set_pull_mode, gpio_num_t, gpio_pull_mode_t);
DEFINE_FAKE_VALUE_FUNC1(esp_err_t, mocked_gpio_install_isr_service, int);
DEFINE_FAKE_VALUE_FUNC2(esp_err_t, mocked_gpio_set_intr_type, gpio_num_t, gpio_int_type_t);
DEFINE_FAKE_VALUE_FUNC1(esp_err_t, mocked_gpio_isr_handler_remove, gpio_num_t);
DEFINE_FAKE_VALUE_FUNC3(esp_err_t, mocked_gpio_isr_handler_add, gpio_num_t, gpio_isr_t, void *);

// Sleep & Power
DEFINE_FAKE_VOID_FUNC1(mocked_esp_deep_sleep, uint64_t);
DEFINE_FAKE_VOID_FUNC0(mocked_esp_deep_sleep_start);
DEFINE_FAKE_VALUE_FUNC1(bool, mocked_esp_sleep_is_valid_wakeup_gpio, gpio_num_t);
DEFINE_FAKE_VALUE_FUNC2(esp_err_t, mocked_esp_deep_sleep_enable_gpio_wakeup, uint64_t, esp_deepsleep_gpio_wake_up_mode_t);
DEFINE_FAKE_VALUE_FUNC0(esp_reset_reason_t, mocked_esp_reset_reason);
DEFINE_FAKE_VALUE_FUNC0(esp_sleep_source_t, mocked_esp_sleep_get_wakeup_cause);
DEFINE_FAKE_VALUE_FUNC0(esp_err_t, mocked_esp_light_sleep_start);
DEFINE_FAKE_VALUE_FUNC1(esp_err_t, mocked_esp_sleep_enable_timer_wakeup, uint64_t);
DEFINE_FAKE_VALUE_FUNC1(esp_err_t, mocked_esp_sleep_disable_wakeup_source, esp_sleep_source_t);


// FreeRTOS & Tasks
DEFINE_FAKE_VALUE_FUNC0(SemaphoreHandle_t, mocked_xSemaphoreCreateMutex);
DEFINE_FAKE_VALUE_FUNC1(SemaphoreHandle_t, mocked_xSemaphoreCreateMutexStatic, StaticSemaphore_t *);
DEFINE_FAKE_VALUE_FUNC2(esp_err_t, mocked_esp_read_mac, uint8_t *, esp_mac_type_t);
DEFINE_FAKE_VALUE_FUNC0(SemaphoreHandle_t, mocked_xSemaphoreCreateRecursiveMutex);
DEFINE_FAKE_VALUE_FUNC2(BaseType_t, mocked_xSemaphoreTake, SemaphoreHandle_t, TickType_t);
DEFINE_FAKE_VALUE_FUNC1(BaseType_t, mocked_xSemaphoreGive, SemaphoreHandle_t);
DEFINE_FAKE_VALUE_FUNC2(BaseType_t, mocked_xSemaphoreTakeRecursive, SemaphoreHandle_t, TickType_t);
DEFINE_FAKE_VALUE_FUNC1(BaseType_t, mocked_xSemaphoreGiveRecursive, SemaphoreHandle_t);
DEFINE_FAKE_VALUE_FUNC2(QueueHandle_t, mocked_xQueueCreate, uint32_t, uint32_t);
DEFINE_FAKE_VALUE_FUNC3(BaseType_t, mocked_xQueueSend, QueueHandle_t, const void *, TickType_t);
DEFINE_FAKE_VALUE_FUNC3(BaseType_t, mocked_xQueueReceive, QueueHandle_t, void *, TickType_t);
DEFINE_FAKE_VALUE_FUNC3(BaseType_t, mocked_xQueueSendFromISR, QueueHandle_t, const void *, BaseType_t *);
DEFINE_FAKE_VALUE_FUNC1(uint32_t, mocked_uxQueueMessagesWaiting, QueueHandle_t);
DEFINE_FAKE_VALUE_FUNC0(TickType_t, mocked_xTaskGetTickCount);
DEFINE_FAKE_VALUE_FUNC0(TickType_t, mocked_xTaskGetTickCountFromISR);
DEFINE_FAKE_VALUE_FUNC0(uint32_t, mocked_uxTaskGetNumberOfTasks);
DEFINE_FAKE_VALUE_FUNC6(BaseType_t, mocked_xTaskCreate, TaskFunction_t, const char *, uint32_t, void *, uint32_t, TaskHandle_t *);
DEFINE_FAKE_VOID_FUNC1(mocked_vTaskDelete, TaskHandle_t);
DEFINE_FAKE_VOID_FUNC1(mocked_vTaskSuspend, TaskHandle_t);
DEFINE_FAKE_VOID_FUNC1(mocked_vTaskDelay, TickType_t);

// Temperature sensor driver
DEFINE_FAKE_VALUE_FUNC2(esp_err_t, mocked_temperature_sensor_install, const temperature_sensor_config_t *, temperature_sensor_handle_t *);
DEFINE_FAKE_VALUE_FUNC1(esp_err_t, mocked_temperature_sensor_uninstall, temperature_sensor_handle_t);
DEFINE_FAKE_VALUE_FUNC1(esp_err_t, mocked_temperature_sensor_enable, temperature_sensor_handle_t);
DEFINE_FAKE_VALUE_FUNC1(esp_err_t, mocked_temperature_sensor_disable, temperature_sensor_handle_t);
DEFINE_FAKE_VALUE_FUNC2(esp_err_t, mocked_temperature_sensor_get_celsius, temperature_sensor_handle_t, float *);

DEFINE_FAKE_VALUE_FUNC1(esp_err_t, mocked_esp_console_init, const esp_console_config_t *);
DEFINE_FAKE_VALUE_FUNC0(esp_err_t, mocked_esp_console_deinit);
DEFINE_FAKE_VALUE_FUNC2(esp_err_t, mocked_esp_console_run, const char *, int *);
DEFINE_FAKE_VOID_FUNC2(mocked_esp_log_level_set, const char *, esp_log_level_t);
DEFINE_FAKE_VALUE_FUNC0(const char *, mocked_esp_get_idf_version);
DEFINE_FAKE_VALUE_FUNC0(esp_err_t, mocked_nvs_flash_deinit);
DEFINE_FAKE_VALUE_FUNC2(esp_err_t, mocked_esp_flash_get_size, esp_flash_t *, uint32_t *);
DEFINE_FAKE_VALUE_FUNC3(BaseType_t, mocked_xTaskNotify, TaskHandle_t, uint32_t, eNotifyAction);

DEFINE_FAKE_VALUE_FUNC0(esp_err_t, mocked_nvs_flash_init);

DEFINE_FAKE_VALUE_FUNC0(esp_err_t, mocked_nvs_flash_erase);

DEFINE_FAKE_VALUE_FUNC0(TaskHandle_t, mocked_xTaskGetCurrentTaskHandle);
DEFINE_FAKE_VALUE_FUNC4(BaseType_t, mocked_xTaskNotifyWait, uint32_t, uint32_t, uint32_t *, TickType_t);
DEFINE_FAKE_VALUE_FUNC1(BaseType_t, mocked_xTaskNotifyGive, TaskHandle_t);

/* ESP32-C6 SDK range metadata boundary; these are not simulated measurements. */
#include "soc/temperature_sensor_periph.h"
const temperature_sensor_attribute_t temperature_sensor_attributes[TEMPERATURE_SENSOR_ATTR_RANGE_NUM] = {
    {-2,5,50,125,3},{-1,7,20,100,2},{0,15,-10,80,1},{1,11,-30,50,2},{2,10,-40,20,3}
};
