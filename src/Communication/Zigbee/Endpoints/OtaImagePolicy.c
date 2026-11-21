#include "Communication/Zigbee/Endpoints/OtaImagePolicy.h"

#define OTA_FILE_ID 0x0BEEF11Eu
#define OTA_HEADER_VERSION 0x0100u
#define OTA_HEADER_MIN 56u
#define OTA_HEADER_HW_OPTION 4u
#define OTA_ELEMENT_HEADER 6u
#define OTA_APP_MAGIC 0xE9u

static uint16_t Read16(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}
static uint32_t Read32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

bool OtaImagePolicy_AcceptOffer(const OtaImagePolicy *policy,
                               const OtaImageOffer *offer) {
    if (!policy || !offer || policy->slot_size < 1u) return false;
    if (offer->manufacturer != policy->manufacturer ||
        offer->image_type != policy->image_type ||
        offer->version <= policy->current_version) return false;
    /* OTA file includes its envelope; check overflow before adding. The
     * envelope can be larger than a flash slot by a bounded header only. */
    if (policy->slot_size > UINT32_MAX - (OTA_HEADER_MIN + OTA_HEADER_HW_OPTION +
                                          OTA_ELEMENT_HEADER)) return false;
    return offer->file_size >= OTA_HEADER_MIN + OTA_HEADER_HW_OPTION +
                               OTA_ELEMENT_HEADER + 1u &&
           offer->file_size <= policy->slot_size + OTA_HEADER_MIN +
                               OTA_HEADER_HW_OPTION + OTA_ELEMENT_HEADER;
}

bool OtaImagePolicy_ValidatePrefix(const OtaImagePolicy *policy,
                                   const OtaImageOffer *offer,
                                   const uint8_t *prefix, size_t length,
                                   uint32_t *image_bytes) {
    if (!OtaImagePolicy_AcceptOffer(policy, offer) || !prefix ||
        !image_bytes || length < OTA_HEADER_MIN) return false;
    if (Read32(prefix) != OTA_FILE_ID ||
        Read16(prefix + 4) != OTA_HEADER_VERSION) return false;
    uint16_t header_len = Read16(prefix + 6);
    uint16_t flags = Read16(prefix + 8);
    /* Reject optional credential/device-specific header formats until
     * there is a specific product policy and safe parser for them. */
    /* #27 requires a checked hardware range in every release image. A
     * missing or ambiguous optional range is not acceptable. */
    if (flags != OTA_HEADER_HW_OPTION ||
        header_len != OTA_HEADER_MIN + OTA_HEADER_HW_OPTION) return false;
    if (Read16(prefix + 10) != offer->manufacturer ||
        Read16(prefix + 12) != offer->image_type ||
        Read32(prefix + 14) != offer->version ||
        Read16(prefix + 18) != 2u || /* Zigbee PRO */
        Read32(prefix + 52) != offer->file_size) return false;
    if (flags & OTA_HEADER_HW_OPTION) {
        if (length < OTA_HEADER_MIN + OTA_HEADER_HW_OPTION) return false;
        uint16_t min_hw = Read16(prefix + OTA_HEADER_MIN);
        uint16_t max_hw = Read16(prefix + OTA_HEADER_MIN + 2u);
        if (min_hw > max_hw || policy->hardware_version < min_hw ||
            policy->hardware_version > max_hw) return false;
    }
    if (length < (size_t)header_len + OTA_ELEMENT_HEADER + 1u) return false;
    const uint8_t *element = prefix + header_len;
    if (Read16(element) != 0u) return false; /* exactly one upgrade image */
    uint32_t image_size = Read32(element + 2);
    if (image_size == 0u || image_size > policy->slot_size) return false;
    if (image_size > UINT32_MAX - (uint32_t)header_len -
                     OTA_ELEMENT_HEADER) return false;
    if ((uint32_t)header_len + OTA_ELEMENT_HEADER + image_size !=
        offer->file_size) return false;
    if (element[OTA_ELEMENT_HEADER] != OTA_APP_MAGIC) return false;
    *image_bytes = image_size;
    return true;
}

bool OtaImagePolicy_AcceptBlock(uint32_t expected_offset, uint32_t offset,
                                const uint8_t *data, uint32_t length,
                                uint32_t file_size) {
    return data && length > 0u && length <= UINT8_MAX &&
           offset == expected_offset && offset < file_size &&
           length <= file_size - offset;
}
