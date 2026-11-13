#ifndef RGBLED_MENU_PLUGIN_H
#define RGBLED_MENU_PLUGIN_H

#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Startup/shutdown owner serializes these calls. Registration is atomic;
 * a failed removal retains ownership for retry after active callbacks return. */
esp_err_t RGBLEDMenuPlugin_Init(void);
esp_err_t RGBLEDMenuPlugin_Deinit(void);

#ifdef __cplusplus
}
#endif

#endif // RGBLED_MENU_PLUGIN_H
