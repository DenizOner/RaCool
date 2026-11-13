#include "unity.h"
#define ServiceLocator_GetService SerialMonitorTarget_GetService
#include "../../../../src/Communication/SerialMonitor/SerialMonitor.c"
#undef ServiceLocator_GetService
#include "driver/uart.h"
IService *SerialMonitorTarget_GetService(const char *name) { (void)name; return NULL; }
static SemaphoreHandle_t tx_release;
static atomic_bool tx_entered;
static atomic_bool reentry_rejected;
static atomic_bool block_tx;
static atomic_uint tx_callbacks;
static bool monitor_initialized;
static void target_tx_callback(ISerialMonitorPlugin *plugin, const void *data, size_t length) {
    (void)plugin; (void)data; (void)length;
    atomic_fetch_add(&tx_callbacks, 1);
    ISerialMonitor *mon = SerialMonitor_GetModule();
    atomic_store(&reentry_rejected, mon->AddPlugin(mon, plugin) == -1 &&
                 mon->RemovePlugin(mon, plugin) == -1 && mon->Flush(mon) == -1);
    atomic_store(&tx_entered, true);
    if (atomic_load(&block_tx)) xSemaphoreTake(tx_release, portMAX_DELAY);
}
static ISerialMonitorPlugin blocking_plugin = {.name="target_blocking_tx", .OnDataTransmitted=target_tx_callback};
void target_serial_monitor_cleanup(void) {
    if (tx_release) xSemaphoreGive(tx_release);
    ISerialMonitor *mon = SerialMonitor_GetModule();
    if (monitor_initialized) {
        if (uart_is_driver_installed(UART_NUM_1)) uart_set_loop_back(UART_NUM_1, false);
        int32_t result = mon->base.Stop(&mon->base);
        TEST_ASSERT_EQUAL(0, result);
        mon->RemovePlugin(mon, &blocking_plugin);
    }
    if (tx_release) vSemaphoreDelete(tx_release);
    tx_release = NULL;
    monitor_initialized = false;
}
void target_serial_monitor_verify_shutdown(void) {
    ISerialMonitor *mon = SerialMonitor_GetModule();
    TEST_ASSERT_EQUAL(0, mon->base.Initialize(&mon->base, NULL));
    monitor_initialized = true;
    SerialMonitorConfig config = SerialMonitorConfig_GetDefault();
    TEST_ASSERT_TRUE(SerialMonitorConfig_IsValid(&config));
    config.use_dma = true;
    TEST_ASSERT_EQUAL(-1, mon->SetConfig(mon, &config));
    config.use_dma = false;
    TEST_ASSERT_EQUAL(0, mon->SetConfig(mon, &config));
    TEST_ASSERT_EQUAL(0, mon->base.Start(&mon->base));
    tx_release = xSemaphoreCreateBinary(); TEST_ASSERT_NOT_NULL(tx_release);
    atomic_store(&tx_entered, false);
    atomic_store(&reentry_rejected, false);
    atomic_store(&block_tx, true);
    TEST_ASSERT_EQUAL(0, mon->AddPlugin(mon, &blocking_plugin));
    TEST_ASSERT_EQUAL(0, mon->StartUART(mon));
    TEST_ASSERT_EQUAL(ESP_OK, uart_set_loop_back(UART_NUM_1, true));
    TEST_ASSERT_EQUAL(0, mon->WriteLog(mon, "owned TX item"));
    TickType_t start = xTaskGetTickCount();
    while (!atomic_load(&tx_entered) && xTaskGetTickCount()-start < pdMS_TO_TICKS(1000)) vTaskDelay(1);
    TEST_ASSERT_TRUE(atomic_load(&tx_entered));
    TEST_ASSERT_TRUE(atomic_load(&reentry_rejected));
    TEST_ASSERT_EQUAL(-1, mon->RemovePlugin(mon, &blocking_plugin));
    TEST_ASSERT_EQUAL(1, serialMonitor.plugin_count);
    TEST_ASSERT_EQUAL(-1, mon->Flush(mon));
    TEST_ASSERT_EQUAL(1, atomic_load(&serialMonitor.pending_tx));
    TEST_ASSERT_EQUAL(-1, mon->base.Stop(&mon->base));
    TEST_ASSERT_EQUAL(-1, SerialMonitor_StartPerfTest(10));
    TEST_ASSERT_TRUE(uart_is_driver_installed(UART_NUM_1));
    TEST_ASSERT_TRUE(serialMonitor.uart_owned);
    TEST_ASSERT_NOT_NULL(atomic_load(&serialMonitor.tx_worker.task));
    TEST_ASSERT_EQUAL(-1, mon->WriteLog(mon, "late producer"));
    xSemaphoreGive(tx_release);
    TEST_ASSERT_EQUAL(ESP_OK, uart_set_loop_back(UART_NUM_1, false));
    TEST_ASSERT_EQUAL(0, mon->base.Stop(&mon->base));
    TEST_ASSERT_FALSE(uart_is_driver_installed(UART_NUM_1));
    TEST_ASSERT_NULL(atomic_load(&serialMonitor.tx_worker.task));
    TEST_ASSERT_NULL(atomic_load(&serialMonitor.rx_worker.task));
    atomic_store(&block_tx, false);
    TEST_ASSERT_EQUAL(0, mon->base.Start(&mon->base));
    TEST_ASSERT_EQUAL(0, mon->StartUART(mon));
    TEST_ASSERT_EQUAL(0, mon->WriteLog(mon, "flush queued data"));
    TEST_ASSERT_EQUAL(0, mon->Flush(mon));
    TEST_ASSERT_EQUAL(0, atomic_load(&serialMonitor.pending_tx));
    unsigned before = atomic_load(&tx_callbacks);
    TEST_ASSERT_EQUAL(0, SerialMonitor_StartPerfTest(50));
    start = xTaskGetTickCount();
    while (atomic_load(&tx_callbacks) == before && xTaskGetTickCount()-start < pdMS_TO_TICKS(1000)) vTaskDelay(1);
    TEST_ASSERT_EQUAL(0, SerialMonitor_StopPerfTest());
    TEST_ASSERT_TRUE(atomic_load(&tx_callbacks) > before);
    TEST_ASSERT_EQUAL(0, mon->Flush(mon));
    TEST_ASSERT_EQUAL(0, mon->base.Stop(&mon->base));
    TEST_ASSERT_EQUAL(-1, SerialMonitor_StartPerfTest(50));
}
