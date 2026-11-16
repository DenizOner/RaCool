#include "Infrastructure/support/test_mocks.h"
#include "unity.h"
#include "Core/IService.h"
#include "Core/ServiceLocator/ServiceNames.h"
#include "Management/ConfigurationManager/IConfigurationManager.h"

#define CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE 1
#include "Core/SystemInitializer/OtaBootRecovery.c"

static esp_partition_t partition;
static bool havePartition, rollbackPossible, snapshotReadable;
static esp_err_t readStateResult, confirmResult, rollbackResult;
static esp_ota_img_states_t imageState;
static unsigned stateReads, confirmCalls, rollbackCalls, getSnapshotCalls;
static IService services[6];
static ServiceState serviceStates[6];
static bool watchdogHealthy, zigbeeBootReady;
static unsigned zigbeeBootChecks;
static IConfigurationManager configuration;
static const char *const serviceNames[] = {
    SVC_NAME_CONFIGURATION_MANAGER, SVC_NAME_RELAY,
    SVC_NAME_IR_TEMP_SENSOR, SVC_NAME_CLIMATE_CONTROLLER,
    SVC_NAME_ZIGBEE, SVC_NAME_WATCHDOG
};

const esp_partition_t *esp_ota_get_running_partition(void) {
    return havePartition ? &partition : NULL;
}
esp_err_t esp_ota_get_state_partition(const esp_partition_t *p, esp_ota_img_states_t *out) {
    TEST_ASSERT_EQUAL_PTR(&partition, p);
    stateReads++;
    if (readStateResult == ESP_OK) *out = imageState;
    return readStateResult;
}
bool esp_ota_check_rollback_is_possible(void) { return rollbackPossible; }
esp_err_t esp_ota_mark_app_valid_cancel_rollback(void) {
    confirmCalls++;
    return confirmResult;
}
esp_err_t esp_ota_mark_app_invalid_rollback_and_reboot(void) {
    rollbackCalls++;
    return rollbackResult;
}
static ServiceState currentState(IService *service) {
    ptrdiff_t index = service - services;
    TEST_ASSERT_TRUE(index >= 0 && index < 6);
    return serviceStates[index];
}
bool Zigbee_IsBootReady(const IService *service) {
    TEST_ASSERT_EQUAL_PTR(&services[4], service);
    zigbeeBootChecks++;
    return zigbeeBootReady;
}
static int32_t checkWatchdog(IService *service) {
    TEST_ASSERT_EQUAL_PTR(&services[5], service);
    return watchdogHealthy ? 0 : -1;
}
static IService *lookupService(const char *name) {
    for (unsigned i = 0; i < 6; ++i)
        if (!strcmp(name, serviceNames[i])) return &services[i];
    return NULL;
}
static esp_err_t getSnapshot(IConfigurationManager *self, SystemSettings *out) {
    TEST_ASSERT_EQUAL_PTR(&configuration, self);
    getSnapshotCalls++;
    memset(out, 0, sizeof(*out));
    return snapshotReadable ? ESP_OK : ESP_FAIL;
}
void setUp(void) {
    havePartition = rollbackPossible = snapshotReadable = watchdogHealthy = true;
    zigbeeBootReady = true;
    zigbeeBootChecks = 0;
    imageState = ESP_OTA_IMG_PENDING_VERIFY;
    readStateResult = confirmResult = rollbackResult = ESP_OK;
    stateReads = confirmCalls = rollbackCalls = getSnapshotCalls = 0;
    for (unsigned i = 0; i < 6; ++i) {
        services[i] = (IService){.GetState = currentState};
        serviceStates[i] = SERVICE_STATE_STARTED;
    }
    services[5].CheckHealth = checkWatchdog;
    configuration = (IConfigurationManager){.GetSnapshot = getSnapshot};
    RESET_FAKE(mocked_ConfigurationManager_GetInstance);
    mocked_ConfigurationManager_GetInstance_fake.return_val = &configuration;
    RESET_FAKE(mocked_ServiceLocator_GetService);
    mocked_ServiceLocator_GetService_fake.custom_fake = lookupService;
    RESET_FAKE(mocked_vTaskDelay);
}
void tearDown(void) {}

