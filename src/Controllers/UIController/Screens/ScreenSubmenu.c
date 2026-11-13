#include "Controllers/MenuManager/MenuRangeEditor.h"
#include "Controllers/MenuManager/MenuEditSession.h"
#include "Hardware/RGBLED/RGBZoneSelection.h"
#include <math.h>
#include "Controllers/UIController/Screens/ScreenSubmenu.h"
#include "Controllers/UIController/UIControllerInternal.h"
#include "Controllers/UIController/UIWidgets.h"
#include "Controllers/UIController/UITranslation.h"
#include "Hardware/OLED/OLEDFonts.h"
#include <stdio.h>

LV_FONT_DECLARE(lv_font_montserrat_10);
LV_FONT_DECLARE(lv_font_montserrat_20);

static bool UsesSeparatedNumericLayout(UIState state) {
  switch (state) {
  case UI_STATE_CALIBRATE_OBJECT:
  case UI_STATE_CALIBRATE_AMBIENT:
  case UI_STATE_SET_HIGH_TEMP:
  case UI_STATE_SET_LOW_TEMP:
  case UI_STATE_SET_RGB_BRIGHTNESS:
  case UI_STATE_SET_RGB_TEMP_COLD:
  case UI_STATE_SET_RGB_TEMP_COLD_MAX:
  case UI_STATE_SET_RGB_TEMP_WARM:
  case UI_STATE_SET_RGB_TEMP_WARM_MAX:
  case UI_STATE_SET_RGB_TEMP_HOT:
  case UI_STATE_SET_RGB_TEMP_HOT_MAX:
  case UI_STATE_SET_RGB_TEMP_DANGER:
  case UI_STATE_SET_RGB_TEMP_FREEZE:
  case UI_STATE_SET_BUILTIN_BRIGHTNESS:
  case UI_STATE_SET_TIMEOUT:
  case UI_STATE_SET_TELEMETRY:
  case UI_STATE_ZB_REP_FAN:
  case UI_STATE_ZB_REP_AMBIENT:
  case UI_STATE_ZB_REP_CPU:
  case UI_STATE_ZB_REP_HEALTH:
  case UI_STATE_ZB_REP_MEMORY:
  case UI_STATE_ZB_REP_UPTIME:
  case UI_STATE_RGB_COLD_X:
  case UI_STATE_RGB_COLD_Y:
  case UI_STATE_RGB_WARM_X:
  case UI_STATE_RGB_WARM_Y:
  case UI_STATE_RGB_HOT_X:
  case UI_STATE_RGB_HOT_Y:
  case UI_STATE_RGB_DANGER_X:
  case UI_STATE_RGB_DANGER_Y:
  case UI_STATE_RGB_FREEZE_X:
  case UI_STATE_RGB_FREEZE_Y:
  case UI_STATE_FAN_DELTA_ON:
  case UI_STATE_FAN_DELTA_OFF:
    return true;
  default:
    return false;
  }
}

static bool UsesDirectionalChoiceLayout(UIState state) {
  switch (state) {
  case UI_STATE_SET_BUILTIN_MODE:
  case UI_STATE_SET_DISPLAY_MODE:
  case UI_STATE_SET_MENU_AUTO_EXIT:
  case UI_STATE_SET_TEMP_UNIT:
  case UI_STATE_FAN_MODE:
  case UI_STATE_SET_LED_MODE:
  case UI_STATE_SET_LED_ANIM:
  case UI_STATE_SET_BUILTIN_IDLE:
  case UI_STATE_SET_BUILTIN_CONNECTING:
  case UI_STATE_SET_BUILTIN_FAULT:
  case UI_STATE_SET_BUILTIN_STARTUP:
  case UI_STATE_SET_RGB_COLD:
  case UI_STATE_SET_RGB_WARM:
  case UI_STATE_SET_RGB_HOT:
  case UI_STATE_SET_RGB_DANGER:
  case UI_STATE_SET_RGB_FREEZE:
  case UI_STATE_SET_ZIGBEE_TX_PROFILE:
  case UI_STATE_SET_POWER_PROFILE:
  case UI_STATE_SET_LANGUAGE:
  case UI_STATE_FACTORY_RESET:
  case UI_STATE_RESTART:
  case UI_STATE_SHUTDOWN:
    return true;
  default:
    return false;
  }
}

