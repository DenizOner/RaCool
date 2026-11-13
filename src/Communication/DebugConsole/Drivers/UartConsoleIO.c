#include "Communication/DebugConsole/Drivers/UartConsoleIO.h"
#include "Communication/DebugConsole/DebugConsoleConfig.h"
#include <driver/uart.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <string.h>
#include <ctype.h>
#include "esp_log.h"
#include "esp_rom_sys.h"
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <limits.h>
#include <errno.h>

typedef struct {
    IConsoleIO base;
    uart_port_t uart_num;
    QueueHandle_t uart_queue;
    bool initialized;
} UartConsoleIO;

static int32_t UartIO_Initialize(IConsoleIO *self) {
    UartConsoleIO *impl = (UartConsoleIO *)self;
    if (impl->initialized) return 0;


    if (impl->uart_num == 0) {
        impl->uart_queue = NULL;
        int flags = fcntl(fileno(stdin), F_GETFL, 0);
        if (flags < 0 || fcntl(fileno(stdin), F_SETFL, flags | O_NONBLOCK) < 0) return -1;
        impl->initialized = true;
        esp_rom_printf("[ConsoleIO] Attached to ESP-IDF stdio console (non-blocking)\n");
        return 0;
    }
    
    /* Other UARTs are borrowed from their owner, never reconfigured here. */
    if (!uart_is_driver_installed(impl->uart_num)) return -1;
    impl->initialized = true;
    return 0;
}

static int32_t UartIO_Deinitialize(IConsoleIO *self) {
    UartConsoleIO *impl = (UartConsoleIO *)self;
    if (!impl->initialized) return 0;
    /* Both stdio and installed UART drivers are borrowed, not owned. */
    impl->uart_queue = NULL;
    impl->initialized = false;
    return 0;
}

static int UartIO_Read(IConsoleIO *self, void *buf, size_t count) {
    UartConsoleIO *impl = (UartConsoleIO *)self;
    if (!impl->initialized || (!buf && count) || count > INT_MAX) return -1;
    if (impl->uart_num == 0) {
        ssize_t n = read(fileno(stdin), buf, count);
        return n >= 0 ? (int)n : ((errno == EAGAIN || errno == EWOULDBLOCK) ? 0 : -1);
    }
    return uart_read_bytes(impl->uart_num, buf, count, 0);
}

static int UartIO_Write(IConsoleIO *self, const void *buf, size_t count) {
    UartConsoleIO *impl = (UartConsoleIO *)self;
    if (!impl->initialized || (!buf && count) || count > INT_MAX) return -1;
    if (impl->uart_num == 0) {
        ssize_t n = write(fileno(stdout), buf, count);
        return n >= 0 ? (int)n : ((errno == EAGAIN || errno == EWOULDBLOCK) ? 0 : -1);
    }
    return uart_write_bytes(impl->uart_num, buf, count);
}

static bool UartIO_IsDataAvailable(IConsoleIO *self, uint32_t timeout_ms) {
    UartConsoleIO *impl = (UartConsoleIO *)self;
    if (!impl->initialized) return false;
    TickType_t wait = pdMS_TO_TICKS(timeout_ms);
    if (timeout_ms && !wait) wait = 1;
    if (impl->uart_num == 0) {
        if (timeout_ms > 0) {
            vTaskDelay(wait);
        }
        return true; // The console task probes stdin with a non-blocking read.
    }

    size_t length = 0;
    if (uart_get_buffered_data_len(impl->uart_num, &length) == ESP_OK && length > 0) {
        return true;
    }
    
    if (timeout_ms > 0 && impl->uart_queue != NULL) {
        uart_event_t event;
        if (xQueueReceive(impl->uart_queue, &event, wait) == pdTRUE) {
            if (event.type == UART_DATA) {
                return true;
            }
        }
    } else if (timeout_ms > 0) {
        vTaskDelay(wait);
        if (uart_get_buffered_data_len(impl->uart_num, &length) == ESP_OK && length > 0) {
            return true;
        }
    }
    
    return false;
}

static void* UartIO_GetNativeHandle(IConsoleIO *self) {
    UartConsoleIO *impl = (UartConsoleIO *)self;
    return (void *)impl->uart_queue;
}

IConsoleIO* UartConsoleIO_GetDriver(uart_port_t uart_num) {
    static UartConsoleIO drivers[UART_NUM_MAX] = {0};
    if ((int)uart_num < 0 || uart_num >= UART_NUM_MAX) return NULL;
    
    if (drivers[uart_num].base.Initialize == NULL) {
        drivers[uart_num].base.Initialize = UartIO_Initialize;
        drivers[uart_num].base.Deinitialize = UartIO_Deinitialize;
        drivers[uart_num].base.Read = UartIO_Read;
        drivers[uart_num].base.Write = UartIO_Write;
        drivers[uart_num].base.IsDataAvailable = UartIO_IsDataAvailable;
        drivers[uart_num].base.GetNativeHandle = UartIO_GetNativeHandle;
        drivers[uart_num].uart_num = uart_num;
        drivers[uart_num].initialized = false;
    }
    return &drivers[uart_num].base;
}

IConsoleIO* StdioConsoleIO_GetDriver(void) {
    return UartConsoleIO_GetDriver(UART_NUM_0);
}
