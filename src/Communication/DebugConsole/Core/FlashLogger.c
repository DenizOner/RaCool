#include "Communication/DebugConsole/Core/FlashLogger.h"
#include "Communication/DebugConsole/DebugConsoleConfig.h"
#include "Management/Watchdog/WatchdogWorker.h"
#include "esp_littlefs.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdarg.h>
#include <unistd.h>
#include <errno.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <ctype.h>

#define MAX_LOG_MSG_LEN DEBUG_CONSOLE_LOGGER_MESSAGE_CAPACITY
_Static_assert(DEBUG_CONSOLE_FLASH_LOG_MAX_FILES > 0, "Log file count must be positive");
_Static_assert(DEBUG_CONSOLE_FLASH_LOG_MAX_SIZE >= DEBUG_CONSOLE_FLASH_LOG_MAX_FILES, "Log file budget must be positive");
_Static_assert(DEBUG_CONSOLE_LOGGER_QUEUE_SIZE > 0, "Logger queue must be positive");

typedef struct {
    uint32_t timestamp;
    esp_log_level_t level;
    char tag[16];
    char message[MAX_LOG_MSG_LEN];
} LogMessage_t;

/* Initialize before publishing to concurrent consumers. All control resources,
 * the mounted filesystem and copied queue items remain process-owned on Stop.
 * Lock order: control -> file -> stats; filters have no dependency on them.
 * The worker checkpoints outside all locks and never takes control. */
static QueueHandle_t s_logQueue;
static SemaphoreHandle_t s_controlMutex,s_fileMutex,s_statsMutex,s_filterMutex;
static WatchdogWorker s_worker;
static atomic_bool s_initialized,s_closing;
static atomic_uint s_accepted,s_completed,s_writeFailures;
static unsigned s_acknowledgedFailures;
static FlashLoggerStats s_stats;
static int s_currentFileIndex;
static char s_filterTags[DEBUG_CONSOLE_MAX_FILTER_TAGS][16];
static unsigned s_filterTagCount;
static ILogManager *s_captureSource;
static FlashCaptureStatus s_capture;
static uint64_t s_captureStartedUs, s_captureDurationUs;
static uint32_t s_captureCursor;
static esp_err_t finish_capture(void) {
    s_capture.active=false;
    if (s_captureSource && s_captureSource->EndCapture(s_captureSource)!=0) return ESP_ERR_TIMEOUT;
    s_captureSource=NULL;return ESP_OK;
}

static bool private_log(const char *text) {
    static const char *words[] = {"password", "passwd", "token", "credential", "secret", "network_key", "link_key", "pin"};
    for (size_t word=0; word<sizeof(words)/sizeof(words[0]); ++word) {
        for (const char *start=text; *start; ++start) {
            size_t i=0;
            while (words[word][i] && start[i] && (unsigned char)tolower((unsigned char)start[i]) == (unsigned char)words[word][i]) ++i;
            if (!words[word][i]) return true;
        }
    }
    return false;
}

