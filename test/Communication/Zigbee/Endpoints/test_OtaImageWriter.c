#include "unity.h"
#include "esp_ota_ops.h"
#include <string.h>

#define RACOOL_OTA_WRITER_NATIVE_TEST 1
#include "Communication/Zigbee/Endpoints/OtaImageWriter.c"
#include "Communication/Zigbee/Endpoints/OtaImageStream.c"
#include "Communication/Zigbee/Endpoints/OtaImagePolicy.c"

static esp_partition_t running, inactive;
static bool haveTarget, haveRunning, returnRunningSlot;
static bool haveBootReadback, selectActuallyChangesBoot;
static esp_err_t beginResult, writeResult, endResult, selectResult, abortResult;
static int beginCalls, writeCalls, endCalls, selectCalls, abortCalls;
static esp_ota_handle_t activeHandle;
static size_t beginImageMode;
static const esp_partition_t *bootPartition;
static OtaImageWriter writer;
static uint8_t data[4] = {0xE9, 1, 2, 3};

const esp_partition_t *esp_ota_get_next_update_partition(const esp_partition_t *start) {
    TEST_ASSERT_NULL(start);
    return haveTarget ? (returnRunningSlot ? &running : &inactive) : NULL;
}
const esp_partition_t *esp_ota_get_running_partition(void) {
    return haveRunning ? &running : NULL;
}
const esp_partition_t *esp_ota_get_boot_partition(void) {
    return haveBootReadback ? bootPartition : NULL;
}
esp_err_t esp_ota_begin(const esp_partition_t *target, size_t size, esp_ota_handle_t *handle) {
    TEST_ASSERT_EQUAL_PTR(&inactive, target);
    TEST_ASSERT_EQUAL((size_t)OTA_WITH_SEQUENTIAL_WRITES, size);
    beginImageMode = size;
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
    if (selectActuallyChangesBoot) bootPartition = &inactive;
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
    returnRunningSlot = false;
    haveBootReadback = selectActuallyChangesBoot = true;
    bootPartition = &running;
    beginResult = writeResult = endResult = selectResult = abortResult = ESP_OK;
    beginCalls = writeCalls = endCalls = selectCalls = abortCalls = 0;
    beginImageMode = 0u;
    activeHandle = (esp_ota_handle_t)42u;
    writer = (OtaImageWriter){0};
}
void tearDown(void) {}

