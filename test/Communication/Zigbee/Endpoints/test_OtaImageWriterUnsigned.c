#include "unity.h"
#include <string.h>

/* The production firmware is unsigned today. This deliberately omits the
 * native signed-test override and verifies that every mutating API remains
 * disabled without the trusted-app Kconfig profile. */
#include "Communication/Zigbee/Endpoints/OtaImageWriter.c"

void setUp(void) {}
void tearDown(void) {}

void test_OtaImageWriterUnsigned_ProductionSecurityGateRejectsOperations(void) {
    OtaImageWriter writer = {0};
    const OtaImageWriter before = writer;
    const uint8_t data[4] = {0xE9, 1, 2, 3};

    TEST_ASSERT_FALSE(OtaImageWriter_SigningReady());
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_SUPPORTED, OtaImageWriter_Begin(&writer, 4u));
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_SUPPORTED, OtaImageWriter_Write(&writer, data, sizeof(data)));
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_SUPPORTED, OtaImageWriter_Verify(&writer));
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_SUPPORTED, OtaImageWriter_SelectBoot(&writer));
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_SUPPORTED, OtaImageWriter_Abort(&writer));
    TEST_ASSERT_EQUAL_MEMORY(&before, &writer, sizeof(writer));
}

void test_OtaImageWriterUnsigned_RepeatedRequestsStayRejectedWithoutMutation(void) {
    OtaImageWriter writer = {0};
    const uint8_t byte = 0xE9u;
    for (unsigned attempt = 0; attempt < 4u; ++attempt) {
        TEST_ASSERT_EQUAL(ESP_ERR_NOT_SUPPORTED, OtaImageWriter_Begin(&writer, UINT32_MAX));
        TEST_ASSERT_EQUAL(ESP_ERR_NOT_SUPPORTED, OtaImageWriter_Write(&writer, &byte, 1u));
        TEST_ASSERT_EQUAL(ESP_ERR_NOT_SUPPORTED, OtaImageWriter_Abort(&writer));
        TEST_ASSERT_FALSE(writer.active);
        TEST_ASSERT_FALSE(writer.verified);
        TEST_ASSERT_FALSE(writer.boot_selected);
        TEST_ASSERT_EQUAL_UINT32(0u, writer.received_size);
    }
}

void test_OtaImageWriterUnsigned_InvalidInputsStillRejectBeforeSecurityCheck(void) {
    OtaImageWriter writer = {0};
    const uint8_t byte = 0xE9u;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, OtaImageWriter_Begin(NULL, 1u));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, OtaImageWriter_Begin(&writer, 0u));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, OtaImageWriter_Write(NULL, &byte, 1u));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, OtaImageWriter_Write(&writer, NULL, 1u));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, OtaImageWriter_Write(&writer, &byte, 0u));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, OtaImageWriter_Verify(NULL));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, OtaImageWriter_SelectBoot(NULL));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, OtaImageWriter_Abort(NULL));
}

void test_OtaImageWriterUnsigned_All(void) {
    RUN_TEST(test_OtaImageWriterUnsigned_ProductionSecurityGateRejectsOperations);
    RUN_TEST(test_OtaImageWriterUnsigned_RepeatedRequestsStayRejectedWithoutMutation);
    RUN_TEST(test_OtaImageWriterUnsigned_InvalidInputsStillRejectBeforeSecurityCheck);
}

#ifndef RACOOL_TARGET_TESTING
int main(void) {
    UNITY_BEGIN();
    test_OtaImageWriterUnsigned_All();
    return UNITY_END();
}
#endif
