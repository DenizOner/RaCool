#include "Communication/SerialMonitor/ISerialBuffer.h"
#include "Communication/SerialMonitor/SerialMonitorConfig.h"
#include <stdatomic.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/ringbuf.h>
#include <freertos/queue.h>
#include <string.h>
#include <stdlib.h>

static const char *TAG = "SerialBuffer";

typedef struct {
    ISerialBuffer base;
    RingbufHandle_t ring_buffer;
    bool initialized;
    size_t buffer_size;
    size_t max_item_size;
    atomic_uint borrowed;
} SerialBufferImpl;

static int32_t SerialBuffer_Initialize(ISerialBuffer *self, size_t buffer_size, size_t queue_size) {
    if (!self || buffer_size < SERIAL_MONITOR_MIN_RING_BUFFER_SIZE ||
        buffer_size > SERIAL_MONITOR_MAX_RING_BUFFER_SIZE || queue_size != 0) return -1;
    SerialBufferImpl *impl = (SerialBufferImpl *)self;
    if (impl->initialized) return impl->buffer_size == buffer_size ? 0 : -1;

    if (buffer_size > 0) {
        impl->ring_buffer = xRingbufferCreate(buffer_size, RINGBUF_TYPE_NOSPLIT);
        if (impl->ring_buffer == NULL) {
            ESP_LOGE(TAG, "Failed to create ring buffer of size %zu", buffer_size);
            return -1;
        }
    }

    impl->max_item_size = xRingbufferGetMaxItemSize(impl->ring_buffer);
    if (!impl->max_item_size) {
        vRingbufferDelete(impl->ring_buffer);
        impl->ring_buffer = NULL;
        return -1;
    }
    impl->buffer_size = buffer_size;
    atomic_store(&impl->borrowed, 0);
    impl->initialized = true;
    return 0;
}

static int32_t SerialBuffer_Deinitialize(ISerialBuffer *self) {
    if (!self) return -1;
    SerialBufferImpl *impl = (SerialBufferImpl *)self;
    if (!impl->initialized) return 0;
    if (atomic_load(&impl->borrowed)) return -1;

    if (impl->ring_buffer) {
        vRingbufferDelete(impl->ring_buffer);
        impl->ring_buffer = NULL;
    }

    impl->initialized = false;
    impl->buffer_size = impl->max_item_size = 0;
    return 0;
}

static bool SerialBuffer_TimeoutTicks(uint32_t milliseconds, TickType_t *ticks) {
    uint64_t rounded = ((uint64_t)milliseconds * configTICK_RATE_HZ + 999U) / 1000U;
    if (rounded >= portMAX_DELAY) return false;
    *ticks = (TickType_t)rounded;
    return true;
}

static int32_t SerialBuffer_PushData(ISerialBuffer *self, const void *data, size_t length, uint32_t timeout_ms) {
    TickType_t ticks;
    if (!self || !data || !length || !SerialBuffer_TimeoutTicks(timeout_ms, &ticks)) return -1;
    SerialBufferImpl *impl = (SerialBufferImpl *)self;
    if (!impl->initialized || !impl->ring_buffer) return -1;
    if (length > impl->max_item_size) return -1;

    BaseType_t ret = xRingbufferSend(impl->ring_buffer, data, length, ticks);
    return (ret == pdTRUE) ? 0 : -1;
}

static void* SerialBuffer_PopData(ISerialBuffer *self, size_t *item_size, uint32_t timeout_ms) {
    TickType_t ticks;
    if (item_size) *item_size = 0;
    if (!self || !item_size || !SerialBuffer_TimeoutTicks(timeout_ms, &ticks)) return NULL;
    SerialBufferImpl *impl = (SerialBufferImpl *)self;
    if (!impl->initialized || !impl->ring_buffer) return NULL;

    void *item = xRingbufferReceive(impl->ring_buffer, item_size, ticks);
    if (item) atomic_fetch_add(&impl->borrowed, 1);
    return item;
}

static void SerialBuffer_ReturnItem(ISerialBuffer *self, void *item) {
    if (!self) return;
    SerialBufferImpl *impl = (SerialBufferImpl *)self;
    if (!impl->initialized || !impl->ring_buffer || !item || !atomic_load(&impl->borrowed)) return;

    vRingbufferReturnItem(impl->ring_buffer, item);
    atomic_fetch_sub(&impl->borrowed, 1);
}

static SerialBufferImpl serialBufferInstance = {
    .base = {
        .Initialize = SerialBuffer_Initialize,
        .Deinitialize = SerialBuffer_Deinitialize,
        .PushData = SerialBuffer_PushData,
        .PopData = SerialBuffer_PopData,
        .ReturnItem = SerialBuffer_ReturnItem
    },
    .initialized = false
};

ISerialBuffer* SerialBuffer_GetInstance(void) {
    return &serialBufferInstance.base;
}
