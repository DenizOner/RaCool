#ifndef TEST_MOCKS_H
#define TEST_MOCKS_H

#include "fff.h"
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

// Forward Declarations & Mock Types
#include "driver/temperature_sensor.h"
typedef int gpio_pull_mode_t;
typedef int (*esp_console_cmd_func_t)(int argc, char **argv);
typedef struct {
    const char *command;
    const char *help;
    const char *hint;
    esp_console_cmd_func_t func;
    void *argtable;
} esp_console_cmd_t;

#include "esp_err.h"
#include "esp_sleep.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/i2c_master.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_mac.h"
#include "esp_chip_info.h"
#include "esp_log.h"
#include "esp_littlefs.h"
#include "esp_task_wdt.h"
#include "nvs.h"

// FreeRTOS Types
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"

// Interface Pointers Forward Declarations
typedef struct IConfigurationManager IConfigurationManager;
typedef struct IConfigurationManager* IConfigurationManagerPtr;

typedef struct IService IService;
typedef struct IService* IServicePtr;

typedef struct IEventBus IEventBus;
typedef struct IEventBus* IEventBusPtr;

typedef struct IDebugConsole IDebugConsole;
typedef struct IDebugConsole* IDebugConsolePtr;

// ---------------------------------------------------------
// 1. FFF GLOBAL DECLARATION
// ---------------------------------------------------------


// ---------------------------------------------------------
// 2. EXPLICIT PROTOTYPES (For linker visibility)
// ---------------------------------------------------------
#ifdef __cplusplus
extern "C" {
#endif

void mocked_esp_rom_printf(const char *fmt, ...);
int64_t mocked_esp_timer_get_time(void);
void mocked_esp_chip_info(esp_chip_info_t *info);
const char *mocked_esp_err_to_name(esp_err_t code);
void mocked_esp_rom_delay_us(uint32_t us);
uint32_t mocked_esp_get_free_heap_size(void);
uint32_t mocked_esp_get_minimum_free_heap_size(void);
uint32_t mocked_esp_cpu_get_cycle_count(void);
void mocked_esp_restart(void);
char *mocked_strdup(const char *s);

#ifdef __cplusplus
}
#endif

// ---------------------------------------------------------
// 3. FFF DECLARATIONS
// ---------------------------------------------------------

// Standard Libs
DECLARE_FAKE_VALUE_FUNC1(char *, mocked_strdup, const char *);

// System & ROM
// mocked_esp_rom_printf is handled manually due to variadic args
DECLARE_FAKE_VALUE_FUNC0(int64_t, mocked_esp_timer_get_time);
DECLARE_FAKE_VOID_FUNC1(mocked_esp_chip_info, esp_chip_info_t *);
DECLARE_FAKE_VALUE_FUNC1(const char *, mocked_esp_err_to_name, esp_err_t);
DECLARE_FAKE_VOID_FUNC1(mocked_esp_rom_delay_us, uint32_t);
DECLARE_FAKE_VALUE_FUNC0(uint32_t, mocked_esp_get_free_heap_size);
DECLARE_FAKE_VALUE_FUNC0(uint32_t, mocked_esp_get_minimum_free_heap_size);
DECLARE_FAKE_VALUE_FUNC0(uint32_t, mocked_esp_cpu_get_cycle_count);
DECLARE_FAKE_VOID_FUNC0(mocked_esp_restart);

