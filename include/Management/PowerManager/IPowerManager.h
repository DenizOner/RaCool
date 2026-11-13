#ifndef IPOWER_MANAGER_H
#define IPOWER_MANAGER_H

#include "Core/IService.h"
#include "Management/PowerManager/PowerManagerConfig.h"
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/**
 * @file IPowerManager.h
 * @brief Power Manager Interface definition.
 * 
 * Provides power mode management, explicit light/deep sleep,
 * power lock acquisition, event-driven sleep notifications, and reset telemetry.
 */

/**
 * @brief Power Manager Event Names for EventBus
 */
#define EVENT_POWER_STATE_CHANGED    "power.state_changed"
#define EVENT_POWER_MODE_CHANGED     "power.mode_changed"
#define EVENT_POWER_PREPARE_SLEEP    "power.prepare_sleep"
#define EVENT_POWER_ENTERING_SLEEP   "power.entering_sleep"
#define EVENT_POWER_WAKEUP          "power.wakeup"
#define EVENT_POWER_RESTART_REQUEST  "power.restart_requested"
#define EVENT_POWER_RESET_REASON     "power.reset_reason"

/**
 * @brief Power Manager Operation Return Codes
 */
typedef enum {
    POWER_OK = 0,
    POWER_ERR_INVALID_ARG = -1,
    POWER_ERR_INVALID_STATE = -2,
    POWER_ERR_BUSY = -3,
    POWER_ERR_NO_MEM = -4,
    POWER_ERR_NOT_SUPPORTED = -5,
    POWER_ERR_LOCK_FAILED = -6,
    POWER_ERR_TIMEOUT = -7
} PowerResult_t;

/**
 * @brief System Power Management Operational Modes
 */
typedef enum {
    POWER_MODE_UNKNOWN     = -1, /**< No successfully applied profile is known. */
    POWER_MODE_PERFORMANCE = 0, /**< High CPU performance (160 MHz), no auto light sleep */
    POWER_MODE_BALANCED    = 1, /**< Dynamic frequency scaling (80-160 MHz), router remains awake */
    POWER_MODE_LOW_POWER   = 2, /**< Dynamic frequency scaling (40-80 MHz), router remains awake */
    POWER_MODE_LIGHT_SLEEP = 3, /**< Deeply gated clock / transient light sleep */
    POWER_MODE_DEEP_SLEEP  = 4  /**< Full RTC deep sleep mode */
} PowerMode_t;

/**
 * @brief Power Reset Reasons
 */
typedef enum {
    POWER_RESET_UNKNOWN = 0,
    POWER_RESET_POWERON,
    POWER_RESET_EXT,
    POWER_RESET_SW,
    POWER_RESET_PANIC,
    POWER_RESET_INT_WDT,
    POWER_RESET_TASK_WDT,
    POWER_RESET_WDT,
    POWER_RESET_DEEPSLEEP,
    POWER_RESET_BROWNOUT,
    POWER_RESET_SDIO
} PowerResetReason_t;

/**
 * @brief Wakeup Source Classifications
 */
typedef enum {
    POWER_WAKEUP_UNDEFINED = 0,
    POWER_WAKEUP_ALL,
    POWER_WAKEUP_EXT0,
    POWER_WAKEUP_EXT1,
    POWER_WAKEUP_TIMER,
    POWER_WAKEUP_TOUCHPAD,
    POWER_WAKEUP_ULP,
    POWER_WAKEUP_GPIO,
    POWER_WAKEUP_UART,
    POWER_WAKEUP_WIFI,
    POWER_WAKEUP_COCPU
} PowerWakeupSource_t;

/**
 * @brief Power Manager Operational Statistics
 */
typedef struct {
    uint32_t totalRestarts;         /**< Count of software restart requests */
    uint32_t totalSleeps;           /**< Count of sleep entries */
    uint32_t totalWakeups;          /**< Count of wakeups recorded */
    uint32_t activePowerLocks;      /**< Currently held active power locks count */
    PowerMode_t currentMode;        /**< Current operational power mode */
    PowerResetReason_t resetReason; /**< Last system reset reason */
    PowerWakeupSource_t wakeCause;  /**< Last wakeup cause */
    uint32_t uptimeSeconds;         /**< System uptime in seconds */
} PowerStats_t;

/**
 * @brief Power Manager Interface Structure
 */
typedef enum {
    POWER_LOCK_CPU_MAX = 0,
    POWER_LOCK_APB_MAX = 1,
    POWER_LOCK_NO_LIGHT_SLEEP = 2,
} PowerLockKind;

typedef struct {
    char name[POWER_LOCK_NAME_CAPACITY];
    PowerLockKind kind;
    uint32_t references;
} PowerLockSnapshot;

typedef struct IPowerManager IPowerManager;

struct IPowerManager {
    IService base;

    /**
     * @brief Initializes PowerManager with specific configuration and DI registry.
     * 
     * @param self Pointer to IPowerManager instance.
     * @param config Custom PowerManager configuration struct.
     * @param registry DI Registry handle.
     * @return POWER_OK on success, or appropriate PowerResult_t error code.
     */
    int32_t (*InitializeWithConfig)(IPowerManager *self,
                                    PowerManagerConfig config,
                                    struct IDIRegistry *registry);

