#include "Communication/SerialMonitor/ISerialPort.h"
#include <driver/uart.h>
#include <limits.h>
#include "Communication/SerialMonitor/SerialMonitorConfig.h"
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

static const char *TAG = "SerialPort_ESP32";

typedef struct {
    ISerialPort base;
    uint8_t uart_num;
    QueueHandle_t uart_event_queue;
    bool initialized;
    bool owns_driver;
    uint32_t baud_rate;
    uint16_t rx_buffer_size;
    uint16_t tx_buffer_size;
} SerialPortESP32Impl;

/* Lifecycle calls require quiescent readers/writers, serialized by the owner. */
static int32_t SerialPort_Deinitialize(ISerialPort *self) {
    if (!self) return -1;
    SerialPortESP32Impl *impl = (SerialPortESP32Impl *)self;
    if (!impl->owns_driver) return 0;
    if (uart_driver_delete(impl->uart_num) != ESP_OK) return -1;
    impl->initialized = false;
    impl->owns_driver = false;
    impl->uart_event_queue = NULL;
    return 0;
}

static int32_t SerialPort_Initialize(ISerialPort *self, uint8_t uart_num, uint32_t baud_rate, uint16_t rx_buffer_size, uint16_t tx_buffer_size) {
    if (!self || uart_num > SERIAL_MONITOR_MAX_UART_NUM ||
        baud_rate < SERIAL_MONITOR_MIN_BAUD_RATE || baud_rate > SERIAL_MONITOR_MAX_BAUD_RATE ||
        rx_buffer_size <= UART_HW_FIFO_LEN(uart_num) ||
        (tx_buffer_size != 0 && tx_buffer_size <= UART_HW_FIFO_LEN(uart_num))) return -1;
    SerialPortESP32Impl *impl = (SerialPortESP32Impl *)self;
    if (impl->initialized) {
        return impl->uart_num == uart_num && impl->baud_rate == baud_rate &&
            impl->rx_buffer_size == rx_buffer_size && impl->tx_buffer_size == tx_buffer_size ? 0 : -1;
    }
    // Pending cleanup must finish before another initialization attempt.
    if (impl->owns_driver || uart_is_driver_installed(uart_num)) return -1;
    QueueHandle_t queue = NULL;
    if (uart_driver_install(uart_num, rx_buffer_size, tx_buffer_size, 20, &queue, 0) != ESP_OK) return -1;
    impl->uart_num = uart_num;
    impl->owns_driver = true;
    impl->uart_event_queue = queue;
    uart_config_t uart_config = {
        .baud_rate = (int)baud_rate,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    // Install first: a failed ownership acquisition must not reconfigure another user.
    if (uart_param_config(uart_num, &uart_config) != ESP_OK) {
        if (SerialPort_Deinitialize(self) != 0) ESP_LOGE(TAG, "UART cleanup failed; retry Deinitialize");
        return -1;
    }
    // Pin routing belongs to the board configuration; no redundant NO_CHANGE call.
    impl->baud_rate = baud_rate;
    impl->rx_buffer_size = rx_buffer_size;
    impl->tx_buffer_size = tx_buffer_size;
    impl->initialized = true;
    ESP_LOGI(TAG, "UART%d initialized at %lu baud", uart_num, (unsigned long)baud_rate);
    return 0;
}

static int32_t SerialPort_WriteBytes(ISerialPort *self, const void *data, size_t length) {
    if (!self) return -1;
    SerialPortESP32Impl *impl = (SerialPortESP32Impl *)self;
    if (!impl->initialized) return -1;

    if (!data || length == 0 || length > INT_MAX) return -1;
    return uart_write_bytes(impl->uart_num, data, length);
}

static int32_t SerialPort_ReadBytes(ISerialPort *self, void *buffer, size_t length, uint32_t timeout_ms) {
    SerialPortESP32Impl *impl = (SerialPortESP32Impl *)self;
    if (!impl->initialized) return -1;

    if (!buffer || length == 0 || length > INT_MAX) return -1;
    return uart_read_bytes(impl->uart_num, buffer, length, pdMS_TO_TICKS(timeout_ms));
}

static int32_t SerialPort_WaitTxDone(ISerialPort *self, uint32_t timeout_ms) {
    SerialPortESP32Impl *impl = (SerialPortESP32Impl *)self;
    if (!impl->initialized) return -1;

    esp_err_t ret = uart_wait_tx_done(impl->uart_num, pdMS_TO_TICKS(timeout_ms));
    return (ret == ESP_OK) ? 0 : -1;
}

static void* SerialPort_GetEventQueue(ISerialPort *self) {
    if (!self) return NULL;
    SerialPortESP32Impl *impl = (SerialPortESP32Impl *)self;
    return impl->initialized ? (void *)impl->uart_event_queue : NULL;
}

static SerialPortESP32Impl serialPortInstance = {
    .base = {
        .Initialize = SerialPort_Initialize,
        .Deinitialize = SerialPort_Deinitialize,
        .WriteBytes = SerialPort_WriteBytes,
        .ReadBytes = SerialPort_ReadBytes,
        .WaitTxDone = SerialPort_WaitTxDone,
        .GetEventQueue = SerialPort_GetEventQueue
    },
    .initialized = false
};

ISerialPort* SerialPort_GetESP32Instance(void) {
    return &serialPortInstance.base;
}
