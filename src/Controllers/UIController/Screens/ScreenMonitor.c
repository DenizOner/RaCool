#include "Controllers/UIController/Screens/ScreenMonitor.h"
#include "Controllers/UIController/UIControllerInternal.h"
#include <stdio.h>
#include <math.h>
#include "Controllers/UIController/Screens/FanModeIcons.h"
#include "Hardware/OLED/OLEDFonts.h"

LV_FONT_DECLARE(lv_font_montserrat_10);
LV_FONT_DECLARE(lv_font_montserrat_20);

/* Three equal 15x21 cells below the right-hand divider. Center the visible
 * ink, not the widget box: font bearings and transparent bitmap padding differ. */
static void CenterStatusInk(lv_obj_t *object, unsigned slot,
                            int left, int top, int width, int height) {
  lv_obj_align(object, LV_ALIGN_TOP_LEFT,
      83 + (int)slot * 15 + (15 - width) / 2 - left,
      43 + (21 - height) / 2 - top);
}

static void CenterModeIcon(const lv_image_dsc_t *image) {
  int left = 11, top = 11, right = -1, bottom = -1;
  for (int y = 0; y < 11; ++y) for (int x = 0; x < 11; ++x) {
    if (image->data[y * image->header.stride + x] < 128) continue;
    if (x < left) left = x;
    if (x > right) right = x;
    if (y < top) top = y;
    if (y > bottom) bottom = y;
  }
  if (right >= left && bottom >= top)
    CenterStatusInk(uiControllerInstance.fanIcon, 1, left, top,
                    right - left + 1, bottom - top + 1);
}