esp_err_t FlashLogger_StartCapture(ILogManager *source, uint32_t seconds, uint32_t records) {
    if (!source || !source->GetRecentLogs || !source->BeginCapture || !source->EndCapture || !seconds || seconds>DEBUG_CONSOLE_CAPTURE_MAX_SECONDS ||
        !records || records>DEBUG_CONSOLE_CAPTURE_MAX_RECORDS) return ESP_ERR_INVALID_ARG;
    if (!atomic_load(&s_initialized) && !FlashLogger_GetModule()) return ESP_ERR_INVALID_STATE;
    if (xSemaphoreTake(s_controlMutex,pdMS_TO_TICKS(DEBUG_CONSOLE_LOGGER_LOCK_MS)+1)!=pdTRUE) return ESP_ERR_TIMEOUT;
    esp_err_t result=ESP_ERR_INVALID_STATE;
    if (!s_captureSource && !atomic_load(&s_closing) && source->BeginCapture(source)==0) {
        SystemLogEntry latest;
        uint32_t count=source->GetRecentLogs(&latest,1);
        s_captureCursor=count ? latest.id : 0;
        s_captureSource=source;
        s_capture=(FlashCaptureStatus){.active=true,.remaining=records};
        s_captureStartedUs=(uint64_t)esp_timer_get_time();
        s_captureDurationUs=(uint64_t)seconds*1000000ULL;
        result=ESP_OK;
    }
    xSemaphoreGive(s_controlMutex); return result;
}
esp_err_t FlashLogger_StopCapture(void) {
    if (!atomic_load(&s_initialized)) return ESP_ERR_INVALID_STATE;
    if (xSemaphoreTake(s_controlMutex,pdMS_TO_TICKS(DEBUG_CONSOLE_LOGGER_LOCK_MS)+1)!=pdTRUE) return ESP_ERR_TIMEOUT;
    esp_err_t result=finish_capture();
    xSemaphoreGive(s_controlMutex); return result;
}
esp_err_t FlashLogger_GetCaptureStatus(FlashCaptureStatus *out) {
    if (!out) return ESP_ERR_INVALID_ARG;
    if (!atomic_load(&s_initialized)) {*out=(FlashCaptureStatus){0};return ESP_OK;}
    if (xSemaphoreTake(s_controlMutex,pdMS_TO_TICKS(DEBUG_CONSOLE_LOGGER_LOCK_MS)+1)!=pdTRUE) return ESP_ERR_TIMEOUT;
    *out=s_capture; xSemaphoreGive(s_controlMutex); return ESP_OK;
}
static void capture_recent_logs(void) {
    if (xSemaphoreTake(s_controlMutex,0)!=pdTRUE) return;
    uint64_t now=(uint64_t)esp_timer_get_time();
    if (s_capture.active && (now<s_captureStartedUs || now-s_captureStartedUs>=s_captureDurationUs || atomic_load(&s_closing))) {
        finish_capture();
    }
    if (!s_capture.active && s_captureSource) finish_capture();
    if (s_capture.active) {
        SystemLogEntry entries[DEBUG_CONSOLE_CAPTURE_BATCH];
        uint32_t count=s_captureSource->GetRecentLogs(entries,DEBUG_CONSOLE_CAPTURE_BATCH);
        if (count>DEBUG_CONSOLE_CAPTURE_BATCH) {finish_capture(); count=0;}
        for (uint32_t i=0; i<count && s_capture.active; ++i) {
            uint32_t distance=entries[i].id-s_captureCursor;
            if (!distance || distance>UINT32_MAX/2) continue;
            s_captureCursor=entries[i].id;
            LogMessage_t msg={.timestamp=entries[i].timestamp*1000u,.level=ESP_LOG_INFO};
            snprintf(msg.tag,sizeof(msg.tag),"Capture");
            snprintf(msg.message,sizeof(msg.message),"%s", private_log(entries[i].message) ? "[REDACTED]" : entries[i].message);
            if (xQueueSend(s_logQueue,&msg,0)==pdTRUE) {++s_capture.admitted; atomic_fetch_add(&s_accepted,1);}
            else ++s_capture.rejected;
            if (!--s_capture.remaining) finish_capture();
        }
    }
    xSemaphoreGive(s_controlMutex);
}