DECLARE_FAKE_VALUE_FUNC2(esp_err_t, mocked_i2c_new_master_bus, const i2c_master_bus_config_t *, i2c_master_bus_handle_t *);
DECLARE_FAKE_VALUE_FUNC1(esp_err_t, mocked_i2c_del_master_bus, i2c_master_bus_handle_t);
DECLARE_FAKE_VALUE_FUNC1(esp_err_t, mocked_i2c_master_bus_reset, i2c_master_bus_handle_t);
DECLARE_FAKE_VALUE_FUNC3(esp_err_t, mocked_i2c_master_bus_add_device, i2c_master_bus_handle_t, const i2c_device_config_t *, i2c_master_dev_handle_t *);
DECLARE_FAKE_VALUE_FUNC1(esp_err_t, mocked_i2c_master_bus_rm_device, i2c_master_dev_handle_t);
DECLARE_FAKE_VALUE_FUNC4(esp_err_t, mocked_i2c_master_transmit, i2c_master_dev_handle_t, const uint8_t *, size_t, int);
DECLARE_FAKE_VALUE_FUNC4(esp_err_t, mocked_i2c_master_receive, i2c_master_dev_handle_t, uint8_t *, size_t, int);
DECLARE_FAKE_VALUE_FUNC6(esp_err_t, mocked_i2c_master_transmit_receive, i2c_master_dev_handle_t, const uint8_t *, size_t, uint8_t *, size_t, int);
DECLARE_FAKE_VALUE_FUNC3(esp_err_t, mocked_i2c_master_probe, i2c_master_bus_handle_t, uint16_t, int);

// LEDC, Flash, Timer
DECLARE_FAKE_VALUE_FUNC1(esp_err_t, mocked_ledc_timer_config, ledc_timer_config_t *);
DECLARE_FAKE_VALUE_FUNC1(esp_err_t, mocked_ledc_channel_config, ledc_channel_config_t *);
DECLARE_FAKE_VALUE_FUNC3(esp_err_t, mocked_ledc_set_duty, ledc_mode_t, ledc_channel_t, uint32_t);
DECLARE_FAKE_VALUE_FUNC2(esp_err_t, mocked_ledc_update_duty, ledc_mode_t, ledc_channel_t);
DECLARE_FAKE_VALUE_FUNC4(esp_err_t, mocked_ledc_set_fade_with_time, ledc_mode_t, ledc_channel_t, uint32_t, int);
DECLARE_FAKE_VALUE_FUNC3(esp_err_t, mocked_ledc_fade_start, ledc_mode_t, ledc_channel_t, ledc_fade_mode_t);
DECLARE_FAKE_VALUE_FUNC1(esp_err_t, mocked_ledc_fade_func_install, int);
DECLARE_FAKE_VALUE_FUNC1(esp_err_t, mocked_esp_vfs_littlefs_register, const esp_vfs_littlefs_conf_t *);
DECLARE_FAKE_VALUE_FUNC1(bool, mocked_esp_littlefs_mounted, const char *);
DECLARE_FAKE_VALUE_FUNC1(size_t, mocked_heap_caps_get_free_size, uint32_t);
DECLARE_FAKE_VALUE_FUNC1(size_t, mocked_heap_caps_get_total_size, uint32_t);
DECLARE_FAKE_VALUE_FUNC1(size_t, mocked_heap_caps_get_largest_free_block, uint32_t);
DECLARE_FAKE_VALUE_FUNC2(esp_err_t, mocked_esp_timer_create, const esp_timer_create_args_t *, esp_timer_handle_t *);
DECLARE_FAKE_VALUE_FUNC2(esp_err_t, mocked_esp_timer_start_once, esp_timer_handle_t, uint64_t);
DECLARE_FAKE_VALUE_FUNC2(esp_err_t, mocked_esp_timer_start_periodic, esp_timer_handle_t, uint64_t);
DECLARE_FAKE_VALUE_FUNC1(esp_err_t, mocked_esp_timer_stop, esp_timer_handle_t);
DECLARE_FAKE_VALUE_FUNC1(esp_err_t, mocked_esp_timer_delete, esp_timer_handle_t);
DECLARE_FAKE_VALUE_FUNC2(esp_err_t, mocked_esp_flash_read_id, void *, uint32_t *);
DECLARE_FAKE_VALUE_FUNC2(esp_err_t, mocked_esp_flash_get_physical_size, void *, uint32_t *);

