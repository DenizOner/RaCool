#include "unity.h"
#define ServiceLocator_GetService SystemTarget_GetService
#define GetSystemTelemetry SystemTarget_GetTelemetry
#include "../../../../src/Controllers/SystemController/SystemController.c"
#undef ServiceLocator_GetService
#undef GetSystemTelemetry
#include "../../../../src/Controllers/SystemController/SystemControllerConfig.c"
#include "../../../../src/Management/ConfigurationManager/ZigbeeReportingSettings.c"

static bool shutdown_enabled;
static IPowerManager shutdown_power;
IService *SystemTarget_GetService(const char *name) {
    return shutdown_enabled && !strcmp(name, SVC_NAME_POWER_MANAGER) ? (IService *)&shutdown_power : NULL;
}
ISystemTelemetry *SystemTarget_GetTelemetry(void) { return NULL; }
static SemaphoreHandle_t indicator_release;
static atomic_bool indicator_entered;
static bool initialized;
static esp_err_t blocked_indicator(IBuiltinLED *self, BuiltinLEDAnimationType type,
                                   BuiltinLEDPriority priority, uint32_t duration) {
    (void)self; (void)type; (void)priority; (void)duration;
    atomic_store(&indicator_entered, true);
    xSemaphoreTake(indicator_release, portMAX_DELAY);
    return ESP_OK;
}
static IBuiltinLED indicator = {.SetAnimation = blocked_indicator};
static atomic_bool shutdown_entered;
static TickType_t shutdown_entry_tick;
static int32_t blocked_shutdown(IPowerManager *self, uint64_t duration) {
    (void)self; (void)duration;
    shutdown_entry_tick = xTaskGetTickCount();
    atomic_store(&shutdown_entered, true);
    xSemaphoreTake(indicator_release, portMAX_DELAY);
    return POWER_ERR_TIMEOUT;
}
void target_system_worker_cleanup(void) {
    if (indicator_release) xSemaphoreGive(indicator_release);
    if (initialized) TEST_ASSERT_EQUAL(0, SC_Stop(&controllerInstance.base.base));
    controllerInstance.builtinLed = NULL;
    shutdown_enabled = false;
    if (indicator_release) {
        vSemaphoreDelete(indicator_release);
        indicator_release = NULL;
    }
}
void target_system_worker_verify_shutdown(void) {
    ISystemController *system = GetSystemController();
    TEST_ASSERT_EQUAL(0, system->base.Initialize(&system->base, NULL));
    initialized = true;
    indicator_release = xSemaphoreCreateBinary();
    TEST_ASSERT_NOT_NULL(indicator_release);
    atomic_store(&indicator_entered, false);
    TEST_ASSERT_EQUAL(0, system->base.Start(&system->base));
    controllerInstance.builtinLed = &indicator;
    system->HandleSystemEvent(system, 200);
    for (unsigned i = 0; i < 100 && !atomic_load(&indicator_entered); ++i)
        vTaskDelay(pdMS_TO_TICKS(10));
    TEST_ASSERT_TRUE(atomic_load(&indicator_entered));
    TaskHandle_t task = atomic_load(&controllerInstance.worker.task);
    QueueHandle_t queue = controllerInstance.eventQueue;
    TEST_ASSERT_NOT_EQUAL(0, system->base.Stop(&system->base));
    TEST_ASSERT_EQUAL_PTR(task, atomic_load(&controllerInstance.worker.task));
    TEST_ASSERT_EQUAL_PTR(queue, controllerInstance.eventQueue);
    system->HandleSystemEvent(system, 100);
    TEST_ASSERT_EQUAL(0, uxQueueMessagesWaiting(queue));
    TEST_ASSERT_NOT_EQUAL(0, system->base.Start(&system->base));
    xSemaphoreGive(indicator_release);
    TEST_ASSERT_EQUAL(0, system->base.Stop(&system->base));
    controllerInstance.builtinLed = NULL;
    TEST_ASSERT_EQUAL(0, system->base.Start(&system->base));
    TEST_ASSERT_EQUAL(0, system->base.CheckHealth(&system->base));
    shutdown_power.EnterDeepSleep = blocked_shutdown;
    shutdown_enabled = true;
    atomic_store(&shutdown_entered, false);
    TickType_t requested = xTaskGetTickCount();
    TEST_ASSERT_EQUAL(ESP_OK, system->RequestShutdown(system, 30));
    TEST_ASSERT_NOT_EQUAL(ESP_OK, system->RequestShutdown(system, 0));
    for (unsigned i = 0; i < 100 && !atomic_load(&shutdown_entered); ++i)
        vTaskDelay(pdMS_TO_TICKS(10));
    TEST_ASSERT_TRUE(atomic_load(&shutdown_entered));
    TEST_ASSERT_TRUE(shutdown_entry_tick - requested >= pdMS_TO_TICKS(30));
    task = atomic_load(&controllerInstance.worker.task);
    TEST_ASSERT_NOT_EQUAL(0, system->base.Stop(&system->base));
    TEST_ASSERT_EQUAL_PTR(task, atomic_load(&controllerInstance.worker.task));
    TEST_ASSERT_EQUAL_PTR(queue, controllerInstance.eventQueue);
    TEST_ASSERT_NOT_EQUAL(ESP_OK, system->RequestShutdown(system, 0));
    xSemaphoreGive(indicator_release);
    TEST_ASSERT_EQUAL(0, system->base.Stop(&system->base));
    TEST_ASSERT_FALSE(atomic_load(&controllerInstance.shutdownPending));
    shutdown_enabled = false;
    TEST_ASSERT_EQUAL(0, system->base.Start(&system->base));
    TEST_ASSERT_NOT_EQUAL(ESP_OK, system->RequestShutdown(system, 0));
    TEST_ASSERT_EQUAL(0, system->base.Stop(&system->base));
}
