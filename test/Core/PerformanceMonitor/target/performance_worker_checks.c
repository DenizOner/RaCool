#include "unity.h"
#include "esp_system.h"
static uint32_t blocked_heap_read(void);
#define esp_get_free_heap_size blocked_heap_read
#include "../../../../src/Core/PerformanceMonitor/PerformanceMonitor.c"
#undef esp_get_free_heap_size
#include "../../../../src/Core/PerformanceMonitor/PerformanceMonitorConfig.c"

static SemaphoreHandle_t heapRelease;
static atomic_bool heapEntered;
static bool started;
static uint32_t blocked_heap_read(void) {
    atomic_store(&heapEntered, true);
    xSemaphoreTake(heapRelease, portMAX_DELAY);
    xSemaphoreGive(heapRelease);
    return 12345;
}
void target_performance_worker_cleanup(void) {
    if (heapRelease) xSemaphoreGive(heapRelease);
    if (started) { TEST_ASSERT_EQUAL(0, Perf_Service_Stop(NULL)); started = false; }
    if (heapRelease) { vSemaphoreDelete(heapRelease); heapRelease = NULL; }
}
void target_performance_worker_verify_shutdown(void) {
    TEST_ASSERT_EQUAL(ESP_OK, Perf_Init());
    heapRelease = xSemaphoreCreateBinary(); TEST_ASSERT_NOT_NULL(heapRelease);
    atomic_store(&heapEntered, false);
    TEST_ASSERT_EQUAL(0, Perf_Service_Start(NULL)); started = true;
    for (unsigned i = 0; i < 100 && !atomic_load(&heapEntered); ++i) vTaskDelay(pdMS_TO_TICKS(10));
    TEST_ASSERT_TRUE(atomic_load(&heapEntered));
    TaskHandle_t task = atomic_load(&monitorWorker.task);
    TEST_ASSERT_EQUAL(-1, Perf_Service_Stop(NULL));
    TEST_ASSERT_EQUAL_PTR(task, atomic_load(&monitorWorker.task));
    TEST_ASSERT_EQUAL(-1, Perf_Service_Start(NULL));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, Perf_InitializeWithConfig(PerformanceMonitorConfig_GetDefault()));
    xSemaphoreGive(heapRelease);
    TEST_ASSERT_EQUAL(0, Perf_Service_Stop(NULL)); started = false;
    TEST_ASSERT_NULL(atomic_load(&monitorWorker.task));
    TEST_ASSERT_EQUAL(0, Perf_Service_Start(NULL)); started = true;
    TEST_ASSERT_EQUAL(0, Perf_Service_Stop(NULL)); started = false;
}
