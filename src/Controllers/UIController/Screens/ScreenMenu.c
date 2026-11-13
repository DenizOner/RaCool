#include "Controllers/UIController/Screens/ScreenMenu.h"
#include "Controllers/UIController/UIControllerInternal.h"
#include "Controllers/UIController/UIWidgets.h"
#include "Controllers/MenuManager/MenuNavigator.h"
#include "Controllers/UIController/UITranslation.h"
#include "Hardware/OLED/OLEDFonts.h"
#include <stdio.h>

LV_FONT_DECLARE(lv_font_montserrat_10);

/* Lists already identify their zone; editor titles retain full context. */
static const char *MenuRowLabel(UIState state,const char *fallback,const LocalizationCatalog *catalog) {
  const char *key=NULL;
  switch(state) {
  case UI_STATE_ZB_REP_FAN: key="menu.report_radiator_short";break;
  case UI_STATE_ZB_REP_AMBIENT: key="menu.report_ambient_short";break;
  case UI_STATE_ZB_REP_CPU: key="menu.report_cpu_short";break;
  case UI_STATE_ZB_REP_HEALTH: key="menu.report_health_short";break;
  case UI_STATE_ZB_REP_MEMORY: key="menu.report_memory_short";break;
  case UI_STATE_ZB_REP_UPTIME: key="menu.report_uptime_short";break;
  case UI_STATE_ZIGBEE_PAIR: key="menu.pairing_short";break;
  case UI_STATE_SET_ZIGBEE_TX_PROFILE: key="menu.transmit_power_short";break;
  case UI_STATE_CALIBRATE_OBJECT: key="menu.calibration_radiator";break;
  case UI_STATE_CALIBRATE_AMBIENT: key="menu.calibration_ambient";break;
  case UI_STATE_SET_RGB_COLD: case UI_STATE_SET_RGB_WARM: case UI_STATE_SET_RGB_HOT:
  case UI_STATE_SET_RGB_FREEZE: case UI_STATE_SET_RGB_DANGER: key="menu.zone_color";break;
  case UI_STATE_SET_RGB_TEMP_COLD: case UI_STATE_SET_RGB_TEMP_WARM: case UI_STATE_SET_RGB_TEMP_HOT:
    key="menu.zone_range";break;
  case UI_STATE_SET_RGB_TEMP_FREEZE: case UI_STATE_SET_RGB_TEMP_DANGER: key="menu.zone_limit";break;
  default:break;
  }
  const char *shortLabel=key ? Localization_TextKey(catalog,key) : NULL;
  return shortLabel ? shortLabel : UITranslation_MenuLabel(state,fallback,catalog);
}

