#include "unity.h"
#include "esp_ota_ops.h"
#include <string.h>

#define RACOOL_OTA_WRITER_NATIVE_TEST 1
#include "Communication/Zigbee/Endpoints/OtaImageWriter.c"

static esp_partition_t running, inactive;
static bool haveTarget, haveRunning;
static esp_err_t beginResult, writeResult, endResult, selectResult, abortResult;
static int beginCalls, writeCalls, endCalls, selectCalls, abortCalls;
static esp_ota_handle_t activeHandle;
static OtaImageWriter writer;
static uint8_t data[4] = {0xE9, 1, 2, 3};

const esp_partition_t *esp_ota_get_next_update_partition(const esp_partition_t *start) {
    TEST_ASSERT_NULL(start);
    return haveTarget ? &inactive : NULL;
}
const esp_partition_t *esp_ota_get_running_partition(void) {
    return haveRunning ? &running : NULL;
}
esp_err_t esp_ota_begin(const esp_partition_t *target, size_t size, esp_ota_handle_t *handle) {
    TEST_ASSERT_EQUAL_PTR(&inactive, target);
    TEST_ASSERT_TRUE(size <= inactive.size);
    beginCalls++;
    if (beginResult == ESP_OK) *handle = activeHandle;
    return beginResult;
}
esp_err_t esp_ota_write(esp_ota_handle_t handle, const void *buf, size_t size) {
    TEST_ASSERT_EQUAL(activeHandle, handle);
    TEST_ASSERT_NOT_NULL(buf);
    TEST_ASSERT_TRUE(size <= inactive.size);
    writeCalls++;
    return writeResult;
}
esp_err_t esp_ota_end(esp_ota_handle_t handle) {
    TEST_ASSERT_EQUAL(activeHandle, handle);
    endCalls++;
    return endResult;
}
esp_err_t esp_ota_set_boot_partition(const esp_partition_t *target) {
    TEST_ASSERT_EQUAL_PTR(&inactive, target);
    selectCalls++;
    return selectResult;
}
esp_err_t esp_ota_abort(esp_ota_handle_t handle) {
    TEST_ASSERT_EQUAL(activeHandle, handle);
    abortCalls++;
    return abortResult;
}
void setUp(void) {
    running = (esp_partition_t){.size=3000u};
    inactive = (esp_partition_t){.size=2000u};
    haveTarget = haveRunning = true;
    beginResult = writeResult = endResult = selectResult = abortResult = ESP_OK;
    beginCalls = writeCalls = endCalls = selectCalls = abortCalls = 0;
    activeHandle = (esp_ota_handle_t)42u;
    writer = (OtaImageWriter){0};
}
void tearDown(void) {}

