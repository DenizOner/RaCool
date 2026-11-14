#include "unity.h"
#include "Infrastructure/support/test_mocks.h"
#include "Management/DiagnosticsManager/IDiagnosticsManager.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// Include the implementation to inspect lifecycle state.
#include "Management/DiagnosticsManager/DiagnosticsManager.c"
#include "Management/DiagnosticsManager/DiagnosticsManagerConsoleCommands.c"

static bool sample_custom_check_pass(void *user_data) {
    return true;
}

static bool sample_custom_check_fail(void *user_data) {
    return false;
}

void setUp_DiagnosticsManager(void) {
  RESET_FAKE(mocked_xSemaphoreCreateMutexStatic);
  RESET_FAKE(mocked_xSemaphoreTake);
  RESET_FAKE(mocked_xSemaphoreGive);
  RESET_FAKE(mocked_xTaskCreate);
  RESET_FAKE(mocked_ServiceLocator_GetService);
  RESET_FAKE(mocked_GetEventBus);
  RESET_FAKE(mocked_xTaskGetCurrentTaskHandle);
  RESET_FAKE(mocked_eTaskGetState);
  RESET_FAKE(mocked_vTaskDelete);
  RESET_FAKE(mocked_vTaskDelay);
  RESET_FAKE(mocked_xTaskGetTickCount);
  RESET_FAKE(mocked_esp_timer_get_time);
  RESET_FAKE(mocked_esp_get_free_heap_size);
  RESET_FAKE(mocked_heap_caps_get_largest_free_block);
  RESET_FAKE(mocked_vTaskSuspend);
  mocked_xSemaphoreCreateMutexStatic_fake.return_val = (SemaphoreHandle_t)0x100;
  mocked_xSemaphoreTake_fake.return_val = pdTRUE;
  mocked_xTaskCreate_fake.return_val = pdPASS;
  IDiagnosticsManager *dm = DiagnosticsManager_GetModule();
  IDiagnosticsManager interface = *dm;
  memset(&diagManager, 0, sizeof(diagManager));
  diagManager.base = interface;
  TEST_ASSERT_EQUAL(DIAG_OK, dm->base.Initialize(&dm->base, NULL));
  diagManager.state = SERVICE_STATE_UNINITIALIZED;
}

void tearDown_DiagnosticsManager(void) {}

/**
 * @brief Test Lifecycle
 */
void test_DiagnosticsManager_Lifecycle(void) {
  setUp_DiagnosticsManager();
  IDiagnosticsManager *dm = DiagnosticsManager_GetModule();
  IService *service = &(dm->base);

  TEST_ASSERT_EQUAL(DIAG_OK, service->Initialize(service, NULL));
  TEST_ASSERT_EQUAL(SERVICE_STATE_INITIALIZED, diagManager.state);

  TEST_ASSERT_EQUAL(0, service->Start(service));
  TEST_ASSERT_EQUAL(SERVICE_STATE_STARTED, diagManager.state);

  TEST_ASSERT_EQUAL(0, service->CheckHealth(service));

  TEST_ASSERT_EQUAL(0, service->Stop(service));
  TEST_ASSERT_EQUAL(SERVICE_STATE_STOPPED, diagManager.state);
}

/**
 * @brief Test Async Self-Test Trigger
 */
void test_DiagnosticsManager_AsyncTrigger(void) {
  setUp_DiagnosticsManager();
  IDiagnosticsManager *dm = DiagnosticsManager_GetModule();
  
  // Fail if not started
  TEST_ASSERT_EQUAL(DIAG_ERR_INVALID_STATE, dm->StartAsyncSelfTest(dm));

  // Started
  diagManager.state = SERVICE_STATE_STARTED;
  
  int32_t res = dm->StartAsyncSelfTest(dm);
  TEST_ASSERT_EQUAL(DIAG_OK, res);
  TEST_ASSERT_EQUAL(0, mocked_xTaskCreate_fake.call_count);
  TEST_ASSERT_TRUE(diagManager.requested);
  TEST_ASSERT_EQUAL(DIAG_ERR_BUSY, dm->StartAsyncSelfTest(dm));
}

/**
 * @brief Test Results Retrieval
 */
void test_DiagnosticsManager_Results(void) {
  setUp_DiagnosticsManager();
  IDiagnosticsManager *dm = DiagnosticsManager_GetModule();
  
  DiagnosticsTestResult res = dm->GetLastTestResult(dm);
  TEST_ASSERT_FALSE(res.is_running);
  TEST_ASSERT_EQUAL(0, res.health_score);
  TEST_ASSERT_EQUAL(DIAG_HEALTH_UNKNOWN, dm->GetHealthStatus(dm));
}

/**
 * @brief Test Health Score Retrieval
 */
void test_DiagnosticsManager_HealthScore(void) {
  setUp_DiagnosticsManager();
  IDiagnosticsManager *dm = DiagnosticsManager_GetModule();

  diagManager.lastResult.completed = true;
  diagManager.lastResult.services_checked = 1;
  diagManager.lastResult.health_score = 85;
  TEST_ASSERT_EQUAL(85, dm->GetSystemHealthScore(dm));
  TEST_ASSERT_EQUAL(DIAG_HEALTH_HEALTHY, dm->GetHealthStatus(dm));

  diagManager.lastResult.health_score = 45;
  TEST_ASSERT_EQUAL(DIAG_HEALTH_CRITICAL, dm->GetHealthStatus(dm));
}

/**
 * @brief Test Custom Plugin Health Check Registration
 */
