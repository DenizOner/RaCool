#ifndef SERIAL_MONITOR_CONFIG_H
#define SERIAL_MONITOR_CONFIG_H

#include <stdint.h>
#include <stdbool.h>

// UART configuration defaults
#define SERIAL_MONITOR_UART_NUM 1
#define SERIAL_MONITOR_BAUD_RATE 115200
#define SERIAL_MONITOR_MIN_BAUD_RATE 300U
#define SERIAL_MONITOR_MAX_BAUD_RATE 4000000U
#define SERIAL_MONITOR_TX_BUF_SIZE 1024
#define SERIAL_MONITOR_RX_BUF_SIZE 1024
#define SERIAL_MONITOR_MAX_UART_NUM 1 

#define SERIAL_MONITOR_MAX_PLUGINS 8
#define SERIAL_MONITOR_RING_BUFFER_SIZE 2048U
#define SERIAL_MONITOR_MIN_RING_BUFFER_SIZE 32U
#define SERIAL_MONITOR_MAX_RING_BUFFER_SIZE UINT16_MAX
#define SERIAL_MONITOR_TASK_PRIORITY 2U
#define SERIAL_MONITOR_CONTROL_TIMEOUT_MS 100U
#define SERIAL_MONITOR_STOP_TIMEOUT_MS 1000U
#define SERIAL_MONITOR_FLUSH_TIMEOUT_MS 1000U
#define SERIAL_MONITOR_WORKER_STACK_SIZE 4096U
#define SERIAL_MONITOR_PERF_DEFAULT_HZ 5000U
#define SERIAL_MONITOR_PERF_MAX_HZ 5000U

// Serial Monitor configuration structure
// Only UART/buffer sizes, baud rate and task priority are configurable.
// use_async must be true. All other feature flags and reserved sizes must be
// false/zero until the corresponding feature is implemented.
typedef struct
{
    uint8_t uart_num;
    uint32_t baud_rate;
    uint16_t tx_buffer_size;
    uint16_t rx_buffer_size;
    bool echo_enabled;
    bool use_dma;              
    bool use_async;            
    uint16_t queue_size;       
    uint16_t ring_buffer_size; 
    uint8_t task_priority;     
    bool enable_binary_log;    

    // Memory Pool and Optimization settings
    bool use_memory_pool;      // Reserved memory pool option
    uint16_t pool_block_size;  // Requested pool block size
    uint16_t pool_block_count; // Requested pool block count
    
    // ISR Settings
    bool use_isr_queue;        // Reserved ISR queue option
    uint16_t isr_queue_size;   // RX event queue size
} SerialMonitorConfig;

// Function declarations
SerialMonitorConfig SerialMonitorConfig_GetDefault(void);
bool SerialMonitorConfig_IsValid(const SerialMonitorConfig *config);

#endif // SERIAL_MONITOR_CONFIG_H
