#ifndef UART_CONSOLE_IO_H
#define UART_CONSOLE_IO_H

#include "Communication/DebugConsole/Core/IConsoleIO.h"
#include <driver/uart.h>

/**
 * @brief Compatibility factory for borrowed console transports.
 * 
 * @param uart_num Zero selects SDK stdin/stdout; other indices borrow an installed UART.
 * @return IConsoleIO* Pointer to the driver instance
 */
IConsoleIO* UartConsoleIO_GetDriver(uart_port_t uart_num);

/** Borrow ESP-IDF's configured stdin/stdout console (USB Serial/JTAG in RaCool).
 * The adapter does not install, delete or close the SDK console transport. */
IConsoleIO* StdioConsoleIO_GetDriver(void);

#endif // UART_CONSOLE_IO_H