void test_DiagnosticsManager_CustomCheckRegistration(void) {
  setUp_DiagnosticsManager();
  IDiagnosticsManager *dm = DiagnosticsManager_GetModule();

  TEST_ASSERT_EQUAL(DIAG_ERR_INVALID_ARG, dm->RegisterCustomCheck(dm, NULL, sample_custom_check_pass, NULL));
  TEST_ASSERT_EQUAL(DIAG_ERR_INVALID_ARG, dm->RegisterCustomCheck(dm, "Check", NULL, NULL));

  TEST_ASSERT_EQUAL(DIAG_OK, dm->RegisterCustomCheck(dm, "CheckPass", sample_custom_check_pass, NULL));
  TEST_ASSERT_EQUAL(1, diagManager.customCheckCount);

  TEST_ASSERT_EQUAL(DIAG_OK, dm->RegisterCustomCheck(dm, "CheckFail", sample_custom_check_fail, NULL));
  TEST_ASSERT_EQUAL(2, diagManager.customCheckCount);
}

/**
 * @brief Test Detailed Metrics and Periodic Configuration
 */
void test_DiagnosticsManager_MetricsAndPeriodic(void) {
  setUp_DiagnosticsManager();
  IDiagnosticsManager *dm = DiagnosticsManager_GetModule();

  DiagnosticsDetailedMetrics metrics = dm->GetDetailedMetrics(dm);
  TEST_ASSERT_EQUAL(0, metrics.health_score);

  TEST_ASSERT_EQUAL(DIAG_OK, dm->SetPeriodicTest(dm, true, 30000));
  TEST_ASSERT_TRUE(diagManager.periodicRunning);
  TEST_ASSERT_EQUAL(30000, diagManager.config.periodicIntervalMs);
}

static TickType_t testTicks;
static unsigned workerPolls;
static int busUnsubscribeResult;
static bool locked;
static unsigned publishesUnderLock;
static unsigned criticalPublications;

static TickType_t ReadTicks(void) { return testTicks; }
static void AdvanceTicks(TickType_t ticks) { testTicks += ticks; }
static int32_t SubscribeOK(IEventBus *bus, const char *name, BusEventHandler handler) {
    (void)bus; (void)name; (void)handler; return 0;
}
static int32_t UnsubscribeResult(IEventBus *bus, const char *name, BusEventHandler handler) {
    (void)bus; (void)name; (void)handler; return busUnsubscribeResult;
}
static IEventBus testBus = {.Subscribe = SubscribeOK, .Unsubscribe = UnsubscribeResult};

void test_DiagnosticsManager_CreationFailureRollsBackSubscription(void) {
    setUp_DiagnosticsManager();
    IDiagnosticsManager *dm = DiagnosticsManager_GetModule();
    TEST_ASSERT_EQUAL(0, dm->base.Initialize(&dm->base, NULL));
    mocked_GetEventBus_fake.return_val = &testBus;
    mocked_xTaskCreate_fake.return_val = pdFAIL;
    busUnsubscribeResult = 0;
    TEST_ASSERT_EQUAL(DIAG_ERR_NO_MEM, dm->base.Start(&dm->base));
    TEST_ASSERT_FALSE(diagManager.subscribed);
    TEST_ASSERT_NULL(diagManager.taskHandle);
    TEST_ASSERT_EQUAL(SERVICE_STATE_INITIALIZED, dm->base.GetState(&dm->base));
    TEST_ASSERT_EQUAL(DIAG_ERR_INVALID_STATE, dm->StartAsyncSelfTest(dm));
}

void test_DiagnosticsManager_StopTimeoutRetainsWorkerAndContext(void) {
    setUp_DiagnosticsManager();
    IDiagnosticsManager *dm = DiagnosticsManager_GetModule();
    diagManager.state = SERVICE_STATE_STARTED;
    diagManager.taskHandle = (TaskHandle_t)0x200;
    TEST_ASSERT_EQUAL(0, dm->RegisterCustomCheck(dm, "Owned", sample_custom_check_pass, (void *)0x300));
    testTicks = 0;
    mocked_xTaskGetTickCount_fake.custom_fake = ReadTicks;
    mocked_vTaskDelay_fake.custom_fake = AdvanceTicks;
    TEST_ASSERT_EQUAL(DIAG_ERR_BUSY, dm->base.Stop(&dm->base));
    TEST_ASSERT_EQUAL_PTR((TaskHandle_t)0x200, diagManager.taskHandle);
    TEST_ASSERT_EQUAL_PTR((void *)0x300, diagManager.customChecks[0].user_data);
    TEST_ASSERT_EQUAL(0, mocked_vTaskDelete_fake.call_count);
    TEST_ASSERT_EQUAL(DIAG_ERR_INVALID_STATE, dm->StartAsyncSelfTest(dm));
    TEST_ASSERT_EQUAL(DIAG_ERR_INVALID_STATE, dm->base.Start(&dm->base));
    TEST_ASSERT_EQUAL(DIAG_ERR_BUSY, dm->InitializeWithConfig(dm, DiagnosticsManagerConfig_GetDefault(), NULL));
    atomic_store(&diagManager.exited, true);
    mocked_eTaskGetState_fake.return_val = eSuspended;
    TEST_ASSERT_EQUAL(0, dm->base.Stop(&dm->base));
    TEST_ASSERT_NULL(diagManager.taskHandle);
    TEST_ASSERT_EQUAL(0, diagManager.customCheckCount);
    TEST_ASSERT_EQUAL(1, mocked_vTaskDelete_fake.call_count);
}

void test_DiagnosticsManager_StopPreservesCapturedEventCallback(void) {
    setUp_DiagnosticsManager();
    IDiagnosticsManager *dm = DiagnosticsManager_GetModule();
    diagManager.eventBus = &testBus;
    diagManager.subscribed = true;
    busUnsubscribeResult = EVENT_BUS_ERR_BUSY;
    TEST_ASSERT_EQUAL(DIAG_ERR_BUSY, dm->base.Stop(&dm->base));
    TEST_ASSERT_TRUE(diagManager.subscribed);
    TEST_ASSERT_FALSE(atomic_load(&diagManager.stopRequested));
    busUnsubscribeResult = 0;
    TEST_ASSERT_EQUAL(0, dm->base.Stop(&dm->base));
    TEST_ASSERT_FALSE(diagManager.subscribed);
}