void test_Writer_ValidatesSignatureGate(void) {
    TEST_ASSERT_TRUE(OtaImageWriter_SigningReady());
}
void test_Writer_BeginUsesSequentialEraseWithVerifiedAppLength(void) {
    /* Caller knows the exact size, but IDF Begin must not bulk-erase it.
     * The writer independently preserves the true payload boundary. */
    TEST_ASSERT_EQUAL(ESP_OK, OtaImageWriter_Begin(&writer, 512u));
    TEST_ASSERT_EQUAL((size_t)OTA_WITH_SEQUENTIAL_WRITES, beginImageMode);
    TEST_ASSERT_EQUAL_UINT32(512u, writer.expected_size);
    TEST_ASSERT_EQUAL_UINT32(0u, writer.received_size);
    TEST_ASSERT_EQUAL(ESP_OK, OtaImageWriter_Abort(&writer));
    TEST_ASSERT_EQUAL(1, abortCalls);
}
void test_Writer_RejectsOversizedWriteBeforeFlashMutation(void) {
    uint8_t payload[256] = {0xE9u};
    TEST_ASSERT_EQUAL(ESP_OK, OtaImageWriter_Begin(&writer, 512u));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,
        OtaImageWriter_Write(&writer, payload, sizeof(payload)));
    TEST_ASSERT_TRUE(writer.write_failed);
    TEST_ASSERT_EQUAL(0, writeCalls);
    TEST_ASSERT_EQUAL_UINT32(0u, writer.received_size);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, OtaImageWriter_Verify(&writer));
    TEST_ASSERT_EQUAL(ESP_OK, OtaImageWriter_Abort(&writer));
    TEST_ASSERT_EQUAL(1, abortCalls);
}
void test_Writer_AcceptsMaximumZigbeeBlockSequentially(void) {
    uint8_t payload[255] = {0xE9u};
    TEST_ASSERT_EQUAL(ESP_OK, OtaImageWriter_Begin(&writer, sizeof(payload)));
    TEST_ASSERT_EQUAL(ESP_OK, OtaImageWriter_Write(&writer, payload, sizeof(payload)));
    TEST_ASSERT_EQUAL(1, writeCalls);
    TEST_ASSERT_EQUAL_UINT32(sizeof(payload), writer.received_size);
    TEST_ASSERT_EQUAL(ESP_OK, OtaImageWriter_Verify(&writer));
    TEST_ASSERT_EQUAL(ESP_OK, OtaImageWriter_SelectBoot(&writer));
    TEST_ASSERT_EQUAL(1, selectCalls);
}
void test_Writer_RejectsRunningPartitionAsTarget(void) {
    returnRunningSlot = true;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, OtaImageWriter_Begin(&writer, 100u));
    TEST_ASSERT_EQUAL(0, beginCalls);
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
    TEST_ASSERT_TRUE(writer.failed);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, OtaImageWriter_Write(&writer, data, 4u));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, OtaImageWriter_Abort(&writer));
    beginResult = ESP_OK;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, OtaImageWriter_Begin(&writer, 4u));
    TEST_ASSERT_EQUAL(1, beginCalls);
    TEST_ASSERT_EQUAL(0, writeCalls);
    TEST_ASSERT_EQUAL(0, abortCalls);
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
void test_Writer_IncompleteWriteRequiresOwnerAbort(void) {
    TEST_ASSERT_EQUAL(ESP_OK, OtaImageWriter_Begin(&writer, 4u));
    TEST_ASSERT_EQUAL(ESP_OK, OtaImageWriter_Write(&writer, data, 2u));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, OtaImageWriter_Verify(&writer));
    TEST_ASSERT_TRUE(writer.write_failed);
    TEST_ASSERT_EQUAL(0, abortCalls);
    TEST_ASSERT_EQUAL(0, endCalls);
    TEST_ASSERT_EQUAL(0, selectCalls);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, OtaImageWriter_Write(&writer, data, 2u));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, OtaImageWriter_SelectBoot(&writer));
    TEST_ASSERT_EQUAL(ESP_OK, OtaImageWriter_Abort(&writer));
    TEST_ASSERT_EQUAL(1, abortCalls);
}
void test_Writer_OverrunLeavesAbortToOwnerWithoutWritingExtraBytes(void) {
    TEST_ASSERT_EQUAL(ESP_OK, OtaImageWriter_Begin(&writer, 4u));
    TEST_ASSERT_EQUAL(ESP_OK, OtaImageWriter_Write(&writer, data, 3u));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, OtaImageWriter_Write(&writer, data, 2u));
    TEST_ASSERT_TRUE(writer.write_failed);
    TEST_ASSERT_EQUAL(1, writeCalls);
    TEST_ASSERT_EQUAL(0, abortCalls);
    TEST_ASSERT_EQUAL(0, selectCalls);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, OtaImageWriter_Verify(&writer));
    TEST_ASSERT_EQUAL(ESP_OK, OtaImageWriter_Abort(&writer));
    TEST_ASSERT_EQUAL(1, abortCalls);
}
void test_Writer_WriteFailureBlocksWritesUntilOwnerAbort(void) {
    TEST_ASSERT_EQUAL(ESP_OK, OtaImageWriter_Begin(&writer, 4u));
    writeResult = ESP_FAIL;
    TEST_ASSERT_EQUAL(ESP_FAIL, OtaImageWriter_Write(&writer, data, 4u));
    TEST_ASSERT_TRUE(writer.write_failed);
    TEST_ASSERT_FALSE(writer.failed);
    TEST_ASSERT_EQUAL(0, abortCalls);
    writeResult = ESP_OK;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, OtaImageWriter_Write(&writer, data, 4u));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, OtaImageWriter_Verify(&writer));
    TEST_ASSERT_EQUAL(0, endCalls);
    TEST_ASSERT_EQUAL(0, selectCalls);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, OtaImageWriter_SelectBoot(&writer));
    TEST_ASSERT_EQUAL(ESP_OK, OtaImageWriter_Abort(&writer));
    TEST_ASSERT_EQUAL(1, abortCalls);
}
void test_Writer_CryptographicEndFailureCannotSelectBoot(void) {
    TEST_ASSERT_EQUAL(ESP_OK, OtaImageWriter_Begin(&writer, 4u));
    TEST_ASSERT_EQUAL(ESP_OK, OtaImageWriter_Write(&writer, data, 4u));
    endResult = ESP_FAIL;
    TEST_ASSERT_EQUAL(ESP_FAIL, OtaImageWriter_Verify(&writer));
    TEST_ASSERT_EQUAL(1, endCalls);
    TEST_ASSERT_EQUAL(0, abortCalls);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, OtaImageWriter_SelectBoot(&writer));
    TEST_ASSERT_EQUAL(ESP_OK, OtaImageWriter_Abort(&writer));
    TEST_ASSERT_EQUAL(0, abortCalls);
}
void test_Writer_MissingHandleOnVerifyCannotBeResetOrRetried(void) {
    TEST_ASSERT_EQUAL(ESP_OK, OtaImageWriter_Begin(&writer, 4u));
    TEST_ASSERT_EQUAL(ESP_OK, OtaImageWriter_Write(&writer, data, 4u));
    endResult = ESP_ERR_NOT_FOUND;
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND, OtaImageWriter_Verify(&writer));
    TEST_ASSERT_TRUE(writer.failed);
    TEST_ASSERT_FALSE(writer.active);
    TEST_ASSERT_FALSE(writer.verified);
    TEST_ASSERT_EQUAL(0, writer.handle);
    endResult = ESP_OK;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, OtaImageWriter_Abort(&writer));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, OtaImageWriter_Begin(&writer, 4u));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, OtaImageWriter_Verify(&writer));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, OtaImageWriter_SelectBoot(&writer));
    TEST_ASSERT_EQUAL(1, beginCalls);
    TEST_ASSERT_EQUAL(1, endCalls);
    TEST_ASSERT_EQUAL(0, selectCalls);
    TEST_ASSERT_EQUAL(0, abortCalls);
}
static void writeAndVerify(void) {
    TEST_ASSERT_EQUAL(ESP_OK, OtaImageWriter_Begin(&writer, 4u));
    TEST_ASSERT_EQUAL(ESP_OK, OtaImageWriter_Write(&writer, data, 4u));
    TEST_ASSERT_EQUAL(ESP_OK, OtaImageWriter_Verify(&writer));
}
void test_Writer_FailedBootSelectionWithChangedReadbackLocksOwner(void) {
    writeAndVerify();
    selectResult = ESP_FAIL;
    TEST_ASSERT_EQUAL(ESP_FAIL, OtaImageWriter_SelectBoot(&writer));
    TEST_ASSERT_EQUAL_PTR(&inactive, esp_ota_get_boot_partition());
    TEST_ASSERT_TRUE(writer.failed);
    TEST_ASSERT_TRUE(writer.boot_selection_uncertain);
    TEST_ASSERT_FALSE(writer.boot_selected);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, OtaImageWriter_SelectBoot(&writer));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, OtaImageWriter_Abort(&writer));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, OtaImageWriter_Begin(&writer, 4u));
    TEST_ASSERT_EQUAL(1, selectCalls);
    TEST_ASSERT_EQUAL(0, abortCalls);
}
void test_Writer_FailedBootSelectionEvenIfOldReadbackLocksOwner(void) {
    writeAndVerify();
    selectActuallyChangesBoot = false;
    selectResult = ESP_FAIL;
    TEST_ASSERT_EQUAL(ESP_FAIL, OtaImageWriter_SelectBoot(&writer));
    TEST_ASSERT_EQUAL_PTR(&running, esp_ota_get_boot_partition());
    TEST_ASSERT_TRUE(writer.boot_selection_uncertain);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, OtaImageWriter_Abort(&writer));
    TEST_ASSERT_EQUAL(1, selectCalls);
}
void test_Writer_SuccessfulSdkReturnWithoutMatchingReadbackFailsClosed(void) {
    writeAndVerify();
    selectActuallyChangesBoot = false;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, OtaImageWriter_SelectBoot(&writer));
    TEST_ASSERT_TRUE(writer.failed);
    TEST_ASSERT_TRUE(writer.boot_selection_uncertain);
    TEST_ASSERT_FALSE(writer.boot_selected);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, OtaImageWriter_Abort(&writer));
    TEST_ASSERT_EQUAL(1, selectCalls);
}
void test_Writer_BeginCannotErasePreviouslySelectedBootSlot(void) {
    bootPartition = &inactive;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, OtaImageWriter_Begin(&writer, 4u));
    TEST_ASSERT_EQUAL(0, beginCalls);
    bootPartition = &running;
    haveBootReadback = false;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, OtaImageWriter_Begin(&writer, 4u));
    TEST_ASSERT_EQUAL(0, beginCalls);
}
void test_Writer_MissingBootReadbackAtSelectLocksOwner(void) {
    writeAndVerify();
    haveBootReadback = false;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, OtaImageWriter_SelectBoot(&writer));
    TEST_ASSERT_TRUE(writer.boot_selection_uncertain);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, OtaImageWriter_Abort(&writer));
    TEST_ASSERT_EQUAL(0, selectCalls);
    TEST_ASSERT_EQUAL(0, abortCalls);
}
void test_Writer_BootChoiceChangedBeforeSelectLocksOwner(void) {
    writeAndVerify();
    bootPartition = &inactive;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, OtaImageWriter_SelectBoot(&writer));
    TEST_ASSERT_TRUE(writer.boot_selection_uncertain);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, OtaImageWriter_SelectBoot(&writer));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, OtaImageWriter_Abort(&writer));
    TEST_ASSERT_EQUAL(0, selectCalls);
    TEST_ASSERT_EQUAL(0, abortCalls);
}
void test_Writer_AbortMissingHandleMustNotBeRetried(void) {
    TEST_ASSERT_EQUAL(ESP_OK, OtaImageWriter_Begin(&writer, 4u));
    abortResult = ESP_ERR_NOT_FOUND;
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND, OtaImageWriter_Abort(&writer));
    TEST_ASSERT_TRUE(writer.active);
    TEST_ASSERT_TRUE(writer.failed);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, OtaImageWriter_Write(&writer, data, 4u));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, OtaImageWriter_Verify(&writer));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, OtaImageWriter_Begin(&writer, 4u));
    TEST_ASSERT_EQUAL(0, writeCalls);
    TEST_ASSERT_EQUAL(activeHandle, writer.handle);
    abortResult = ESP_OK;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, OtaImageWriter_Abort(&writer));
    TEST_ASSERT_EQUAL(1, abortCalls);
}
void test_Writer_WriteErrorWithFailedAbortCannotBeRetried(void) {
    TEST_ASSERT_EQUAL(ESP_OK, OtaImageWriter_Begin(&writer, 4u));
    writeResult = ESP_FAIL;
    abortResult = ESP_ERR_NOT_FOUND;
    TEST_ASSERT_EQUAL(ESP_FAIL, OtaImageWriter_Write(&writer, data, 4u));
    TEST_ASSERT_TRUE(writer.write_failed);
    TEST_ASSERT_FALSE(writer.failed);
    TEST_ASSERT_EQUAL(0, abortCalls);
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND, OtaImageWriter_Abort(&writer));
    TEST_ASSERT_TRUE(writer.failed);
    writeResult = abortResult = ESP_OK;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, OtaImageWriter_Write(&writer, data, 4u));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, OtaImageWriter_Abort(&writer));
    TEST_ASSERT_EQUAL(1, abortCalls);
    TEST_ASSERT_EQUAL(1, writeCalls);
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

