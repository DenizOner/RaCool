#include "Core/ServiceLocator/ServiceLocator.h"
#include "Core/SystemInitializer/ISystemInitializer.h"
#include "Controllers/UIController/BootAnimation.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <esp_rom_sys.h>
#include <esp_timer.h>
#include <stdint.h>
#include <stdio.h>

#ifndef RACOOL_TARGET_TESTING
static const char *TAG = "Main";

static void app_startup_task(void *pvParameters) {
  uint64_t start_time = esp_timer_get_time();
  ESP_LOGI(TAG, "app_main: RaCool application starting");

  SystemInitializer *initializer = GetSystemInitializer();

  if (initializer) {
    if (initializer->base.Initialize((IService *)initializer, NULL) == 0) {
      ServiceLocator *locator = GetServiceLocator();
      if (locator && locator->StartAllServices && locator->StartAllServices() == 0) {

        uint64_t end_time = esp_timer_get_time();
        uint64_t duration_ms = (end_time - start_time) / 1000;

        ESP_LOGI(TAG, "Boot complete, duration=%lld ms", duration_ms);
          BootAnimation_NotifyReady();
        ESP_LOGI("Main", "app_main: System ready.");
      } else {
        ESP_LOGE(TAG, "Service startup failed; system is not ready.");
      }
    } else {
      ESP_LOGE(TAG, "System Initialization FAILED!");
    }
  } else {
    ESP_LOGE(TAG, "Failed to get SystemInitializer!");
  }
  vTaskDelete(NULL);
}

void app_main() {
  // Optional delay to give serial monitor time to attach during debugging.
  // Uncomment during active development when early boot logs need to be captured,
  // and re-comment for production/fast-boot. Do NOT delete this block.
  /*
  #if RACOOL_TARGET_MONITOR
    for (int i = 10; i > 0; i--) {
      ESP_LOGI(TAG, "Waiting for monitor... %d", i);
      vTaskDelay(pdMS_TO_TICKS(1000));
    }
  */
  ESP_LOGI(TAG, "!!! RACOOL SYSTEM STARTING !!!");
  /*
    fflush(stdout);
  #endif
  */
  if (xTaskCreate(app_startup_task, "app_startup", 16384, NULL, 5, NULL) != pdPASS)
    ESP_LOGE(TAG, "Failed to create application startup task.");
}
#endif // RACOOL_TARGET_TESTING
