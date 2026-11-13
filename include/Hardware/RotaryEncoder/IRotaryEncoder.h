#ifndef IROTARYENCODER_H
#define IROTARYENCODER_H

/**
 * @file IRotaryEncoder.h
 * @brief Public interface for the KY-040 Rotary Encoder service.
 *
 * Exposes event-driven rotation/button subscription, runtime configuration,
 * diagnostics, and LVGL input-device integration.  All methods are
 * thread-safe and designed for asynchronous, non-blocking operation.
 */

#include "Core/IService.h"
#include "Hardware/RotaryEncoder/RotaryEncoderTypes.h"
#include <esp_err.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct IRotaryEncoder IRotaryEncoder;

/**
 * @brief Rotary Encoder service virtual table.
 *
 * Inherits IService for lifecycle management (Initialize / Start / Stop).
 * All function pointers are guaranteed non-NULL after RotaryEncoder_GetModule().
 */
struct IRotaryEncoder {
    /** @brief IService base providing Initialize/Start/Stop/GetState/CheckHealth. */
    IService base;

    /* ── Event Subscription ──────────────────────────────────────────── */

    /**
     * @brief Register a rotation event callback.
     *
     * Up to RE_MAX_CALLBACK_SUBSCRIBERS callbacks can be registered.
     * Callbacks are invoked from the encoder task context.
     *
     * @param self      Service instance.
     * @param callback  Function to call on each rotation detent.
     * @param ctx       Opaque context forwarded to the callback.
     * @return ESP_OK on success, ESP_ERR_NO_MEM if all slots are occupied.
     */
    esp_err_t (*OnRotation)(IRotaryEncoder *self,
                            RotaryEncoderRotationCallback callback,
                            void *ctx);

    /**
     * @brief Register a button event callback.
     *
     * @param self      Service instance.
     * @param callback  Function to call on each button gesture.
     * @param ctx       Opaque context forwarded to the callback.
     * @return ESP_OK on success, ESP_ERR_NO_MEM if all slots are occupied.
     */
    esp_err_t (*OnButton)(IRotaryEncoder *self,
                          RotaryEncoderButtonCallback callback,
                          void *ctx);

    /**
     * @brief Unsubscribe a previously registered rotation callback.
     *
     * @param self      Service instance.
     * @param callback  The exact function pointer that was registered.
     * @return ESP_OK if removed, ESP_ERR_NOT_FOUND if absent, or
     * ESP_ERR_INVALID_STATE while a captured callback still owns its context.
     * Retain callback resources and retry after dispatch returns.
     */
    esp_err_t (*UnsubscribeRotation)(IRotaryEncoder *self,
                                     RotaryEncoderRotationCallback callback);

    /**
     * @brief Unsubscribe a previously registered button callback.
     *
     * @param self      Service instance.
     * @param callback  The exact function pointer that was registered.
     * @return ESP_OK if removed, ESP_ERR_NOT_FOUND if absent, or
     * ESP_ERR_INVALID_STATE while a captured callback still owns its context.
     * Retain callback resources and retry after dispatch returns.
     */
    esp_err_t (*UnsubscribeButton)(IRotaryEncoder *self,
                                   RotaryEncoderButtonCallback callback);

    /* ── Status & Diagnostics ────────────────────────────────────────── */

    /**
     * @brief Get a thread-safe snapshot of the encoder status.
     */
    RotaryEncoderStatus (*GetStatus)(IRotaryEncoder *self);

    /**
     * @brief Get the current absolute position counter.
     */
    int32_t (*GetAbsolutePosition)(IRotaryEncoder *self);

    /**
     * @brief Check whether the button is currently held down.
     */
    bool (*IsButtonHeld)(IRotaryEncoder *self);

    /**
     * @brief Retrieve diagnostic counters.
     *
     * @param self         Service instance.
     * @param diagnostics  Output pointer (must not be NULL).
     * @return ESP_OK on success, ESP_ERR_INVALID_ARG if diagnostics is NULL.
     */
    esp_err_t (*GetDiagnostics)(IRotaryEncoder *self,
                                RotaryEncoderDiagnostics *diagnostics);

    /**
     * @brief Retrieve the current runtime configuration snapshot.
     *
     * @param self    Service instance.
     * @param config  Output pointer (must not be NULL).
     * @return ESP_OK on success, ESP_ERR_INVALID_ARG if config is NULL.
     */
    esp_err_t (*GetRuntimeConfig)(IRotaryEncoder *self,
                                  RotaryEncoderRuntimeConfig *config);

    /* ── Runtime Configuration ───────────────────────────────────────── */

    /**
     * @brief Enable or disable rotation acceleration and set threshold.
     */
    esp_err_t (*SetAcceleration)(IRotaryEncoder *self, bool enabled,
                                 uint8_t threshold_ms);

    /**
     * @brief Set debounce durations for rotation and button.
     */
    esp_err_t (*SetDebounceMs)(IRotaryEncoder *self, uint16_t rotation_ms,
                               uint16_t button_ms);

    /**
     * @brief Set long-press detection threshold.
     */
    esp_err_t (*SetLongPressMs)(IRotaryEncoder *self, uint16_t duration_ms);

    /**
     * @brief Set double-click detection window.
     */
    esp_err_t (*SetDoubleClickMs)(IRotaryEncoder *self, uint16_t window_ms);

    /**
     * @brief Set held-button repeat interval.
     */
    esp_err_t (*SetHeldRepeatMs)(IRotaryEncoder *self, uint16_t interval_ms);

    /**
     * @brief Invert the logical rotation direction.
     */
    esp_err_t (*SetDirectionInverted)(IRotaryEncoder *self, bool inverted);

    /**
       * @brief Change actual PCNT limits through the existing worker; zero selects the default.
       * Failed setup restores the previous setting or closes admission while retaining cleanup.
       * A timeout may leave an admitted control pending; read back before retrying.
     */
    esp_err_t (*SetStepsPerDetent)(IRotaryEncoder *self, uint8_t steps);

    /* ── Utility ─────────────────────────────────────────────────────── */

    /**
     * @brief Reset all counters, queues, and gesture state to zero.
     */
    esp_err_t (*Reset)(IRotaryEncoder *self);
};

/**
 * @brief Get the singleton RotaryEncoder module instance.
 *
 * Lazily initializes the vtable on first call.
 * @return Pointer to the global IRotaryEncoder interface.
 */
IRotaryEncoder *RotaryEncoder_GetModule(void);

#ifdef __cplusplus
}
#endif

#endif /* IROTARYENCODER_H */
