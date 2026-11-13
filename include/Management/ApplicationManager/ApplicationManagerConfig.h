/**
 * @file ApplicationManagerConfig.h
 * @brief Configuration parameters for the ApplicationManager module.
 *
 * Centralizes all configurable parameters for ApplicationManager facade/orchestration,
 * including FreeRTOS tasks, event queues, timeouts, interaction limits, and service settings.
 *
 * @author RaCool Team
 * @date 2026
 */

#ifndef APPLICATION_MANAGER_CONFIG_H
#define APPLICATION_MANAGER_CONFIG_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ===========================================================================
// FreeRTOS Task Configuration Parameters
// ===========================================================================

/** @brief Stack size for the main ApplicationManager task (in bytes). */
#define APP_TASK_STACK_SIZE         8192

/** @brief FreeRTOS priority for the main ApplicationManager task (0 = lowest). */
#define APP_TASK_PRIORITY           5

/** @brief Task name identifier for debugging and tracing. */
#define APP_TASK_NAME               "AppTask"

/** @brief Size of the internal async event queue for ApplicationManager. */
#define APP_EVENT_QUEUE_SIZE        64

/** @brief Maximum wait time when pushing events to queue (in milliseconds). */
#define APP_EVENT_QUEUE_SEND_TIMEOUT_MS  50

/** @brief Timeout for Mutex acquisition when guarding shared state (in milliseconds). */
#define APP_MUTEX_TIMEOUT_MS        1000

// ===========================================================================
// Timing & Interaction Parameters
// ===========================================================================

/** @brief Default Zigbee telemetry and status publish interval (in milliseconds). */
#define APP_ZIGBEE_PUBLISH_INTERVAL_MS   20000

/** @brief Threshold duration for holding button to trigger factory reset (in milliseconds). */
#define APP_FACTORY_RESET_HOLD_DURATION_MS 10000

#define APP_FACTORY_RESET_TIMEOUT_MS 30000U
#define APP_FACTORY_RESET_POLL_MS 100U

/** @brief Retry count during startup service lookup phase. */
#define APP_SERVICE_INIT_MAX_RETRIES     100

/** @brief Delay between service lookup retries (in milliseconds). */
#define APP_SERVICE_INIT_RETRY_DELAY_MS  50

#ifdef __cplusplus
}
#endif

#endif // APPLICATION_MANAGER_CONFIG_H