void test_DiagnosticsManager_NamesAndResultsAreOwned(void) {
    setUp_DiagnosticsManager();
    IDiagnosticsManager *dm = DiagnosticsManager_GetModule();
    char name[] = "Mutable";
    TEST_ASSERT_EQUAL(0, dm->RegisterCustomCheck(dm, name, sample_custom_check_pass, NULL));
    name[0] = 'X';
    TEST_ASSERT_EQUAL_STRING("Mutable", diagManager.customChecks[0].name);
    TEST_ASSERT_EQUAL(0, dm->RegisterCustomCheck(dm, "Mutable", sample_custom_check_pass, NULL));
    TEST_ASSERT_EQUAL(1, diagManager.customCheckCount);
    TEST_ASSERT_EQUAL(DIAG_ERR_BUSY, dm->RegisterCustomCheck(dm, "Mutable", sample_custom_check_fail, NULL));
    strcpy(diagManager.lastResult.last_failed_service, "Failure");
    DiagnosticsTestResult result = dm->GetLastTestResult(dm);
    diagManager.lastResult.last_failed_service[0] = 'X';
    TEST_ASSERT_EQUAL_STRING("Failure", result.last_failed_service);
}

void test_DiagnosticsManager_ValidationAndLockFailure(void) {
    setUp_DiagnosticsManager();
    IDiagnosticsManager *dm = DiagnosticsManager_GetModule();
    DiagnosticsManagerConfig config = DiagnosticsManagerConfig_GetDefault();
    config.criticalServiceCount = DIAGNOSTICS_MAX_CRITICAL_SERVICES + 1;
    TEST_ASSERT_EQUAL(DIAG_ERR_INVALID_ARG, dm->InitializeWithConfig(dm, config, NULL));
    TEST_ASSERT_EQUAL(DIAG_ERR_INVALID_ARG, dm->SetPeriodicTest(dm, true, 1));
    char longName[DIAGNOSTICS_NAME_CAPACITY + 1];
    memset(longName, 'x', sizeof(longName)); longName[sizeof(longName) - 1] = 0;
    TEST_ASSERT_EQUAL(DIAG_ERR_INVALID_ARG, dm->RegisterCustomCheck(dm, longName, sample_custom_check_pass, NULL));
    mocked_xSemaphoreTake_fake.return_val = pdFALSE;
    TEST_ASSERT_EQUAL(DIAG_ERR_BUSY, dm->RegisterCustomCheck(dm, "Safe", sample_custom_check_pass, NULL));
    TEST_ASSERT_EQUAL(DIAG_ERR_BUSY, dm->SetPeriodicTest(dm, true, 1000));
    TEST_ASSERT_EQUAL(DIAG_HEALTH_UNKNOWN, dm->GetHealthStatus(dm));
}

void test_DiagnosticsManager_ThermalCallbackNeverBlocks(void) {
    setUp_DiagnosticsManager();
    RESET_FAKE(mocked_vTaskDelay);
    float temperature = 96.0f;
    BusEvent event = {.type = EVENT_TYPE_ERROR, .data = &temperature, .dataSize = sizeof(temperature)};
    OnThermalAlert(event);
    TEST_ASSERT_TRUE(atomic_load(&diagManager.emergency));
    TEST_ASSERT_EQUAL(0, mocked_vTaskDelay_fake.call_count);
    IDiagnosticsManager *dm = DiagnosticsManager_GetModule();
    TEST_ASSERT_EQUAL(DIAG_ERR_BUSY, dm->base.Stop(&dm->base));
}

static BaseType_t TrackTake(SemaphoreHandle_t mutex, TickType_t ticks) {
    (void)mutex; (void)ticks; locked = true; return pdTRUE;
}
static BaseType_t TrackGive(SemaphoreHandle_t mutex) { (void)mutex; locked = false; return pdTRUE; }
static int32_t TrackPublish(BusEvent event) {
    (void)event;
    if (locked) ++publishesUnderLock;
    if (strcmp(event.eventName, EVENT_DIAG_HEALTH_CRITICAL) == 0 || strcmp(event.eventName, EVENT_DIAG_HEALTH_CHANGED) == 0) ++criticalPublications;
    return 0;
}
void test_DiagnosticsManager_PublicationsRunOutsideMutex(void) {
    setUp_DiagnosticsManager();
    diagManager.config.criticalServiceCount = 0;
    diagManager.config.minFreeHeapThreshold = UINT32_MAX;
    diagManager.lastResult.is_running = true;
    mocked_xSemaphoreTake_fake.custom_fake = TrackTake;
    mocked_xSemaphoreGive_fake.custom_fake = TrackGive;
    testBus.Publish = TrackPublish;
    mocked_GetEventBus_fake.return_val = &testBus;
    locked = false; publishesUnderLock = criticalPublications = 0;
    RunScan(false);
    TEST_ASSERT_EQUAL(0, publishesUnderLock);
    TEST_ASSERT_EQUAL(0, criticalPublications);
    TEST_ASSERT_EQUAL(0, diagManager.lastResult.health_score);
    TEST_ASSERT_TRUE(diagManager.lastResult.completed);
    TEST_ASSERT_EQUAL(DIAG_HEALTH_UNKNOWN, DiagnosticsManager_GetModule()->GetHealthStatus(DiagnosticsManager_GetModule()));
}

