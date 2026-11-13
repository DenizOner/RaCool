#include "unity.h"
#include "../../../Infrastructure/support/test_mocks.h"
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
static int file_open_calls,file_close_calls,file_write_calls,file_unlink_calls;
static int file_close_result,file_unlink_result,file_write_result;
static long file_size;
static const char *read_rows[8];
static unsigned read_index;
static int read_error;
static bool allocation_fails;
static unsigned allocated_scratch,freed_scratch;
static char opened_path[64];
static char *test_getline(char *output,int capacity,FILE *file) {
    (void)file;if(!read_rows[read_index])return NULL;
    snprintf(output,(size_t)capacity,"%s",read_rows[read_index++]);return output;
}
static int test_file_error(FILE *file) {(void)file;return read_error;}
static void *test_allocate(size_t size) {++allocated_scratch;return allocation_fails ? NULL : malloc(size);}
static void test_free(void *memory) {if(memory)++freed_scratch;free(memory);}

static FILE *test_open(const char *path,const char *mode) {(void)mode;snprintf(opened_path,sizeof(opened_path),"%s",path);++file_open_calls;return (FILE *)1;}
static int test_seek(FILE *file,long offset,int origin) {(void)file;(void)offset;(void)origin;return 0;}
static long test_tell(FILE *file) {(void)file;return file_size;}
static int test_print(FILE *file,const char *format,...) {(void)file;(void)format;++file_write_calls;return file_write_result;}
static int test_close(FILE *file) {(void)file;++file_close_calls;return file_close_result;}
static int test_unlink(const char *path) {(void)path;++file_unlink_calls;errno=EIO;return file_unlink_result;}
#define fgets test_getline
#define ferror test_file_error
#define malloc test_allocate
#define free test_free
#define fopen test_open
#define fseek test_seek
#define ftell test_tell
#define fprintf test_print
#define fclose test_close
#define unlink test_unlink
#include "../../../../src/Communication/DebugConsole/Core/FlashLogger.c"
#undef fgets
#undef ferror
#undef malloc
#undef free
#undef fopen
#undef fseek
#undef ftell
#undef fprintf
#undef fclose
#undef unlink

