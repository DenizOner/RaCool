#include "Controllers/UIController/Screens/ScreenZigbeePair.h"
#include "Controllers/UIController/UIControllerInternal.h"
#include "Hardware/OLED/OLEDFonts.h"
#include <stdio.h>

LV_FONT_DECLARE(lv_font_montserrat_10);

/* Module-private pairing timer state */
static uint32_t s_pairStartTimeMs = 0;
static bool     s_pairTimedOut    = false;

bool ScreenZigbeePair_Create(void) {
  const LocalizationCatalog *catalog=Localization_Snapshot();
  uiControllerInstance.zigbeePairScreen = lv_obj_create(NULL);
  if (!uiControllerInstance.zigbeePairScreen) return false;
  lv_obj_set_size(uiControllerInstance.zigbeePairScreen, 128, 64);
  lv_obj_set_scrollable(uiControllerInstance.zigbeePairScreen, false);
  lv_obj_set_style_bg_color(uiControllerInstance.zigbeePairScreen, lv_color_hex(0x000000), 0);
  lv_obj_set_style_radius(uiControllerInstance.zigbeePairScreen, 0, 0);
  lv_obj_set_style_border_width(uiControllerInstance.zigbeePairScreen, 0, 0);
  lv_obj_set_style_pad_all(uiControllerInstance.zigbeePairScreen, 0, 0);

  // Breadcrumb / Title
  uiControllerInstance.zbBreadcrumb = lv_label_create(uiControllerInstance.zigbeePairScreen);
  if (!uiControllerInstance.zbBreadcrumb) return false;
  lv_obj_set_size(uiControllerInstance.zbBreadcrumb, 128, 16);
  lv_obj_align(uiControllerInstance.zbBreadcrumb, LV_ALIGN_TOP_LEFT, 0, 0);
  lv_obj_set_style_text_font(uiControllerInstance.zbBreadcrumb, &racool_font_10, 0);
  lv_obj_set_style_border_side(uiControllerInstance.zbBreadcrumb, LV_BORDER_SIDE_BOTTOM, 0);
  lv_obj_set_style_border_width(uiControllerInstance.zbBreadcrumb, 1, 0);
  lv_obj_set_style_border_color(uiControllerInstance.zbBreadcrumb, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_color(uiControllerInstance.zbBreadcrumb, lv_color_hex(0xFFFFFF), 0);
  lv_label_set_text(uiControllerInstance.zbBreadcrumb, Localization_Text(catalog,LOC_PAIRING_BREADCRUMB));

  // Status line (Y: 16)
  uiControllerInstance.zbStatusLabel = lv_label_create(uiControllerInstance.zigbeePairScreen);
  if (!uiControllerInstance.zbStatusLabel) return false;
  lv_obj_set_size(uiControllerInstance.zbStatusLabel, 124, 16);
  lv_obj_align(uiControllerInstance.zbStatusLabel, LV_ALIGN_TOP_LEFT, 2, 16);
  lv_obj_set_style_text_font(uiControllerInstance.zbStatusLabel, &racool_font_10, 0);
  lv_obj_set_style_text_color(uiControllerInstance.zbStatusLabel, lv_color_hex(0xFFFFFF), 0);
  lv_label_set_text(uiControllerInstance.zbStatusLabel, Localization_Text(catalog,LOC_PAIRING_READY));

  // Info line (Y: 32)
  uiControllerInstance.zbInfoLabel = lv_label_create(uiControllerInstance.zigbeePairScreen);
  if (!uiControllerInstance.zbInfoLabel) return false;
  lv_obj_set_size(uiControllerInstance.zbInfoLabel, 124, 16);
  lv_obj_align(uiControllerInstance.zbInfoLabel, LV_ALIGN_TOP_LEFT, 2, 32);
  lv_obj_set_style_text_font(uiControllerInstance.zbInfoLabel, &racool_font_10, 0);
  lv_obj_set_style_text_color(uiControllerInstance.zbInfoLabel, lv_color_hex(0xFFFFFF), 0);
  lv_label_set_text(uiControllerInstance.zbInfoLabel, Localization_Text(catalog,LOC_PAIRING_CHANNEL_AUTO));

  // Countdown shares the info row (Y: 32)
  uiControllerInstance.zbCountdownLabel = lv_label_create(uiControllerInstance.zigbeePairScreen);
  if (!uiControllerInstance.zbCountdownLabel) return false;
  lv_obj_set_size(uiControllerInstance.zbCountdownLabel, 124, 16);
  lv_obj_align(uiControllerInstance.zbCountdownLabel, LV_ALIGN_TOP_LEFT, 2, 32);
  lv_obj_set_style_text_font(uiControllerInstance.zbCountdownLabel, &racool_font_10, 0);
  lv_obj_set_style_text_color(uiControllerInstance.zbCountdownLabel, lv_color_hex(0xFFFFFF), 0);
  lv_label_set_text(uiControllerInstance.zbCountdownLabel, Localization_Text(catalog,LOC_PAIRING_OPEN_NETWORK));

  // Footer container (Y: 48, 16px)
  lv_obj_t *zbFooter = lv_obj_create(uiControllerInstance.zigbeePairScreen);
  if (!zbFooter) return false;
  lv_obj_set_size(zbFooter, 128, 16);
  lv_obj_align(zbFooter, LV_ALIGN_TOP_LEFT, 0, 48);
  lv_obj_set_style_bg_color(zbFooter, lv_color_hex(0x000000), 0);
  lv_obj_set_style_radius(zbFooter, 0, 0);
  lv_obj_set_style_pad_all(zbFooter, 0, 0);
  lv_obj_set_style_border_side(zbFooter, LV_BORDER_SIDE_TOP, 0);
  lv_obj_set_style_border_width(zbFooter, 0, 0);
  lv_obj_set_style_border_color(zbFooter, lv_color_hex(0xFFFFFF), 0);

  // Left Action Button
  uiControllerInstance.zbActionBtn = lv_obj_create(zbFooter);
  if (!uiControllerInstance.zbActionBtn) return false;
  lv_obj_set_size(uiControllerInstance.zbActionBtn, 62, 16);
  lv_obj_align(uiControllerInstance.zbActionBtn, LV_ALIGN_TOP_LEFT, 1, 0);
  lv_obj_set_style_radius(uiControllerInstance.zbActionBtn, 0, 0);
  lv_obj_set_style_pad_all(uiControllerInstance.zbActionBtn, 0, 0);
  lv_obj_set_style_border_width(uiControllerInstance.zbActionBtn, 0, 0);
  uiControllerInstance.zbActionLabel = lv_label_create(uiControllerInstance.zbActionBtn);
  if (!uiControllerInstance.zbActionLabel) return false;
  lv_obj_align(uiControllerInstance.zbActionLabel, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_text_font(uiControllerInstance.zbActionLabel, &racool_font_10, 0);
  lv_label_set_text(uiControllerInstance.zbActionLabel, Localization_Text(catalog,LOC_PAIRING_START));

  // Right Back Button
  uiControllerInstance.zbBackBtn = lv_obj_create(zbFooter);
  if (!uiControllerInstance.zbBackBtn) return false;
  lv_obj_set_size(uiControllerInstance.zbBackBtn, 62, 16);
  lv_obj_align(uiControllerInstance.zbBackBtn, LV_ALIGN_TOP_RIGHT, -1, 0);
  lv_obj_set_style_radius(uiControllerInstance.zbBackBtn, 0, 0);
  lv_obj_set_style_pad_all(uiControllerInstance.zbBackBtn, 0, 0);
  lv_obj_set_style_border_width(uiControllerInstance.zbBackBtn, 0, 0);
  uiControllerInstance.zbBackLabel = lv_label_create(uiControllerInstance.zbBackBtn);
  if (!uiControllerInstance.zbBackLabel) return false;
  lv_obj_align(uiControllerInstance.zbBackLabel, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_text_font(uiControllerInstance.zbBackLabel, &racool_font_10, 0);
  lv_label_set_text(uiControllerInstance.zbBackLabel, Localization_Text(catalog,LOC_MENU_BACK));
  lv_obj_set_hidden(uiControllerInstance.zbCountdownLabel,true);
  lv_label_set_long_mode(uiControllerInstance.zbInfoLabel,LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
  lv_label_set_long_mode(uiControllerInstance.zbCountdownLabel,LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
  lv_obj_t *textBands[]={uiControllerInstance.zbBreadcrumb,uiControllerInstance.zbStatusLabel,
                        uiControllerInstance.zbInfoLabel,uiControllerInstance.zbCountdownLabel};
  for (unsigned i=0;i<sizeof(textBands)/sizeof(textBands[0]);++i) {
    lv_obj_set_style_text_align(textBands[i],LV_TEXT_ALIGN_CENTER,0);
    lv_label_set_long_mode(textBands[i],LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
  }
  return true;
}

void ScreenZigbeePair_Update(uint32_t nowMs, bool isZigbeePairing,
                             bool isZigbeeConnected, bool zbActionSelected, const LocalizationCatalog *catalog) {
  if (lv_screen_active() != uiControllerInstance.zigbeePairScreen)
    lv_screen_load(uiControllerInstance.zigbeePairScreen);

  if (isZigbeePairing) {
    if (s_pairStartTimeMs == 0) {
      s_pairStartTimeMs = nowMs;
      s_pairTimedOut    = false;
    }
    uint32_t elapsed    = (nowMs - s_pairStartTimeMs) / 1000;
    uint32_t timeout    = 180;
    uint32_t remainSec  = (elapsed < timeout) ? (timeout - elapsed) : 0;

    // Animated sweep
    static const char *const waves[]={".","..","...",">>>",">>",">"};
    char statusBuf[128];
    snprintf(statusBuf,sizeof(statusBuf),"%s %s",Localization_Text(catalog,LOC_PAIRING_SEARCHING),waves[(nowMs/250)%6]);
    lv_label_set_text(uiControllerInstance.zbStatusLabel, statusBuf);
    lv_label_set_text(uiControllerInstance.zbInfoLabel, Localization_Text(catalog,LOC_PAIRING_CHANNEL_SCAN));

    char countBuf[128];
    snprintf(countBuf, sizeof(countBuf), "%s %lu:%02lu",
             Localization_Text(catalog,LOC_PAIRING_SEARCHING),
             (unsigned long)(remainSec / 60), (unsigned long)(remainSec % 60));
    lv_label_set_text(uiControllerInstance.zbCountdownLabel, countBuf);

    lv_label_set_text(uiControllerInstance.zbActionLabel, Localization_Text(catalog,LOC_PAIRING_CANCEL));

    if (remainSec == 0 && !s_pairTimedOut) {
      s_pairTimedOut = true;
      /* UIController owns checked timeout cancellation and retry. Rendering
       * while holding the OLED lock must never issue a second radio command. */
    }
  } else if (isZigbeeConnected) {
    s_pairStartTimeMs = 0;
    s_pairTimedOut    = false;
    lv_label_set_text(uiControllerInstance.zbStatusLabel,    Localization_Text(catalog,LOC_PAIRING_CONNECTED));
    lv_label_set_text(uiControllerInstance.zbInfoLabel,      Localization_Text(catalog,LOC_PAIRING_NETWORK_CONNECTED));
    lv_label_set_text(uiControllerInstance.zbCountdownLabel, Localization_Text(catalog,LOC_PAIRING_REPAIR_PROMPT));
    lv_label_set_text(uiControllerInstance.zbActionLabel, Localization_Text(catalog,LOC_PAIRING_REPAIR));
  } else if (s_pairTimedOut) {
    lv_label_set_text(uiControllerInstance.zbStatusLabel,    Localization_Text(catalog,LOC_PAIRING_TIMEOUT));
    lv_label_set_text(uiControllerInstance.zbInfoLabel,      Localization_Text(catalog,LOC_PAIRING_OPEN_NETWORK));
    lv_label_set_text(uiControllerInstance.zbCountdownLabel, Localization_Text(catalog,LOC_PAIRING_RETRY_PROMPT));
    lv_label_set_text(uiControllerInstance.zbActionLabel, Localization_Text(catalog,LOC_PAIRING_RETRY));
  } else {
    s_pairStartTimeMs = 0;
    lv_label_set_text(uiControllerInstance.zbStatusLabel,    Localization_Text(catalog,LOC_PAIRING_READY));
    lv_label_set_text(uiControllerInstance.zbInfoLabel,      Localization_Text(catalog,LOC_PAIRING_CHANNEL_AUTO));
    lv_label_set_text(uiControllerInstance.zbCountdownLabel, Localization_Text(catalog,LOC_PAIRING_OPEN_NETWORK));
    lv_label_set_text(uiControllerInstance.zbActionLabel, Localization_Text(catalog,LOC_PAIRING_START));
  }

  /* Info and countdown share one 16-pixel row; never clip five text rows
   * into a 64-pixel panel. A pairing countdown replaces the channel hint. */
  lv_label_set_text(uiControllerInstance.zbBreadcrumb,Localization_Text(catalog,LOC_PAIRING_BREADCRUMB));
  lv_label_set_text(uiControllerInstance.zbBackLabel,Localization_Text(catalog,LOC_MENU_BACK));
  lv_obj_set_hidden(uiControllerInstance.zbInfoLabel,isZigbeePairing);
  lv_obj_set_hidden(uiControllerInstance.zbCountdownLabel,!isZigbeePairing);

  // Button layout
  if (isZigbeePairing) {
    lv_obj_set_size(uiControllerInstance.zbActionBtn, 126, 16);
    lv_obj_align(uiControllerInstance.zbActionBtn, LV_ALIGN_TOP_LEFT, 1, 0);
    lv_obj_set_style_bg_color(uiControllerInstance.zbActionBtn, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_color(uiControllerInstance.zbActionLabel, lv_color_hex(0x000000), 0);
    lv_obj_set_hidden(uiControllerInstance.zbBackBtn, true);
  } else {
    lv_obj_set_size(uiControllerInstance.zbActionBtn, 62, 16);
    lv_obj_align(uiControllerInstance.zbActionBtn, LV_ALIGN_TOP_LEFT, 1, 0);
    lv_obj_set_hidden(uiControllerInstance.zbBackBtn, false);

    bool backSelected = zbActionSelected;
    lv_obj_set_style_bg_color(uiControllerInstance.zbActionBtn,
        backSelected ? lv_color_hex(0x000000) : lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_color(uiControllerInstance.zbActionLabel,
        backSelected ? lv_color_hex(0xFFFFFF) : lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_color(uiControllerInstance.zbBackBtn,
        backSelected ? lv_color_hex(0xFFFFFF) : lv_color_hex(0x000000), 0);
    lv_obj_set_style_text_color(uiControllerInstance.zbBackLabel,
        backSelected ? lv_color_hex(0x000000) : lv_color_hex(0xFFFFFF), 0);
  }
}
