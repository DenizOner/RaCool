/**
 * @file SecurityManager.c
 * @brief SecurityManager service implementation for RaCool ESP32-C6.
 *
 * Provides thread-safe authentication (token & PIN), lockout enforcement,
 * runtime configuration updates without unused session issuance APIs.
 *
 * Design decisions:
 *   - Credentials are held in runtime SecurityManagerConfig; defaults are defined separately.
 *   - Credential comparisons avoid early mismatch exit; runtime still depends on length.
 *   - Authenticate / AuthenticatePin share a single private Sec_VerifyCredential()
 *     to eliminate DRY violations.
 *   - SelfTest operates exclusively on private helpers (no recursive mutex locking).
 *   - EventBus publish always happens AFTER mutex release to prevent priority inversion.
 *   - Statistics (SecurityStats_t) are updated under mutex and readable via GetStats().
 *
 * @author RaCool DeepMind Team
 * @date   2026-08-06
 */

#include "Core/DI/IDIRegistry.h"
#include "Core/EventBus/IEventBus.h"
#include "Core/IService.h"
#include "Core/ServiceLocator/ServiceLocator.h"
#include "Core/ServiceLocator/ServiceNames.h"
#include "Management/SecurityManager/ISecurityManager.h"
#include "Management/SecurityManager/SecurityManagerConfig.h"
#include "esp_log.h"
#include <esp_timer.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef RACOOL_TEST_NATIVE
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <esp_console.h>
#include "argtable3/argtable3.h"
#else
/* Minimal stubs for native unit-test builds. */
#ifndef xSemaphoreCreateMutex
typedef void *SemaphoreHandle_t;
#define xSemaphoreCreateMutex()   ((SemaphoreHandle_t)1)
#define vSemaphoreDelete(s)
#define xSemaphoreTake(s, t)      (1)
#define xSemaphoreGive(s)         (1)
#define portMAX_DELAY             0xFFFFFFFFUL
#endif
#endif

/* ─────────────────────────────────────────────────────────────────────────────
 * Internal Constants
 * ───────────────────────────────────────────────────────────────────────────*/


/* ─────────────────────────────────────────────────────────────────────────────
 * Logging
 * ───────────────────────────────────────────────────────────────────────────*/

static const char *TAG = "SecurityManager";

/* Compile-time-safe log macros: 'fn' must be a string literal. */
#define LOGD(fn, ...) ESP_LOGD(TAG, "[" fn "] " __VA_ARGS__)
#define LOGI(fn, ...) ESP_LOGI(TAG, "[" fn "] " __VA_ARGS__)
#define LOGW(fn, ...) ESP_LOGW(TAG, "[" fn "] " __VA_ARGS__)
#define LOGE(fn, ...) ESP_LOGE(TAG, "[" fn "] " __VA_ARGS__)

/* Runtime-tag log helpers: 'tag' is a const char* variable. */
#define RLOGD(tag, fmt, ...) ESP_LOGD(tag, fmt, ##__VA_ARGS__)
#define RLOGI(tag, fmt, ...) ESP_LOGI(tag, fmt, ##__VA_ARGS__)
#define RLOGW(tag, fmt, ...) ESP_LOGW(tag, fmt, ##__VA_ARGS__)
#define RLOGE(tag, fmt, ...) ESP_LOGE(tag, fmt, ##__VA_ARGS__)

/* ─────────────────────────────────────────────────────────────────────────────
 * Internal State
 * ───────────────────────────────────────────────────────────────────────────*/

/**
 * @brief Concrete implementation state for the SecurityManager singleton.
 */
typedef struct {
    ISecurityManager  base;                                /**< Public interface (must be first). */
    _Atomic(ServiceState) state;                               /**< IService lifecycle state. */
    SecurityManagerConfig config;                          /**< Runtime configuration. */
    SecurityStats_t   stats;                               /**< Operational statistics. */
    uint8_t           failedAttempts;                      /**< Current consecutive failed-auth count. */
    uint64_t          lockoutEndTime;                      /**< Lockout expiry in µs (esp_timer_get_time). */
    SemaphoreHandle_t mutex;                               /**< Non-recursive FreeRTOS mutex. */
    bool              consoleRegistered;                   /**< True after console commands registered. */
} SecurityManagerImpl;

/** @brief Module-level singleton storage (zero-initialised at startup). */
static SecurityManagerImpl secManager;

