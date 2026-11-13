#ifndef RGBLED_HARDWARE_H
#define RGBLED_HARDWARE_H

#include "Hardware/RGBLED/RGBLEDTypes.h"
#include "Hardware/RGBLED/RGBLEDConfig.h"
#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Serialized by the RGB service state mutex. GPIO/channel/fade ownership is
 * retained if cleanup fails; retry Deinit before another Init. Only a fade
 * service installed by this driver is uninstalled. The SDK retains its shared
 * LEDC timer infrastructure. Operations return the first SDK failure while
 * attempting the remaining channels. Partial output is not reported as success.
 * The physical red/green pin mapping is intentionally preserved. */

/** Initialize an 8-bit LEDC configuration, rolling back partial setup on error. */
esp_err_t RGBLEDHardware_Init(const RGBLEDConfig *config);
/** Stop owned fades/channels and release pins; failed resources remain owned. */
esp_err_t RGBLEDHardware_Deinit(void);
/** Set output immediately; brightness is 0..255. Return the SDK result. */
esp_err_t RGBLEDHardware_SetColorImmediate(RGBColor color, uint8_t brightness);
/** Start a hardware fade; duration must fit the SDK's nonnegative int range. */
esp_err_t RGBLEDHardware_FadeToColor(RGBColor target, uint8_t brightness, uint32_t fade_ms);
/** Set black output immediately and report any SDK failure. */
esp_err_t RGBLEDHardware_TurnOff(void);

#ifdef __cplusplus
}
#endif

#endif // RGBLED_HARDWARE_H
