#ifndef IDIAGNOSTICS_MANAGER_H
#define IDIAGNOSTICS_MANAGER_H

#include "Core/IService.h"
#include "Management/DiagnosticsManager/DiagnosticsManagerConfig.h"
#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Diagnostics Manager Event Names
 */
#define EVENT_DIAG_SELFTEST_STARTED "diag.selftest.started"
#define EVENT_DIAG_SELFTEST_STEP    "diag.selftest.step"
#define EVENT_DIAG_SELFTEST_FINISHED "diag.selftest.finished"
#define EVENT_DIAG_HEALTH_CRITICAL  "diag.health.critical"
#define EVENT_DIAG_HEALTH_CHANGED   "diag.health.changed"
#define EVENT_DIAG_HEAP_WARNING     "diag.heap.warning"

/**
 * @brief Diagnostic status return codes.
 */
typedef enum {
    DIAG_OK = 0,
    DIAG_ERR_INVALID_ARG = -1,
    DIAG_ERR_INVALID_STATE = -2,
    DIAG_ERR_BUSY = -3,
    DIAG_ERR_NO_MEM = -4,
    DIAG_ERR_FULL = -5
} DiagResult_t;

/**
 * @brief System overall health status classification.
 */
typedef enum {
    DIAG_HEALTH_UNKNOWN = 0,
    DIAG_HEALTH_CRITICAL = 1,  // Health score < 50
    DIAG_HEALTH_WARNING = 2,   // Health score 50 - 79
    DIAG_HEALTH_HEALTHY = 3    // Health score 80 - 100
} DiagHealthStatus_t;

/**
 * @brief Diagnostic self-test execution result structure.
 *
 * Attributes:
 *     completed (bool): True only after a complete scan; zero checked services means unknown.
 *     is_running (bool): Indicates if an asynchronous test is currently executing.
 *     health_score (int32_t): Evaluated health score (0 to 100).
 *     services_checked (uint32_t): Total number of services evaluated.
 *     services_healthy (uint32_t): Count of services returning zero error health code.
 *     last_test_time (uint32_t): System uptime timestamp in seconds of the last test completion.
 *     last_failed_service (char[]): Name of the last service that failed health check.
 */
typedef struct {
    bool is_running;
    bool completed;
    int32_t health_score;
    uint32_t services_checked;
    uint32_t services_healthy;
    uint32_t last_test_time;
    char last_failed_service[DIAGNOSTICS_NAME_CAPACITY];
} DiagnosticsTestResult;

/**
 * @brief Comprehensive system health and memory metrics structure.
 *
 * Attributes:
 *     free_heap (uint32_t): Current free heap in bytes.
 *     minimum_free_heap (uint32_t): Minimum recorded free heap since boot in bytes.
 *     largest_free_block (uint32_t): Size of the largest contiguous allocatable heap block.
 *     uptime_seconds (uint32_t): System uptime in seconds.
 *     task_count (uint32_t): Total active FreeRTOS task count.
 *     health_score (int32_t): Current calculated health score (0-100).
 *     health_status (DiagHealthStatus_t): Categorized health level.
 */
typedef struct {
    uint32_t free_heap;
    uint32_t minimum_free_heap;
    uint32_t largest_free_block;
    uint32_t uptime_seconds;
    uint32_t task_count;
    int32_t health_score;
    DiagHealthStatus_t health_status;
} DiagnosticsDetailedMetrics;

/**
 * @brief Diagnostics Manager Interface Definition.
 */
typedef struct IDiagnosticsManager IDiagnosticsManager;

/* Custom callback contexts remain borrowed until Stop succeeds. Stop closes
 * admission and may return BUSY while a callback or EventBus dispatch is active.
 * Retain those contexts and retry; reinitialization cannot erase a live worker. */
struct IDiagnosticsManager {
    IService base;

    /**
     * @brief Initializes DiagnosticsManager with custom configuration.
     *
     * Args:
     *     self (IDiagnosticsManager*): Pointer to instance.
     *     config (DiagnosticsManagerConfig): Configuration structure.
     *     registry (struct IDIRegistry*): Dependency injection registry.
     *
     * Returns:
     *     int32_t: 0 on success, negative error code otherwise.
     */
    int32_t (*InitializeWithConfig)(IDiagnosticsManager *self,
                                    DiagnosticsManagerConfig config,
                                    struct IDIRegistry *registry);

