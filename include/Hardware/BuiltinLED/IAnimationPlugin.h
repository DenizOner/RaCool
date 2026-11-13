#ifndef I_ANIMATION_PLUGIN_H
#define I_ANIMATION_PLUGIN_H

#include "Hardware/BuiltinLED/BuiltinLEDConfig.h"
#include <stdint.h>
#include <stddef.h>

/**
 * @brief RGB color representation for a single WS2812 LED pixel.
 *
 * Each channel ranges from 0 to 255. Plugins are expected to produce colors
 * at the nominal brightness defined by BUILTIN_LED_DEFAULT_BRIGHTNESS; the
 * BuiltinLED driver applies global brightness scaling before hardware output.
 * Smooth built-in catalog effects instead receive the actual output ceiling
 * through render_brightness and quantize once; custom plugins retain the
 * nominal-scale contract.
 */
typedef struct {
    uint8_t r;  ///< Red channel intensity (0â€“255).
    uint8_t g;  ///< Green channel intensity (0â€“255).
    uint8_t b;  ///< Blue channel intensity (0â€“255).
} LEDColor;

/**
 * @brief Animation plugin interface for the BuiltinLED subsystem.
 *
 * Every animation â€” built-in or externally registered â€” must conform to this
 * interface. The plugin system is the sole rendering path; ad-hoc inline
 * rendering is not permitted.
 *
 * Lifecycle:
 *   1. Plugin is registered via BuiltinLED->RegisterAnimation().
 *   2. When selected, Reset() is called to initialise internal state.
 *   3. Update() is called at ~50 FPS, returning the color for that frame.
 *
 * Thread-safety:
 *   Update() runs in the animation task; Reset() also runs during service
 *   initialization. Both run while the service mutex is held. Callbacks must
 *   remain bounded and nonblocking. Plugins MUST NOT call any BuiltinLED
 *   API functions from inside these callbacks.
 */
typedef struct AnimationPlugin AnimationPlugin;

/* Shared state types allow independent catalog storage without type-punning. */
typedef struct { uint32_t timer; } LEDAnimationClockContext;
/* Breathing no longer keeps a frame-rate-dependent quantization accumulator. */
typedef LEDAnimationClockContext LEDAnimationBreathingContext;

struct AnimationPlugin {
    BuiltinLEDAnimationType type;  ///< Unique type ID â€” must match the enum value.
    const char             *name;  ///< Human-readable name (e.g. "Heartbeat Red").

    /**
     * @brief Resets the animation to its initial frame.
     *
     * Called every time this animation is newly selected to prevent visual
     * artifacts carried over from a previous run. Implementations must reset
     * all internal timer and state variables stored in `context`.
     *
     * Args:
     *     self: Pointer to the AnimationPlugin instance.
     */
    void (*Reset)(AnimationPlugin *self);

    /**
     * @brief Advances the animation by one frame and returns the pixel color.
     *
     * Plugins should produce colors at BUILTIN_LED_DEFAULT_BRIGHTNESS scale.
     * Global brightness is applied by the driver after this call returns.
     *
     * Args:
     *     self:     Pointer to the AnimationPlugin instance.
     *     delta_ms: Milliseconds elapsed since the previous Update() call.
     *
     * Returns:
     *     LEDColor for the current animation frame.
     */
    LEDColor (*Update)(AnimationPlugin *self, uint32_t delta_ms);

    /**
     * @brief Plugin-private context data (animation state variables).
     *
     * Typically points to a statically-allocated struct containing timers
     * and other state required for the animation computation.
     */
    void *context;

    /** Size of the private state, used only for caller-owned catalog instances. */
    size_t context_size;

    /** Nominal renderer intensity; zero retains the legacy default. */
    uint8_t render_brightness;
};

static inline uint8_t LEDAnimation_GetBrightness(const AnimationPlugin *plugin) {
    return plugin && plugin->render_brightness
        ? plugin->render_brightness : BUILTIN_LED_DEFAULT_BRIGHTNESS;
}

static inline uint8_t LEDAnimation_GetScaledBrightness(const AnimationPlugin *plugin,
                                                       uint32_t multiplier) {
    uint64_t value = (uint64_t)LEDAnimation_GetBrightness(plugin) * multiplier;
    return value > 255U ? 255U : (uint8_t)value;
}

#endif // I_ANIMATION_PLUGIN_H
