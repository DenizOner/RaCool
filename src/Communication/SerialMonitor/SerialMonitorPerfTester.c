#include "SerialMonitorInternal.h"
#include <esp_timer.h>
#include "Communication/SerialMonitor/SerialMonitorConfig.h"
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string.h>
#include <stdio.h>
#include <stdatomic.h>

static const char *TAG = "SerPerf";

typedef struct {
    atomic_uint total_queued;
    atomic_uint total_errors;
    uint32_t start_time;
    atomic_bool running;
    esp_timer_handle_t timer;
} PerfTestState;

static PerfTestState perfState = {0};

void SerialMonitor_PerfTickLocked(SerialMonitorPerfEnqueue enqueue) {
    if (!perfState.running) return;

    static uint32_t counter = 0;
    char packet[32];
    snprintf(packet, sizeof(packet), "TIMER_DATA:%08lu\n", counter++);
    
    // Attempt to write log (Asynchronously)
    if (enqueue(packet, strlen(packet)) != 0) {
        perfState.total_errors++;
    } else {
        perfState.total_queued++;
    }
}

static void timer_callback(void *arg) {
    (void)arg;
    SerialMonitor_PerfTick();
}

int32_t SerialMonitor_PerfStartLocked(uint32_t frequency_hz) {
    if (frequency_hz == 0 || frequency_hz > SERIAL_MONITOR_PERF_MAX_HZ) return -1;
    if (perfState.running) return -1;


    perfState.total_queued = 0;
    perfState.total_errors = 0;
    perfState.start_time = (uint32_t)(esp_timer_get_time() / 1000);

    const esp_timer_create_args_t timer_args = {
        .callback = &timer_callback,
        .name = "ser_perf_timer"
    };

    if (!perfState.timer) {
        esp_timer_handle_t created = NULL;
        if (esp_timer_create(&timer_args, &created) != ESP_OK) return -1;
        perfState.timer = created;
    }
    
    // Period in microseconds
    uint64_t period = 1000000ULL / frequency_hz;
    perfState.running = true;
    if (esp_timer_start_periodic(perfState.timer, period) != ESP_OK) {
        perfState.running = false;
        return -1;
    }
    ESP_LOGI(TAG, "Performance test started at %lu Hz", frequency_hz);
    return 0;
}

int32_t SerialMonitor_PerfStopLocked(void) {
    if (!perfState.running) return 0;
    perfState.running = false;
    esp_err_t result = esp_timer_stop(perfState.timer);
    if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) {
        perfState.running = true;
        return -1;
    }
    // Keep the single timer for subsequent runs; callbacks reference static state.

    uint32_t end_time = (uint32_t)(esp_timer_get_time() / 1000);
    uint32_t duration = end_time - perfState.start_time;

    ESP_LOGI(TAG, "Performance Test Finished:");
    ESP_LOGI(TAG, "Duration: %lu ms", duration);
    ESP_LOGI(TAG, "Total Queued: %lu", (unsigned long)atomic_load(&perfState.total_queued));
    ESP_LOGI(TAG, "Total Queue Errors: %lu", (unsigned long)atomic_load(&perfState.total_errors));
    if (duration > 0) {
        float throughput = (float)perfState.total_queued * 1000.0f / duration;
        ESP_LOGI(TAG, "Avg Throughput: %.2f packets/sec", throughput);
    }
    return 0;
}

#ifdef RACOOL_HEALTH_REPORT_DIAGNOSTIC
bool SerialMonitor_PerfDiagnosticActive(void) { return atomic_load(&perfState.running); }
#endif
