#include "Core/Localization/Localization.h"
#include "Communication/DebugConsole/Core/ConsolePrivacy.h"
/**
 * @file SecurityManagerConsoleCommands.c
 * @brief Debug Console CLI commands for the SecurityManager module.
 *
 * Registers the "sec" command group with the DebugConsole service.
 * All subcommands use the ServiceLocator (SVC_NAME_SECURITY_MANAGER) for
 * module access, consistent with the project-wide console command pattern.
 *
 * Available subcommands:
 *   sec status            — Service state, lockout status, current config
 *   sec auth <token>      — Authenticate using the stored token
 *   sec pin-auth <pin>    — Authenticate using the stored PIN
 *   sec reset-lockout     — Administratively clear lockout state
 *   sec set-mode <0-3>    — Change authentication mode at runtime
 *   sec self-test         — Run built-in diagnostic self-test
 *   sec stats             — Dump SecurityStats_t counters
 *   sec set-token <token> — Update the stored authentication token
 *   sec set-pin <pin>     — Update the stored PIN
 *   sec get-config        — Dump full runtime configuration
 *
 * @author RaCool DeepMind Team
 * @date   2026-08-06
 */

#include "Management/SecurityManager/SecurityManagerConsoleCommands.h"
#include "Management/SecurityManager/ISecurityManager.h"
#include "Core/ServiceLocator/ServiceLocator.h"
#include "Core/ServiceLocator/ServiceNames.h"
#include "Communication/DebugConsole/IDebugConsole.h"
#include "esp_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef RACOOL_TEST_NATIVE
#include <esp_console.h>
#endif

/* ─────────────────────────────────────────────────────────────────────────────
 * Internal Constants & Helpers
 * ───────────────────────────────────────────────────────────────────────────*/

static const char *SEC_CMD_TAG = "SecurityConsole";


/**
 * @brief Resolves the SecurityManager service via ServiceLocator.
 *
 * Prints an error message and returns NULL if the service is unavailable.
 *
 * @return Pointer to ISecurityManager, or NULL if not registered.
 */
static ISecurityManager *Sec_GetService(const LocalizationCatalog *catalog) {
    ISecurityManager *sm = (ISecurityManager *)ServiceLocator_GetService(SVC_NAME_SECURITY_MANAGER);
    if (sm == NULL) {
        LOC_PRINTF(catalog, LOC_CLI_SEC_UNAVAILABLE, "[sec] ERROR: SecurityManager service not available.\n");
    }
    return sm;
}

/**
 * @brief Returns a human-readable string for the given AuthMode enum value.
 *
 * @param mode AuthMode enum value.
 * @return Constant string describing the mode.
 */