static int32_t Healthy(IService *service) { (void)service; return 0; }
static int32_t Unhealthy(IService *service) { (void)service; return -1; }
static IService readinessService = {.CheckHealth = Healthy};
static void FinishOnePoll(TickType_t ticks) {
    testTicks += ticks;
    if (++workerPolls >= 1) { atomic_store(&diagManager.closing, true); atomic_store(&diagManager.stopRequested, true); }
}
void test_DiagnosticsManager_InitialScanWaitsForActualReadiness(void) {
    setUp_DiagnosticsManager();
    diagManager.config.criticalServiceCount = 1;
    diagManager.config.criticalServices[0] = SVC_NAME_ZIGBEE;
    diagManager.initialPending = true;
    readinessService.CheckHealth = Unhealthy;
    mocked_ServiceLocator_GetService_fake.return_val = &readinessService;
    mocked_xTaskGetTickCount_fake.custom_fake = ReadTicks;
    mocked_vTaskDelay_fake.custom_fake = FinishOnePoll;
    workerPolls = 0; testTicks = 0;
    DiagnosticWorker(NULL);
    TEST_ASSERT_FALSE(diagManager.lastResult.completed);
    TEST_ASSERT_EQUAL(0, diagManager.lastResult.services_checked);
    TEST_ASSERT_TRUE(atomic_load(&diagManager.exited));
}

static void FinishScan(TickType_t ticks) {
    testTicks += ticks;
    if (diagManager.lastResult.completed) { atomic_store(&diagManager.closing, true); atomic_store(&diagManager.stopRequested, true); }
}
void test_DiagnosticsManager_ReadyInitialScanUsesSameWorker(void) {
    setUp_DiagnosticsManager();
    diagManager.config.criticalServiceCount = 1;
    diagManager.config.criticalServices[0] = SVC_NAME_ZIGBEE;
    diagManager.initialPending = true;
    readinessService.CheckHealth = Healthy;
    mocked_ServiceLocator_GetService_fake.return_val = &readinessService;
    mocked_xTaskGetTickCount_fake.custom_fake = ReadTicks;
    mocked_vTaskDelay_fake.custom_fake = FinishScan;
    testTicks = 0;
    DiagnosticWorker(NULL);
    TEST_ASSERT_TRUE(diagManager.lastResult.completed);
    TEST_ASSERT_EQUAL(1, diagManager.lastResult.services_checked);
    TEST_ASSERT_EQUAL(100, diagManager.lastResult.health_score);
    TEST_ASSERT_EQUAL(0, mocked_xTaskCreate_fake.call_count);
}

static void FinishPeriodicEvidence(TickType_t ticks) {
    testTicks += ticks;
    if (diagManager.lastResult.completed || testTicks > pdMS_TO_TICKS(40000)) {
        atomic_store(&diagManager.stopRequested, true);
    }
}

static void RecoverZigbeeAndFinish(TickType_t ticks) {
    testTicks += ticks;
    if (testTicks >= pdMS_TO_TICKS(1500)) readinessService.CheckHealth = Healthy;
    if (diagManager.lastResult.completed || testTicks > pdMS_TO_TICKS(40000)) {
        atomic_store(&diagManager.stopRequested, true);
    }
}

static void ConfigureUnhealthyPeriodicZigbee(void) {
    diagManager.config.criticalServiceCount = 1;
    diagManager.config.criticalServices[0] = SVC_NAME_ZIGBEE;
    diagManager.config.periodicIntervalMs = 1000;
    diagManager.periodicRunning = true;
    diagManager.initialPending = false;
    readinessService.CheckHealth = Unhealthy;
    mocked_ServiceLocator_GetService_fake.return_val = &readinessService;
    mocked_xTaskGetTickCount_fake.custom_fake = ReadTicks;
    /* A healthy heap must not be misclassified by default-zero test mocks. */
    mocked_esp_get_free_heap_size_fake.return_val = UINT32_MAX;
    mocked_heap_caps_get_largest_free_block_fake.return_val = UINT32_MAX;
    testTicks = 0;
}

void test_DiagnosticsManager_PeriodicScanDefersTransientZigbeeFailure(void) {
    setUp_DiagnosticsManager();
    ConfigureUnhealthyPeriodicZigbee();
    mocked_vTaskDelay_fake.custom_fake = RecoverZigbeeAndFinish;
    DiagnosticWorker(NULL);
    TEST_ASSERT_TRUE(diagManager.lastResult.completed);
    TEST_ASSERT_EQUAL(1, diagManager.lastResult.services_checked);
    TEST_ASSERT_EQUAL(100, diagManager.lastResult.health_score);
    TEST_ASSERT_GREATER_OR_EQUAL(pdMS_TO_TICKS(1500), testTicks);
    TEST_ASSERT_LESS_THAN(pdMS_TO_TICKS(4000), testTicks);
}

void test_DiagnosticsManager_PeriodicScanReportsPersistentZigbeeFailure(void) {
    setUp_DiagnosticsManager();
    ConfigureUnhealthyPeriodicZigbee();
    mocked_vTaskDelay_fake.custom_fake = FinishPeriodicEvidence;
    DiagnosticWorker(NULL);
    TEST_ASSERT_TRUE(diagManager.lastResult.completed);
    TEST_ASSERT_EQUAL(1, diagManager.lastResult.services_checked);
    TEST_ASSERT_EQUAL(0, diagManager.lastResult.health_score);
    TEST_ASSERT_EQUAL_STRING(SVC_NAME_ZIGBEE, diagManager.lastResult.last_failed_service);
    TEST_ASSERT_GREATER_OR_EQUAL(pdMS_TO_TICKS(1000 + DIAGNOSTICS_ZIGBEE_RECOVERY_GRACE_MS), testTicks);
    TEST_ASSERT_LESS_THAN(pdMS_TO_TICKS(40000), testTicks);
}

/* A real heap warning and a transient Zigbee sample are independent:
 * report memory pressure immediately, but do not invent/emit a partial score. */