static TickType_t wait_ticks(uint32_t ms) {
    TickType_t value=pdMS_TO_TICKS(ms);return value ? value : 1;
}
static bool valid_tag(const char *tag) {return tag && tag[0] && strnlen(tag,16)<16;}
static void record_drop(void) {
    if (s_statsMutex && xSemaphoreTake(s_statsMutex,0)==pdTRUE) {
        ++s_stats.droppedLogs;xSemaphoreGive(s_statsMutex);
    }
}
static bool get_log_path(int index,char *path,size_t capacity) {
    int written=index ? snprintf(path,capacity,"%s.%d",DEBUG_CONSOLE_FLASH_LOG_PATH,index)
                      : snprintf(path,capacity,"%s",DEBUG_CONSOLE_FLASH_LOG_PATH);
    return written>=0 && (size_t)written<capacity;
}
static esp_err_t init_littlefs(void) {
    if (esp_littlefs_mounted("storage")) return ESP_OK;
    esp_vfs_littlefs_conf_t conf={.base_path=DEBUG_CONSOLE_FLASH_MOUNT_POINT,
        .partition_label="storage",.format_if_mount_failed=false,.dont_mount=false};
    return esp_vfs_littlefs_register(&conf);
}
static bool write_message(const LogMessage_t *msg) {
    static const char *levels[]={"NONE","ERROR","WARN","INFO","DEBUG","VERBOSE"};
    char path[64];bool success=false;FILE *file=NULL;
    if (xSemaphoreTake(s_fileMutex,wait_ticks(DEBUG_CONSOLE_LOGGER_CONTROL_MS))!=pdTRUE) return false;
    if (!get_log_path(s_currentFileIndex,path,sizeof(path))) goto done;
    file=fopen(path,"a");if (!file) goto done;
    if (fseek(file,0,SEEK_END)!=0) goto done;
    long size=ftell(file);if (size<0) goto done;
    size_t limit=DEBUG_CONSOLE_FLASH_LOG_MAX_SIZE/DEBUG_CONSOLE_FLASH_LOG_MAX_FILES;
    if ((size_t)size>=limit) {
        int closeResult=fclose(file);file=NULL;if (closeResult!=0) goto done;
        int next=(s_currentFileIndex+1)%DEBUG_CONSOLE_FLASH_LOG_MAX_FILES;
        if (!get_log_path(next,path,sizeof(path))) goto done;
        if (unlink(path)!=0 && errno!=ENOENT) goto done;
        file=fopen(path,"w");if (!file) goto done;
        s_currentFileIndex=next;size=0;
    }
    int written=fprintf(file,"[%lu] [%s] [%s] %s\n",(unsigned long)msg->timestamp,
        levels[msg->level],msg->tag,msg->message);
    if (written<=0) goto done;
    int closeResult=fclose(file);file=NULL;if (closeResult!=0) goto done;
    success=true;
    if (xSemaphoreTake(s_statsMutex,wait_ticks(DEBUG_CONSOLE_LOGGER_LOCK_MS))==pdTRUE) {
        ++s_stats.totalLogsWritten;s_stats.lastLogTimestamp=msg->timestamp;
        s_stats.currentFileIndex=(uint32_t)s_currentFileIndex;
        s_stats.currentFileSize=(uint32_t)((size_t)size+(size_t)written);
        xSemaphoreGive(s_statsMutex);
    }
done:
    if (file && fclose(file)!=0) success=false;
    xSemaphoreGive(s_fileMutex);return success;
}
static void logger_task(void *argument) {
    (void)argument;
    for (;;) {
        WatchdogWorker_Checkpoint(&s_worker,NULL,(uint64_t)esp_timer_get_time()/1000u);
        capture_recent_logs();
        LogMessage_t msg;
        if (xQueueReceive(s_logQueue,&msg,wait_ticks(DEBUG_CONSOLE_LOGGER_POLL_MS))!=pdTRUE) continue;
        if (!write_message(&msg)) atomic_fetch_add(&s_writeFailures,1);
        /* Completion is after file closure, not merely queue removal. */
        atomic_fetch_add(&s_completed,1);
    }
}
static void Logger_VLog(ILogger *self,esp_log_level_t level,const char *tag,const char *format,va_list arguments) {
    (void)self;
    if (!format || level<ESP_LOG_NONE || level>ESP_LOG_VERBOSE ||
        level>DEBUG_CONSOLE_FLASH_LOG_LEVEL || !atomic_load(&s_initialized)) return;
    if (xSemaphoreTake(s_controlMutex,0)!=pdTRUE) {record_drop();return;}
    if (!atomic_load(&s_initialized) || atomic_load(&s_closing)) {xSemaphoreGive(s_controlMutex);record_drop();return;}
    LogMessage_t msg={.timestamp=(uint32_t)(esp_timer_get_time()/1000),.level=level};
    snprintf(msg.tag,sizeof(msg.tag),"%s",tag ? tag : "?");
    int length=vsnprintf(msg.message,sizeof(msg.message),format,arguments);
    if (length<0 || xQueueSend(s_logQueue,&msg,0)!=pdTRUE) record_drop();
    else atomic_fetch_add(&s_accepted,1);
    xSemaphoreGive(s_controlMutex);
}
static void Logger_Log(ILogger *self,esp_log_level_t level,const char *tag,const char *format,...) {
    va_list arguments;va_start(arguments,format);Logger_VLog(self,level,tag,format,arguments);va_end(arguments);
}
static ILogger s_loggerInterface={.Log=Logger_Log,.VLog=Logger_VLog};

