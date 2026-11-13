#include "Hardware/RGBLED/RGBLEDAnimations.h"
#include "Hardware/RGBLED/RGBLEDConfig.h"
#include "Hardware/RGBLED/RGBLEDColorEngine.h"
#include "Hardware/RGBLED/Animations/RainbowFlowAnim.h"
#include "Hardware/LEDAnimations/LEDAnimations.h"
#include <esp_log.h>
#include "Hardware/LEDAnimations/LEDAnimationPatterns.h"

static const char *TAG = "RGBLEDAnimEngine";

typedef struct {
    bool active;
    RGBLEDAnimType type;
    RGBLEDPriority priority;
    RGBColor baseColor;
    uint32_t total_duration_ms;
    uint32_t elapsed_total_ms;
    uint32_t state_time_ms; // Used for periodic tracking inside animation
} AnimState;

static AnimState current_anim = {0};
static LEDAnimationInstance catalog_instance;

static BuiltinLEDAnimationType CatalogType(RGBLEDAnimType type) {
    switch (type) {
        case RGBLED_ANIM_HEARTBEAT_RED: return HEARTBEAT_RED;
        case RGBLED_ANIM_BLINK_WHITE: return BLINK_WHITE;
        case RGBLED_ANIM_SOS_RED: return SOS_RED;
        case RGBLED_ANIM_BREATHING_GREEN: return BREATHING_GREEN;
        case RGBLED_ANIM_FADE_BLUE: return FADE_BLUE;
        case RGBLED_ANIM_STROBE_WHITE: return STROBE_WHITE;
        case RGBLED_ANIM_POLICE_STROBE: return POLICE_STROBE;
        case RGBLED_ANIM_SOLID_RED: return SOLID_RED;
        case RGBLED_ANIM_SOLID_GREEN: return SOLID_GREEN;
        case RGBLED_ANIM_SOLID_BLUE: return SOLID_BLUE;
        case RGBLED_ANIM_SOLID_WHITE: return SOLID_WHITE;
        case RGBLED_ANIM_BLINK_RED: return BLINK_RED;
        case RGBLED_ANIM_BREATHING_RED: return BREATHING_RED;
        case RGBLED_ANIM_DOUBLE_BLINK_WHITE: return DOUBLE_BLINK_WHITE;
        default: return BUILTIN_LED_ANIM_NONE;
    }
}

esp_err_t RGBLEDAnimEngine_Init(void) {
    RGBLEDAnimEngine_Deinit();
    return ESP_OK;
}

void RGBLEDAnimEngine_Deinit(void) {
    current_anim.active = false;
    current_anim.type = RGBLED_ANIM_NONE;
}

esp_err_t RGBLEDAnimEngine_Start(RGBLEDAnimType type, RGBColor baseColor,
                                  RGBLEDPriority priority, uint32_t duration_ms) {

    if ((unsigned)type >= RGBLED_ANIM_COUNT || type == RGBLED_ANIM_TEMPERATURE ||
        (unsigned)priority > RGBLED_PRIO_CRITICAL)
        return ESP_ERR_INVALID_ARG;
    if (type == RGBLED_ANIM_NONE) {
        return RGBLEDAnimEngine_Stop();
    }

    // Check priority - don't overwrite higher priority
    if (current_anim.active && priority < current_anim.priority) {
        ESP_LOGD(TAG, "Ignoring animation start: current priority %d > requested %d", 
                 current_anim.priority, priority);
        return ESP_ERR_INVALID_STATE;
    }

    BuiltinLEDAnimationType catalog_type = CatalogType(type);
    if (catalog_type != BUILTIN_LED_ANIM_NONE) {
        esp_err_t result = LEDAnimations_CreateInstance(catalog_type, &catalog_instance);
        if (result != ESP_OK) return result;
        catalog_instance.plugin.render_brightness = 255;
    }

    current_anim.active = true;
    current_anim.type = type;
    current_anim.priority = priority;
    current_anim.baseColor = baseColor;
    current_anim.total_duration_ms = duration_ms;
    current_anim.elapsed_total_ms = 0;
    current_anim.state_time_ms = 0;

    if (type == RGBLED_ANIM_RAINBOW) {
        RainbowFlowAnim_Reset();
    }

    ESP_LOGI(TAG, "Animation %d started (Prio: %d, Dur: %lu)", type, priority, duration_ms);
    return ESP_OK;
}

