#include "unity.h"
#define ServiceLocator_GetService OLEDTarget_GetService
#include "../../../../src/Hardware/OLED/OLED.c"
#define TAG OLEDTargetConfigTag
#include "../../../../src/Hardware/OLED/OLEDConfig.c"
#undef TAG
#undef ServiceLocator_GetService

/* Real LVGL/FreeRTOS worker and locks; no physical panel is attached here. */
static bool oled_initialized, lvgl_lock_held;
static unsigned released_leases;
static esp_err_t release_test_lease(II2CManager *self, const void *owner) {
    (void)self; (void)owner;
    released_leases++;
    return ESP_OK;
}
static II2CManager test_manager = {.ReleaseBus = release_test_lease};
IService *OLEDTarget_GetService(const char *name) { (void)name; return NULL; }

void target_oled_worker_cleanup(void) {
    if (lvgl_lock_held) {
        xSemaphoreGive(oledService.lvgl_mutex);
        lvgl_lock_held = false;
    }
    if (oled_initialized) TEST_ASSERT_EQUAL(0, OLED_Stop(&oledService.base.base));
}

void target_oled_worker_verify_shutdown(void) {
    IOLED *oled = OLED_GetModule();
    TEST_ASSERT_EQUAL(0, oled->base.Initialize(&oled->base, NULL));
    oled_initialized = true;
    if (!lv_is_initialized()) lv_init();
    oledService.lvgl_initialized = true;
    released_leases = 0;
    oledService.bus_manager = &test_manager;
    oledService.bus_acquired = true;
    TEST_ASSERT_EQUAL(pdTRUE, xSemaphoreTake(oledService.lvgl_mutex, pdMS_TO_TICKS(100)));
    lvgl_lock_held = true;
    oledService.state = SERVICE_STATE_STARTED;
    TEST_ASSERT_EQUAL(0, WatchdogWorker_Start(&oledService.worker, oled_lvgl_task,
        "oled_test", OLED_TASK_STACK_SIZE, NULL, OLED_TASK_PRIORITY));
    vTaskDelay(pdMS_TO_TICKS(50));
    TaskHandle_t worker = atomic_load(&oledService.worker.task);
    SemaphoreHandle_t mutex = oledService.lvgl_mutex;
    TEST_ASSERT_EQUAL(-1, oled->base.Stop(&oled->base));
    TEST_ASSERT_EQUAL_PTR(worker, atomic_load(&oledService.worker.task));
    TEST_ASSERT_EQUAL_PTR(mutex, oledService.lvgl_mutex);
    TEST_ASSERT_EQUAL(0, released_leases);
    TEST_ASSERT_TRUE(oledService.bus_acquired);
    xSemaphoreGive(oledService.lvgl_mutex);
    lvgl_lock_held = false;
    TEST_ASSERT_EQUAL(0, oled->base.Stop(&oled->base));
    TEST_ASSERT_NULL(atomic_load(&oledService.worker.task));
    TEST_ASSERT_EQUAL_PTR(mutex, oledService.lvgl_mutex);
    TEST_ASSERT_EQUAL(1, released_leases);
    TEST_ASSERT_FALSE(oledService.bus_acquired);
    oledService.state = SERVICE_STATE_STARTED;
    TEST_ASSERT_EQUAL(0, WatchdogWorker_Start(&oledService.worker, oled_lvgl_task,
        "oled_test", OLED_TASK_STACK_SIZE, NULL, OLED_TASK_PRIORITY));
    vTaskDelay(pdMS_TO_TICKS(30));
    TEST_ASSERT_EQUAL(0, oled->base.Stop(&oled->base));
}
