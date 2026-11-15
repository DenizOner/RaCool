#include "Communication/Zigbee/Endpoints/OtaImageWriter.h"

#include <string.h>

#ifndef RACOOL_TEST_NATIVE
#include "sdkconfig.h"
#endif

/* This is a hard build-time security boundary, not a runtime UI toggle.
 * Native host mocks exercise the authorized branch with their own fakes.
 * The current production SDK configuration is intentionally unsigned. */
#if (defined(CONFIG_SECURE_SIGNED_APPS_NO_SECURE_BOOT) && \
     defined(CONFIG_SECURE_SIGNED_ON_UPDATE_NO_SECURE_BOOT) && \
     defined(CONFIG_SECURE_SIGNED_APPS_RSA_SCHEME) && \
     defined(CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE) && \
     !defined(CONFIG_SECURE_BOOT)) || (defined(RACOOL_TEST_NATIVE) && \
     defined(RACOOL_OTA_WRITER_NATIVE_TEST))
#define RACOOL_OTA_WRITER_SIGNED 1
#else
#define RACOOL_OTA_WRITER_SIGNED 0
#endif

#if RACOOL_OTA_WRITER_SIGNED
#include "esp_ota_ops.h"

static void OtaWriter_Reset(OtaImageWriter *writer) {
    memset(writer, 0, sizeof(*writer));
}
#endif

bool OtaImageWriter_SigningReady(void) {
    return RACOOL_OTA_WRITER_SIGNED != 0;
}

esp_err_t OtaImageWriter_Begin(OtaImageWriter *writer, uint32_t app_size) {
    if (!writer || !app_size) return ESP_ERR_INVALID_ARG;
#if RACOOL_OTA_WRITER_SIGNED
    if (writer->active || writer->failed || writer->verified || writer->boot_selected)
        return ESP_ERR_INVALID_STATE;

    const esp_partition_t *target = esp_ota_get_next_update_partition(NULL);
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (!target || !running || target == running ||
        app_size > target->size) return ESP_ERR_INVALID_STATE;

    esp_ota_handle_t handle = 0;
    /* Exact size bounds erase work. This may take time; never call from an
     * SDK/timer/ISR callback or while holding the Zigbee lock. */
    esp_err_t result = esp_ota_begin(target, app_size, &handle);
    if (result != ESP_OK) return result;

    writer->partition = target;
    writer->handle = (uintptr_t)handle;
    writer->expected_size = app_size;
    writer->received_size = 0;
    writer->failed = false;
    writer->verified = false;
    writer->boot_selected = false;
    writer->active = true;
    return ESP_OK;
#else
    (void)app_size;
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

esp_err_t OtaImageWriter_Write(OtaImageWriter *writer,
                               const uint8_t *data, uint32_t length) {
    if (!writer || !data || !length) return ESP_ERR_INVALID_ARG;
#if RACOOL_OTA_WRITER_SIGNED
    if (!writer->active || writer->failed || writer->verified || writer->boot_selected)
        return ESP_ERR_INVALID_STATE;
    if (writer->received_size >= writer->expected_size ||
        length > writer->expected_size - writer->received_size) {
        (void)OtaImageWriter_Abort(writer);
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t result = esp_ota_write((esp_ota_handle_t)writer->handle,
                                     data, (size_t)length);
    if (result != ESP_OK) {
        (void)OtaImageWriter_Abort(writer);
        return result;
    }
    writer->received_size += length;
    return ESP_OK;
#else
    (void)length;
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

esp_err_t OtaImageWriter_Verify(OtaImageWriter *writer) {
    if (!writer) return ESP_ERR_INVALID_ARG;
#if RACOOL_OTA_WRITER_SIGNED
    if (!writer->active || writer->failed || writer->verified || writer->boot_selected)
        return ESP_ERR_INVALID_STATE;
    if (writer->received_size != writer->expected_size) {
        (void)OtaImageWriter_Abort(writer);
        return ESP_ERR_INVALID_STATE;
    }
    /* esp_ota_end owns validation and ALWAYS frees the handle on return.
     * Signed-app verification is required above, never replaced by a CRC. */
    esp_err_t result = esp_ota_end((esp_ota_handle_t)writer->handle);
    writer->handle = 0;
    writer->active = false;
    if (result == ESP_OK) writer->verified = true;
    else OtaWriter_Reset(writer);
    return result;
#else
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

esp_err_t OtaImageWriter_SelectBoot(OtaImageWriter *writer) {
    if (!writer) return ESP_ERR_INVALID_ARG;
#if RACOOL_OTA_WRITER_SIGNED
    if (!writer->verified || writer->active || writer->failed ||
        writer->boot_selected || !writer->partition) return ESP_ERR_INVALID_STATE;
    /*
     * A failed otadata write may partially change which slot will boot.
     * Capture and verify the boot choice through the SDK; a set_boot return
     * value alone does not prove that the intended slot was selected.
     */
    const esp_partition_t *before = esp_ota_get_boot_partition();
    if (!before || before == writer->partition) return ESP_ERR_INVALID_STATE;
    esp_err_t result = esp_ota_set_boot_partition(
        (const esp_partition_t *)writer->partition);
    const esp_partition_t *after = esp_ota_get_boot_partition();
    if (result != ESP_OK || after != writer->partition) {
        writer->failed = true;
        writer->boot_selection_uncertain = true;
        /* Preserve evidence for the OTA owner: no blind retry, cleanup or
         * reboot after an uncertain boot selection, even if readback matches
         * the old slot. A separate, explicit recovery decision is required. */
        return result != ESP_OK ? result : ESP_ERR_INVALID_STATE;
    }
    writer->boot_selected = true;
    return ESP_OK;
#else
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

esp_err_t OtaImageWriter_Abort(OtaImageWriter *writer) {
    if (!writer) return ESP_ERR_INVALID_ARG;
#if RACOOL_OTA_WRITER_SIGNED
    if (writer->boot_selected || writer->boot_selection_uncertain) return ESP_ERR_INVALID_STATE;
    esp_err_t result = ESP_OK;
    if (writer->active) result = esp_ota_abort((esp_ota_handle_t)writer->handle);
    /* On failure, preserve handle for controlled retry by its owner;
     * never claim a canceled flash operation when SDK cleanup failed. */
    if (result == ESP_OK) OtaWriter_Reset(writer);
    else writer->failed = true;
    return result;
#else
    return ESP_ERR_NOT_SUPPORTED;
#endif
}
