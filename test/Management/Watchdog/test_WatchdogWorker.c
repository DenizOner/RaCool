#include "test_mocks.h"
#include "unity.h"
#include "Management/Watchdog/WatchdogWorker.h"
#include <string.h>

static WatchdogWorker worker;
static IWatchdog watchdog;
static ServiceState state;
static int adds, removes, feeds, addResult, removeResult;
static int32_t ack;
static bool ackReady, timeoutOnly;
static ServiceState getState(IService *self) { (void)self; return state; }
static int32_t add(IWatchdog *self, void *task, uint32_t timeout) {
    (void)self; (void)timeout; TEST_ASSERT_NULL(task); adds++; return addResult;
}
static int32_t removeTask(IWatchdog *self, void *task) {
    (void)self; TEST_ASSERT_EQUAL_PTR((void *)3, task); removes++; return removeResult;
}
static void feed(IWatchdog *self) { (void)self; feeds++; }
static void body(void *arg) { (void)arg; }
static BaseType_t createTask(TaskFunction_t fn, const char *name, uint32_t stack,
                            void *arg, uint32_t priority, TaskHandle_t *task) {
    (void)fn; (void)name; (void)stack; (void)arg; (void)priority;
    *task = (TaskHandle_t)3; return pdPASS;
}
static BaseType_t sendAck(QueueHandle_t queue, const void *value, TickType_t wait) {
    (void)queue; (void)wait; ack = *(const int32_t *)value; ackReady = true; return pdTRUE;
}
static void checkpoint(uint64_t now) {
    mocked_xTaskGetCurrentTaskHandle_fake.return_val = (TaskHandle_t)3;
    WatchdogWorker_Checkpoint(&worker, &watchdog, now);
    mocked_xTaskGetCurrentTaskHandle_fake.return_val = (TaskHandle_t)4;
}
static BaseType_t receiveAck(QueueHandle_t queue, void *value, TickType_t wait) {
    (void)queue; (void)wait;
    if (timeoutOnly) return pdFALSE;
    if (!ackReady) checkpoint(2000);
    if (!ackReady) return pdFALSE;
    *(int32_t *)value = ack; ackReady = false; return pdTRUE;
}
void setUp(void) {
    memset(&worker, 0, sizeof(worker));
    memset(&watchdog, 0, sizeof(watchdog));
    watchdog.base.GetState = getState; watchdog.AddTask = add;
    watchdog.RemoveTask = removeTask; watchdog.Feed = feed;
    state = SERVICE_STATE_STARTED; adds = removes = feeds = addResult = removeResult = 0;
    ackReady = timeoutOnly = false;
    RESET_FAKE(mocked_xSemaphoreCreateMutex); RESET_FAKE(mocked_xSemaphoreTake);
    RESET_FAKE(mocked_xSemaphoreGive); RESET_FAKE(mocked_xQueueCreate);
    RESET_FAKE(mocked_xQueueSend); RESET_FAKE(mocked_xQueueReceive);
    RESET_FAKE(mocked_xTaskCreate); RESET_FAKE(mocked_vTaskDelete);
    RESET_FAKE(mocked_vTaskSuspend); RESET_FAKE(mocked_xTaskGetCurrentTaskHandle);
    RESET_FAKE(mocked_xTaskGetTickCount);
    mocked_xSemaphoreCreateMutex_fake.return_val = (SemaphoreHandle_t)1;
    mocked_xSemaphoreTake_fake.return_val = pdTRUE;
    mocked_xSemaphoreGive_fake.return_val = pdTRUE;
    mocked_xQueueCreate_fake.return_val = (QueueHandle_t)2;
    mocked_xQueueSend_fake.custom_fake = sendAck;
    mocked_xQueueReceive_fake.custom_fake = receiveAck;
    mocked_xTaskCreate_fake.custom_fake = createTask;
    mocked_xTaskGetCurrentTaskHandle_fake.return_val = (TaskHandle_t)4;
}
void tearDown(void) {}
static void start(void) {
    TEST_ASSERT_EQUAL(0, WatchdogWorker_Init(&worker));
    TEST_ASSERT_EQUAL(0, WatchdogWorker_Start(&worker, body, "test", 1024, NULL, 1));
}
void test_Worker_AllocationAndTaskFailure(void) {
    mocked_xSemaphoreCreateMutex_fake.return_val = NULL;
    TEST_ASSERT_EQUAL(-1, WatchdogWorker_Init(&worker));
    mocked_xSemaphoreCreateMutex_fake.return_val = (SemaphoreHandle_t)1;
    mocked_xQueueCreate_fake.return_val = NULL;
    TEST_ASSERT_EQUAL(-1, WatchdogWorker_Init(&worker));
    TEST_ASSERT_NULL(worker.gate);
    mocked_xQueueCreate_fake.return_val = (QueueHandle_t)2;
    TEST_ASSERT_EQUAL(0, WatchdogWorker_Init(&worker));
    mocked_xTaskCreate_fake.custom_fake = NULL;
    mocked_xTaskCreate_fake.return_val = pdFAIL;
    TEST_ASSERT_EQUAL(-1, WatchdogWorker_Start(&worker, body, "test", 1024, NULL, 1));
    TEST_ASSERT_NULL(atomic_load(&worker.task));
}
void test_Worker_RegistersBeforeFeedAndRetries(void) {
    start(); state = SERVICE_STATE_INITIALIZED;
    checkpoint(0); TEST_ASSERT_EQUAL(0, adds); TEST_ASSERT_EQUAL(0, feeds);
    TEST_ASSERT_FALSE(WatchdogWorker_Healthy(&worker));
    state = SERVICE_STATE_STARTED; addResult = -1;
    checkpoint(1000); TEST_ASSERT_EQUAL(1, adds); TEST_ASSERT_EQUAL(0, feeds);
    checkpoint(1001); TEST_ASSERT_EQUAL(1, adds);
    addResult = 0; checkpoint(2000);
    TEST_ASSERT_EQUAL(2, adds); TEST_ASSERT_EQUAL(1, feeds);
    TEST_ASSERT_TRUE(WatchdogWorker_Healthy(&worker));
}
void test_Worker_StopAcknowledgesBeforeDelete(void) {
    start(); checkpoint(0);
    TEST_ASSERT_EQUAL(0, WatchdogWorker_Stop(&worker, 100));
    TEST_ASSERT_EQUAL(1, removes);
    TEST_ASSERT_EQUAL(1, mocked_vTaskSuspend_fake.call_count);
    TEST_ASSERT_EQUAL(1, mocked_vTaskDelete_fake.call_count);
    TEST_ASSERT_NULL(atomic_load(&worker.task));
    TEST_ASSERT_FALSE(atomic_load(&worker.registered));
}
void test_Worker_StopTimeoutRetainsTaskAndConsumesLateAck(void) {
    start(); checkpoint(0); timeoutOnly = true;
    TEST_ASSERT_EQUAL(-1, WatchdogWorker_Stop(&worker, 100));
    TEST_ASSERT_EQUAL(0, mocked_vTaskDelete_fake.call_count);
    TEST_ASSERT_NOT_NULL(atomic_load(&worker.task));
    TEST_ASSERT_FALSE(WatchdogWorker_Healthy(&worker));
    checkpoint(2000); timeoutOnly = false;
    TEST_ASSERT_EQUAL(0, WatchdogWorker_Stop(&worker, 100));
    TEST_ASSERT_EQUAL(1, removes);
    TEST_ASSERT_EQUAL(1, mocked_vTaskDelete_fake.call_count);
}
void test_Worker_RemoveFailureKeepsTaskAndFeeds(void) {
    start(); checkpoint(0); removeResult = -1;
    TEST_ASSERT_EQUAL(-1, WatchdogWorker_Stop(&worker, 100));
    TEST_ASSERT_EQUAL(0, mocked_vTaskDelete_fake.call_count);
    TEST_ASSERT_TRUE(atomic_load(&worker.registered));
    TEST_ASSERT_EQUAL(2, feeds);
    removeResult = 0;
    TEST_ASSERT_EQUAL(0, WatchdogWorker_Stop(&worker, 100));
    TEST_ASSERT_EQUAL(2, removes);
}
void test_Worker_StopFromSelfAndLockFailureDoNotDelete(void) {
    start(); mocked_xTaskGetCurrentTaskHandle_fake.return_val = (TaskHandle_t)3;
    TEST_ASSERT_EQUAL(-1, WatchdogWorker_Stop(&worker, 100));
    mocked_xTaskGetCurrentTaskHandle_fake.return_val = (TaskHandle_t)4;
    mocked_xSemaphoreTake_fake.return_val = pdFALSE;
    TEST_ASSERT_EQUAL(-1, WatchdogWorker_Stop(&worker, 100));
    TEST_ASSERT_EQUAL(0, mocked_vTaskDelete_fake.call_count);
}