static unsigned heapWarningEvents, completedScanEvents, healthChangeEvents;
static uint32_t lastHeapWarningFreeBytes;
static int32_t CaptureIndependentHealthEvents(BusEvent event) {
    if (strcmp(event.eventName, EVENT_DIAG_HEAP_WARNING) == 0) {
        ++heapWarningEvents;
        TEST_ASSERT_EQUAL(sizeof(uint32_t), event.dataSize);
        memcpy(&lastHeapWarningFreeBytes, event.data, sizeof(lastHeapWarningFreeBytes));
    } else if (strcmp(event.eventName, EVENT_DIAG_SELFTEST_FINISHED) == 0) {
        ++completedScanEvents;
    } else if (strcmp(event.eventName, EVENT_DIAG_HEALTH_CHANGED) == 0) {
        ++healthChangeEvents;
    }
    return 0;
}
static void ConfigureHeapWarningDuringZigbeeGrace(void) {
    ConfigureUnhealthyPeriodicZigbee();
    mocked_esp_get_free_heap_size_fake.return_val = 45916;
    testBus.Publish = CaptureIndependentHealthEvents;
    mocked_GetEventBus_fake.return_val = &testBus;
    heapWarningEvents = completedScanEvents = healthChangeEvents = 0;
    lastHeapWarningFreeBytes = 0;
}
void test_DiagnosticsManager_HeapWarningDoesNotBypassZigbeeGrace(void) {
    setUp_DiagnosticsManager();
    ConfigureHeapWarningDuringZigbeeGrace();
    TEST_ASSERT_FALSE(RunScan(true));
    TEST_ASSERT_TRUE(atomic_load(&diagManager.periodicZigbeePending));
    TEST_ASSERT_TRUE(diagManager.scanPending);
    TEST_ASSERT_FALSE(diagManager.lastResult.completed);
    TEST_ASSERT_EQUAL(1, heapWarningEvents);
    TEST_ASSERT_EQUAL(45916, lastHeapWarningFreeBytes);
    TEST_ASSERT_EQUAL(0, completedScanEvents);
    TEST_ASSERT_EQUAL(0, healthChangeEvents);
    readinessService.CheckHealth = Healthy;
    TEST_ASSERT_TRUE(CompletePeriodicZigbee(pdMS_TO_TICKS(500), false));
    TEST_ASSERT_TRUE(diagManager.lastResult.completed);
    TEST_ASSERT_EQUAL(100, diagManager.lastResult.health_score);
    TEST_ASSERT_EQUAL(1, heapWarningEvents);
    TEST_ASSERT_EQUAL(1, completedScanEvents);
}
void test_DiagnosticsManager_PersistentZigbeeFailureKeepsHeapWarningAndRealScore(void) {
    setUp_DiagnosticsManager();
    ConfigureHeapWarningDuringZigbeeGrace();
    TEST_ASSERT_FALSE(RunScan(true));
    TEST_ASSERT_EQUAL(1, heapWarningEvents);
    TEST_ASSERT_EQUAL(0, completedScanEvents);
    TEST_ASSERT_FALSE(CompletePeriodicZigbee(pdMS_TO_TICKS(500), false));
    TEST_ASSERT_EQUAL(0, completedScanEvents);
    TEST_ASSERT_TRUE(CompletePeriodicZigbee(
        pdMS_TO_TICKS(DIAGNOSTICS_ZIGBEE_RECOVERY_GRACE_MS), false));
    TEST_ASSERT_EQUAL(0, diagManager.lastResult.health_score);
    TEST_ASSERT_EQUAL_STRING(SVC_NAME_ZIGBEE, diagManager.lastResult.last_failed_service);
    TEST_ASSERT_EQUAL(1, completedScanEvents);
    TEST_ASSERT_EQUAL(1, heapWarningEvents);
}
void test_DiagnosticsManager_OtherFailureWithHeapWarningIsImmediate(void) {
    setUp_DiagnosticsManager();
    ConfigureHeapWarningDuringZigbeeGrace();
    diagManager.customCheckCount = 1;
    diagManager.customChecks[0] = (DiagnosticsCustomCheckItem){
        .name = "RealFailure", .check_fn = sample_custom_check_fail
    };
    TEST_ASSERT_TRUE(RunScan(true));
    TEST_ASSERT_FALSE(atomic_load(&diagManager.periodicZigbeePending));
    TEST_ASSERT_TRUE(diagManager.lastResult.completed);
    TEST_ASSERT_EQUAL(0, diagManager.lastResult.health_score);
    TEST_ASSERT_EQUAL_STRING(SVC_NAME_ZIGBEE, diagManager.lastResult.last_failed_service);
    TEST_ASSERT_EQUAL(1, heapWarningEvents);
    TEST_ASSERT_EQUAL(1, completedScanEvents);
}
void test_DiagnosticsManager_LargestFreeBlockWarningStillIndependent(void) {
    setUp_DiagnosticsManager();
    ConfigureHeapWarningDuringZigbeeGrace();
    mocked_esp_get_free_heap_size_fake.return_val = UINT32_MAX;
    mocked_heap_caps_get_largest_free_block_fake.return_val = 8192;
    TEST_ASSERT_FALSE(RunScan(true));
    TEST_ASSERT_TRUE(atomic_load(&diagManager.periodicZigbeePending));
    TEST_ASSERT_FALSE(diagManager.lastResult.completed);
    TEST_ASSERT_EQUAL(1, heapWarningEvents);
    TEST_ASSERT_EQUAL(UINT32_MAX, lastHeapWarningFreeBytes);
    readinessService.CheckHealth = Healthy;
    TEST_ASSERT_TRUE(CompletePeriodicZigbee(pdMS_TO_TICKS(500), false));
    TEST_ASSERT_EQUAL(1, heapWarningEvents);
    TEST_ASSERT_EQUAL(100, diagManager.lastResult.health_score);
}

