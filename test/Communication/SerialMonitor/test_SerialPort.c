#include "unity.h"
#include "Infrastructure/support/test_mocks.h"
#include "driver/uart.h"
#define UART_DATA_8_BITS 3
#define UART_PARITY_DISABLE 0
#define UART_STOP_BITS_1 1
#define UART_HW_FLOWCTRL_DISABLE 0
#define UART_SCLK_DEFAULT 0
static bool installed;
static int installs, configs, deletes, reads, writes;
static esp_err_t install_result, config_result, delete_result;
bool uart_is_driver_installed(int port) { (void)port; return installed; }
esp_err_t uart_driver_install(int port, int rx, int tx, int count, QueueHandle_t *queue, int flags) {
    (void)port; (void)rx; (void)tx; (void)count; (void)flags; installs++;
    if (install_result == ESP_OK) { installed = true; *queue = (QueueHandle_t)0x123; }
    return install_result;
}
esp_err_t uart_param_config(int port, const uart_config_t *config) {
    (void)port; TEST_ASSERT_TRUE(installed); TEST_ASSERT_NOT_NULL(config); configs++; return config_result;
}
esp_err_t uart_driver_delete(int port) {
    (void)port; deletes++; if (delete_result == ESP_OK) installed = false; return delete_result;
}
int uart_write_bytes(int port, const void *data, size_t length) {
    (void)port; (void)data; writes++; return (int)length;
}
int uart_read_bytes(int port, void *data, uint32_t length, TickType_t ticks) {
    (void)port; (void)data; (void)length; (void)ticks; reads++; return 0;
}
esp_err_t uart_wait_tx_done(int port, TickType_t ticks) { (void)port; (void)ticks; return ESP_OK; }
#include "Communication/SerialMonitor/SerialPort_ESP32.c"
void setUp(void) {
    ISerialPort interface = serialPortInstance.base;
    memset(&serialPortInstance, 0, sizeof(serialPortInstance)); serialPortInstance.base = interface;
    installed = false; installs = configs = deletes = reads = writes = 0;
    install_result = config_result = delete_result = ESP_OK;
}
void tearDown(void) {}
void test_SerialPort_RejectsForeignDriver(void) {
    ISerialPort *port = SerialPort_GetESP32Instance(); installed = true;
    TEST_ASSERT_EQUAL(-1, port->Initialize(port, 0, 115200, 1024, 1024));
    TEST_ASSERT_EQUAL(0, installs); TEST_ASSERT_EQUAL(0, configs);
    TEST_ASSERT_EQUAL(0, port->Deinitialize(port)); TEST_ASSERT_EQUAL(0, deletes);
    TEST_ASSERT_TRUE(installed);
}
void test_SerialPort_OwnedLifecycleAndConfiguration(void) {
    ISerialPort *port = SerialPort_GetESP32Instance();
    TEST_ASSERT_EQUAL(0, port->Initialize(port, 1, 115200, 1024, 1024));
    TEST_ASSERT_EQUAL_PTR((void *)0x123, port->GetEventQueue(port));
    TEST_ASSERT_EQUAL(0, port->Initialize(port, 1, 115200, 1024, 1024));
    TEST_ASSERT_EQUAL(-1, port->Initialize(port, 0, 115200, 1024, 1024));
    TEST_ASSERT_EQUAL(-1, port->Initialize(port, 1, 9600, 1024, 1024));
    TEST_ASSERT_EQUAL(1, installs); TEST_ASSERT_EQUAL(1, configs);
    TEST_ASSERT_EQUAL(0, port->Deinitialize(port)); TEST_ASSERT_FALSE(installed);
    TEST_ASSERT_NULL(port->GetEventQueue(port));
    TEST_ASSERT_EQUAL(0, port->Deinitialize(port)); TEST_ASSERT_EQUAL(1, deletes);
}
void test_SerialPort_InstallFailureDoesNotConfigureOrDelete(void) {
    ISerialPort *port = SerialPort_GetESP32Instance(); install_result = ESP_FAIL;
    TEST_ASSERT_EQUAL(-1, port->Initialize(port, 1, 115200, 1024, 1024));
    TEST_ASSERT_EQUAL(0, configs); TEST_ASSERT_EQUAL(0, deletes);
    TEST_ASSERT_FALSE(serialPortInstance.owns_driver);
}
void test_SerialPort_ConfigFailureCleanupRetry(void) {
    ISerialPort *port = SerialPort_GetESP32Instance(); config_result = ESP_FAIL; delete_result = ESP_FAIL;
    TEST_ASSERT_EQUAL(-1, port->Initialize(port, 1, 115200, 1024, 1024));
    TEST_ASSERT_TRUE(serialPortInstance.owns_driver); TEST_ASSERT_FALSE(serialPortInstance.initialized);
    TEST_ASSERT_NULL(port->GetEventQueue(port));
    TEST_ASSERT_EQUAL(-1, port->Initialize(port, 1, 115200, 1024, 1024));
    TEST_ASSERT_EQUAL(1, installs);
    delete_result = ESP_OK;
    TEST_ASSERT_EQUAL(0, port->Deinitialize(port)); TEST_ASSERT_FALSE(installed);
    config_result = ESP_OK;
    TEST_ASSERT_EQUAL(0, port->Initialize(port, 1, 115200, 1024, 1024));
    TEST_ASSERT_EQUAL(2, installs);
}
void test_SerialPort_DeleteFailurePreservesOwnership(void) {
    ISerialPort *port = SerialPort_GetESP32Instance();
    TEST_ASSERT_EQUAL(0, port->Initialize(port, 1, 115200, 1024, 1024));
    delete_result = ESP_FAIL;
    TEST_ASSERT_EQUAL(-1, port->Deinitialize(port));
    TEST_ASSERT_TRUE(serialPortInstance.owns_driver); TEST_ASSERT_TRUE(installed);
    delete_result = ESP_OK; TEST_ASSERT_EQUAL(0, port->Deinitialize(port));
    TEST_ASSERT_FALSE(serialPortInstance.owns_driver); TEST_ASSERT_EQUAL(2, deletes);
}
void test_SerialPort_InputBoundaries(void) {
    ISerialPort *port = SerialPort_GetESP32Instance(); char buffer[2];
    TEST_ASSERT_EQUAL(-1, port->Initialize(NULL, 1, 115200, 1024, 1024));
    TEST_ASSERT_EQUAL(-1, port->Initialize(port, 2, 115200, 1024, 1024));
    TEST_ASSERT_EQUAL(-1, port->Initialize(port, 1, UINT32_MAX, 1024, 1024));
    TEST_ASSERT_EQUAL(-1, port->Initialize(port, 1, 115200, 128, 1024));
    TEST_ASSERT_EQUAL(-1, port->Initialize(port, 1, 115200, 1024, 128));
    TEST_ASSERT_EQUAL(0, installs);
    TEST_ASSERT_EQUAL(0, port->Initialize(port, 1, 115200, 1024, 0));
    TEST_ASSERT_EQUAL(-1, port->ReadBytes(port, NULL, 1, 0));
    TEST_ASSERT_EQUAL(-1, port->WriteBytes(port, buffer, SIZE_MAX));
    TEST_ASSERT_EQUAL(-1, port->ReadBytes(port, buffer, SIZE_MAX, 0));
    TEST_ASSERT_EQUAL(0, reads); TEST_ASSERT_EQUAL(0, writes);
    TEST_ASSERT_EQUAL(2, port->WriteBytes(port, buffer, 2));
    TEST_ASSERT_EQUAL(0, port->ReadBytes(port, buffer, 2, 0));
}
