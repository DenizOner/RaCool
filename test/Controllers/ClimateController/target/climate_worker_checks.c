#include "unity.h"
#include <esp_err.h>
#define ServiceLocator_GetService ClimateTarget_GetService
#define ServiceLocator_GetEventBus ClimateTarget_GetEventBus
#define ConfigurationManager_GetInstance ClimateTarget_GetConfig
#define CLIMATE_CONSOLE_COMMANDS_H
struct IDebugConsole;
static esp_err_t ClimateCommands_Register(struct IDebugConsole *console) { (void)console; return ESP_OK; }
#include "../../../../src/Controllers/ClimateController/ClimateController.c"
#undef ServiceLocator_GetService
#undef ServiceLocator_GetEventBus
#undef ConfigurationManager_GetInstance
#include "../../../../src/Controllers/ClimateController/ClimateControllerConfig.c"

static SemaphoreHandle_t relay_release;
static atomic_bool relay_entered;
static bool initialized;
static SystemSettings settings;
static esp_err_t get_snapshot(IConfigurationManager *self, SystemSettings *out) {
    (void)self; *out = settings; return ESP_OK;
}
static IConfigurationManager config = {.GetSnapshot = get_snapshot};
static bool is_open(IRelay *self) { (void)self; return true; }
static esp_err_t blocked_close(IRelay *self) {
    (void)self;
    atomic_store(&relay_entered, true);
    xSemaphoreTake(relay_release, portMAX_DELAY);
    return ESP_OK;
}
static IRelay relay = {.IsOpen = is_open, .Close = blocked_close};
IService *ClimateTarget_GetService(const char *name) {
    return strcmp(name, SVC_NAME_RELAY) == 0 ? (IService *)&relay : NULL;
}
IEventBus *ClimateTarget_GetEventBus(void) { return NULL; }
IConfigurationManager *ClimateTarget_GetConfig(void) { return &config; }
void target_climate_worker_cleanup(void) {
    if (relay_release) xSemaphoreGive(relay_release);
    if (initialized) TEST_ASSERT_EQUAL(0, ClimateController_Stop(&climateControllerInstance.base.base));
    if (relay_release) { vSemaphoreDelete(relay_release); relay_release = NULL; }
}
void target_climate_worker_verify_shutdown(void) {
    IClimateController *climate = ClimateController_GetModule();
    TEST_ASSERT_EQUAL(0, climate->base.Initialize(&climate->base, NULL));
    initialized = true;
    relay_release = xSemaphoreCreateBinary();
    TEST_ASSERT_NOT_NULL(relay_release);
    atomic_store(&relay_entered, false);
    TEST_ASSERT_EQUAL(0, climate->base.Start(&climate->base));
    for (unsigned i = 0; i < 100 && !atomic_load(&relay_entered); ++i)
        vTaskDelay(pdMS_TO_TICKS(10));
    TEST_ASSERT_TRUE(atomic_load(&relay_entered));
    ClimateControllerConfig initial = climate->GetConfig(climate);
    ClimateControllerConfig changed = initial;
    changed.mainLoopDelayMs++;
    TEST_ASSERT_NOT_EQUAL(0, climate->InitializeWithConfig(climate, changed, NULL));
    TEST_ASSERT_EQUAL(initial.mainLoopDelayMs, climate->GetConfig(climate).mainLoopDelayMs);
    TaskHandle_t task = atomic_load(&climateControllerInstance.worker.task);
    TEST_ASSERT_NOT_EQUAL(0, climate->base.Stop(&climate->base));
    TEST_ASSERT_EQUAL_PTR(task, atomic_load(&climateControllerInstance.worker.task));
    TEST_ASSERT_NOT_EQUAL(0, climate->base.Start(&climate->base));
    xSemaphoreGive(relay_release);
    TEST_ASSERT_EQUAL(0, climate->base.Stop(&climate->base));
    TEST_ASSERT_NULL(atomic_load(&climateControllerInstance.worker.task));
    settings.system.system_enabled = true;
    settings.control.fan_manual_mode = true;
    settings.control.fan_manual_state = true;
    TEST_ASSERT_EQUAL(0, climate->base.Start(&climate->base));
    TEST_ASSERT_EQUAL(0, climate->base.Stop(&climate->base));
}