void test_DiagnosticsManager_ExplicitScanIsNotDeferred(void) {
    setUp_DiagnosticsManager();
    ConfigureUnhealthyPeriodicZigbee();
    diagManager.periodicRunning = false;
    diagManager.requested = true;
    mocked_vTaskDelay_fake.custom_fake = FinishPeriodicEvidence;
    DiagnosticWorker(NULL);
    TEST_ASSERT_TRUE(diagManager.lastResult.completed);
    TEST_ASSERT_EQUAL(0, diagManager.lastResult.health_score);
    TEST_ASSERT_LESS_THAN(pdMS_TO_TICKS(1000), testTicks);
}

static TickType_t otherCheckAt;
static bool RecordHealthyOther(void *unused) {
    (void)unused;
    otherCheckAt = testTicks;
    return true;
}
static bool RestartDuringOtherCheck(void *unused) {
    (void)unused;
    readinessService.CheckHealth = Unhealthy;
    return true;
}

void test_DiagnosticsManager_PeriodicChecksOthersBeforeZigbeeGrace(void) {
    setUp_DiagnosticsManager();
    ConfigureUnhealthyPeriodicZigbee();
    diagManager.customCheckCount = 1;
    diagManager.customChecks[0] = (DiagnosticsCustomCheckItem){
        .name = "Other", .check_fn = RecordHealthyOther
    };
    otherCheckAt = pdMS_TO_TICKS(40000);
    TEST_ASSERT_FALSE(RunScan(true));
    TEST_ASSERT_EQUAL(0, otherCheckAt);
    TEST_ASSERT_TRUE(atomic_load(&diagManager.periodicZigbeePending));
    TEST_ASSERT_TRUE(diagManager.scanPending);
    TEST_ASSERT_FALSE(diagManager.lastResult.completed);
    TEST_ASSERT_EQUAL(2, diagManager.pendingResult.services_checked);
    TEST_ASSERT_EQUAL(1, diagManager.pendingResult.services_healthy);
    TEST_ASSERT_FALSE(CompletePeriodicZigbee(pdMS_TO_TICKS(300), false));
    TEST_ASSERT_FALSE(diagManager.lastResult.completed);
    readinessService.CheckHealth = Healthy;
    TEST_ASSERT_TRUE(CompletePeriodicZigbee(pdMS_TO_TICKS(500), false));
    TEST_ASSERT_TRUE(diagManager.lastResult.completed);
    TEST_ASSERT_EQUAL(2, diagManager.lastResult.services_checked);
    TEST_ASSERT_EQUAL(2, diagManager.lastResult.services_healthy);
    TEST_ASSERT_EQUAL(100, diagManager.lastResult.health_score);
    TEST_ASSERT_EQUAL_STRING("", diagManager.lastResult.last_failed_service);
}

void test_DiagnosticsManager_PersistentOtherFailureNeverGetsHidden(void) {
    setUp_DiagnosticsManager();
    ConfigureUnhealthyPeriodicZigbee();
    diagManager.customCheckCount = 1;
    diagManager.customChecks[0] = (DiagnosticsCustomCheckItem){
        .name = "Other", .check_fn = sample_custom_check_fail
    };
    TEST_ASSERT_TRUE(RunScan(true));
    TEST_ASSERT_FALSE(atomic_load(&diagManager.periodicZigbeePending));
    TEST_ASSERT_TRUE(diagManager.lastResult.completed);
    TEST_ASSERT_EQUAL(2, diagManager.lastResult.services_checked);
    TEST_ASSERT_EQUAL(0, diagManager.lastResult.health_score);
}

void test_DiagnosticsManager_ZigbeeRestartDuringOtherScanUsesGrace(void) {
    setUp_DiagnosticsManager();
    ConfigureUnhealthyPeriodicZigbee();
    readinessService.CheckHealth = Healthy;
    diagManager.customCheckCount = 1;
    diagManager.customChecks[0] = (DiagnosticsCustomCheckItem){
        .name = "Restart", .check_fn = RestartDuringOtherCheck
    };
    TEST_ASSERT_FALSE(RunScan(true));
    TEST_ASSERT_TRUE(atomic_load(&diagManager.periodicZigbeePending));
    readinessService.CheckHealth = Healthy;
    TEST_ASSERT_TRUE(CompletePeriodicZigbee(pdMS_TO_TICKS(500), false));
    TEST_ASSERT_EQUAL(100, diagManager.lastResult.health_score);
}

void test_DiagnosticsManager_ManualRequestInterruptsPendingGrace(void) {
    setUp_DiagnosticsManager();
    ConfigureUnhealthyPeriodicZigbee();
    TEST_ASSERT_FALSE(RunScan(true));
    diagManager.state = SERVICE_STATE_STARTED;
    IDiagnosticsManager *dm = DiagnosticsManager_GetModule();
    TEST_ASSERT_EQUAL(DIAG_OK, dm->StartAsyncSelfTest(dm));
    TEST_ASSERT_EQUAL(DIAG_ERR_BUSY, dm->StartAsyncSelfTest(dm));
    TEST_ASSERT_TRUE(diagManager.requested);
    TEST_ASSERT_TRUE(atomic_load(&diagManager.periodicZigbeePending));
    TEST_ASSERT_TRUE(CompletePeriodicZigbee(pdMS_TO_TICKS(100), true));
    TEST_ASSERT_FALSE(atomic_load(&diagManager.periodicZigbeePending));
    TEST_ASSERT_TRUE(diagManager.lastResult.completed);
    TEST_ASSERT_EQUAL(0, diagManager.lastResult.health_score);
    TEST_ASSERT_TRUE(diagManager.requested);
    TEST_ASSERT_TRUE(RunScan(false));
    TEST_ASSERT_FALSE(diagManager.requested);
    TEST_ASSERT_EQUAL(0, diagManager.lastResult.health_score);
}

