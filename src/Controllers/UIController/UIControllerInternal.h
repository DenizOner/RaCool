#ifndef UICONTROLLER_INTERNAL_H
#define UICONTROLLER_INTERNAL_H

#include "Controllers/UIController/IUIController.h"
#include "Controllers/UIController/UIControllerConfig.h"
#include "Controllers/MenuManager/IMenuManager.h"
#include "Management/ConfigurationManager/IConfigurationManager.h"
#include "Hardware/OLED/IOLED.h"
#include "Hardware/RGBLED/IRGBLED.h"
#include "Hardware/BuiltinLED/IBuiltinLED.h"
#include "Controllers/ClimateController/IClimateController.h"
#include "Management/DiagnosticsManager/IDiagnosticsManager.h"
#include "Communication/Zigbee/IZigbee.h"
#include "Management/Watchdog/IWatchdog.h"
#include "Management/Watchdog/WatchdogWorker.h"
#include "Core/EventBus/IEventBus.h"
#include "Hardware/Relay/IRelay.h"
#include "lvgl.h"
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

typedef struct {
  IUIController base;
  _Atomic(ServiceState) state;
  WatchdogWorker worker;
  SemaphoreHandle_t lifecycleMutex;
  IEventBus *ownedBus;
  uint16_t subscriptions;
  TaskHandle_t taskHandle;
  UIControllerConfig config;

  bool isScreenOn;
  bool wakeNavigationPending;
    bool idleNavigationPending;
  bool buttonPressTracked;
  bool buttonStartedAsleep;
  bool factoryResetIssued;
  bool resetKeepsScreenOff;
  QueueHandle_t eventQueue;
  SemaphoreHandle_t mutex;

  IConfigurationManager *configMgr;
  IOLED *oled;
  IRGBLED *rgbLed;
  IBuiltinLED *builtinLed;
  IClimateController *climateCtrl;
  IDiagnosticsManager *diag;
  IZigbee *zigbee;
  IWatchdog *watchdog;
  IMenuManager *menu;
  IRelay *relay;

  // Animation states
  UIState lastTargetState;
  float currentOffsetY;
  uint32_t lastFrameTime;

  // LVGL Objects
  lv_obj_t *mainScreen;
  lv_obj_t *menuScreen;
  lv_obj_t *submenuScreen;
  lv_obj_t *tempLabel;
  lv_obj_t *statusLabel;
  lv_obj_t *ambientLabel;
  lv_obj_t *cpuLabel;
  lv_obj_t *zigbeeIcon;
  lv_obj_t *zigbeeIconClip;
  lv_obj_t *fanIcon;
  lv_obj_t *relayIcon;
  lv_obj_t *menuList;
  lv_obj_t *menuSidebar;
  lv_obj_t *menuIcons[4];
  lv_obj_t *menuHeader;
  lv_obj_t *menuFooter;
  lv_obj_t *submenuBreadcrumb;
  lv_obj_t *submenuValueLabel;
  lv_obj_t *submenuUnitLabel;
  lv_obj_t *submenuBar;
  lv_obj_t *submenuFooter;
    lv_obj_t *unitChoiceLabels[2];
    lv_obj_t *unitChoiceSymbols[2];
  lv_group_t *inputGroup;

  // Zigbee Pairing Screen Objects
  lv_obj_t *zigbeePairScreen;
  lv_obj_t *zbBreadcrumb;
  lv_obj_t *zbStatusLabel;
  lv_obj_t *zbInfoLabel;
  lv_obj_t *zbCountdownLabel;
  lv_obj_t *zbActionBtn;
  lv_obj_t *zbActionLabel;
  lv_obj_t *zbBackBtn;
  lv_obj_t *zbBackLabel;
  bool pairingTimeoutTracked;
  uint32_t pairingTimeoutStartMs;
  bool zbActionSelected; // false = pair/cancel selected, true = back selected

  // Boot Animation Objects
  lv_obj_t *bootScreen;
  lv_obj_t *bootLogoLabel;
  lv_obj_t *bootTaglineLabel;
  lv_obj_t *bootSubLabel;
  lv_obj_t *bootProgressBar;
  lv_obj_t *bootStatusLabel;
  lv_obj_t *bootVersionLabel;
  lv_obj_t *bootTopLine;
  lv_obj_t *bootBottomLine;
  lv_obj_t *bootDissolveOverlay;

  // Temperature Caches
  float cachedObjectTemp;
  float cachedAmbientTemp;
  float cachedCpuTemp;
} UIControllerImpl;

extern UIControllerImpl uiControllerInstance;

int32_t UIController_PostEvent(IUIController *self, const UIEvent *event);

#endif // UICONTROLLER_INTERNAL_H
