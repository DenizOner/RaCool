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
    /* A write/length failure forbids further writes but allows one Abort. */
    bool write_failed;
    bool verified;
    bool boot_selected;
    /* Ambiguous SDK boot-selection result must not be cleaned up as success. */
    bool boot_selection_uncertain;
} OtaImageWriter;

/* Each Write accepts at most one Zigbee OTA transport block (1..255 bytes).
 * This bounds the incremental ESP-IDF erase to one 4 KiB sector per call.
 * It is not a measured watchdog or Zigbee callback timing guarantee. */
#define RACOOL_OTA_WRITER_MAX_CHUNK_BYTES UINT8_MAX

/* Write/Verify failures do not abort their own SDK handle. The sole stream
 * owner must call Abort once after a failure, then keep the failed session
 * closed. A failed Abort or failed Begin is terminal until owner recovery. */

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
