#ifndef RINGBUF_H
#define RINGBUF_H

#include <stddef.h>
#include <stdint.h>
#include "FreeRTOS.h"

typedef void *RingbufHandle_t;

typedef enum {
    RINGBUF_TYPE_NOSPLIT,
    RINGBUF_TYPE_ALLOWSPLIT,
    RINGBUF_TYPE_BYTEBUF
} RingbufferType_t;

#endif /* RINGBUF_H */
