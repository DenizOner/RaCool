/**
 * @file RelayInternal.h
 * @brief Internal header defining shared structures and declarations for Relay sub-modules.
 */

#ifndef RELAY_INTERNAL_H
#define RELAY_INTERNAL_H

#include "Hardware/Relay/IRelay.h"
#include "Hardware/Relay/RelayConfig.h"
#include "Core/EventBus/IEventBus.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RELAY_TAG "Relay"

#define RELAY_LOGD(FUNC, ...) ESP_LOGD(RELAY_TAG, "[" FUNC "] " __VA_ARGS__)
#define RELAY_LOGI(FUNC, ...) ESP_LOGI(RELAY_TAG, "[" FUNC "] " __VA_ARGS__)
#define RELAY_LOGW(FUNC, ...) ESP_LOGW(RELAY_TAG, "[" FUNC "] " __VA_ARGS__)
#define RELAY_LOGE(FUNC, ...) ESP_LOGE(RELAY_TAG, "[" FUNC "] " __VA_ARGS__)

/**
 * @brief Private structure representing the Relay Service state and context.
 */
typedef struct {
  IRelay base;                      /**< Public interface structure */
  ServiceState state;              /**< Current service state */
  RelayState current_status;        /**< Operational relay state */
  RelayMode current_mode;          /**< Control mode (Manual/Automatic) */
  gpio_num_t relay_pin;            /**< Configured GPIO pin number */
  IEventBus *eventBus;             /**< Reference to system EventBus */
  SemaphoreHandle_t mutex;         /**< Mutex handle for thread safety */
  esp_timer_handle_t async_timer;  /**< High precision esp_timer for async ops */
  bool auto_off_pending;           /**< Protected by mutex; canceled by manual operations */
  uint64_t auto_off_deadline_us;   /**< Absolute ESP timer deadline */
  uint64_t last_switch_time_us;    /**< Timestamp of last physical pin state toggle (us) */
  uint64_t energized_start_time_us;/**< Timestamp when relay was energized (us) */
  RelayStats stats;                /**< Operational statistics counters */
} RelayServiceImpl;

extern RelayServiceImpl relayService;

// Internal Driver Declarations
esp_err_t Relay_SetPhysicalState(bool energize);
esp_err_t Relay_SetPhysicalStateImmediate(bool energize);
esp_err_t Relay_Lock(void);
esp_err_t Relay_CancelAutoOff(void);
uint32_t Relay_CalculateAntiShortCycleRemainingMs_Internal(void);
esp_err_t Relay_Open_Internal(bool acquire_lock);
esp_err_t Relay_Close_Internal(bool acquire_lock);

// Internal Stats Declarations
esp_err_t Relay_GetStats(IRelay *self, RelayStats *out_stats);
esp_err_t Relay_ResetStats(IRelay *self);
uint32_t Relay_GetAntiShortCycleRemainingMs(IRelay *self);

// Internal Event Helper
void Relay_PublishEvent(const char *event_name, EventType type, void *data, uint32_t data_size);

// Console Command Registration
esp_err_t Relay_RegisterCommands(void);

#ifdef __cplusplus
}
#endif

#endif // RELAY_INTERNAL_H