ILogger *FlashLogger_GetModule(void) {
    if (atomic_load(&s_initialized)) return atomic_load(&s_closing) ? NULL : &s_loggerInterface;
    if (!s_controlMutex) s_controlMutex=xSemaphoreCreateMutex();
    if (!s_controlMutex || xSemaphoreTake(s_controlMutex,wait_ticks(DEBUG_CONSOLE_LOGGER_CONTROL_MS))!=pdTRUE) return NULL;
    ILogger *result=NULL;
    if (atomic_load(&s_closing)) goto done;
    if (!s_statsMutex) s_statsMutex=xSemaphoreCreateMutex();
    if (!s_filterMutex) s_filterMutex=xSemaphoreCreateMutex();
    if (!s_fileMutex) s_fileMutex=xSemaphoreCreateMutex();
    if (!s_statsMutex || !s_filterMutex || !s_fileMutex || WatchdogWorker_Init(&s_worker)!=0) goto done;
    char path[64];if (!get_log_path(DEBUG_CONSOLE_FLASH_LOG_MAX_FILES-1,path,sizeof(path))) goto done;
    if (init_littlefs()!=ESP_OK) goto done;
    if (!s_logQueue) s_logQueue=xQueueCreate(DEBUG_CONSOLE_LOGGER_QUEUE_SIZE,sizeof(LogMessage_t));
    if (!s_logQueue || WatchdogWorker_Start(&s_worker,logger_task,"logger_task",
        DEBUG_CONSOLE_LOGGER_TASK_STACK_SIZE,NULL,DEBUG_CONSOLE_LOGGER_TASK_PRIORITY)!=0) goto done;
    atomic_store(&s_initialized,true);result=&s_loggerInterface;
done:
    xSemaphoreGive(s_controlMutex);return result;
}
esp_err_t FlashLogger_Deinit(void) {
    if (!s_controlMutex) return ESP_ERR_INVALID_STATE;
    if (xSemaphoreTake(s_controlMutex,wait_ticks(DEBUG_CONSOLE_LOGGER_CONTROL_MS))!=pdTRUE) return ESP_ERR_TIMEOUT;
    atomic_store(&s_closing,true);
    if (finish_capture()!=ESP_OK) {xSemaphoreGive(s_controlMutex);return ESP_ERR_TIMEOUT;}
    esp_err_t result=!atomic_load(&s_worker.task) || WatchdogWorker_Stop(&s_worker,wait_ticks(DEBUG_CONSOLE_LOGGER_STOP_MS))==0 ? ESP_OK : ESP_ERR_TIMEOUT;
    if (result==ESP_OK) {atomic_store(&s_initialized,false);atomic_store(&s_closing,false);}
    /* Failed Stop retains task, queue, filesystem and admission closure. */
    xSemaphoreGive(s_controlMutex);return result;
}
static esp_err_t flush_owned(void) {
    if (!atomic_load(&s_initialized) || atomic_load(&s_closing)) return ESP_ERR_INVALID_STATE;
    unsigned target=atomic_load(&s_accepted);TickType_t started=xTaskGetTickCount();
    while (atomic_load(&s_completed)!=target) {
        if (xTaskGetTickCount()-started>=wait_ticks(DEBUG_CONSOLE_LOGGER_FLUSH_MS)) return ESP_ERR_TIMEOUT;
        vTaskDelay(wait_ticks(DEBUG_CONSOLE_LOGGER_POLL_MS));
    }
    unsigned failures=atomic_load(&s_writeFailures);
    bool failed=failures!=s_acknowledgedFailures;s_acknowledgedFailures=failures;
    return failed ? ESP_FAIL : ESP_OK;
}
esp_err_t FlashLogger_Flush(void) {
    if (!s_controlMutex) return ESP_ERR_INVALID_STATE;
    if (xSemaphoreTake(s_controlMutex,wait_ticks(DEBUG_CONSOLE_LOGGER_CONTROL_MS))!=pdTRUE) return ESP_ERR_TIMEOUT;
    esp_err_t result=flush_owned();xSemaphoreGive(s_controlMutex);return result;
}
esp_err_t FlashLogger_Clear(void) {
    if (!s_controlMutex) return ESP_ERR_INVALID_STATE;
    if (xSemaphoreTake(s_controlMutex,wait_ticks(DEBUG_CONSOLE_LOGGER_CONTROL_MS))!=pdTRUE) return ESP_ERR_TIMEOUT;
    esp_err_t result=flush_owned();
    if (result!=ESP_OK) goto done;
    if (xSemaphoreTake(s_fileMutex,wait_ticks(DEBUG_CONSOLE_LOGGER_LOCK_MS))!=pdTRUE) {result=ESP_ERR_TIMEOUT;goto done;}
    for (int i=0;i<DEBUG_CONSOLE_FLASH_LOG_MAX_FILES;++i) {
        char path[64];
        if (!get_log_path(i,path,sizeof(path)) || (unlink(path)!=0 && errno!=ENOENT)) {result=ESP_FAIL;break;}
    }
    if (result==ESP_OK) {
        if (xSemaphoreTake(s_statsMutex,wait_ticks(DEBUG_CONSOLE_LOGGER_LOCK_MS))!=pdTRUE) result=ESP_ERR_TIMEOUT;
        else {memset(&s_stats,0,sizeof(s_stats));s_stats.failedWrites=atomic_load(&s_writeFailures);s_currentFileIndex=0;xSemaphoreGive(s_statsMutex);}
    }
    xSemaphoreGive(s_fileMutex);
done:
    xSemaphoreGive(s_controlMutex);return result;
}
esp_err_t FlashLogger_GetStats(FlashLoggerStats *stats) {
    if (!stats) return ESP_ERR_INVALID_ARG;
    if (!s_statsMutex) return ESP_ERR_INVALID_STATE;
    if (xSemaphoreTake(s_statsMutex,wait_ticks(DEBUG_CONSOLE_LOGGER_LOCK_MS))!=pdTRUE) return ESP_ERR_TIMEOUT;
    *stats=s_stats;stats->failedWrites=atomic_load(&s_writeFailures)-s_stats.failedWrites;xSemaphoreGive(s_statsMutex);return ESP_OK;
}
esp_err_t FlashLogger_ResetStats(void) {
    if (!s_statsMutex) return ESP_ERR_INVALID_STATE;
    if (xSemaphoreTake(s_statsMutex,wait_ticks(DEBUG_CONSOLE_LOGGER_LOCK_MS))!=pdTRUE) return ESP_ERR_TIMEOUT;
    memset(&s_stats,0,sizeof(s_stats));s_stats.failedWrites=atomic_load(&s_writeFailures);xSemaphoreGive(s_statsMutex);return ESP_OK;
}
esp_err_t FlashLogger_AddFilter(const char *tag) {
    if (!valid_tag(tag)) return ESP_ERR_INVALID_ARG;
    if (!s_filterMutex) return ESP_ERR_INVALID_STATE;
    if (xSemaphoreTake(s_filterMutex,wait_ticks(DEBUG_CONSOLE_LOGGER_LOCK_MS))!=pdTRUE) return ESP_ERR_TIMEOUT;
    esp_err_t result=ESP_OK;
    for (unsigned i=0;i<s_filterTagCount;++i) if (!strcasecmp(s_filterTags[i],tag)) goto done;
    if (s_filterTagCount>=DEBUG_CONSOLE_MAX_FILTER_TAGS) {result=ESP_ERR_NO_MEM;goto done;}
    if (!s_filterTagCount) esp_log_level_set("*",ESP_LOG_WARN);
    strcpy(s_filterTags[s_filterTagCount++],tag);esp_log_level_set(tag,ESP_LOG_INFO);
done:
    xSemaphoreGive(s_filterMutex);return result;
}
esp_err_t FlashLogger_ClearFilters(void) {
    if (!s_filterMutex) return ESP_ERR_INVALID_STATE;
    if (xSemaphoreTake(s_filterMutex,wait_ticks(DEBUG_CONSOLE_LOGGER_LOCK_MS))!=pdTRUE) return ESP_ERR_TIMEOUT;
    s_filterTagCount=0;esp_log_level_set("*",ESP_LOG_INFO);xSemaphoreGive(s_filterMutex);return ESP_OK;
}
bool FlashLogger_IsTagFiltered(const char *tag) {
    if (!tag || !s_filterMutex || xSemaphoreTake(s_filterMutex,0)!=pdTRUE) return false;
    bool found=s_filterTagCount==0;
    for (unsigned i=0;i<s_filterTagCount;++i) if (!strcasecmp(s_filterTags[i],tag)) {found=true;break;}
    xSemaphoreGive(s_filterMutex);return found;
}