// Watchdog
DECLARE_FAKE_VALUE_FUNC1(esp_err_t, mocked_esp_task_wdt_init, const esp_task_wdt_config_t *);
DECLARE_FAKE_VALUE_FUNC0(esp_err_t, mocked_esp_task_wdt_reset);
DECLARE_FAKE_VALUE_FUNC2(esp_err_t, mocked_esp_task_wdt_add_user, const char *, esp_task_wdt_user_handle_t *);
DECLARE_FAKE_VALUE_FUNC1(esp_err_t, mocked_esp_task_wdt_reset_user, esp_task_wdt_user_handle_t);
DECLARE_FAKE_VALUE_FUNC1(esp_err_t, mocked_esp_task_wdt_delete_user, esp_task_wdt_user_handle_t);
DECLARE_FAKE_VALUE_FUNC1(esp_err_t, mocked_esp_task_wdt_add, TaskHandle_t);
DECLARE_FAKE_VALUE_FUNC1(esp_err_t, mocked_esp_task_wdt_delete, TaskHandle_t);
DECLARE_FAKE_VALUE_FUNC1(esp_err_t, mocked_esp_task_wdt_reconfigure, const esp_task_wdt_config_t *);

// NVS
DECLARE_FAKE_VALUE_FUNC3(esp_err_t, mocked_nvs_open, const char *, nvs_open_mode_t, nvs_handle_t *);
DECLARE_FAKE_VOID_FUNC1(mocked_nvs_close, nvs_handle_t);
DECLARE_FAKE_VALUE_FUNC4(esp_err_t, mocked_nvs_set_blob, nvs_handle_t, const char *, const void *, size_t);
DECLARE_FAKE_VALUE_FUNC4(esp_err_t, mocked_nvs_get_blob, nvs_handle_t, const char *, void *, size_t *);
DECLARE_FAKE_VALUE_FUNC1(esp_err_t, mocked_nvs_commit, nvs_handle_t);
DECLARE_FAKE_VALUE_FUNC3(esp_err_t, mocked_nvs_get_u8, nvs_handle_t, const char *, uint8_t *);
DECLARE_FAKE_VALUE_FUNC3(esp_err_t, mocked_nvs_set_u8, nvs_handle_t, const char *, uint8_t);
DECLARE_FAKE_VALUE_FUNC3(esp_err_t, mocked_nvs_get_u32, nvs_handle_t, const char *, uint32_t *);
DECLARE_FAKE_VALUE_FUNC3(esp_err_t, mocked_nvs_set_u32, nvs_handle_t, const char *, uint32_t);
DECLARE_FAKE_VALUE_FUNC3(esp_err_t, mocked_nvs_get_i32, nvs_handle_t, const char *, int32_t *);
DECLARE_FAKE_VALUE_FUNC3(esp_err_t, mocked_nvs_set_i32, nvs_handle_t, const char *, int32_t);
DECLARE_FAKE_VALUE_FUNC4(esp_err_t, mocked_nvs_get_str, nvs_handle_t, const char *, char *, size_t *);
DECLARE_FAKE_VALUE_FUNC3(esp_err_t, mocked_nvs_set_str, nvs_handle_t, const char *, const char *);
DECLARE_FAKE_VALUE_FUNC1(esp_err_t, mocked_nvs_erase_all, nvs_handle_t);

// GPIO
DECLARE_FAKE_VALUE_FUNC1(esp_err_t, mocked_gpio_config, const gpio_config_t *);
DECLARE_FAKE_VALUE_FUNC1(esp_err_t, mocked_gpio_reset_pin, gpio_num_t);
DECLARE_FAKE_VALUE_FUNC2(esp_err_t, mocked_gpio_set_level, gpio_num_t, uint32_t);
DECLARE_FAKE_VALUE_FUNC1(int, mocked_gpio_get_level, gpio_num_t);
DECLARE_FAKE_VALUE_FUNC2(esp_err_t, mocked_gpio_set_direction, gpio_num_t, gpio_mode_t);
DECLARE_FAKE_VALUE_FUNC2(esp_err_t, mocked_gpio_set_pull_mode, gpio_num_t, gpio_pull_mode_t);
DECLARE_FAKE_VALUE_FUNC1(esp_err_t, mocked_gpio_install_isr_service, int);
DECLARE_FAKE_VALUE_FUNC2(esp_err_t, mocked_gpio_set_intr_type, gpio_num_t, gpio_int_type_t);
DECLARE_FAKE_VALUE_FUNC1(esp_err_t, mocked_gpio_isr_handler_remove, gpio_num_t);
DECLARE_FAKE_VALUE_FUNC3(esp_err_t, mocked_gpio_isr_handler_add, gpio_num_t, gpio_isr_t, void *);

