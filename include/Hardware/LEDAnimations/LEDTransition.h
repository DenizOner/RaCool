#ifndef LED_TRANSITION_H
#define LED_TRANSITION_H
#include <stdbool.h>
#include <stdint.h>

#define LED_TRANSITION_DEFAULT_MS 300U
#define LED_TRANSITION_FRAME_MS 20U

typedef struct { uint8_t r, g, b, brightness; } LEDFrame;
typedef struct {
    LEDFrame start;
    LEDFrame displayed;
    uint32_t start_ms;
    uint32_t duration_ms;
    bool active;
} LEDTransition;

/** Rebase on the last rendered frame, including a transition already in progress. */
void LEDTransition_Start(LEDTransition *state, uint32_t now_ms, uint32_t duration_ms);
/** Follow a moving animation target without restarting its phase or the transition. */
LEDFrame LEDTransition_Sample(LEDTransition *state, LEDFrame target, uint32_t now_ms);
/** Cancel all interpolation for immediate lifecycle shutdown. */
void LEDTransition_Reset(LEDTransition *state, LEDFrame displayed);
#endif