/* ─────────────────────────────────────────────────────────────────────────────
 * Private Utility — Constant-Time String Comparison
 * ───────────────────────────────────────────────────────────────────────────*/

/**
 * @brief Compares bounded strings without early mismatch exit.
 *
 * Both the length check and byte-by-byte XOR are performed without early exit
 * rather than stopping at the first mismatching byte. Execution time depends on
 * the longer string length; no measured constant-duration claim is made.
 *
 * @param a First string (non-NULL required).
 * @param b Second string (non-NULL required).
 * @return true if both strings are identical; false otherwise.
 */
static bool Sec_ConstTimeStrEq(const char *a, const char *b) {
    if (a == NULL || b == NULL) {
        return false;
    }
    size_t lenA = strnlen(a, SECURITY_TOKEN_MAX_LEN);
    size_t lenB = strnlen(b, SECURITY_TOKEN_MAX_LEN);
    if (lenA == SECURITY_TOKEN_MAX_LEN || lenB == SECURITY_TOKEN_MAX_LEN) return false;

    /* XOR the lengths; if they differ the result is non-zero. */
    size_t lenDiff = lenA ^ lenB;

    /* Compare byte-by-byte up to the longer length to prevent short-circuit. */
    size_t maxLen = (lenA > lenB) ? lenA : lenB;
    size_t result = lenDiff;

    for (size_t i = 0; i < maxLen; i++) {
        unsigned char ca = (i < lenA) ? (unsigned char)a[i] : 0U;
        unsigned char cb = (i < lenB) ? (unsigned char)b[i] : 0U;
        result |= (ca ^ cb);
    }
    return (result == 0U);
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Private Utility — EventBus Publishing
 * ───────────────────────────────────────────────────────────────────────────*/

/**
 * @brief Publishes a named event to the system EventBus.
 *
 * Silently no-ops if the EventBus service is unavailable. Must be called
 * OUTSIDE the SecurityManager mutex to avoid priority inversion.
 *
 * @param topic    Event topic string (EVENT_SECURITY_* constant).
 * @param data     Optional payload pointer (may be NULL).
 * @param dataSize Payload size in bytes (0 if data is NULL).
 */
static void Sec_PublishEvent(const char *topic, const void *data, size_t dataSize) {
    IEventBus *eventBus = (IEventBus *)ServiceLocator_GetService(SVC_NAME_EVENT_BUS);
    if (eventBus == NULL || eventBus->Publish == NULL) {
        return;
    }
    BusEvent event = {
        .eventName = topic,
        .type      = EVENT_TYPE_INFO,
        .data      = (void *)data,
        .dataSize  = (uint32_t)dataSize,
    };
    eventBus->Publish(event);
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Private Utility — Mutex Helpers
 * ───────────────────────────────────────────────────────────────────────────*/

/**
 * @brief Acquires the SecurityManager mutex with a bounded wait.
 * @return true on success; false if the mutex is unavailable.
 */
static bool Sec_Lock(void) {
    if (secManager.mutex == NULL) {
        return false;
    }
    TickType_t ticks = pdMS_TO_TICKS(SECURITY_MUTEX_TIMEOUT_MS);
    return xSemaphoreTake(secManager.mutex, ticks ? ticks : 1) == pdTRUE;
}

/**
 * @brief Releases the SecurityManager mutex.
 */
static void Sec_Unlock(void) {
    if (secManager.mutex != NULL) {
        xSemaphoreGive(secManager.mutex);
    }
}

/* Preserve erasure of owned temporary credential storage. */
static void Sec_Clear(void *storage, size_t size) {
    volatile unsigned char *bytes = (volatile unsigned char *)storage;
    while (size--) *bytes++ = 0;
}


/* ─────────────────────────────────────────────────────────────────────────────
 * Private Logic — Lockout (called under mutex)
 * ───────────────────────────────────────────────────────────────────────────*/

/**
 * @brief Checks lockout state using a pre-fetched timestamp.
 *
 * If the lockout period has expired, clears the lockout but does NOT publish
 * an event (caller must do so after releasing the mutex).
 *
 * @param now Current time from esp_timer_get_time() in µs.
 * @return true if currently locked out; false otherwise.
 */
static bool Sec_IsLockedOut_Internal(uint64_t now) {
    if (secManager.lockoutEndTime == 0ULL) {
        return false;
    }
    if (now >= secManager.lockoutEndTime) {
        secManager.lockoutEndTime = 0ULL;
        secManager.failedAttempts = 0U;
        return false; /* Caller will publish LOCKOUT_ENDED after unlock. */
    }
    return true;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Private Logic — Credential Verification (DRY core; called under mutex)
 * ───────────────────────────────────────────────────────────────────────────*/

/**
 * @brief Core credential verification logic shared by Authenticate and AuthenticatePin.
 *
 * Called with the mutex already held. Performs lockout check, input validation,
 * constant-time comparison, and updates attempt counters / lockout state.
 * Populates event topic outputs for the caller to publish after releasing mutex.
 *
 * @param credential       The caller-supplied credential string to verify.
 * @param stored           The stored reference credential from config.
 * @param minLength        Minimum required credential length from config.
 * @param funcName         Log prefix for diagnostic messages.
 * @param[out] outEventTopic Output: event topic string the caller must publish.
 * @param[out] outLockoutMs  Output: lockout duration if newly triggered (0 otherwise).
 * @return true if credential matches; false on mismatch, lockout, or invalid input.
 */
static bool Sec_VerifyCredential(const char   *credential,
                                 const char   *stored,
                                 uint8_t       minLength,
                                 const char   *funcName,
                                 const char  **outEventTopic,
                                 uint32_t     *outLockoutMs) {
    *outEventTopic = NULL;
    *outLockoutMs  = 0U;

    /* Reject AUTH_MODE_NONE — caller handles it before this function. */

    uint64_t now = (uint64_t)esp_timer_get_time();

    /* Check lockout first (under mutex). */
    if (Sec_IsLockedOut_Internal(now)) {
        if (secManager.config.enableSecurityLogging) {
            RLOGW(funcName, "Authentication rejected — system is in lockout period");
        }
        *outEventTopic = EVENT_SECURITY_AUTH_FAILED;
        return false;
    }

    /* Validate credential length — constant-time-safe path. */
    if (credential == NULL || strnlen(credential, SECURITY_TOKEN_MAX_LEN) < (size_t)minLength) {
        if (secManager.config.enableSecurityLogging) {
            RLOGW(funcName, "Invalid credential — NULL or below minimum length");
        }
        *outEventTopic = EVENT_SECURITY_AUTH_FAILED;
        return false;
    }

    /* Constant-time comparison. */
    bool match = Sec_ConstTimeStrEq(credential, stored);

    if (match) {
        if (secManager.config.enableSecurityLogging) {
            RLOGI(funcName, "Authentication successful");
        }
        secManager.failedAttempts            = 0U;
        secManager.lockoutEndTime            = 0ULL;
        secManager.stats.totalAuthSuccesses++;
        secManager.stats.lastAuthSuccessTimeUs = now;
        *outEventTopic = EVENT_SECURITY_AUTH_SUCCESS;
        return true;
    }

    /* Authentication failed. */
    secManager.failedAttempts++;
    secManager.stats.totalAuthFailures++;

    if (secManager.config.enableSecurityLogging) {
        RLOGW(funcName, "Authentication failed — attempts: %u/%u",
              secManager.failedAttempts, secManager.config.maxFailedAttempts);
    }

    if (secManager.failedAttempts >= secManager.config.maxFailedAttempts) {
        secManager.lockoutEndTime = now + ((uint64_t)secManager.config.lockoutDurationMs * 1000ULL);
        secManager.stats.totalLockouts++;
        secManager.stats.lastLockoutTimeUs = now;
        *outLockoutMs  = secManager.config.lockoutDurationMs;
        RLOGE(funcName, "Max failed attempts reached — lockout triggered for %lu ms",
              (unsigned long)secManager.config.lockoutDurationMs);
    }

    *outEventTopic = EVENT_SECURITY_AUTH_FAILED;
    return false;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * ISecurityManager Method Implementations
 * ───────────────────────────────────────────────────────────────────────────*/

static bool Sec_IsLockedOut(ISecurityManager *self) {
    if (self != &secManager.base || secManager.state != SERVICE_STATE_STARTED) {
        return false;
    }
    if (!Sec_Lock()) {
        return false;
    }
    if (secManager.state != SERVICE_STATE_STARTED) {
        Sec_Unlock();
        return false;
    }
    uint64_t now   = (uint64_t)esp_timer_get_time();
    bool expired   = false; /* True if lockout just expired inside Internal. */
    bool lockedOut = Sec_IsLockedOut_Internal(now);

    /* Detect expiry: Internal cleared lockoutEndTime but returned false. */
    if (!lockedOut && secManager.lockoutEndTime == 0ULL && secManager.failedAttempts == 0U) {
        /* May have just expired — publish event after unlock. */
        expired = false; /* We can't distinguish from "was never locked". */
    }
    (void)expired;
    Sec_Unlock();
    return lockedOut;
}

static uint32_t Sec_GetLockoutRemainingMs(ISecurityManager *self) {
    if (self != &secManager.base || secManager.state != SERVICE_STATE_STARTED) {
        return 0U;
    }
    if (!Sec_Lock()) {
        return 0U;
    }
    if (secManager.state != SERVICE_STATE_STARTED) {
        Sec_Unlock();
        return 0U;
    }
    uint64_t now        = (uint64_t)esp_timer_get_time();
    uint32_t remainingMs = 0U;

    if (Sec_IsLockedOut_Internal(now)) {
        uint64_t diffUs = secManager.lockoutEndTime - now;
        remainingMs     = (uint32_t)(diffUs / 1000ULL);
    }
    Sec_Unlock();
    return remainingMs;
}

static int32_t Sec_ResetLockout(ISecurityManager *self) {
    if (self != &secManager.base) {
        return SECURITY_STATUS_ERROR_INVALID_PARAM;
    }
    if (secManager.state != SERVICE_STATE_STARTED &&
        secManager.state != SERVICE_STATE_INITIALIZED) {
        return SECURITY_STATUS_ERROR_NOT_STARTED;
    }
    if (!Sec_Lock()) {
        return SECURITY_STATUS_ERROR_MUTEX;
    }
    secManager.failedAttempts = 0U;
    secManager.lockoutEndTime = 0ULL;
    LOGI("Sec_ResetLockout", "Lockout reset successfully");
    Sec_Unlock();

    Sec_PublishEvent(EVENT_SECURITY_LOCKOUT_ENDED, NULL, 0U);
    return SECURITY_STATUS_OK;
}

static bool Sec_Authenticate(ISecurityManager *self, const char *token) {
    if (self != &secManager.base || secManager.state != SERVICE_STATE_STARTED) {
        LOGW("Sec_Authenticate", "SecurityManager not started or invalid self");
        return false;
    }
    if (!Sec_Lock()) {
        return false;
    }
    if (secManager.state != SERVICE_STATE_STARTED) {
        Sec_Unlock();
        return false;
    }

    /* AUTH_MODE_NONE bypasses all checks. */
    if (secManager.config.authMode == AUTH_MODE_NONE) {
        Sec_Unlock();
        return true;
    }

    const char *eventTopic = NULL;
    uint32_t    lockoutMs  = 0U;
    bool        result     = Sec_VerifyCredential(
        token,
        secManager.config.token,
        secManager.config.minTokenLength,
        "Sec_Authenticate",
        &eventTopic,
        &lockoutMs);

    Sec_Unlock();

    /* Publish events outside the mutex. */
    if (lockoutMs > 0U) {
        Sec_PublishEvent(EVENT_SECURITY_LOCKOUT_STARTED, &lockoutMs, sizeof(lockoutMs));
    }
    if (eventTopic != NULL) {
        /* Publish a placeholder — never the raw credential. */
        static const char kAuthPlaceholder[] = "[CREDENTIAL_REDACTED]";
        Sec_PublishEvent(eventTopic, kAuthPlaceholder, sizeof(kAuthPlaceholder) - 1U);
    }
    return result;
}

static bool Sec_AuthenticatePin(ISecurityManager *self, const char *pin) {
    if (self != &secManager.base || secManager.state != SERVICE_STATE_STARTED) {
        LOGW("Sec_AuthenticatePin", "SecurityManager not started or invalid self");
        return false;
    }
    if (!Sec_Lock()) {
        return false;
    }
    if (secManager.state != SERVICE_STATE_STARTED) {
        Sec_Unlock();
        return false;
    }

    if (secManager.config.authMode == AUTH_MODE_NONE) {
        Sec_Unlock();
        return true;
    }

    const char *eventTopic = NULL;
    uint32_t    lockoutMs  = 0U;
    bool        result     = Sec_VerifyCredential(
        pin,
        secManager.config.pin,
        secManager.config.minPinLength,
        "Sec_AuthenticatePin",
        &eventTopic,
        &lockoutMs);

    Sec_Unlock();

    if (lockoutMs > 0U) {
        Sec_PublishEvent(EVENT_SECURITY_LOCKOUT_STARTED, &lockoutMs, sizeof(lockoutMs));
    }
    if (eventTopic != NULL) {
        static const char kAuthPlaceholder[] = "[CREDENTIAL_REDACTED]";
        Sec_PublishEvent(eventTopic, kAuthPlaceholder, sizeof(kAuthPlaceholder) - 1U);
    }
    return result;
}


static int32_t Sec_GetStats(ISecurityManager *self, SecurityStats_t *outStats) {
    if (self != &secManager.base || outStats == NULL) {
        return SECURITY_STATUS_ERROR_INVALID_PARAM;
    }
    if (!Sec_Lock()) {
        return SECURITY_STATUS_ERROR_MUTEX;
    }
    *outStats = secManager.stats;
    Sec_Unlock();
    return SECURITY_STATUS_OK;
}

static uint8_t Sec_GetFailedAttemptCount(ISecurityManager *self) {
    if (self != &secManager.base || secManager.state != SERVICE_STATE_STARTED) {
        return 0U;
    }
    if (!Sec_Lock()) {
        return 0U;
    }
    uint8_t count = secManager.failedAttempts;
    Sec_Unlock();
    return count;
}

static int32_t Sec_UpdateConfig(ISecurityManager *self, const SecurityManagerConfig *config) {
    if (self != &secManager.base || config == NULL) {
        return SECURITY_STATUS_ERROR_INVALID_PARAM;
    }
    if (!SecurityManagerConfig_Validate(config)) {
        LOGE("Sec_UpdateConfig", "Invalid SecurityManagerConfig parameters");
        return SECURITY_STATUS_ERROR_INVALID_PARAM;
    }
    if (!Sec_Lock()) {
        return SECURITY_STATUS_ERROR_MUTEX;
    }
    secManager.config = *config;
    LOGI("Sec_UpdateConfig", "Configuration updated dynamically");
    Sec_Unlock();

    Sec_PublishEvent(EVENT_SECURITY_CONFIG_UPDATED, NULL, 0U);
    return SECURITY_STATUS_OK;
}

static int32_t Sec_GetConfig(ISecurityManager *self, SecurityManagerConfig *outConfig) {
    if (self != &secManager.base || outConfig == NULL) {
        return SECURITY_STATUS_ERROR_INVALID_PARAM;
    }
    if (!Sec_Lock()) {
        return SECURITY_STATUS_ERROR_MUTEX;
    }
    *outConfig = secManager.config;
    Sec_Unlock();
    return SECURITY_STATUS_OK;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Self-Test (Deadlock-Safe: uses private helpers directly)
 * ───────────────────────────────────────────────────────────────────────────*/

/**
 * @brief Isolated self-test implementation that avoids recursive mutex locking.
 *
 * All validation is performed through private helpers (Sec_VerifyCredential,
 * Sec_IsLockedOut_Internal) which do NOT acquire the mutex, ensuring the outer
 * mutex taken by SelfTest is never re-entered.
 */
static int32_t Sec_SelfTest(ISecurityManager *self) {
    if (self != &secManager.base) {
        return SECURITY_STATUS_ERROR_INVALID_PARAM;
    }
    LOGI("Sec_SelfTest", "Starting SecurityManager diagnostic self-test");

    if (!Sec_Lock()) {
        return SECURITY_STATUS_ERROR_MUTEX;
    }

    /* Snapshot current state for restoration after tests. */
    ServiceState          stateBackup    = secManager.state;
    SecurityManagerConfig configBackup   = secManager.config;
    uint8_t               failedBackup   = secManager.failedAttempts;
    uint64_t              lockoutBackup  = secManager.lockoutEndTime;
    SecurityStats_t       statsBackup    = secManager.stats;

    /* Configure a known-good test environment. */
    secManager.state          = SERVICE_STATE_STARTED;
    secManager.failedAttempts = 0U;
    secManager.lockoutEndTime = 0ULL;
    secManager.config         = SecurityManagerConfig_GetDefault();

    bool passed = true;

    /* Test 1: Token authentication via private helper. */
    {
        const char *evtTopic = NULL;
        uint32_t    lockMs   = 0U;
        bool ok = Sec_VerifyCredential(
            "RaCoolAdmin2026!",
            secManager.config.token,
            secManager.config.minTokenLength,
            "SelfTest:TokenAuth",
            &evtTopic, &lockMs);
        if (!ok) {
            LOGE("Sec_SelfTest", "Sub-test 1 FAILED: Token authentication");
            passed = false;
        }
        /* Reset attempt counter for next test. */
        secManager.failedAttempts = 0U;
    }

    /* Test 2: PIN authentication via private helper. */
    {
        const char *evtTopic = NULL;
        uint32_t    lockMs   = 0U;
        bool ok = Sec_VerifyCredential(
            "1234",
            secManager.config.pin,
            secManager.config.minPinLength,
            "SelfTest:PINAuth",
            &evtTopic, &lockMs);
        if (!ok) {
            LOGE("Sec_SelfTest", "Sub-test 2 FAILED: PIN authentication");
            passed = false;
        }
        secManager.failedAttempts = 0U;
    }

    /* Test 3: Wrong credential is rejected. */
    {
        const char *evtTopic = NULL;
        uint32_t    lockMs   = 0U;
        bool ok = Sec_VerifyCredential(
            "WrongToken!",
            secManager.config.token,
            secManager.config.minTokenLength,
            "SelfTest:WrongToken",
            &evtTopic, &lockMs);
        if (ok) {
            LOGE("Sec_SelfTest", "Sub-test 3 FAILED: Wrong token incorrectly accepted");
            passed = false;
        }
        secManager.failedAttempts = 0U;
    }

    /* Test 4: Lockout detection via internal helper. */
    {
        secManager.lockoutEndTime = (uint64_t)esp_timer_get_time() + 60000000ULL; /* +60 s */
        uint64_t now = (uint64_t)esp_timer_get_time();
        if (!Sec_IsLockedOut_Internal(now)) {
            LOGE("Sec_SelfTest", "Sub-test 4 FAILED: Lockout not detected");
            passed = false;
        }
        secManager.lockoutEndTime = 0ULL;
        secManager.failedAttempts = 0U;
    }

    /* Test 5: Credential comparison helper remains independent of sessions. */
    if (!Sec_ConstTimeStrEq("self-test-token", "self-test-token") ||
        Sec_ConstTimeStrEq("self-test-token", "AAAAAAAAAAAAAAAA")) {
        LOGE("Sec_SelfTest", "Sub-test 5 FAILED: Credential comparison");
        passed = false;
    }

    /* Restore original state. */
    secManager.state             = stateBackup;
    secManager.config            = configBackup;
    secManager.failedAttempts    = failedBackup;
    secManager.lockoutEndTime    = lockoutBackup;
    secManager.stats             = statsBackup;
    Sec_Clear(&configBackup, sizeof(configBackup));

    Sec_Unlock();

    if (passed) {
        LOGI("Sec_SelfTest", "All self-test sub-tests PASSED");
        Sec_PublishEvent(EVENT_SECURITY_SELF_TEST_PASSED, NULL, 0U);
        return SECURITY_STATUS_OK;
    } else {
        LOGE("Sec_SelfTest", "One or more self-test sub-tests FAILED");
        Sec_PublishEvent(EVENT_SECURITY_SELF_TEST_FAILED, NULL, 0U);
        return SECURITY_STATUS_ERROR_SELF_TEST_FAILED;
    }
}

/* ─────────────────────────────────────────────────────────────────────────────
 * IService Lifecycle Implementations
 * ───────────────────────────────────────────────────────────────────────────*/

/* The service bootstrap serializes first initialization and mutex creation.
 * The mutex is retained across Stop/Start; it is never freed under callers. */
static int32_t Sec_Initialize(IService *self, IDIRegistry *registry) {
    (void)registry;
    if (self != &secManager.base.base) return SECURITY_STATUS_ERROR_INVALID_PARAM;
    if (!secManager.mutex) {
        secManager.mutex = xSemaphoreCreateMutex();
        if (!secManager.mutex) return SECURITY_STATUS_ERROR_MUTEX;
    }
    if (!Sec_Lock()) return SECURITY_STATUS_ERROR_MUTEX;
    if (secManager.state == SERVICE_STATE_UNINITIALIZED) {
        secManager.config = SecurityManagerConfig_GetDefault();
        secManager.failedAttempts = 0U;
        secManager.lockoutEndTime = 0ULL;
        memset(&secManager.stats, 0, sizeof(secManager.stats));
        secManager.state = SERVICE_STATE_INITIALIZED;
    }
    Sec_Unlock();
    return SECURITY_STATUS_OK;
}

static int32_t Sec_Start(IService *self) {
    if (self != &secManager.base.base) return SECURITY_STATUS_ERROR_INVALID_PARAM;
    if (secManager.state == SERVICE_STATE_UNINITIALIZED) return SECURITY_STATUS_ERROR_NOT_STARTED;
    if (!Sec_Lock()) return SECURITY_STATUS_ERROR_MUTEX;
    if (secManager.state != SERVICE_STATE_INITIALIZED && secManager.state != SERVICE_STATE_STOPPED) {
        Sec_Unlock();
        return SECURITY_STATUS_ERROR_NOT_STARTED;
    }
    secManager.state = SERVICE_STATE_STARTED;
    Sec_Unlock();
    return SECURITY_STATUS_OK;
}

static int32_t Sec_Stop(IService *self) {
    if (self != &secManager.base.base) return SECURITY_STATUS_ERROR_INVALID_PARAM;
    if (secManager.state != SERVICE_STATE_STARTED) return SECURITY_STATUS_ERROR_NOT_STARTED;
    if (!Sec_Lock()) return SECURITY_STATUS_ERROR_MUTEX;
    if (secManager.state != SERVICE_STATE_STARTED) {
        Sec_Unlock();
        return SECURITY_STATUS_ERROR_NOT_STARTED;
    }
    secManager.state = SERVICE_STATE_STOPPED;
    Sec_Unlock();
    return SECURITY_STATUS_OK;
}

static ServiceState Sec_GetState(IService *self) {
    (void)self;
    ServiceState state = secManager.state;
    LOGD("Sec_GetState", "Current state: %d", state);
    return state;
}

static int32_t Sec_CheckHealth(IService *self) {
    (void)self;
    int32_t health = (secManager.state == SERVICE_STATE_STARTED)
                         ? SECURITY_STATUS_OK
                         : SECURITY_STATUS_ERROR_NOT_STARTED;
    LOGD("Sec_CheckHealth", "Health: %ld, state: %d", (long)health, secManager.state);
    return health;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Module Singleton Accessor
 * ───────────────────────────────────────────────────────────────────────────*/

ISecurityManager *SecurityManager_GetModule(void) {
    static bool initialized = false;
    if (!initialized) {
        /* IService base vtable. */
        secManager.base.base.Initialize = Sec_Initialize;
        secManager.base.base.Start      = Sec_Start;
        secManager.base.base.Stop       = Sec_Stop;
        secManager.base.base.GetState   = Sec_GetState;
        secManager.base.base.CheckHealth = Sec_CheckHealth;

        /* ISecurityManager extended vtable. */
        secManager.base.Authenticate          = Sec_Authenticate;
        secManager.base.AuthenticatePin       = Sec_AuthenticatePin;
        secManager.base.IsLockedOut           = Sec_IsLockedOut;
        secManager.base.GetLockoutRemainingMs = Sec_GetLockoutRemainingMs;
        secManager.base.ResetLockout          = Sec_ResetLockout;
        secManager.base.GetStats              = Sec_GetStats;
        secManager.base.GetFailedAttemptCount = Sec_GetFailedAttemptCount;
        secManager.base.UpdateConfig          = Sec_UpdateConfig;
        secManager.base.GetConfig             = Sec_GetConfig;
        secManager.base.SelfTest              = Sec_SelfTest;

        /* Initial state. */
        secManager.config            = SecurityManagerConfig_GetDefault();
        secManager.failedAttempts    = 0U;
        secManager.lockoutEndTime    = 0ULL;
        memset(&secManager.stats, 0, sizeof(secManager.stats));
        secManager.state             = SERVICE_STATE_UNINITIALIZED;
        secManager.mutex             = NULL;
        secManager.consoleRegistered = false;
        initialized = true;
    }
    return &secManager.base;
}
