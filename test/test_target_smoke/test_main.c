void target_localization_verify_catalogs(void);
static void test_target_localization_catalogs(void) { target_localization_verify_catalogs(); }
void target_flash_worker_cleanup(void);
void target_flash_worker_verify_shutdown(void);
static void test_target_flash_worker_shutdown(void) {target_flash_worker_verify_shutdown();}
void target_cpu_timer_cleanup(void);
void target_cpu_verify_timer_ownership(void);
#include "driver/uart.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "esp_pm.h"
#include "Management/Watchdog/WatchdogWorker.h"
#include "../../src/Management/Watchdog/WatchdogWorker.c"
#include <string.h>
#include "Communication/SerialMonitor/ISerialPort.h"
// On-target checks use real ESP-IDF hardware APIs and production color math.
#include "unity.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "driver/temperature_sensor.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "../../src/Hardware/RGBLED/RGBLEDColorEngine.c"
#include "../../src/Management/ConfigurationManager/SystemConfigs.c"

static bool uart_loop_installed;
static bool restore_pm_profile;
static esp_pm_config_t original_pm_profile;
static bool serial_port_cleanup;
static WatchdogWorker target_worker;
static bool target_worker_initialized, target_wdt_owned;
static atomic_uint worker_iterations, worker_feed_errors;
static atomic_bool worker_removed;
void target_eventbus_verify_owned_payload_and_stop(void);
static void test_target_eventbus_owned_payload_and_stop(void) {
    target_eventbus_verify_owned_payload_and_stop();
}
void target_eventbus_cleanup(void);
void target_serial_monitor_cleanup(void);
void target_serial_monitor_verify_shutdown(void);
void target_serial_buffer_verify_contract(void);
void target_serial_buffer_cleanup(void);
void target_infrared_worker_cleanup(void);
void target_infrared_worker_verify_shutdown(void);
static void test_target_infrared_worker_shutdown(void) { target_infrared_worker_verify_shutdown(); }
static void test_target_serial_buffer_contract(void) { target_serial_buffer_verify_contract(); }
static void test_target_serial_monitor_shutdown(void) { target_serial_monitor_verify_shutdown(); }
void target_oled_worker_cleanup(void);
void target_oled_worker_verify_shutdown(void);
static void test_target_oled_worker_shutdown(void) { target_oled_worker_verify_shutdown(); }
void target_ui_screen_cleanup(void);
void target_ui_screen_verify_rollback(void);
static void test_target_ui_screen_rollback(void) { target_ui_screen_verify_rollback(); }
void target_diagnostics_worker_cleanup(void);
void target_zigbee_worker_cleanup(void);
void target_zigbee_worker_verify_shutdown(void);
static void test_target_zigbee_worker_shutdown(void) { target_zigbee_worker_verify_shutdown(); }
void target_diagnostics_worker_verify_shutdown(void);
static void test_target_diagnostics_worker_shutdown(void) { target_diagnostics_worker_verify_shutdown(); }
void target_performance_worker_cleanup(void);
void target_performance_worker_verify_shutdown(void);
static void test_target_performance_worker_shutdown(void) { target_performance_worker_verify_shutdown(); }
void target_log_capture_cleanup(void);
void target_log_capture_verify_lifetime(void);
static void test_target_log_capture_lifetime(void) { target_log_capture_verify_lifetime(); }
void target_memory_cleanup(void);
void target_memory_verify_ownership(void);
static void test_target_memory_ownership(void) { target_memory_verify_ownership(); }
void target_error_cleanup(void);
void target_error_verify_shutdown(void);
static void test_target_error_worker_shutdown(void) {target_error_verify_shutdown();}
void setUp(void) {}
void target_rotary_typed_pin_verify_ownership(void);
static void test_target_rotary_typed_pin_ownership(void) { target_rotary_typed_pin_verify_ownership(); }
void target_rotary_worker_cleanup(void);
void target_rotary_worker_verify_shutdown(void);
static void test_target_rotary_worker_shutdown(void) { target_rotary_worker_verify_shutdown(); }
void target_rotary_pcnt_cleanup(void);
void target_rotary_pcnt_verify_lifecycle(void);
static void test_target_rotary_pcnt_lifecycle(void) { target_rotary_pcnt_verify_lifecycle(); }
void target_relay_timer_cleanup(void);
void target_relay_timer_verify_nonblocking(void);
static void test_target_relay_timer_nonblocking(void) { target_relay_timer_verify_nonblocking(); }
void target_service_locator_cleanup(void);
void target_service_locator_verify_lifetime(void);
static void test_target_service_locator_lifetime(void) { target_service_locator_verify_lifetime(); }
void target_i2c_verify_arbitration(void);
void target_i2c_verify_sdk_recovery(void);
static void test_target_i2c_sdk_recovery(void) { target_i2c_verify_sdk_recovery(); }
static void test_target_i2c_arbitration(void) { target_i2c_verify_arbitration(); }
void target_application_worker_cleanup(void);
void target_application_worker_verify_shutdown(void);
void target_application_factory_reset_verify(void);
static void test_target_application_factory_reset(void) { target_application_factory_reset_verify(); }
static void test_target_application_worker_shutdown(void) { target_application_worker_verify_shutdown(); }
void target_builtin_worker_cleanup(void);
void target_builtin_worker_verify_shutdown(void);
static void test_target_builtin_worker_shutdown(void) { target_builtin_worker_verify_shutdown(); }
void target_watchdog_feed_cleanup(void);
void target_watchdog_feed_verify_contention(void);
static void test_target_watchdog_feed_contention(void) { target_watchdog_feed_verify_contention(); }
void target_menu_worker_cleanup(void);
void target_menu_worker_verify_shutdown(void);
static void test_target_menu_worker_shutdown(void) { target_menu_worker_verify_shutdown(); }
void target_telemetry_worker_cleanup(void);
void target_telemetry_worker_verify_shutdown(void);
static void test_target_telemetry_worker_shutdown(void) { target_telemetry_worker_verify_shutdown(); }
void target_system_worker_cleanup(void);
void target_system_worker_verify_shutdown(void);
static void test_target_system_worker_shutdown(void) { target_system_worker_verify_shutdown(); }
void target_climate_worker_cleanup(void);
void target_climate_worker_verify_shutdown(void);
static void test_target_climate_worker_shutdown(void) { target_climate_worker_verify_shutdown(); }
void target_config_worker_cleanup(void);
void target_console_worker_cleanup(void);
void target_console_worker_verify_shutdown(void);
static void test_target_console_worker_shutdown(void) { target_console_worker_verify_shutdown(); }
void target_config_worker_verify_shutdown(void);
static void test_target_config_worker_shutdown(void) { target_config_worker_verify_shutdown(); }
void tearDown(void) {
    target_flash_worker_cleanup();
    target_cpu_timer_cleanup();
    target_error_cleanup();
    target_memory_cleanup();
    target_log_capture_cleanup();
    target_performance_worker_cleanup();
    target_zigbee_worker_cleanup();
    target_diagnostics_worker_cleanup();
    target_rotary_worker_cleanup();
    target_rotary_pcnt_cleanup();
    target_relay_timer_cleanup();
    target_service_locator_cleanup();
    target_console_worker_cleanup();
    target_config_worker_cleanup();
    target_ui_screen_cleanup();
    target_application_worker_cleanup();
    target_builtin_worker_cleanup();
    target_watchdog_feed_cleanup();
    target_climate_worker_cleanup();
    target_system_worker_cleanup();
    target_telemetry_worker_cleanup();
    target_menu_worker_cleanup();
    target_oled_worker_cleanup();
    if (restore_pm_profile) {
        esp_err_t result = esp_pm_configure(&original_pm_profile);
        restore_pm_profile = false;
        TEST_ASSERT_EQUAL(ESP_OK, result);
    }
    target_infrared_worker_cleanup();
    target_serial_monitor_cleanup();
    target_serial_buffer_cleanup();
    target_eventbus_cleanup();
    if (target_worker_initialized) {
        int32_t stopped = WatchdogWorker_Stop(&target_worker, pdMS_TO_TICKS(2000));
        if (stopped == 0) {
            vQueueDelete(target_worker.stopped);
            vSemaphoreDelete(target_worker.gate);
            target_worker.stopped = NULL;
            target_worker.gate = NULL;
            target_worker_initialized = false;
        }
        TEST_ASSERT_EQUAL(0, stopped);
    }
    if (target_wdt_owned) {
        esp_err_t result = esp_task_wdt_deinit();
        target_wdt_owned = false;
        TEST_ASSERT_EQUAL(ESP_OK, result);
    }
    if (serial_port_cleanup) {
        ISerialPort *port = SerialPort_GetESP32Instance();
        if (uart_is_driver_installed(UART_NUM_1)) uart_set_loop_back(UART_NUM_1, false);
        int32_t result = port->Deinitialize(port);
        serial_port_cleanup = false;
        TEST_ASSERT_EQUAL(0, result);
    }
    if (uart_loop_installed) {
        esp_err_t loop = uart_set_loop_back(UART_NUM_1, false);
        esp_err_t removed = uart_driver_delete(UART_NUM_1);
        uart_loop_installed = false;
        TEST_ASSERT_EQUAL(ESP_OK, loop);
        TEST_ASSERT_EQUAL(ESP_OK, removed);
    }
}

