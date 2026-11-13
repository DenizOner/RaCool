#ifndef I_BUILTIN_LED_H
#define I_BUILTIN_LED_H

#include "Core/IService.h"
#include "Hardware/BuiltinLED/BuiltinLEDConfig.h"
#include <esp_err.h>

// Forward declaration
typedef struct AnimationPlugin AnimationPlugin;

/**
 * @brief Structure representing the status of the BuiltinLED module.
 */
typedef struct {
    BuiltinLEDAnimationType current_animation;  ///< Currently running animation type.
    BuiltinLEDPriority current_priority;        ///< Priority of the current animation.
    uint32_t remaining_ms;                      ///< Remaining duration; zero means no timed expiry.
    uint8_t brightness;                         ///< Current brightness level (0-255).
    bool is_on;                                 ///< Logical requested output, not physical light evidence.
    uint8_t registered_plugins_count;           ///< Number of registered animation plugins.
    bool enabled;                               ///< Applied global gate, independent of the current frame.
} BuiltinLEDStatus;

/**
 * @brief Interface for the BuiltinLED Service.
 * 
 * Provides methods for initializing the built-in LED, running animations,
 * turning the LED on/off, and configuring the module.
 */
typedef struct IBuiltinLED IBuiltinLED;

struct IBuiltinLED {
    IService base;  ///< Base service interface.

    /**
     * @brief Configures the BuiltinLED module with custom settings.
     * 
     * Args:
     *     self: Pointer to the IBuiltinLED instance.
     *     config: Pointer to the configuration structure.
     * 
     * Returns:
     *     ESP_OK when queued, or an error code on rejection. Worker application
     *     remains asynchronous and may be preempted by a higher priority.
     */
    esp_err_t (*InitModule)(IBuiltinLED *self, const BuiltinLEDConfig *config);

    /**
     * @brief Sets and starts an animation asynchronously.
     * 
     * Args:
     *     self: Pointer to the IBuiltinLED instance.
     *     type: The type of the animation to run.
     *     priority: The priority level of the animation.
     *     duration_ms: Duration in milliseconds (0 for infinite/default).
     * 
     * Returns:
     *     ESP_OK on success, or an error code on failure.
     */
    esp_err_t (*SetAnimation)(IBuiltinLED *self, BuiltinLEDAnimationType type,
                              BuiltinLEDPriority priority, uint32_t duration_ms);
    /** Splash owner queues the boot phase independently of the runtime default.
     * The worker applies the persisted startup selection. Notifications retain
     * priority; global disabled mode remains authoritative. Queue acceptance
     * is not completed application. Only the UI splash owner controls this phase. */
    esp_err_t (*SetStartupActive)(IBuiltinLED *self, bool active);
    esp_err_t (*PreviewAnimation)(IBuiltinLED *self, BuiltinLEDAnimationType type, uint32_t duration_ms);

    /**
     * @brief Stops the currently running animation and clears the LED.
     * 
     * Args:
     *     self: Pointer to the IBuiltinLED instance.
     * 
     * Returns:
     *     ESP_OK on success, or an error code on failure.
     */
    esp_err_t (*StopAnimation)(IBuiltinLED *self);

    /**
     * @brief Turns on the LED with a static solid white color.
     * 
     * Args:
     *     self: Pointer to the IBuiltinLED instance.
     * 
     * Returns:
     *     ESP_OK on success, or an error code on failure.
     */
    esp_err_t (*TurnOn)(IBuiltinLED *self);

    /**
     * @brief Turns off the LED.
     * 
     * Args:
     *     self: Pointer to the IBuiltinLED instance.
     * 
     * Returns:
     *     ESP_OK on success, or an error code on failure.
     */
    esp_err_t (*TurnOff)(IBuiltinLED *self);

    /**
     * @brief Registers a new custom animation plugin.
     * Plugin, name and context remain caller-owned and must stay valid for
     * the service lifetime, including queued work and restarts. The caller
     * must not mutate them while the worker owns plugin execution.
     * 
     * Args:
     *     self: Pointer to the IBuiltinLED instance.
     *     plugin: Pointer to the animation plugin to register.
     * 
     * Returns:
     *     ESP_OK on success, or an error code on failure.
     */
    esp_err_t (*RegisterAnimation)(IBuiltinLED *self, AnimationPlugin *plugin);

    /**
     * @brief Adjusts the maximum brightness of the LED.
     * 
     * Args:
     *     self: Pointer to the IBuiltinLED instance.
     *     brightness: Brightness value between 0 and 255.
     * 
     * Returns:
     *     ESP_OK on success, or an error code on failure.
     */
    esp_err_t (*SetBrightness)(IBuiltinLED *self, uint8_t brightness);
    /* Transient worker overlay; does not alter the saved brightness. */
    esp_err_t (*PreviewBrightness)(IBuiltinLED *self, uint8_t brightness, uint32_t duration_ms);

    /**
     * @brief Retrieves the current status of the BuiltinLED module.
     * 
     * Args:
     *     self: Pointer to the IBuiltinLED instance.
     *     status: Pointer to the structure where status will be stored.
     * 
     * Returns:
     *     ESP_OK on success, or an error code on failure.
     */
    esp_err_t (*GetStatus)(IBuiltinLED *self, BuiltinLEDStatus *status);
    /** Copy the last recorded SDK result without a peripheral wait. Zero means
     * no recorded failure; it does not mean a queued command has executed.
     * This is separate from the fixed-size EventBus status payload and does
     * not prove physical light output or completion of a queued command. */
    esp_err_t (*GetDriverError)(IBuiltinLED *self, esp_err_t *error);
};

/**
 * @brief Retrieves the singleton instance of the BuiltinLED module.
 * 
 * Returns:
 *     Pointer to the IBuiltinLED interface.
 */
IBuiltinLED *BuiltinLED_GetModule(void);

/** Persist the global mode shared by CLI, HA and UI; success means saved. */
esp_err_t BuiltinLED_SaveEnabled(bool enabled);

/** Persist a decimal brightness setting; success means saved, not rendered. */
esp_err_t BuiltinLED_SaveBrightnessText(const char *value);

#endif // I_BUILTIN_LED_H
