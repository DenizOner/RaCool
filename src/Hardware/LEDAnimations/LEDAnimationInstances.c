#include "Hardware/LEDAnimations/LEDAnimations.h"
#include <string.h>

AnimationPlugin *LEDAnimations_GetTemplate(BuiltinLEDAnimationType type) {
    static AnimationPlugin *(*const factories[BUILTIN_LED_ANIM_COUNT])(void) = {
        HeartbeatAnim_Get,
        BlinkWhiteAnim_Get,
        SOSRedAnim_Get,
        BreathingGreenAnim_Get,
        FadeBlueAnim_Get,
        StrobeWhiteAnim_Get,
        RainbowFlowAnim_Get,
        PoliceStrobeAnim_Get,
        SolidRedAnim_Get,
        SolidGreenAnim_Get,
        SolidBlueAnim_Get,
        SolidWhiteAnim_Get,
        BlinkRedAnim_Get,
        BreathingRedAnim_Get,
        DoubleBlinkWhiteAnim_Get,
        GenericBreathingAnim_Get,
        GenericPulseAnim_Get,
        GenericCandleAnim_Get,
        GenericAlertAnim_Get,
        GenericFadeAnim_Get,
        GenericColorCycleAnim_Get,

    };
    if ((unsigned)type >= BUILTIN_LED_ANIM_COUNT || !factories[type]) return NULL;
    return factories[type]();
}

esp_err_t LEDAnimations_CreateInstance(BuiltinLEDAnimationType type,
                                       LEDAnimationInstance *instance) {
    if (!instance) return ESP_ERR_INVALID_ARG;
    AnimationPlugin *source = LEDAnimations_GetTemplate(type);
    if (!source) return ESP_ERR_INVALID_ARG;
    if (source->type != type || !source->Update ||
        (source->context_size != 0 &&
         source->context_size != sizeof(LEDAnimationClockContext) &&
         source->context_size != sizeof(LEDAnimationBreathingContext)) ||
        ((source->context != NULL) != (source->context_size != 0)))
        return ESP_ERR_INVALID_STATE;
    AnimationPlugin metadata = *source;
    memset(instance, 0, sizeof(*instance));
    instance->plugin = metadata;
    instance->plugin.context = source->context_size == sizeof(LEDAnimationClockContext)
        ? (void *)&instance->state.clock
        : source->context_size == sizeof(LEDAnimationBreathingContext)
            ? (void *)&instance->state.breathing : NULL;
    if (instance->plugin.Reset) instance->plugin.Reset(&instance->plugin);
    return ESP_OK;
}
