#ifndef RACOOL_OTA_IMAGE_POLICY_H
#define RACOOL_OTA_IMAGE_POLICY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Pure Zigbee OTA file admission: no flash, NVS, SDK calls, or signature
 * verification. The ESP-IDF signed-app verifier remains mandatory before
 * any application is selected for boot. */
typedef struct {
    uint16_t manufacturer;
    uint16_t image_type;
    uint16_t hardware_version;
    uint32_t current_version;
    uint32_t slot_size;
} OtaImagePolicy;

typedef struct {
    uint16_t manufacturer;
    uint16_t image_type;
    uint32_t version;
    uint32_t file_size;
} OtaImageOffer;

/* Reports whether an announced image may be considered for download. */
bool OtaImagePolicy_AcceptOffer(const OtaImagePolicy *policy,
                               const OtaImageOffer *offer);

/* Inspect the complete OTA header and first upgrade-image element header.
 * The prefix may arrive across multiple Zigbee blocks: an incomplete buffer
 * must not pass validation. The ESP app's first image byte must be present.
 * This does NOT verify the signed firmware payload or mark it bootable. */
bool OtaImagePolicy_ValidatePrefix(const OtaImagePolicy *policy,
                                   const OtaImageOffer *offer,
                                   const uint8_t *prefix, size_t prefix_len,
                                   uint32_t *image_bytes);

/* Prevent duplicate/skipped/out-of-range OTA blocks before parser or flash. */
bool OtaImagePolicy_AcceptBlock(uint32_t expected_offset, uint32_t offset,
                                const uint8_t *data, uint32_t length,
                                uint32_t announced_file_size);

#endif
