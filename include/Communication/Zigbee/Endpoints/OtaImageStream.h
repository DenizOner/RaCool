#ifndef RACOOL_OTA_IMAGE_STREAM_H
#define RACOOL_OTA_IMAGE_STREAM_H

#include "Communication/Zigbee/Endpoints/OtaImagePolicy.h"
#include "esp_err.h"

/* This pure, single-owner stream processor must run outside Zigbee SDK locks
 * in a bounded flash worker. The callback must not acknowledge a block
 * before Consume returns ESP_OK; queue admission is not flash completion. */
typedef struct {
    esp_err_t (*begin)(void *ctx, uint32_t image_bytes);
    esp_err_t (*write)(void *ctx, const uint8_t *bytes, uint32_t length);
    esp_err_t (*verify)(void *ctx);
    esp_err_t (*select_boot)(void *ctx);
    esp_err_t (*abort)(void *ctx);
} OtaImageStreamOps;

typedef struct {
    OtaImagePolicy policy;
    OtaImageOffer offer;
    OtaImageStreamOps ops;
    void *ctx;
    uint8_t prefix[67];   /* Zigbee header 60, element tag/length 6, app byte 1 */
    uint32_t next_offset;
    uint32_t image_bytes;
    uint32_t written;
    uint8_t prefix_size;
    bool active;
    bool begun;
    bool verified;
    bool selected;
    bool failed;
    bool cleanup_attempted;
    bool cleanup_uncertain;
} OtaImageStream;

esp_err_t OtaImageStream_Start(OtaImageStream *stream,
                                const OtaImagePolicy *policy,
                                const OtaImageOffer *offer,
                                const OtaImageStreamOps *ops, void *ctx);
esp_err_t OtaImageStream_Consume(OtaImageStream *stream, uint32_t offset,
                                  const uint8_t *data, uint32_t length);
esp_err_t OtaImageStream_Check(OtaImageStream *stream);
esp_err_t OtaImageStream_Apply(OtaImageStream *stream);
esp_err_t OtaImageStream_Cancel(OtaImageStream *stream);

#endif
