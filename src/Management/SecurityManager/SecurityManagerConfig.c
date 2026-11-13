/**
 * @file SecurityManagerConfig.c
 * @brief Default configuration and validation implementation for the SecurityManager module.
 *
 * Provides SecurityManagerConfig_GetDefault() to initialise the module with
 * safe, immediately operational values, and SecurityManagerConfig_Validate()
 * to enforce parameter invariants before applying any configuration update.
 *
 * @author RaCool DeepMind Team
 * @date   2026-08-06
 */

#include "Management/SecurityManager/SecurityManagerConfig.h"
#include <stddef.h>
#include <string.h>

/* ─────────────────────────────────────────────────────────────────────────────
 * Public API
 * ───────────────────────────────────────────────────────────────────────────*/

SecurityManagerConfig SecurityManagerConfig_GetDefault(void) {
    SecurityManagerConfig config;
    memset(&config, 0, sizeof(config));

    config.authMode             = AUTH_MODE_TOKEN;
    config.authTimeoutMs        = 300000U; /* 5 minutes */
    config.maxFailedAttempts    = 5U;
    config.lockoutDurationMs    = 600000U; /* 10 minutes */
    config.enableSecurityLogging = true;
    config.minTokenLength       = 4U;
    config.minPinLength         = 4U;

    /*
     * Default credentials — MUST be changed in production via UpdateConfig()
     * or NVS-backed persistent storage. These values exist solely to allow
     * the module to start in a functional state during development.
     */
    strncpy(config.token, "RaCoolAdmin2026!", SECURITY_TOKEN_MAX_LEN - 1);
    strncpy(config.pin,   "1234",            SECURITY_PIN_MAX_LEN  - 1);

    return config;
}

bool SecurityManagerConfig_Validate(const SecurityManagerConfig *config) {
    if (config == NULL) {
        return false;
    }

    /* Authentication mode must be a known enum value. */
    if ((int)config->authMode < AUTH_MODE_NONE || (int)config->authMode > AUTH_MODE_HARDCODED) {
        return false;
    }

    /* At least one failed attempt must be required before lockout. */
    if (config->maxFailedAttempts == 0U) {
        return false;
    }

    /* Minimum lengths must be non-zero to prevent trivially short credentials. */
    if (config->minTokenLength == 0U || config->minPinLength == 0U) {
        return false;
    }

    /* Reject unterminated arrays before any consumer can treat them as strings. */
    const char *tokenEnd = memchr(config->token, '\0', sizeof(config->token));
    const char *pinEnd = memchr(config->pin, '\0', sizeof(config->pin));
    if (!tokenEnd || !pinEnd || config->minTokenLength >= sizeof(config->token) ||
        config->minPinLength >= sizeof(config->pin) || config->authTimeoutMs == 0U) return false;
    if (config->authMode == AUTH_MODE_TOKEN &&
        (size_t)(tokenEnd - config->token) < config->minTokenLength) return false;
    if (config->authMode == AUTH_MODE_PIN &&
        (size_t)(pinEnd - config->pin) < config->minPinLength) return false;

    /* Token and PIN must be non-empty when their respective auth mode is active. */
    if (config->authMode == AUTH_MODE_TOKEN && config->token[0] == '\0') {
        return false;
    }

    if (config->authMode == AUTH_MODE_PIN && config->pin[0] == '\0') {
        return false;
    }

    /* Lockout duration must be positive when max attempts are configured. */
    if (config->maxFailedAttempts > 0U && config->lockoutDurationMs == 0U) {
        return false;
    }

    return true;
}
