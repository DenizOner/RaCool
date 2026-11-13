/**
 * @file RelayDriver.c
 * @brief Low-level driver implementation for Relay hardware driving and protection.
 */

#include "Hardware/Relay/RelayInternal.h"
#include "Core/ServiceLocator/ServiceLocator.h"
#include "esp_log.h"

void Relay_PublishEvent(const char *event_name, EventType type, void *data, uint32_t data_size) {
  IEventBus *bus = relayService.eventBus;
  if (bus && bus->Publish) {
    BusEvent e = {
      .eventName = event_name,
      .type = type,
      .data = data,
      .dataSize = data_size
    };
    bus->Publish(e);
  }
}

uint32_t Relay_CalculateAntiShortCycleRemainingMs_Internal(void) {
  if (relayService.last_switch_time_us == 0) return 0;
  uint64_t now_us = esp_timer_get_time();
  uint64_t elapsed_ms = (now_us - relayService.last_switch_time_us) / 1000ULL;
  if (elapsed_ms < RELAY_ANTI_SHORT_CYCLE_MIN_MS) {
    return (uint32_t)(RELAY_ANTI_SHORT_CYCLE_MIN_MS - elapsed_ms);
  }
  return 0;
}

esp_err_t Relay_Lock(void) {
  if (!relayService.mutex) return ESP_ERR_INVALID_STATE;
  TickType_t ticks = pdMS_TO_TICKS(RELAY_LOCK_TIMEOUT_MS);
  if (ticks == 0) ticks = 1;
  return xSemaphoreTake(relayService.mutex, ticks) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}

esp_err_t Relay_CancelAutoOff(void) {
  if (!relayService.auto_off_pending) return ESP_OK;
  esp_err_t result = esp_timer_stop(relayService.async_timer);
  if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) return result;
  relayService.auto_off_pending = false;
  return ESP_OK;
}

esp_err_t Relay_SetPhysicalStateImmediate(bool energize) {
  if (relayService.relay_pin == GPIO_NUM_NC) {
    RELAY_LOGE("Relay_SetPhysicalState", "Relay pin not configured!");
    return ESP_ERR_INVALID_STATE;
  }

  int level = energize ? (RELAY_ACTIVE_HIGH ? 1 : 0) : (RELAY_ACTIVE_HIGH ? 0 : 1);
  esp_err_t ret = gpio_set_level(relayService.relay_pin, level);
  if (ret == ESP_OK) {
    uint64_t current_time_us = esp_timer_get_time();
    if (energize) {
      relayService.energized_start_time_us = current_time_us;
      relayService.stats.total_switches++;
    } else {
      if (relayService.energized_start_time_us > 0) {
        uint64_t active_ms = (current_time_us - relayService.energized_start_time_us) / 1000ULL;
        relayService.stats.total_active_time_ms += active_ms;
        relayService.energized_start_time_us = 0;
      }
    }
    relayService.last_switch_time_us = current_time_us;
  }
  return ret;
}

esp_err_t Relay_SetPhysicalState(bool energize) {
  uint64_t now_us = esp_timer_get_time();
  uint64_t elapsed_us = now_us - relayService.last_switch_time_us;
  uint64_t minimum_us = (uint64_t)RELAY_MIN_SWITCH_INTERVAL_MS * 1000ULL;
  if (relayService.last_switch_time_us && elapsed_us < minimum_us) {
    uint32_t delay_ms = (uint32_t)((minimum_us - elapsed_us + 999ULL) / 1000ULL);
    TickType_t ticks = pdMS_TO_TICKS(delay_ms);
    if (ticks == 0) ticks = 1;
    vTaskDelay(ticks);
  }
  return Relay_SetPhysicalStateImmediate(energize);
}

esp_err_t Relay_Open_Internal(bool acquire_lock) {
  if (acquire_lock) { esp_err_t lock = Relay_Lock(); if (lock != ESP_OK) return lock; }
  if (relayService.state != SERVICE_STATE_STARTED) {
    if (acquire_lock) xSemaphoreGive(relayService.mutex);
    return ESP_ERR_INVALID_STATE;
  }
  esp_err_t cancel = Relay_CancelAutoOff();
  if (cancel != ESP_OK) {
    if (acquire_lock) xSemaphoreGive(relayService.mutex);
    return cancel;
  }
  if (relayService.current_status == RELAY_STATE_OPEN ||
      relayService.current_status == RELAY_STATE_PULSING) {
    relayService.current_status = RELAY_STATE_OPEN;
    if (acquire_lock) xSemaphoreGive(relayService.mutex);
    return ESP_OK;
  }

  uint32_t remaining_safety_ms = Relay_CalculateAntiShortCycleRemainingMs_Internal();
  if (remaining_safety_ms > 0) {
    RELAY_LOGW("Relay_Open", "Anti-short cycle protection active. Waiting %lu ms before energizing.", (unsigned long)remaining_safety_ms);
    relayService.stats.safety_triggers_count++;
    Relay_PublishEvent(RELAY_EVENT_SAFETY_ALERT, EVENT_TYPE_WARNING, &remaining_safety_ms, sizeof(remaining_safety_ms));
    TickType_t ticks = pdMS_TO_TICKS(remaining_safety_ms);
    if (ticks == 0) ticks = 1;
    vTaskDelay(ticks);
  }

  esp_err_t ret = Relay_SetPhysicalState(true);
  if (ret == ESP_OK) {
    relayService.current_status = RELAY_STATE_OPEN;
    RELAY_LOGI("Relay_Open", "Relay energized (OPEN)");
    int state_val = 1;
    Relay_PublishEvent(RELAY_EVENT_STATE_CHANGED, EVENT_TYPE_INFO, &state_val, sizeof(state_val));
  } else {
    RELAY_LOGE("Relay_Open", "Failed to set physical state: 0x%x", ret);
  }

  if (acquire_lock) {
    xSemaphoreGive(relayService.mutex);
  }
  return ret;
}

esp_err_t Relay_Close_Internal(bool acquire_lock) {
  if (acquire_lock) { esp_err_t lock = Relay_Lock(); if (lock != ESP_OK) return lock; }
  if (relayService.state != SERVICE_STATE_STARTED) {
    if (acquire_lock) xSemaphoreGive(relayService.mutex);
    return ESP_ERR_INVALID_STATE;
  }
  esp_err_t cancel = Relay_CancelAutoOff();
  if (cancel != ESP_OK) {
    if (acquire_lock) xSemaphoreGive(relayService.mutex);
    return cancel;
  }
  if (relayService.current_status == RELAY_STATE_CLOSED) {
    if (acquire_lock) xSemaphoreGive(relayService.mutex);
    return ESP_OK;
  }

  esp_err_t ret = Relay_SetPhysicalState(false);
  if (ret == ESP_OK) {
    relayService.current_status = RELAY_STATE_CLOSED;
    RELAY_LOGI("Relay_Close", "Relay de-energized (CLOSED)");
    int state_val = 0;
    Relay_PublishEvent(RELAY_EVENT_STATE_CHANGED, EVENT_TYPE_INFO, &state_val, sizeof(state_val));
  } else {
    RELAY_LOGE("Relay_Close", "Failed to set physical state: 0x%x", ret);
  }

  if (acquire_lock && relayService.mutex) {
    xSemaphoreGive(relayService.mutex);
  }
  return ret;
}