esp_err_t FlashLogger_CopyTail(char *output,size_t capacity,unsigned maxLines,unsigned *copiedLines) {
    if (!output || !copiedLines || !maxLines || maxLines>DEBUG_CONSOLE_LOGGER_TAIL_LINES ||
        maxLines>(SIZE_MAX-1)/DEBUG_CONSOLE_LOGGER_LINE_CAPACITY) return ESP_ERR_INVALID_ARG;
    size_t bytes=(size_t)maxLines*DEBUG_CONSOLE_LOGGER_LINE_CAPACITY;
    if (capacity<bytes+1) return ESP_ERR_INVALID_SIZE;
    if (!s_fileMutex) return ESP_ERR_INVALID_STATE;
    char *scratch=malloc(bytes);if (!scratch) return ESP_ERR_NO_MEM;
    if (xSemaphoreTake(s_fileMutex,wait_ticks(DEBUG_CONSOLE_LOGGER_LOCK_MS))!=pdTRUE) {free(scratch);return ESP_ERR_TIMEOUT;}
    char path[64];FILE *file=NULL;unsigned kept=0,index=0;size_t readBytes=0;
    esp_err_t result=ESP_OK;uint64_t started=(uint64_t)esp_timer_get_time();
    if (!get_log_path(s_currentFileIndex,path,sizeof(path))) {result=ESP_ERR_INVALID_SIZE;goto done;}
    file=fopen(path,"r");
    if (!file) {if (errno!=ENOENT) result=ESP_FAIL;goto done;}
    while (fgets(scratch+(size_t)index*DEBUG_CONSOLE_LOGGER_LINE_CAPACITY,DEBUG_CONSOLE_LOGGER_LINE_CAPACITY,file)) {
        readBytes+=strlen(scratch+(size_t)index*DEBUG_CONSOLE_LOGGER_LINE_CAPACITY);
        if (readBytes>DEBUG_CONSOLE_FLASH_LOG_MAX_SIZE) {result=ESP_ERR_INVALID_SIZE;break;}
        if ((uint64_t)esp_timer_get_time()-started>=(uint64_t)DEBUG_CONSOLE_LOGGER_READ_MS*1000u) {result=ESP_ERR_TIMEOUT;break;}
        index=(index+1)%maxLines;if (kept<maxLines) ++kept;
    }
    if (ferror(file)) result=ESP_FAIL;
    if (fclose(file)!=0) result=ESP_FAIL;
    file=NULL;
done:
    if (file && fclose(file)!=0) result=ESP_FAIL;
    xSemaphoreGive(s_fileMutex);
    if (result==ESP_OK) {
        size_t length=0;unsigned start=kept<maxLines ? 0 : index;
        for (unsigned i=0;i<kept;++i) {
            const char *row=scratch+(size_t)((start+i)%maxLines)*DEBUG_CONSOLE_LOGGER_LINE_CAPACITY;
            size_t rowLength=strlen(row);memcpy(output+length,row,rowLength);length+=rowLength;
        }
        output[length]='\0';*copiedLines=kept;
    }
    free(scratch);return result;
}
