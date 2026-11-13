#include "Hardware/LEDAnimations/LEDAnimations.h"
#include "Hardware/LEDAnimations/LEDAnimationPatterns.h"

static void GenericReset(AnimationPlugin *plugin) {
    if (plugin && plugin->context) ((LEDAnimationClockContext *)plugin->context)->timer = 0;
}

static LEDColor GenericUpdate(AnimationPlugin *plugin, uint32_t elapsed_ms) {
    if (!plugin || !plugin->context || plugin->type < GENERIC_BREATHING ||
        plugin->type > GENERIC_COLOR_CYCLE) return (LEDColor){0};
    LEDAnimationPattern pattern = (LEDAnimationPattern)(plugin->type - GENERIC_BREATHING);
    LEDAnimationClockContext *context = plugin->context;
    uint32_t period = LEDAnimation_PatternPeriod(pattern);
    context->timer = period ? (uint32_t)(((uint64_t)context->timer + elapsed_ms) % period)
                            : context->timer + elapsed_ms;
    uint8_t brightness = LEDAnimation_GetBrightness(plugin);
    return LEDAnimation_RenderPattern(pattern, context->timer,
                                      (LEDColor){brightness, brightness, brightness});
}

static LEDAnimationClockContext context_Breathing;
static AnimationPlugin plugin_Breathing = {
    .type = GENERIC_BREATHING, .name = "Breathing", .Reset = GenericReset,
    .Update = GenericUpdate, .context = &context_Breathing,
    .context_size = sizeof(context_Breathing)
};
AnimationPlugin *GenericBreathingAnim_Get(void) { return &plugin_Breathing; }

static LEDAnimationClockContext context_Pulse;
static AnimationPlugin plugin_Pulse = {
    .type = GENERIC_PULSE, .name = "Pulse", .Reset = GenericReset,
    .Update = GenericUpdate, .context = &context_Pulse,
    .context_size = sizeof(context_Pulse)
};
AnimationPlugin *GenericPulseAnim_Get(void) { return &plugin_Pulse; }

static LEDAnimationClockContext context_Candle;
static AnimationPlugin plugin_Candle = {
    .type = GENERIC_CANDLE, .name = "Candle", .Reset = GenericReset,
    .Update = GenericUpdate, .context = &context_Candle,
    .context_size = sizeof(context_Candle)
};
AnimationPlugin *GenericCandleAnim_Get(void) { return &plugin_Candle; }

static LEDAnimationClockContext context_Alert;
static AnimationPlugin plugin_Alert = {
    .type = GENERIC_ALERT, .name = "Alert", .Reset = GenericReset,
    .Update = GenericUpdate, .context = &context_Alert,
    .context_size = sizeof(context_Alert)
};
AnimationPlugin *GenericAlertAnim_Get(void) { return &plugin_Alert; }

static LEDAnimationClockContext context_Fade;
static AnimationPlugin plugin_Fade = {
    .type = GENERIC_FADE, .name = "Fade", .Reset = GenericReset,
    .Update = GenericUpdate, .context = &context_Fade,
    .context_size = sizeof(context_Fade)
};
AnimationPlugin *GenericFadeAnim_Get(void) { return &plugin_Fade; }

static LEDAnimationClockContext context_ColorCycle;
static AnimationPlugin plugin_ColorCycle = {
    .type = GENERIC_COLOR_CYCLE, .name = "ColorCycle", .Reset = GenericReset,
    .Update = GenericUpdate, .context = &context_ColorCycle,
    .context_size = sizeof(context_ColorCycle)
};
AnimationPlugin *GenericColorCycleAnim_Get(void) { return &plugin_ColorCycle; }

