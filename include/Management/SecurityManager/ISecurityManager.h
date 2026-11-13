/**
 * @file ISecurityManager.h
 * @brief Public interface for the SecurityManager service.
 *
 * The SecurityManager provides diagnostic authentication, lockout enforcement
 * and runtime configuration update capabilities for the
 * RaCool ESP32-C6 firmware.
 *
 * Architecture notes:
 *   - Implements the IService lifecycle (Initialize → Start → Stop).
 *   - Mutating/snapshot methods use a bounded FreeRTOS mutex; failures preserve output.
 *   - EventBus notifications are best effort and do not prove subscriber delivery.
 *   - Credentials (token / PIN) are stored in SecurityManagerConfig and never
 *     enter normal logs; development self-test credentials are public fixtures.
 *
 * @author RaCool DeepMind Team
 * @date   2026-08-06
 */

#ifndef ISECURITY_MANAGER_H
#define ISECURITY_MANAGER_H

#include "Core/IService.h"
#include "Management/SecurityManager/SecurityManagerConfig.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ISecurityManager ISecurityManager;

struct ISecurityManager {
    /** @brief Inherited IService lifecycle interface (Initialize / Start / Stop / …). */
    IService base;

    /* ── Authentication ─────────────────────────────────────────────────────*/

    /**
     * @brief Authenticates a caller using a token string.
     *
     * Performs a constant-time comparison against the stored token. Increments
     * the failed-attempt counter and may trigger lockout on repeated failures.
     *
     * @param self  Pointer to the ISecurityManager instance.
     * @param token Null-terminated authentication token.
     * @return true if authentication succeeds; false otherwise.
     */
    bool (*Authenticate)(ISecurityManager *self, const char *token);

    /**
     * @brief Authenticates a caller using a numeric or alphanumeric PIN.
     *
     * Performs a constant-time comparison against the stored PIN. Shares the
     * same lockout counter as token-based authentication.
     *
     * @param self Pointer to the ISecurityManager instance.
     * @param pin  Null-terminated PIN string.
     * @return true if authentication succeeds; false otherwise.
     */
    bool (*AuthenticatePin)(ISecurityManager *self, const char *pin);

    /* ── Lockout Management ─────────────────────────────────────────────────*/

    /**
     * @brief Returns whether the system is currently in a lockout period.
     *
     * Automatically clears an expired lockout and publishes
     * EVENT_SECURITY_LOCKOUT_ENDED when expiry is detected.
     *
     * @param self Pointer to the ISecurityManager instance.
     * @return true if locked out; false otherwise.
     */
    bool (*IsLockedOut)(ISecurityManager *self);

    /**
     * @brief Returns the remaining lockout duration.
     *
     * @param self Pointer to the ISecurityManager instance.
     * @return Remaining lockout time in milliseconds; 0 if not locked out.
     */
    uint32_t (*GetLockoutRemainingMs)(ISecurityManager *self);

    /**
     * @brief Administratively resets the lockout state and failed-attempt counter.
     *
     * Publishes EVENT_SECURITY_LOCKOUT_ENDED on success.
     *
     * @param self Pointer to the ISecurityManager instance.
     * @return SECURITY_STATUS_OK on success; SECURITY_STATUS_ERROR_* on failure.
     */
    int32_t (*ResetLockout)(ISecurityManager *self);

    /* ── Statistics & Diagnostics ───────────────────────────────────────────*/

    /**
     * @brief Retrieves a snapshot of operational statistics.
     *
     * Thread-safe: takes the internal mutex before copying stats.
     *
     * @param self     Pointer to the ISecurityManager instance.
     * @param outStats Output pointer to a SecurityStats_t struct to populate.
     * @return SECURITY_STATUS_OK on success; SECURITY_STATUS_ERROR_* on failure.
     */
    int32_t (*GetStats)(ISecurityManager *self, SecurityStats_t *outStats);

    /**
     * @brief Returns the current number of consecutive failed authentication attempts.
     *
     * Read-only; does not modify state.
     *
     * @param self Pointer to the ISecurityManager instance.
     * @return Current failed-attempt count; 0 if none or not started.
     */
    uint8_t (*GetFailedAttemptCount)(ISecurityManager *self);

    /**
     * @brief Executes an internal diagnostic self-test suite.
     *
     * Tests authentication, PIN auth, lockout, and reset logic using isolated
     * helper functions (no recursive mutex locking). Restores all state after
     * testing. Publishes EVENT_SECURITY_SELF_TEST_PASSED or
     * EVENT_SECURITY_SELF_TEST_FAILED.
     *
     * @param self Pointer to the ISecurityManager instance.
     * @return SECURITY_STATUS_OK if all sub-tests pass; SECURITY_STATUS_ERROR_SELF_TEST_FAILED otherwise.
     */
    int32_t (*SelfTest)(ISecurityManager *self);

    /* ── Configuration ──────────────────────────────────────────────────────*/

    /**
     * @brief Applies a new runtime configuration to the SecurityManager.
     *
     * Validates the supplied config before applying. Publishes
     * EVENT_SECURITY_CONFIG_UPDATED on success.
     *
     * @param self   Pointer to the ISecurityManager instance.
     * @param config Pointer to the new configuration struct to apply.
     * @return SECURITY_STATUS_OK on success; SECURITY_STATUS_ERROR_* on failure.
     */
    int32_t (*UpdateConfig)(ISecurityManager *self, const SecurityManagerConfig *config);

    /**
     * @brief Retrieves a copy of the current runtime configuration.
     *
     * Thread-safe: takes the internal mutex before copying config.
     *
     * @param self      Pointer to the ISecurityManager instance.
     * @param outConfig Output pointer to a SecurityManagerConfig struct to populate.
     * @return SECURITY_STATUS_OK on success; SECURITY_STATUS_ERROR_* on failure.
     */
    int32_t (*GetConfig)(ISecurityManager *self, SecurityManagerConfig *outConfig);
};

/**
 * @brief Returns the global singleton ISecurityManager instance.
 *
 * Initialises the vtable on first call. Not re-entrant on first call —
 * must be called from a single-threaded context (e.g. app startup) before
 * any concurrent access.
 *
 * @return Non-null pointer to the ISecurityManager singleton.
 */
ISecurityManager *SecurityManager_GetModule(void);

#ifdef __cplusplus
}
#endif

#endif /* ISECURITY_MANAGER_H */