bool ScreenMonitor_Create(void) {
  const LocalizationCatalog *catalog=Localization_Snapshot();
  uiControllerInstance.mainScreen = lv_obj_create(NULL);
  if (!uiControllerInstance.mainScreen) return false;
  lv_obj_set_style_bg_color(uiControllerInstance.mainScreen, lv_color_hex(0x000000), 0);
  lv_obj_set_style_pad_all(uiControllerInstance.mainScreen, 0, 0);
  lv_obj_set_style_border_width(uiControllerInstance.mainScreen, 0, 0);
  lv_obj_set_style_outline_width(uiControllerInstance.mainScreen, 0, 0);

  // Vertical line divider at X = 83
  lv_obj_t *divider = lv_obj_create(uiControllerInstance.mainScreen);
  if (!divider) return false;
  lv_obj_set_size(divider, 1, 64);
  lv_obj_align(divider, LV_ALIGN_TOP_LEFT, 83, 0);
  lv_obj_set_style_bg_color(divider, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_bg_opa(divider, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(divider, 0, 0);
  lv_obj_set_style_radius(divider, 0, 0);
  lv_obj_set_style_pad_all(divider, 0, 0);

  // Temperature Label
  uiControllerInstance.tempLabel = lv_label_create(uiControllerInstance.mainScreen);
  if (!uiControllerInstance.tempLabel) return false;
  lv_obj_align(uiControllerInstance.tempLabel, LV_ALIGN_TOP_LEFT, 6, 8);
  lv_obj_set_style_text_font(uiControllerInstance.tempLabel, &racool_font_20, 0);
  lv_obj_set_style_text_color(uiControllerInstance.tempLabel, lv_color_hex(0xFFFFFF), 0);
  lv_label_set_text(uiControllerInstance.tempLabel, "--.-");

  // Horizontal divider at Y = 44 (Left Section)
  lv_obj_t *h_line = lv_obj_create(uiControllerInstance.mainScreen);
  if (!h_line) return false;
  lv_obj_set_size(h_line, 83, 1);
  lv_obj_align(h_line, LV_ALIGN_TOP_LEFT, 0, 44);
  lv_obj_set_style_bg_color(h_line, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_bg_opa(h_line, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(h_line, 0, 0);
  lv_obj_set_style_radius(h_line, 0, 0);
  lv_obj_set_style_pad_all(h_line, 0, 0);

  // Status Label
  uiControllerInstance.statusLabel = lv_label_create(uiControllerInstance.mainScreen);
  if (!uiControllerInstance.statusLabel) return false;
  lv_obj_align(uiControllerInstance.statusLabel, LV_ALIGN_TOP_LEFT, 6, 48);
  lv_obj_set_style_text_font(uiControllerInstance.statusLabel, &racool_font_10, 0);
  lv_obj_set_size(uiControllerInstance.statusLabel,77,16);
  lv_label_set_long_mode(uiControllerInstance.statusLabel,LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
  lv_obj_set_style_text_color(uiControllerInstance.statusLabel, lv_color_hex(0xFFFFFF), 0);
  lv_label_set_text(uiControllerInstance.statusLabel,
      Localization_Text(catalog, LOC_DISPLAY_NO_SAMPLE));

  // Ambient Label (Right Section)
  uiControllerInstance.ambientLabel = lv_label_create(uiControllerInstance.mainScreen);
  if (!uiControllerInstance.ambientLabel) return false;
  lv_obj_align(uiControllerInstance.ambientLabel, LV_ALIGN_TOP_LEFT, 86, 4);
  lv_obj_set_style_text_font(uiControllerInstance.ambientLabel, &racool_font_10, 0);
  lv_obj_set_style_text_color(uiControllerInstance.ambientLabel, lv_color_hex(0xFFFFFF), 0);
  char ambientPlaceholder[128];
  snprintf(ambientPlaceholder,sizeof(ambientPlaceholder),"%s:--",Localization_Text(catalog,LOC_MONITOR_AMBIENT_PREFIX));
  lv_label_set_text(uiControllerInstance.ambientLabel, ambientPlaceholder);

  // Right divider line 1 (Y = 21)
  lv_obj_t *r_line1 = lv_obj_create(uiControllerInstance.mainScreen);
  if (!r_line1) return false;
  lv_obj_set_size(r_line1, 45, 1);
  lv_obj_align(r_line1, LV_ALIGN_TOP_LEFT, 83, 21);
  lv_obj_set_style_bg_color(r_line1, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_bg_opa(r_line1, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(r_line1, 0, 0);
  lv_obj_set_style_radius(r_line1, 0, 0);
  lv_obj_set_style_pad_all(r_line1, 0, 0);

  // CPU Label
  uiControllerInstance.cpuLabel = lv_label_create(uiControllerInstance.mainScreen);
  if (!uiControllerInstance.cpuLabel) return false;
  lv_obj_align(uiControllerInstance.cpuLabel, LV_ALIGN_TOP_LEFT, 86, 25);
  lv_obj_set_style_text_font(uiControllerInstance.cpuLabel, &racool_font_10, 0);
  lv_obj_set_style_text_color(uiControllerInstance.cpuLabel, lv_color_hex(0xFFFFFF), 0);
  lv_label_set_text(uiControllerInstance.cpuLabel, "C:--");

  // Right divider line 2 (Y = 42)
  lv_obj_t *r_line2 = lv_obj_create(uiControllerInstance.mainScreen);
  if (!r_line2) return false;
  lv_obj_set_size(r_line2, 45, 1);
  lv_obj_align(r_line2, LV_ALIGN_TOP_LEFT, 83, 42);
  lv_obj_set_style_bg_color(r_line2, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_bg_opa(r_line2, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(r_line2, 0, 0);
  lv_obj_set_style_radius(r_line2, 0, 0);
  lv_obj_set_style_pad_all(r_line2, 0, 0);

  // Status Icons
  uiControllerInstance.zigbeeIconClip = lv_obj_create(uiControllerInstance.mainScreen);
  if (!uiControllerInstance.zigbeeIconClip) return false;
  lv_obj_set_size(uiControllerInstance.zigbeeIconClip, 13, 11);
  lv_obj_set_style_pad_all(uiControllerInstance.zigbeeIconClip, 0, 0);
  lv_obj_set_style_border_width(uiControllerInstance.zigbeeIconClip, 0, 0);
  lv_obj_set_style_radius(uiControllerInstance.zigbeeIconClip, 0, 0);
  lv_obj_set_style_bg_color(uiControllerInstance.zigbeeIconClip, lv_color_hex(0), 0);
  uiControllerInstance.zigbeeIcon = lv_label_create(uiControllerInstance.zigbeeIconClip);
  if (!uiControllerInstance.zigbeeIcon) return false;
  lv_obj_align(uiControllerInstance.zigbeeIcon, LV_ALIGN_BOTTOM_LEFT, 0, 0);
  lv_obj_set_style_text_font(uiControllerInstance.zigbeeIcon, &lv_font_montserrat_10, 0);
  lv_label_set_text(uiControllerInstance.zigbeeIcon, LV_SYMBOL_WIFI);

  uiControllerInstance.fanIcon = lv_image_create(uiControllerInstance.mainScreen);
  if (!uiControllerInstance.fanIcon) return false;
  lv_image_set_src(uiControllerInstance.fanIcon, &fan_mode_images[CLIMATE_FAN_OBJECT_SENSITIVE]);
  CenterModeIcon(&fan_mode_images[CLIMATE_FAN_OBJECT_SENSITIVE]);
  lv_obj_set_style_image_recolor(uiControllerInstance.fanIcon, lv_color_white(), 0);
  lv_obj_set_style_image_recolor_opa(uiControllerInstance.fanIcon, LV_OPA_COVER, 0);

  uiControllerInstance.relayIcon = lv_label_create(uiControllerInstance.mainScreen);
  if (!uiControllerInstance.relayIcon) return false;
  /* Montserrat 10 U+F011 visible monochrome ink: x=0..9, y=0..9.
   * The font's 11-pixel line includes a blank final row. Font tests pin this. */
  CenterStatusInk(uiControllerInstance.relayIcon, 2, 0, 0, 10, 10);
  lv_obj_set_style_text_font(uiControllerInstance.relayIcon, &lv_font_montserrat_10, 0);
  lv_label_set_text(uiControllerInstance.relayIcon, LV_SYMBOL_POWER);
  return true;
}

void ScreenMonitor_Update(uint32_t nowMs, SystemSettings *settings,
                          float objectTemp, float ambientTemp, float cpuTemp,
                          bool isRelayOn, ClimateFanMode fanMode, bool isZigbeeConnected, uint8_t signalBars,
                          const LocalizationCatalog *catalog) {
  (void)nowMs;
  char buf[32];
  char unitChar = (settings->system.temp_unit == 1) ? 'F' : 'C';
  float dispTemp = (settings->system.temp_unit == 1)
                       ? (objectTemp * 1.8f + 32.0f) : objectTemp;

  if(isfinite(dispTemp)) snprintf(buf, sizeof(buf), "%.1f°%c", dispTemp, unitChar);
  else snprintf(buf,sizeof(buf),"--.-°%c",unitChar);
  lv_label_set_text(uiControllerInstance.tempLabel, buf);

  lv_label_set_text(uiControllerInstance.statusLabel, Localization_Text(catalog,
      isRelayOn ? LOC_DISPLAY_FAN_ACTIVE :
      isfinite(objectTemp) && isfinite(ambientTemp) && isfinite(cpuTemp) ?
      LOC_DISPLAY_SYSTEM_STABLE : LOC_DISPLAY_NO_SAMPLE));

  /* Crop the upper arcs of the existing glyph, preserving its dot and shape. */
  uint8_t height = signalBars >= 3 ? 11 : signalBars == 2 ? 7 : 4;
  lv_obj_set_size(uiControllerInstance.zigbeeIconClip, 13, height);
  /* U+F1EB retains its existing crop/shape. These ink bounds exclude the
   * label's blank bottom row and are checked against the actual font bitmap. */
  if (signalBars >= 3)
    CenterStatusInk(uiControllerInstance.zigbeeIconClip, 0, 0, 1, 12, 9);
  else if (signalBars == 2)
    CenterStatusInk(uiControllerInstance.zigbeeIconClip, 0, 2, 0, 8, 6);
  else
    CenterStatusInk(uiControllerInstance.zigbeeIconClip, 0, 5, 0, 3, 3);
  lv_obj_set_style_text_color(uiControllerInstance.zigbeeIcon,
      isZigbeeConnected ? lv_color_white() : lv_color_hex(0x111111), 0);
  /* An invalid caller mode cannot index beyond the immutable image table. */
  if ((unsigned)fanMode < sizeof(fan_mode_images) / sizeof(fan_mode_images[0])) {
    lv_image_set_src(uiControllerInstance.fanIcon, &fan_mode_images[fanMode]);
    CenterModeIcon(&fan_mode_images[fanMode]);
  }
  lv_obj_set_style_text_color(uiControllerInstance.relayIcon,
      isRelayOn ? lv_color_white() : lv_color_hex(0x111111), 0);

  float dispAmb = (settings->system.temp_unit == 1) ? (ambientTemp * 1.8f + 32.0f) : ambientTemp;
  float dispCpu = (settings->system.temp_unit == 1) ? (cpuTemp * 1.8f + 32.0f) : cpuTemp;

  if(isfinite(dispAmb)) snprintf(buf, sizeof(buf), "%s:%.1f°", Localization_Text(catalog, LOC_MONITOR_AMBIENT_PREFIX), dispAmb);
  else snprintf(buf,sizeof(buf),"%s:--.-°",Localization_Text(catalog,LOC_MONITOR_AMBIENT_PREFIX));
  lv_label_set_text(uiControllerInstance.ambientLabel, buf);

  if(isfinite(dispCpu)) snprintf(buf, sizeof(buf), "C:%.1f°", dispCpu);
  else snprintf(buf,sizeof(buf),"C:--.-°");
  lv_label_set_text(uiControllerInstance.cpuLabel, buf);

  if (lv_screen_active() != uiControllerInstance.mainScreen)
    lv_screen_load(uiControllerInstance.mainScreen);
}
