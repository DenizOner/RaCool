#ifndef DIAGNOSTICS_MANAGER_CONFIG_H
#define DIAGNOSTICS_MANAGER_CONFIG_H

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Maximum number of critical services that can be monitored.
 */
#define DIAGNOSTICS_MAX_CRITICAL_SERVICES 16

/**
 * @brief Maximum number of custom health check callbacks that can be registered.
 */
#define DIAGNOSTICS_MAX_CUSTOM_CHECKS 8
#define DIAGNOSTICS_NAME_CAPACITY 64
#define DIAGNOSTICS_LOCK_TIMEOUT_MS 250
#define DIAGNOSTICS_STOP_TIMEOUT_MS 2000
#define DIAGNOSTICS_WORKER_POLL_MS 100
#define DIAGNOSTICS_STARTUP_READY_TIMEOUT_MS 10000
#define DIAGNOSTICS_ZIGBEE_RECOVERY_GRACE_MS 30000
#define DIAGNOSTICS_EMERGENCY_DELAY_MS 1000
#define DIAGNOSTICS_EMERGENCY_TEMPERATURE_C 95.0f
#define DIAGNOSTICS_MIN_PERIOD_MS 1000
#define DIAGNOSTICS_MAX_PERIOD_MS 86400000
#define DIAGNOSTICS_MIN_STACK_BYTES 4096
#define DIAGNOSTICS_MAX_STACK_BYTES 32768

/**
 * @brief Custom health check function signature.
 *
 * Args:
 *     user_data (void*): Pointer to optional user context or NULL.
 *
 * Returns:
 *     bool: true if component is healthy, false otherwise.
 */
typedef bool (*DiagnosticsCustomCheckFn)(void *user_data);

/**
 * @brief Named custom health check item.
 */
typedef struct {
    const char *name;
    DiagnosticsCustomCheckFn check_fn;
    void *user_data;
} DiagnosticsCustomCheckItem;

/**
 * @brief Configuration structure for the DiagnosticsManager.
 *
 * Attributes:
 *     criticalServices (const char*[]): Names of critical services to monitor via ServiceLocator.
 *     criticalServiceCount (uint32_t): Number of valid critical service names.
 *     enablePeriodicTest (bool): Enables automatic background periodic diagnostic checks.
 *     periodicIntervalMs (uint32_t): Time interval between background diagnostic runs in milliseconds.
 *     serviceCheckTimeoutMs (uint32_t): Timeout allocated per service check in milliseconds.
 *     minFreeHeapThreshold (uint32_t): Critical heap memory threshold in bytes.
 *     minFreeBlockThreshold (uint32_t): Minimum contiguous free heap block threshold in bytes.
 *     taskStackSize (uint32_t): Stack size allocated for asynchronous diagnostic tasks.
 *     taskPriority (uint32_t): Task priority for the diagnostic background worker.
 */
typedef struct {
    const char *criticalServices[DIAGNOSTICS_MAX_CRITICAL_SERVICES];
    uint32_t criticalServiceCount;
    
    bool enablePeriodicTest;
    uint32_t periodicIntervalMs;
    uint32_t serviceCheckTimeoutMs;
    uint32_t minFreeHeapThreshold;
    uint32_t minFreeBlockThreshold;
    
    uint32_t taskStackSize;
    uint32_t taskPriority;
} DiagnosticsManagerConfig;

/**
 * @brief Gets the default configuration for the DiagnosticsManager.
 *
 * Returns:
 *     DiagnosticsManagerConfig: Pre-populated configuration structure with safe default values.
 */
DiagnosticsManagerConfig DiagnosticsManagerConfig_GetDefault(void);

#endif // DIAGNOSTICS_MANAGER_CONFIG_H

