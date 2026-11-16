#include "unity.h"
#include "Communication/Zigbee/Endpoints/OtaImageStream.c"
#include "Communication/Zigbee/Endpoints/OtaImagePolicy.c"
#include <string.h>

static OtaImageStream stream;
static OtaImagePolicy policy;
static OtaImageOffer offer;
static uint8_t file[72];
static unsigned beginCalls, writeCalls, verifyCalls, selectCalls, abortCalls;
static uint32_t beginSize, bytesWritten;
static esp_err_t beginResult, writeResult, verifyResult, selectResult, abortResult;

static void W16(uint8_t *p, uint16_t v) { p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8); }
static void W32(uint8_t *p, uint32_t v) {
    W16(p,(uint16_t)v);W16(p+2,(uint16_t)(v>>16));
}
static esp_err_t begin(void *ctx, uint32_t size) {
    TEST_ASSERT_EQUAL_PTR(&stream, ctx);
    beginCalls++; beginSize=size; return beginResult;
}
static esp_err_t write(void *ctx, const uint8_t *p, uint32_t len) {
    TEST_ASSERT_EQUAL_PTR(&stream, ctx);
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_TRUE(len>0);
    if(bytesWritten==0) TEST_ASSERT_EQUAL_HEX8(0xE9,p[0]);
    writeCalls++;if(writeResult==ESP_OK)bytesWritten+=len;
    return writeResult;
}
static esp_err_t verify(void *ctx) {TEST_ASSERT_EQUAL_PTR(&stream,ctx);verifyCalls++;return verifyResult;}
static esp_err_t selectBoot(void *ctx) {TEST_ASSERT_EQUAL_PTR(&stream,ctx);selectCalls++;return selectResult;}
static esp_err_t abortFlash(void *ctx) {TEST_ASSERT_EQUAL_PTR(&stream,ctx);abortCalls++;return abortResult;}
static OtaImageStreamOps ops={
    .begin=begin,.write=write,.verify=verify,
    .select_boot=selectBoot,.abort=abortFlash
};