void test_OtaBoot_NonPendingDoesNotModifyBootState(void) {
    imageState = ESP_OTA_IMG_VALID;
    OtaBootRecovery_CompleteFirstBoot(true);
    TEST_ASSERT_EQUAL(1, stateReads);
    TEST_ASSERT_EQUAL(0, confirmCalls);
    TEST_ASSERT_EQUAL(0, rollbackCalls);
    TEST_ASSERT_EQUAL(0, getSnapshotCalls);
}
void test_OtaBoot_HealthyStartupConfirmsOnlyAfterTwoSnapshots(void) {
    OtaBootRecovery_CompleteFirstBoot(true);
    TEST_ASSERT_EQUAL(1, confirmCalls);
    TEST_ASSERT_EQUAL(0, rollbackCalls);
    TEST_ASSERT_EQUAL(2, getSnapshotCalls);
    TEST_ASSERT_EQUAL(1, mocked_vTaskDelay_fake.call_count);
}
void test_OtaBoot_InvalidStateReadNeverMutatesBoot(void) {
    readStateResult = ESP_FAIL;
    OtaBootRecovery_CompleteFirstBoot(true);
    TEST_ASSERT_EQUAL(0, confirmCalls);
    TEST_ASSERT_EQUAL(0, rollbackCalls);
    havePartition = false;
    OtaBootRecovery_CompleteFirstBoot(true);
    TEST_ASSERT_EQUAL(0, confirmCalls);
}
void test_OtaBoot_BadSettingsRollsBackOnlyWhenFallbackExists(void) {
    snapshotReadable = false;
    OtaBootRecovery_CompleteFirstBoot(true);
    TEST_ASSERT_EQUAL(0, confirmCalls);
    TEST_ASSERT_EQUAL(1, rollbackCalls);
    TEST_ASSERT_EQUAL(6, getSnapshotCalls);
    rollbackPossible = false; rollbackCalls = 0;
    OtaBootRecovery_CompleteFirstBoot(true);
    TEST_ASSERT_EQUAL(0, rollbackCalls);
}
static void loseZigbeeMainloopAfterFirstSample(TickType_t delay) {
    (void)delay;
    zigbeeBootReady = false;
}
void test_OtaBoot_StartedZigbeeWithoutSdkMainloopBlocksConfirmation(void) {
    /* The IService state and watchdog both look healthy. Only the SDK owner
     * knows that Zigbee's mainloop has exited or is closing. */
    serviceStates[4] = SERVICE_STATE_STARTED;
    watchdogHealthy = true;
    zigbeeBootReady = false;
    OtaBootRecovery_CompleteFirstBoot(true);
    TEST_ASSERT_EQUAL(0, confirmCalls);
    TEST_ASSERT_EQUAL(1, rollbackCalls);
    TEST_ASSERT_EQUAL(0, getSnapshotCalls);
    TEST_ASSERT_EQUAL(6, zigbeeBootChecks);
}
void test_OtaBoot_ZigbeeMainloopExitBetweenHealthySamplesPreventsConfirmation(void) {
    mocked_vTaskDelay_fake.custom_fake = loseZigbeeMainloopAfterFirstSample;
    OtaBootRecovery_CompleteFirstBoot(true);
    TEST_ASSERT_EQUAL(0, confirmCalls);
    TEST_ASSERT_EQUAL(1, rollbackCalls);
    TEST_ASSERT_EQUAL(1, getSnapshotCalls);
    TEST_ASSERT_EQUAL(6, zigbeeBootChecks);
    TEST_ASSERT_EQUAL(5, mocked_vTaskDelay_fake.call_count);
}
void test_OtaBoot_ZigbeeMainloopMissingWithoutFallbackStaysUnconfirmed(void) {
    zigbeeBootReady = false;
    rollbackPossible = false;
    OtaBootRecovery_CompleteFirstBoot(true);
    TEST_ASSERT_EQUAL(0, confirmCalls);
    TEST_ASSERT_EQUAL(0, rollbackCalls);
    TEST_ASSERT_EQUAL(6, zigbeeBootChecks);
}
void test_OtaBoot_WatchdogFailureBlocksConfirmation(void) {
    watchdogHealthy = false;
    OtaBootRecovery_CompleteFirstBoot(true);
    TEST_ASSERT_EQUAL(0, confirmCalls);
    TEST_ASSERT_EQUAL(1, rollbackCalls);
    TEST_ASSERT_EQUAL(0, getSnapshotCalls);
}
void test_OtaBoot_TransientSensorSampleDoesNotGateServices(void) {
    /* The contract checks sensor worker readiness, not a single MLX value or
     * Zigbee RF join; a transient sample therefore never enters this API. */
    OtaBootRecovery_CompleteFirstBoot(true);
    TEST_ASSERT_EQUAL(1, confirmCalls);
}
void test_OtaBoot_UnstartedRelayNeverConfirms(void) {
    serviceStates[1] = SERVICE_STATE_ERROR;
    OtaBootRecovery_CompleteFirstBoot(true);
    TEST_ASSERT_EQUAL(0, confirmCalls);
    TEST_ASSERT_EQUAL(1, rollbackCalls);
}
void test_OtaBoot_FailedStartupRequestsRollbackNotConfirmation(void) {
    OtaBootRecovery_CompleteFirstBoot(false);
    TEST_ASSERT_EQUAL(0, confirmCalls);
    TEST_ASSERT_EQUAL(1, rollbackCalls);
    TEST_ASSERT_EQUAL(0, getSnapshotCalls);
    TEST_ASSERT_EQUAL(0, mocked_vTaskDelay_fake.call_count);
}
void test_OtaBoot_ConfirmationFailureCannotBecomePass(void) {
    confirmResult = ESP_FAIL;
    OtaBootRecovery_CompleteFirstBoot(true);
    TEST_ASSERT_EQUAL(1, confirmCalls);
    TEST_ASSERT_EQUAL(1, rollbackCalls);
}

