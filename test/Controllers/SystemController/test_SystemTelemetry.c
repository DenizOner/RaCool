#include <unity.h>
#include <setjmp.h>
#include "Controllers/SystemController/SystemController_Reporting.c"

void setUp(void) {
    ISystemTelemetry interface = *GetSystemTelemetry();
    memset(&telemetryInstance, 0, sizeof(telemetryInstance));
    telemetryInstance.interface = interface;
    telemetryInstance.state = SERVICE_STATE_UNINITIALIZED;
    RESET_FAKE(mocked_xSemaphoreCreateMutex);
    RESET_FAKE(mocked_xSemaphoreTake);
    RESET_FAKE(mocked_xSemaphoreGive);
    RESET_FAKE(mocked_xQueueCreate);
    RESET_FAKE(mocked_xQueueReceive);
    RESET_FAKE(mocked_xTaskCreate);
    RESET_FAKE(mocked_xTaskGetCurrentTaskHandle);
    RESET_FAKE(mocked_xTaskGetTickCount);
    RESET_FAKE(mocked_xTaskNotifyGive);
    RESET_FAKE(mocked_vTaskDelete);
    RESET_FAKE(mocked_xTaskNotifyWait);
    RESET_FAKE(mocked_esp_timer_get_time);
    RESET_FAKE(mocked_ServiceLocator_GetEventBus);
    RESET_FAKE(mocked_ServiceLocator_GetService);
    mocked_xSemaphoreCreateMutex_fake.return_val = (void *)1;
    mocked_xSemaphoreTake_fake.return_val = pdTRUE;
    mocked_xQueueCreate_fake.return_val = (void *)2;
}
void tearDown(void) {}

void test_TelemetryInitializationFailureDoesNotPublishInitialized(void) {
    ISystemTelemetry *telemetry = GetSystemTelemetry();
    mocked_xSemaphoreCreateMutex_fake.return_val = NULL;
    TEST_ASSERT_NOT_EQUAL(0, telemetry->base.Initialize(&telemetry->base, NULL));
    TEST_ASSERT_EQUAL(SERVICE_STATE_UNINITIALIZED, telemetry->base.GetState(&telemetry->base));
    mocked_xSemaphoreCreateMutex_fake.return_val = (void *)1;
    TEST_ASSERT_EQUAL(0, telemetry->base.Initialize(&telemetry->base, NULL));
    TEST_ASSERT_EQUAL(SERVICE_STATE_INITIALIZED, telemetry->base.GetState(&telemetry->base));
}
void test_TelemetryTaskCreationFailureDoesNotPublishStarted(void) {
    ISystemTelemetry *telemetry = GetSystemTelemetry();
    TEST_ASSERT_EQUAL(0, telemetry->base.Initialize(&telemetry->base, NULL));
    mocked_xTaskCreate_fake.return_val = pdFALSE;
    TEST_ASSERT_NOT_EQUAL(0, telemetry->base.Start(&telemetry->base));
    TEST_ASSERT_EQUAL(SERVICE_STATE_INITIALIZED, telemetry->base.GetState(&telemetry->base));
}