static void test_target_chip_and_flash(void) {
    esp_chip_info_t chip;
    esp_chip_info(&chip);
    TEST_ASSERT_EQUAL(CHIP_ESP32C6, chip.model);
    uint32_t bytes = 0;
    TEST_ASSERT_EQUAL(ESP_OK, esp_flash_get_size(NULL, &bytes));
    TEST_ASSERT_EQUAL_UINT32(8 * 1024 * 1024, bytes);
}

static void test_target_internal_temperature(void) {
    temperature_sensor_handle_t sensor = NULL;
    temperature_sensor_config_t config = TEMPERATURE_SENSOR_CONFIG_DEFAULT(-10, 80);
    TEST_ASSERT_EQUAL(ESP_OK, temperature_sensor_install(&config, &sensor));
    esp_err_t enabled = temperature_sensor_enable(sensor);
    float temperature = NAN;
    esp_err_t read = enabled == ESP_OK ? temperature_sensor_get_celsius(sensor, &temperature) : enabled;
    if (enabled == ESP_OK) temperature_sensor_disable(sensor);
    esp_err_t removed = temperature_sensor_uninstall(sensor);
    TEST_ASSERT_EQUAL(ESP_OK, enabled);
    TEST_ASSERT_EQUAL(ESP_OK, read);
    TEST_ASSERT_EQUAL(ESP_OK, removed);
    TEST_ASSERT_TRUE(isfinite(temperature));
    TEST_ASSERT_FLOAT_WITHIN(65.0f, 40.0f, temperature);
}