// Sleep & Power
DECLARE_FAKE_VOID_FUNC1(mocked_esp_deep_sleep, uint64_t);
DECLARE_FAKE_VOID_FUNC0(mocked_esp_deep_sleep_start);
DECLARE_FAKE_VALUE_FUNC1(bool, mocked_esp_sleep_is_valid_wakeup_gpio, gpio_num_t);
#define esp_sleep_is_valid_wakeup_gpio mocked_esp_sleep_is_valid_wakeup_gpio
DECLARE_FAKE_VALUE_FUNC2(esp_err_t, mocked_esp_deep_sleep_enable_gpio_wakeup, uint64_t, esp_deepsleep_gpio_wake_up_mode_t);
DECLARE_FAKE_VALUE_FUNC0(esp_reset_reason_t, mocked_esp_reset_reason);
DECLARE_FAKE_VALUE_FUNC0(esp_sleep_source_t, mocked_esp_sleep_get_wakeup_cause);
DECLARE_FAKE_VALUE_FUNC0(esp_err_t, mocked_esp_light_sleep_start);
DECLARE_FAKE_VALUE_FUNC1(esp_err_t, mocked_esp_sleep_enable_timer_wakeup, uint64_t);
DECLARE_FAKE_VALUE_FUNC1(esp_err_t, mocked_esp_sleep_disable_wakeup_source, esp_sleep_source_t);


// FreeRTOS & Tasks
DECLARE_FAKE_VALUE_FUNC0(SemaphoreHandle_t, mocked_xSemaphoreCreateMutex);
DECLARE_FAKE_VALUE_FUNC1(SemaphoreHandle_t, mocked_xSemaphoreCreateMutexStatic, StaticSemaphore_t *);
DECLARE_FAKE_VALUE_FUNC2(esp_err_t, mocked_esp_read_mac, uint8_t *, esp_mac_type_t);
DECLARE_FAKE_VALUE_FUNC0(SemaphoreHandle_t, mocked_xSemaphoreCreateRecursiveMutex);
DECLARE_FAKE_VALUE_FUNC2(BaseType_t, mocked_xSemaphoreTake, SemaphoreHandle_t, TickType_t);
DECLARE_FAKE_VALUE_FUNC1(BaseType_t, mocked_xSemaphoreGive, SemaphoreHandle_t);
DECLARE_FAKE_VALUE_FUNC2(BaseType_t, mocked_xSemaphoreTakeRecursive, SemaphoreHandle_t, TickType_t);
DECLARE_FAKE_VALUE_FUNC1(BaseType_t, mocked_xSemaphoreGiveRecursive, SemaphoreHandle_t);
DECLARE_FAKE_VALUE_FUNC2(QueueHandle_t, mocked_xQueueCreate, uint32_t, uint32_t);
DECLARE_FAKE_VALUE_FUNC3(BaseType_t, mocked_xQueueSend, QueueHandle_t, const void *, TickType_t);
DECLARE_FAKE_VALUE_FUNC3(BaseType_t, mocked_xQueueReceive, QueueHandle_t, void *, TickType_t);
DECLARE_FAKE_VALUE_FUNC3(BaseType_t, mocked_xQueueSendFromISR, QueueHandle_t, const void *, BaseType_t *);
DECLARE_FAKE_VALUE_FUNC1(uint32_t, mocked_uxQueueMessagesWaiting, QueueHandle_t);
DECLARE_FAKE_VOID_FUNC1(mocked_vQueueDelete, QueueHandle_t);
DECLARE_FAKE_VALUE_FUNC0(TickType_t, mocked_xTaskGetTickCount);
DECLARE_FAKE_VALUE_FUNC0(TickType_t, mocked_xTaskGetTickCountFromISR);
DECLARE_FAKE_VALUE_FUNC1(eTaskState, mocked_eTaskGetState, TaskHandle_t);
DECLARE_FAKE_VALUE_FUNC0(uint32_t, mocked_uxTaskGetNumberOfTasks);
DECLARE_FAKE_VALUE_FUNC6(BaseType_t, mocked_xTaskCreate, TaskFunction_t, const char *, uint32_t, void *, uint32_t, TaskHandle_t *);
DECLARE_FAKE_VOID_FUNC1(mocked_vTaskDelete, TaskHandle_t);
DECLARE_FAKE_VOID_FUNC1(mocked_vTaskSuspend, TaskHandle_t);
DECLARE_FAKE_VOID_FUNC1(mocked_vTaskDelay, TickType_t);

