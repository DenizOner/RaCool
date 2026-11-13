/**
 * @file RelayStats.c
 * @brief Implementation of stats tracking and anti-short cycle metrics for Relay.
 */

#include "Hardware/Relay/RelayInternal.h"
#include <string.h>

esp_err_t Relay_GetStats(IRelay *self, RelayStats *out_stats) {
  if (!out_stats) return ESP_ERR_INVALID_ARG;
  esp_err_t admission = Relay_Lock();
  if (admission != ESP_OK) return admission;
  *out_stats = relayService.stats;
  if (relayService.current_status == RELAY_STATE_OPEN || relayService.current_status == RELAY_STATE_PULSING) {
    if (relayService.energized_start_time_us > 0) {
      uint64_t now_us = esp_timer_get_time();
      out_stats->total_active_time_ms += (now_us - relayService.energized_start_time_us) / 1000ULL;
    }
  }
  if (relayService.mutex) {
    xSemaphoreGive(relayService.mutex);
  }
  return ESP_OK;
}

esp_err_t Relay_ResetStats(IRelay *self) {
  esp_err_t admission = Relay_Lock();
  if (admission != ESP_OK) return admission;
  memset(&relayService.stats, 0, sizeof(RelayStats));
  if (relayService.current_status == RELAY_STATE_OPEN || relayService.current_status == RELAY_STATE_PULSING) {
    relayService.energized_start_time_us = esp_timer_get_time();
  }
  RELAY_LOGI("Relay_ResetStats", "Relay operational statistics reset.");
  if (relayService.mutex) {
    xSemaphoreGive(relayService.mutex);
  }
  return ESP_OK;
}

uint32_t Relay_GetAntiShortCycleRemainingMs(IRelay *self) {
  if (Relay_Lock() != ESP_OK) return RELAY_ANTI_SHORT_CYCLE_MIN_MS;
  uint32_t rem = Relay_CalculateAntiShortCycleRemainingMs_Internal();
  if (relayService.mutex) {
    xSemaphoreGive(relayService.mutex);
  }
  return rem;
}
