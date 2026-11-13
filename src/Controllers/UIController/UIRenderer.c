#include "Controllers/UIController/UIRenderer.h"
#include "Controllers/MenuManager/MenuEditSession.h"
#include "Controllers/UIController/BootAnimation.h"
#include "Controllers/UIController/Screens/ScreenMonitor.h"
#include "Controllers/UIController/Screens/ScreenMenu.h"
#include "Controllers/UIController/Screens/ScreenSubmenu.h"
#include "Controllers/UIController/Screens/ScreenZigbeePair.h"
#include "Controllers/UIController/Screens/ScreenBoot.h"
#include <math.h>
#include <string.h>

static bool renderValid;
void UIRenderer_Invalidate(void) { renderValid=false; }

static uint8_t UIRenderer_ClassifySignal(int8_t rssi, uint8_t previousBars) {
  /* Keep the current level near each boundary; downgrade only after an extra 3 dB. */
  if (previousBars >= 3)
    return rssi < UI_SIGNAL_STRONG_RSSI_DBM - UI_SIGNAL_HYSTERESIS_DBM ? 2 : 3;
  if (previousBars == 2) {
    if (rssi >= UI_SIGNAL_STRONG_RSSI_DBM) return 3;
    return rssi < UI_SIGNAL_MEDIUM_RSSI_DBM - UI_SIGNAL_HYSTERESIS_DBM ? 1 : 2;
  }
  return rssi >= UI_SIGNAL_STRONG_RSSI_DBM ? 3 :
         rssi >= UI_SIGNAL_MEDIUM_RSSI_DBM ? 2 : 1;
}

static bool TemperatureChanged(float current,float previous,float hysteresis) {
  if(isfinite(current)!=isfinite(previous)) return true;
  return isfinite(current) && fabsf(current-previous)>hysteresis;
}

/* Caller owns the OLED lock. No newly created screen is active on failure. */
static void UIRenderer_DiscardPartialUI(void) {
  renderValid = false;
  lv_obj_t *roots[] = {uiControllerInstance.mainScreen, uiControllerInstance.menuScreen,
    uiControllerInstance.submenuScreen, uiControllerInstance.zigbeePairScreen,
    uiControllerInstance.bootScreen};
  for (unsigned i = 0; i < sizeof(roots) / sizeof(roots[0]); ++i)
    if (roots[i]) lv_obj_delete(roots[i]);
  if (uiControllerInstance.inputGroup) lv_group_delete(uiControllerInstance.inputGroup);
  uiControllerInstance.mainScreen = NULL;
  uiControllerInstance.menuScreen = NULL;
  uiControllerInstance.submenuScreen = NULL;
  uiControllerInstance.tempLabel = NULL;
  uiControllerInstance.statusLabel = NULL;
  uiControllerInstance.ambientLabel = NULL;
  uiControllerInstance.cpuLabel = NULL;
  uiControllerInstance.zigbeeIcon = NULL;
  uiControllerInstance.zigbeeIconClip = NULL;
  uiControllerInstance.fanIcon = NULL;
  uiControllerInstance.relayIcon = NULL;
  uiControllerInstance.menuList = NULL;
  uiControllerInstance.menuSidebar = NULL;
  for (unsigned i = 0; i < 4; ++i) uiControllerInstance.menuIcons[i] = NULL;
  uiControllerInstance.menuHeader = NULL;
  uiControllerInstance.menuFooter = NULL;
  uiControllerInstance.submenuBreadcrumb = NULL;
  uiControllerInstance.submenuValueLabel = NULL;
  uiControllerInstance.submenuUnitLabel = NULL;
  uiControllerInstance.submenuBar = NULL;
  uiControllerInstance.submenuFooter = NULL;
  for (unsigned i=0;i<2;++i) uiControllerInstance.unitChoiceLabels[i]=NULL;
  for (unsigned i=0;i<2;++i) uiControllerInstance.unitChoiceSymbols[i]=NULL;
  uiControllerInstance.zigbeePairScreen = NULL;
  uiControllerInstance.zbBreadcrumb = NULL;
  uiControllerInstance.zbStatusLabel = NULL;
  uiControllerInstance.zbInfoLabel = NULL;
  uiControllerInstance.zbCountdownLabel = NULL;
  uiControllerInstance.zbActionBtn = NULL;
  uiControllerInstance.zbActionLabel = NULL;
  uiControllerInstance.zbBackBtn = NULL;
  uiControllerInstance.zbBackLabel = NULL;
  uiControllerInstance.bootScreen = NULL;
  uiControllerInstance.bootLogoLabel = NULL;
  uiControllerInstance.bootTaglineLabel = NULL;
  uiControllerInstance.bootSubLabel = NULL;
  uiControllerInstance.bootProgressBar = NULL;
  uiControllerInstance.bootStatusLabel = NULL;
  uiControllerInstance.bootVersionLabel = NULL;
  uiControllerInstance.bootTopLine = NULL;
  uiControllerInstance.bootBottomLine = NULL;
  uiControllerInstance.bootDissolveOverlay = NULL;
  uiControllerInstance.inputGroup = NULL;
}

