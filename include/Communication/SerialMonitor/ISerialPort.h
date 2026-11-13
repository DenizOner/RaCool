#ifndef ISERIAL_PORT_H
#define ISERIAL_PORT_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/** Hardware boundary for synchronous UART access.
 * The owner must serialize lifecycle calls and stop all readers/writers before
 * Deinitialize. Read/write calls may run concurrently while initialized.
 */
typedef struct ISerialPort ISerialPort;
struct ISerialPort {
    /** Acquire an unused UART and configure it. Existing drivers are rejected
     * without modification. Repeating the same owned configuration is idempotent;
     * changing it requires Deinitialize first. Returns 0 on success, -1 on failure.
     * Failed initialization can retain owned resources if rollback fails: call
     * Deinitialize again before retrying initialization.
     */
    int32_t (*Initialize)(ISerialPort *self, uint8_t uart_num, uint32_t baud_rate,
                          uint16_t rx_buffer_size, uint16_t tx_buffer_size);
    /** Release only this adapter's driver. Failure preserves ownership for retry. */
    int32_t (*Deinitialize)(ISerialPort *self);
    /** Return bytes written, including a possible short write; negative on error. */
    int32_t (*WriteBytes)(ISerialPort *self, const void *data, size_t length);
    /** Return bytes read, zero on timeout, or a negative error. */
    int32_t (*ReadBytes)(ISerialPort *self, void *buffer, size_t length, uint32_t timeout_ms);
    /** Wait for hardware TX completion; does not drain the facade's software queue. */
    int32_t (*WaitTxDone)(ISerialPort *self, uint32_t timeout_ms);
    /** Borrow the driver event queue while initialized; never delete this handle. */
    void* (*GetEventQueue)(ISerialPort *self);
};

ISerialPort* SerialPort_GetESP32Instance(void);
#endif // ISERIAL_PORT_H
