/**
 * @file RainbowFlowAnim.c
 * @brief Animation plugin for smooth rainbow color flowing effect on LEDs.
 * Unified for Built-in LED and External RGB LED.
 */

#include "Hardware/BuiltinLED/IAnimationPlugin.h"
#include "Hardware/BuiltinLED/BuiltinLEDConfig.h"
#include "Hardware/RGBLED/Animations/RainbowFlowAnim.h"
#include "Hardware/RGBLED/RGBLEDConfig.h"
#include "esp_timer.h"
#include <stdatomic.h>

/* Full red-to-red sweep, shared by both LEDs. Increase to slow the flow. */
#define RAINBOW_CYCLE_DURATION_MS 6000U
_Static_assert(RAINBOW_CYCLE_DURATION_MS > 0U &&
               RAINBOW_CYCLE_DURATION_MS <= UINT32_MAX,
               "Rainbow cycle duration must fit a positive uint32_t");
#if defined(RACOOL_LED_PHASE_DIAGNOSTIC)
#include "esp_log.h"
#endif

#if RGBLED_ANIM_TICK_MS != BUILTIN_LED_FRAME_INTERVAL_MS
#error Synchronized startup Rainbow requires equal LED frame intervals.
#endif

/**
 * @brief Context structure for the Rainbow Flow animation.
 */
typedef LEDAnimationClockContext RainbowFlowContext;

static RainbowFlowContext rainbow_ctx = {0};
static RainbowFlowContext builtin_rainbow_ctx = {0};
static atomic_uint s_synchronized_epoch_ms;
static atomic_bool s_synchronized_phase;

void RainbowFlowAnim_BeginSynchronizedPhase(uint32_t epoch_ms) {
    atomic_store_explicit(&s_synchronized_epoch_ms, epoch_ms, memory_order_relaxed);
    atomic_store_explicit(&s_synchronized_phase, true, memory_order_release);
#if defined(RACOOL_LED_PHASE_DIAGNOSTIC)
    ESP_LOGI("LEDPHASE", "begin epoch=%lu", (unsigned long)epoch_ms);
#endif
}

void RainbowFlowAnim_EndSynchronizedPhase(void) {
#if defined(RACOOL_LED_PHASE_DIAGNOSTIC)
    ESP_LOGI("LEDPHASE", "end t=%llu", (unsigned long long)(esp_timer_get_time()/1000ULL));
#endif
    atomic_store_explicit(&s_synchronized_phase, false, memory_order_release);
}

bool RainbowFlowAnim_IsSynchronizedPhase(void) {
    return atomic_load_explicit(&s_synchronized_phase, memory_order_acquire);
}

void RainbowFlowAnim_Reset(void) {
    rainbow_ctx.timer = 0;
}

static RGBColor RainbowFlow_Render(RainbowFlowContext *ctx, uint32_t delta_ms, uint8_t brightness) {
    RGBColor color = {0, 0, 0};
    if (atomic_load_explicit(&s_synchronized_phase, memory_order_acquire)) {
        uint32_t now_ms = (uint32_t)((uint64_t)esp_timer_get_time() / 1000ULL);
        uint32_t epoch_ms = atomic_load_explicit(&s_synchronized_epoch_ms, memory_order_relaxed);
        uint32_t elapsed_ms = (uint32_t)(now_ms - epoch_ms);
        uint32_t shared_frame_ms = elapsed_ms - (elapsed_ms % BUILTIN_LED_FRAME_INTERVAL_MS);
        ctx->timer = shared_frame_ms % RAINBOW_CYCLE_DURATION_MS;
    } else {
        ctx->timer = (uint32_t)(((uint64_t)ctx->timer + delta_ms) % RAINBOW_CYCLE_DURATION_MS);
    }
    
    /* Separate elapsed milliseconds from the six 256-step color sectors. */
    uint16_t pos = (uint16_t)(((uint64_t)ctx->timer * 1536U) / RAINBOW_CYCLE_DURATION_MS);
    uint8_t sector = pos / 256;
    uint8_t rel = pos % 256;

    switch (sector) {
        case 0: 
            color.r = brightness; 
            color.g = (rel * brightness) / 255; 
            break;
        case 1: 
            color.g = brightness; 
            color.r = ((255 - rel) * brightness) / 255; 
            break;
        case 2: 
            color.g = brightness; 
            color.b = (rel * brightness) / 255; 
            break;
        case 3: 
            color.b = brightness; 
            color.g = ((255 - rel) * brightness) / 255; 
            break;
        case 4: 
            color.b = brightness; 
            color.r = (rel * brightness) / 255; 
            break;
        case 5: 
        default:
            color.r = brightness; 
            color.b = ((255 - rel) * brightness) / 255; 
            break;
    }
    return color;
}

/**
 * @brief Resets the rainbow flow animation timer for BuiltinLED plugin.
 */
static void RainbowFlow_Reset(AnimationPlugin *self) {
    if (self && self->context) ((RainbowFlowContext *)self->context)->timer = 0;
}

/**
 * @brief Updates the Rainbow Flow animation frame for BuiltinLED plugin.
 */
static LEDColor RainbowFlow_Update(AnimationPlugin *self, uint32_t delta_ms) {
    if (!self || !self->context) return (LEDColor){0};
    RGBColor c = RainbowFlow_Render(self->context, delta_ms, LEDAnimation_GetBrightness(self));
    return (LEDColor){ .r = c.r, .g = c.g, .b = c.b };
}

RGBColor RainbowFlowAnim_Update(uint32_t delta_ms, uint8_t brightness) {
    return RainbowFlow_Render(&rainbow_ctx, delta_ms, brightness);
}

static AnimationPlugin rainbow_plugin = {
    .type = RAINBOW_FLOW,
    .name = "Rainbow Flow",
    .Reset = RainbowFlow_Reset,
    .Update = RainbowFlow_Update,
    .context = &builtin_rainbow_ctx,
    .context_size = sizeof(builtin_rainbow_ctx),
};

/**
 * @brief Retrieves the Rainbow Flow animation plugin instance.
 */
AnimationPlugin *RainbowFlowAnim_Get(void) {
    return &rainbow_plugin;
}