// Temperature Sensor Driver
DECLARE_FAKE_VALUE_FUNC2(esp_err_t, mocked_temperature_sensor_install, const temperature_sensor_config_t *, temperature_sensor_handle_t *);
DECLARE_FAKE_VALUE_FUNC1(esp_err_t, mocked_temperature_sensor_uninstall, temperature_sensor_handle_t);
DECLARE_FAKE_VALUE_FUNC1(esp_err_t, mocked_temperature_sensor_enable, temperature_sensor_handle_t);
DECLARE_FAKE_VALUE_FUNC1(esp_err_t, mocked_temperature_sensor_disable, temperature_sensor_handle_t);
DECLARE_FAKE_VALUE_FUNC2(esp_err_t, mocked_temperature_sensor_get_celsius, temperature_sensor_handle_t, float *);

// Console & Shell
DECLARE_FAKE_VALUE_FUNC1(esp_err_t, mocked_esp_console_cmd_register, const esp_console_cmd_t *);

// --- Core Services ---
DECLARE_FAKE_VALUE_FUNC0(IConfigurationManagerPtr, mocked_ConfigurationManager_GetInstance);
DECLARE_FAKE_VALUE_FUNC1(IServicePtr, mocked_ServiceLocator_GetService, const char *);
DECLARE_FAKE_VALUE_FUNC0(IEventBusPtr, mocked_ServiceLocator_GetEventBus);
DECLARE_FAKE_VALUE_FUNC0(IEventBusPtr, mocked_GetEventBus);

// ---------------------------------------------------------
// 4. FUNCTION REDIRECTIONS (C Code standard symbol replacement)
// ---------------------------------------------------------

// FreeRTOS Mocks (Always active when included)
#define xSemaphoreCreateMutex mocked_xSemaphoreCreateMutex
#define xSemaphoreCreateMutexStatic mocked_xSemaphoreCreateMutexStatic
#define esp_read_mac mocked_esp_read_mac
#define xSemaphoreCreateRecursiveMutex mocked_xSemaphoreCreateRecursiveMutex
#define xSemaphoreTake mocked_xSemaphoreTake
#define xSemaphoreGive mocked_xSemaphoreGive
#define xSemaphoreTakeRecursive mocked_xSemaphoreTakeRecursive
#define xSemaphoreGiveRecursive mocked_xSemaphoreGiveRecursive
#define xQueueCreate mocked_xQueueCreate
#define xQueueSend mocked_xQueueSend
#define xQueueSendFromISR mocked_xQueueSendFromISR
#define xQueueReceive mocked_xQueueReceive
#define uxQueueMessagesWaiting mocked_uxQueueMessagesWaiting
#define vQueueDelete mocked_vQueueDelete
#define xTaskGetTickCount mocked_xTaskGetTickCount
#define xTaskGetTickCountFromISR mocked_xTaskGetTickCountFromISR
#define eTaskGetState mocked_eTaskGetState
#define uxTaskGetNumberOfTasks mocked_uxTaskGetNumberOfTasks
#define xTaskCreate mocked_xTaskCreate
#define vTaskDelete mocked_vTaskDelete
#define vTaskSuspend mocked_vTaskSuspend
#define vTaskDelay mocked_vTaskDelay

// Hardware Drivers
#define gpio_config mocked_gpio_config
#define gpio_reset_pin mocked_gpio_reset_pin
#define gpio_set_level mocked_gpio_set_level
#define gpio_get_level mocked_gpio_get_level
#define gpio_set_direction mocked_gpio_set_direction
#define gpio_install_isr_service mocked_gpio_install_isr_service
#define gpio_isr_handler_add mocked_gpio_isr_handler_add
#define gpio_set_intr_type mocked_gpio_set_intr_type
#define gpio_isr_handler_remove mocked_gpio_isr_handler_remove