static TickType_t ticks;
static bool complete_during_wait;
static TickType_t tick_now(void) {return ticks;}
static void advance_ticks(TickType_t elapsed) {ticks+=elapsed;if(complete_during_wait)atomic_store(&s_completed,atomic_load(&s_accepted));}
static BaseType_t create_worker(TaskFunction_t fn,const char *name,uint32_t stack,void *arg,uint32_t priority,TaskHandle_t *task) {
    (void)fn;(void)name;(void)stack;(void)arg;(void)priority;*task=(TaskHandle_t)3;return pdPASS;
}
static BaseType_t acknowledge_stop(QueueHandle_t queue,void *value,TickType_t timeout) {
    if(queue==s_worker.stopped && timeout) {*(int32_t *)value=0;return pdTRUE;}return pdFALSE;
}
void setUp(void) {
    s_controlMutex=s_fileMutex=s_statsMutex=s_filterMutex=NULL;s_logQueue=NULL;
    memset(&s_worker,0,sizeof(s_worker));memset(&s_stats,0,sizeof(s_stats));
    atomic_store(&s_initialized,false);atomic_store(&s_closing,false);
    atomic_store(&s_accepted,0);atomic_store(&s_completed,0);atomic_store(&s_writeFailures,0);
    s_acknowledgedFailures=0;s_filterTagCount=0;s_currentFileIndex=0;
    s_capture=(FlashCaptureStatus){0};s_captureSource=NULL;
    RESET_FAKE(mocked_esp_timer_get_time);
    RESET_FAKE(mocked_xSemaphoreCreateMutex);RESET_FAKE(mocked_xSemaphoreTake);RESET_FAKE(mocked_xSemaphoreGive);
    RESET_FAKE(mocked_xQueueCreate);RESET_FAKE(mocked_xQueueSend);RESET_FAKE(mocked_xQueueReceive);
    RESET_FAKE(mocked_xTaskCreate);RESET_FAKE(mocked_vTaskDelete);RESET_FAKE(mocked_xTaskGetCurrentTaskHandle);
    RESET_FAKE(mocked_xTaskGetTickCount);RESET_FAKE(mocked_vTaskDelay);
    RESET_FAKE(mocked_esp_littlefs_mounted);RESET_FAKE(mocked_esp_vfs_littlefs_register);
    mocked_xSemaphoreCreateMutex_fake.return_val=(SemaphoreHandle_t)1;
    mocked_xSemaphoreTake_fake.return_val=pdTRUE;mocked_xQueueCreate_fake.return_val=(QueueHandle_t)2;
    mocked_xQueueSend_fake.return_val=pdTRUE;mocked_xTaskCreate_fake.custom_fake=create_worker;
    mocked_esp_littlefs_mounted_fake.return_val=true;
    ticks=0;complete_during_wait=false;mocked_xTaskGetTickCount_fake.custom_fake=tick_now;
    mocked_vTaskDelay_fake.custom_fake=advance_ticks;
    file_open_calls=file_close_calls=file_write_calls=file_unlink_calls=0;
    file_close_result=file_unlink_result=0;file_write_result=20;file_size=0;
    memset(read_rows,0,sizeof(read_rows));read_index=0;read_error=0;allocation_fails=false;allocated_scratch=freed_scratch=0;opened_path[0]=0;
}
void tearDown(void) {}
static uint32_t capture_sequence;
static int32_t fixture_capture_control(ILogManager *self) {(void)self;return 0;}
static int32_t fixture_release_result;
static int32_t fixture_capture_release(ILogManager *self) {(void)self;return fixture_release_result;}
static uint32_t fixture_recent_logs(SystemLogEntry *out,uint32_t capacity) {
    if (!capture_sequence) return 0;
    TEST_ASSERT_TRUE(capacity>=1);
    *out=(SystemLogEntry){.id=capture_sequence,.timestamp=1};
    snprintf(out->message,sizeof(out->message),"password=SYNTHETIC_MARKER");
    return 1;
}
static BaseType_t inspect_redacted_capture(QueueHandle_t queue,const void *item,TickType_t timeout) {
    (void)queue;(void)timeout;
    const LogMessage_t *message=item;
    TEST_ASSERT_EQUAL_STRING("[REDACTED]",message->message);
    return pdTRUE;
}
void test_FlashCaptureBoundsRedactionDuplicateAndExpiry(void) {
    TEST_ASSERT_NOT_NULL(FlashLogger_GetModule());
    ILogManager source={.GetRecentLogs=fixture_recent_logs,.BeginCapture=fixture_capture_control,.EndCapture=fixture_capture_control};
    capture_sequence=0;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,FlashLogger_StartCapture(&source,0,2));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,FlashLogger_StartCapture(&source,1,257));
    TEST_ASSERT_EQUAL(ESP_OK,FlashLogger_StartCapture(&source,1,2));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE,FlashLogger_StartCapture(&source,1,2));
    mocked_xQueueSend_fake.custom_fake=inspect_redacted_capture;
    capture_sequence=1;capture_recent_logs();capture_recent_logs();
    TEST_ASSERT_EQUAL(1,s_capture.admitted);TEST_ASSERT_EQUAL(1,s_capture.remaining);
    mocked_esp_timer_get_time_fake.return_val=1000000;
    capture_sequence=2;capture_recent_logs();
    TEST_ASSERT_FALSE(s_capture.active);TEST_ASSERT_EQUAL(1,s_capture.admitted);
    TEST_ASSERT_NULL(s_captureSource);
}
void test_FlashCaptureQueueFailureConsumesBoundAndStopPreservesCounters(void) {
    TEST_ASSERT_NOT_NULL(FlashLogger_GetModule());
    ILogManager source={.GetRecentLogs=fixture_recent_logs,.BeginCapture=fixture_capture_control,.EndCapture=fixture_capture_control};capture_sequence=0;
    TEST_ASSERT_EQUAL(ESP_OK,FlashLogger_StartCapture(&source,1,1));
    mocked_xQueueSend_fake.return_val=pdFALSE;capture_sequence=1;capture_recent_logs();
    TEST_ASSERT_EQUAL(1,s_capture.rejected);TEST_ASSERT_EQUAL(0,s_capture.admitted);
    TEST_ASSERT_FALSE(s_capture.active);
    TEST_ASSERT_EQUAL(ESP_OK,FlashLogger_StopCapture());
    FlashCaptureStatus status={.remaining=99};
    mocked_xSemaphoreTake_fake.return_val=pdFALSE;
    TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT,FlashLogger_GetCaptureStatus(&status));
    TEST_ASSERT_EQUAL(99,status.remaining);
}
void test_FlashCaptureFailedReleaseRetainsSourceUntilRetry(void) {
    TEST_ASSERT_NOT_NULL(FlashLogger_GetModule());
    ILogManager source={.GetRecentLogs=fixture_recent_logs,.BeginCapture=fixture_capture_control,.EndCapture=fixture_capture_release};
    capture_sequence=0;fixture_release_result=0;
    TEST_ASSERT_EQUAL(ESP_OK,FlashLogger_StartCapture(&source,1,1));
    fixture_release_result=-1;
    TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT,FlashLogger_StopCapture());
    TEST_ASSERT_FALSE(s_capture.active);TEST_ASSERT_EQUAL_PTR(&source,s_captureSource);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE,FlashLogger_StartCapture(&source,1,1));
    fixture_release_result=0;
    TEST_ASSERT_EQUAL(ESP_OK,FlashLogger_StopCapture());TEST_ASSERT_NULL(s_captureSource);
}
void test_FlashLogger_UninitializedGettersPreserveOutput(void) {
    FlashLoggerStats stats={.totalLogsWritten=123};
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE,FlashLogger_GetStats(&stats));
    TEST_ASSERT_EQUAL(123,stats.totalLogsWritten);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE,FlashLogger_Flush());
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE,FlashLogger_Clear());
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,FlashLogger_AddFilter(""));
}
void test_FlashLogger_MutexAndWorkerFailureCannotPublishInitialized(void) {
    mocked_xSemaphoreCreateMutex_fake.return_val=NULL;
    TEST_ASSERT_NULL(FlashLogger_GetModule());TEST_ASSERT_FALSE(atomic_load(&s_initialized));
    mocked_xSemaphoreCreateMutex_fake.return_val=(SemaphoreHandle_t)1;
    mocked_xTaskCreate_fake.custom_fake=NULL;mocked_xTaskCreate_fake.return_val=pdFAIL;
    TEST_ASSERT_NULL(FlashLogger_GetModule());TEST_ASSERT_FALSE(atomic_load(&s_initialized));
    QueueHandle_t retained=s_logQueue;TEST_ASSERT_NOT_NULL(retained);
    mocked_xTaskCreate_fake.custom_fake=create_worker;
    TEST_ASSERT_NOT_NULL(FlashLogger_GetModule());TEST_ASSERT_EQUAL_PTR(retained,s_logQueue);
}
static esp_err_t failed_mount(const esp_vfs_littlefs_conf_t *config) {
    TEST_ASSERT_FALSE(config->format_if_mount_failed);return ESP_FAIL;
}
void test_FlashLogger_MountFailureDoesNotFormatOrReportSuccess(void) {
    mocked_esp_littlefs_mounted_fake.return_val=false;
    mocked_esp_vfs_littlefs_register_fake.custom_fake=failed_mount;
    TEST_ASSERT_NULL(FlashLogger_GetModule());TEST_ASSERT_FALSE(atomic_load(&s_initialized));
    TEST_ASSERT_EQUAL(0,mocked_xTaskCreate_fake.call_count);
}
void test_FlashLogger_StopTimeoutRetainsWorkerQueueAndRejectsRestart(void) {
    ILogger *logger=FlashLogger_GetModule();TEST_ASSERT_NOT_NULL(logger);
    TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT,FlashLogger_Deinit());
    TEST_ASSERT_TRUE(atomic_load(&s_closing));TEST_ASSERT_NOT_NULL(atomic_load(&s_worker.task));
    TEST_ASSERT_EQUAL(0,mocked_vTaskDelete_fake.call_count);TEST_ASSERT_NULL(FlashLogger_GetModule());
    unsigned before=mocked_xQueueSend_fake.call_count;logger->Log(logger,ESP_LOG_ERROR,"tag","message");
    TEST_ASSERT_EQUAL(before,mocked_xQueueSend_fake.call_count);
    mocked_xQueueReceive_fake.custom_fake=acknowledge_stop;
    TEST_ASSERT_EQUAL(ESP_OK,FlashLogger_Deinit());TEST_ASSERT_EQUAL(1,mocked_vTaskDelete_fake.call_count);
    TEST_ASSERT_NOT_NULL(s_logQueue);TEST_ASSERT_NOT_NULL(FlashLogger_GetModule());
}
void test_FlashLogger_RejectsInvalidSeverityAndReportsQueueDrop(void) {
    ILogger *logger=FlashLogger_GetModule();TEST_ASSERT_NOT_NULL(logger);
    logger->Log(logger,(esp_log_level_t)-1,"tag","message");logger->Log(logger,ESP_LOG_ERROR,"tag",NULL);
    TEST_ASSERT_EQUAL(0,mocked_xQueueSend_fake.call_count);
    mocked_xQueueSend_fake.return_val=pdFALSE;logger->Log(logger,ESP_LOG_ERROR,"tag","message");
    TEST_ASSERT_EQUAL(1,s_stats.droppedLogs);TEST_ASSERT_EQUAL(0,atomic_load(&s_accepted));
}
void test_FlashLogger_FlushRequiresCompletedWritesNotEmptyQueue(void) {
    TEST_ASSERT_NOT_NULL(FlashLogger_GetModule());atomic_store(&s_accepted,1);
    TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT,FlashLogger_Flush());TEST_ASSERT_EQUAL(0,atomic_load(&s_completed));
    complete_during_wait=true;TEST_ASSERT_EQUAL(ESP_OK,FlashLogger_Flush());
    atomic_store(&s_writeFailures,1);TEST_ASSERT_EQUAL(ESP_FAIL,FlashLogger_Flush());
    TEST_ASSERT_EQUAL(ESP_OK,FlashLogger_Flush());
}
void test_FlashLogger_FileWriteAndCloseFailuresAreNotSuccessfulWrites(void) {
    TEST_ASSERT_NOT_NULL(FlashLogger_GetModule());
    LogMessage_t msg={.level=ESP_LOG_ERROR};strcpy(msg.tag,"tag");strcpy(msg.message,"message");
    file_write_result=-1;TEST_ASSERT_FALSE(write_message(&msg));TEST_ASSERT_EQUAL(0,s_stats.totalLogsWritten);
    file_write_result=20;file_close_result=-1;TEST_ASSERT_FALSE(write_message(&msg));TEST_ASSERT_EQUAL(0,s_stats.totalLogsWritten);
    file_close_result=0;TEST_ASSERT_TRUE(write_message(&msg));TEST_ASSERT_EQUAL(1,s_stats.totalLogsWritten);
}
void test_FlashLogger_ClearReportsPartialFilesystemFailure(void) {
    TEST_ASSERT_NOT_NULL(FlashLogger_GetModule());file_unlink_result=-1;
    TEST_ASSERT_EQUAL(ESP_FAIL,FlashLogger_Clear());TEST_ASSERT_EQUAL(1,file_unlink_calls);
    file_unlink_result=0;TEST_ASSERT_EQUAL(ESP_OK,FlashLogger_Clear());
    TEST_ASSERT_EQUAL(1+DEBUG_CONSOLE_FLASH_LOG_MAX_FILES,file_unlink_calls);
}
void test_FlashLogger_StatsLockFailurePreservesOutput(void) {
    TEST_ASSERT_NOT_NULL(FlashLogger_GetModule());FlashLoggerStats stats={.totalLogsWritten=77};
    mocked_xSemaphoreTake_fake.return_val=pdFALSE;
    TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT,FlashLogger_GetStats(&stats));TEST_ASSERT_EQUAL(77,stats.totalLogsWritten);
}
void test_FlashLogger_FilterValidationCapacityAndDuplicateHandling(void) {
    TEST_ASSERT_NOT_NULL(FlashLogger_GetModule());
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,FlashLogger_AddFilter("abcdefghijklmnop"));
    TEST_ASSERT_EQUAL(ESP_OK,FlashLogger_AddFilter("tag"));TEST_ASSERT_EQUAL(ESP_OK,FlashLogger_AddFilter("TAG"));
    TEST_ASSERT_EQUAL(1,s_filterTagCount);
    s_filterTagCount=DEBUG_CONSOLE_MAX_FILTER_TAGS;
    TEST_ASSERT_EQUAL(ESP_ERR_NO_MEM,FlashLogger_AddFilter("another"));
    TEST_ASSERT_EQUAL(ESP_OK,FlashLogger_ClearFilters());TEST_ASSERT_EQUAL(0,s_filterTagCount);
}