void test_OtaBootRecovery_All(void) {
    RUN_TEST(test_OtaBoot_NonPendingDoesNotModifyBootState);
    RUN_TEST(test_OtaBoot_HealthyStartupConfirmsOnlyAfterTwoSnapshots);
    RUN_TEST(test_OtaBoot_InvalidStateReadNeverMutatesBoot);
    RUN_TEST(test_OtaBoot_BadSettingsRollsBackOnlyWhenFallbackExists);
    RUN_TEST(test_OtaBoot_WatchdogFailureBlocksConfirmation);
    RUN_TEST(test_OtaBoot_StartedZigbeeWithoutSdkMainloopBlocksConfirmation);
    RUN_TEST(test_OtaBoot_ZigbeeMainloopExitBetweenHealthySamplesPreventsConfirmation);
    RUN_TEST(test_OtaBoot_ZigbeeMainloopMissingWithoutFallbackStaysUnconfirmed);
    RUN_TEST(test_OtaBoot_TransientSensorSampleDoesNotGateServices);
    RUN_TEST(test_OtaBoot_UnstartedRelayNeverConfirms);
    RUN_TEST(test_OtaBoot_FailedStartupRequestsRollbackNotConfirmation);
    RUN_TEST(test_OtaBoot_ConfirmationFailureCannotBecomePass);
}
#ifndef RACOOL_TARGET_TESTING
int main(void) {
    UNITY_BEGIN();
    test_OtaBootRecovery_All();
    return UNITY_END();
}
#endif