static BaseType_t create_telemetry_task(TaskFunction_t fn, const char *name, uint32_t stack,
    void *arg, uint32_t priority, TaskHandle_t *task) {
    (void)fn; (void)name; (void)stack; (void)arg; (void)priority;
    *task = (void *)3; return pdPASS;
}
static BaseType_t telemetry_stop_ack(QueueHandle_t queue, void *output, TickType_t wait) {
    (void)wait;
    TEST_ASSERT_EQUAL_PTR(telemetryInstance.worker.stopped, queue);
    *(int32_t *)output = 0; return pdTRUE;
}
static ISystemTelemetry *start_telemetry(void) {
    ISystemTelemetry *telemetry = GetSystemTelemetry();
    TEST_ASSERT_EQUAL(0, telemetry->base.Initialize(&telemetry->base, NULL));
    mocked_xTaskCreate_fake.custom_fake = create_telemetry_task;
    mocked_xTaskGetCurrentTaskHandle_fake.return_val = (void *)4;
    TEST_ASSERT_EQUAL(0, telemetry->base.Start(&telemetry->base));
    return telemetry;
}
void test_TelemetryStopTimeoutPreservesWorkerRejectsLateNotifyAndRestarts(void) {
    ISystemTelemetry *telemetry = start_telemetry();
    telemetry->TriggerImmediateReport(telemetry);
    TEST_ASSERT_EQUAL(1, mocked_xTaskNotifyGive_fake.call_count);
    TEST_ASSERT_NOT_EQUAL(0, telemetry->base.Stop(&telemetry->base));
    TEST_ASSERT_EQUAL(SERVICE_STATE_ERROR, telemetry->base.GetState(&telemetry->base));
    TEST_ASSERT_EQUAL_PTR((void *)3, atomic_load(&telemetryInstance.worker.task));
    TEST_ASSERT_EQUAL(0, mocked_vTaskDelete_fake.call_count);
    telemetry->TriggerImmediateReport(telemetry);
    TEST_ASSERT_EQUAL(1, mocked_xTaskNotifyGive_fake.call_count);
    TEST_ASSERT_NOT_EQUAL(0, telemetry->base.Start(&telemetry->base));
    mocked_xQueueReceive_fake.custom_fake = telemetry_stop_ack;
    TEST_ASSERT_EQUAL(0, telemetry->base.Stop(&telemetry->base));
    TEST_ASSERT_EQUAL(SERVICE_STATE_STOPPED, telemetry->base.GetState(&telemetry->base));
    TEST_ASSERT_NULL(atomic_load(&telemetryInstance.worker.task));
    TEST_ASSERT_EQUAL_PTR((void *)3, mocked_vTaskDelete_fake.arg0_val);
    TEST_ASSERT_EQUAL(0, telemetry->base.Start(&telemetry->base));
    TEST_ASSERT_EQUAL(0, telemetry->base.CheckHealth(&telemetry->base));
}
void test_TelemetrySelfStopAndControlLockFailurePreserveStartedState(void) {
    ISystemTelemetry *telemetry = start_telemetry();
    mocked_xTaskGetCurrentTaskHandle_fake.return_val = (void *)3;
    TEST_ASSERT_NOT_EQUAL(0, telemetry->base.Stop(&telemetry->base));
    TEST_ASSERT_EQUAL(SERVICE_STATE_STARTED, telemetry->base.GetState(&telemetry->base));
    mocked_xTaskGetCurrentTaskHandle_fake.return_val = (void *)4;
    unsigned gives = mocked_xSemaphoreGive_fake.call_count;
    mocked_xSemaphoreTake_fake.return_val = pdFALSE;
    TEST_ASSERT_NOT_EQUAL(0, telemetry->base.Stop(&telemetry->base));
    TEST_ASSERT_EQUAL(gives, mocked_xSemaphoreGive_fake.call_count);
    telemetry->TriggerImmediateReport(telemetry);
    TEST_ASSERT_EQUAL(0, mocked_xTaskNotifyGive_fake.call_count);
}
void test_TelemetryQueueAllocationFailureCanRetry(void) {
    ISystemTelemetry *telemetry = GetSystemTelemetry();
    mocked_xQueueCreate_fake.return_val = NULL;
    TEST_ASSERT_NOT_EQUAL(0, telemetry->base.Initialize(&telemetry->base, NULL));
    TEST_ASSERT_NULL(telemetryInstance.controlMutex);
    TEST_ASSERT_NULL(telemetryInstance.worker.gate);
    mocked_xQueueCreate_fake.return_val = (void *)2;
    TEST_ASSERT_EQUAL(0, telemetry->base.Initialize(&telemetry->base, NULL));
}
void test_TelemetryIntervalAndRepeatedInitDoNotResetLiveConfiguration(void) {
    ISystemTelemetry *telemetry = start_telemetry();
    telemetry->SetReportingInterval(telemetry, 5000);
    TEST_ASSERT_EQUAL(5000, telemetry->GetReportingInterval(telemetry));
    TEST_ASSERT_EQUAL(0, telemetry->base.Initialize(&telemetry->base, NULL));
    TEST_ASSERT_EQUAL(5000, telemetry->GetReportingInterval(telemetry));
    TEST_ASSERT_EQUAL(SERVICE_STATE_STARTED, telemetry->base.GetState(&telemetry->base));
    telemetry->SetReportingInterval(telemetry, 0);
    TEST_ASSERT_EQUAL(TELEMETRY_MIN_MS, telemetry->GetReportingInterval(telemetry));
}
static jmp_buf telemetry_loop_exit;
static unsigned waits, publications;
static BaseType_t advance_telemetry_clock(uint32_t enter, uint32_t leave, uint32_t *value, TickType_t ticks) {
    (void)enter; (void)leave; (void)value;
    TEST_ASSERT_TRUE(ticks > 0 && ticks <= pdMS_TO_TICKS(SYSTEM_TELEMETRY_WORKER_POLL_MS));
    if (++waits == 4) longjmp(telemetry_loop_exit, 1);
    mocked_esp_timer_get_time_fake.return_val = waits == 1 ? 500000 : waits == 2 ? 1000000 : 1010000;
    return waits == 3 ? pdTRUE : pdFALSE;
}
static int32_t count_telemetry_event(BusEvent event) {
    TEST_ASSERT_NOT_NULL(event.data);
    TEST_ASSERT_EQUAL(sizeof(uint32_t), event.dataSize);
    ++publications; return 0;
}
void test_TelemetryPollingDoesNotPublishEarlyAndManualRequestPublishes(void) {
    ISystemTelemetry *telemetry = start_telemetry();
    telemetry->SetReportingInterval(telemetry, 1000);
    IEventBus bus = {.Publish = count_telemetry_event};
    mocked_ServiceLocator_GetEventBus_fake.return_val = &bus;
    mocked_xTaskNotifyWait_fake.custom_fake = advance_telemetry_clock;
    waits = publications = 0;
    if (!setjmp(telemetry_loop_exit)) Telemetry_Task(&telemetryInstance);
    TEST_ASSERT_EQUAL(4, publications);
}