static void test_target_preset_color_conversion(void) {
    const RGBColor expected[] = {
        {0,0,255}, {0,255,255}, {0,255,0}, {255,255,0},
        {255,128,0}, {255,0,0}, {255,0,255}, {255,255,255}
    };
    for (int i = 0; i < RGB_COLOR_COUNT; ++i) {
        uint16_t x, y;
        RGBLEDColorEngine_PresetToXY(i, &x, &y);
        TEST_ASSERT_EQUAL(i, RGBLEDColorEngine_XYtoNearestPreset(x, y));
        RGBColor actual = RGBLEDColorEngine_XYtoRGB(x, y);
        TEST_ASSERT_INT_WITHIN(3, expected[i].r, actual.r);
        TEST_ASSERT_INT_WITHIN(3, expected[i].g, actual.g);
        TEST_ASSERT_INT_WITHIN(3, expected[i].b, actual.b);
    }
}

static void test_target_thermal_validation(void) {
    HardwareConfigs hw = {
        .ext_led_temp_cold_min = DEFAULT_EXT_LED_TEMP_COLD_MIN,
        .ext_led_temp_cold_max = DEFAULT_EXT_LED_TEMP_COLD_MAX,
        .ext_led_temp_warm_min = DEFAULT_EXT_LED_TEMP_WARM_MIN,
        .ext_led_temp_warm_max = DEFAULT_EXT_LED_TEMP_WARM_MAX,
        .ext_led_temp_hot_min = DEFAULT_EXT_LED_TEMP_HOT_MIN,
        .ext_led_temp_hot_max = DEFAULT_EXT_LED_TEMP_HOT_MAX,
        .ext_led_temp_danger = DEFAULT_EXT_LED_TEMP_DANGER,
        .ext_led_temp_freeze = DEFAULT_EXT_LED_TEMP_FREEZE,
    };
    TEST_ASSERT_TRUE(SystemConfigs_ValidateRGBThermal(&hw));
    hw.ext_led_temp_cold_min = hw.ext_led_temp_cold_max;
    TEST_ASSERT_FALSE(SystemConfigs_ValidateRGBThermal(&hw));
    hw.ext_led_temp_cold_min = NAN;
    TEST_ASSERT_FALSE(SystemConfigs_ValidateRGBThermal(&hw));
}

