#ifndef ISERIAL_BUFFER_H
#define ISERIAL_BUFFER_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/**
 * @file ISerialBuffer.h
 * @brief Asynchronous memory buffer and queue interface.
 * Copies producer data into an ESP-IDF no-split ring buffer; PopData borrows
 * an item until ReturnItem. There is no separate memory pool or ISR queue.
 * The owner must serialize Initialize/Deinitialize and quiesce producers and
 * consumers before either lifecycle call. Deinitialize rejects borrowed items;
 * queued but unborrowed data is discarded. Return each borrowed pointer exactly
 * once, unchanged. Waits are finite; unrepresentable tick durations are rejected.
 */
typedef struct ISerialBuffer ISerialBuffer;

struct ISerialBuffer {
    /**
     * @brief Initializes the buffer manager.
     * @param self Pointer to ISerialBuffer instance.
     * @param buffer_size Total buffer memory size.
     * @param queue_size Reserved, must be zero.
     * @return 0 on success, -1 on failure.
     */
    int32_t (*Initialize)(ISerialBuffer *self, size_t buffer_size, size_t queue_size);

    /**
     * @brief Releases buffer resources.
     * @param self Pointer to ISerialBuffer instance.
     * @return 0 on success, -1 on failure.
     */
    int32_t (*Deinitialize)(ISerialBuffer *self);

    /**
     * @brief Copies data into the buffer; producer storage may expire on return.
     * @param self Pointer to ISerialBuffer instance.
     * @param data Pointer to source data.
     * @param length Length of data in bytes.
     * @param timeout_ms Timeout in milliseconds (0 for non-blocking).
     * @return 0 on success, -1 if buffer full or timeout.
     */
    int32_t (*PushData)(ISerialBuffer *self, const void *data, size_t length, uint32_t timeout_ms);

    /**
     * @brief Pops a data buffer item without copying payload.
     * @param self Pointer to ISerialBuffer instance.
     * @param item_size Output pointer receiving item byte length.
     * @param timeout_ms Timeout in milliseconds.
     * @return Data pointer or NULL if empty/timeout.
     */
    void* (*PopData)(ISerialBuffer *self, size_t *item_size, uint32_t timeout_ms);

    /**
     * @brief Returns a popped item to its ring buffer after processing.
     * @param self Pointer to ISerialBuffer instance.
     * @param item Pointer to item buffer block to return.
     */
    void (*ReturnItem)(ISerialBuffer *self, void *item);
};

#endif // ISERIAL_BUFFER_H