/* Exercise the real stream and writer composition, not only separate
 * mock callbacks. Exactly one layer owns the SDK abort on write errors. */
static esp_err_t streamWriterBegin(void *ctx, uint32_t size) {
    return OtaImageWriter_Begin((OtaImageWriter *)ctx, size);
}
static esp_err_t streamWriterWrite(void *ctx, const uint8_t *bytes, uint32_t size) {
    return OtaImageWriter_Write((OtaImageWriter *)ctx, bytes, size);
}
static esp_err_t streamWriterVerify(void *ctx) {
    return OtaImageWriter_Verify((OtaImageWriter *)ctx);
}
static esp_err_t streamWriterSelect(void *ctx) {
    return OtaImageWriter_SelectBoot((OtaImageWriter *)ctx);
}
static esp_err_t streamWriterAbort(void *ctx) {
    return OtaImageWriter_Abort((OtaImageWriter *)ctx);
}
static const OtaImageStreamOps writerStreamOps = {
    .begin = streamWriterBegin, .write = streamWriterWrite,
    .verify = streamWriterVerify, .select_boot = streamWriterSelect,
    .abort = streamWriterAbort
};
static void streamFileW16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
}
static void streamFileW32(uint8_t *p, uint32_t v) {
    streamFileW16(p, (uint16_t)v);
    streamFileW16(p+2, (uint16_t)(v >> 16));
}
static void setupWriterStream(OtaImageStream *stream, uint8_t file[69]) {
    const OtaImagePolicy policy = {
        .manufacturer = 0x1234u, .image_type = 0x1234u,
        .hardware_version = 1u, .current_version = 1u, .slot_size = 2000u
    };
    const OtaImageOffer offer = {
        .manufacturer = 0x1234u, .image_type = 0x1234u,
        .version = 2u, .file_size = 69u
    };
    memset(file, 0, 69u);
    streamFileW32(file, 0x0BEEF11Eu);
    streamFileW16(file+4, 0x0100u);
    streamFileW16(file+6, 60u);
    streamFileW16(file+8, 4u);
    streamFileW16(file+10, 0x1234u);
    streamFileW16(file+12, 0x1234u);
    streamFileW32(file+14, 2u);
    streamFileW16(file+18, 2u);
    streamFileW32(file+52, 69u);
    streamFileW16(file+56, 1u);
    streamFileW16(file+58, 1u);
    streamFileW16(file+60, 0u);
    streamFileW32(file+62, 3u);
    file[66] = 0xE9u;
    file[67] = 1u;
    file[68] = 2u;
    TEST_ASSERT_EQUAL(ESP_OK, OtaImageStream_Start(
        stream, &policy, &offer, &writerStreamOps, &writer));
}
void test_WriterStream_SuccessfulImageWritesVerifiesAndSelectsOnlyOnce(void) {
    OtaImageStream stream = {0};
    uint8_t file[69];
    setupWriterStream(&stream, file);
    TEST_ASSERT_EQUAL(ESP_OK, OtaImageStream_Consume(&stream, 0u, file, 69u));
    TEST_ASSERT_EQUAL(2, writeCalls);
    TEST_ASSERT_EQUAL(3, writer.received_size);
    TEST_ASSERT_EQUAL(ESP_OK, OtaImageStream_Check(&stream));
    TEST_ASSERT_EQUAL(ESP_OK, OtaImageStream_Apply(&stream));
    TEST_ASSERT_TRUE(writer.boot_selected);
    TEST_ASSERT_EQUAL(1, beginCalls);
    TEST_ASSERT_EQUAL(1, endCalls);
    TEST_ASSERT_EQUAL(1, selectCalls);
    TEST_ASSERT_EQUAL(0, abortCalls);
}
void test_WriterStream_WriteFailureAbortsExactlyOnceAndAllowsNewTransfer(void) {
    OtaImageStream stream = {0};
    uint8_t file[69];
    setupWriterStream(&stream, file);
    writeResult = ESP_FAIL;
    TEST_ASSERT_EQUAL(ESP_FAIL, OtaImageStream_Consume(&stream, 0u, file, 69u));
    TEST_ASSERT_TRUE(stream.failed);
    TEST_ASSERT_TRUE(stream.cleanup_attempted);
    TEST_ASSERT_FALSE(stream.cleanup_uncertain);
    TEST_ASSERT_EQUAL(1, writeCalls);
    TEST_ASSERT_EQUAL(1, abortCalls);
    TEST_ASSERT_FALSE(writer.active);
    TEST_ASSERT_FALSE(writer.failed);
    TEST_ASSERT_FALSE(writer.write_failed);
    TEST_ASSERT_EQUAL(ESP_OK, OtaImageStream_Cancel(&stream));
    TEST_ASSERT_EQUAL(1, abortCalls);
    writeResult = ESP_OK;
    setupWriterStream(&stream, file);
    TEST_ASSERT_EQUAL(ESP_OK, OtaImageStream_Consume(&stream, 0u, file, 69u));
    TEST_ASSERT_EQUAL(ESP_OK, OtaImageStream_Check(&stream));
    TEST_ASSERT_EQUAL(ESP_OK, OtaImageStream_Apply(&stream));
    TEST_ASSERT_EQUAL(2, beginCalls);
    TEST_ASSERT_EQUAL(1, abortCalls);
    TEST_ASSERT_EQUAL(1, selectCalls);
}
void test_WriterStream_MissingHandleAtVerifyBlocksNewTransfer(void) {
    OtaImageStream stream = {0};
    uint8_t file[69];
    setupWriterStream(&stream, file);
    TEST_ASSERT_EQUAL(ESP_OK, OtaImageStream_Consume(&stream, 0u, file, 69u));
    TEST_ASSERT_EQUAL(1, beginCalls);
    TEST_ASSERT_EQUAL(2, writeCalls);
    endResult = ESP_ERR_NOT_FOUND;
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND, OtaImageStream_Check(&stream));
    TEST_ASSERT_TRUE(writer.failed);
    TEST_ASSERT_FALSE(writer.active);
    TEST_ASSERT_TRUE(stream.failed);
    TEST_ASSERT_TRUE(stream.cleanup_attempted);
    TEST_ASSERT_TRUE(stream.cleanup_uncertain);
    endResult = ESP_OK;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, OtaImageStream_Cancel(&stream));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE,
        OtaImageStream_Consume(&stream, 69u, file, 1u));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, OtaImageWriter_Begin(&writer, 3u));
    TEST_ASSERT_EQUAL(1, beginCalls);
    TEST_ASSERT_EQUAL(1, endCalls);
    TEST_ASSERT_EQUAL(0, abortCalls);
    TEST_ASSERT_EQUAL(0, selectCalls);
}
void test_WriterStream_BeginFailureCannotBeCanceledIntoFalseRecovery(void) {
    OtaImageStream stream = {0};
    uint8_t file[69];
    setupWriterStream(&stream, file);
    beginResult = ESP_FAIL;
    TEST_ASSERT_EQUAL(ESP_FAIL, OtaImageStream_Consume(&stream, 0u, file, 69u));
    TEST_ASSERT_TRUE(stream.failed);
    TEST_ASSERT_TRUE(stream.cleanup_uncertain);
    TEST_ASSERT_FALSE(stream.begun);
    TEST_ASSERT_FALSE(stream.cleanup_attempted);
    TEST_ASSERT_TRUE(writer.failed);
    TEST_ASSERT_FALSE(writer.active);
    TEST_ASSERT_EQUAL(1, beginCalls);
    TEST_ASSERT_EQUAL(0, abortCalls);
    TEST_ASSERT_EQUAL(0, writeCalls);
    beginResult = ESP_OK;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, OtaImageStream_Cancel(&stream));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE,
        OtaImageStream_Consume(&stream, 67u, file+67, 2u));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, OtaImageWriter_Begin(&writer, 3u));
    TEST_ASSERT_EQUAL(1, beginCalls);
    TEST_ASSERT_EQUAL(0, abortCalls);
}
/* Reproduce the late/duplicated 50-byte OTA blocks reported against SDK
 * 2.0.5 (#934) with the real stream -> writer composition. This asserts
 * only local fail-closed behavior; SDK retransmission remains unproven. */
