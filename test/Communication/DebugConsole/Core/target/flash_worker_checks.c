#include "unity.h"
#include "esp_littlefs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <stdio.h>
#include <unistd.h>
#include <stdatomic.h>
static SemaphoreHandle_t flashEntered,flashRelease;
static atomic_bool flashBlock;
static bool flashActive;
static FILE *TargetFlash_Open(const char *path,const char *mode) {(void)path;(void)mode;return (FILE *)1;}
static int TargetFlash_Seek(FILE *file,long position,int origin) {(void)file;(void)position;(void)origin;return 0;}
static long TargetFlash_Tell(FILE *file) {(void)file;return 0;}
static int TargetFlash_Print(FILE *file,const char *format,...) {
    (void)file;(void)format;
    if(atomic_exchange(&flashBlock,false)) {xSemaphoreGive(flashEntered);xSemaphoreTake(flashRelease,portMAX_DELAY);}
    return 30;
}
static int TargetFlash_Close(FILE *file) {(void)file;return 0;}
static int TargetFlash_Unlink(const char *path) {(void)path;return 0;}
static bool TargetFlash_Mounted(const char *label) {(void)label;return true;}
#define FlashLogger_GetModule TargetFlash_GetModule
#define FlashLogger_Deinit TargetFlash_Deinit
#define FlashLogger_Flush TargetFlash_Flush
#define FlashLogger_CopyTail TargetFlash_CopyTail
#define FlashLogger_Clear TargetFlash_Clear
#define FlashLogger_GetStats TargetFlash_GetStats
#define FlashLogger_ResetStats TargetFlash_ResetStats
#define FlashLogger_AddFilter TargetFlash_AddFilter
#define FlashLogger_ClearFilters TargetFlash_ClearFilters
#define FlashLogger_IsTagFiltered TargetFlash_IsTagFiltered
#define esp_littlefs_mounted TargetFlash_Mounted
#define fopen TargetFlash_Open
#define fseek TargetFlash_Seek
#define ftell TargetFlash_Tell
#define fprintf TargetFlash_Print
#define fclose TargetFlash_Close
#define unlink TargetFlash_Unlink
#include "../../../../../src/Communication/DebugConsole/Core/FlashLogger.c"
#undef fopen
#undef fseek
#undef ftell
#undef fprintf
#undef fclose
#undef unlink
#undef esp_littlefs_mounted
#undef FlashLogger_GetModule
#undef FlashLogger_Deinit
#undef FlashLogger_Flush
#undef FlashLogger_CopyTail
#undef FlashLogger_Clear
#undef FlashLogger_GetStats
#undef FlashLogger_ResetStats
#undef FlashLogger_AddFilter
#undef FlashLogger_ClearFilters
#undef FlashLogger_IsTagFiltered
void target_flash_worker_cleanup(void) {
    if(flashRelease) xSemaphoreGive(flashRelease);
    if(flashActive) {TEST_ASSERT_EQUAL(ESP_OK,TargetFlash_Deinit());flashActive=false;}
    if(flashEntered) {vSemaphoreDelete(flashEntered);flashEntered=NULL;}
    if(flashRelease) {vSemaphoreDelete(flashRelease);flashRelease=NULL;}
}
void target_flash_worker_verify_shutdown(void) {
    flashActive=true;flashEntered=xSemaphoreCreateBinary();flashRelease=xSemaphoreCreateBinary();
    TEST_ASSERT_NOT_NULL(flashEntered);TEST_ASSERT_NOT_NULL(flashRelease);
    ILogger *logger=TargetFlash_GetModule();TEST_ASSERT_NOT_NULL(logger);
    atomic_store(&flashBlock,true);logger->Log(logger,ESP_LOG_ERROR,"TargetFlash","owned file write");
    TEST_ASSERT_EQUAL(pdTRUE,xSemaphoreTake(flashEntered,pdMS_TO_TICKS(2000)));
    TEST_ASSERT_EQUAL(0,uxQueueMessagesWaiting(s_logQueue));
    TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT,TargetFlash_Flush());
    TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT,TargetFlash_Deinit());
    TEST_ASSERT_NOT_NULL(atomic_load(&s_worker.task));TEST_ASSERT_NOT_NULL(s_logQueue);
    TEST_ASSERT_NULL(TargetFlash_GetModule());
    unsigned accepted=atomic_load(&s_accepted);logger->Log(logger,ESP_LOG_ERROR,"closing","must reject");
    TEST_ASSERT_EQUAL(accepted,atomic_load(&s_accepted));
    xSemaphoreGive(flashRelease);TEST_ASSERT_EQUAL(ESP_OK,TargetFlash_Deinit());
    TEST_ASSERT_NULL(atomic_load(&s_worker.task));TEST_ASSERT_EQUAL(1,atomic_load(&s_completed));
    logger=TargetFlash_GetModule();TEST_ASSERT_NOT_NULL(logger);
    logger->Log(logger,ESP_LOG_ERROR,"restart","new owned write");
    TEST_ASSERT_EQUAL(ESP_OK,TargetFlash_Flush());
    FlashLoggerStats stats;TEST_ASSERT_EQUAL(ESP_OK,TargetFlash_GetStats(&stats));
    TEST_ASSERT_EQUAL(2,stats.totalLogsWritten);TEST_ASSERT_EQUAL(0,stats.failedWrites);
    TEST_ASSERT_EQUAL(ESP_OK,TargetFlash_Clear());
    TEST_ASSERT_EQUAL(ESP_OK,TargetFlash_Deinit());flashActive=false;
    target_flash_worker_cleanup();
}
