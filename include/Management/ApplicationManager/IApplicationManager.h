/**
 * @file IApplicationManager.h
 * @brief Interface definition for the ApplicationManager (Facade/Orchestrator) module.
 *
 * Provides lifecycle management, async event dispatching, system event handling,
 * diagnostic status reporting, and high-level application control functions for RaCool IoT platform.
 *
 * @author RaCool Team
 * @date 2026
 */

#ifndef IAPPLICATION_MANAGER_H
#define IAPPLICATION_MANAGER_H

#include <stdbool.h>
#include <stdint.h>
#include "Core/IService.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Event IDs supported by ApplicationManager internal event queue.
 */
typedef enum {
    APP_EVENT_NONE = 0,         /**< No event / invalid sentinel */
    APP_EVENT_ROTATION,         /**< Encoder rotation event */
    APP_EVENT_BUTTON_CLICK,     /**< Encoder button click event */
    APP_EVENT_BUTTON_LONGPRESS, /**< Encoder button long press event */
    APP_EVENT_TRIGGER_TELEMETRY,/**< Reserved legacy action; currently ESP_ERR_NOT_SUPPORTED. */
    APP_EVENT_SET_TARGET_TEMP,  /**< Reserved legacy action; currently ESP_ERR_NOT_SUPPORTED. */
    APP_EVENT_TOGGLE_RELAY,     /**< Request relay toggle state */
    APP_EVENT_SCREEN_WAKE,      /**< Explicit screen wake request */
    APP_EVENT_FACTORY_RESET     /**< Trigger factory reset sequence */
} AppManagerEventId;

/**
 * @brief Data payload union associated with AppManagerEvent.
 */
typedef union {
    int32_t rawValue;           /**< Generic integer parameter or encoder step count */
    float targetTemperature;   /**< Target temperature in degrees Celsius */
    bool relayState;            /**< Desired state for relay ON/OFF */
    uint32_t durationMs;        /**< Duration parameter in milliseconds */
} AppManagerEventPayload;

/**
 * @brief Type-safe data structure for queued ApplicationManager events.
 */
typedef struct {
    AppManagerEventId id;             /**< Unique Event Identifier */
    AppManagerEventPayload payload;   /**< Structured event payload */
    uint32_t timestamp_ms;            /**< Uptime timestamp when event was created */
} AppManagerEvent;

/**
 * @brief Status metrics for ApplicationManager diagnostic inspect.
 */
typedef struct {
    ServiceState state;            /**< Current lifecycle state of service */
    uint32_t processedEventsCount; /**< Total successfully processed event count */
    uint32_t droppedEventsCount;   /**< Total events dropped due to queue congestion */
    uint32_t queueCapacity;        /**< Max element capacity of event queue */
    uint32_t queueCurrentUsage;    /**< Current elements in event queue */
    uint32_t uptimeMs;             /**< Service uptime in milliseconds */
    bool isWatchdogRegistered;     /**< Flag indicating if Watchdog task feed is active */
    uint32_t factoryResetId;
    uint32_t factoryResetPhase;    /**< FactoryResetPhase; restart request is not a new boot. */
    int32_t factoryResetError;
    uint32_t factoryResetStartedMs;
    bool factoryResetBusy;
} AppManagerStatus;

typedef struct IApplicationManager IApplicationManager;

/**
 * @brief Structure defining the ApplicationManager interface contract.
 */
struct IApplicationManager {
    IService base;

    /**
     * @brief Gets current health and diagnostic status of ApplicationManager.
     *
     * @param self Pointer to instance.
     * @param status Output status structure pointer.
     * @return ESP_OK on success, ESP_ERR_INVALID_ARG or ESP_ERR_TIMEOUT on failure.
     */
    esp_err_t (*GetStatus)(IApplicationManager *self, AppManagerStatus *status);

    /**
     * @brief Dispatches an asynchronous event into ApplicationManager queue.
     *
     * @param self Pointer to instance.
     * @param event Pointer to event structure to push.
     * @return ESP_OK when queued, ESP_ERR_TIMEOUT if busy/full, ESP_ERR_INVALID_ARG if invalid,
     *         ESP_ERR_INVALID_STATE while stopped, or ESP_ERR_NOT_SUPPORTED for reserved actions.
     */
    esp_err_t (*PostEvent)(IApplicationManager *self, const AppManagerEvent *event);

    /**
     * @brief Triggers system factory reset sequence safely.
     *
     * @param self Pointer to instance.
     * @return ESP_OK on success.
     */
    esp_err_t (*TriggerFactoryReset)(IApplicationManager *self);
};

/**
 * @brief Retrieves the singleton instance of ApplicationManager.
 *
 * @return Pointer to IApplicationManager interface contract.
 */
IApplicationManager *ApplicationManager_GetModule(void);

#ifdef __cplusplus
}
#endif

#endif // IAPPLICATION_MANAGER_H
