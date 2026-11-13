#include "unity.h"
#include "Management/ApplicationManager/FactoryReset.h"
#include <string.h>

static IConfigurationManager config;
static IPowerManager power;
static IZigbee radio;
void setUp(void) {}
void tearDown(void) {}

static unsigned action_calls, fail_action, restart_calls, network_calls;
static bool network_pending, factory_new, connected, pairing, bad_cpu, bad_tx, bad_receipt;
static bool power_pending;
static esp_err_t network_result;
static esp_err_t checked_action(void) { return ++action_calls == fail_action ? ESP_FAIL : ESP_OK; }
static esp_err_t checked_guard(IConfigurationManager *self, bool active) {
    (void)self; TEST_ASSERT_TRUE(active); return checked_action();
}
static bool checked_power_pending(IPowerManager *self) { (void)self; return power_pending; }
static esp_err_t checked_reset(IConfigurationManager *self, const char *block) {
    (void)self; TEST_ASSERT_NULL(block); return checked_action();
}
static esp_err_t checked_defaults(IConfigurationManager *self, SystemSettings *out) {
    (void)self;
    esp_err_t result = checked_action();
    if (result == ESP_OK) { memset(out, 0, sizeof(*out)); out->system.power_profile = POWER_MODE_LOW_POWER; }
    return result;
}
static int32_t checked_cpu(IPowerManager *self, PowerMode_t mode) {
    (void)self; TEST_ASSERT_EQUAL(POWER_MODE_LOW_POWER, mode); return checked_action();
}
static PowerMode_t checked_mode(IPowerManager *self) { (void)self; return bad_cpu ? POWER_MODE_BALANCED : POWER_MODE_LOW_POWER; }
static int32_t checked_selftest(IPowerManager *self) { (void)self; return checked_action(); }
static int32_t checked_tx(IZigbee *self, ZigbeeTxProfile profile) {
    (void)self; TEST_ASSERT_EQUAL(ZIGBEE_TX_ECO, profile); return checked_action();
}
static int32_t checked_tx_status(IZigbee *self, ZigbeeTxStatus *out) {
    (void)self; out->profile = bad_tx ? ZIGBEE_TX_BOOST : ZIGBEE_TX_ECO; out->dbm = bad_tx ? 20 : 0;
    return checked_action();
}
static bool checked_pairing(IZigbee *self) { (void)self; return pairing; }
static int32_t checked_network(IZigbee *self, uint32_t *receipt) {
    (void)self; ++network_calls; *receipt = 42; return checked_action();
}
static int32_t checked_network_status(IZigbee *self, ZigbeeFactoryResetStatus *out) {
    (void)self;
    *out = (ZigbeeFactoryResetStatus){.operation_id = bad_receipt ? 41 : 42,
        .pending = network_pending, .factory_new = factory_new, .connected = connected, .result = network_result};
    return checked_action();
}
static int32_t checked_restart(IPowerManager *self, uint32_t delay) {
    (void)self; TEST_ASSERT_EQUAL(0, delay); ++restart_calls; return checked_action();
}
static FactoryResetOperation prepare_coordinator(uint32_t start) {
    action_calls = fail_action = restart_calls = network_calls = 0;
    network_pending = factory_new = pairing = bad_cpu = bad_tx = bad_receipt = false;
    connected = true; network_result = ESP_OK;
    power_pending = false;
    config = (IConfigurationManager){.ResetToDefaults = checked_reset, .VerifyDefaults = checked_defaults,
        .SetFactoryResetGuard = checked_guard};
    power = (IPowerManager){.SetPowerMode = checked_cpu, .GetPowerMode = checked_mode,
        .SelfTest = checked_selftest, .RestartAsync = checked_restart, .IsTransitionPending = checked_power_pending};
    radio = (IZigbee){.SetTxProfile = checked_tx, .GetTxStatus = checked_tx_status,
        .IsPairing = checked_pairing, .BeginFactoryReset = checked_network, .GetFactoryResetStatus = checked_network_status};
    return (FactoryResetOperation){.phase = FACTORY_RESET_PREFLIGHT, .started_ms = start,
        .config = &config, .power = &power, .radio = &radio};
}
static void advance_to_network_wait(FactoryResetOperation *op, uint32_t now) {
    for (unsigned i = 0; i < 5; ++i) FactoryReset_Advance(op, now);
}
void test_FactoryCoordinatorRequiresCompletionBeforeExactlyOneRestart(void) {
    FactoryResetOperation op = prepare_coordinator(0);
    advance_to_network_wait(&op, 0);
    TEST_ASSERT_EQUAL(FACTORY_RESET_WAIT_NETWORK, op.phase);
    network_pending = true;
    for (unsigned i = 0; i < 100; ++i) FactoryReset_Advance(&op, i * 100);
    TEST_ASSERT_EQUAL(0, restart_calls);
    TEST_ASSERT_EQUAL(1, network_calls);
    network_pending = false; factory_new = true; connected = false;
    FactoryReset_Advance(&op, 10000); FactoryReset_Advance(&op, 10100);
    TEST_ASSERT_EQUAL(FACTORY_RESET_RESTART_REQUESTED, op.phase);
    for (unsigned i = 0; i < 100; ++i) FactoryReset_Advance(&op, 10200 + i * 100);
    TEST_ASSERT_EQUAL(1, restart_calls);
}
void test_FactoryCoordinatorEveryOwnerFailurePreventsSuccessAndRetry(void) {
    for (unsigned failure = 1; failure <= 15; ++failure) {
        FactoryResetOperation op = prepare_coordinator(0);
        fail_action = failure;
        for (unsigned i = 0; i < 10; ++i) {
            if (op.phase == FACTORY_RESET_WAIT_NETWORK) { factory_new = true; connected = false; }
            FactoryReset_Advance(&op, i * 100);
        }
        TEST_ASSERT_EQUAL(FACTORY_RESET_FAILED, op.phase);
        TEST_ASSERT_EQUAL(ESP_FAIL, op.error);
        TEST_ASSERT_TRUE(restart_calls <= 1);
        if (failure < 15) TEST_ASSERT_EQUAL(0, restart_calls);
    }
}
void test_FactoryCoordinatorMissingRestartOrReadbackRejectsBeforeWrites(void) {
    FactoryResetOperation op = prepare_coordinator(0);
    power.RestartAsync = NULL;
    FactoryReset_Advance(&op, 0);
    TEST_ASSERT_EQUAL(FACTORY_RESET_FAILED, op.phase); TEST_ASSERT_EQUAL(0, action_calls);
    op = prepare_coordinator(0); config.VerifyDefaults = NULL;
    FactoryReset_Advance(&op, 0);
    TEST_ASSERT_EQUAL(FACTORY_RESET_FAILED, op.phase); TEST_ASSERT_EQUAL(0, action_calls);
}
void test_FactoryCoordinatorEveryRequiredDependencyIsCheckedBeforeGuardOrWrites(void) {
    for (unsigned missing = 0; missing < 16; ++missing) {
        FactoryResetOperation op = prepare_coordinator(0);
        switch (missing) {
            case 0: op.config = NULL; break;
            case 1: op.power = NULL; break;
            case 2: op.radio = NULL; break;
            case 3: config.ResetToDefaults = NULL; break;
            case 4: config.VerifyDefaults = NULL; break;
            case 5: config.SetFactoryResetGuard = NULL; break;
            case 6: power.SetPowerMode = NULL; break;
            case 7: power.GetPowerMode = NULL; break;
            case 8: power.SelfTest = NULL; break;
            case 9: power.RestartAsync = NULL; break;
            case 10: power.IsTransitionPending = NULL; break;
            case 11: radio.SetTxProfile = NULL; break;
            case 12: radio.GetTxStatus = NULL; break;
            case 13: radio.BeginFactoryReset = NULL; break;
            case 14: radio.GetFactoryResetStatus = NULL; break;
            case 15: radio.IsPairing = NULL; break;
        }
        FactoryReset_Advance(&op, 0);
        TEST_ASSERT_EQUAL(FACTORY_RESET_FAILED, op.phase);
        TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, op.error);
        TEST_ASSERT_EQUAL(0, action_calls); TEST_ASSERT_EQUAL(0, restart_calls);
    }
}
void test_FactoryCoordinatorWrapSafeDeadlineNeverRestartsAfterTimeout(void) {
    FactoryResetOperation op = prepare_coordinator(UINT32_MAX - 999);
    advance_to_network_wait(&op, UINT32_MAX - 900);
    network_pending = true;
    FactoryReset_Advance(&op, 28999);
    TEST_ASSERT_EQUAL(FACTORY_RESET_WAIT_NETWORK, op.phase);
    FactoryReset_Advance(&op, 29000);
    TEST_ASSERT_EQUAL(FACTORY_RESET_FAILED, op.phase);
    TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT, op.error); TEST_ASSERT_EQUAL(0, restart_calls);
}
void test_FactoryCoordinatorProfilesAndReceiptMismatchNeverRestart(void) {
    for (unsigned mismatch = 0; mismatch < 5; ++mismatch) {
        FactoryResetOperation op = prepare_coordinator(0);
        bad_cpu = mismatch == 0; bad_tx = mismatch == 1;
        advance_to_network_wait(&op, 0);
        factory_new = true; connected = false;
        bad_receipt = mismatch == 2;
        network_result = mismatch == 3 ? ESP_FAIL : ESP_OK;
        if (mismatch == 4) factory_new = false;
        for (unsigned i = 0; i < 3; ++i) FactoryReset_Advance(&op, 100);
        TEST_ASSERT_EQUAL(FACTORY_RESET_FAILED, op.phase); TEST_ASSERT_EQUAL(0, restart_calls);
    }
}
void test_FactoryCoordinatorPendingSleepOrRestartRejectsBeforeSettingsReset(void) {
    FactoryResetOperation op = prepare_coordinator(0);
    power_pending = true;
    FactoryReset_Advance(&op, 0);
    TEST_ASSERT_EQUAL(FACTORY_RESET_FAILED, op.phase);
    TEST_ASSERT_EQUAL(1, action_calls);
    TEST_ASSERT_EQUAL(0, network_calls); TEST_ASSERT_EQUAL(0, restart_calls);
}
static uint32_t late_reset_clock(void) { return APP_FACTORY_RESET_TIMEOUT_MS; }
void test_FactoryCoordinatorLateSynchronousVerificationCannotAuthorizeRestart(void) {
    FactoryResetOperation op = prepare_coordinator(0);
    advance_to_network_wait(&op, 0);
    factory_new = true; connected = false;
    FactoryReset_Advance(&op, 100);
    op.ClockMs = late_reset_clock;
    FactoryReset_Advance(&op, 200);
    TEST_ASSERT_EQUAL(FACTORY_RESET_FAILED, op.phase);
    TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT, op.error); TEST_ASSERT_EQUAL(0, restart_calls);
}