static QueueHandle_t reject_log_queue(unsigned length,unsigned width) {
    (void)width;return length==DEBUG_CONSOLE_LOGGER_QUEUE_SIZE ? NULL : (QueueHandle_t)2;
}
void test_FlashLogger_LogQueueFailureCannotStartWorker(void) {
    mocked_xQueueCreate_fake.custom_fake=reject_log_queue;
    TEST_ASSERT_NULL(FlashLogger_GetModule());TEST_ASSERT_FALSE(atomic_load(&s_initialized));
    TEST_ASSERT_EQUAL(0,mocked_xTaskCreate_fake.call_count);
    TEST_ASSERT_EQUAL(ESP_OK,FlashLogger_Deinit());
    mocked_xQueueCreate_fake.custom_fake=NULL;
    TEST_ASSERT_NOT_NULL(FlashLogger_GetModule());
}
void test_FlashLogger_RotationDeletionFailurePreservesIndex(void) {
    TEST_ASSERT_NOT_NULL(FlashLogger_GetModule());
    LogMessage_t msg={.level=ESP_LOG_ERROR};
    file_size=DEBUG_CONSOLE_FLASH_LOG_MAX_SIZE/DEBUG_CONSOLE_FLASH_LOG_MAX_FILES;
    file_unlink_result=-1;
    TEST_ASSERT_FALSE(write_message(&msg));TEST_ASSERT_EQUAL(0,s_currentFileIndex);
    TEST_ASSERT_EQUAL(0,s_stats.totalLogsWritten);TEST_ASSERT_EQUAL(0,file_write_calls);
}
void test_FlashLogger_FailedWriteStatsResetKeepsFlushFailureEvidence(void) {
    TEST_ASSERT_NOT_NULL(FlashLogger_GetModule());atomic_store(&s_writeFailures,2);
    TEST_ASSERT_EQUAL(ESP_OK,FlashLogger_ResetStats());
    FlashLoggerStats stats;TEST_ASSERT_EQUAL(ESP_OK,FlashLogger_GetStats(&stats));TEST_ASSERT_EQUAL(0,stats.failedWrites);
    TEST_ASSERT_EQUAL(ESP_FAIL,FlashLogger_Flush());
    atomic_fetch_add(&s_writeFailures,1);
    TEST_ASSERT_EQUAL(ESP_OK,FlashLogger_GetStats(&stats));TEST_ASSERT_EQUAL(1,stats.failedWrites);
}

