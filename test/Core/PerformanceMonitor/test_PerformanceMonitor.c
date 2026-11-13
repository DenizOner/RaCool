#ifdef _WIN32
#include <io.h>
#define perf_capture_dup _dup
#define perf_capture_dup2 _dup2
#define perf_capture_close _close
#define perf_capture_fileno _fileno
#else
#include <unistd.h>
#define perf_capture_dup dup
#define perf_capture_dup2 dup2
#define perf_capture_close close
#define perf_capture_fileno fileno
#endif
#include "unity.h"
#undef pdMS_TO_TICKS
#define pdMS_TO_TICKS(ms) ((ms) / 10U)
#include "Core/PerformanceMonitor/PerformanceMonitor.c"
#include "Core/PerformanceMonitor/PerformanceMonitorConsoleCommands.c"

static BaseType_t create_worker(TaskFunction_t fn, const char *name, uint32_t stack, void *arg, uint32_t priority, TaskHandle_t *task) {
    (void)fn; (void)name; (void)stack; (void)arg; (void)priority;
    *task = (TaskHandle_t)3; return pdPASS;
}
void setUp(void) {
    memset(&monitorWorker, 0, sizeof(monitorWorker));
    controlMutex = NULL; monitorState = SERVICE_STATE_UNINITIALIZED;
    cpuBaseline = cpuValid = false;
    RESET_FAKE(mocked_xSemaphoreCreateMutex); RESET_FAKE(mocked_xSemaphoreTake);
    RESET_FAKE(mocked_xQueueCreate); RESET_FAKE(mocked_xTaskCreate);
    RESET_FAKE(mocked_xQueueReceive); RESET_FAKE(mocked_xQueueSend);
    RESET_FAKE(mocked_vTaskDelete); RESET_FAKE(mocked_xTaskGetCurrentTaskHandle);
    mocked_xSemaphoreCreateMutex_fake.return_val = (SemaphoreHandle_t)1;
    mocked_xSemaphoreTake_fake.return_val = pdTRUE;
    mocked_xQueueCreate_fake.return_val = (QueueHandle_t)2;
    mocked_xTaskCreate_fake.custom_fake = create_worker;
    mocked_xTaskGetCurrentTaskHandle_fake.return_val = (TaskHandle_t)4;
}
void tearDown(void) {}
void test_PerformanceMonitor_ValidatesConfiguration(void) {
    PerformanceMonitorConfig config = PerformanceMonitorConfig_GetDefault();
    config.reportIntervalMs = 0;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, Perf_InitializeWithConfig(config));
    config.reportIntervalMs = PERF_MONITOR_MAX_INTERVAL_MS + 1;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, Perf_InitializeWithConfig(config));
    config = PerformanceMonitorConfig_GetDefault(); config.taskPriority = configMAX_PRIORITIES;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, Perf_InitializeWithConfig(config));
    config = PerformanceMonitorConfig_GetDefault(); config.taskStackSize = 1;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, Perf_InitializeWithConfig(config));
    TEST_ASSERT_EQUAL(SERVICE_STATE_UNINITIALIZED, monitorState);
}
void test_PerformanceMonitor_PreservesOwnershipAfterStopTimeout(void) {
    TEST_ASSERT_EQUAL(ESP_OK, Perf_Init());
    TEST_ASSERT_EQUAL(0, Perf_Service_Start(NULL));
    TEST_ASSERT_EQUAL(-1, Perf_Service_Stop(NULL));
    TEST_ASSERT_EQUAL_PTR((TaskHandle_t)3, atomic_load(&monitorWorker.task));
    TEST_ASSERT_EQUAL(0, mocked_vTaskDelete_fake.call_count);
    TEST_ASSERT_EQUAL(SERVICE_STATE_ERROR, monitorState);
    TEST_ASSERT_EQUAL(-1, Perf_Service_Start(NULL));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, Perf_InitializeWithConfig(PerformanceMonitorConfig_GetDefault()));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, Perf_Init());
}
static BaseType_t receive_stop(QueueHandle_t queue, void *value, TickType_t wait) {
    (void)queue; (void)wait; *(int32_t *)value = 0; return pdTRUE;
}
void test_PerformanceMonitor_StopRetryAndRestart(void) {
    TEST_ASSERT_EQUAL(ESP_OK, Perf_Init()); TEST_ASSERT_EQUAL(0, Perf_Service_Start(NULL));
    TEST_ASSERT_EQUAL(-1, Perf_Service_Stop(NULL));
    mocked_xQueueReceive_fake.custom_fake = receive_stop;
    TEST_ASSERT_EQUAL(0, Perf_Service_Stop(NULL));
    TEST_ASSERT_EQUAL(1, mocked_vTaskDelete_fake.call_count);
    TEST_ASSERT_NULL(atomic_load(&monitorWorker.task));
    TEST_ASSERT_EQUAL(SERVICE_STATE_STOPPED, monitorState);
    TEST_ASSERT_EQUAL(0, Perf_Service_Start(NULL));
}
void test_PerformanceMonitor_StartAndAllocationFailures(void) {
    TEST_ASSERT_EQUAL(-1, Perf_Service_Start(NULL));
    mocked_xSemaphoreCreateMutex_fake.return_val = NULL;
    TEST_ASSERT_EQUAL(ESP_ERR_NO_MEM, Perf_Init());
    mocked_xSemaphoreCreateMutex_fake.return_val = (SemaphoreHandle_t)1;
    TEST_ASSERT_EQUAL(ESP_OK, Perf_Init());
    mocked_xTaskCreate_fake.custom_fake = NULL; mocked_xTaskCreate_fake.return_val = pdFAIL;
    TEST_ASSERT_EQUAL(-1, Perf_Service_Start(NULL));
    TEST_ASSERT_EQUAL(SERVICE_STATE_INITIALIZED, monitorState);
    TEST_ASSERT_EQUAL(-1, Perf_Service_CheckHealth(NULL));
}
void test_PerformanceMonitor_PositiveDelayAlwaysYields(void) {
    TEST_ASSERT_EQUAL(1, Perf_DelayTicks(1));
    TEST_ASSERT_EQUAL(1, Perf_DelayTicks(9));
    TEST_ASSERT_EQUAL(10, Perf_DelayTicks(100));
}
void test_PerformanceMonitor_CLIPropagatesFailures(void) {
    char *start[] = {"perf_start"}; char *stop[] = {"perf_stop"};
    TEST_ASSERT_NOT_EQUAL(0, cmd_perf_start(1, start));
    TEST_ASSERT_NOT_EQUAL(0, cmd_perf_stop(1, stop));
    TEST_ASSERT_EQUAL(ESP_OK, Perf_Init());
    TEST_ASSERT_EQUAL(0, cmd_perf_start(1, start));
    TEST_ASSERT_NOT_EQUAL(0, cmd_perf_stop(1, stop));
    TEST_ASSERT_NOT_EQUAL(0, cmd_perf_start(2, start));
}

