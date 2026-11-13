/**
 * @file IRelay.h
 * @brief Interface definition for the Relay Hardware Module.
 *
 * Defines the public API contract for controlling physical relays in RaCool,
 * supporting manual, automated, pulse, timed operation, safety protections
 * (anti-short cycle, pulse caps), thread safety, lifetime statistics, and event notifications.
 */

#ifndef IRELAY_H
#define IRELAY_H

#include "Core/IService.h"
#include <stdbool.h>
#include <stdint.h>
#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Event name string definitions published to EventBus.
 */
#define RELAY_EVENT_STATE_CHANGED "RELAY_STATE_CHANGED"
#define RELAY_EVENT_MODE_CHANGED  "RELAY_MODE_CHANGED"
#define RELAY_EVENT_SAFETY_ALERT  "RELAY_SAFETY_ALERT"

/**
 * @brief Relay State enumeration.
 */
typedef enum {
  RELAY_STATE_CLOSED = 0, /**< Relay contact is open / coil de-energized (OFF) */
  RELAY_STATE_OPEN = 1,   /**< Relay contact is closed / coil energized (ON) */
  RELAY_STATE_PULSING = 2 /**< Relay is undergoing a pulse duration operation */
} RelayState;

/**
 * @brief Relay Operation Mode.
 */
typedef enum {
  RELAY_MODE_MANUAL = 0,   /**< Relay is controlled manually via user/API commands */
  RELAY_MODE_AUTOMATIC = 1 /**< Relay is controlled automatically based on system events/sensors */
} RelayMode;

/**
 * @brief Structure containing lifetime operation statistics for the Relay hardware.
 */
typedef struct {
  uint32_t total_switches;        /**< Total number of state toggle/open operations */
  uint64_t total_active_time_ms;  /**< Total accumulated active (energized) time in milliseconds */
  uint32_t safety_triggers_count; /**< Total times anti-short cycle or safety limits deferred/blocked action */
} RelayStats;

typedef struct IRelay IRelay;

/**
 * @brief Struct representing the Relay interface operations.
 */
struct IRelay {
  /** Base service lifecycle interface */
  IService base;

  /**
   * @brief Energizes the relay coil (turns relay ON / closes contact).
   *
   * @param self Pointer to the IRelay interface instance.
   * @return ESP_OK on success, or appropriate error code.
   */
  esp_err_t (*Open)(IRelay *self);

  /**
   * @brief De-energizes the relay coil (turns relay OFF / opens contact).
   *
   * @param self Pointer to the IRelay interface instance.
   * @return ESP_OK on success, or appropriate error code.
   */
  esp_err_t (*Close)(IRelay *self);

  /**
   * @brief Toggles the current state of the relay coil.
   *
   * @param self Pointer to the IRelay interface instance.
   * @return ESP_OK on success, or appropriate error code.
   */
  esp_err_t (*Toggle)(IRelay *self);

  /**
   * @brief Energizes the relay coil for a specified duration in milliseconds asynchronously.
   *
   * @param self Pointer to the IRelay interface instance.
   * @param duration_ms Duration in milliseconds to stay energized.
   * @return ESP_OK on success, or appropriate error code.
   */
  esp_err_t (*PulseAsync)(IRelay *self, uint32_t duration_ms);

  /**
   * @brief Energizes the relay coil and schedules automatic turn off after specified timeout.
   *
   * @param self Pointer to the IRelay interface instance.
   * @param timeout_ms Timeout duration in milliseconds before auto-off.
   * @return ESP_OK on success, or appropriate error code.
   */
  esp_err_t (*SetAutoOff)(IRelay *self, uint32_t timeout_ms);

  /**
   * @brief Checks if the relay coil is currently energized (OPEN state).
   *
   * @param self Pointer to the IRelay interface instance.
   * @return true if coil is energized/active, false otherwise.
   */
  bool (*IsOpen)(IRelay *self);

  /**
   * @brief Retrieves the current state of the relay.
   *
   * @param self Pointer to the IRelay interface instance.
   * @return Current RelayState.
   */
  RelayState (*GetStatus)(IRelay *self);

  /** Nonblocking checked applied output-state snapshot; preserves output on error.
   *  This is not a relay contact or fan rotation measurement. */
  esp_err_t (*ReadState)(IRelay *self, bool *energized);

  /**
   * @brief Sets the operating mode of the relay (Manual or Automatic).
   *
   * @param self Pointer to the IRelay interface instance.
   * @param mode Target RelayMode.
   * @return ESP_OK on success, or appropriate error code.
   */
  esp_err_t (*SetMode)(IRelay *self, RelayMode mode);

  /**
   * @brief Gets the current operating mode of the relay.
   *
   * @param self Pointer to the IRelay interface instance.
   * @return Current RelayMode.
   */
  RelayMode (*GetMode)(IRelay *self);

  /**
   * @brief Retrieves operational statistics (switch count, total active time, safety triggers).
   *
   * @param self Pointer to the IRelay interface instance.
   * @param out_stats Pointer to RelayStats structure to populate.
   * @return ESP_OK on success, or ESP_ERR_INVALID_ARG if out_stats is NULL.
   */
  esp_err_t (*GetStats)(IRelay *self, RelayStats *out_stats);

  /**
   * @brief Resets all operational statistics counters.
   *
   * @param self Pointer to the IRelay interface instance.
   * @return ESP_OK on success.
   */
  esp_err_t (*ResetStats)(IRelay *self);

  /**
   * @brief Returns remaining time in milliseconds for the anti-short cycle protection lock, if active.
   *
   * @param self Pointer to the IRelay interface instance.
   * @return Remaining delay in ms (0 if ready).
   */
  uint32_t (*GetAntiShortCycleRemainingMs)(IRelay *self);

  /**
   * @brief Executes a comprehensive diagnostic self-test sequence.
   *
   * @param self Pointer to the IRelay interface instance.
   * @return ESP_OK if self-test passes, error code otherwise.
   */
  esp_err_t (*RunSelfTest)(IRelay *self);
};

/**
 * @brief Singleton provider for the Relay module interface.
 *
 * @return Pointer to the global IRelay instance.
 */
IRelay *Relay_GetModule(void);

#ifdef __cplusplus
}
#endif

#endif // IRELAY_H
