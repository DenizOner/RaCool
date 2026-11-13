#ifndef RACOOL_TEMPERATURE_SOURCES_H
#define RACOOL_TEMPERATURE_SOURCES_H
#include "Management/ConfigurationManager/SystemConfigs.h"
#include <math.h>
#include <stdint.h>
#include <stdbool.h>

typedef enum { TEMPERATURE_WAITING_FOR_UPDATE, TEMPERATURE_VALID, TEMPERATURE_STALE } TemperatureSlotState;
typedef enum { TEMPERATURE_SOURCE_NORMAL, TEMPERATURE_SOURCE_FALLBACK, TEMPERATURE_SOURCE_INVALID } TemperatureSourceStatus;
typedef struct {
    float value;
    bool valid;
    uint32_t last_update_ms;
} ExternalTemperatureSlot;
typedef struct {
    float value;
    bool valid;
    uint8_t configured_source;
    uint8_t effective_source;
    TemperatureSourceStatus status;
    TemperatureSlotState configured_state;
    uint32_t age_ms;
} EffectiveTemperature;
typedef struct {
    ControlConfigs config;
    EffectiveTemperature object;
    EffectiveTemperature ambient;
    float temperature_difference;
    bool difference_valid;
    ExternalTemperatureSlot slots[3];
    uint32_t now_ms;
} TemperatureSourceSnapshot;

/* Caller serializes slot mutation; timestamps are acquisition/admission times.
 * Runtime slots are deliberately absent from ControlConfigs/NVS. */
static inline bool TemperatureSources_IsInputValid(float value) {
    return isfinite(value) && value >= EXTERNAL_TEMPERATURE_MIN_C && value <= EXTERNAL_TEMPERATURE_MAX_C;
}
static inline TemperatureSlotState TemperatureSources_SlotState(const ExternalTemperatureSlot *slot, uint32_t now) {
    if (!slot->valid) return TEMPERATURE_WAITING_FOR_UPDATE;
    return (uint32_t)(now - slot->last_update_ms) >= EXTERNAL_TEMPERATURE_STALE_MS ? TEMPERATURE_STALE : TEMPERATURE_VALID;
}
static inline EffectiveTemperature TemperatureSources_Resolve(uint8_t source,
    float internal, bool internal_valid, uint32_t internal_age,
    const ExternalTemperatureSlot slots[3], uint32_t now) {
    EffectiveTemperature result = {.value = internal, .valid = internal_valid && TemperatureSources_IsInputValid(internal),
        .configured_source = source, .effective_source = 0, .age_ms = internal_age,
        .configured_state = TEMPERATURE_WAITING_FOR_UPDATE};
    if (source > 0 && source < TEMPERATURE_SOURCE_COUNT) {
        const ExternalTemperatureSlot *slot = &slots[source - 1];
        result.configured_state = TemperatureSources_SlotState(slot, now);
        if (result.configured_state == TEMPERATURE_VALID && TemperatureSources_IsInputValid(slot->value)) {
            result.value = slot->value; result.valid = true;
            result.effective_source = source;
            result.age_ms = (uint32_t)(now - slot->last_update_ms);
        }
    }
    result.status = !result.valid ? TEMPERATURE_SOURCE_INVALID :
        result.effective_source != source ? TEMPERATURE_SOURCE_FALLBACK : TEMPERATURE_SOURCE_NORMAL;
    return result;
}
static inline void TemperatureSources_ComputeDifference(TemperatureSourceSnapshot *snapshot) {
    snapshot->difference_valid = snapshot->object.valid && snapshot->ambient.valid;
    snapshot->temperature_difference = snapshot->difference_valid ?
        snapshot->object.value - snapshot->ambient.value : NAN;
}
/* Pure hysteresis: equality belongs to the switching boundary. */
static inline bool FanControl_EvaluateObjectSensitive(bool current, float object, float on, float off) {
    return current ? object > off : object >= on;
}
static inline bool FanControl_EvaluateAmbientDelta(bool current, float delta, int16_t on, int16_t off) {
    if (!isfinite(delta) || delta < -250 || delta > 250) return true;
    /* Match signed ZCL hundredths without binary32 subtraction boundary noise. */
    int32_t centic = (int32_t)lroundf(delta * 100);
    return current ? centic > (int32_t)off * 10 : centic >= (int32_t)on * 10;
}
static inline bool FanControl_Evaluate(bool current, const ControlConfigs *config, const TemperatureSourceSnapshot *temperature) {
    if (config->fan_manual_mode) return config->fan_manual_state;
    /* Invalid automatic inputs fail safe ON. CPU protection is owned above this layer. */
    if (!temperature->object.valid || (config->fan_strategy == 1 && !temperature->difference_valid)) return true;
    return config->fan_strategy == 1 ? FanControl_EvaluateAmbientDelta(current,
        temperature->temperature_difference, config->fan_delta_on_decic, config->fan_delta_off_decic) :
        FanControl_EvaluateObjectSensitive(current, temperature->object.value, config->relay_open_temp, config->relay_close_temp);
}
#endif
