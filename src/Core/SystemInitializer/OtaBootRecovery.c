#include "Core/SystemInitializer/OtaBootRecovery.h"

#ifndef RACOOL_TEST_NATIVE
#include "sdkconfig.h"
#endif

#include "Core/ServiceLocator/ServiceLocator.h"
#include "Core/ServiceLocator/ServiceNames.h"
#include "Management/ConfigurationManager/IConfigurationManager.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#if !defined(CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE) && \
    !defined(RACOOL_TARGET_TESTING) && !defined(RACOOL_TEST_NATIVE)
#error "Production OTA boot health gate requires CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE"
#endif

#if defined(CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE)
static const char *TAG = "OtaBootRecovery";
#define OTA_BOOT_HEALTH_POLL_MS 500u
#define OTA_BOOT_HEALTH_MAX_SAMPLES 6u
#define OTA_BOOT_HEALTH_REQUIRED_CONSECUTIVE 2u

/* Check owners and persistence, not transient sensor samples, an MQTT cache,
 * or Zigbee association with a particular network at boot. */
static bool OtaBootRecovery_IsHealthy(void) {
    static const char *const required[] = {
        SVC_NAME_CONFIGURATION_MANAGER, SVC_NAME_RELAY,
        SVC_NAME_IR_TEMP_SENSOR, SVC_NAME_CLIMATE_CONTROLLER,
        SVC_NAME_ZIGBEE, SVC_NAME_WATCHDOG
    };
    for (size_t i = 0; i < sizeof(required) / sizeof(required[0]); ++i) {
        IService *service = ServiceLocator_GetService(required[i]);
        if (!service || !service->GetState ||
            service->GetState(service) != SERVICE_STATE_STARTED)
            return false;
        /* Read watchdog health, not Zigbee RF association or a single MLX
         * sample; those can be temporarily unavailable on a sound boot. */
        if (i == sizeof(required) / sizeof(required[0]) - 1u &&
            (!service->CheckHealth || service->CheckHealth(service) != 0))
            return false;
    }

    IConfigurationManager *config = ConfigurationManager_GetInstance();
    SystemSettings settings;
    return config && config->GetSnapshot &&
           config->GetSnapshot(config, &settings) == ESP_OK;
}
#endif

void OtaBootRecovery_CompleteFirstBoot(bool startupSucceeded) {
#if defined(CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE)
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
    if (!running || esp_ota_get_state_partition(running, &state) != ESP_OK) {
        /* No valid state readback means no authorization to mark or reboot. */
        ESP_LOGE(TAG, "OTA boot state cannot be verified; no boot mutation");
        return;
    }
    if (state != ESP_OTA_IMG_PENDING_VERIFY) return;

    unsigned consecutiveHealthy = 0u;
    if (startupSucceeded) {
        for (unsigned i = 0; i < OTA_BOOT_HEALTH_MAX_SAMPLES; ++i) {
            consecutiveHealthy = OtaBootRecovery_IsHealthy() ?
                consecutiveHealthy + 1u : 0u;
            if (consecutiveHealthy >= OTA_BOOT_HEALTH_REQUIRED_CONSECUTIVE)
                break;
            if (i + 1u < OTA_BOOT_HEALTH_MAX_SAMPLES) {
                TickType_t delay = pdMS_TO_TICKS(OTA_BOOT_HEALTH_POLL_MS);
                vTaskDelay(delay ? delay : 1);
            }
        }
    }

    if (consecutiveHealthy >= OTA_BOOT_HEALTH_REQUIRED_CONSECUTIVE) {
        esp_err_t result = esp_ota_mark_app_valid_cancel_rollback();
        if (result == ESP_OK) {
            ESP_LOGI(TAG, "Pending OTA image confirmed after critical service checks");
            return;
        }
        ESP_LOGE(TAG, "OTA boot confirmation failed: %s", esp_err_to_name(result));
    } else {
        ESP_LOGE(TAG, "Pending OTA image failed bounded service health checks");
    }

    /* Never force a reboot without a second bootable application. Inability
     * to roll back keeps PENDING_VERIFY for the bootloader's next attempt. */
    if (!esp_ota_check_rollback_is_possible()) {
        ESP_LOGE(TAG, "No verified fallback image; remaining unconfirmed");
        return;
    }
    esp_err_t result = esp_ota_mark_app_invalid_rollback_and_reboot();
    ESP_LOGE(TAG, "OTA rollback call returned: %s", esp_err_to_name(result));
#else
    (void)startupSucceeded;
#endif
}