void setUp(void) {
    memset(&stream,0,sizeof(stream));
    memset(file,0,sizeof(file));
    policy=(OtaImagePolicy){.manufacturer=0x1234u,.image_type=0x1234u,
        .hardware_version=1u,.current_version=1u,.slot_size=0x300000u};
    offer=(OtaImageOffer){.manufacturer=0x1234u,.image_type=0x1234u,
        .version=2u,.file_size=69u};
    W32(file,0x0BEEF11Eu);W16(file+4,0x0100u);
    W16(file+6,60u);W16(file+8,4u);
    W16(file+10,offer.manufacturer);W16(file+12,offer.image_type);
    W32(file+14,offer.version);W16(file+18,2u);W32(file+52,offer.file_size);
    W16(file+56,1u);W16(file+58,1u);W16(file+60,0u);
    W32(file+62,3u);file[66]=0xE9u;file[67]=1;file[68]=2;
    beginCalls=writeCalls=verifyCalls=selectCalls=abortCalls=0;
    beginSize=bytesWritten=0;
    beginResult=writeResult=verifyResult=selectResult=abortResult=ESP_OK;
}
void tearDown(void) {}
static esp_err_t start(void) {
    return OtaImageStream_Start(&stream,&policy,&offer,&ops,&stream);
}
void test_Stream_SplitHeaderAndPayloadAreWrittenExactlyOnce(void) {
    TEST_ASSERT_EQUAL(ESP_OK,start());
    for(unsigned pos=0;pos<69u;){
        unsigned len=(pos%3u)+1u;
        if(len>69u-pos)len=69u-pos;
        TEST_ASSERT_EQUAL(ESP_OK,OtaImageStream_Consume(&stream,pos,file+pos,len));
        pos+=len;
    }
    TEST_ASSERT_EQUAL(1,beginCalls);TEST_ASSERT_EQUAL(3,beginSize);
    TEST_ASSERT_EQUAL(3,bytesWritten);TEST_ASSERT_EQUAL(69,stream.next_offset);
    TEST_ASSERT_EQUAL(ESP_OK,OtaImageStream_Check(&stream));
    TEST_ASSERT_EQUAL(ESP_OK,OtaImageStream_Apply(&stream));
    TEST_ASSERT_EQUAL(1,verifyCalls);TEST_ASSERT_EQUAL(1,selectCalls);
    TEST_ASSERT_EQUAL(0,abortCalls);
}
void test_Stream_BlocksBeforeHeaderDoNotWriteFlash(void) {
    TEST_ASSERT_EQUAL(ESP_OK,start());
    TEST_ASSERT_EQUAL(ESP_OK,OtaImageStream_Consume(&stream,0,file,48u));
    TEST_ASSERT_EQUAL(0,beginCalls);TEST_ASSERT_EQUAL(0,writeCalls);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE,OtaImageStream_Check(&stream));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE,OtaImageStream_Apply(&stream));
    TEST_ASSERT_EQUAL(ESP_OK,OtaImageStream_Cancel(&stream));
}
void test_Stream_DuplicateOrSkippedBlockFailsClosed(void) {
    TEST_ASSERT_EQUAL(ESP_OK,start());
    TEST_ASSERT_EQUAL(ESP_OK,OtaImageStream_Consume(&stream,0,file,48u));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,OtaImageStream_Consume(&stream,0,file,48u));
    TEST_ASSERT_TRUE(stream.failed);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE,OtaImageStream_Consume(&stream,48,file+48,21u));
    TEST_ASSERT_EQUAL(0,beginCalls);
}
void test_Stream_WrongHeaderNeverStartsFlash(void) {
    file[0]^=1u;
    TEST_ASSERT_EQUAL(ESP_OK,start());
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,OtaImageStream_Consume(&stream,0,file,69u));
    TEST_ASSERT_EQUAL(0,beginCalls);
    TEST_ASSERT_EQUAL(0,writeCalls);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE,OtaImageStream_Apply(&stream));
}
void test_Stream_StartupRejectsOldOrForeignOffer(void) {
    offer.version=1u;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,start());
    offer.version=2u;offer.manufacturer=0x4444u;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,start());
    TEST_ASSERT_EQUAL(0,beginCalls);
}
void test_Stream_IncompleteDownloadAbortsBackend(void) {
    TEST_ASSERT_EQUAL(ESP_OK,start());
    TEST_ASSERT_EQUAL(ESP_OK,OtaImageStream_Consume(&stream,0,file,67u));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE,OtaImageStream_Check(&stream));
    TEST_ASSERT_EQUAL(1,abortCalls);TEST_ASSERT_EQUAL(0,verifyCalls);
}
void test_Stream_FlashBeginFailureStopsTransfer(void) {
    beginResult=ESP_FAIL;
    TEST_ASSERT_EQUAL(ESP_OK,start());
    TEST_ASSERT_EQUAL(ESP_FAIL,OtaImageStream_Consume(&stream,0,file,67u));
    TEST_ASSERT_EQUAL(1,beginCalls);TEST_ASSERT_EQUAL(0,writeCalls);
    TEST_ASSERT_EQUAL(0,abortCalls);
}
void test_Stream_FlashWriteFailureAbortsBackend(void) {
    writeResult=ESP_FAIL;
    TEST_ASSERT_EQUAL(ESP_OK,start());
    TEST_ASSERT_EQUAL(ESP_FAIL,OtaImageStream_Consume(&stream,0,file,67u));
    TEST_ASSERT_EQUAL(1,abortCalls);TEST_ASSERT_EQUAL(0,bytesWritten);
}
void test_Stream_VerificationFailureCannotSelectBoot(void) {
    verifyResult=ESP_FAIL;
    TEST_ASSERT_EQUAL(ESP_OK,start());
    TEST_ASSERT_EQUAL(ESP_OK,OtaImageStream_Consume(&stream,0,file,69u));
    TEST_ASSERT_EQUAL(ESP_FAIL,OtaImageStream_Check(&stream));
    TEST_ASSERT_EQUAL(0,selectCalls);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE,OtaImageStream_Apply(&stream));
}
void test_Stream_BootSelectionFailureNeverSignalsSuccess(void) {
    selectResult=ESP_FAIL;
    TEST_ASSERT_EQUAL(ESP_OK,start());
    TEST_ASSERT_EQUAL(ESP_OK,OtaImageStream_Consume(&stream,0,file,69u));
    TEST_ASSERT_EQUAL(ESP_OK,OtaImageStream_Check(&stream));
    TEST_ASSERT_EQUAL(ESP_FAIL,OtaImageStream_Apply(&stream));
    TEST_ASSERT_EQUAL(1,selectCalls);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE,OtaImageStream_Apply(&stream));
}
void test_Stream_CancelAfterBackendAbortFailureBlocksReuse(void) {
    TEST_ASSERT_EQUAL(ESP_OK,start());
    TEST_ASSERT_EQUAL(ESP_OK,OtaImageStream_Consume(&stream,0,file,67u));
    abortResult=ESP_ERR_NOT_FOUND;
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND,OtaImageStream_Cancel(&stream));
    TEST_ASSERT_TRUE(stream.failed);
    TEST_ASSERT_TRUE(stream.cleanup_attempted);
    TEST_ASSERT_TRUE(stream.cleanup_uncertain);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE,OtaImageStream_Consume(&stream,67,file+67,2u));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE,start());
    abortResult=ESP_OK;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE,OtaImageStream_Cancel(&stream));
    TEST_ASSERT_EQUAL(1,abortCalls);
}
void test_Stream_TransferErrorCleansUpExactlyOnce(void) {
    TEST_ASSERT_EQUAL(ESP_OK,start());
    TEST_ASSERT_EQUAL(ESP_OK,OtaImageStream_Consume(&stream,0,file,67u));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,OtaImageStream_Consume(&stream,0,file,2u));
    TEST_ASSERT_TRUE(stream.failed);
    TEST_ASSERT_TRUE(stream.cleanup_attempted);
    TEST_ASSERT_FALSE(stream.cleanup_uncertain);
    TEST_ASSERT_EQUAL(1,abortCalls);
    TEST_ASSERT_EQUAL(ESP_OK,OtaImageStream_Cancel(&stream));
    TEST_ASSERT_EQUAL(1,abortCalls);
    TEST_ASSERT_EQUAL(ESP_OK,start());
}
void test_Stream_TransferErrorWithAbortFailureIsTerminal(void) {
    TEST_ASSERT_EQUAL(ESP_OK,start());
    TEST_ASSERT_EQUAL(ESP_OK,OtaImageStream_Consume(&stream,0,file,67u));
    abortResult=ESP_ERR_NOT_FOUND;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,OtaImageStream_Consume(&stream,0,file,2u));
    TEST_ASSERT_TRUE(stream.failed);
    TEST_ASSERT_TRUE(stream.cleanup_uncertain);
    abortResult=ESP_OK;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE,OtaImageStream_Cancel(&stream));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE,start());
    TEST_ASSERT_EQUAL(1,abortCalls);
}
void test_Stream_ExplicitCancelAfterBegunCleansUpOnce(void) {
    TEST_ASSERT_EQUAL(ESP_OK,start());
    TEST_ASSERT_EQUAL(ESP_OK,OtaImageStream_Consume(&stream,0,file,67u));
    TEST_ASSERT_EQUAL(ESP_OK,OtaImageStream_Cancel(&stream));
    TEST_ASSERT_EQUAL(1,abortCalls);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE,OtaImageStream_Cancel(&stream));
    TEST_ASSERT_EQUAL(1,abortCalls);
}
void test_Stream_NoCancelAfterBootSlotSelected(void) {
    TEST_ASSERT_EQUAL(ESP_OK,start());
    TEST_ASSERT_EQUAL(ESP_OK,OtaImageStream_Consume(&stream,0,file,69u));
    TEST_ASSERT_EQUAL(ESP_OK,OtaImageStream_Check(&stream));
    TEST_ASSERT_EQUAL(ESP_OK,OtaImageStream_Apply(&stream));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE,OtaImageStream_Cancel(&stream));
}
void test_Stream_RejectNullBadInputs(void) {
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,OtaImageStream_Start(NULL,&policy,&offer,&ops,&stream));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE,OtaImageStream_Consume(&stream,0,file,3u));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,OtaImageStream_Cancel(NULL));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,OtaImageStream_Check(NULL));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,OtaImageStream_Apply(NULL));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,OtaImageStream_Consume(NULL,0,file,3u));
    TEST_ASSERT_EQUAL(ESP_OK,start());
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,OtaImageStream_Consume(&stream,0,NULL,3u));
}
void test_OtaImageStream_All(void) {
    RUN_TEST(test_Stream_SplitHeaderAndPayloadAreWrittenExactlyOnce);
    RUN_TEST(test_Stream_BlocksBeforeHeaderDoNotWriteFlash);
    RUN_TEST(test_Stream_DuplicateOrSkippedBlockFailsClosed);
    RUN_TEST(test_Stream_WrongHeaderNeverStartsFlash);
    RUN_TEST(test_Stream_StartupRejectsOldOrForeignOffer);
    RUN_TEST(test_Stream_IncompleteDownloadAbortsBackend);
    RUN_TEST(test_Stream_FlashBeginFailureStopsTransfer);
    RUN_TEST(test_Stream_FlashWriteFailureAbortsBackend);
    RUN_TEST(test_Stream_VerificationFailureCannotSelectBoot);
    RUN_TEST(test_Stream_BootSelectionFailureNeverSignalsSuccess);
    RUN_TEST(test_Stream_CancelAfterBackendAbortFailureBlocksReuse);
    RUN_TEST(test_Stream_TransferErrorCleansUpExactlyOnce);
    RUN_TEST(test_Stream_TransferErrorWithAbortFailureIsTerminal);
    RUN_TEST(test_Stream_ExplicitCancelAfterBegunCleansUpOnce);
    RUN_TEST(test_Stream_NoCancelAfterBootSlotSelected);
    RUN_TEST(test_Stream_RejectNullBadInputs);
}
#ifndef RACOOL_TARGET_TESTING
int main(void) {UNITY_BEGIN();test_OtaImageStream_All();return UNITY_END();}
#endif