    /**
     * @brief Enters deep sleep mode after sending pre-sleep events.
     * 
     * @param self Pointer to IPowerManager instance.
     * @param duration_us Sleep duration in microseconds, at most POWER_SLEEP_MAX_DURATION_US (0 for indefinite).
     * @return Preparation failure code; incomplete dependency restoration returns
     * POWER_ERR_INVALID_STATE. Successful SDK deep sleep does not return.
     */
    int32_t (*EnterDeepSleep)(IPowerManager *self, uint64_t duration_us);

    /**
     * @brief Enters light sleep mode safely with pre-sleep events.
     * 
     * @param self Pointer to IPowerManager instance.
     * @param duration_us Positive timer-backed duration, at most POWER_SLEEP_MAX_DURATION_US microseconds.
     * @return POWER_OK on success, error code otherwise.
     */
    int32_t (*EnterLightSleep)(IPowerManager *self, uint64_t duration_us);

    /**
     * @brief Triggers system restart (synchronous or deferred via async event).
     * 
     * @param self Pointer to IPowerManager instance.
     */
    void (*Restart)(IPowerManager *self);

    /**
     * @brief Triggers deferred system restart after giving components time to handle pre-restart events.
     * 
     * @param self Pointer to IPowerManager instance.
     * @param delay_ms Delay in milliseconds before executing restart.
     * @return POWER_OK on success.
     */
    int32_t (*RestartAsync)(IPowerManager *self, uint32_t delay_ms);
    /** True while unavailable or an admitted sleep/restart is pending. */
    bool (*IsTransitionPending)(IPowerManager *self);

    /**
     * @brief Sets current power management operational mode.
     * 
     * @param self Pointer to IPowerManager instance.
     * @param mode Desired PowerMode_t profile.
     * @return POWER_OK on success, error code otherwise.
     */
    int32_t (*SetPowerMode)(IPowerManager *self, PowerMode_t mode);

    /**
     * @brief Gets current power management operational mode.
     * 
     * @param self Pointer to IPowerManager instance.
     * @return Current PowerMode_t enum value.
     */
    PowerMode_t (*GetPowerMode)(IPowerManager *self);

    /**
     * @brief Acquires power lock to prevent lower power / light sleep state during critical tasks.
     * 
     * @param self Pointer to IPowerManager instance.
     * @param lock_name Descriptive tag name for the lock owner.
     * @return POWER_OK on success, error code otherwise.
     */
    int32_t (*AcquirePowerLock)(IPowerManager *self, const char *lock_name);

    /**
     * @brief Releases previously acquired power lock.
     * 
     * @param self Pointer to IPowerManager instance.
     * @param lock_name Descriptive tag name for the lock owner.
     * @return POWER_OK on success, error code otherwise.
     */
    int32_t (*ReleasePowerLock)(IPowerManager *self, const char *lock_name);

    /** Acquire a named SDK lock; a name cannot change kind while allocated. */
    int32_t (*AcquireTypedPowerLock)(IPowerManager *self, const char *lock_name, PowerLockKind kind);

    /** Copy active application lock owners under the manager lock. No SDK handles escape.
     * On insufficient capacity, return NO_MEM and the required count without partial output.
     */
    int32_t (*GetLockSnapshot)(IPowerManager *self, PowerLockSnapshot *entries, size_t capacity, size_t *count);

    /**
     * @brief Retrieves operational stats and counters.
     * 
     * @param self Pointer to IPowerManager instance.
     * @param stats Pointer to output PowerStats_t structure.
     * @return POWER_OK on success, error code otherwise.
     */
    int32_t (*GetStats)(IPowerManager *self, PowerStats_t *stats);

    /**
     * @brief Returns system reset reason enum.
     * 
     * @param self Pointer to IPowerManager instance.
     * @return PowerResetReason_t enum value.
     */
    PowerResetReason_t (*GetResetReason)(IPowerManager *self);

    /**
     * @brief Returns system reset reason description string.
     * 
     * @param self Pointer to IPowerManager instance.
     * @return Const char string describing reset reason.
     */
    const char *(*GetResetReasonString)(IPowerManager *self);

    /**
     * @brief Returns last sleep wakeup cause.
     * 
     * @param self Pointer to IPowerManager instance.
     * @return PowerWakeupSource_t enum value.
     */
    PowerWakeupSource_t (*GetWakeupCause)(IPowerManager *self);

    /**
     * @brief Runs self-test sequence to evaluate power manager health.
     * 
     * @param self Pointer to IPowerManager instance.
     * @return POWER_OK if all tests pass, error code otherwise.
     */
    int32_t (*SelfTest)(IPowerManager *self);
};

/**
 * @brief Retrieves singleton instance of PowerManager.
 * 
 * @return Pointer to IPowerManager interface.
 */
IPowerManager *PowerManager_GetInstance(void);

#endif // IPOWER_MANAGER_H