void test_CpuUsageMeasuresWindowAndHandlesIdleCounterWrap(void) {
    monitorState = SERVICE_STATE_STARTED;
    CpuUsageSnapshot result;
    Perf_UpdateCpuCounters(UINT32_MAX - 499999, 1000000);
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FINISHED, Perf_GetCpuUsage(&result));
    Perf_UpdateCpuCounters(0, 2000000);
    TEST_ASSERT_EQUAL(ESP_OK, Perf_GetCpuUsage(&result));
    TEST_ASSERT_EQUAL(5000, result.busy_hundredths_percent);
    TEST_ASSERT_EQUAL(1000000, result.window_us);
    Perf_UpdateCpuCounters(1000000, 3000000);
    TEST_ASSERT_EQUAL(ESP_OK, Perf_GetCpuUsage(&result));
    TEST_ASSERT_EQUAL(0, result.busy_hundredths_percent);
    Perf_UpdateCpuCounters(1000000, 4000000);
    TEST_ASSERT_EQUAL(ESP_OK, Perf_GetCpuUsage(&result));
    TEST_ASSERT_EQUAL(10000, result.busy_hundredths_percent);
}
void test_CpuUsageRejectsClockCounterInconsistencyAndPreservesOutput(void) {
    monitorState = SERVICE_STATE_STARTED;
    CpuUsageSnapshot out = {.window_us=77};
    Perf_UpdateCpuCounters(0, 1000000);
    Perf_UpdateCpuCounters(1000001, 2000000);
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FINISHED, Perf_GetCpuUsage(&out));
    TEST_ASSERT_EQUAL(77, out.window_us);
    Perf_UpdateCpuCounters(2000001, 3000000);
    TEST_ASSERT_EQUAL(ESP_OK, Perf_GetCpuUsage(&out));
    Perf_UpdateCpuCounters(0, 3000000ULL + UINT32_MAX + 1ULL);
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FINISHED, Perf_GetCpuUsage(&out));
    Perf_UpdateCpuCounters(0, 2);
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FINISHED, Perf_GetCpuUsage(&out));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, Perf_GetCpuUsage(NULL));
}