void test_FlashLogger_SnapshotUsesCurrentRotationAndOrdersLastRows(void) {
    TEST_ASSERT_NOT_NULL(FlashLogger_GetModule());s_currentFileIndex=2;
    read_rows[0]="first\n";read_rows[1]="second\n";read_rows[2]="third\n";
    char output[2*DEBUG_CONSOLE_LOGGER_LINE_CAPACITY+1];unsigned copied=0;
    TEST_ASSERT_EQUAL(ESP_OK,FlashLogger_CopyTail(output,sizeof(output),2,&copied));
    TEST_ASSERT_EQUAL_STRING(DEBUG_CONSOLE_FLASH_LOG_PATH ".2",opened_path);
    TEST_ASSERT_EQUAL_STRING("second\nthird\n",output);TEST_ASSERT_EQUAL(2,copied);
    TEST_ASSERT_EQUAL(1,allocated_scratch);TEST_ASSERT_EQUAL(1,freed_scratch);
}
void test_FlashLogger_SnapshotErrorsPreserveCallerOutput(void) {
    TEST_ASSERT_NOT_NULL(FlashLogger_GetModule());read_rows[0]="record\n";
    char output[DEBUG_CONSOLE_LOGGER_LINE_CAPACITY+1]="unchanged";unsigned copied=77;
    file_close_result=-1;
    TEST_ASSERT_EQUAL(ESP_FAIL,FlashLogger_CopyTail(output,sizeof(output),1,&copied));
    TEST_ASSERT_EQUAL_STRING("unchanged",output);TEST_ASSERT_EQUAL(77,copied);
    read_index=0;file_close_result=0;read_error=1;
    TEST_ASSERT_EQUAL(ESP_FAIL,FlashLogger_CopyTail(output,sizeof(output),1,&copied));
    TEST_ASSERT_EQUAL_STRING("unchanged",output);TEST_ASSERT_EQUAL(77,copied);
    TEST_ASSERT_EQUAL(allocated_scratch,freed_scratch);
}
void test_FlashLogger_SnapshotAllocationAndLockFailuresAreBounded(void) {
    TEST_ASSERT_NOT_NULL(FlashLogger_GetModule());
    char output[DEBUG_CONSOLE_LOGGER_LINE_CAPACITY+1]="original";unsigned copied=12;
    allocation_fails=true;
    TEST_ASSERT_EQUAL(ESP_ERR_NO_MEM,FlashLogger_CopyTail(output,sizeof(output),1,&copied));
    TEST_ASSERT_EQUAL(0,file_open_calls);
    allocation_fails=false;mocked_xSemaphoreTake_fake.return_val=pdFALSE;
    TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT,FlashLogger_CopyTail(output,sizeof(output),1,&copied));
    TEST_ASSERT_EQUAL(0,file_open_calls);TEST_ASSERT_EQUAL(1,freed_scratch);
    TEST_ASSERT_EQUAL_STRING("original",output);TEST_ASSERT_EQUAL(12,copied);
}
void test_FlashLogger_SnapshotRejectsCapacityAndCountBeforeAllocation(void) {
    char output[1]="";unsigned copied=11;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,FlashLogger_CopyTail(output,1,0,&copied));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,FlashLogger_CopyTail(output,1,DEBUG_CONSOLE_LOGGER_TAIL_LINES+1,&copied));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_SIZE,FlashLogger_CopyTail(output,1,1,&copied));
    TEST_ASSERT_EQUAL(0,allocated_scratch);TEST_ASSERT_EQUAL(11,copied);
}