void test_DiagnosticsManager_PeriodicGraceIsWrapSafe(void) {
    setUp_DiagnosticsManager();
    ConfigureUnhealthyPeriodicZigbee();
    testTicks = (TickType_t)(~(TickType_t)0 - PositiveTicks(1000));
    TickType_t started = testTicks;
    TEST_ASSERT_FALSE(RunScan(true));
    TEST_ASSERT_FALSE(CompletePeriodicZigbee(
        (TickType_t)(started + PositiveTicks(2000)), false));
    TEST_ASSERT_FALSE(diagManager.lastResult.completed);
    TEST_ASSERT_TRUE(CompletePeriodicZigbee(
        (TickType_t)(started + PositiveTicks(DIAGNOSTICS_ZIGBEE_RECOVERY_GRACE_MS)), false));
    TEST_ASSERT_FALSE(atomic_load(&diagManager.periodicZigbeePending));
    TEST_ASSERT_EQUAL(0, diagManager.lastResult.health_score);
    TEST_ASSERT_EQUAL_STRING(SVC_NAME_ZIGBEE, diagManager.lastResult.last_failed_service);
}

static void ThermalGracePoll(TickType_t ticks) {
    testTicks += ticks;
    if (testTicks >= pdMS_TO_TICKS(1200)) atomic_store(&diagManager.stopRequested, true);
}
void test_DiagnosticsManager_ThermalEmergencyNotDelayedByZigbeeGrace(void) {
    setUp_DiagnosticsManager();
    ConfigureUnhealthyPeriodicZigbee();
    TEST_ASSERT_FALSE(RunScan(true));
    RESET_FAKE(mocked_esp_restart);
    mocked_vTaskDelay_fake.custom_fake = ThermalGracePoll;
    atomic_store(&diagManager.emergency, true);
    DiagnosticWorker(NULL);
    TEST_ASSERT_GREATER_THAN(0, mocked_esp_restart_fake.call_count);
    TEST_ASSERT_TRUE(atomic_load(&diagManager.exited));
}


void test_DiagnosticsManager_LateThermalCallbackKeepsWorkerAlive(void) {
    setUp_DiagnosticsManager();
    IDiagnosticsManager *dm = DiagnosticsManager_GetModule();
    diagManager.eventBus = &testBus;
    diagManager.subscribed = true;
    busUnsubscribeResult = EVENT_BUS_ERR_BUSY;
    TEST_ASSERT_EQUAL(DIAG_ERR_BUSY, dm->base.Stop(&dm->base));
    TEST_ASSERT_TRUE(atomic_load(&diagManager.closing));
    TEST_ASSERT_FALSE(atomic_load(&diagManager.stopRequested));
    float temperature = 96.0f;
    OnThermalAlert((BusEvent){.type = EVENT_TYPE_ERROR, .data = &temperature, .dataSize = sizeof(temperature)});
    busUnsubscribeResult = 0;
    TEST_ASSERT_EQUAL(DIAG_ERR_BUSY, dm->base.Stop(&dm->base));
    TEST_ASSERT_FALSE(atomic_load(&diagManager.stopRequested));
    TEST_ASSERT_TRUE(atomic_load(&diagManager.emergency));
}

void test_DiagnosticsManager_ReadinessTimeoutReportsRealFailure(void) {
    setUp_DiagnosticsManager();
    diagManager.config.criticalServiceCount = 1;
    diagManager.config.criticalServices[0] = SVC_NAME_ZIGBEE;
    diagManager.initialPending = true;
    readinessService.CheckHealth = Unhealthy;
    mocked_ServiceLocator_GetService_fake.return_val = &readinessService;
    mocked_xTaskGetTickCount_fake.custom_fake = ReadTicks;
    mocked_vTaskDelay_fake.custom_fake = FinishScan;
    testTicks = 0;
    DiagnosticWorker(NULL);
    TEST_ASSERT_TRUE(diagManager.lastResult.completed);
    TEST_ASSERT_EQUAL(1, diagManager.lastResult.services_checked);
    TEST_ASSERT_EQUAL(0, diagManager.lastResult.health_score);
    TEST_ASSERT_GREATER_OR_EQUAL(DIAGNOSTICS_STARTUP_READY_TIMEOUT_MS, testTicks);
    TEST_ASSERT_EQUAL_STRING(SVC_NAME_ZIGBEE, diagManager.lastResult.last_failed_service);
}

void test_DiagnosticsManager_ScanAdmissionLockFailureRetainsRequestAndResult(void) {
    setUp_DiagnosticsManager();
    diagManager.requested = true;
    diagManager.initialPending = true;
    diagManager.lastResult.completed = true;
    diagManager.lastResult.health_score = 85;
    DiagnosticsTestResult before = diagManager.lastResult;
    unsigned gives = mocked_xSemaphoreGive_fake.call_count;
    mocked_xSemaphoreTake_fake.return_val = pdFALSE;
    TEST_ASSERT_FALSE(RunScan(false));
    TEST_ASSERT_EQUAL_MEMORY(&before, &diagManager.lastResult, sizeof(before));
    TEST_ASSERT_TRUE(diagManager.requested);
    TEST_ASSERT_TRUE(diagManager.initialPending);
    TEST_ASSERT_FALSE(diagManager.scanPending);
    TEST_ASSERT_EQUAL(gives, mocked_xSemaphoreGive_fake.call_count);
    TEST_ASSERT_TRUE(mocked_xSemaphoreTake_fake.arg1_val > 0);
    TEST_ASSERT_TRUE(mocked_xSemaphoreTake_fake.arg1_val < portMAX_DELAY);
}

