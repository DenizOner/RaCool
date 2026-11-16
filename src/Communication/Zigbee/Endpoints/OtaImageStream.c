#include "Communication/Zigbee/Endpoints/OtaImageStream.h"

#include <string.h>

#define RACOOL_OTA_PREFIX_SIZE 67u

static esp_err_t StreamFail(OtaImageStream *stream, esp_err_t reason) {
    stream->failed = true;
    /* Abort the backend at most once. A failed abort cannot be interpreted
     * as completed cleanup and must never be retried blindly. */
    if (stream->begun && !stream->cleanup_attempted) {
        stream->cleanup_attempted = true;
        if (stream->ops.abort(stream->ctx) != ESP_OK)
            stream->cleanup_uncertain = true;
    }
    return reason;
}

esp_err_t OtaImageStream_Start(OtaImageStream *stream,
                                const OtaImagePolicy *policy,
                                const OtaImageOffer *offer,
                                const OtaImageStreamOps *ops, void *ctx) {
    if (!stream || !policy || !offer || !ops ||
        !ops->begin || !ops->write || !ops->verify ||
        !ops->select_boot || !ops->abort || !ctx)
        return ESP_ERR_INVALID_ARG;
    if (stream->active) return ESP_ERR_INVALID_STATE;
    if (!OtaImagePolicy_AcceptOffer(policy, offer)) return ESP_ERR_INVALID_ARG;
    memset(stream, 0, sizeof(*stream));
    stream->policy = *policy;
    stream->offer = *offer;
    stream->ops = *ops;
    stream->ctx = ctx;
    stream->active = true;
    return ESP_OK;
}

esp_err_t OtaImageStream_Consume(OtaImageStream *stream, uint32_t offset,
                                  const uint8_t *data, uint32_t length) {
    if (!stream) return ESP_ERR_INVALID_ARG;
    if (!stream->active || stream->failed ||
        stream->verified || stream->selected) return ESP_ERR_INVALID_STATE;
    if (!OtaImagePolicy_AcceptBlock(stream->next_offset, offset, data, length,
                                    stream->offer.file_size))
        return StreamFail(stream, ESP_ERR_INVALID_ARG);

    uint32_t cursor = 0;
    if (!stream->begun) {
        uint32_t remaining = RACOOL_OTA_PREFIX_SIZE - stream->prefix_size;
        uint32_t take = length < remaining ? length : remaining;
        memcpy(stream->prefix + stream->prefix_size, data, take);
        stream->prefix_size += (uint8_t)take;
        cursor = take;
        if (stream->prefix_size == RACOOL_OTA_PREFIX_SIZE) {
            uint32_t image_bytes = 0;
            if (!OtaImagePolicy_ValidatePrefix(&stream->policy, &stream->offer,
                                               stream->prefix,
                                               sizeof(stream->prefix), &image_bytes))
                return StreamFail(stream, ESP_ERR_INVALID_ARG);
            esp_err_t result = stream->ops.begin(stream->ctx, image_bytes);
            if (result != ESP_OK) return StreamFail(stream, result);
            stream->begun = true;
            stream->image_bytes = image_bytes;
            /* The byte used to validate the ESP header is also the first
             * image byte; don't lose it or write the Zigbee wrapper. */
            result = stream->ops.write(stream->ctx,
                                       &stream->prefix[RACOOL_OTA_PREFIX_SIZE - 1u], 1u);
            if (result != ESP_OK) return StreamFail(stream, result);
            stream->written = 1u;
        }
    }

    if (stream->begun && cursor < length) {
        uint32_t tail = length - cursor;
        if (stream->written >= stream->image_bytes ||
            tail > stream->image_bytes - stream->written)
            return StreamFail(stream, ESP_ERR_INVALID_ARG);
        esp_err_t result = stream->ops.write(stream->ctx, data + cursor, tail);
        if (result != ESP_OK) return StreamFail(stream, result);
        stream->written += tail;
    }
    stream->next_offset += length;
    return ESP_OK;
}

esp_err_t OtaImageStream_Check(OtaImageStream *stream) {
    if (!stream) return ESP_ERR_INVALID_ARG;
    if (!stream->active || stream->failed ||
        !stream->begun || stream->verified || stream->selected)
        return ESP_ERR_INVALID_STATE;
    if (stream->next_offset != stream->offer.file_size ||
        stream->written != stream->image_bytes)
        return StreamFail(stream, ESP_ERR_INVALID_STATE);
    esp_err_t result = stream->ops.verify(stream->ctx);
    if (result != ESP_OK) return StreamFail(stream, result);
    stream->verified = true;
    return ESP_OK;
}

esp_err_t OtaImageStream_Apply(OtaImageStream *stream) {
    if (!stream) return ESP_ERR_INVALID_ARG;
    if (!stream->active || stream->failed ||
        !stream->verified || stream->selected) return ESP_ERR_INVALID_STATE;
    esp_err_t result = stream->ops.select_boot(stream->ctx);
    if (result != ESP_OK) return StreamFail(stream, result);
    stream->selected = true;
    return ESP_OK;
}

esp_err_t OtaImageStream_Cancel(OtaImageStream *stream) {
    if (!stream) return ESP_ERR_INVALID_ARG;
    if (!stream->active || stream->selected ||
        stream->cleanup_uncertain) return ESP_ERR_INVALID_STATE;
    if (stream->begun && !stream->cleanup_attempted) {
        stream->cleanup_attempted = true;
        esp_err_t result = stream->ops.abort(stream->ctx);
        if (result != ESP_OK) {
            stream->failed = true;
            stream->cleanup_uncertain = true;
            return result;
        }
    }
    memset(stream, 0, sizeof(*stream));
    return ESP_OK;
}
