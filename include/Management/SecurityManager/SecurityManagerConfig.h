/**
 * @file SecurityManagerConfig.h
 * @brief Configuration structures, enumerations, and constants for the SecurityManager module.
 *
 * Defines the runtime-configurable parameters for authentication mode, lockout policy,
 * credential storage, and operational statistics.
 *
 * Design notes:
 *   - Token and PIN fields are runtime state. This module currently has no
 *     ConfigurationManager/NVS persistence integration.
 *   - All event topic strings follow the project-wide "domain.event" naming convention.
 *   - Status codes use negative int32_t values for failure to align with IService contract.
 *
 * @author RaCool DeepMind Team
 * @date   2026-08-06
 */

#ifndef SECURITY_MANAGER_CONFIG_H
#define SECURITY_MANAGER_CONFIG_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ─────────────────────────────────────────────────────────────────────────────
 * Credential Length Limits
 * ───────────────────────────────────────────────────────────────────────────*/

/** @brief Maximum stored token length (including null terminator). */
#define SECURITY_TOKEN_MAX_LEN  64
#define SECURITY_MUTEX_TIMEOUT_MS 1000u

/** @brief Maximum stored PIN length (including null terminator). */
#define SECURITY_PIN_MAX_LEN    16

/* ─────────────────────────────────────────────────────────────────────────────
 * EventBus Topic Strings
 * All topics follow the project-wide "domain.event" convention.
 * ───────────────────────────────────────────────────────────────────────────*/

#define EVENT_SECURITY_AUTH_SUCCESS      "security.auth.success"
#define EVENT_SECURITY_AUTH_FAILED       "security.auth.failed"
#define EVENT_SECURITY_LOCKOUT_STARTED   "security.lockout.started"
#define EVENT_SECURITY_LOCKOUT_ENDED     "security.lockout.ended"
#define EVENT_SECURITY_CONFIG_UPDATED    "security.config.updated"
#define EVENT_SECURITY_SELF_TEST_PASSED  "security.selftest.passed"
#define EVENT_SECURITY_SELF_TEST_FAILED  "security.selftest.failed"

/* ─────────────────────────────────────────────────────────────────────────────
 * Authentication Mode Enumeration
 * ───────────────────────────────────────────────────────────────────────────*/

/**
 * @brief Authentication modes for SecurityManager.
 */
typedef enum {
    AUTH_MODE_NONE       = 0, /**< No authentication required — all requests pass. */
    AUTH_MODE_TOKEN      = 1, /**< Token-based authentication against stored token. */
    AUTH_MODE_PIN        = 2, /**< PIN-based authentication against stored PIN. */
    AUTH_MODE_HARDCODED  = 3, /**< Legacy fallback to compile-time constants (deprecated). */
} AuthMode;

/* ─────────────────────────────────────────────────────────────────────────────
 * Status Codes
 * ───────────────────────────────────────────────────────────────────────────*/

/**
 * @brief Return codes for SecurityManager operations.
 *
 * All error values are negative to conform with the IService int32_t contract.
 */
typedef enum {
    SECURITY_STATUS_OK                     =  0,
    SECURITY_STATUS_ERROR_INVALID_PARAM    = -1,
    SECURITY_STATUS_ERROR_LOCKED_OUT       = -2,
    SECURITY_STATUS_ERROR_AUTH_FAILED      = -3,
    SECURITY_STATUS_ERROR_NOT_STARTED      = -4,
    SECURITY_STATUS_ERROR_ALREADY_INIT     = -5,
    SECURITY_STATUS_ERROR_MUTEX            = -6,
    SECURITY_STATUS_ERROR_SELF_TEST_FAILED = -7,
    SECURITY_STATUS_ERROR_SESSION_INVALID  = -8,
    SECURITY_STATUS_ERROR_STORAGE          = -9,
} SecurityManagerStatus;

/* ─────────────────────────────────────────────────────────────────────────────
 * Runtime Configuration Structure
 * ───────────────────────────────────────────────────────────────────────────*/

/**
 * @brief Runtime configuration for the SecurityManager.
 *
 * Credentials can be updated at runtime. Defaults are currently supplied by
 * SecurityManagerConfig_GetDefault(); this structure alone provides no persistence.
 */
typedef struct {
    AuthMode authMode;                          /**< Active authentication mode. */
    uint32_t authTimeoutMs;                     /**< Reserved legacy timeout; no session issuance API. */
    uint8_t  maxFailedAttempts;                 /**< Failed attempts before lockout triggers. */
    uint32_t lockoutDurationMs;                 /**< Duration of lockout period in milliseconds. */
    bool     enableSecurityLogging;             /**< Emit ESP_LOGx entries for security events. */
    uint8_t  minTokenLength;                    /**< Minimum acceptable token string length. */
    uint8_t  minPinLength;                      /**< Minimum acceptable PIN string length. */

    /** @brief Stored authentication token (null-terminated, configurable at runtime). */
    char token[SECURITY_TOKEN_MAX_LEN];

    /** @brief Stored PIN credential (null-terminated, configurable at runtime). */
    char pin[SECURITY_PIN_MAX_LEN];
} SecurityManagerConfig;

/* ─────────────────────────────────────────────────────────────────────────────
 * Operational Statistics Structure
 * ───────────────────────────────────────────────────────────────────────────*/

/**
 * @brief Accumulated runtime statistics for the SecurityManager.
 *
 * All counters are monotonically increasing and reset only on module
 * re-initialization. Safe to read under mutex or by snapshot copy.
 */
typedef struct {
    uint32_t totalAuthSuccesses;   /**< Cumulative successful authentications. */
    uint32_t totalAuthFailures;    /**< Cumulative failed authentication attempts. */
    uint32_t totalLockouts;        /**< Cumulative lockout events triggered. */
    uint32_t totalSessionsCreated; /**< Reserved compatibility counter; session APIs retired. */
    uint32_t totalSessionsExpired; /**< Reserved compatibility counter; session APIs retired. */
    uint64_t lastAuthSuccessTimeUs;/**< esp_timer_get_time() of last successful auth. */
    uint64_t lastLockoutTimeUs;    /**< esp_timer_get_time() of last lockout trigger. */
} SecurityStats_t;

/* ─────────────────────────────────────────────────────────────────────────────
 * Config Helper Functions
 * ───────────────────────────────────────────────────────────────────────────*/

/**
 * @brief Returns a fully-populated default configuration for the SecurityManager.
 *
 * Default token is "RaCoolAdmin2026!" and default PIN is "1234". These values
 * are development defaults, not product interface protection. This module
 * has runtime UpdateConfig() but no NVS persistence integration.
 *
 * @return SecurityManagerConfig populated with safe default values.
 */
SecurityManagerConfig SecurityManagerConfig_GetDefault(void);

/**
 * @brief Validates all fields in a SecurityManagerConfig instance.
 *
 * Checks that authMode, maxFailedAttempts, and minimum length fields
 * are within accepted bounds. Requires bounded, terminated credential arrays,
 * active credential minimum lengths and a nonzero legacy timeout field.
 *
 * @param config Pointer to the configuration structure to validate.
 * @return true if all parameters are valid; false if config is NULL or invalid.
 */
bool SecurityManagerConfig_Validate(const SecurityManagerConfig *config);

#ifdef __cplusplus
}
#endif

#endif /* SECURITY_MANAGER_CONFIG_H */
