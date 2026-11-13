#ifndef UART_H
#define UART_H

#include "esp_err.h"

typedef int uart_port_t;
#define UART_NUM_0 0
#define UART_HW_FIFO_LEN(port) 128

typedef struct {
    int baud_rate;
    int data_bits;
    int parity;
    int stop_bits;
    int flow_ctrl;
    uint8_t rx_flow_ctrl_thresh;
    int source_clk;
} uart_config_t;

#endif /* UART_H */
