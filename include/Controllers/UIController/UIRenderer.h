#ifndef UI_RENDERER_H
#define UI_RENDERER_H

#include "Controllers/UIController/UIControllerInternal.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize and construct all LVGL screen objects.
 */
/* Returns false after rolling back partial objects; retains the active screen. */
bool UIRenderer_CreateUI(void);
/** Worker-only invalidation: refresh on the next allowed frame, without waking. */
void UIRenderer_Invalidate(void);

/**
 * @brief Run the drawing/refresh logic for OLED screen based on current state.
 * 
 * @param nowMs Current system timestamp in milliseconds.
 * @param settings Pointer to system settings.
 * @param cachedObjectTemp Current cached object temperature.
 * @param cachedAmbientTemp Current cached ambient temperature.
 * @param cachedCpuTemp Current cached CPU temperature.
 */
void UIRenderer_Update(uint32_t nowMs, SystemSettings *settings,
                       float cachedObjectTemp, float cachedAmbientTemp, float cachedCpuTemp);

#ifdef __cplusplus
}
#endif

#endif // UI_RENDERER_H