// LEDC
#define ledc_timer_config mocked_ledc_timer_config
#define ledc_channel_config mocked_ledc_channel_config
#define ledc_set_duty mocked_ledc_set_duty
#define ledc_update_duty mocked_ledc_update_duty
#define ledc_set_fade_with_time mocked_ledc_set_fade_with_time
#define ledc_fade_start mocked_ledc_fade_start
#define ledc_fade_func_install mocked_ledc_fade_func_install

// System / IDF Mocks
#define strdup mocked_strdup
#define esp_rom_printf mocked_esp_rom_printf
#define esp_timer_get_time mocked_esp_timer_get_time
#define esp_chip_info mocked_esp_chip_info
#define esp_err_to_name mocked_esp_err_to_name
#define esp_rom_delay_us mocked_esp_rom_delay_us
#define esp_get_free_heap_size mocked_esp_get_free_heap_size
#define esp_get_minimum_free_heap_size mocked_esp_get_minimum_free_heap_size
#define esp_cpu_get_cycle_count mocked_esp_cpu_get_cycle_count
#define esp_restart mocked_esp_restart

// I2C Master
#define i2c_new_master_bus mocked_i2c_new_master_bus
#define i2c_del_master_bus mocked_i2c_del_master_bus
#define i2c_master_bus_reset mocked_i2c_master_bus_reset
#define i2c_master_bus_add_device mocked_i2c_master_bus_add_device
#define i2c_master_bus_rm_device mocked_i2c_master_bus_rm_device
#define i2c_master_transmit mocked_i2c_master_transmit
#define i2c_master_receive mocked_i2c_master_receive
#define i2c_master_transmit_receive mocked_i2c_master_transmit_receive
#define i2c_master_probe mocked_i2c_master_probe

// Storage
#define esp_vfs_littlefs_register mocked_esp_vfs_littlefs_register
#define esp_littlefs_mounted mocked_esp_littlefs_mounted
#define nvs_open mocked_nvs_open
#define nvs_close mocked_nvs_close
#define nvs_set_blob mocked_nvs_set_blob
#define nvs_get_blob mocked_nvs_get_blob
#define nvs_commit mocked_nvs_commit
#define nvs_get_u8 mocked_nvs_get_u8
#define nvs_set_u8 mocked_nvs_set_u8
#define nvs_get_u32 mocked_nvs_get_u32
#define nvs_set_u32 mocked_nvs_set_u32
#define nvs_get_i32 mocked_nvs_get_i32
#define nvs_set_i32 mocked_nvs_set_i32
#define nvs_get_str mocked_nvs_get_str
#define nvs_set_str mocked_nvs_set_str
#define nvs_erase_all mocked_nvs_erase_all

// Heap
#define heap_caps_get_free_size mocked_heap_caps_get_free_size
#define heap_caps_get_total_size mocked_heap_caps_get_total_size
#define heap_caps_get_largest_free_block mocked_heap_caps_get_largest_free_block
#define esp_timer_create mocked_esp_timer_create
#define esp_timer_start_once mocked_esp_timer_start_once
#define esp_timer_start_periodic mocked_esp_timer_start_periodic
#define esp_timer_stop mocked_esp_timer_stop
#define esp_timer_delete mocked_esp_timer_delete
#define esp_flash_read_id mocked_esp_flash_read_id
#define esp_flash_get_physical_size mocked_esp_flash_get_physical_size

// Watchdog
#define esp_task_wdt_init mocked_esp_task_wdt_init
#define esp_task_wdt_reset mocked_esp_task_wdt_reset
#define esp_task_wdt_add_user mocked_esp_task_wdt_add_user
#define esp_task_wdt_reset_user mocked_esp_task_wdt_reset_user
#define esp_task_wdt_delete_user mocked_esp_task_wdt_delete_user
#define esp_task_wdt_add mocked_esp_task_wdt_add
#define esp_task_wdt_delete mocked_esp_task_wdt_delete
#define esp_task_wdt_reconfigure mocked_esp_task_wdt_reconfigure

