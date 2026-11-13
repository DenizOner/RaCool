#ifndef FAN_MODE_ICONS_H
#define FAN_MODE_ICONS_H

#include "lvgl.h"
#include "Controllers/ClimateController/ClimateControllerConfig.h"

/* Fixed pixel artwork: 11x11 alpha masks, independent of product text fonts.
 * Order follows ClimateFanMode, not menu presentation or legacy protocol enums. */
static const uint8_t fan_mode_object_sensitive_pixels[] = {
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 255, 0, 255, 0, 255, 0, 255, 0, 0,
  0, 0, 255, 0, 255, 0, 255, 0, 255, 0, 0,
  0, 255, 255, 255, 255, 255, 255, 255, 255, 255, 0,
  0, 0, 255, 0, 255, 0, 255, 0, 255, 0, 0,
  0, 0, 255, 0, 255, 0, 255, 0, 255, 0, 0,
  0, 255, 255, 255, 255, 255, 255, 255, 255, 255, 0,
  0, 0, 255, 0, 255, 0, 255, 0, 255, 0, 0,
  0, 0, 255, 0, 255, 0, 255, 0, 255, 0, 0,
  0, 0, 255, 0, 0, 0, 0, 0, 255, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};
static const uint8_t fan_mode_ambient_delta_pixels[] = {
  0, 0, 0, 0, 0, 255, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 255, 0, 255, 0, 0, 0, 0,
  0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0,
  0, 0, 255, 0, 0, 0, 0, 0, 255, 0, 0,
  0, 255, 0, 0, 255, 0, 0, 0, 0, 255, 0,
  0, 0, 255, 0, 255, 0, 0, 0, 255, 0, 0,
  0, 0, 255, 0, 255, 0, 0, 0, 255, 0, 0,
  0, 0, 255, 0, 255, 0, 0, 0, 255, 0, 0,
  0, 0, 255, 255, 255, 255, 0, 0, 255, 0, 0,
  0, 0, 255, 0, 255, 0, 0, 0, 255, 0, 0,
  0, 0, 255, 255, 255, 255, 255, 255, 255, 0, 0,
};
static const uint8_t fan_mode_manual_on_pixels[] = {
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 255, 255, 0, 0, 0, 255, 255, 0, 0,
  0, 255, 0, 0, 255, 0, 255, 0, 0, 255, 0,
  255, 0, 0, 0, 0, 255, 0, 0, 0, 0, 255,
  255, 0, 0, 0, 0, 255, 0, 0, 0, 0, 255,
  0, 255, 0, 0, 255, 0, 255, 0, 0, 255, 0,
  0, 0, 255, 255, 0, 0, 0, 255, 255, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};
static const uint8_t fan_mode_manual_off_pixels[] = {
  255, 0, 0, 0, 0, 255, 255, 0, 0, 0, 0,
  0, 255, 0, 0, 255, 255, 255, 0, 0, 0, 0,
  0, 0, 255, 0, 0, 255, 255, 0, 0, 255, 0,
  0, 0, 0, 255, 0, 255, 0, 0, 255, 255, 0,
  0, 0, 0, 0, 255, 0, 0, 255, 255, 255, 0,
  0, 255, 255, 255, 255, 255, 255, 255, 255, 255, 0,
  0, 255, 255, 255, 0, 0, 255, 0, 0, 0, 0,
  0, 255, 255, 0, 0, 255, 0, 255, 0, 0, 0,
  0, 255, 0, 0, 255, 255, 0, 0, 255, 0, 0,
  0, 0, 0, 0, 255, 255, 255, 0, 0, 255, 0,
  0, 0, 0, 0, 255, 255, 0, 0, 0, 0, 255,
};

#define FAN_MODE_IMAGE(name) { .header = { .magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_A8, .w = 11, .h = 11, .stride = 11 }, .data_size = sizeof(fan_mode_##name##_pixels), .data = fan_mode_##name##_pixels }
static const lv_image_dsc_t fan_mode_images[] = {
  [CLIMATE_FAN_OBJECT_SENSITIVE] = FAN_MODE_IMAGE(object_sensitive),
  [CLIMATE_FAN_MANUAL_ON] = FAN_MODE_IMAGE(manual_on),
  [CLIMATE_FAN_MANUAL_OFF] = FAN_MODE_IMAGE(manual_off),
  [CLIMATE_FAN_AMBIENT_DELTA] = FAN_MODE_IMAGE(ambient_delta),
};
#undef FAN_MODE_IMAGE

#endif /* FAN_MODE_ICONS_H */