    /**
     * @brief Deprecated synchronous self-test trigger. Runs asynchronously under the hood.
     *
     * Args:
     *     self (IDiagnosticsManager*): Pointer to instance.
     */
    void (*RunSelfTest)(IDiagnosticsManager *self);

    /**
     * @brief Starts an asynchronous self-test task in the background.
     *
     * Args:
     *     self (IDiagnosticsManager*): Pointer to instance.
     *
     * Returns:
     *     int32_t: 0 on successful launch, negative error code if already running or uninitialized.
     */
    int32_t (*StartAsyncSelfTest)(IDiagnosticsManager *self);

    /**
     * @brief Gets current system health score (0 to 100).
     *
     * Args:
     *     self (IDiagnosticsManager*): Pointer to instance.
     *
     * Returns:
     *     int32_t: Health score between 0 and 100.
     */
    int32_t (*GetSystemHealthScore)(IDiagnosticsManager *self);

    /**
     * @brief Gets system health status classification.
     *
     * Args:
     *     self (IDiagnosticsManager*): Pointer to instance.
     *
     * Returns:
     *     DiagHealthStatus_t: Health status category.
     */
    DiagHealthStatus_t (*GetHealthStatus)(IDiagnosticsManager *self);

    /**
     * @brief Retrieves the last completed self-test result structure.
     *
     * Args:
     *     self (IDiagnosticsManager*): Pointer to instance.
     *
     * Returns:
     *     DiagnosticsTestResult: Copy of the last test result.
     */
    DiagnosticsTestResult (*GetLastTestResult)(IDiagnosticsManager *self);

    /**
     * @brief Retrieves detailed system health and memory metrics.
     *
     * Args:
     *     self (IDiagnosticsManager*): Pointer to instance.
     *
     * Returns:
     *     DiagnosticsDetailedMetrics: Populated metrics structure.
     */
    DiagnosticsDetailedMetrics (*GetDetailedMetrics)(IDiagnosticsManager *self);

    /**
     * @brief Gets total current free heap memory in bytes.
     *
     * Args:
     *     self (IDiagnosticsManager*): Pointer to instance.
     *
     * Returns:
     *     uint32_t: Free heap size in bytes.
     */
    uint32_t (*GetFreeHeap)(IDiagnosticsManager *self);

    /**
     * @brief Gets current system uptime in seconds.
     *
     * Args:
     *     self (IDiagnosticsManager*): Pointer to instance.
     *
     * Returns:
     *     uint32_t: Uptime in seconds.
     */
    uint32_t (*GetSystemUptime)(IDiagnosticsManager *self);

    /**
     * @brief Registers a custom health check function (Plugin Pattern).
     *
     * Args:
     *     self (IDiagnosticsManager*): Pointer to instance.
     *     name (const char*): Human-readable name of component/check.
     *     check_fn (DiagnosticsCustomCheckFn): Function returning true if healthy.
     *     user_data (void*): Optional user context passed to check_fn.
     *
     * Returns:
     *     int32_t: 0 on success, negative error code if full or invalid.
     */
    int32_t (*RegisterCustomCheck)(IDiagnosticsManager *self,
                                    const char *name,
                                    DiagnosticsCustomCheckFn check_fn,
                                    void *user_data);

    /**
     * @brief Enables or disables runtime periodic background diagnostic checks.
     *
     * Args:
     *     self (IDiagnosticsManager*): Pointer to instance.
     *     enable (bool): True to enable background checks, false to disable.
     *     interval_ms (uint32_t): Periodic check interval in milliseconds (ignored if 0).
     *
     * Returns:
     *     int32_t: 0 on success, negative error code otherwise.
     */
    int32_t (*SetPeriodicTest)(IDiagnosticsManager *self, bool enable, uint32_t interval_ms);
};

/**
 * @brief Retrieves global singleton instance of DiagnosticsManager.
 *
 * Returns:
 *     IDiagnosticsManager*: Pointer to singleton diagnostics manager.
 */
IDiagnosticsManager *DiagnosticsManager_GetModule(void);

#endif // IDIAGNOSTICS_MANAGER_H