void test_Writer_ValidatesSignatureGate(void) {
    TEST_ASSERT_TRUE(OtaImageWriter_SigningReady());
}
void test_Writer_StartRequiresInactiveSlotAndCapacity(void) {
    haveTarget = false;
    TEST_ASSERT_NOT_EQUAL(ESP_OK, OtaImageWriter_Begin(&writer, 100u));
    TEST_ASSERT_EQUAL(0, beginCalls);
    haveTarget = true;
    haveRunning = false;
    TEST_ASSERT_NOT_EQUAL(ESP_OK, OtaImageWriter_Begin(&writer, 100u));
    haveRunning = true;
    TEST_ASSERT_NOT_EQUAL(ESP_OK, OtaImageWriter_Begin(&writer, 2001u));
    TEST_ASSERT_EQUAL(0, beginCalls);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, OtaImageWriter_Begin(&writer, 0u));
    TEST_ASSERT_EQUAL(0, beginCalls);
}
void test_Writer_BeginFailureDoesNotAdmitWrite(void) {
    beginResult = ESP_FAIL;
    TEST_ASSERT_EQUAL(ESP_FAIL, OtaImageWriter_Begin(&writer, 4u));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, OtaImageWriter_Write(&writer, data, 4u));
    TEST_ASSERT_EQUAL(0, writeCalls);
}
void test_Writer_SuccessRequiresCompleteVerifiedWriteBeforeBoot(void) {
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, OtaImageWriter_SelectBoot(&writer));
    TEST_ASSERT_EQUAL(ESP_OK, OtaImageWriter_Begin(&writer, 4u));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, OtaImageWriter_Begin(&writer, 4u));
    TEST_ASSERT_EQUAL(ESP_OK, OtaImageWriter_Write(&writer, data, 2u));
    TEST_ASSERT_EQUAL(ESP_OK, OtaImageWriter_Write(&writer, data+2, 2u));
    TEST_ASSERT_EQUAL(ESP_OK, OtaImageWriter_Verify(&writer));
    TEST_ASSERT_EQUAL(ESP_OK, OtaImageWriter_SelectBoot(&writer));
    TEST_ASSERT_EQUAL(1, beginCalls);
    TEST_ASSERT_EQUAL(2, writeCalls);
    TEST_ASSERT_EQUAL(1, endCalls);
    TEST_ASSERT_EQUAL(1, selectCalls);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, OtaImageWriter_SelectBoot(&writer));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, OtaImageWriter_Abort(&writer));
}
void test_Writer_IncompleteWriteAbortsAndNeverSelectsBoot(void) {
    TEST_ASSERT_EQUAL(ESP_OK, OtaImageWriter_Begin(&writer, 4u));
    TEST_ASSERT_EQUAL(ESP_OK, OtaImageWriter_Write(&writer, data, 2u));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, OtaImageWriter_Verify(&writer));
    TEST_ASSERT_EQUAL(1, abortCalls);
    TEST_ASSERT_EQUAL(0, endCalls);
    TEST_ASSERT_EQUAL(0, selectCalls);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, OtaImageWriter_SelectBoot(&writer));
}
void test_Writer_OverrunAbortsWithoutWritingExtraBytes(void) {
    TEST_ASSERT_EQUAL(ESP_OK, OtaImageWriter_Begin(&writer, 4u));
    TEST_ASSERT_EQUAL(ESP_OK, OtaImageWriter_Write(&writer, data, 3u));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, OtaImageWriter_Write(&writer, data, 2u));
    TEST_ASSERT_EQUAL(1, writeCalls);
    TEST_ASSERT_EQUAL(1, abortCalls);
    TEST_ASSERT_EQUAL(0, selectCalls);
}
void test_Writer_WriteFailureAbortsAndRetainsFailClosedState(void) {
    TEST_ASSERT_EQUAL(ESP_OK, OtaImageWriter_Begin(&writer, 4u));
    writeResult = ESP_FAIL;
    TEST_ASSERT_EQUAL(ESP_FAIL, OtaImageWriter_Write(&writer, data, 4u));
    TEST_ASSERT_EQUAL(1, abortCalls);
    TEST_ASSERT_EQUAL(0, endCalls);
    TEST_ASSERT_EQUAL(0, selectCalls);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, OtaImageWriter_SelectBoot(&writer));
}
void test_Writer_CryptographicEndFailureCannotSelectBoot(void) {
    TEST_ASSERT_EQUAL(ESP_OK, OtaImageWriter_Begin(&writer, 4u));
    TEST_ASSERT_EQUAL(ESP_OK, OtaImageWriter_Write(&writer, data, 4u));
    endResult = ESP_FAIL;
    TEST_ASSERT_EQUAL(ESP_FAIL, OtaImageWriter_Verify(&writer));
    TEST_ASSERT_EQUAL(1, endCalls);
    TEST_ASSERT_EQUAL(0, abortCalls);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, OtaImageWriter_SelectBoot(&writer));
}
void test_Writer_BootSelectionFailureCanRetryWithoutReset(void) {
    TEST_ASSERT_EQUAL(ESP_OK, OtaImageWriter_Begin(&writer, 4u));
    TEST_ASSERT_EQUAL(ESP_OK, OtaImageWriter_Write(&writer, data, 4u));
    TEST_ASSERT_EQUAL(ESP_OK, OtaImageWriter_Verify(&writer));
    selectResult = ESP_FAIL;
    TEST_ASSERT_EQUAL(ESP_FAIL, OtaImageWriter_SelectBoot(&writer));
    TEST_ASSERT_FALSE(writer.boot_selected);
    TEST_ASSERT_EQUAL(ESP_OK, OtaImageWriter_Abort(&writer));
    TEST_ASSERT_EQUAL(0, abortCalls); /* verified but not pending handle */
}
void test_Writer_AbortFailureRetainsOwnedHandleForRetry(void) {
    TEST_ASSERT_EQUAL(ESP_OK, OtaImageWriter_Begin(&writer, 4u));
    abortResult = ESP_FAIL;
    TEST_ASSERT_EQUAL(ESP_FAIL, OtaImageWriter_Abort(&writer));
    TEST_ASSERT_TRUE(writer.active);
    TEST_ASSERT_EQUAL(activeHandle, writer.handle);
    abortResult = ESP_OK;
    TEST_ASSERT_EQUAL(ESP_OK, OtaImageWriter_Abort(&writer));
    TEST_ASSERT_FALSE(writer.active);
    TEST_ASSERT_EQUAL(2, abortCalls);
}
void test_Writer_NullInputOrEmptyWriteCannotMutate(void) {
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, OtaImageWriter_Begin(NULL, 4u));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, OtaImageWriter_Abort(NULL));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, OtaImageWriter_Verify(NULL));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, OtaImageWriter_SelectBoot(NULL));
    TEST_ASSERT_EQUAL(ESP_OK, OtaImageWriter_Begin(&writer, 4u));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, OtaImageWriter_Write(&writer, NULL, 4u));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, OtaImageWriter_Write(&writer, data, 0u));
    TEST_ASSERT_EQUAL(0, writeCalls);
    TEST_ASSERT_EQUAL(ESP_OK, OtaImageWriter_Abort(&writer));
}

void test_OtaImageWriter_All(void) {
    RUN_TEST(test_Writer_ValidatesSignatureGate);
    RUN_TEST(test_Writer_StartRequiresInactiveSlotAndCapacity);
    RUN_TEST(test_Writer_BeginFailureDoesNotAdmitWrite);
    RUN_TEST(test_Writer_SuccessRequiresCompleteVerifiedWriteBeforeBoot);
    RUN_TEST(test_Writer_IncompleteWriteAbortsAndNeverSelectsBoot);
    RUN_TEST(test_Writer_OverrunAbortsWithoutWritingExtraBytes);
    RUN_TEST(test_Writer_WriteFailureAbortsAndRetainsFailClosedState);
    RUN_TEST(test_Writer_CryptographicEndFailureCannotSelectBoot);
    RUN_TEST(test_Writer_BootSelectionFailureCanRetryWithoutReset);
    RUN_TEST(test_Writer_AbortFailureRetainsOwnedHandleForRetry);
    RUN_TEST(test_Writer_NullInputOrEmptyWriteCannotMutate);
}
#ifndef RACOOL_TARGET_TESTING
int main(void) {
    UNITY_BEGIN();
    test_OtaImageWriter_All();
    return UNITY_END();
}
#endif
