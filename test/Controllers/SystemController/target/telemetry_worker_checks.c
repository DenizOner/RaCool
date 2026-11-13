#include "unity.h"
#define ServiceLocator_GetService TelemetryTarget_GetService
#define ServiceLocator_GetEventBus TelemetryTarget_GetEventBus
#include "../../../../src/Controllers/SystemController/SystemController_Reporting.c"
#undef ServiceLocator_GetService
#undef ServiceLocator_GetEventBus

static SemaphoreHandle_t publish_release;
static atomic_bool publish_entered;
static atomic_uint publish_count;
static bool initialized;
static int32_t blocked_publish(BusEvent event) {
    (void)event;
    atomic_fetch_add(&publish_count, 1);
    atomic_store(&publish_entered, true);
    xSemaphoreTake(publish_release, portMAX_DELAY);
    xSemaphoreGive(publish_release);
    return 0;
}
static IEventBus target_bus = {.Publish = blocked_publish};
IService *TelemetryTarget_GetService(const char *name) { (void)name; return NULL; }
IEventBus *TelemetryTarget_GetEventBus(void) { return &target_bus; }
void target_telemetry_worker_cleanup(void) {
    if (publish_release) xSemaphoreGive(publish_release);
    if (initialized) TEST_ASSERT_EQUAL(0, Telemetry_Stop(&telemetryInstance.interface.base));
    if (publish_release) {
        vSemaphoreDelete(publish_release);
        publish_release = NULL;
    }
}
void target_telemetry_worker_verify_shutdown(void) {
    ISystemTelemetry *telemetry = GetSystemTelemetry();
    TEST_ASSERT_EQUAL(0, telemetry->base.Initialize(&telemetry->base, NULL));
    initialized = true;
    publish_release = xSemaphoreCreateBinary();
    TEST_ASSERT_NOT_NULL(publish_release);
    atomic_store(&publish_entered, false);
    atomic_store(&publish_count, 0);
    TEST_ASSERT_EQUAL(0, telemetry->base.Start(&telemetry->base));
    telemetry->TriggerImmediateReport(telemetry);
    for (unsigned i = 0; i < 100 && !atomic_load(&publish_entered); ++i)
        vTaskDelay(pdMS_TO_TICKS(10));
    TEST_ASSERT_TRUE(atomic_load(&publish_entered));
    TaskHandle_t task = atomic_load(&telemetryInstance.worker.task);
    TEST_ASSERT_NOT_EQUAL(0, telemetry->base.Stop(&telemetry->base));
    TEST_ASSERT_EQUAL_PTR(task, atomic_load(&telemetryInstance.worker.task));
    TEST_ASSERT_NOT_EQUAL(0, telemetry->base.Start(&telemetry->base));
    telemetry->TriggerImmediateReport(telemetry);
    xSemaphoreGive(publish_release);
    TEST_ASSERT_EQUAL(0, telemetry->base.Stop(&telemetry->base));
    unsigned count = atomic_load(&publish_count);
    vTaskDelay(pdMS_TO_TICKS(100));
    TEST_ASSERT_EQUAL(count, atomic_load(&publish_count));
    TEST_ASSERT_EQUAL(0, telemetry->base.Start(&telemetry->base));
    vTaskDelay(pdMS_TO_TICKS(100));
    TEST_ASSERT_EQUAL(count, atomic_load(&publish_count));
    TEST_ASSERT_EQUAL(0, telemetry->base.Stop(&telemetry->base));
}
