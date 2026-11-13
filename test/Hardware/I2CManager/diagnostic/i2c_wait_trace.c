/* Diagnostic image only: preserve SDK behavior and report the failed wait caller.
 * No logging or allocation occurs inside the semaphore wrapper. */
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_log.h"
#include <stdatomic.h>
#include <string.h>

typedef struct {
    void *caller;
    QueueHandle_t queue;
    TickType_t budget;
    int64_t elapsed_us;
    BaseType_t result;
} WaitRecord;
typedef struct { unsigned count, dropped; WaitRecord waits[64]; } ReadTrace;
static _Atomic(TaskHandle_t) trace_owner;
/* Only the owning task accesses this stack-backed pointer. */
static ReadTrace *current_trace;

typedef struct {
    unsigned sequence;
    int64_t start_us, end_us;
    size_t bytes;
    esp_err_t result;
    UBaseType_t priority;
    char task[16];
} WriteRecord;
static portMUX_TYPE write_lock = portMUX_INITIALIZER_UNLOCKED;
static unsigned write_sequence;
static WriteRecord writes[16];
esp_err_t __real_i2c_master_transmit(i2c_master_dev_handle_t dev, const uint8_t *data, size_t size, int timeout);
esp_err_t __wrap_i2c_master_transmit(i2c_master_dev_handle_t dev, const uint8_t *data, size_t size, int timeout) {
    WriteRecord record = {.start_us = esp_timer_get_time(), .bytes = size, .result = ESP_ERR_NOT_FINISHED};
    strncpy(record.task, pcTaskGetName(NULL), sizeof(record.task) - 1);
    record.priority = uxTaskPriorityGet(NULL);
    portENTER_CRITICAL(&write_lock);
    unsigned sequence = ++write_sequence;
    record.sequence = sequence;
    writes[sequence % 16] = record;
    portEXIT_CRITICAL(&write_lock);
    TaskHandle_t vacant = NULL;
    bool owned = atomic_compare_exchange_strong(&trace_owner, &vacant, xTaskGetCurrentTaskHandle());
    ReadTrace trace = {0};
    if (owned) current_trace = &trace;
    esp_err_t result = __real_i2c_master_transmit(dev, data, size, timeout);
    if (owned) {
        current_trace = NULL;
        atomic_store(&trace_owner, NULL);
    }
    int64_t end = esp_timer_get_time();
    portENTER_CRITICAL(&write_lock);
    if (writes[sequence % 16].sequence == sequence) {
        writes[sequence % 16].end_us = end;
        writes[sequence % 16].result = result;
    }
    portEXIT_CRITICAL(&write_lock);
    if (owned && (result != ESP_OK || end - record.start_us > 200000)) {
        ESP_LOGW("I2C_WAIT_TRACE", "slow_write=%u bytes=%u timeout_ms=%d priority=%u start_us=%lld elapsed_us=%lld result=%d waits=%u dropped=%u",
            sequence, (unsigned)size, timeout, (unsigned)record.priority, (long long)record.start_us,
            (long long)(end-record.start_us), result, trace.count, trace.dropped);
        for (unsigned i=0;i<trace.count;++i) {
            const WaitRecord *wait=&trace.waits[i];
            ESP_LOGW("I2C_WAIT_TRACE", "write_wait=%u caller=%p ticks=%lu elapsed_us=%lld result=%ld",
                i,wait->caller,(unsigned long)wait->budget,(long long)wait->elapsed_us,(long)wait->result);
        }
    }
    return result;
}

static void DumpWrites(void) {
            WriteRecord recent[8];
            portENTER_CRITICAL(&write_lock);
            unsigned latest = write_sequence;
            for (unsigned i = 0; i < 8; ++i) recent[i] = writes[(latest - i) % 16];
            portEXIT_CRITICAL(&write_lock);
            for (unsigned i = 0; i < 8; ++i) {
                const WriteRecord *w = &recent[i];
                if (w->sequence == 0) continue;
                ESP_LOGW("I2C_WAIT_TRACE", "write=%u task=%s priority=%u bytes=%u start_us=%lld end_us=%lld result=%d",
                    w->sequence, w->task, (unsigned)w->priority, (unsigned)w->bytes, (long long)w->start_us,
                    (long long)w->end_us, w->result);
            }
}

void RaCool_I2CTraceManagerWait(int64_t start, int64_t elapsed) {
    ESP_LOGW("I2C_WAIT_TRACE", "manager timeout task=%s priority=%u start_us=%lld elapsed_us=%lld",
        pcTaskGetName(NULL), (unsigned)uxTaskPriorityGet(NULL), (long long)start, (long long)elapsed);
    DumpWrites();
}

BaseType_t __real_xQueueSemaphoreTake(QueueHandle_t queue, TickType_t ticks);
BaseType_t __wrap_xQueueSemaphoreTake(QueueHandle_t queue, TickType_t ticks) {
    bool traced = atomic_load(&trace_owner) == xTaskGetCurrentTaskHandle();
    int64_t start = traced ? esp_timer_get_time() : 0;
    BaseType_t result = __real_xQueueSemaphoreTake(queue, ticks);
    if (traced && current_trace) {
        ReadTrace *trace = current_trace;
        if (trace->count < sizeof(trace->waits) / sizeof(trace->waits[0])) {
            trace->waits[trace->count++] = (WaitRecord){
                .caller = __builtin_return_address(0), .queue = queue,
                .budget = ticks, .elapsed_us = esp_timer_get_time() - start,
                .result = result,
            };
        } else trace->dropped++;
    }
    return result;
}

esp_err_t __real_i2c_master_transmit_receive(i2c_master_dev_handle_t dev,
    const uint8_t *write, size_t write_size, uint8_t *read, size_t read_size, int timeout);
esp_err_t __wrap_i2c_master_transmit_receive(i2c_master_dev_handle_t dev,
    const uint8_t *write, size_t write_size, uint8_t *read, size_t read_size, int timeout) {
    TaskHandle_t vacant = NULL;
    bool owned = atomic_compare_exchange_strong(&trace_owner, &vacant, xTaskGetCurrentTaskHandle());
    ReadTrace trace = {0};
    if (owned) current_trace = &trace;
    int64_t start = esp_timer_get_time();
    esp_err_t result = __real_i2c_master_transmit_receive(dev, write, write_size, read, read_size, timeout);
    int64_t elapsed = esp_timer_get_time() - start;
    if (owned) {
        current_trace = NULL;
        atomic_store(&trace_owner, NULL);
        if (result != ESP_OK) {
            ESP_LOGW("I2C_WAIT_TRACE", "read error=%d start_us=%lld elapsed_us=%lld waits=%u dropped=%u",
                result, (long long)start, (long long)elapsed, trace.count, trace.dropped);
            DumpWrites();
            for (unsigned i = 0; i < trace.count; ++i) {
                const WaitRecord *w = &trace.waits[i];
                ESP_LOGW("I2C_WAIT_TRACE", "wait=%u caller=%p queue=%p ticks=%lu elapsed_us=%lld result=%ld",
                    i, w->caller, w->queue, (unsigned long)w->budget,
                    (long long)w->elapsed_us, (long)w->result);
            }
        }
    }
    return result;
}