static DiagnosticsTestResult diagnosticResult;
static unsigned measuredHealthEvents;
static DiagnosticsTestResult ReadDiagnosticResult(IDiagnosticsManager *self) {
    (void)self; return diagnosticResult;
}
static int32_t CountHealthEvent(BusEvent event) {
    if (strcmp(event.eventName, "diag.health.updated") == 0) {
        TEST_ASSERT_EQUAL(sizeof(int32_t), event.dataSize);
        TEST_ASSERT_EQUAL(100, *(const int32_t *)event.data);
        ++measuredHealthEvents;
    }
    return 0;
}
void test_TelemetryDoesNotPublishUnmeasuredHealth(void) {
    IDiagnosticsManager diagnostics = {.GetLastTestResult = ReadDiagnosticResult};
    telemetryInstance.diagnostics = &diagnostics;
    IEventBus bus = {.Publish = CountHealthEvent};
    mocked_ServiceLocator_GetEventBus_fake.return_val = &bus;
    measuredHealthEvents = 0;
    diagnosticResult = (DiagnosticsTestResult){.health_score = 100};
    Telemetry_PublishMetrics(&telemetryInstance);
    diagnosticResult.completed = true;
    Telemetry_PublishMetrics(&telemetryInstance);
    diagnostics.GetLastTestResult = NULL;
    Telemetry_PublishMetrics(&telemetryInstance);
    TEST_ASSERT_EQUAL(0, measuredHealthEvents);
}
void test_TelemetryPublishesCompletedHealthSnapshot(void) {
    IDiagnosticsManager diagnostics = {.GetLastTestResult = ReadDiagnosticResult};
    telemetryInstance.diagnostics = &diagnostics;
    IEventBus bus = {.Publish = CountHealthEvent};
    mocked_ServiceLocator_GetEventBus_fake.return_val = &bus;
    diagnosticResult = (DiagnosticsTestResult){.completed = true, .services_checked = 9, .health_score = 100};
    measuredHealthEvents = 0;
    Telemetry_PublishMetrics(&telemetryInstance);
    TEST_ASSERT_EQUAL(1, measuredHealthEvents);
}

void test_TelemetryMissingPublisherOrClimateMethodDoesNotCrash(void) {
    IEventBus bus={0};IClimateController climate={0};
    telemetryInstance.climate=&climate;
    mocked_ServiceLocator_GetEventBus_fake.return_val=&bus;
    Telemetry_PublishMetrics(&telemetryInstance);
    bus.Publish=CountHealthEvent;measuredHealthEvents=0;
    Telemetry_PublishMetrics(&telemetryInstance);
    TEST_ASSERT_EQUAL(0,measuredHealthEvents);
}

void test_TelemetryZeroSuppressesPeriodicButAllowsExplicitReport(void) {
    ISystemTelemetry *telemetry = start_telemetry();
    telemetry->SetReportingInterval(telemetry, 0);
    TEST_ASSERT_EQUAL(0, telemetry->GetReportingInterval(telemetry));
    IEventBus bus = {.Publish = count_telemetry_event};
    mocked_ServiceLocator_GetEventBus_fake.return_val = &bus;
    mocked_xTaskNotifyWait_fake.custom_fake = advance_telemetry_clock;
    waits = publications = 0;
    if (!setjmp(telemetry_loop_exit)) Telemetry_Task(&telemetryInstance);
    TEST_ASSERT_EQUAL(2, publications);
    telemetry->SetReportingInterval(telemetry, 1);
    TEST_ASSERT_EQUAL(TELEMETRY_ACTIVE_MIN_MS, telemetry->GetReportingInterval(telemetry));
    telemetry->SetReportingInterval(telemetry, UINT32_MAX);
    TEST_ASSERT_EQUAL(TELEMETRY_MAX_MS, telemetry->GetReportingInterval(telemetry));
}
