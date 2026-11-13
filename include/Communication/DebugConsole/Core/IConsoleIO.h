#ifndef ICONSOLE_IO_H
#define ICONSOLE_IO_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/**
 * @brief Console I/O Interface (Dependency Inversion Principle)
 * Allows the DebugConsole to be independent of the physical transport (UART, USB-CDC, etc.)
 */
typedef struct IConsoleIO {
    /**
     * @brief Initialize the I/O driver
     */
    int32_t (*Initialize)(struct IConsoleIO *self);

    /**
     * @brief Deinitialize the I/O driver, releasing resources
     */
    int32_t (*Deinitialize)(struct IConsoleIO *self);

    /**
     * @brief Read data from I/O (Non-blocking or with timeout)
     */
    int (*Read)(struct IConsoleIO *self, void *buf, size_t count);

    /**
     * @brief Write data to I/O
     */
    int (*Write)(struct IConsoleIO *self, const void *buf, size_t count);

    /**
     * @brief Check if data is available (for event-based structures)
     */
    bool (*IsDataAvailable)(struct IConsoleIO *self, uint32_t timeout_ms);

    /**
     * @brief Get the event queue or handle if needed for RTOS integration
     */
    void* (*GetNativeHandle)(struct IConsoleIO *self);

} IConsoleIO;

#endif // ICONSOLE_IO_H
