#ifndef UI_CONTROLLER_CONFIG_H
#define UI_CONTROLLER_CONFIG_H

/* UI_HEALTH_CHECK_INTERVAL_MS is defined in SystemConfigs.h */

#include <freertos/FreeRTOS.h>
#include <stdint.h>
#include "Core/TaskPriorities.h"

/* Startup resource and timing bounds; live user settings use SystemConfigs. */
#define UI_SIGNAL_REFRESH_MS 1000u
#define UI_STARTUP_MESSAGE_MIN_MS 1000u
#define UI_SIGNAL_STRONG_RSSI_DBM (-65)
#define UI_SIGNAL_MEDIUM_RSSI_DBM (-80)
#define UI_SIGNAL_HYSTERESIS_DBM 3
#define UI_FACTORY_RESET_HOLD_MS 10000u

#define UI_PAIRING_TIMEOUT_MS 180000u
#define UI_PAIRING_CANCEL_RETRY_MS 1000u

#define UI_CONFIG_STACK_MIN_BYTES 4096u
#define UI_CONFIG_STACK_MAX_BYTES 65536u
#define UI_CONFIG_QUEUE_MAX_ITEMS 64u
#define UI_CONFIG_REFRESH_MAX_MS 1000u
#define UI_CONFIG_MUTEX_MAX_MS 1000u
#define UI_CONFIG_HEALTH_MAX_MS 3600000u

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Configuration structure for UIController task and display logic.
 */
typedef struct {
  uint32_t taskStackSize;      /**< Stack size for the UI task */
  UBaseType_t taskPriority;    /**< Priority for the UI task */
  uint32_t refreshIntervalMs;  /**< Standard UI refresh interval (ms) */
  uint32_t screenTimeoutSec;   /**< Default screen timeout if settings unavailable (sec) */
  float tempRefreshHysteresis; /**< Temperature change required for display refresh (deg C) */

  // RGB LED thermal boundaries
  float tempColdMin;    /**< Lower cold-zone boundary (deg C). */
  float tempColdMax;    /**< Upper cold-zone boundary (deg C). */
  float tempWarmMin;    /**< Lower warm-zone boundary (deg C). */
  float tempWarmMax;    /**< Upper warm-zone boundary (deg C). */
  float tempHotMin;     /**< Lower hot-zone boundary (deg C). */
  float tempHotMax;     /**< Upper hot-zone boundary (deg C). */
  float tempDanger;     /**< Danger threshold (deg C). */
  float tempFreeze;     /**< Freeze threshold (deg C). */

  // Diagnostics / Health pulse
  uint32_t healthCheckIntervalMs; /**< Interval for system health check pulse (ms) */

  // Event Driven and Thread Safety Config
  uint32_t eventQueueSize;   /**< Maximum number of events UI queue can hold */
  uint32_t mutexTimeoutMs;   /**< Timeout for thread synchronization mutex (ms) */
} UIControllerConfig;

/**
 * @brief Get the default configuration for UIController.
 *
 * @return UIControllerConfig Default settings.
 */
UIControllerConfig UIController_GetDefaultConfig(void);

#ifdef __cplusplus
}
#endif

#endif // UI_CONTROLLER_CONFIG_H

