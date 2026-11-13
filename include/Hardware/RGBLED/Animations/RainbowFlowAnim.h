/**
 * @file RainbowFlowAnim.h
 * @brief Public interface for External RGB LED Rainbow Flow animation plugin.
 */

#ifndef RAINBOW_FLOW_ANIM_H
#define RAINBOW_FLOW_ANIM_H

#include "Hardware/RGBLED/RGBLEDTypes.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Resets the Rainbow Flow animation state and timer.
 */
void RainbowFlowAnim_Reset(void);

/**
 * @brief Lock both LED players to a shared monotonic animation epoch.
 *
 * Used only while the startup splash owns the LED startup effects. Each player
 * derives the same Rainbow phase from this epoch, regardless of task wake time.
 */
void RainbowFlowAnim_BeginSynchronizedPhase(uint32_t epoch_ms);

/** @brief Return Rainbow players to their independent elapsed-time clocks. */
void RainbowFlowAnim_EndSynchronizedPhase(void);

/** @brief Whether the splash currently owns the shared startup timeline. */
bool RainbowFlowAnim_IsSynchronizedPhase(void);

/**
 * @brief Updates the Rainbow Flow animation and calculates the output RGB color.
 * 
 * @param delta_ms Milliseconds elapsed since the last update.
 * @param brightness Brightness scale factor (0-255).
 * @return Computed RGBColor.
 */
RGBColor RainbowFlowAnim_Update(uint32_t delta_ms, uint8_t brightness);

#ifdef __cplusplus
}
#endif

#endif // RAINBOW_FLOW_ANIM_H