static void test_target_serial_port_ownership_and_loopback(void) {
    ISerialPort *port = SerialPort_GetESP32Instance();
    TEST_ASSERT_FALSE(uart_is_driver_installed(UART_NUM_1));
    TEST_ASSERT_EQUAL(ESP_OK, uart_driver_install(UART_NUM_1, 512, 0, 0, NULL, 0));
    uart_loop_installed = true;
    TEST_ASSERT_EQUAL(ESP_OK, uart_set_baudrate(UART_NUM_1, 9600));
    TEST_ASSERT_EQUAL(-1, port->Initialize(port, UART_NUM_1, 115200, 512, 512));
    TEST_ASSERT_EQUAL(0, port->Deinitialize(port));
    TEST_ASSERT_TRUE(uart_is_driver_installed(UART_NUM_1));
    uint32_t rate = 0;
    TEST_ASSERT_EQUAL(ESP_OK, uart_get_baudrate(UART_NUM_1, &rate));
    TEST_ASSERT_INT_WITHIN(100, 9600, rate);
    TEST_ASSERT_EQUAL(ESP_OK, uart_driver_delete(UART_NUM_1));
    uart_loop_installed = false;
    serial_port_cleanup = true;
    TEST_ASSERT_EQUAL(0, port->Initialize(port, UART_NUM_1, 115200, 512, 512));
    TEST_ASSERT_EQUAL(ESP_OK, uart_set_loop_back(UART_NUM_1, true));
    uint8_t sent[32], received[32] = {0};
    for (unsigned i = 0; i < sizeof(sent); ++i) sent[i] = (uint8_t)(i * 7);
    TEST_ASSERT_EQUAL(sizeof(sent), port->WriteBytes(port, sent, sizeof(sent)));
    TEST_ASSERT_EQUAL(0, port->WaitTxDone(port, 1000));
    TEST_ASSERT_EQUAL(sizeof(received), port->ReadBytes(port, received, sizeof(received), 1000));
    TEST_ASSERT_EQUAL_MEMORY(sent, received, sizeof(sent));
}

