#include "unity.h"
#define ServiceLocator_GetService LogTarget_GetService
#include "../../../../src/Core/LogManager/LogManager.c"
#undef ServiceLocator_GetService
#include "../../../../src/Core/LogManager/LogManagerConfig.c"
IService *LogTarget_GetService(const char *name) { (void)name; return NULL; }
static ISerialMonitorPlugin plugin;
ISerialMonitorPlugin *LogManager_GetSerialPlugin(void) { return &plugin; }
static SemaphoreHandle_t sinkRelease, callbackDone;
static TaskHandle_t callbackTask;
static atomic_bool sinkEntered;
static bool active;
static int blocked_sink(const char *format, va_list args) {
    (void)format; (void)args;
    atomic_store(&sinkEntered, true);
    xSemaphoreTake(sinkRelease, portMAX_DELAY);
    return 1;
}
static int invoke_capture(const char *format, ...) {
    va_list args; va_start(args, format); int result = LogManager_vprint(format, args); va_end(args); return result;
}
static void captured_callback(void *arg) {
    (void)arg;
    invoke_capture("I: captured callback after stop\n");
    xSemaphoreGive(callbackDone);
    vTaskSuspend(NULL);
}
void target_log_capture_cleanup(void) {
    if (sinkRelease) xSemaphoreGive(sinkRelease);
    if (callbackTask) {
        TEST_ASSERT_EQUAL(pdTRUE, xSemaphoreTake(callbackDone, pdMS_TO_TICKS(2000)));
        vTaskDelete(callbackTask); callbackTask = NULL;
    }
    if (active) { TEST_ASSERT_EQUAL(0, LogManager_Stop(NULL)); active = false; }
    if (sinkRelease) { vSemaphoreDelete(sinkRelease); sinkRelease = NULL; }
    if (callbackDone) { vSemaphoreDelete(callbackDone); callbackDone = NULL; }
}
void target_log_capture_verify_lifetime(void) {
    TEST_ASSERT_EQUAL(0, LogManager_Initialize(NULL, NULL));
    TEST_ASSERT_EQUAL(0, LogManager_Start(NULL)); active = true;
    sinkRelease = xSemaphoreCreateBinary(); callbackDone = xSemaphoreCreateBinary();
    TEST_ASSERT_NOT_NULL(sinkRelease); TEST_ASSERT_NOT_NULL(callbackDone);
    /* Inject only the sink boundary. SDK output remains unchanged; the actual
     * production capture callback and ESP-IDF critical section run on device. */
    portENTER_CRITICAL(&log_spinlock);
    logManagerInstance.original_vprintf = blocked_sink;
    logManagerInstance.captureEnabled = true;
    portEXIT_CRITICAL(&log_spinlock);
    atomic_store(&sinkEntered, false);
    TEST_ASSERT_EQUAL(pdPASS, xTaskCreate(captured_callback, "LogCaptureTest", 4096, NULL, 2, &callbackTask));
    for (unsigned i = 0; i < 100 && !atomic_load(&sinkEntered); ++i) vTaskDelay(pdMS_TO_TICKS(10));
    TEST_ASSERT_TRUE(atomic_load(&sinkEntered));
    TEST_ASSERT_EQUAL(0, LogManager_Stop(NULL)); active = false;
    TEST_ASSERT_NULL(logManagerInstance.logBuffer);
    xSemaphoreGive(sinkRelease);
    TEST_ASSERT_EQUAL(pdTRUE, xSemaphoreTake(callbackDone, pdMS_TO_TICKS(2000)));
    vTaskDelete(callbackTask); callbackTask = NULL;
    TEST_ASSERT_EQUAL(0, logManagerInstance.logCount);
    TEST_ASSERT_EQUAL(0, LogManager_Start(NULL)); active = true;
    TEST_ASSERT_NOT_NULL(logManagerInstance.logBuffer);
    TEST_ASSERT_EQUAL(0, LogManager_Stop(NULL)); active = false;
    LogManagerConfig config = *LogManagerConfig_GetDefault();
    config.enableInterception = true;
    TEST_ASSERT_EQUAL(0, LogManager_InitializeWithConfig(NULL, &config, NULL));
    TEST_ASSERT_EQUAL(0, LogManager_Start(NULL)); active = true;
    vprintf_like_t original = logManagerInstance.original_vprintf;
    TEST_ASSERT_NOT_NULL(original);
    TEST_ASSERT_EQUAL_PTR(LogManager_vprint, esp_log_set_vprintf(LogManager_vprint));
    ESP_LOGI("LogCaptureTarget", "SDK hook capture smoke");
    SystemLogEntry entries[2];
    TEST_ASSERT_GREATER_THAN(0, LogManager_GetRecentLogs(entries, 2));
    TEST_ASSERT_EQUAL(0, LogManager_Stop(NULL)); active = false;
    TEST_ASSERT_EQUAL_PTR(original, esp_log_set_vprintf(original));
}