bool ScreenMenu_Create(void) {
  uiControllerInstance.menuScreen = lv_obj_create(NULL);
  if (!uiControllerInstance.menuScreen) return false;
  lv_obj_set_scrollable(uiControllerInstance.menuScreen, false);
  lv_obj_set_style_bg_color(uiControllerInstance.menuScreen, lv_color_hex(0x000000), 0);
  lv_obj_set_style_pad_all(uiControllerInstance.menuScreen, 0, 0);
  lv_obj_set_style_border_width(uiControllerInstance.menuScreen, 0, 0);
  lv_obj_set_style_outline_width(uiControllerInstance.menuScreen, 0, 0);

  // Menu Sidebar (24px wide)
  uiControllerInstance.menuSidebar = lv_obj_create(uiControllerInstance.menuScreen);
  if (!uiControllerInstance.menuSidebar) return false;
  lv_obj_set_scrollable(uiControllerInstance.menuSidebar, false);
  lv_obj_set_size(uiControllerInstance.menuSidebar, 24, 64);
  lv_obj_align(uiControllerInstance.menuSidebar, LV_ALIGN_TOP_LEFT, 0, 0);
  lv_obj_set_style_bg_color(uiControllerInstance.menuSidebar, lv_color_hex(0x000000), 0);
  lv_obj_set_style_border_width(uiControllerInstance.menuSidebar, 0, 0);
  lv_obj_set_style_border_side(uiControllerInstance.menuSidebar, LV_BORDER_SIDE_RIGHT, 0);
  lv_obj_set_style_border_color(uiControllerInstance.menuSidebar, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_border_width(uiControllerInstance.menuSidebar, 1, 0);
  lv_obj_set_style_radius(uiControllerInstance.menuSidebar, 0, 0);
  lv_obj_set_style_pad_all(uiControllerInstance.menuSidebar, 0, 0);

  // Sidebar Category Icons
  for (int i = 0; i < 4; i++) {
    uiControllerInstance.menuIcons[i] = lv_label_create(uiControllerInstance.menuSidebar);
    if (!uiControllerInstance.menuIcons[i]) return false;
    lv_obj_set_size(uiControllerInstance.menuIcons[i], 18, 12);
    lv_obj_align(uiControllerInstance.menuIcons[i], LV_ALIGN_TOP_MID, 0, 2 + i * 16);
    lv_obj_set_style_pad_top(uiControllerInstance.menuIcons[i],1,0);
    lv_obj_set_style_text_font(uiControllerInstance.menuIcons[i], &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_align(uiControllerInstance.menuIcons[i], LV_TEXT_ALIGN_CENTER, 0);
  }
  lv_label_set_text(uiControllerInstance.menuIcons[0], LV_SYMBOL_LOOP);
  lv_label_set_text(uiControllerInstance.menuIcons[1], LV_SYMBOL_IMAGE);
  lv_label_set_text(uiControllerInstance.menuIcons[2], LV_SYMBOL_EDIT);
  lv_label_set_text(uiControllerInstance.menuIcons[3], LV_SYMBOL_SETTINGS);

  // Menu Header
  uiControllerInstance.menuHeader = lv_label_create(uiControllerInstance.menuScreen);
  if (!uiControllerInstance.menuHeader) return false;
  lv_obj_set_size(uiControllerInstance.menuHeader, 104, 16);
  lv_obj_align(uiControllerInstance.menuHeader, LV_ALIGN_TOP_LEFT, 24, 0);
  lv_obj_set_style_text_font(uiControllerInstance.menuHeader, &racool_font_10, 0);
  lv_label_set_long_mode(uiControllerInstance.menuHeader,LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
  lv_obj_set_style_bg_color(uiControllerInstance.menuHeader, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_bg_opa(uiControllerInstance.menuHeader, LV_OPA_COVER, 0);
  lv_obj_set_style_text_color(uiControllerInstance.menuHeader, lv_color_hex(0x000000), 0);
  lv_label_set_text(uiControllerInstance.menuHeader, Localization_Text(Localization_Snapshot(),LOC_MENU_ROOT));

  // Menu List
  uiControllerInstance.menuList = lv_obj_create(uiControllerInstance.menuScreen);
  if (!uiControllerInstance.menuList) return false;
  lv_obj_set_size(uiControllerInstance.menuList, 104, 32);
  lv_obj_align(uiControllerInstance.menuList, LV_ALIGN_TOP_LEFT, 24, 16);
  lv_obj_set_style_bg_color(uiControllerInstance.menuList, lv_color_hex(0x000000), 0);
  lv_obj_set_style_pad_all(uiControllerInstance.menuList, 0, 0);
  lv_obj_set_style_border_width(uiControllerInstance.menuList, 0, 0);
  lv_obj_set_style_outline_width(uiControllerInstance.menuList, 0, 0);
  lv_obj_set_style_radius(uiControllerInstance.menuList, 0, 0);

  // A monochrome position indicator must not inherit the gray desktop theme.
  // Reserve a gutter so the thumb never obscures the selected row or its text.
  lv_obj_set_scroll_dir(uiControllerInstance.menuList, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(uiControllerInstance.menuList, LV_SCROLLBAR_MODE_AUTO);
  lv_obj_set_style_pad_right(uiControllerInstance.menuList, 4, 0);
  lv_obj_set_style_width(uiControllerInstance.menuList, 2, LV_PART_SCROLLBAR);
  lv_obj_set_style_pad_all(uiControllerInstance.menuList, 0, LV_PART_SCROLLBAR);
  lv_obj_set_style_radius(uiControllerInstance.menuList, 0, LV_PART_SCROLLBAR);
  lv_obj_set_style_bg_color(uiControllerInstance.menuList, lv_color_hex(0xFFFFFF), LV_PART_SCROLLBAR);
  lv_obj_set_style_bg_opa(uiControllerInstance.menuList, LV_OPA_COVER, LV_PART_SCROLLBAR);

  lv_obj_set_flex_flow(uiControllerInstance.menuList, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(uiControllerInstance.menuList, 0, 0);

  // Menu Footer
  uiControllerInstance.menuFooter = lv_label_create(uiControllerInstance.menuScreen);
  if (!uiControllerInstance.menuFooter) return false;
  lv_obj_set_size(uiControllerInstance.menuFooter, 104, 16);
  lv_obj_align(uiControllerInstance.menuFooter, LV_ALIGN_TOP_LEFT, 24, 48);
  lv_obj_set_style_text_font(uiControllerInstance.menuFooter, &racool_font_10, 0);
  lv_obj_set_style_text_align(uiControllerInstance.menuFooter, LV_TEXT_ALIGN_RIGHT, 0);
  lv_obj_set_style_bg_color(uiControllerInstance.menuFooter, lv_color_hex(0x000000), 0);
  lv_obj_set_style_text_color(uiControllerInstance.menuFooter, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_border_side(uiControllerInstance.menuFooter, LV_BORDER_SIDE_TOP, 0);
  lv_obj_set_style_border_width(uiControllerInstance.menuFooter, 0, 0);
  lv_obj_set_style_border_color(uiControllerInstance.menuFooter, lv_color_hex(0xFFFFFF), 0);
  lv_label_set_text(uiControllerInstance.menuFooter, "--.- C ");
  return true;
}

bool ScreenMenu_Update(SystemSettings *settings, float cachedObjectTemp, bool zigbeeConnected, const LocalizationCatalog *catalog) {
  if (!settings || !uiControllerInstance.menu || !uiControllerInstance.menuList) return false;

  if (lv_screen_active() != uiControllerInstance.menuScreen)
    lv_screen_load(uiControllerInstance.menuScreen);

  MenuVisibilityContext visibility=MenuNavigator_Context(settings,zigbeeConnected);
  UIState activeCat  = uiControllerInstance.menu->GetActiveCategory(uiControllerInstance.menu);
  UIState currentSel = uiControllerInstance.menu->GetCurrentState(uiControllerInstance.menu);
  UIState sidebarCat=activeCat;
  /* Every nested page keeps the root branch highlighted. Bound traversal so
   * malformed navigation metadata cannot stall the renderer. */
  for (unsigned depth=0; depth<UI_STATE_COUNT; ++depth) {
    UIState parent=MenuNavigator_GetParent(sidebarCat);
    if (parent==UI_STATE_MONITOR || parent==sidebarCat || (unsigned)parent>=UI_STATE_COUNT) break;
    sidebarCat=parent;
  }

  // Category sidebar highlighting
  const UIState kCats[4] = {
    UI_STATE_CAT_TEMP, UI_STATE_CAT_LIGHT, UI_STATE_CAT_DEVICE, UI_STATE_CAT_POWER
  };
  for (int i = 0; i < 4; i++) {
    bool isActive = (sidebarCat == kCats[i]) ||
        (activeCat == UI_STATE_MONITOR && currentSel == kCats[i]);
    lv_obj_set_style_bg_color(uiControllerInstance.menuIcons[i],
        isActive ? lv_color_hex(0xFFFFFF) : lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(uiControllerInstance.menuIcons[i],
        isActive ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_set_style_text_color(uiControllerInstance.menuIcons[i],
        isActive ? lv_color_hex(0x000000) : lv_color_hex(0xFFFFFF), 0);
  }

  bool rootMenu = activeCat == UI_STATE_MONITOR;
  lv_obj_set_hidden(uiControllerInstance.menuHeader, rootMenu);
  lv_obj_set_size(uiControllerInstance.menuList, 104, rootMenu ? 48 : 32);
  lv_obj_align(uiControllerInstance.menuList, LV_ALIGN_TOP_LEFT, 24, rootMenu ? 0 : 16);

  // Header
  char catLabel[MENU_LABEL_CAPACITY] = "";
  uiControllerInstance.menu->CopyLabel(uiControllerInstance.menu,activeCat,catLabel,sizeof(catLabel));
  lv_label_set_text(uiControllerInstance.menuHeader,UITranslation_MenuLabel(activeCat,catLabel,catalog));

  // Footer
  char ftBuf[32];
  char unitChar = (settings->system.temp_unit == 1) ? 'F' : 'C';
  float dispTemp = (settings->system.temp_unit == 1)
                       ? (cachedObjectTemp * 1.8f + 32.0f) : cachedObjectTemp;
  snprintf(ftBuf, sizeof(ftBuf), "%.1f°%c ", dispTemp, unitChar);
  lv_label_set_text(uiControllerInstance.menuFooter, ftBuf);

  // Query the owned object tree; no pointers survive screen destruction.
  int itemCount = 0;
  {
    UIState c = MenuNavigator_FirstVisible(activeCat,&visibility);
    while (c != UI_STATE_COUNT) {
      char label[MENU_LABEL_CAPACITY];
      if (uiControllerInstance.menu->CopyLabel(uiControllerInstance.menu, c, label, sizeof(label))) itemCount++;
      UIState nx = MenuNavigator_NextVisible(c,activeCat,false,&visibility);
      if (nx == c) break;
      c = nx;
    }
  }

  bool rebuildList = lv_obj_get_child_count(uiControllerInstance.menuList) != (uint32_t)itemCount;
  if (rebuildList) {
    lv_obj_clean(uiControllerInstance.menuList);
    lv_obj_scroll_to_y(uiControllerInstance.menuList, 0, LV_ANIM_OFF);
  }

  const lv_state_t kAllStates[] = {
    LV_STATE_DEFAULT, LV_STATE_FOCUSED, LV_STATE_PRESSED,
    LV_STATE_CHECKED, LV_STATE_FOCUSED | LV_STATE_PRESSED,
    LV_STATE_FOCUSED | LV_STATE_CHECKED,
  };

  lv_obj_t *selectedBtn = NULL;
  int btnIdx = 0;
  UIState child = MenuNavigator_FirstVisible(activeCat,&visibility);

  while (child != UI_STATE_COUNT) {
    char label[MENU_LABEL_CAPACITY];
    if (uiControllerInstance.menu->CopyLabel(uiControllerInstance.menu, child, label, sizeof(label))) {
      char displayBuf[256];
      FormatMenuItemLabel(child, MenuRowLabel(child,label,catalog), settings, displayBuf, sizeof(displayBuf), catalog);

      lv_obj_t *btn = NULL;
      if (rebuildList) {
        btn = lv_obj_create(uiControllerInstance.menuList);
        if (!btn) {
          lv_obj_clean(uiControllerInstance.menuList);
          return false;
        }
        lv_obj_set_scrollable(btn, false);
        lv_obj_t *rowLabel = lv_label_create(btn);
        if (!rowLabel) {
          lv_obj_clean(uiControllerInstance.menuList);
          return false;
        }
        lv_obj_set_size(btn, lv_pct(100), 16);
        lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_grow(rowLabel, 1);
        lv_label_set_long_mode(rowLabel, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
        lv_label_set_text(rowLabel, displayBuf);
        lv_obj_set_style_radius(btn, 0, 0);
        lv_obj_set_style_border_width(btn, 0, 0);
        lv_obj_set_style_pad_all(btn, 0, 0);
        lv_obj_set_style_text_font(btn, &racool_font_10, 0);
      } else {
        btn = lv_obj_get_child(uiControllerInstance.menuList, btnIdx);
        if (btn) {
          lv_obj_t *existingLbl = lv_obj_get_child(btn, 0);
          if (existingLbl) lv_label_set_text(existingLbl, displayBuf);
        }
      }
      btnIdx++;

      if (btn) {
        lv_obj_t *lbl = lv_obj_get_child(btn, 0);
        bool selected = (child == currentSel);
        if (lbl) lv_label_set_long_mode(lbl,selected ? LV_LABEL_LONG_MODE_SCROLL_CIRCULAR : LV_LABEL_LONG_MODE_DOTS);
        for (int si = 0; si < 6; si++) {
          lv_obj_set_style_bg_color(btn,  selected ? lv_color_hex(0xFFFFFF) : lv_color_hex(0x000000), kAllStates[si]);
          lv_obj_set_style_bg_opa(btn,    selected ? LV_OPA_COVER : LV_OPA_TRANSP, kAllStates[si]);
          lv_obj_set_style_text_color(btn, selected ? lv_color_hex(0x000000) : lv_color_hex(0xFFFFFF), kAllStates[si]);
          if (lbl) {
            lv_obj_set_style_text_color(lbl, selected ? lv_color_hex(0x000000) : lv_color_hex(0xFFFFFF), kAllStates[si]);
            lv_obj_set_style_bg_color(lbl,   selected ? lv_color_hex(0xFFFFFF) : lv_color_hex(0x000000), kAllStates[si]);
            if (!selected) lv_obj_set_style_bg_opa(lbl, LV_OPA_TRANSP, kAllStates[si]);
          }
        }
        if (selected) selectedBtn = btn;
      }
    }
    UIState next = MenuNavigator_NextVisible(child,activeCat,false,&visibility);
    if (next == child) break;
    child = next;
  }

  lv_obj_update_layout(uiControllerInstance.menuList);
  if (selectedBtn)
    lv_obj_scroll_to_view(selectedBtn, LV_ANIM_OFF);
  else
    lv_obj_scroll_to_y(uiControllerInstance.menuList, 0, LV_ANIM_OFF);
  return true;
}