static void test_target_uart_hardware_loopback(void) {
    // UART1 internal loopback exercises the real UART TX/RX peripheral and ISR.
    // It does not claim coverage of external wiring or the UART0 USB bridge.
    const int rates[] = {9600, 115200, 460800};
    uart_config_t config = {
        .baud_rate = rates[0], .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE, .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE, .source_clk = UART_SCLK_DEFAULT,
    };
    TEST_ASSERT_EQUAL(ESP_OK, uart_param_config(UART_NUM_1, &config));
    TEST_ASSERT_EQUAL(ESP_OK, uart_driver_install(UART_NUM_1, 512, 0, 0, NULL, 0));
    uart_loop_installed = true;
    TEST_ASSERT_EQUAL(ESP_OK, uart_set_loop_back(UART_NUM_1, true));
    uint8_t sent[256], received[256];
    for (unsigned i=0; i<sizeof(sent); ++i) sent[i]=(uint8_t)i;
    TEST_ASSERT_EQUAL(ESP_OK, esp_pm_get_configuration(&original_pm_profile));
    restore_pm_profile = true;
    const esp_pm_config_t profiles[] = {
        {.min_freq_mhz=160, .max_freq_mhz=160, .light_sleep_enable=false},
        {.min_freq_mhz=80, .max_freq_mhz=160, .light_sleep_enable=false},
        {.min_freq_mhz=40, .max_freq_mhz=80, .light_sleep_enable=false},
    };
    for (unsigned p=0; p<sizeof(profiles)/sizeof(profiles[0]); ++p) {
      TEST_ASSERT_EQUAL(ESP_OK, esp_pm_configure(&profiles[p]));
      esp_pm_config_t actual;
      TEST_ASSERT_EQUAL(ESP_OK, esp_pm_get_configuration(&actual));
      TEST_ASSERT_EQUAL(profiles[p].min_freq_mhz, actual.min_freq_mhz);
      TEST_ASSERT_EQUAL(profiles[p].max_freq_mhz, actual.max_freq_mhz);
      TEST_ASSERT_FALSE(actual.light_sleep_enable);
    for (unsigned i=0; i<sizeof(rates)/sizeof(rates[0]); ++i) {
        TEST_ASSERT_EQUAL(ESP_OK, uart_set_baudrate(UART_NUM_1, rates[i]));
        TEST_ASSERT_EQUAL(ESP_OK, uart_flush_input(UART_NUM_1));
        memset(received, 0, sizeof(received));
        TEST_ASSERT_EQUAL(sizeof(sent), uart_write_bytes(UART_NUM_1, sent, sizeof(sent)));
        TEST_ASSERT_EQUAL(ESP_OK, uart_wait_tx_done(UART_NUM_1, pdMS_TO_TICKS(1000)));
        TEST_ASSERT_EQUAL(sizeof(received), uart_read_bytes(UART_NUM_1, received, sizeof(received), pdMS_TO_TICKS(1000)));
        TEST_ASSERT_EQUAL_MEMORY(sent, received, sizeof(sent));
    }
    }
}


/* This adapter uses the real SDK for registration/feed/delete. The production
 * Watchdog facade has separate native tests and live firmware evidence. */