static void setupLongWriterStream(OtaImageStream *stream, uint8_t file[266]) {
    const OtaImagePolicy policy = {
        .manufacturer = 0x1234u, .image_type = 0x1234u,
        .hardware_version = 1u, .current_version = 1u,
        .slot_size = 2000u
    };
    const OtaImageOffer offer = {
        .manufacturer = 0x1234u, .image_type = 0x1234u,
        .version = 2u, .file_size = 266u
    };
    memset(file, 0, 266u);
    streamFileW32(file, 0x0BEEF11Eu);
    streamFileW16(file + 4u, 0x0100u);
    streamFileW16(file + 6u, 60u);
    streamFileW16(file + 8u, 4u);
    streamFileW16(file + 10u, offer.manufacturer);
    streamFileW16(file + 12u, offer.image_type);
    streamFileW32(file + 14u, offer.version);
    streamFileW16(file + 18u, 2u);
    streamFileW32(file + 52u, offer.file_size);
    streamFileW16(file + 56u, 1u);
    streamFileW16(file + 58u, 1u);
    streamFileW16(file + 60u, 0u);
    streamFileW32(file + 62u, 200u);
    file[66] = 0xE9u;
    for (unsigned i = 67u; i < 266u; ++i)
        file[i] = (uint8_t)(i % 251u);
    TEST_ASSERT_EQUAL(ESP_OK, OtaImageStream_Start(
        stream, &policy, &offer, &writerStreamOps, &writer));
}
void test_WriterStream_LateDuplicateAfterFlashWriteCannotSelectBoot(void) {
    OtaImageStream stream = {0};
    uint8_t file[266];
    setupLongWriterStream(&stream, file);
    TEST_ASSERT_EQUAL(ESP_OK, OtaImageStream_Consume(&stream, 0u, file, 67u));
    TEST_ASSERT_EQUAL(ESP_OK, OtaImageStream_Consume(&stream, 67u, file + 67u, 50u));
    TEST_ASSERT_EQUAL(ESP_OK, OtaImageStream_Consume(&stream, 117u, file + 117u, 50u));
    TEST_ASSERT_EQUAL(3, writeCalls);
    TEST_ASSERT_EQUAL_UINT32(167u, stream.next_offset);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,
        OtaImageStream_Consume(&stream, 117u, file + 117u, 50u));
    TEST_ASSERT_TRUE(stream.failed);
    TEST_ASSERT_TRUE(stream.cleanup_attempted);
    TEST_ASSERT_FALSE(stream.cleanup_uncertain);
    TEST_ASSERT_EQUAL(1, abortCalls);
    TEST_ASSERT_EQUAL(3, writeCalls);
    TEST_ASSERT_EQUAL(0, endCalls);
    TEST_ASSERT_EQUAL(0, selectCalls);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, OtaImageStream_Check(&stream));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, OtaImageStream_Apply(&stream));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE,
        OtaImageStream_Consume(&stream, 167u, file + 167u, 50u));
    TEST_ASSERT_EQUAL(1, abortCalls);
    TEST_ASSERT_EQUAL(ESP_OK, OtaImageStream_Cancel(&stream));
    TEST_ASSERT_EQUAL(1, abortCalls);
}
void test_WriterStream_SkippedBlockAfterFlashWriteCannotSelectBoot(void) {
    OtaImageStream stream = {0};
    uint8_t file[266];
    setupLongWriterStream(&stream, file);
    TEST_ASSERT_EQUAL(ESP_OK, OtaImageStream_Consume(&stream, 0u, file, 67u));
    TEST_ASSERT_EQUAL(ESP_OK, OtaImageStream_Consume(&stream, 67u, file + 67u, 50u));
    /* SDK advanced FileOffset past the expected 117-byte boundary. */
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,
        OtaImageStream_Consume(&stream, 167u, file + 167u, 50u));
    TEST_ASSERT_TRUE(stream.failed);
    TEST_ASSERT_EQUAL_UINT32(117u, stream.next_offset);
    TEST_ASSERT_TRUE(stream.cleanup_attempted);
    TEST_ASSERT_FALSE(stream.cleanup_uncertain);
    TEST_ASSERT_EQUAL(2, writeCalls);
    TEST_ASSERT_EQUAL(1, abortCalls);
    TEST_ASSERT_EQUAL(0, endCalls);
    TEST_ASSERT_EQUAL(0, selectCalls);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, OtaImageStream_Check(&stream));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, OtaImageStream_Apply(&stream));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE,
        OtaImageStream_Consume(&stream, 117u, file + 117u, 50u));
    TEST_ASSERT_EQUAL(1, abortCalls);
}
void test_WriterStream_AbortFailureNeverRetriesLostHandle(void) {
    OtaImageStream stream = {0};
    uint8_t file[69];
    setupWriterStream(&stream, file);
    writeResult = ESP_FAIL;
    abortResult = ESP_ERR_NOT_FOUND;
    TEST_ASSERT_EQUAL(ESP_FAIL, OtaImageStream_Consume(&stream, 0u, file, 69u));
    TEST_ASSERT_TRUE(stream.failed);
    TEST_ASSERT_TRUE(stream.cleanup_attempted);
    TEST_ASSERT_TRUE(stream.cleanup_uncertain);
    TEST_ASSERT_TRUE(writer.failed);
    abortResult = ESP_OK;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, OtaImageStream_Cancel(&stream));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, OtaImageStream_Consume(&stream, 67u, file+67, 2u));
    TEST_ASSERT_EQUAL(1, abortCalls);
    TEST_ASSERT_EQUAL(1, writeCalls);
    TEST_ASSERT_EQUAL(0, endCalls);
    TEST_ASSERT_EQUAL(0, selectCalls);
}
void test_OtaImageWriter_All(void) {
    RUN_TEST(test_Writer_ValidatesSignatureGate);
    RUN_TEST(test_Writer_BeginUsesSequentialEraseWithVerifiedAppLength);
    RUN_TEST(test_Writer_RejectsOversizedWriteBeforeFlashMutation);
    RUN_TEST(test_Writer_AcceptsMaximumZigbeeBlockSequentially);
    RUN_TEST(test_Writer_StartRequiresInactiveSlotAndCapacity);
    RUN_TEST(test_Writer_RejectsRunningPartitionAsTarget);
    RUN_TEST(test_Writer_BeginFailureDoesNotAdmitWrite);
    RUN_TEST(test_Writer_SuccessRequiresCompleteVerifiedWriteBeforeBoot);
    RUN_TEST(test_Writer_IncompleteWriteRequiresOwnerAbort);
    RUN_TEST(test_Writer_OverrunLeavesAbortToOwnerWithoutWritingExtraBytes);
    RUN_TEST(test_Writer_WriteFailureBlocksWritesUntilOwnerAbort);
    RUN_TEST(test_Writer_CryptographicEndFailureCannotSelectBoot);
    RUN_TEST(test_Writer_MissingHandleOnVerifyCannotBeResetOrRetried);
    RUN_TEST(test_Writer_FailedBootSelectionWithChangedReadbackLocksOwner);
    RUN_TEST(test_Writer_FailedBootSelectionEvenIfOldReadbackLocksOwner);
    RUN_TEST(test_Writer_SuccessfulSdkReturnWithoutMatchingReadbackFailsClosed);
    RUN_TEST(test_Writer_BeginCannotErasePreviouslySelectedBootSlot);
    RUN_TEST(test_Writer_MissingBootReadbackAtSelectLocksOwner);
    RUN_TEST(test_Writer_BootChoiceChangedBeforeSelectLocksOwner);
    RUN_TEST(test_Writer_AbortMissingHandleMustNotBeRetried);
    RUN_TEST(test_Writer_WriteErrorWithFailedAbortCannotBeRetried);
    RUN_TEST(test_Writer_NullInputOrEmptyWriteCannotMutate);
    RUN_TEST(test_WriterStream_SuccessfulImageWritesVerifiesAndSelectsOnlyOnce);
    RUN_TEST(test_WriterStream_WriteFailureAbortsExactlyOnceAndAllowsNewTransfer);
    RUN_TEST(test_WriterStream_AbortFailureNeverRetriesLostHandle);
    RUN_TEST(test_WriterStream_BeginFailureCannotBeCanceledIntoFalseRecovery);
    RUN_TEST(test_WriterStream_MissingHandleAtVerifyBlocksNewTransfer);
    RUN_TEST(test_WriterStream_LateDuplicateAfterFlashWriteCannotSelectBoot);
    RUN_TEST(test_WriterStream_SkippedBlockAfterFlashWriteCannotSelectBoot);
}
#ifndef RACOOL_TARGET_TESTING
int main(void) {
    UNITY_BEGIN();
    test_OtaImageWriter_All();
    return UNITY_END();
}
#endif