esp_err_t RGBLEDAnimEngine_Stop(void) {
    if (!current_anim.active) return ESP_OK;
    
    current_anim.active = false;
    current_anim.type = RGBLED_ANIM_NONE;
    current_anim.priority = RGBLED_PRIO_LOW;
    ESP_LOGD(TAG, "Animation stopped");
    return ESP_OK;
}

static LEDAnimationPattern GenericPattern(RGBLEDAnimType type) {
    switch (type) {
        case RGBLED_ANIM_BREATHING: return LED_PATTERN_BREATHING;
        case RGBLED_ANIM_PULSE: return LED_PATTERN_PULSE;
        case RGBLED_ANIM_CANDLE: return LED_PATTERN_CANDLE;
        case RGBLED_ANIM_ALERT: return LED_PATTERN_ALERT;
        case RGBLED_ANIM_FADE_IN_OUT: return LED_PATTERN_FADE;
        case RGBLED_ANIM_COLOR_CYCLE: return LED_PATTERN_COLOR_CYCLE;
        default: return LED_PATTERN_COUNT;
    }
}

bool RGBLEDAnimEngine_Tick(uint32_t elapsed_ms, RGBColor *out_color) {
    if (!out_color || !current_anim.active) return false;

    /* Compare before adding so a long scheduling gap cannot wrap the timeout. */
    if (current_anim.total_duration_ms > 0) {
        uint32_t remaining = current_anim.total_duration_ms - current_anim.elapsed_total_ms;
        if (elapsed_ms >= remaining) {
            RGBLEDAnimEngine_Stop();
            return false;
        }
        current_anim.elapsed_total_ms += elapsed_ms;
    }
    LEDAnimationPattern pattern = GenericPattern(current_anim.type);
    uint32_t period = LEDAnimation_PatternPeriod(pattern);
    if (period) {
        current_anim.state_time_ms = (uint32_t)(((uint64_t)current_anim.state_time_ms + elapsed_ms) % period);
    } else {
        /* Candle uses hashed elapsed-time knots instead of a repeating frame seed.
         * Rainbow owns its own bounded phase accumulator. */
        current_anim.state_time_ms += elapsed_ms;
    }

    if (CatalogType(current_anim.type) != BUILTIN_LED_ANIM_NONE) {
        LEDColor rendered = catalog_instance.plugin.Update(&catalog_instance.plugin, elapsed_ms);
        *out_color = (RGBColor){rendered.r, rendered.g, rendered.b};
        return true;
    }

    if (pattern != LED_PATTERN_COUNT) {
        RGBColor base = current_anim.type == RGBLED_ANIM_COLOR_CYCLE
            ? (RGBColor){255,255,255} : current_anim.baseColor;
        LEDColor rendered = LEDAnimation_RenderPattern(pattern, current_anim.state_time_ms,
                                                        (LEDColor){base.r,base.g,base.b});
        *out_color = (RGBColor){rendered.r, rendered.g, rendered.b};
    } else if (current_anim.type == RGBLED_ANIM_RAINBOW) {
        *out_color = RainbowFlowAnim_Update(elapsed_ms, 255);
    } else {
        *out_color = current_anim.baseColor;
    }

    return true;
}

bool RGBLEDAnimEngine_IsActive(void) {
    return current_anim.active;
}

RGBLEDAnimType RGBLEDAnimEngine_GetCurrentType(void) {
    return current_anim.active ? current_anim.type : RGBLED_ANIM_NONE;
}

RGBLEDPriority RGBLEDAnimEngine_GetCurrentPriority(void) {
    return current_anim.active ? current_anim.priority : RGBLED_PRIO_LOW;
}