/** Build all screens before publishing the input group or starting animations. */
bool UIRenderer_CreateUI(void) {
  IOLED *oled = uiControllerInstance.oled;
  if (!oled || !oled->Lock || !oled->Unlock || !oled->base.GetState ||
      oled->base.GetState(&oled->base) != SERVICE_STATE_STARTED) return false;
  if (!oled->Lock(oled)) return false;
  bool ready = ScreenMonitor_Create() && ScreenMenu_Create() && ScreenSubmenu_Create() &&
               ScreenZigbeePair_Create() && ScreenBoot_Create();
  if (ready) {
    uiControllerInstance.inputGroup = lv_group_create();
    ready = uiControllerInstance.inputGroup != NULL;
  }
  if (!ready) {
    UIRenderer_DiscardPartialUI();
    oled->Unlock(oled);
    return false;
  }
  lv_group_set_default(uiControllerInstance.inputGroup);
  lv_screen_load(uiControllerInstance.bootScreen);
  BootAnimation_Start();
  oled->Unlock(oled);
  return true;
}

/**
 * @brief Route per-frame update to the correct Screen module.
 *
 * Called every frame from UIController_Task. Boot animation is handled
 * first; all other screens are updated only after it completes.
 */
void UIRenderer_Update(uint32_t nowMs, SystemSettings *settings,
                       float cachedObjectTemp, float cachedAmbientTemp,
                       float cachedCpuTemp) {
  if (!settings || !uiControllerInstance.oled ||
      uiControllerInstance.oled->base.GetState((IService *)uiControllerInstance.oled) !=
          SERVICE_STATE_STARTED) {
    return;
  }
  SystemSettings draft=*settings;
  if(MenuEditSession_Merge(&draft))settings=&draft;

  // Boot animation takes priority
  if (ScreenBoot_IsRunning()) {
    ScreenBoot_Tick(nowMs);
    return;
  }

  static UIState   lastState           = (UIState)-1;
  static const LocalizationCatalog *lastCatalog;
  static SystemSettings lastSettings;
  const LocalizationCatalog *catalog = Localization_Snapshot();
  static bool      lastEditing         = false;
  static esp_err_t lastEditResult;
  static uint32_t  lastActivityMark    = 0;
  static bool      lastRelayOn         = false;
  static ClimateFanMode lastFanMode = CLIMATE_FAN_OBJECT_SENSITIVE;
  static bool      lastZigbeeConnected = false;
  static uint32_t signalReadMs;
  static uint8_t signalBars;
  static uint8_t lastSignalBars;
  static bool      lastZigbeePairing   = false;
  static bool      lastZbActionSel     = false;
  static float     lastDispTemp        = -127.0f;
  static float     lastAmbientTemp     = -127.0f;
  static float     lastCpuTemp         = -127.0f;
  static uint32_t  lastAnimTime        = 0;

  UIState state = uiControllerInstance.menu
                      ? uiControllerInstance.menu->GetCurrentState(uiControllerInstance.menu)
                      : UI_STATE_MONITOR;

  uint32_t frameDelta = nowMs - uiControllerInstance.lastFrameTime;
  uiControllerInstance.lastFrameTime = nowMs;
  if (frameDelta > 100) frameDelta = 16;

  if (state != uiControllerInstance.lastTargetState)
    uiControllerInstance.lastTargetState = state;

  bool    editing      = uiControllerInstance.menu ? uiControllerInstance.menu->IsEditing(uiControllerInstance.menu) : false;
  uint32_t activityMark = uiControllerInstance.menu ? uiControllerInstance.menu->GetLastActivityTime(uiControllerInstance.menu) : 0;
  bool    isRelayOn    = uiControllerInstance.relay ? uiControllerInstance.relay->IsOpen(uiControllerInstance.relay) : false;
  ClimateFanMode fanMode = ScreenMonitor_GetFanMode(&settings->control);
  bool    isZbConn     = uiControllerInstance.zigbee ? uiControllerInstance.zigbee->IsConnected(uiControllerInstance.zigbee) : false;
  bool    isZbPairing  = uiControllerInstance.zigbee && uiControllerInstance.zigbee->IsPairing ? uiControllerInstance.zigbee->IsPairing(uiControllerInstance.zigbee) : false;

  if (!isZbConn) signalBars = 0;
  else if (!lastZigbeeConnected || (uint32_t)(nowMs - signalReadMs) >= UI_SIGNAL_REFRESH_MS) {
    signalReadMs = nowMs;
    ZigbeeNetworkInfo info = {0};
    if (uiControllerInstance.zigbee->GetNetworkInfo &&
        uiControllerInstance.zigbee->GetNetworkInfo(uiControllerInstance.zigbee, &info) == 0 &&
        info.coordinator_link_valid)
      signalBars = UIRenderer_ClassifySignal(info.rssi, signalBars);
    else if (signalBars == 0)
      signalBars = 1; /* Connected, but unknown quality must not imply full signal. */
  }
  /* ConfigurationManager publishes byte-stable owned snapshots. This comparison
   * only invalidates drawing; it is not semantic equality or a wake request. */
  esp_err_t editResult=MenuEditSession_Result(state);
  bool needsRefresh = editResult!=lastEditResult || uiControllerInstance.wakeNavigationPending || !renderValid || catalog != lastCatalog ||
      memcmp(settings, &lastSettings, sizeof(lastSettings)) != 0 ||
      (state != lastState) || (editing != lastEditing) ||
      (activityMark != lastActivityMark) || (isRelayOn != lastRelayOn) ||
      (signalBars != lastSignalBars) || (fanMode != lastFanMode) || (isZbConn != lastZigbeeConnected) ||
      (isZbPairing != lastZigbeePairing) ||
      (uiControllerInstance.zbActionSelected != lastZbActionSel) ||
      (state == UI_STATE_ZIGBEE_PAIR && (isZbPairing || (nowMs - lastAnimTime >= 100))) ||
      TemperatureChanged(cachedObjectTemp,lastDispTemp,uiControllerInstance.config.tempRefreshHysteresis) ||
      TemperatureChanged(cachedAmbientTemp,lastAmbientTemp,uiControllerInstance.config.tempRefreshHysteresis) ||
      TemperatureChanged(cachedCpuTemp,lastCpuTemp,uiControllerInstance.config.tempRefreshHysteresis);

  if (!needsRefresh) return;

  bool drawn = true;

  if (!uiControllerInstance.oled->Lock(uiControllerInstance.oled)) {
    renderValid = false;
    return;
  }

  if (state == UI_STATE_MONITOR) {
    ScreenMonitor_Update(nowMs, settings, cachedObjectTemp, cachedAmbientTemp,
                         cachedCpuTemp, isRelayOn, fanMode, isZbConn, signalBars, catalog);
  } else if (editing && state == UI_STATE_ZIGBEE_PAIR) {
    ScreenZigbeePair_Update(nowMs, isZbPairing, isZbConn,
                            uiControllerInstance.zbActionSelected, catalog);
  } else if (editing) {
    ScreenSubmenu_Update(settings, catalog);
  } else {
    drawn = ScreenMenu_Update(settings,cachedObjectTemp,isZbConn,catalog);
  }

  if (drawn && uiControllerInstance.wakeNavigationPending && state == UI_STATE_MONITOR && !editing) {
    IOLED *oled = uiControllerInstance.oled;
    esp_err_t result = oled->PresentAndWake ? oled->PresentAndWake(oled) : ESP_ERR_INVALID_STATE;
    if (result == ESP_OK) {
      uiControllerInstance.isScreenOn = true;
      uiControllerInstance.wakeNavigationPending = false;
    } else drawn = false;
  }
  if (uiControllerInstance.oled)
    uiControllerInstance.oled->Unlock(uiControllerInstance.oled);
  renderValid = drawn;
  if (!drawn) return;
  if (nowMs - lastAnimTime >= 100) lastAnimTime = nowMs;
  lastState           = state;
  lastCatalog = catalog;
  memcpy(&lastSettings, settings, sizeof(lastSettings));
  lastEditing         = editing;
  lastEditResult       = editResult;
  lastActivityMark    = activityMark;
  lastDispTemp        = cachedObjectTemp;
  lastAmbientTemp = cachedAmbientTemp;
  lastCpuTemp = cachedCpuTemp;
  lastRelayOn         = isRelayOn;
  lastFanMode         = fanMode;
  lastZigbeeConnected = isZbConn;
  lastSignalBars = signalBars;
  lastZigbeePairing   = isZbPairing;
  lastZbActionSel     = uiControllerInstance.zbActionSelected;


}
