#include "Controllers/UIController/Screens/ScreenBoot.h"
#include "Controllers/UIController/UIControllerInternal.h"
#include "Controllers/UIController/BootAnimation.h"
#include "Hardware/OLED/OLEDFonts.h"
#include "Core/Localization/Localization.h"
#include "Communication/Zigbee/ZigbeeConfig.h"
#include <stdio.h>

LV_FONT_DECLARE(lv_font_montserrat_10);
LV_FONT_DECLARE(lv_font_montserrat_20);

bool ScreenBoot_Create(void) {
  const LocalizationCatalog *catalog=Localization_Snapshot();
  uiControllerInstance.bootScreen = lv_obj_create(NULL);
  if (!uiControllerInstance.bootScreen) return false;
  lv_obj_set_style_bg_color(uiControllerInstance.bootScreen, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_pad_all(uiControllerInstance.bootScreen, 0, 0);
  lv_obj_set_style_border_width(uiControllerInstance.bootScreen, 0, 0);
  lv_obj_set_style_outline_width(uiControllerInstance.bootScreen, 0, 0);

  // Version label
  uiControllerInstance.bootVersionLabel = lv_label_create(uiControllerInstance.bootScreen);
  if (!uiControllerInstance.bootVersionLabel) return false;
  lv_obj_align(uiControllerInstance.bootVersionLabel, LV_ALIGN_TOP_RIGHT, -2, 2);
  lv_obj_set_style_text_font(uiControllerInstance.bootVersionLabel, &racool_font_10, 0);
  lv_obj_set_style_text_color(uiControllerInstance.bootVersionLabel, lv_color_hex(0xFFFFFF), 0);
  /* An asterisk marks a development build; the CLI exposes its full identity. */
  lv_obj_set_size(uiControllerInstance.bootVersionLabel,56,16);
  lv_obj_set_style_text_align(uiControllerInstance.bootVersionLabel,LV_TEXT_ALIGN_RIGHT,0);
  lv_label_set_long_mode(uiControllerInstance.bootVersionLabel,LV_LABEL_LONG_MODE_CLIP);
  lv_label_set_text(uiControllerInstance.bootVersionLabel, RACOOL_PRODUCT_VERSION_SHORT);
  lv_obj_set_hidden(uiControllerInstance.bootVersionLabel, true);

  // Logo
  uiControllerInstance.bootLogoLabel = lv_label_create(uiControllerInstance.bootScreen);
  if (!uiControllerInstance.bootLogoLabel) return false;
  lv_obj_set_size(uiControllerInstance.bootLogoLabel,64,22);
  lv_obj_align(uiControllerInstance.bootLogoLabel, LV_ALIGN_TOP_LEFT, 4, 0);
  lv_label_set_long_mode(uiControllerInstance.bootLogoLabel,LV_LABEL_LONG_MODE_CLIP);
  lv_obj_set_style_text_font(uiControllerInstance.bootLogoLabel, &racool_font_14, 0);
  lv_obj_set_style_text_color(uiControllerInstance.bootLogoLabel, lv_color_hex(0xFFFFFF), 0);
  lv_label_set_text(uiControllerInstance.bootLogoLabel, Localization_Text(catalog,LOC_BRAND_NAME));
  lv_obj_set_hidden(uiControllerInstance.bootLogoLabel, true);

  // Top line (Y=26)
  uiControllerInstance.bootTopLine = lv_obj_create(uiControllerInstance.bootScreen);
  if (!uiControllerInstance.bootTopLine) return false;
  lv_obj_set_size(uiControllerInstance.bootTopLine, 0, 1);
  lv_obj_align(uiControllerInstance.bootTopLine, LV_ALIGN_TOP_LEFT, 0, 26);
  lv_obj_set_style_bg_color(uiControllerInstance.bootTopLine, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_bg_opa(uiControllerInstance.bootTopLine, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(uiControllerInstance.bootTopLine, 0, 0);
  lv_obj_set_style_radius(uiControllerInstance.bootTopLine, 0, 0);
  lv_obj_set_style_pad_all(uiControllerInstance.bootTopLine, 0, 0);
  lv_obj_set_hidden(uiControllerInstance.bootTopLine, true);

  // Live startup stage
  uiControllerInstance.bootTaglineLabel = lv_label_create(uiControllerInstance.bootScreen);
  if (!uiControllerInstance.bootTaglineLabel) return false;
  lv_obj_align(uiControllerInstance.bootTaglineLabel, LV_ALIGN_TOP_MID, 0, 27);
  lv_obj_set_style_text_font(uiControllerInstance.bootTaglineLabel, &racool_font_10, 0);
  lv_obj_set_size(uiControllerInstance.bootTaglineLabel,128,16);
  lv_obj_set_style_text_align(uiControllerInstance.bootTaglineLabel,LV_TEXT_ALIGN_CENTER,0);
  lv_label_set_long_mode(uiControllerInstance.bootTaglineLabel,LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
  lv_obj_set_style_text_color(uiControllerInstance.bootTaglineLabel, lv_color_hex(0xFFFFFF), 0);
  lv_label_set_text(uiControllerInstance.bootTaglineLabel, Localization_Text(catalog,LOC_BOOT_INITIALIZING));
  lv_obj_set_hidden(uiControllerInstance.bootTaglineLabel, true);

  // Bottom divider at Y=43
  uiControllerInstance.bootBottomLine = lv_obj_create(uiControllerInstance.bootScreen);
  if (!uiControllerInstance.bootBottomLine) return false;
  lv_obj_set_size(uiControllerInstance.bootBottomLine, 128, 1);
  lv_obj_align(uiControllerInstance.bootBottomLine, LV_ALIGN_TOP_LEFT, 0, 43);
  lv_obj_set_style_bg_color(uiControllerInstance.bootBottomLine, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_bg_opa(uiControllerInstance.bootBottomLine, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(uiControllerInstance.bootBottomLine, 0, 0);
  lv_obj_set_style_radius(uiControllerInstance.bootBottomLine, 0, 0);
  lv_obj_set_style_pad_all(uiControllerInstance.bootBottomLine, 0, 0);
  lv_obj_set_hidden(uiControllerInstance.bootBottomLine, true);

  // Progress bar occupies Y=44..46
  uiControllerInstance.bootProgressBar = lv_bar_create(uiControllerInstance.bootScreen);
  if (!uiControllerInstance.bootProgressBar) return false;
  lv_obj_set_size(uiControllerInstance.bootProgressBar, 120, 3);
  lv_obj_align(uiControllerInstance.bootProgressBar, LV_ALIGN_TOP_MID, 0, 44);
  lv_obj_set_style_bg_color(uiControllerInstance.bootProgressBar, lv_color_hex(0x000000), 0);
  lv_obj_set_style_border_width(uiControllerInstance.bootProgressBar, 1, 0);
  lv_obj_set_style_border_color(uiControllerInstance.bootProgressBar, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_bg_color(uiControllerInstance.bootProgressBar, lv_color_hex(0xFFFFFF), LV_PART_INDICATOR);
  lv_obj_set_style_radius(uiControllerInstance.bootProgressBar, 0, 0);
  lv_obj_set_style_radius(uiControllerInstance.bootProgressBar, 0, LV_PART_INDICATOR);
  lv_bar_set_range(uiControllerInstance.bootProgressBar, 0, 100);
  lv_bar_set_value(uiControllerInstance.bootProgressBar, 0, LV_ANIM_OFF);
  lv_obj_set_hidden(uiControllerInstance.bootProgressBar, true);

  // Status label occupies Y=48..63
  uiControllerInstance.bootStatusLabel = lv_label_create(uiControllerInstance.bootScreen);
  if (!uiControllerInstance.bootStatusLabel) return false;
  lv_obj_align(uiControllerInstance.bootStatusLabel, LV_ALIGN_TOP_LEFT, 4, 48);
  lv_obj_set_style_text_font(uiControllerInstance.bootStatusLabel, &racool_font_10, 0);
  lv_obj_set_size(uiControllerInstance.bootStatusLabel,120,16);
  lv_obj_set_style_text_align(uiControllerInstance.bootStatusLabel,LV_TEXT_ALIGN_CENTER,0);
  lv_label_set_long_mode(uiControllerInstance.bootStatusLabel,LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
  lv_obj_set_style_text_color(uiControllerInstance.bootStatusLabel, lv_color_hex(0xFFFFFF), 0);
  lv_label_set_text(uiControllerInstance.bootStatusLabel, Localization_Text(catalog,LOC_BOOT_INITIALIZING));
  lv_obj_set_hidden(uiControllerInstance.bootStatusLabel, true);

  // Dissolve Overlay
  uiControllerInstance.bootDissolveOverlay = lv_obj_create(uiControllerInstance.bootScreen);
  if (!uiControllerInstance.bootDissolveOverlay) return false;
  lv_obj_set_size(uiControllerInstance.bootDissolveOverlay, 128, 64);
  lv_obj_align(uiControllerInstance.bootDissolveOverlay, LV_ALIGN_TOP_LEFT, 0, 0);
  lv_obj_set_style_bg_color(uiControllerInstance.bootDissolveOverlay, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(uiControllerInstance.bootDissolveOverlay, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(uiControllerInstance.bootDissolveOverlay, 0, 0);
  lv_obj_set_style_radius(uiControllerInstance.bootDissolveOverlay, 0, 0);
  lv_obj_set_style_pad_all(uiControllerInstance.bootDissolveOverlay, 0, 0);
  lv_obj_set_hidden(uiControllerInstance.bootDissolveOverlay, true);

  /* The renderer activates this screen only after the entire UI exists. */
  return true;
}

bool ScreenBoot_Tick(uint32_t nowMs) {
  if (BootAnimation_IsRunning()) {
    BootAnimation_Tick(nowMs);
    return true;
  }
  return false;
}

bool ScreenBoot_IsRunning(void) {
  return BootAnimation_IsRunning();
}