static ServiceState target_watchdog_state(IService *self) { (void)self; return SERVICE_STATE_STARTED; }
static int32_t target_watchdog_add(IWatchdog *self, void *task, uint32_t timeout) {
    (void)self; (void)timeout; return esp_task_wdt_add((TaskHandle_t)task);
}
static int32_t target_watchdog_remove(IWatchdog *self, void *task) {
    (void)self;
    esp_err_t result = esp_task_wdt_delete((TaskHandle_t)task);
    if (result == ESP_OK)
        atomic_store(&worker_removed, esp_task_wdt_status((TaskHandle_t)task) == ESP_ERR_NOT_FOUND);
    return result;
}
static void target_watchdog_feed(IWatchdog *self) {
    (void)self;
    if (esp_task_wdt_reset() != ESP_OK) atomic_fetch_add(&worker_feed_errors, 1);
}
static IWatchdog target_watchdog = {
    .base = {.GetState = target_watchdog_state},
    .AddTask = target_watchdog_add, .RemoveTask = target_watchdog_remove,
    .Feed = target_watchdog_feed,
};
static void target_worker_body(void *arg) {
    (void)arg;
    for (;;) {
        WatchdogWorker_Checkpoint(&target_worker, &target_watchdog,
                                 (uint64_t)esp_timer_get_time() / 1000ULL);
        atomic_fetch_add(&worker_iterations, 1);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
static void test_target_watchdog_worker_shutdown(void) {
    if (esp_task_wdt_status(NULL) == ESP_ERR_INVALID_STATE) {
        const esp_task_wdt_config_t cfg = {.timeout_ms = 5000, .idle_core_mask = 0, .trigger_panic = true};
        TEST_ASSERT_EQUAL(ESP_OK, esp_task_wdt_init(&cfg));
        target_wdt_owned = true;
    }
    atomic_store(&worker_iterations, 0);
    atomic_store(&worker_feed_errors, 0);
    atomic_store(&worker_removed, false);
    TEST_ASSERT_EQUAL(0, WatchdogWorker_Init(&target_worker));
    target_worker_initialized = true;
    TEST_ASSERT_EQUAL(0, WatchdogWorker_Start(&target_worker, target_worker_body,
                                            "worker_test", 4096, NULL, 5));
    vTaskDelay(pdMS_TO_TICKS(100));
    TEST_ASSERT_GREATER_THAN(0, atomic_load(&worker_iterations));
    TEST_ASSERT_EQUAL(0, atomic_load(&worker_feed_errors));
    TEST_ASSERT_TRUE(atomic_load(&target_worker.registered));
    TEST_ASSERT_EQUAL(ESP_OK, esp_task_wdt_status(atomic_load(&target_worker.task)));
    TEST_ASSERT_EQUAL(0, WatchdogWorker_Stop(&target_worker, pdMS_TO_TICKS(2000)));
    TEST_ASSERT_TRUE(atomic_load(&worker_removed));
    TEST_ASSERT_NULL(atomic_load(&target_worker.task));
    unsigned iterations = atomic_load(&worker_iterations);
    vTaskDelay(pdMS_TO_TICKS(30));
    TEST_ASSERT_EQUAL(iterations, atomic_load(&worker_iterations));
}

static void test_target_cpu_timer_ownership(void) {target_cpu_verify_timer_ownership();}

void app_main(void) {
    // Allow the serial monitor to attach before emitting any Unity results.
    vTaskDelay(pdMS_TO_TICKS(10000));
    UNITY_BEGIN();
    RUN_TEST(test_target_watchdog_worker_shutdown);
    RUN_TEST(test_target_watchdog_feed_contention);
    RUN_TEST(test_target_builtin_worker_shutdown);
    RUN_TEST(test_target_application_worker_shutdown);
    RUN_TEST(test_target_application_factory_reset);
    RUN_TEST(test_target_performance_worker_shutdown);
    RUN_TEST(test_target_log_capture_lifetime);
    RUN_TEST(test_target_memory_ownership);
    RUN_TEST(test_target_error_worker_shutdown);
    RUN_TEST(test_target_flash_worker_shutdown);
    RUN_TEST(test_target_diagnostics_worker_shutdown);
    RUN_TEST(test_target_zigbee_worker_shutdown);
    RUN_TEST(test_target_eventbus_owned_payload_and_stop);
    RUN_TEST(test_target_chip_and_flash);
    RUN_TEST(test_target_uart_hardware_loopback);
    RUN_TEST(test_target_serial_port_ownership_and_loopback);
    RUN_TEST(test_target_serial_monitor_shutdown);
    RUN_TEST(test_target_serial_buffer_contract);
    RUN_TEST(test_target_infrared_worker_shutdown);
    RUN_TEST(test_target_oled_worker_shutdown);
    RUN_TEST(test_target_ui_screen_rollback);
    RUN_TEST(test_target_localization_catalogs);
    RUN_TEST(test_target_i2c_arbitration);
    RUN_TEST(test_target_i2c_sdk_recovery);
    RUN_TEST(test_target_menu_worker_shutdown);
    RUN_TEST(test_target_telemetry_worker_shutdown);
    RUN_TEST(test_target_system_worker_shutdown);
    RUN_TEST(test_target_climate_worker_shutdown);
    RUN_TEST(test_target_config_worker_shutdown);
    RUN_TEST(test_target_console_worker_shutdown);
    RUN_TEST(test_target_service_locator_lifetime);
    RUN_TEST(test_target_relay_timer_nonblocking);
    RUN_TEST(test_target_rotary_pcnt_lifecycle);
    RUN_TEST(test_target_rotary_worker_shutdown);
    RUN_TEST(test_target_rotary_typed_pin_ownership);
    RUN_TEST(test_target_internal_temperature);
    RUN_TEST(test_target_cpu_timer_ownership);
    RUN_TEST(test_target_preset_color_conversion);
    RUN_TEST(test_target_thermal_validation);
    UNITY_END();
}

