#include "Hardware/LEDAnimations/LEDTransition.h"

void LEDTransition_Reset(LEDTransition *state, LEDFrame displayed) {
    *state = (LEDTransition){.start = displayed, .displayed = displayed};
}
void LEDTransition_Start(LEDTransition *state, uint32_t now_ms, uint32_t duration_ms) {
    state->start = state->displayed;
    state->start_ms = now_ms;
    state->duration_ms = duration_ms;
    state->active = duration_ms != 0;
}
static uint8_t blend(uint8_t from, uint8_t to, float weight) {
    return (uint8_t)((float)from + ((float)to - (float)from) * weight + 0.5f);
}
LEDFrame LEDTransition_Sample(LEDTransition *state, LEDFrame target, uint32_t now_ms) {
    uint32_t elapsed = now_ms - state->start_ms;
    if (!state->active || elapsed >= state->duration_ms) {
        state->active = false;
        state->displayed = target;
        return target;
    }
    float t = (float)elapsed / (float)state->duration_ms;
    float weight = t * t * (3.0f - 2.0f * t);
    state->displayed = (LEDFrame){
        blend(state->start.r, target.r, weight),
        blend(state->start.g, target.g, weight),
        blend(state->start.b, target.b, weight),
        blend(state->start.brightness, target.brightness, weight),
    };
    return state->displayed;
}
