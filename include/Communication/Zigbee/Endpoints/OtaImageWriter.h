#ifndef RACOOL_OTA_IMAGE_WRITER_H
#define RACOOL_OTA_IMAGE_WRITER_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

/* ESP-IDF signed-OTA backend; the Zigbee SDK callback does not invoke this
 * directly. The service owner must arrange bounded worker execution and
 * acknowledge an OTA block only after the write actually succeeds. */
typedef struct {
    uint32_t expected_size;
    uint32_t received_size;
    uintptr_t handle;
    const void *partition;
    bool active;
    bool failed;
    bool verified;
    bool boot_selected;
    /* Ambiguous SDK boot-selection result must not be cleaned up as success. */
    bool boot_selection_uncertain;
} OtaImageWriter;

/* True only with software-only signed-app verification AND rollback enabled.
 * The unsigned production build is deliberately non-mutating. */
bool OtaImageWriter_SigningReady(void);

esp_err_t OtaImageWriter_Begin(OtaImageWriter *writer, uint32_t app_size);
esp_err_t OtaImageWriter_Write(OtaImageWriter *writer,
                               const uint8_t *data, uint32_t length);
esp_err_t OtaImageWriter_Verify(OtaImageWriter *writer);
esp_err_t OtaImageWriter_SelectBoot(OtaImageWriter *writer);
esp_err_t OtaImageWriter_Abort(OtaImageWriter *writer);

#endif
