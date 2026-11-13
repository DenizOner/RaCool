#include "Communication/SerialMonitor/SerialMonitorConfig.h"
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"

SerialMonitorConfig SerialMonitorConfig_GetDefault(void)
{
    SerialMonitorConfig config = {
        .uart_num = SERIAL_MONITOR_UART_NUM,
        .baud_rate = SERIAL_MONITOR_BAUD_RATE,
        .tx_buffer_size = SERIAL_MONITOR_TX_BUF_SIZE,
        .rx_buffer_size = SERIAL_MONITOR_RX_BUF_SIZE,
        .echo_enabled = false,
        .use_dma = false,
        .use_async = true,
        .queue_size = 0,
        .ring_buffer_size = SERIAL_MONITOR_RING_BUFFER_SIZE,
        .task_priority = SERIAL_MONITOR_TASK_PRIORITY,
        .enable_binary_log = false,
        
        // Memory Pool defaults
        .use_memory_pool = false,
        .pool_block_size = 0,
        .pool_block_count = 0,
        
        // ISR defaults
        .use_isr_queue = false,
        .isr_queue_size = 0
    };
    return config;
}

bool SerialMonitorConfig_IsValid(const SerialMonitorConfig *config)
{
    if (!config)
    {
        return false;
    }

    // UART number range
    if (config->uart_num > SERIAL_MONITOR_MAX_UART_NUM)
    {
        return false;
    }

    // Baud rate range
    if (config->baud_rate < SERIAL_MONITOR_MIN_BAUD_RATE || config->baud_rate > SERIAL_MONITOR_MAX_BAUD_RATE)
    {
        return false;
    }

    // Match the UART driver's buffer constraints; TX zero means unbuffered.
    if (config->rx_buffer_size <= UART_HW_FIFO_LEN(config->uart_num) ||
        (config->tx_buffer_size && config->tx_buffer_size <= UART_HW_FIFO_LEN(config->uart_num)) ||
        config->ring_buffer_size < SERIAL_MONITOR_MIN_RING_BUFFER_SIZE ||
        config->task_priority == 0 || config->task_priority >= configMAX_PRIORITIES)
    {
        return false;
    }

    // The implementation always uses asynchronous copied ring-buffer writes.
    // Reserved options must not advertise capabilities that are not implemented.
    if (!config->use_async || config->echo_enabled || config->use_dma ||
        config->enable_binary_log || config->queue_size || config->use_memory_pool ||
        config->pool_block_size || config->pool_block_count || config->use_isr_queue ||
        config->isr_queue_size) return false;

    return true;
}
