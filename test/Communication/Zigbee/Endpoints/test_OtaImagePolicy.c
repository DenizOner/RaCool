#include <string.h>
#include "unity.h"
#include "Communication/Zigbee/Endpoints/OtaImagePolicy.h"
#include "Communication/Zigbee/Endpoints/OtaImagePolicy.c"

static const OtaImagePolicy policy = {
    .manufacturer = 0x1234u, .image_type = 0x1234u,
    .hardware_version = 1u, .current_version = 0x01020000u,
    .slot_size = 0x300000u
};
static OtaImageOffer offer;
static uint8_t prefix[80];
static void W16(uint8_t *p, uint16_t value) {
    p[0] = (uint8_t)value; p[1] = (uint8_t)(value >> 8);
}
static void W32(uint8_t *p, uint32_t value) {
    W16(p, (uint16_t)value); W16(p + 2, (uint16_t)(value >> 16));
}
void setUp(void) {
    offer = (OtaImageOffer){.manufacturer = policy.manufacturer,
        .image_type = policy.image_type, .version = policy.current_version + 1u,
        .file_size = 63u};
    memset(prefix, 0, sizeof(prefix));
    W32(prefix, 0x0BEEF11Eu);
    W16(prefix + 4, 0x0100u);
    W16(prefix + 6, 56u);
    W16(prefix + 10, policy.manufacturer);
    W16(prefix + 12, policy.image_type);
    W32(prefix + 14, offer.version);
    W32(prefix + 52, offer.file_size);
    W32(prefix + 58, 1u);
    prefix[62] = 0xE9u;
}
void tearDown(void) {}
void test_OtaImagePolicy_ValidCandidateAndAppPrefix(void) {
    uint32_t app_bytes = 0;
    TEST_ASSERT_TRUE(OtaImagePolicy_AcceptOffer(&policy, &offer));
    TEST_ASSERT_TRUE(OtaImagePolicy_ValidatePrefix(&policy, &offer,
                                                  prefix, 63u, &app_bytes));
    TEST_ASSERT_EQUAL_UINT32(1u, app_bytes);
}
void test_OtaImagePolicy_RejectDifferentManufacturerAndImageType(void) {
    offer.manufacturer++;
    TEST_ASSERT_FALSE(OtaImagePolicy_AcceptOffer(&policy, &offer));
    offer.manufacturer--;
    offer.image_type++;
    TEST_ASSERT_FALSE(OtaImagePolicy_AcceptOffer(&policy, &offer));
}
void test_OtaImagePolicy_RejectEqualOlderAndOversizedImage(void) {
    offer.version = policy.current_version;
    TEST_ASSERT_FALSE(OtaImagePolicy_AcceptOffer(&policy, &offer));
    offer.version--;
    TEST_ASSERT_FALSE(OtaImagePolicy_AcceptOffer(&policy, &offer));
    offer.version = policy.current_version + 1u;
    offer.file_size = policy.slot_size + 100u;
    TEST_ASSERT_FALSE(OtaImagePolicy_AcceptOffer(&policy, &offer));
    offer.file_size = 62u;
    TEST_ASSERT_FALSE(OtaImagePolicy_AcceptOffer(&policy, &offer));
}
void test_OtaImagePolicy_RejectTruncatedOrInvalidEnvelope(void) {
    uint32_t bytes = 9u;
    TEST_ASSERT_FALSE(OtaImagePolicy_ValidatePrefix(&policy, &offer, prefix, 62u, &bytes));
    TEST_ASSERT_FALSE(OtaImagePolicy_ValidatePrefix(&policy, &offer, NULL, 63u, &bytes));
    prefix[0] ^= 1u;
    TEST_ASSERT_FALSE(OtaImagePolicy_ValidatePrefix(&policy, &offer, prefix, 63u, &bytes));
    prefix[0] ^= 1u;
    W16(prefix + 4, 0x0101u);
    TEST_ASSERT_FALSE(OtaImagePolicy_ValidatePrefix(&policy, &offer, prefix, 63u, &bytes));
    W16(prefix + 4, 0x0100u);
    W32(prefix + 52, 64u);
    TEST_ASSERT_FALSE(OtaImagePolicy_ValidatePrefix(&policy, &offer, prefix, 63u, &bytes));
}
void test_OtaImagePolicy_RejectUnsafeFlagsAndHardwareRange(void) {
    uint32_t bytes = 0;
    W16(prefix + 8, 2u); /* device-specific, not supported */
    TEST_ASSERT_FALSE(OtaImagePolicy_ValidatePrefix(&policy, &offer, prefix, 63u, &bytes));
    W16(prefix + 8, 4u);
    W16(prefix + 6, 60u);
    W16(prefix + 56, 2u);
    W16(prefix + 58, 3u);
    W16(prefix + 62, 0u);
    W32(prefix + 64, 1u);
    prefix[68] = 0xE9u;
    offer.file_size = 69u;
    W32(prefix + 52, 69u);
    TEST_ASSERT_FALSE(OtaImagePolicy_ValidatePrefix(&policy, &offer, prefix, 69u, &bytes));
    W16(prefix + 56, 1u);
    TEST_ASSERT_TRUE(OtaImagePolicy_ValidatePrefix(&policy, &offer, prefix, 69u, &bytes));
}
void test_OtaImagePolicy_RejectElementSizeTagAndMagic(void) {
    uint32_t bytes = 0;
    W16(prefix + 56, 1u);
    TEST_ASSERT_FALSE(OtaImagePolicy_ValidatePrefix(&policy, &offer, prefix, 63u, &bytes));
    W16(prefix + 56, 0u);
    W32(prefix + 58, 2u);
    TEST_ASSERT_FALSE(OtaImagePolicy_ValidatePrefix(&policy, &offer, prefix, 63u, &bytes));
    W32(prefix + 58, 1u);
    prefix[62] = 0x00u;
    TEST_ASSERT_FALSE(OtaImagePolicy_ValidatePrefix(&policy, &offer, prefix, 63u, &bytes));
}
void test_OtaImagePolicy_RejectDuplicateSkippedAndOverrunBlocks(void) {
    uint8_t block[4] = {1,2,3,4};
    TEST_ASSERT_TRUE(OtaImagePolicy_AcceptBlock(0u, 0u, block, 4u, 10u));
    TEST_ASSERT_TRUE(OtaImagePolicy_AcceptBlock(4u, 4u, block, 4u, 10u));
    TEST_ASSERT_FALSE(OtaImagePolicy_AcceptBlock(4u, 0u, block, 4u, 10u));
    TEST_ASSERT_FALSE(OtaImagePolicy_AcceptBlock(4u, 5u, block, 4u, 10u));
    TEST_ASSERT_FALSE(OtaImagePolicy_AcceptBlock(8u, 8u, block, 4u, 10u));
    TEST_ASSERT_FALSE(OtaImagePolicy_AcceptBlock(UINT32_MAX, UINT32_MAX, block, 4u, UINT32_MAX));
    TEST_ASSERT_FALSE(OtaImagePolicy_AcceptBlock(0u, 0u, NULL, 4u, 10u));
    TEST_ASSERT_FALSE(OtaImagePolicy_AcceptBlock(0u, 0u, block, 0u, 10u));
    TEST_ASSERT_FALSE(OtaImagePolicy_AcceptBlock(0u, 0u, block, 256u, 300u));
}
void test_OtaImagePolicy_NullAndExtremeInputsAreSafe(void) {
    uint32_t bytes = 0;
    OtaImagePolicy huge = policy;
    huge.slot_size = UINT32_MAX;
    TEST_ASSERT_FALSE(OtaImagePolicy_AcceptOffer(NULL, &offer));
    TEST_ASSERT_FALSE(OtaImagePolicy_AcceptOffer(&policy, NULL));
    TEST_ASSERT_FALSE(OtaImagePolicy_AcceptOffer(&huge, &offer));
    TEST_ASSERT_FALSE(OtaImagePolicy_ValidatePrefix(&policy, &offer, prefix, 63u, NULL));
    TEST_ASSERT_EQUAL_UINT32(0, bytes);
}

void test_OtaImagePolicy_All(void) {
    RUN_TEST(test_OtaImagePolicy_ValidCandidateAndAppPrefix);
    RUN_TEST(test_OtaImagePolicy_RejectDifferentManufacturerAndImageType);
    RUN_TEST(test_OtaImagePolicy_RejectEqualOlderAndOversizedImage);
    RUN_TEST(test_OtaImagePolicy_RejectTruncatedOrInvalidEnvelope);
    RUN_TEST(test_OtaImagePolicy_RejectUnsafeFlagsAndHardwareRange);
    RUN_TEST(test_OtaImagePolicy_RejectElementSizeTagAndMagic);
    RUN_TEST(test_OtaImagePolicy_RejectDuplicateSkippedAndOverrunBlocks);
    RUN_TEST(test_OtaImagePolicy_NullAndExtremeInputsAreSafe);
}
#ifndef RACOOL_TARGET_TESTING
int main(void) {
    UNITY_BEGIN();
    test_OtaImagePolicy_All();
    return UNITY_END();
}
#endif
