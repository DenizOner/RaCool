#include "unity.h"
#define ServiceLocator_GetService DiagTarget_GetService
#define GetEventBus DiagTarget_GetEventBus
#include "../../../../src/Management/DiagnosticsManager/DiagnosticsManager.c"
#include "../../../../src/Management/DiagnosticsManager/DiagnosticsManagerConfig.c"
#undef ServiceLocator_GetService
#undef GetEventBus

static SemaphoreHandle_t callbackRelease;
static atomic_bool callbackEntered;
static bool initialized;
IService *DiagTarget_GetService(const char *name) { (void)name; return NULL; }
IEventBus *DiagTarget_GetEventBus(void) { return NULL; }
static bool BlockedCheck(void *context) {
    if (context != callbackRelease) return false;
    atomic_store(&callbackEntered, true);
    xSemaphoreTake(callbackRelease, portMAX_DELAY);
    return true;
}
void target_diagnostics_worker_cleanup(void) {
    if (callbackRelease) xSemaphoreGive(callbackRelease);
    if (initialized) {
        TEST_ASSERT_EQUAL(DIAG_OK, Diag_Stop(&diagManager.base.base));
        initialized = false;
    }
    if (callbackRelease) { vSemaphoreDelete(callbackRelease); callbackRelease = NULL; }
}
void target_diagnostics_worker_verify_shutdown(void) {
    IDiagnosticsManager *dm = DiagnosticsManager_GetModule();
    DiagnosticsManagerConfig config = DiagnosticsManagerConfig_GetDefault();
    config.criticalServiceCount = 0;
    config.enablePeriodicTest = false;
    TEST_ASSERT_EQUAL(DIAG_OK, dm->InitializeWithConfig(dm, config, NULL));
    TEST_ASSERT_EQUAL(DIAG_HEALTH_UNKNOWN, dm->GetHealthStatus(dm));
    callbackRelease = xSemaphoreCreateBinary();
    TEST_ASSERT_NOT_NULL(callbackRelease);
    atomic_store(&callbackEntered, false);
    TEST_ASSERT_EQUAL(DIAG_OK, dm->RegisterCustomCheck(dm, "Blocked", BlockedCheck, callbackRelease));
    TEST_ASSERT_EQUAL(DIAG_OK, dm->base.Start(&dm->base));
    initialized = true;
    for (unsigned i = 0; i < 100 && !atomic_load(&callbackEntered); ++i) vTaskDelay(pdMS_TO_TICKS(10));
    TEST_ASSERT_TRUE(atomic_load(&callbackEntered));
    TaskHandle_t worker = diagManager.taskHandle;
    TEST_ASSERT_NOT_NULL(worker);
    TEST_ASSERT_EQUAL(DIAG_ERR_BUSY, dm->base.Stop(&dm->base));
    TEST_ASSERT_EQUAL_PTR(worker, diagManager.taskHandle);
    TEST_ASSERT_EQUAL_PTR(callbackRelease, diagManager.customChecks[0].user_data);
    TEST_ASSERT_EQUAL(DIAG_ERR_INVALID_STATE, dm->StartAsyncSelfTest(dm));
    TEST_ASSERT_EQUAL(DIAG_ERR_INVALID_STATE, dm->base.Start(&dm->base));
    TEST_ASSERT_EQUAL(DIAG_ERR_BUSY, dm->InitializeWithConfig(dm, config, NULL));
    xSemaphoreGive(callbackRelease);
    TEST_ASSERT_EQUAL(DIAG_OK, dm->base.Stop(&dm->base));
    initialized = false;
    TEST_ASSERT_NULL(diagManager.taskHandle);
    TEST_ASSERT_EQUAL(0, diagManager.customCheckCount);
    TEST_ASSERT_EQUAL(DIAG_OK, dm->base.Start(&dm->base));
    initialized = true;
    TEST_ASSERT_EQUAL(DIAG_OK, dm->base.Stop(&dm->base));
    initialized = false;
}