void test_Worker_FailedAckRetainsPendingStop(void) {
    start(); checkpoint(0); timeoutOnly = true;
    TEST_ASSERT_EQUAL(-1, WatchdogWorker_Stop(&worker, 100));
    mocked_xQueueSend_fake.custom_fake = NULL;
    mocked_xQueueSend_fake.return_val = pdFALSE;
    checkpoint(1000);
    TEST_ASSERT_TRUE(atomic_load(&worker.stopRequested));
    TEST_ASSERT_FALSE(atomic_load(&worker.registered));
    TEST_ASSERT_EQUAL(1, removes);
    TEST_ASSERT_EQUAL(0, mocked_vTaskDelete_fake.call_count);
    mocked_xQueueSend_fake.custom_fake = sendAck;
    checkpoint(2000); timeoutOnly = false;
    TEST_ASSERT_EQUAL(0, WatchdogWorker_Stop(&worker, 100));
    TEST_ASSERT_EQUAL(1, removes);
    TEST_ASSERT_EQUAL(1, mocked_vTaskDelete_fake.call_count);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_Worker_FailedAckRetainsPendingStop);
    RUN_TEST(test_Worker_AllocationAndTaskFailure);
    RUN_TEST(test_Worker_RegistersBeforeFeedAndRetries);
    RUN_TEST(test_Worker_StopAcknowledgesBeforeDelete);
    RUN_TEST(test_Worker_StopTimeoutRetainsTaskAndConsumesLateAck);
    RUN_TEST(test_Worker_RemoveFailureKeepsTaskAndFeeds);
    RUN_TEST(test_Worker_StopFromSelfAndLockFailureDoNotDelete);
    return UNITY_END();
}
