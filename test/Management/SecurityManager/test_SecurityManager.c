/**
 * @file test_SecurityManager.c
 * @brief Unit and native integration tests for the SecurityManager module.
 *
 * Tests exercise ISecurityManager with native hardware mocks. Implementation
 * units are included so lifecycle and failure fixtures can inspect internal state.
 *
 * Test Groups:
 *   - Lifecycle & state transitions
 *   - Token authentication
 *   - PIN authentication
 *   - Lockout and reset logic
 *   - Configuration update and validation
 *   - Statistics accumulation
 *   - Constant-time comparison behaviour
 *   - Error paths and null-pointer guards
 *   - SelfTest (isolated, no deadlock)
 *   - Console command registration (smoke test)
 *
 * @author RaCool DeepMind Team
 * @date   2026-08-06
 */

#include "Management/SecurityManager/ISecurityManager.h"
#include "Management/SecurityManager/SecurityManagerConfig.h"
#include "Management/SecurityManager/SecurityManagerConsoleCommands.h"
#include "test_mocks.h"
#include "unity.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * The fixture includes console dispatch and service implementation
 * units are included once for the internal-state fixtures below.
 */
#include "Management/SecurityManager/SecurityManagerConfig.c"
#include "Management/SecurityManager/SecurityManager.c"
#include "Management/SecurityManager/SecurityManagerConsoleCommands.c"

/* ─────────────────────────────────────────────────────────────────────────────
 * Test Fixtures
 * ───────────────────────────────────────────────────────────────────────────*/


static IService *security_test_service(const char *name) {
    if (strcmp(name, SVC_NAME_SECURITY_MANAGER) == 0) return (IService *)SecurityManager_GetModule();
    return NULL;
}

void setUp(void) {
    RESET_FAKE(mocked_ServiceLocator_GetService);
    mocked_ServiceLocator_GetService_fake.custom_fake = security_test_service;
    RESET_FAKE(mocked_xSemaphoreCreateMutex);
    RESET_FAKE(mocked_xSemaphoreTake);
    RESET_FAKE(mocked_xSemaphoreGive);
    mocked_xSemaphoreCreateMutex_fake.return_val = (SemaphoreHandle_t)1;
    mocked_xSemaphoreTake_fake.return_val = pdTRUE;
    mocked_xSemaphoreGive_fake.return_val = pdTRUE;
}
void tearDown(void) { /* Required by Unity; intentionally empty. */ }

/**
 * @brief Resets the SecurityManager singleton to a pristine, uninitialized state.
 *
 * Accesses the internal `secManager` variable which is translation-unit-visible
 * because this file is compiled together with SecurityManager.c via the runner.
 * Uses IService->Initialize() to drive proper state transitions, then
 * IService->Start() to put the service in a testable running state.
 */