bool ScreenSubmenu_Create(void) {
  uiControllerInstance.submenuScreen = lv_obj_create(NULL);
  if (!uiControllerInstance.submenuScreen) return false;
  lv_obj_set_scrollable(uiControllerInstance.submenuScreen, false);
  lv_obj_set_style_bg_color(uiControllerInstance.submenuScreen, lv_color_hex(0x000000), 0);
  lv_obj_set_style_pad_all(uiControllerInstance.submenuScreen, 0, 0);
  lv_obj_set_style_border_width(uiControllerInstance.submenuScreen, 0, 0);
  lv_obj_set_style_outline_width(uiControllerInstance.submenuScreen, 0, 0);

  // Breadcrumb (top bar)
  uiControllerInstance.submenuBreadcrumb = lv_label_create(uiControllerInstance.submenuScreen);
  if (!uiControllerInstance.submenuBreadcrumb) return false;
  // The 16 px font needs its full line height inside the 1 px border.
  lv_obj_set_size(uiControllerInstance.submenuBreadcrumb, 128, 18);
  lv_obj_set_style_pad_all(uiControllerInstance.submenuBreadcrumb, 0, 0);
  lv_obj_align(uiControllerInstance.submenuBreadcrumb, LV_ALIGN_TOP_LEFT, 0, 0);
  lv_obj_set_style_text_font(uiControllerInstance.submenuBreadcrumb, &racool_font_10, 0);
  lv_obj_set_style_border_side(uiControllerInstance.submenuBreadcrumb, LV_BORDER_SIDE_BOTTOM, 0);
  lv_obj_set_style_border_width(uiControllerInstance.submenuBreadcrumb, 1, 0);
  lv_obj_set_style_border_color(uiControllerInstance.submenuBreadcrumb, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_color(uiControllerInstance.submenuBreadcrumb, lv_color_hex(0xFFFFFF), 0);
  lv_label_set_text(uiControllerInstance.submenuBreadcrumb, Localization_Text(Localization_Snapshot(),LOC_EDITOR_SETTING));
  lv_label_set_long_mode(uiControllerInstance.submenuBreadcrumb,LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);

  // Value Label (center)
  uiControllerInstance.submenuValueLabel = lv_label_create(uiControllerInstance.submenuScreen);
  if (!uiControllerInstance.submenuValueLabel) return false;
  lv_obj_align(uiControllerInstance.submenuValueLabel, LV_ALIGN_CENTER, -10, -3);
  lv_obj_set_style_text_font(uiControllerInstance.submenuValueLabel, &racool_font_14, 0);
  lv_obj_set_style_text_color(uiControllerInstance.submenuValueLabel, lv_color_hex(0xFFFFFF), 0);
  lv_label_set_text(uiControllerInstance.submenuValueLabel, "--.-");

  // Unit Label
  uiControllerInstance.submenuUnitLabel = lv_label_create(uiControllerInstance.submenuScreen);
  if (!uiControllerInstance.submenuUnitLabel) return false;
  lv_obj_align_to(uiControllerInstance.submenuUnitLabel,
                  uiControllerInstance.submenuValueLabel,
                  LV_ALIGN_OUT_RIGHT_BOTTOM, 2, -2);
  lv_obj_set_style_text_font(uiControllerInstance.submenuUnitLabel, &racool_font_10, 0);
  lv_obj_set_style_text_color(uiControllerInstance.submenuUnitLabel, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_border_width(uiControllerInstance.submenuUnitLabel, 0, 0);
  lv_obj_set_style_border_color(uiControllerInstance.submenuUnitLabel, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_pad_hor(uiControllerInstance.submenuUnitLabel, 2, 0);
  lv_obj_set_style_radius(uiControllerInstance.submenuUnitLabel, 0, 0);
  lv_label_set_text(uiControllerInstance.submenuUnitLabel, "C");

  // Progress Bar
  uiControllerInstance.submenuBar = lv_bar_create(uiControllerInstance.submenuScreen);
  if (!uiControllerInstance.submenuBar) return false;
  lv_obj_set_size(uiControllerInstance.submenuBar, 96, 4);
  lv_obj_align(uiControllerInstance.submenuBar, LV_ALIGN_CENTER, 0, 14);
  lv_obj_set_style_bg_color(uiControllerInstance.submenuBar, lv_color_hex(0x000000), 0);
  lv_obj_set_style_border_width(uiControllerInstance.submenuBar, 1, 0);
  lv_obj_set_style_border_color(uiControllerInstance.submenuBar, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_bg_color(uiControllerInstance.submenuBar, lv_color_hex(0xFFFFFF), LV_PART_INDICATOR);
  lv_obj_set_style_radius(uiControllerInstance.submenuBar, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(uiControllerInstance.submenuBar, 0, LV_PART_INDICATOR);
  lv_bar_set_range(uiControllerInstance.submenuBar, 0, 100);
  lv_bar_set_value(uiControllerInstance.submenuBar, 50, LV_ANIM_OFF);

  // Footer
  uiControllerInstance.submenuFooter = lv_label_create(uiControllerInstance.submenuScreen);
  if (!uiControllerInstance.submenuFooter) return false;
  lv_obj_set_size(uiControllerInstance.submenuFooter, 128, 18);
  lv_obj_set_style_pad_all(uiControllerInstance.submenuFooter, 0, 0);
  lv_obj_align(uiControllerInstance.submenuFooter, LV_ALIGN_TOP_LEFT, 0, 46);
  lv_obj_set_style_text_font(uiControllerInstance.submenuFooter, &racool_font_10, 0);
  lv_label_set_long_mode(uiControllerInstance.submenuFooter,LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
  lv_obj_set_style_border_side(uiControllerInstance.submenuFooter, LV_BORDER_SIDE_TOP, 0);
  lv_obj_set_style_border_width(uiControllerInstance.submenuFooter, 1, 0);
  lv_obj_set_style_border_color(uiControllerInstance.submenuFooter, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_color(uiControllerInstance.submenuFooter, lv_color_hex(0xFFFFFF), 0);
  lv_label_set_text(uiControllerInstance.submenuFooter, Localization_Text(Localization_Snapshot(),LOC_EDITOR_HELP));
  // Reviewed choice screens share two indicators, separate from numeric controls.
  for (unsigned i=0;i<2;++i) {
    lv_obj_t *row=lv_label_create(uiControllerInstance.submenuScreen);
    if (!row) return false;
    uiControllerInstance.unitChoiceLabels[i]=row;
    lv_obj_set_size(row,128,16);
    lv_obj_align(row,LV_ALIGN_TOP_LEFT,0,16+(int)i*16);
    lv_obj_set_style_pad_all(row,0,0);
    lv_obj_set_style_pad_left(row,4,0);
    lv_obj_set_style_border_width(row,0,0);
    lv_obj_set_style_text_font(row,&racool_font_10,0);
    lv_label_set_long_mode(row,LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_hidden(row,true);
    lv_obj_t *symbol=lv_label_create(uiControllerInstance.submenuScreen);
    if (!symbol) return false;
    uiControllerInstance.unitChoiceSymbols[i]=symbol;
    lv_obj_set_size(symbol,24,16);
    lv_obj_align(symbol,LV_ALIGN_RIGHT_MID,-4,0);
    lv_obj_set_style_pad_all(symbol,0,0);
    lv_obj_set_style_border_width(symbol,0,0);
    lv_obj_set_style_text_font(symbol,&racool_font_10,0);
    lv_obj_set_style_text_align(symbol,LV_TEXT_ALIGN_RIGHT,0);
    lv_label_set_long_mode(symbol,LV_LABEL_LONG_MODE_CLIP);
  }
  return true;
}

void ScreenSubmenu_Update(SystemSettings *settings, const LocalizationCatalog *catalog) {
  if (!settings || !uiControllerInstance.menu) return;

  if (lv_screen_active() != uiControllerInstance.submenuScreen)
    lv_screen_load(uiControllerInstance.submenuScreen);

  UIState state    = uiControllerInstance.menu->GetCurrentState(uiControllerInstance.menu);
  UIState activeCat = uiControllerInstance.menu->GetActiveCategory(uiControllerInstance.menu);

  // Breadcrumb
  char settingLabel[MENU_LABEL_CAPACITY] = "";
  (void)activeCat;
  uiControllerInstance.menu->CopyLabel(uiControllerInstance.menu,state,settingLabel,sizeof(settingLabel));
  lv_label_set_text(uiControllerInstance.submenuBreadcrumb,UITranslation_MenuLabel(state,settingLabel,catalog));
  lv_label_set_text(uiControllerInstance.submenuFooter,Localization_Text(catalog,LOC_EDITOR_HELP));
  if(MenuEditSession_Result(state)!=ESP_OK) {
    lv_obj_set_hidden(uiControllerInstance.submenuValueLabel,false);
    lv_obj_set_size(uiControllerInstance.submenuValueLabel,96,16);
    lv_obj_align(uiControllerInstance.submenuValueLabel,LV_ALIGN_TOP_LEFT,16,24);
    lv_obj_set_style_text_font(uiControllerInstance.submenuValueLabel,&racool_font_10,0);
    lv_obj_set_style_text_align(uiControllerInstance.submenuValueLabel,LV_TEXT_ALIGN_CENTER,0);
    lv_obj_set_hidden(uiControllerInstance.submenuBar,true);
    lv_obj_set_hidden(uiControllerInstance.submenuUnitLabel,true);
    lv_obj_set_hidden(uiControllerInstance.submenuFooter,true);
    for(unsigned i=0;i<2;++i) {
      lv_obj_set_hidden(uiControllerInstance.unitChoiceLabels[i],true);
      lv_obj_set_hidden(uiControllerInstance.unitChoiceSymbols[i],true);
    }
    lv_label_set_text(uiControllerInstance.submenuValueLabel,Localization_TextKey(catalog,"editor.range_failed"));
    return;
  }
  bool rangeEditor=MenuRangeEditor_Zone(state)>=0;
  if (rangeEditor) {
    const bool fahrenheit=settings->system.temp_unit!=0;
    float storedLow,storedHigh; size_t lowOffset,highOffset;
    int16_t low,high,minimum,maximum; unsigned phase=0;
    if (!MenuRangeEditor_Values(state,settings,&storedLow,&storedHigh,&lowOffset,&highOffset) ||
        !MenuRangeEditor_Bounds(state,&minimum,&maximum)) return;
    low=(int16_t)lroundf(storedLow*10); high=(int16_t)lroundf(storedHigh*10);
    if (!uiControllerInstance.menu->IsEditing(uiControllerInstance.menu) || !MenuRangeEditor_Read(state,&low,&high,&phase)) phase=2;
    lv_obj_set_hidden(uiControllerInstance.submenuUnitLabel,true);
    lv_obj_set_hidden(uiControllerInstance.submenuBar,false);
    lv_bar_set_mode(uiControllerInstance.submenuBar,LV_BAR_MODE_RANGE);
    lv_bar_set_range(uiControllerInstance.submenuBar,minimum,maximum);
    lv_bar_set_start_value(uiControllerInstance.submenuBar,minimum,LV_ANIM_OFF);
    lv_bar_set_value(uiControllerInstance.submenuBar,high,LV_ANIM_OFF);
    lv_bar_set_start_value(uiControllerInstance.submenuBar,low,LV_ANIM_OFF);
    lv_obj_set_size(uiControllerInstance.submenuBar,104,3);
    lv_obj_align(uiControllerInstance.submenuBar,LV_ALIGN_TOP_LEFT,12,48);
    lv_obj_set_size(uiControllerInstance.submenuValueLabel,24,22);
    lv_obj_align(uiControllerInstance.submenuValueLabel,LV_ALIGN_TOP_LEFT,52,23);
    lv_obj_set_style_text_font(uiControllerInstance.submenuValueLabel,&racool_font_14,0);
    lv_label_set_long_mode(uiControllerInstance.submenuValueLabel,LV_LABEL_LONG_MODE_CLIP);
    lv_label_set_text(uiControllerInstance.submenuValueLabel,fahrenheit ? "°F" : "°C");
    int positions[]={12+(low-minimum)*99/(maximum-minimum),12+(high-minimum)*99/(maximum-minimum)};
    /* A subpixel logical gap still needs two distinguishable physical handles. */
    if (positions[1]-positions[0]<7) {
      positions[1]=positions[0]+7;
      if (positions[1]>111) { positions[1]=111; positions[0]=104; }
    }
    for (unsigned i=0;i<2;++i) {
      double displayValue=(i ? high : low)/10.0;
      if (fahrenheit) displayValue=displayValue*1.8+(state==UI_STATE_FAN_DELTA_OFF ? 0 : 32);
      char value[16]; snprintf(value,sizeof(value),"%.1f",displayValue);
      lv_obj_t *label=uiControllerInstance.unitChoiceLabels[i];
      lv_obj_set_hidden(label,false); lv_obj_set_size(label,42,22);
      lv_obj_align(label,LV_ALIGN_TOP_LEFT,i ? 76 : 10,23);
      lv_obj_set_style_pad_all(label,0,0);
      lv_obj_set_style_text_font(label,&racool_font_14,0);
      lv_obj_set_style_text_align(label,LV_TEXT_ALIGN_CENTER,0);
      lv_label_set_long_mode(label,LV_LABEL_LONG_MODE_CLIP);
      lv_label_set_text(label,value);
      lv_obj_set_style_bg_opa(label,LV_OPA_TRANSP,0);
      lv_obj_set_style_text_color(label,lv_color_white(),0);
      lv_obj_t *thumb=uiControllerInstance.unitChoiceSymbols[i];
      lv_obj_set_hidden(thumb,false); lv_obj_set_size(thumb,5,9);
      lv_obj_align(thumb,LV_ALIGN_TOP_LEFT,positions[i],45);
      lv_obj_set_style_pad_all(thumb,0,0); lv_label_set_text(thumb,"");
      lv_obj_set_style_border_width(thumb,1,0);
      lv_obj_set_style_border_color(thumb,lv_color_white(),0);
      lv_obj_set_style_bg_color(thumb,phase==i ? lv_color_white() : lv_color_black(),0);
      lv_obj_set_style_bg_opa(thumb,LV_OPA_COVER,0);
    }
    lv_obj_set_hidden(uiControllerInstance.submenuBar,phase==3);
    for (unsigned i=0;i<2;++i) lv_obj_set_hidden(uiControllerInstance.unitChoiceSymbols[i],phase==3);
    lv_obj_set_hidden(uiControllerInstance.submenuFooter,phase!=3);
    lv_obj_set_style_border_width(uiControllerInstance.submenuFooter,0,0);
    lv_label_set_text(uiControllerInstance.submenuFooter,phase==3 ? Localization_TextKey(catalog,"editor.range_failed") : "");
    return;
  }
  lv_bar_set_mode(uiControllerInstance.submenuBar,LV_BAR_MODE_NORMAL);
  lv_bar_set_range(uiControllerInstance.submenuBar,0,100);
  lv_obj_set_size(uiControllerInstance.submenuBar,96,4);
  bool unitChoice=state==UI_STATE_SET_TEMP_UNIT;
  bool centeredChoice=UsesDirectionalChoiceLayout(state);
  bool centeredReadOnly=state==UI_STATE_SYSTEM_STATUS;
  bool centeredValue=centeredChoice || centeredReadOnly;
  bool confirmation=state==UI_STATE_FACTORY_RESET || state==UI_STATE_RESTART || state==UI_STATE_SHUTDOWN;
  lv_obj_set_hidden(uiControllerInstance.submenuFooter,!confirmation && !centeredReadOnly);
  bool reviewedNumeric=UsesSeparatedNumericLayout(state);
  bool centeredChrome=centeredValue || reviewedNumeric;
  for (unsigned i=0;i<2;++i) {
    lv_obj_set_hidden(uiControllerInstance.unitChoiceLabels[i],!centeredChoice);
    lv_obj_set_hidden(uiControllerInstance.unitChoiceSymbols[i],true);
  }
  lv_obj_set_hidden(uiControllerInstance.submenuValueLabel,false);
  lv_obj_set_hidden(uiControllerInstance.submenuBar,centeredValue);
  lv_obj_set_size(uiControllerInstance.submenuBreadcrumb,128,18);
  lv_obj_set_style_border_width(uiControllerInstance.submenuBreadcrumb,1,0);
  lv_obj_set_style_pad_left(uiControllerInstance.submenuBreadcrumb,0,0);
  lv_obj_set_style_text_align(uiControllerInstance.submenuBreadcrumb,centeredChrome ? LV_TEXT_ALIGN_CENTER : LV_TEXT_ALIGN_LEFT,0);
  lv_label_set_long_mode(uiControllerInstance.submenuBreadcrumb,LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
  lv_obj_set_size(uiControllerInstance.submenuFooter,128,18);
  lv_obj_align(uiControllerInstance.submenuFooter,LV_ALIGN_TOP_LEFT,0,46);
  lv_obj_set_style_border_width(uiControllerInstance.submenuFooter,1,0);
  lv_obj_set_style_pad_left(uiControllerInstance.submenuFooter,0,0);
  lv_obj_set_style_text_align(uiControllerInstance.submenuFooter,centeredChrome ? LV_TEXT_ALIGN_CENTER : LV_TEXT_ALIGN_LEFT,0);
  lv_label_set_long_mode(uiControllerInstance.submenuFooter,centeredValue ? LV_LABEL_LONG_MODE_CLIP : LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
  if (centeredValue) {
    lv_obj_set_hidden(uiControllerInstance.submenuUnitLabel,true);
    lv_obj_set_style_text_font(uiControllerInstance.submenuValueLabel,&racool_font_10,0);
    lv_obj_set_size(uiControllerInstance.submenuValueLabel,96,16);
    lv_obj_align(uiControllerInstance.submenuValueLabel,LV_ALIGN_TOP_LEFT,16,24);
    lv_obj_set_style_text_align(uiControllerInstance.submenuValueLabel,LV_TEXT_ALIGN_CENTER,0);
    lv_label_set_long_mode(uiControllerInstance.submenuValueLabel,LV_LABEL_LONG_MODE_CLIP);
    char value[128];
    if (unitChoice) snprintf(value,sizeof(value),"%s %s",Localization_Text(catalog,
        settings->system.temp_unit ? LOC_TEMPERATURE_FAHRENHEIT : LOC_TEMPERATURE_CELSIUS),
        settings->system.temp_unit ? "°F" : "°C");
    else {
      char unusedUnit[16];
      int unusedPercent=0;
      GetSubmenuValueAndUnit(state,settings,uiControllerInstance.menu,value,sizeof(value),
                            unusedUnit,sizeof(unusedUnit),&unusedPercent,catalog);
    }
    lv_label_set_text(uiControllerInstance.submenuValueLabel,value);
    lv_point_t textSize;
    lv_text_get_size(&textSize,value,&racool_font_10,0,0,LV_COORD_MAX,LV_TEXT_FLAG_NONE);
    lv_label_set_long_mode(uiControllerInstance.submenuValueLabel,
        textSize.x>96 ? LV_LABEL_LONG_MODE_SCROLL_CIRCULAR : LV_LABEL_LONG_MODE_CLIP);
    lv_label_set_text(uiControllerInstance.submenuFooter,Localization_Text(catalog,
        centeredReadOnly ? LOC_MENU_BACK : LOC_EDITOR_CONFIRM));
    if (!centeredReadOnly && !confirmation) lv_label_set_text(uiControllerInstance.submenuFooter,"");
    for (unsigned i=0;i<2;++i) {
      lv_obj_t *arrow=uiControllerInstance.unitChoiceLabels[i];
      lv_obj_set_style_text_font(arrow,&racool_font_10,0);
      lv_obj_set_size(arrow,12,16);
      lv_obj_align(arrow,LV_ALIGN_TOP_LEFT,i ? 110 : 6,24);
      lv_obj_set_style_pad_all(arrow,0,0);
      lv_obj_set_style_text_align(arrow,LV_TEXT_ALIGN_CENTER,0);
      lv_label_set_long_mode(arrow,LV_LABEL_LONG_MODE_CLIP);
      lv_label_set_text(arrow,i ? ">" : "<");
      lv_obj_set_style_bg_color(arrow,lv_color_black(),0);
      lv_obj_set_style_bg_opa(arrow,LV_OPA_TRANSP,0);
      lv_obj_set_style_text_color(arrow,lv_color_white(),0);
    }
    return;
  }

  // Value / Unit / Progress
  char valBuf[128], unitBuf[16];
  int percent = 0;
  GetSubmenuValueAndUnit(state, settings, uiControllerInstance.menu,
                         valBuf, sizeof(valBuf), unitBuf, sizeof(unitBuf), &percent, catalog);

  bool numeric=state==UI_STATE_SET_LOW_TEMP || state==UI_STATE_SET_HIGH_TEMP ||
      state==UI_STATE_CALIBRATE_OBJECT || state==UI_STATE_CALIBRATE_AMBIENT ||
      state==UI_STATE_SET_BUILTIN_BRIGHTNESS || state==UI_STATE_SET_RGB_BRIGHTNESS ||
      state==UI_STATE_SET_TIMEOUT || state==UI_STATE_SET_TELEMETRY ||
      (state>=UI_STATE_ZB_REP_FAN && state<=UI_STATE_ZB_REP_UPTIME) ||
      (state>=UI_STATE_SET_RGB_TEMP_COLD && state<=UI_STATE_SET_RGB_TEMP_FREEZE) ||
      (state>=UI_STATE_RGB_COLD_X && state<=UI_STATE_RGB_FREEZE_Y) ||
      state==UI_STATE_FAN_DELTA_ON || state==UI_STATE_FAN_DELTA_OFF;
  lv_obj_set_hidden(uiControllerInstance.submenuBar,!numeric);
  lv_label_set_text(uiControllerInstance.submenuFooter,Localization_Text(catalog,
      confirmation ? LOC_EDITOR_CONFIRM : state==UI_STATE_SYSTEM_STATUS ? LOC_MENU_BACK :
      numeric ? LOC_EDITOR_NUMERIC_HELP : LOC_EDITOR_CHOICE_SAVE));
  if (!confirmation && state!=UI_STATE_SYSTEM_STATUS) lv_label_set_text(uiControllerInstance.submenuFooter,"");
  lv_obj_set_style_text_font(uiControllerInstance.submenuValueLabel,numeric ? &racool_font_14 : &racool_font_10,0);
  lv_obj_set_size(uiControllerInstance.submenuValueLabel,unitBuf[0] ? 88 : 124,numeric ? 22 : 16);
  lv_obj_set_style_text_align(uiControllerInstance.submenuValueLabel,LV_TEXT_ALIGN_CENTER,0);
  lv_label_set_long_mode(uiControllerInstance.submenuValueLabel,LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
  lv_obj_align(uiControllerInstance.submenuValueLabel,LV_ALIGN_CENTER,unitBuf[0] ? -14 : 0,-3);
  lv_obj_set_hidden(uiControllerInstance.submenuUnitLabel,!unitBuf[0]);
  lv_label_set_text(uiControllerInstance.submenuValueLabel, valBuf);
  lv_label_set_text(uiControllerInstance.submenuUnitLabel, unitBuf);
  lv_bar_set_value(uiControllerInstance.submenuBar, percent, LV_ANIM_OFF);
  /* Keep this reviewed numeric track above the footer separator. Other
   * numeric pages retain their layout until their individual design review. */
  if (reviewedNumeric)
    lv_obj_align(uiControllerInstance.submenuBar,LV_ALIGN_TOP_LEFT,16,41);
  else
    lv_obj_align(uiControllerInstance.submenuBar,LV_ALIGN_CENTER,0,14);

  lv_obj_align_to(uiControllerInstance.submenuUnitLabel,
                  uiControllerInstance.submenuValueLabel,
                  LV_ALIGN_OUT_RIGHT_BOTTOM, 2, -2);
}