static const char *AuthModeToString(AuthMode mode, const LocalizationCatalog *catalog) {
    switch (mode) {
        case AUTH_MODE_NONE:       return Localization_Text(catalog,LOC_CLI_SEC_MODE_NONE);
        case AUTH_MODE_TOKEN:      return Localization_Text(catalog,LOC_CLI_SEC_MODE_TOKEN);
        case AUTH_MODE_PIN:        return Localization_Text(catalog,LOC_CLI_SEC_MODE_PIN);
        case AUTH_MODE_HARDCODED:  return Localization_Text(catalog,LOC_CLI_SEC_MODE_LEGACY);
        default:                   return Localization_Text(catalog,LOC_COMMON_UNKNOWN);
    }
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Command Handler
 * ───────────────────────────────────────────────────────────────────────────*/

/**
 * @brief Main dispatch handler for all "sec" subcommands.
 *
 * Routes argc/argv to the appropriate ISecurityManager method and prints
 * human-readable output. Returns 0 on success, -1 on error.
 *
 * @param argc Argument count (argv[0] is "sec").
 * @param argv Argument vector.
 * @return 0 on success; -1 on error or unknown subcommand.
 */
static int sec_console_cmd_handler(int argc, char **argv) {
    const LocalizationCatalog *catalog = Localization_Snapshot();
    if (argc < 2 || !argv || !argv[0] || !argv[1]) {
        LOC_PRINTF(catalog, LOC_CLI_SEC_USAGE, "Usage: sec <status|auth|pin-auth|reset-lockout|set-mode|self-test|\n");
        LOC_PRINTF(catalog, LOC_CLI_SEC_USAGE_TAIL, "            stats|set-token|set-pin|get-config>\n");
        LOC_PRINTF(catalog, LOC_CLI_SEC_USAGE_ARGUMENTS, "       sec <subcommand> [args...]\n");
        return -1;
    }
    if (argc == 3 && argv[2] && strcmp(argv[2], "--machine") == 0) {
        const bool retired = strcmp(argv[1], "session-token") == 0 || strcmp(argv[1], "invalidate-session") == 0;
        printf("RACOOL_CLI v=1 type=security_command result=rejected reason=%s\n", retired ? "retired" : "unsupported");
        return -1;
    }
    bool takes_value = strcmp(argv[1], "auth") == 0 || strcmp(argv[1], "pin-auth") == 0 ||
                       strcmp(argv[1], "set-mode") == 0 || strcmp(argv[1], "set-token") == 0 ||
                       strcmp(argv[1], "set-pin") == 0;
    if (argc != (takes_value ? 3 : 2) || (takes_value && !argv[2])) {
        LOC_PRINTF(catalog, LOC_CLI_SEC_ARGUMENTS, "[sec] Invalid arguments; use sec <subcommand> [value]\n");
        return -1;
    }


    /* ── status ───────────────────────────────────────────────────────────── */
    if (strcmp(argv[1], "status") == 0) {
        ISecurityManager *sm = Sec_GetService(catalog);
        if (!sm || !sm->IsLockedOut || !sm->GetLockoutRemainingMs || !sm->GetFailedAttemptCount || !sm->GetConfig || !sm->base.GetState) return -1;

        bool     locked    = sm->IsLockedOut(sm);
        uint32_t remaining = sm->GetLockoutRemainingMs(sm);
        SecurityManagerConfig cfg;
        memset(&cfg, 0, sizeof(cfg));
        if (sm->GetConfig(sm, &cfg) != SECURITY_STATUS_OK) {
            LOC_PRINTF(catalog, LOC_CLI_SEC_CONFIG_FAILED, "[sec] GetConfig failed\n");
            ConsolePrivacy_Clear(&cfg, sizeof(cfg));
            return -1;
        }

        const char *stateStr = (sm->base.GetState(&sm->base) == SERVICE_STATE_STARTED)
                               ? Localization_Text(catalog,LOC_CLI_SEC_STARTED) : Localization_Text(catalog,LOC_CLI_SEC_STOPPED);
        LOC_PRINTF(catalog, LOC_CLI_SEC_TITLE, "--- SecurityManager Status ---\n");
        LOC_PRINTF(catalog, LOC_CLI_SEC_STATE, "  Service State   : %s\n",          stateStr);
        LOC_PRINTF(catalog, LOC_CLI_SEC_LOCKED, "  Locked Out      : %s\n",          locked ? Localization_Text(catalog,LOC_COMMON_YES) : Localization_Text(catalog,LOC_COMMON_NO));
        LOC_PRINTF(catalog, LOC_CLI_SEC_REMAINING, "  Lockout Remain  : %lu ms\n",      (unsigned long)remaining);
        LOC_PRINTF(catalog, LOC_CLI_SEC_ATTEMPTS_FAILED, "  Failed Attempts : %u\n",          sm->GetFailedAttemptCount(sm));
        LOC_PRINTF(catalog, LOC_CLI_SEC_MODE, "  Auth Mode       : %s (%d)\n",     AuthModeToString(cfg.authMode,catalog), cfg.authMode);
        LOC_PRINTF(catalog, LOC_CLI_SEC_MAXIMUM, "  Max Attempts    : %u\n",          cfg.maxFailedAttempts);
        LOC_PRINTF(catalog, LOC_CLI_SEC_LEGACY_TIMEOUT, "  Legacy Timeout  : %lu ms (reserved)\n", (unsigned long)cfg.authTimeoutMs);
        LOC_PRINTF(catalog, LOC_CLI_SEC_LOCK_DURATION, "  Lockout Duration: %lu ms\n",      (unsigned long)cfg.lockoutDurationMs);
        LOC_PRINTF(catalog, LOC_CLI_SEC_LOGGING, "  Sec Logging     : %s\n",          cfg.enableSecurityLogging ? Localization_Text(catalog,LOC_COMMON_ON) : Localization_Text(catalog,LOC_COMMON_OFF));
        ConsolePrivacy_Clear(&cfg, sizeof(cfg));
        return 0;
    }

    /* ── auth ─────────────────────────────────────────────────────────────── */
    if (strcmp(argv[1], "auth") == 0) {
        if (argc < 3) {
            LOC_PRINTF(catalog, LOC_CLI_SEC_AUTH_USAGE, "Usage: sec auth <token>\n");
            return 0;
        }
        ISecurityManager *sm = Sec_GetService(catalog);
        if (!sm || !sm->Authenticate) return -1;

        bool ok = sm->Authenticate(sm, argv[2]);
        LOC_PRINTF(catalog, LOC_CLI_SEC_AUTH, "[sec] Token auth: %s\n", ok ? Localization_Text(catalog,LOC_CLI_SEC_SUCCESS) : Localization_Text(catalog,LOC_CLI_SEC_FAILED));
        return ok ? 0 : -1;
    }

    /* ── pin-auth ─────────────────────────────────────────────────────────── */
    if (strcmp(argv[1], "pin-auth") == 0) {
        if (argc < 3) {
            LOC_PRINTF(catalog, LOC_CLI_SEC_PIN_USAGE, "Usage: sec pin-auth <pin>\n");
            return 0;
        }
        ISecurityManager *sm = Sec_GetService(catalog);
        if (!sm || !sm->AuthenticatePin) return -1;

        bool ok = sm->AuthenticatePin(sm, argv[2]);
        LOC_PRINTF(catalog, LOC_CLI_SEC_PIN, "[sec] PIN auth: %s\n", ok ? Localization_Text(catalog,LOC_CLI_SEC_SUCCESS) : Localization_Text(catalog,LOC_CLI_SEC_FAILED));
        return ok ? 0 : -1;
    }

    /* ── reset-lockout ────────────────────────────────────────────────────── */
    if (strcmp(argv[1], "reset-lockout") == 0) {
        ISecurityManager *sm = Sec_GetService(catalog);
        if (!sm || !sm->ResetLockout) return -1;

        int32_t res = sm->ResetLockout(sm);
        LOC_PRINTF(catalog, LOC_CLI_SEC_LOCK_RESET, "[sec] Lockout reset: %s\n", (res == SECURITY_STATUS_OK) ? Localization_Text(catalog,LOC_CLI_SEC_OK) : Localization_Text(catalog,LOC_CLI_SEC_FAILED));
        return (res == SECURITY_STATUS_OK) ? 0 : -1;
    }

    /* ── set-mode ─────────────────────────────────────────────────────────── */
    if (strcmp(argv[1], "set-mode") == 0) {
        if (argc < 3) {
            LOC_PRINTF(catalog, LOC_CLI_SEC_MODE_USAGE, "Usage: sec set-mode <0:NONE|1:TOKEN|2:PIN|3:HARDCODED>\n");
            return 0;
        }
        ISecurityManager *sm = Sec_GetService(catalog);
        if (!sm || !sm->GetConfig || !sm->UpdateConfig) return -1;

        int mode = (argv[2][0] >= '0' && argv[2][0] <= '3' && argv[2][1] == '\0')
                       ? argv[2][0] - '0' : -1;
        if (mode < AUTH_MODE_NONE || mode > AUTH_MODE_HARDCODED) {
            LOC_PRINTF(catalog, LOC_CLI_SEC_INVALID_MODE, "[sec] Invalid mode %d (valid: 0-3)\n", mode);
            return -1;
        }
        SecurityManagerConfig cfg;
        memset(&cfg, 0, sizeof(cfg));
        if (sm->GetConfig(sm, &cfg) != SECURITY_STATUS_OK) {
            LOC_PRINTF(catalog, LOC_CLI_SEC_CONFIG_UNCHANGED, "[sec] GetConfig failed; settings unchanged\n");
            ConsolePrivacy_Clear(&cfg, sizeof(cfg));
            return -1;
        }
        cfg.authMode = (AuthMode)mode;
        int32_t res = sm->UpdateConfig(sm, &cfg);
        LOC_PRINTF(catalog, LOC_CLI_SEC_MODE_SET, "[sec] Auth mode set to %s (%d): %s\n",
               AuthModeToString((AuthMode)mode,catalog), mode,
               (res == SECURITY_STATUS_OK) ? Localization_Text(catalog,LOC_CLI_SEC_OK) : Localization_Text(catalog,LOC_CLI_SEC_FAILED));
        ConsolePrivacy_Clear(&cfg, sizeof(cfg));
        return (res == SECURITY_STATUS_OK) ? 0 : -1;
    }

    /* ── self-test ────────────────────────────────────────────────────────── */
    if (strcmp(argv[1], "self-test") == 0) {
        ISecurityManager *sm = Sec_GetService(catalog);
        if (!sm || !sm->SelfTest) return -1;

        LOC_PRINTF(catalog, LOC_CLI_SEC_TEST_START, "[sec] Running self-test...\n");
        int32_t res = sm->SelfTest(sm);
        LOC_PRINTF(catalog, LOC_CLI_SEC_TEST_RESULT, "[sec] Self-test: %s (code: %ld)\n",
               (res == SECURITY_STATUS_OK) ? "PASSED" : Localization_Text(catalog,LOC_CLI_SEC_FAILED), (long)res);
        return (res == SECURITY_STATUS_OK) ? 0 : -1;
    }

    /* ── stats ────────────────────────────────────────────────────────────── */
    if (strcmp(argv[1], "stats") == 0) {
        ISecurityManager *sm = Sec_GetService(catalog);
        if (!sm || !sm->GetStats) return -1;

        SecurityStats_t stats;
        memset(&stats, 0, sizeof(stats));
        int32_t res = sm->GetStats(sm, &stats);
        if (res != SECURITY_STATUS_OK) {
            LOC_PRINTF(catalog, LOC_CLI_SEC_STATS_FAILED, "[sec] GetStats failed (%ld)\n", (long)res);
            return -1;
        }
        LOC_PRINTF(catalog, LOC_CLI_SEC_STATS_TITLE, "--- SecurityManager Statistics ---\n");
        LOC_PRINTF(catalog, LOC_CLI_SEC_AUTH_SUCCESSES, "  Auth Successes      : %lu\n",   (unsigned long)stats.totalAuthSuccesses);
        LOC_PRINTF(catalog, LOC_CLI_SEC_AUTH_FAILURES, "  Auth Failures       : %lu\n",   (unsigned long)stats.totalAuthFailures);
        LOC_PRINTF(catalog, LOC_CLI_SEC_LOCKOUTS, "  Total Lockouts      : %lu\n",   (unsigned long)stats.totalLockouts);
        LOC_PRINTF(catalog, LOC_CLI_SEC_LAST_SUCCESS, "  Last Auth Success   : %llu us\n",(unsigned long long)stats.lastAuthSuccessTimeUs);
        LOC_PRINTF(catalog, LOC_CLI_SEC_LAST_LOCKOUT, "  Last Lockout        : %llu us\n",(unsigned long long)stats.lastLockoutTimeUs);
        return 0;
    }

    /* ── set-token ────────────────────────────────────────────────────────── */
    if (strcmp(argv[1], "set-token") == 0) {
        if (argc < 3) {
            LOC_PRINTF(catalog, LOC_CLI_SEC_TOKEN_USAGE, "Usage: sec set-token <new_token>\n");
            return 0;
        }
        ISecurityManager *sm = Sec_GetService(catalog);
        if (!sm || !sm->GetConfig || !sm->UpdateConfig) return -1;

        SecurityManagerConfig cfg;
        memset(&cfg, 0, sizeof(cfg));
        if (sm->GetConfig(sm, &cfg) != SECURITY_STATUS_OK) {
            LOC_PRINTF(catalog, LOC_CLI_SEC_CONFIG_UNCHANGED, "[sec] GetConfig failed; settings unchanged\n");
            ConsolePrivacy_Clear(&cfg, sizeof(cfg));
            return -1;
        }
        size_t length = strnlen(argv[2], sizeof(cfg.token));
        if (length >= sizeof(cfg.token)) {
            LOC_PRINTF(catalog, LOC_CLI_SEC_TOO_LONG, "[sec] Value too long; settings unchanged\n");
            ConsolePrivacy_Clear(&cfg, sizeof(cfg));
            return -1;
        }
        memset(cfg.token, 0, sizeof(cfg.token));
        memcpy(cfg.token, argv[2], length);

        int32_t res = sm->UpdateConfig(sm, &cfg);
        LOC_PRINTF(catalog, LOC_CLI_SEC_TOKEN_UPDATED, "[sec] Token updated: %s\n", (res == SECURITY_STATUS_OK) ? Localization_Text(catalog,LOC_CLI_SEC_OK) : Localization_Text(catalog,LOC_CLI_SEC_VALIDATION_FAILED));
        ConsolePrivacy_Clear(&cfg, sizeof(cfg));
        return (res == SECURITY_STATUS_OK) ? 0 : -1;
    }

    /* ── set-pin ──────────────────────────────────────────────────────────── */
    if (strcmp(argv[1], "set-pin") == 0) {
        if (argc < 3) {
            LOC_PRINTF(catalog, LOC_CLI_SEC_SET_PIN_USAGE, "Usage: sec set-pin <new_pin>\n");
            return 0;
        }
        ISecurityManager *sm = Sec_GetService(catalog);
        if (!sm || !sm->GetConfig || !sm->UpdateConfig) return -1;

        SecurityManagerConfig cfg;
        memset(&cfg, 0, sizeof(cfg));
        if (sm->GetConfig(sm, &cfg) != SECURITY_STATUS_OK) {
            LOC_PRINTF(catalog, LOC_CLI_SEC_CONFIG_UNCHANGED, "[sec] GetConfig failed; settings unchanged\n");
            ConsolePrivacy_Clear(&cfg, sizeof(cfg));
            return -1;
        }
        size_t length = strnlen(argv[2], sizeof(cfg.pin));
        if (length >= sizeof(cfg.pin)) {
            LOC_PRINTF(catalog, LOC_CLI_SEC_TOO_LONG, "[sec] Value too long; settings unchanged\n");
            ConsolePrivacy_Clear(&cfg, sizeof(cfg));
            return -1;
        }
        memset(cfg.pin, 0, sizeof(cfg.pin));
        memcpy(cfg.pin, argv[2], length);

        int32_t res = sm->UpdateConfig(sm, &cfg);
        LOC_PRINTF(catalog, LOC_CLI_SEC_PIN_UPDATED, "[sec] PIN updated: %s\n", (res == SECURITY_STATUS_OK) ? Localization_Text(catalog,LOC_CLI_SEC_OK) : Localization_Text(catalog,LOC_CLI_SEC_VALIDATION_FAILED));
        ConsolePrivacy_Clear(&cfg, sizeof(cfg));
        return (res == SECURITY_STATUS_OK) ? 0 : -1;
    }

    /* ── get-config ───────────────────────────────────────────────────────── */
    if (strcmp(argv[1], "get-config") == 0) {
        ISecurityManager *sm = Sec_GetService(catalog);
        if (!sm || !sm->GetConfig) return -1;

        SecurityManagerConfig cfg;
        memset(&cfg, 0, sizeof(cfg));
        int32_t res = sm->GetConfig(sm, &cfg);
        if (res != SECURITY_STATUS_OK) {
            LOC_PRINTF(catalog, LOC_CLI_SEC_CONFIG_ERROR, "[sec] GetConfig failed (%ld)\n", (long)res);
            ConsolePrivacy_Clear(&cfg, sizeof(cfg));
            return -1;
        }
        LOC_PRINTF(catalog, LOC_CLI_SEC_CONFIG_TITLE, "--- SecurityManager Configuration ---\n");
        LOC_PRINTF(catalog, LOC_CLI_SEC_CONFIG_MODE, "  Auth Mode        : %s (%d)\n",    AuthModeToString(cfg.authMode,catalog), cfg.authMode);
        LOC_PRINTF(catalog, LOC_CLI_SEC_CONFIG_TIMEOUT, "  Legacy Timeout   : %lu ms (reserved)\n", (unsigned long)cfg.authTimeoutMs);
        LOC_PRINTF(catalog, LOC_CLI_SEC_CONFIG_MAXIMUM, "  Max Attempts     : %u\n",         cfg.maxFailedAttempts);
        LOC_PRINTF(catalog, LOC_CLI_SEC_CONFIG_LOCK, "  Lockout Duration : %lu ms\n",     (unsigned long)cfg.lockoutDurationMs);
        LOC_PRINTF(catalog, LOC_CLI_SEC_CONFIG_LOGGING, "  Sec Logging      : %s\n",         cfg.enableSecurityLogging ? Localization_Text(catalog,LOC_COMMON_ON) : Localization_Text(catalog,LOC_COMMON_OFF));
        LOC_PRINTF(catalog, LOC_CLI_SEC_MIN_TOKEN, "  Min Token Length : %u\n",         cfg.minTokenLength);
        LOC_PRINTF(catalog, LOC_CLI_SEC_MIN_PIN, "  Min PIN Length   : %u\n",         cfg.minPinLength);
        /* Credentials are intentionally not printed for security. */
        LOC_PRINTF(catalog, LOC_CLI_SEC_REDACTED_TOKEN, "  Token            : [REDACTED]\n");
        LOC_PRINTF(catalog, LOC_CLI_SEC_REDACTED_PIN, "  PIN              : [REDACTED]\n");
        ConsolePrivacy_Clear(&cfg, sizeof(cfg));
        return 0;
    }

    /* ── Unknown subcommand ───────────────────────────────────────────────── */
    LOC_PRINTF(catalog, LOC_CLI_SEC_UNKNOWN, "[sec] Unknown subcommand\n");
    LOC_PRINTF(catalog, LOC_CLI_SEC_VALID_COMMANDS, "Valid: status, auth, pin-auth, reset-lockout, set-mode, self-test,\n");
    LOC_PRINTF(catalog, LOC_CLI_SEC_VALID_TAIL, "       stats, set-token, set-pin, get-config\n");
    return -1;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Command Registration
 * ───────────────────────────────────────────────────────────────────────────*/

esp_err_t SecurityManager_RegisterConsoleCommands(void) {
    IDebugConsole *console = (IDebugConsole *)ServiceLocator_GetService(SVC_NAME_DEBUG_CONSOLE);
    if (!console || !console->RegisterCommandWithCategory) return ESP_ERR_INVALID_STATE;

    static const esp_console_cmd_t sec_cmd = {
        .command  = "sec",
        .help     = "SecurityManager diagnostic and control commands",
        .hint     = "<status|auth|pin-auth|reset-lockout|set-mode|self-test|stats|"
                    "set-token|set-pin|get-config>",
        .func     = &sec_console_cmd_handler,
        .argtable = NULL,
    };

    esp_err_t result = console->RegisterCommandWithCategory(console, &sec_cmd, CMD_CAT_SYSTEM);
    if (result != ESP_OK) return result;
    ESP_LOGI(SEC_CMD_TAG, "SecurityManager console commands registered successfully");
    return ESP_OK;
}