static char perfOutput[4096];
static int capture_performance(int (*handler)(int, char **), int argc, char **argv) {
    fflush(stdout);
    FILE *capture = tmpfile(); TEST_ASSERT_NOT_NULL(capture);
    int saved = perf_capture_dup(perf_capture_fileno(stdout)); TEST_ASSERT_TRUE(saved >= 0);
    TEST_ASSERT_TRUE(perf_capture_dup2(perf_capture_fileno(capture), perf_capture_fileno(stdout)) >= 0);
    int result = handler(argc, argv); fflush(stdout);
    TEST_ASSERT_TRUE(perf_capture_dup2(saved, perf_capture_fileno(stdout)) >= 0);
    perf_capture_close(saved); rewind(capture);
    size_t length = fread(perfOutput, 1, sizeof(perfOutput)-1, capture);
    perfOutput[length] = 0; fclose(capture); return result;
}
void test_PerformanceCLIStableMachineAndImmediateHumanLanguage(void) {
    const LocalizationCatalog *baseline = Localization_Snapshot();
    TEST_ASSERT_EQUAL(ESP_OK, Perf_Init());
    char *status[] = {"perf_status", "--machine"};
    for (unsigned language = 0; language < 2; ++language) {
        TEST_ASSERT_TRUE(Localization_Apply(language ? "en" : "tr"));
        TEST_ASSERT_EQUAL(0, capture_performance(cmd_perf_status, 2, status));
        TEST_ASSERT_EQUAL_STRING("RACOOL_CLI v=1 type=performance_status state=1\n", perfOutput);
        TEST_ASSERT_EQUAL(0, capture_performance(cmd_perf_status, 1, status));
        TEST_ASSERT_NOT_NULL(strstr(perfOutput, language ? "PerformanceMonitor Status" : "Performans İzleme Durumu"));
    }
    char *start[] = {"perf_start", "--machine"};
    TEST_ASSERT_EQUAL(0, capture_performance(cmd_perf_start, 2, start));
    TEST_ASSERT_EQUAL_STRING("RACOOL_CLI v=1 type=performance_start result=applied\n", perfOutput);
    char *stop[] = {"perf_stop", "--machine"};
    TEST_ASSERT_NOT_EQUAL(0, capture_performance(cmd_perf_stop, 2, stop));
    TEST_ASSERT_EQUAL_STRING("RACOOL_CLI v=1 type=performance_stop result=rejected\n", perfOutput);
    TEST_ASSERT_TRUE(Localization_Apply(baseline->code));
}