static void reset_and_start(void) {
    /* Force re-initialisation path. */
    secManager.state          = SERVICE_STATE_UNINITIALIZED;
    secManager.failedAttempts = 0U;
    secManager.lockoutEndTime = 0ULL;
    memset(&secManager.stats, 0, sizeof(secManager.stats));
    secManager.config         = SecurityManagerConfig_GetDefault();
    secManager.mutex          = NULL;

    ISecurityManager *sm = SecurityManager_GetModule();
    IService         *svc = &sm->base;
    svc->Initialize(svc, NULL);
    svc->Start(svc);
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Group 1: Lifecycle & State Transitions
 * ───────────────────────────────────────────────────────────────────────────*/

/**
 * @brief Validates the full Initialize → Start → CheckHealth → Stop lifecycle.
 */
void test_SecurityManager_Lifecycle(void) {
    secManager.state = SERVICE_STATE_UNINITIALIZED;
    secManager.mutex = NULL;

    ISecurityManager *sm  = SecurityManager_GetModule();
    IService         *svc = &sm->base;

    TEST_ASSERT_EQUAL(SECURITY_STATUS_OK,            svc->Initialize(svc, NULL));
    TEST_ASSERT_EQUAL(SERVICE_STATE_INITIALIZED,     secManager.state);

    TEST_ASSERT_EQUAL(SECURITY_STATUS_OK,            svc->Start(svc));
    TEST_ASSERT_EQUAL(SERVICE_STATE_STARTED,         secManager.state);

    TEST_ASSERT_EQUAL(SECURITY_STATUS_OK,            svc->CheckHealth(svc));

    TEST_ASSERT_EQUAL(SECURITY_STATUS_OK,            svc->Stop(svc));
    TEST_ASSERT_EQUAL(SERVICE_STATE_STOPPED,         secManager.state);
}

/**
 * @brief Verifies that Start from STOPPED state succeeds (restart path).
 */
void test_SecurityManager_Restart(void) {
    reset_and_start();
    ISecurityManager *sm  = SecurityManager_GetModule();
    IService         *svc = &sm->base;

    TEST_ASSERT_EQUAL(SECURITY_STATUS_OK, svc->Stop(svc));
    TEST_ASSERT_EQUAL(SECURITY_STATUS_OK, svc->Start(svc)); /* Restart from STOPPED. */
    TEST_ASSERT_EQUAL(SERVICE_STATE_STARTED, secManager.state);
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Group 2: Token Authentication
 * ───────────────────────────────────────────────────────────────────────────*/

/**
 * @brief Token auth succeeds with the configured token value.
 */
void test_SecurityManager_TokenAuth_Success(void) {
    reset_and_start();
    ISecurityManager *sm = SecurityManager_GetModule();

    TEST_ASSERT_TRUE(sm->Authenticate(sm, "RaCoolAdmin2026!"));
}

/**
 * @brief Token auth fails for wrong, NULL, or too-short tokens.
 */
void test_SecurityManager_TokenAuth_Failures(void) {
    reset_and_start();
    ISecurityManager *sm = SecurityManager_GetModule();

    TEST_ASSERT_FALSE(sm->Authenticate(sm, "wrongToken"));
    /* Reset attempts to avoid lockout contaminating subsequent assertions. */
    sm->ResetLockout(sm);
    TEST_ASSERT_FALSE(sm->Authenticate(sm, NULL));
    sm->ResetLockout(sm);
    TEST_ASSERT_FALSE(sm->Authenticate(sm, "ab")); /* Below minTokenLength (4). */
}

/**
 * @brief Auth fails when service is not started.
 */
void test_SecurityManager_TokenAuth_NotStarted(void) {
    reset_and_start();
    ISecurityManager *sm = SecurityManager_GetModule();
    secManager.state = SERVICE_STATE_INITIALIZED; /* Not STARTED. */
    TEST_ASSERT_FALSE(sm->Authenticate(sm, "RaCoolAdmin2026!"));
}

/**
 * @brief AUTH_MODE_NONE allows any token without checking credentials.
 */
void test_SecurityManager_AuthModeNone_AlwaysSuccess(void) {
    reset_and_start();
    ISecurityManager *sm = SecurityManager_GetModule();

    SecurityManagerConfig cfg;
    sm->GetConfig(sm, &cfg);
    cfg.authMode = AUTH_MODE_NONE;
    sm->UpdateConfig(sm, &cfg);

    TEST_ASSERT_TRUE(sm->Authenticate(sm, "completely_wrong_token"));
    TEST_ASSERT_TRUE(sm->AuthenticatePin(sm, "completely_wrong_pin"));
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Group 3: PIN Authentication
 * ───────────────────────────────────────────────────────────────────────────*/

/**
 * @brief PIN auth succeeds with the configured PIN value.
 */
void test_SecurityManager_PinAuth_Success(void) {
    reset_and_start();
    ISecurityManager *sm = SecurityManager_GetModule();

    TEST_ASSERT_TRUE(sm->AuthenticatePin(sm, "1234"));
}

/**
 * @brief PIN auth fails for wrong, NULL, or too-short PINs.
 */
void test_SecurityManager_PinAuth_Failures(void) {
    reset_and_start();
    ISecurityManager *sm = SecurityManager_GetModule();

    TEST_ASSERT_FALSE(sm->AuthenticatePin(sm, "0000"));
    sm->ResetLockout(sm);
    TEST_ASSERT_FALSE(sm->AuthenticatePin(sm, NULL));
    sm->ResetLockout(sm);
    TEST_ASSERT_FALSE(sm->AuthenticatePin(sm, "12")); /* Below minPinLength (4). */
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Group 4: Lockout Logic
 * ───────────────────────────────────────────────────────────────────────────*/

/**
 * @brief Repeated failures trigger lockout at maxFailedAttempts threshold.
 */
void test_SecurityManager_Lockout_TriggeredAtThreshold(void) {
    reset_and_start();
    ISecurityManager *sm = SecurityManager_GetModule();
    uint8_t maxAttempts = secManager.config.maxFailedAttempts; /* Default: 5 */

    for (uint8_t i = 0; i < maxAttempts - 1U; i++) {
        TEST_ASSERT_FALSE(sm->Authenticate(sm, "invalid"));
        TEST_ASSERT_FALSE(sm->IsLockedOut(sm));
    }
    /* Nth attempt should trigger lockout. */
    TEST_ASSERT_FALSE(sm->Authenticate(sm, "invalid"));
    TEST_ASSERT_TRUE(sm->IsLockedOut(sm));
    TEST_ASSERT_GREATER_THAN(0U, sm->GetLockoutRemainingMs(sm));
}

/**
 * @brief Authentication is rejected while locked out, even with valid credentials.
 */
void test_SecurityManager_Lockout_RejectsValidCredentials(void) {
    reset_and_start();
    ISecurityManager *sm = SecurityManager_GetModule();

    /* Manually set lockout state. */
    secManager.lockoutEndTime = (uint64_t)esp_timer_get_time() + 60000000ULL; /* +60 s */

    TEST_ASSERT_FALSE(sm->Authenticate(sm, "RaCoolAdmin2026!"));
    TEST_ASSERT_FALSE(sm->AuthenticatePin(sm, "1234"));
}

/**
 * @brief ResetLockout clears state and restores authentication ability.
 */
void test_SecurityManager_Lockout_ResetRestoresAuth(void) {
    reset_and_start();
    ISecurityManager *sm = SecurityManager_GetModule();

    /* Trigger lockout. */
    for (int i = 0; i < (int)secManager.config.maxFailedAttempts; i++) {
        sm->Authenticate(sm, "invalid");
    }
    TEST_ASSERT_TRUE(sm->IsLockedOut(sm));

    TEST_ASSERT_EQUAL(SECURITY_STATUS_OK, sm->ResetLockout(sm));
    TEST_ASSERT_FALSE(sm->IsLockedOut(sm));
    TEST_ASSERT_EQUAL(0U, sm->GetLockoutRemainingMs(sm));
    TEST_ASSERT_TRUE(sm->Authenticate(sm, "RaCoolAdmin2026!"));
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Group 5: Configuration
 * ───────────────────────────────────────────────────────────────────────────*/

/**
 * @brief GetConfig returns a copy of the active configuration.
 */
void test_SecurityManager_Config_Get(void) {
    reset_and_start();
    ISecurityManager *sm = SecurityManager_GetModule();

    SecurityManagerConfig cfg;
    TEST_ASSERT_EQUAL(SECURITY_STATUS_OK, sm->GetConfig(sm, &cfg));
    TEST_ASSERT_EQUAL(AUTH_MODE_TOKEN, cfg.authMode);
    TEST_ASSERT_EQUAL(5U, cfg.maxFailedAttempts);
}

/**
 * @brief UpdateConfig with a valid config is accepted and takes effect.
 */
void test_SecurityManager_Config_Update(void) {
    reset_and_start();
    ISecurityManager *sm = SecurityManager_GetModule();

    SecurityManagerConfig cfg;
    sm->GetConfig(sm, &cfg);
    cfg.maxFailedAttempts = 3U;
    TEST_ASSERT_EQUAL(SECURITY_STATUS_OK, sm->UpdateConfig(sm, &cfg));

    SecurityManagerConfig readBack;
    sm->GetConfig(sm, &readBack);
    TEST_ASSERT_EQUAL(3U, readBack.maxFailedAttempts);
}

/**
 * @brief UpdateConfig rejects NULL and invalid parameter combinations.
 */
void test_SecurityManager_Config_InvalidRejected(void) {
    reset_and_start();
    ISecurityManager *sm = SecurityManager_GetModule();

    TEST_ASSERT_EQUAL(SECURITY_STATUS_ERROR_INVALID_PARAM, sm->UpdateConfig(sm, NULL));

    SecurityManagerConfig bad;
    sm->GetConfig(sm, &bad);
    bad.minTokenLength = 0U; /* Forbidden value. */
    TEST_ASSERT_EQUAL(SECURITY_STATUS_ERROR_INVALID_PARAM, sm->UpdateConfig(sm, &bad));
}

/**
 * @brief Runtime token update via config takes effect on next authentication.
 */
void test_SecurityManager_Config_TokenUpdateApplied(void) {
    reset_and_start();
    ISecurityManager *sm = SecurityManager_GetModule();

    SecurityManagerConfig cfg;
    sm->GetConfig(sm, &cfg);
    strncpy(cfg.token, "NewToken2026!", SECURITY_TOKEN_MAX_LEN - 1);
    sm->UpdateConfig(sm, &cfg);

    TEST_ASSERT_FALSE(sm->Authenticate(sm, "RaCoolAdmin2026!")); /* Old token rejected. */
    sm->ResetLockout(sm);
    TEST_ASSERT_TRUE(sm->Authenticate(sm, "NewToken2026!"));     /* New token accepted. */
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Group 7: Statistics
 * ───────────────────────────────────────────────────────────────────────────*/

/**
 * @brief Statistics accumulate correctly across auth successes and failures.
 */
void test_SecurityManager_Stats_Accumulation(void) {
    reset_and_start();
    ISecurityManager *sm = SecurityManager_GetModule();

    sm->Authenticate(sm, "RaCoolAdmin2026!"); /* 1 success */
    sm->Authenticate(sm, "wrong");            /* 1 failure */
    sm->ResetLockout(sm);
    sm->Authenticate(sm, "wrong2");           /* 2nd failure */
    sm->ResetLockout(sm);

    SecurityStats_t stats;
    TEST_ASSERT_EQUAL(SECURITY_STATUS_OK, sm->GetStats(sm, &stats));
    TEST_ASSERT_EQUAL(1U, stats.totalAuthSuccesses);
    TEST_ASSERT_EQUAL(2U, stats.totalAuthFailures);
}

/**
 * @brief Lockout counter increments when maxFailedAttempts is reached.
 */
void test_SecurityManager_Stats_LockoutCount(void) {
    reset_and_start();
    ISecurityManager *sm = SecurityManager_GetModule();

    for (int i = 0; i < (int)secManager.config.maxFailedAttempts; i++) {
        sm->Authenticate(sm, "invalid");
    }
    SecurityStats_t stats;
    sm->GetStats(sm, &stats);
    TEST_ASSERT_EQUAL(1U, stats.totalLockouts);
}

/**
 * @brief GetStats rejects NULL output pointer.
 */
void test_SecurityManager_Stats_NullRejected(void) {
    reset_and_start();
    ISecurityManager *sm = SecurityManager_GetModule();
    TEST_ASSERT_EQUAL(SECURITY_STATUS_ERROR_INVALID_PARAM, sm->GetStats(sm, NULL));
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Group 8: Error Paths & Null Guards
 * ───────────────────────────────────────────────────────────────────────────*/

/**
 * @brief Start fails when service is in UNINITIALIZED state.
 */
void test_SecurityManager_Errors_StartNotInitialized(void) {
    secManager.state = SERVICE_STATE_UNINITIALIZED;
    ISecurityManager *sm  = SecurityManager_GetModule();
    IService         *svc = &sm->base;
    TEST_ASSERT_EQUAL(SECURITY_STATUS_ERROR_NOT_STARTED, svc->Start(svc));
}

/**
 * @brief Stop fails when service is not started.
 */
void test_SecurityManager_Errors_StopNotStarted(void) {
    secManager.state = SERVICE_STATE_INITIALIZED;
    ISecurityManager *sm  = SecurityManager_GetModule();
    IService         *svc = &sm->base;
    TEST_ASSERT_EQUAL(SECURITY_STATUS_ERROR_NOT_STARTED, svc->Stop(svc));
}

/**
 * @brief All methods handle NULL self gracefully.
 */
void test_SecurityManager_Errors_NullSelfGuards(void) {
    ISecurityManager *sm = SecurityManager_GetModule();

    TEST_ASSERT_FALSE(sm->IsLockedOut(NULL));
    TEST_ASSERT_EQUAL(0U, sm->GetLockoutRemainingMs(NULL));
    TEST_ASSERT_EQUAL(SECURITY_STATUS_ERROR_INVALID_PARAM, sm->ResetLockout(NULL));
    TEST_ASSERT_EQUAL(SECURITY_STATUS_ERROR_INVALID_PARAM, sm->SelfTest(NULL));
    TEST_ASSERT_EQUAL(SECURITY_STATUS_ERROR_INVALID_PARAM, sm->GetStats(NULL, NULL));
    TEST_ASSERT_EQUAL(0U, sm->GetFailedAttemptCount(NULL));
}

/**
 * @brief GetFailedAttemptCount tracks consecutive failures.
 */
void test_SecurityManager_Errors_FailedAttemptCount(void) {
    reset_and_start();
    ISecurityManager *sm = SecurityManager_GetModule();

    TEST_ASSERT_EQUAL(0U, sm->GetFailedAttemptCount(sm));
    sm->Authenticate(sm, "wrong");
    TEST_ASSERT_EQUAL(1U, sm->GetFailedAttemptCount(sm));
    sm->Authenticate(sm, "RaCoolAdmin2026!"); /* Success resets counter. */
    TEST_ASSERT_EQUAL(0U, sm->GetFailedAttemptCount(sm));
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Group 9: SelfTest
 * ───────────────────────────────────────────────────────────────────────────*/

/**
 * @brief SelfTest passes on a freshly initialized module.
 */
void test_SecurityManager_SelfTest_Passes(void) {
    reset_and_start();
    ISecurityManager *sm = SecurityManager_GetModule();
    TEST_ASSERT_EQUAL(SECURITY_STATUS_OK, sm->SelfTest(sm));
}

/**
 * @brief SelfTest restores the original state (no side effects on config / stats).
 */
void test_SecurityManager_SelfTest_NoSideEffects(void) {
    reset_and_start();
    ISecurityManager *sm = SecurityManager_GetModule();

    SecurityManagerConfig cfgBefore;
    sm->GetConfig(sm, &cfgBefore);
    SecurityStats_t statsBefore;
    sm->GetStats(sm, &statsBefore);

    sm->SelfTest(sm);

    SecurityManagerConfig cfgAfter;
    sm->GetConfig(sm, &cfgAfter);
    SecurityStats_t statsAfter;
    sm->GetStats(sm, &statsAfter);

    /* Config must be unchanged after self-test. */
    TEST_ASSERT_EQUAL(cfgBefore.authMode,          cfgAfter.authMode);
    TEST_ASSERT_EQUAL(cfgBefore.maxFailedAttempts,  cfgAfter.maxFailedAttempts);

    /* Statistics must be unchanged (self-test uses snapshotted stats). */
    TEST_ASSERT_EQUAL(statsBefore.totalAuthSuccesses, statsAfter.totalAuthSuccesses);
    TEST_ASSERT_EQUAL(statsBefore.totalAuthFailures,  statsAfter.totalAuthFailures);
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Group 10: Console Command Registration (Smoke)
 * ───────────────────────────────────────────────────────────────────────────*/

/** @brief Missing CLI ownership must not be reported as registration success. */
void test_SecurityManager_ConsoleCommands_Registration(void) {
    mocked_ServiceLocator_GetService_fake.custom_fake = NULL;
    mocked_ServiceLocator_GetService_fake.return_val = NULL;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, SecurityManager_RegisterConsoleCommands());
}

void test_SecurityManager_Config_UnterminatedArraysRejected(void) {
    reset_and_start();
    ISecurityManager *sm = SecurityManager_GetModule();
    SecurityManagerConfig before;
    TEST_ASSERT_EQUAL(SECURITY_STATUS_OK, sm->GetConfig(sm, &before));
    SecurityManagerConfig bad = before;
    memset(bad.token, 'x', sizeof(bad.token));
    TEST_ASSERT_EQUAL(SECURITY_STATUS_ERROR_INVALID_PARAM, sm->UpdateConfig(sm, &bad));
    bad = before;
    memset(bad.pin, '1', sizeof(bad.pin));
    TEST_ASSERT_EQUAL(SECURITY_STATUS_ERROR_INVALID_PARAM, sm->UpdateConfig(sm, &bad));
    SecurityManagerConfig after;
    TEST_ASSERT_EQUAL(SECURITY_STATUS_OK, sm->GetConfig(sm, &after));
    TEST_ASSERT_EQUAL_MEMORY(&before, &after, sizeof(before));
}

void test_SecurityManager_Config_ImpossibleLengthsAndLifetimeRejected(void) {
    SecurityManagerConfig cfg = SecurityManagerConfig_GetDefault();
    cfg.minTokenLength = SECURITY_TOKEN_MAX_LEN;
    TEST_ASSERT_FALSE(SecurityManagerConfig_Validate(&cfg));
    cfg = SecurityManagerConfig_GetDefault();
    cfg.minPinLength = SECURITY_PIN_MAX_LEN;
    TEST_ASSERT_FALSE(SecurityManagerConfig_Validate(&cfg));
    cfg = SecurityManagerConfig_GetDefault();
    cfg.authTimeoutMs = 0;
    TEST_ASSERT_FALSE(SecurityManagerConfig_Validate(&cfg));
    cfg = SecurityManagerConfig_GetDefault();
    strcpy(cfg.token, "ab");
    TEST_ASSERT_FALSE(SecurityManagerConfig_Validate(&cfg));
    cfg = SecurityManagerConfig_GetDefault();
    cfg.authMode = AUTH_MODE_PIN;
    strcpy(cfg.pin, "12");
    TEST_ASSERT_FALSE(SecurityManagerConfig_Validate(&cfg));
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Test Runner
 * ───────────────────────────────────────────────────────────────────────────*/


static int32_t console_read_failure(ISecurityManager *self, SecurityManagerConfig *cfg) {
    (void)self; (void)cfg;
    return SECURITY_STATUS_ERROR_MUTEX;
}

void test_SecurityManager_Console_InvalidModePreservesConfig(void) {
    reset_and_start();
    ISecurityManager *sm = SecurityManager_GetModule();
    mocked_ServiceLocator_GetService_fake.return_val = (IService *)sm;
    SecurityManagerConfig before = secManager.config;
    const char *invalid[] = {"abc", "", "1x", "-1", "4", "999999999999999999999"};
    for (size_t i = 0; i < sizeof(invalid)/sizeof(invalid[0]); ++i) {
        char *argv[] = {"sec", "set-mode", (char *)invalid[i]};
        TEST_ASSERT_EQUAL(-1, sec_console_cmd_handler(3, argv));
        TEST_ASSERT_EQUAL_MEMORY(&before, &secManager.config, sizeof(before));
    }
    char *valid[] = {"sec", "set-mode", "0"};
    TEST_ASSERT_EQUAL(0, sec_console_cmd_handler(3, valid));
    TEST_ASSERT_EQUAL(AUTH_MODE_NONE, secManager.config.authMode);
    mocked_ServiceLocator_GetService_fake.return_val = NULL;
}

void test_SecurityManager_Console_ReadFailurePreservesConfig(void) {
    reset_and_start();
    ISecurityManager *sm = SecurityManager_GetModule();
    mocked_ServiceLocator_GetService_fake.return_val = (IService *)sm;
    SecurityManagerConfig before = secManager.config;
    sm->GetConfig = console_read_failure;
    char *commands[] = {"set-mode", "set-token", "set-pin", "status"};
    int results[4];
    for (size_t i = 0; i < 4; ++i) {
        char *argv[] = {"sec", commands[i], "0"};
        results[i] = sec_console_cmd_handler(3, argv);
    }
    sm->GetConfig = Sec_GetConfig;
    mocked_ServiceLocator_GetService_fake.return_val = NULL;
    for (size_t i = 0; i < 4; ++i) TEST_ASSERT_EQUAL(-1, results[i]);
    TEST_ASSERT_EQUAL_MEMORY(&before, &secManager.config, sizeof(before));
}

void test_SecurityManager_Console_OversizedValuesRejected(void) {
    reset_and_start();
    ISecurityManager *sm = SecurityManager_GetModule();
    mocked_ServiceLocator_GetService_fake.return_val = (IService *)sm;
    SecurityManagerConfig before = secManager.config;
    char value[SECURITY_TOKEN_MAX_LEN + SECURITY_PIN_MAX_LEN + 1];
    memset(value, '1', sizeof(value)-1);
    value[sizeof(value)-1] = '\0';
    char *commands[] = {"set-token", "set-pin"};
    for (size_t i = 0; i < 2; ++i) {
        char *argv[] = {"sec", commands[i], value};
        TEST_ASSERT_EQUAL(-1, sec_console_cmd_handler(3, argv));
        TEST_ASSERT_EQUAL_MEMORY(&before, &secManager.config, sizeof(before));
    }
    mocked_ServiceLocator_GetService_fake.return_val = NULL;
}


void test_SecurityManager_MutexAllocationFailureAndRetry(void) {
    ISecurityManager *sm = SecurityManager_GetModule();
    secManager.state = SERVICE_STATE_UNINITIALIZED;
    secManager.mutex = NULL;
    mocked_xSemaphoreCreateMutex_fake.return_val = NULL;
    TEST_ASSERT_EQUAL(SECURITY_STATUS_ERROR_MUTEX, sm->base.Initialize(&sm->base, NULL));
    TEST_ASSERT_EQUAL(SERVICE_STATE_UNINITIALIZED, secManager.state);
    SecurityManagerConfig cfg;
    TEST_ASSERT_EQUAL(SECURITY_STATUS_ERROR_MUTEX, sm->GetConfig(sm, &cfg));
    mocked_xSemaphoreCreateMutex_fake.return_val = (SemaphoreHandle_t)1;
    TEST_ASSERT_EQUAL(SECURITY_STATUS_OK, sm->base.Initialize(&sm->base, NULL));
    TEST_ASSERT_EQUAL(SERVICE_STATE_INITIALIZED, secManager.state);
}

void test_SecurityManager_ReinitializePreservesRuntimeState(void) {
    reset_and_start();
    ISecurityManager *sm = SecurityManager_GetModule();
    SecurityManagerConfig cfg = secManager.config;
    cfg.authTimeoutMs += 1000;
    TEST_ASSERT_EQUAL(SECURITY_STATUS_OK, sm->UpdateConfig(sm, &cfg));
    TEST_ASSERT_EQUAL(SECURITY_STATUS_OK, sm->base.Initialize(&sm->base, NULL));
    TEST_ASSERT_EQUAL(SERVICE_STATE_STARTED, secManager.state);
    TEST_ASSERT_EQUAL_MEMORY(&cfg, &secManager.config, sizeof(cfg));
    mocked_xSemaphoreTake_fake.return_val = pdFALSE;
    TEST_ASSERT_EQUAL(SECURITY_STATUS_ERROR_MUTEX, sm->base.Stop(&sm->base));
    TEST_ASSERT_EQUAL(SERVICE_STATE_STARTED, secManager.state);
    mocked_xSemaphoreTake_fake.return_val = pdTRUE;
    TEST_ASSERT_EQUAL(SECURITY_STATUS_OK, sm->base.Stop(&sm->base));
}

static BaseType_t stop_before_security_lock(SemaphoreHandle_t gate, TickType_t wait) {
    (void)gate; (void)wait;
    secManager.state = SERVICE_STATE_STOPPED;
    return pdTRUE;
}

void test_SecurityManager_StateRecheckedAfterLock(void) {
    reset_and_start();
    ISecurityManager *sm = SecurityManager_GetModule();
    secManager.config.authMode = AUTH_MODE_NONE;
    mocked_xSemaphoreTake_fake.custom_fake = stop_before_security_lock;
    bool authenticated = sm->Authenticate(sm, "dummy");
    mocked_xSemaphoreTake_fake.custom_fake = NULL;
    TEST_ASSERT_FALSE(authenticated);
}

void test_SecurityManager_BoundedCredentialComparison(void) {
    char oversized[SECURITY_TOKEN_MAX_LEN + 1];
    memset(oversized, 'x', sizeof(oversized)-1);
    oversized[sizeof(oversized)-1] = '\0';
    TEST_ASSERT_FALSE(Sec_ConstTimeStrEq(oversized, oversized));
    oversized[SECURITY_TOKEN_MAX_LEN-1] = '\0';
    TEST_ASSERT_TRUE(Sec_ConstTimeStrEq(oversized, oversized));
    TEST_ASSERT_FALSE(Sec_ConstTimeStrEq("prefix", "prefix-extra"));
    TEST_ASSERT_FALSE(Sec_ConstTimeStrEq(NULL, "x"));
}


void test_SecurityConsole_RejectsMalformedArgumentBoundaries(void);
void test_SecurityManager_LockFailurePreservesOwnedStateAndOutputs(void) {
    reset_and_start();
    ISecurityManager *sm = SecurityManager_GetModule();
    SecurityManagerConfig originalConfig = secManager.config;
    SecurityStats_t originalStats = secManager.stats;
    SecurityManagerConfig outConfig;
    SecurityStats_t outStats;
    memset(&outConfig, 0x5a, sizeof(outConfig));
    memset(&outStats, 0x5a, sizeof(outStats));
    unsigned gives = mocked_xSemaphoreGive_fake.call_count;
    mocked_xSemaphoreTake_fake.return_val = pdFALSE;
    TEST_ASSERT_EQUAL(SECURITY_STATUS_ERROR_MUTEX, sm->GetConfig(sm, &outConfig));
    TEST_ASSERT_EQUAL(SECURITY_STATUS_ERROR_MUTEX, sm->GetStats(sm, &outStats));
    TEST_ASSERT_EQUAL(SECURITY_STATUS_ERROR_MUTEX, sm->UpdateConfig(sm, &originalConfig));
    TEST_ASSERT_EQUAL(SECURITY_STATUS_ERROR_MUTEX, sm->SelfTest(sm));
    TEST_ASSERT_EQUAL(SECURITY_STATUS_ERROR_MUTEX, sm->base.Stop(&sm->base));
    TEST_ASSERT_FALSE(sm->Authenticate(sm, "fixture"));
    TEST_ASSERT_EQUAL(gives, mocked_xSemaphoreGive_fake.call_count);
    TEST_ASSERT_TRUE(mocked_xSemaphoreTake_fake.arg1_val > 0);
    TEST_ASSERT_TRUE(mocked_xSemaphoreTake_fake.arg1_val < portMAX_DELAY);
    TEST_ASSERT_EQUAL_MEMORY(&originalConfig, &secManager.config, sizeof(originalConfig));
    TEST_ASSERT_EQUAL_MEMORY(&originalStats, &secManager.stats, sizeof(originalStats));
    const unsigned char *configBytes = (const unsigned char *)&outConfig;
    const unsigned char *statsBytes = (const unsigned char *)&outStats;
    for (size_t i = 0; i < sizeof(outConfig); ++i) TEST_ASSERT_EQUAL_HEX8(0x5a, configBytes[i]);
    for (size_t i = 0; i < sizeof(outStats); ++i) TEST_ASSERT_EQUAL_HEX8(0x5a, statsBytes[i]);
    TEST_ASSERT_EQUAL(SERVICE_STATE_STARTED, secManager.state);
}


void test_SecurityManager_ForeignOwnerCannotChangeSingleton(void) {
    reset_and_start();
    ISecurityManager foreign = {0};
    SecurityManagerConfig original = secManager.config;
    unsigned locks = mocked_xSemaphoreTake_fake.call_count;
    TEST_ASSERT_EQUAL(SECURITY_STATUS_ERROR_INVALID_PARAM, Sec_Initialize(&foreign.base, NULL));
    TEST_ASSERT_EQUAL(SECURITY_STATUS_ERROR_INVALID_PARAM, Sec_Start(&foreign.base));
    TEST_ASSERT_EQUAL(SECURITY_STATUS_ERROR_INVALID_PARAM, Sec_Stop(&foreign.base));
    TEST_ASSERT_EQUAL(SECURITY_STATUS_ERROR_INVALID_PARAM, Sec_UpdateConfig(&foreign, &original));
    TEST_ASSERT_FALSE(Sec_Authenticate(&foreign, "fixture"));
    TEST_ASSERT_EQUAL(locks, mocked_xSemaphoreTake_fake.call_count);
    TEST_ASSERT_EQUAL_MEMORY(&original, &secManager.config, sizeof(original));
    TEST_ASSERT_EQUAL(SERVICE_STATE_STARTED, secManager.state);
}

void test_SecurityConsole_RetiredSessionCommandsCannotIssueCredentials(void) {
    reset_and_start();
    SecurityStats_t original=secManager.stats;
    char *issue[]={"sec","session-token"};
    char *invalidate[]={"sec","invalidate-session"};
    TEST_ASSERT_EQUAL(-1,sec_console_cmd_handler(2,issue));
    TEST_ASSERT_EQUAL(-1,sec_console_cmd_handler(2,invalidate));
    TEST_ASSERT_EQUAL_MEMORY(&original,&secManager.stats,sizeof(original));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_SecurityConsole_RetiredSessionCommandsCannotIssueCredentials);
    RUN_TEST(test_SecurityManager_LockFailurePreservesOwnedStateAndOutputs);
    RUN_TEST(test_SecurityManager_ForeignOwnerCannotChangeSingleton);

    /* Group 1: Lifecycle */
    RUN_TEST(test_SecurityManager_Lifecycle);
    RUN_TEST(test_SecurityManager_Restart);

    /* Group 2: Token Auth */
    RUN_TEST(test_SecurityManager_TokenAuth_Success);
    RUN_TEST(test_SecurityManager_TokenAuth_Failures);
    RUN_TEST(test_SecurityManager_TokenAuth_NotStarted);
    RUN_TEST(test_SecurityManager_AuthModeNone_AlwaysSuccess);

    /* Group 3: PIN Auth */
    RUN_TEST(test_SecurityManager_PinAuth_Success);
    RUN_TEST(test_SecurityManager_PinAuth_Failures);

    /* Group 4: Lockout */
    RUN_TEST(test_SecurityManager_Lockout_TriggeredAtThreshold);
    RUN_TEST(test_SecurityManager_Lockout_RejectsValidCredentials);
    RUN_TEST(test_SecurityManager_Lockout_ResetRestoresAuth);

    /* Group 5: Config */
    RUN_TEST(test_SecurityManager_Config_Get);
    RUN_TEST(test_SecurityManager_Config_Update);
    RUN_TEST(test_SecurityManager_Config_InvalidRejected);
    RUN_TEST(test_SecurityManager_Config_UnterminatedArraysRejected);
    RUN_TEST(test_SecurityManager_Config_ImpossibleLengthsAndLifetimeRejected);
    RUN_TEST(test_SecurityManager_Config_TokenUpdateApplied);


    /* Group 7: Stats */
    RUN_TEST(test_SecurityManager_Stats_Accumulation);
    RUN_TEST(test_SecurityManager_Stats_LockoutCount);
    RUN_TEST(test_SecurityManager_Stats_NullRejected);

    /* Group 8: Errors */
    RUN_TEST(test_SecurityManager_Errors_StartNotInitialized);
    RUN_TEST(test_SecurityManager_Errors_StopNotStarted);
    RUN_TEST(test_SecurityManager_Errors_NullSelfGuards);
    RUN_TEST(test_SecurityManager_Errors_FailedAttemptCount);

    /* Group 9: SelfTest */
    RUN_TEST(test_SecurityManager_SelfTest_Passes);
    RUN_TEST(test_SecurityManager_SelfTest_NoSideEffects);

    /* Group 10: Console */
    RUN_TEST(test_SecurityManager_ConsoleCommands_Registration);

    RUN_TEST(test_SecurityManager_Console_InvalidModePreservesConfig);
    RUN_TEST(test_SecurityManager_Console_ReadFailurePreservesConfig);
    RUN_TEST(test_SecurityManager_Console_OversizedValuesRejected);
    RUN_TEST(test_SecurityManager_MutexAllocationFailureAndRetry);
    RUN_TEST(test_SecurityManager_ReinitializePreservesRuntimeState);
    RUN_TEST(test_SecurityManager_StateRecheckedAfterLock);
    RUN_TEST(test_SecurityManager_BoundedCredentialComparison);
    RUN_TEST(test_SecurityConsole_RejectsMalformedArgumentBoundaries);
    return UNITY_END();
}


void test_SecurityConsole_RejectsMalformedArgumentBoundaries(void) {
    char *missing[] = {"sec", "auth"};
    char *extra[] = {"sec", "invalidate-session", "unexpected"};
    char *null_value[] = {"sec", "set-token", NULL};
    char *null_command[] = {"sec", NULL};
    TEST_ASSERT_NOT_EQUAL(0,sec_console_cmd_handler(2,NULL));
    TEST_ASSERT_NOT_EQUAL(0,sec_console_cmd_handler(2,missing));
    TEST_ASSERT_NOT_EQUAL(0,sec_console_cmd_handler(3,extra));
    TEST_ASSERT_NOT_EQUAL(0,sec_console_cmd_handler(3,null_value));
    TEST_ASSERT_NOT_EQUAL(0,sec_console_cmd_handler(2,null_command));
}