// Sleep
#define esp_deep_sleep mocked_esp_deep_sleep
#define esp_deep_sleep_start mocked_esp_deep_sleep_start
#define esp_deep_sleep_enable_gpio_wakeup mocked_esp_deep_sleep_enable_gpio_wakeup
#define esp_reset_reason mocked_esp_reset_reason
#define esp_sleep_get_wakeup_cause mocked_esp_sleep_get_wakeup_cause
#define esp_light_sleep_start mocked_esp_light_sleep_start
#define esp_sleep_enable_timer_wakeup mocked_esp_sleep_enable_timer_wakeup
#define esp_sleep_disable_wakeup_source mocked_esp_sleep_disable_wakeup_source


// Temperature sensor driver
#define temperature_sensor_install mocked_temperature_sensor_install
#define temperature_sensor_uninstall mocked_temperature_sensor_uninstall
#define temperature_sensor_enable mocked_temperature_sensor_enable
#define temperature_sensor_disable mocked_temperature_sensor_disable
#define temperature_sensor_get_celsius mocked_temperature_sensor_get_celsius
#define esp_console_cmd_register mocked_esp_console_cmd_register

// --- Project Service Mocks (Always active) ---
#define ConfigurationManager_GetInstance mocked_ConfigurationManager_GetInstance
#define ServiceLocator_GetService mocked_ServiceLocator_GetService
#define ServiceLocator_GetEventBus mocked_ServiceLocator_GetEventBus
#define GetEventBus mocked_GetEventBus

#include "esp_console.h"
#include "esp_flash.h"
DECLARE_FAKE_VALUE_FUNC1(esp_err_t, mocked_esp_console_init, const esp_console_config_t *);
#define esp_console_init mocked_esp_console_init
DECLARE_FAKE_VALUE_FUNC0(esp_err_t, mocked_esp_console_deinit);
#define esp_console_deinit mocked_esp_console_deinit
DECLARE_FAKE_VALUE_FUNC2(esp_err_t, mocked_esp_console_run, const char *, int *);
#define esp_console_run mocked_esp_console_run
DECLARE_FAKE_VOID_FUNC2(mocked_esp_log_level_set, const char *, esp_log_level_t);
#define esp_log_level_set mocked_esp_log_level_set
DECLARE_FAKE_VALUE_FUNC0(const char *, mocked_esp_get_idf_version);
#define esp_get_idf_version mocked_esp_get_idf_version
DECLARE_FAKE_VALUE_FUNC0(esp_err_t, mocked_nvs_flash_deinit);
#define nvs_flash_deinit mocked_nvs_flash_deinit
DECLARE_FAKE_VALUE_FUNC2(esp_err_t, mocked_esp_flash_get_size, esp_flash_t *, uint32_t *);
#define esp_flash_get_size mocked_esp_flash_get_size
DECLARE_FAKE_VALUE_FUNC3(BaseType_t, mocked_xTaskNotify, TaskHandle_t, uint32_t, eNotifyAction);
#define xTaskNotify mocked_xTaskNotify

DECLARE_FAKE_VALUE_FUNC0(esp_err_t, mocked_nvs_flash_init);
#define nvs_flash_init mocked_nvs_flash_init
DECLARE_FAKE_VALUE_FUNC0(esp_err_t, mocked_nvs_flash_erase);
#define nvs_flash_erase mocked_nvs_flash_erase

DECLARE_FAKE_VALUE_FUNC0(TaskHandle_t, mocked_xTaskGetCurrentTaskHandle);
#define xTaskGetCurrentTaskHandle mocked_xTaskGetCurrentTaskHandle
DECLARE_FAKE_VALUE_FUNC4(BaseType_t, mocked_xTaskNotifyWait, uint32_t, uint32_t, uint32_t *, TickType_t);
#define xTaskNotifyWait mocked_xTaskNotifyWait
DECLARE_FAKE_VALUE_FUNC1(BaseType_t, mocked_xTaskNotifyGive, TaskHandle_t);
#define xTaskNotifyGive mocked_xTaskNotifyGive

#endif