static unsigned completedCallbacks;
static bool complete_and_block_result_commit(void *context) {
    (void)context;
    ++completedCallbacks;
    mocked_xSemaphoreTake_fake.return_val = pdFALSE;
    return true;
}
void test_DiagnosticsManager_CommitContentionRetainsResultWithoutRepeatingCallback(void) {
    setUp_DiagnosticsManager();
    diagManager.config.criticalServiceCount = 0;
    diagManager.customCheckCount = 1;
    diagManager.customChecks[0] = (DiagnosticsCustomCheckItem){
        .name = "Commit contention", .check_fn = complete_and_block_result_commit};
    completedCallbacks = 0;
    TEST_ASSERT_FALSE(RunScan(false));
    TEST_ASSERT_TRUE(diagManager.scanPending);
    TEST_ASSERT_TRUE(diagManager.lastResult.is_running);
    TEST_ASSERT_FALSE(diagManager.lastResult.completed);
    TEST_ASSERT_EQUAL(1, completedCallbacks);
    TEST_ASSERT_FALSE(RunScan(false));
    TEST_ASSERT_EQUAL(1, completedCallbacks);
    mocked_xSemaphoreTake_fake.return_val = pdTRUE;
    TEST_ASSERT_TRUE(RunScan(false));
    TEST_ASSERT_EQUAL(1, completedCallbacks);
    TEST_ASSERT_FALSE(diagManager.scanPending);
    TEST_ASSERT_TRUE(diagManager.lastResult.completed);
    TEST_ASSERT_FALSE(diagManager.lastResult.is_running);
    TEST_ASSERT_EQUAL(1, diagManager.lastResult.services_checked);
    TEST_ASSERT_EQUAL(100, diagManager.lastResult.health_score);
}

static bool close_during_scan(void *context) {
    (void)context;
    atomic_store(&diagManager.closing, true);
    return true;
}
void test_DiagnosticsManager_ClosingScanNeverCommitsCompletedHealth(void) {
    setUp_DiagnosticsManager();
    diagManager.config.criticalServiceCount = 0;
    diagManager.customCheckCount = 1;
    diagManager.customChecks[0] = (DiagnosticsCustomCheckItem){
        .name = "Closing", .check_fn = close_during_scan};
    TEST_ASSERT_FALSE(RunScan(false));
    TEST_ASSERT_FALSE(diagManager.scanPending);
    TEST_ASSERT_FALSE(diagManager.lastResult.completed);
    TEST_ASSERT_EQUAL(0, diagManager.lastResult.services_checked);
    IDiagnosticsManager *dm = DiagnosticsManager_GetModule();
    TEST_ASSERT_EQUAL(DIAG_OK, dm->base.Stop(&dm->base));
    TEST_ASSERT_FALSE(diagManager.lastResult.is_running);
}

void test_DiagnosticsManager_StopDropsOnlyWorkerOwnedPendingResult(void) {
    setUp_DiagnosticsManager();
    diagManager.config.criticalServiceCount = 0;
    diagManager.customCheckCount = 1;
    diagManager.customChecks[0] = (DiagnosticsCustomCheckItem){
        .name = "Pending stop", .check_fn = complete_and_block_result_commit};
    TEST_ASSERT_FALSE(RunScan(false));
    TEST_ASSERT_TRUE(diagManager.scanPending);
    mocked_xSemaphoreTake_fake.return_val = pdTRUE;
    IDiagnosticsManager *dm = DiagnosticsManager_GetModule();
    TEST_ASSERT_EQUAL(DIAG_OK, dm->base.Stop(&dm->base));
    TEST_ASSERT_FALSE(diagManager.scanPending);
    TEST_ASSERT_FALSE(diagManager.lastResult.completed);
    TEST_ASSERT_FALSE(diagManager.lastResult.is_running);
    TEST_ASSERT_EQUAL(0, diagManager.customCheckCount);
}

void test_DiagnosticsConsole_InvalidArgumentsCannotAcquireLocksOrRequestScan(void) {
    setUp_DiagnosticsManager();
    char *nullCommand[] = {"diag", NULL};
    char *emptyCommand[] = {"diag", ""};
    char *extra[] = {"diag", "selftest", "unexpected"};
    char *nullName[] = {NULL};
    unsigned locks = mocked_xSemaphoreTake_fake.call_count;
    TEST_ASSERT_NOT_EQUAL(0, diag_console_cmd_handler(0, NULL));
    TEST_ASSERT_NOT_EQUAL(0, diag_console_cmd_handler(2, NULL));
    TEST_ASSERT_NOT_EQUAL(0, diag_console_cmd_handler(2, nullCommand));
    TEST_ASSERT_NOT_EQUAL(0, diag_console_cmd_handler(2, emptyCommand));
    TEST_ASSERT_NOT_EQUAL(0, diag_console_cmd_handler(3, extra));
    TEST_ASSERT_NOT_EQUAL(0, diag_console_cmd_handler(1, nullName));
    TEST_ASSERT_EQUAL(locks, mocked_xSemaphoreTake_fake.call_count);
    TEST_ASSERT_FALSE(diagManager.requested);
}

void test_DiagnosticsConsole_MissingCallbacksFailWithoutRequest(void) {
    setUp_DiagnosticsManager();
    IDiagnosticsManager *dm = DiagnosticsManager_GetModule();
    IDiagnosticsManager original = *dm;
    char *status[] = {"diag", "status"};
    char *selftest[] = {"diag", "selftest"};
    dm->GetDetailedMetrics = NULL;
    int statusResult = diag_console_cmd_handler(2, status);
    dm->StartAsyncSelfTest = NULL;
    int testResult = diag_console_cmd_handler(2, selftest);
    *dm = original;
    TEST_ASSERT_NOT_EQUAL(0, statusResult);
    TEST_ASSERT_NOT_EQUAL(0, testResult);
    TEST_ASSERT_FALSE(diagManager.requested);
}
