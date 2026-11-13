#include "Management/ApplicationManager/IApplicationManager.h"
#include "Management/ApplicationManager/FactoryReset.h"
#include <math.h>
#include "Controllers/UIController/IUIController.h"
#include "Controllers/UIController/UIControllerConfig.h"
#include "Controllers/UIController/UIControllerInternal.h"
#include "Controllers/UIController/UIRenderer.h"
#include "Controllers/UIController/UIWidgets.h"
#include "Controllers/UIController/BootAnimation.h"
#include "Communication/Zigbee/IZigbee.h"
#include "Controllers/ClimateController/IClimateController.h"
#include "Controllers/MenuManager/IMenuManager.h"
#include "Controllers/MenuManager/MenuManagerConfig.h"
#include "Controllers/MenuManager/MenuNavigator.h"
#include "Controllers/MenuManager/MenuStateMachine.h"
#include "Controllers/SystemController/ISystemController.h"
#include "Core/ServiceLocator/ServiceLocator.h"
#include "Core/ServiceLocator/ServiceNames.h"
#include "Core/EventBus/IEventBus.h"
#include "Hardware/RotaryEncoder/RotaryEncoderEvents.h"
#include "Hardware/RotaryEncoder/RotaryEncoderConfig.h"
#include "Hardware/RotaryEncoder/RotaryEncoderTypes.h"
#include "Hardware/BuiltinLED/IBuiltinLED.h"
#include "Hardware/OLED/IOLED.h"
#include "Hardware/RGBLED/IRGBLED.h"
#include "Hardware/RGBLED/RGBLEDColorEngine.h"
#include "Hardware/Relay/IRelay.h"
#include "Management/ConfigurationManager/IConfigurationManager.h"
#include "Management/ConfigurationManager/SystemConfigs.h"
#include "Management/DiagnosticsManager/IDiagnosticsManager.h"
#include "Management/Watchdog/IWatchdog.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "lvgl.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static TickType_t UI_TimeoutTicks(uint32_t milliseconds) {
  TickType_t ticks = pdMS_TO_TICKS(milliseconds);
  return milliseconds && !ticks ? 1 : ticks;
}

static bool UI_ApplyScreenPower(bool on) {
  IOLED *oled = uiControllerInstance.oled;
  if (!oled || !oled->SetPower || oled->SetPower(oled, on) != ESP_OK) return false;
  uiControllerInstance.isScreenOn = on;
  return true;
}

/* The menu worker owns navigation. Keep the panel off until its queued reset
 * has actually cleared editing/history; admission alone is not completion. */
static bool UI_WakeToMonitor(void) {
  if (uiControllerInstance.resetKeepsScreenOff) return false;
  if (uiControllerInstance.isScreenOn) return true;
  IMenuManager *menu = uiControllerInstance.menu;
  if (!menu || !menu->GetCurrentState || !menu->IsEditing) return false;
  if (menu->GetCurrentState(menu) != UI_STATE_MONITOR || menu->IsEditing(menu)) {
    if (!uiControllerInstance.wakeNavigationPending) {
      MenuEvent event = {.type = MENU_EVENT_WAKE_UP};
      if (!menu->PostEvent || menu->PostEvent(menu, &event) != MENU_OK) return false;
      uiControllerInstance.wakeNavigationPending = true;
    }
    return false;
  }
  uiControllerInstance.wakeNavigationPending = true;
  return false; /* Rendering and panel transfer complete outside the UI mutex. */
}

/* Only a terminal failed operation releases the dark-reset latch. A failed
 * status read retains darkness; restart success clears it through new boot. */
static void UI_UpdateResetScreenLatch(void) {
  if (!uiControllerInstance.resetKeepsScreenOff) return;
  IApplicationManager *app = (IApplicationManager *)ServiceLocator_GetService(SVC_NAME_APPLICATION_MANAGER);
  AppManagerStatus status;
  if (app && app->GetStatus && app->GetStatus(app, &status) == ESP_OK &&
      status.factoryResetPhase == FACTORY_RESET_FAILED && !status.factoryResetBusy) {
    uiControllerInstance.resetKeepsScreenOff = false;
  }
}

static const char *TAG = "UIController";

/** Global instance of the UI Controller */
UIControllerImpl uiControllerInstance;

/* Read activity before the clock: this also avoids treating an activity
 * update during queue wait as a future timestamp relative to an old sample. */
static void UI_ApplyInactivityPower(uint32_t timeoutMs, bool allowSleep, bool autoExitEnabled) {
  IMenuManager *menu = uiControllerInstance.menu;
  if (!menu) return;
  if (uiControllerInstance.idleNavigationPending && menu->GetCurrentState &&
      menu->GetCurrentState(menu) == UI_STATE_MONITOR)
    uiControllerInstance.idleNavigationPending = false;
  uint64_t elapsedMs;
  if (menu->GetLastActivityTimeMs) {
    uint64_t activityMs = menu->GetLastActivityTimeMs(menu);
    uint64_t currentMs = (uint64_t)esp_timer_get_time() / 1000ULL;
    elapsedMs = currentMs >= activityMs ? currentMs - activityMs : 0;
  } else if (menu->GetLastActivityTime) {
    /* Legacy injected interfaces support elapsed intervals shorter than 2^32 ms. */
    uint32_t activityMs = menu->GetLastActivityTime(menu);
    uint32_t currentMs = (uint32_t)((uint64_t)esp_timer_get_time() / 1000ULL);
    elapsedMs = (uint32_t)(currentMs - activityMs);
  } else return;
  if (allowSleep && uiControllerInstance.isScreenOn && elapsedMs > timeoutMs) {
    ESP_LOGI(TAG, "[SCREEN-SLEEP] Inactivity %llu ms > timeout %lu ms. Sleeping screen.",
             (unsigned long long)elapsedMs, (unsigned long)timeoutMs);
    (void)UI_ApplyScreenPower(false);
  } else if (autoExitEnabled && !allowSleep && uiControllerInstance.isScreenOn && elapsedMs > timeoutMs &&
             !uiControllerInstance.idleNavigationPending && menu->GetCurrentState &&
             menu->GetCurrentState(menu) != UI_STATE_MONITOR) {
    MenuEvent event = {.type=MENU_EVENT_NAVIGATE_TO, .data.targetState=UI_STATE_MONITOR};
    if (menu->PostEvent && menu->PostEvent(menu, &event) == MENU_OK) {
      uiControllerInstance.idleNavigationPending = true;
      ESP_LOGI(TAG, "[MENU-IDLE] Home navigation requested; screen stays on");
    }
  }
}

/* UI worker owns this deadline under its state mutex. Failed cancellation
 * retries are bounded; unsigned elapsed arithmetic also covers timer wrap. */
static bool UI_UpdatePairingTimeout(uint32_t nowMs) {
  IZigbee *radio = uiControllerInstance.zigbee;
  bool active = radio && radio->IsPairing && radio->IsPairing(radio);
  if (!active) {
    uiControllerInstance.pairingTimeoutTracked = false;
    return false;
  }
  if (!uiControllerInstance.pairingTimeoutTracked) {
    uiControllerInstance.pairingTimeoutStartMs = nowMs;
    uiControllerInstance.pairingTimeoutTracked = true;
  }
  if ((uint32_t)(nowMs - uiControllerInstance.pairingTimeoutStartMs) < UI_PAIRING_TIMEOUT_MS) return true;
  int32_t result = radio->DisconnectFromNetwork ? radio->DisconnectFromNetwork(radio) : ESP_ERR_INVALID_STATE;
  if (result == ESP_OK) {
    uiControllerInstance.pairingTimeoutTracked = false;
    uiControllerInstance.zbActionSelected = false;
    return radio->IsPairing(radio);
  }
  ESP_LOGE(TAG, "Pairing timeout cancellation rejected: %ld", (long)result);
  uiControllerInstance.pairingTimeoutStartMs = nowMs - UI_PAIRING_TIMEOUT_MS + UI_PAIRING_CANCEL_RETRY_MS;
  return true;
}

// EventBus Handlers
#include "Controllers/UIController/EventHandlers/UIEventHandlerTemp.h"
#include "Controllers/UIController/EventHandlers/UIEventHandlerRotary.h"
#include "Controllers/UIController/EventHandlers/UIEventHandlerRelay.h"
#include "Controllers/UIController/EventHandlers/UIEventHandlerConfig.h"
#include "Controllers/UIController/EventHandlers/UIEventHandlerZigbee.h"

// Caller holds the UI state mutex; queue ownership remains in the worker.
static void UIController_HandleEvent(const UIEvent *event) {
        switch (event->type) {
          case UI_EVENT_TEMP_UPDATE:
            if (event->data.temp.objectTemp > -100.0f) {
              uiControllerInstance.cachedObjectTemp = event->data.temp.objectTemp;
            }
            if (event->data.temp.ambientTemp > -100.0f) {
              uiControllerInstance.cachedAmbientTemp = event->data.temp.ambientTemp;
            }
            if (event->data.temp.cpuTemp > -100.0f) {
              uiControllerInstance.cachedCpuTemp = event->data.temp.cpuTemp;
            }
            break;
          case UI_EVENT_ENCODER_MOVE:
            // A sleeping display accepts button gestures only; rotation is inert.
            if (!uiControllerInstance.isScreenOn) break;
            if (uiControllerInstance.menu) {
              static uint32_t s_enc_move_count = 0;
              int32_t diff = event->data.encoder.difference;
              uint32_t ts  = (uint32_t)(esp_timer_get_time() / 1000ULL);
              ESP_LOGI(TAG, "[DIAG-UI] ENCODER_MOVE #%lu diff=%ld t=%lums",
                       (unsigned long)++s_enc_move_count, (long)diff, (unsigned long)ts);
              uiControllerInstance.menu->UpdateActivity(uiControllerInstance.menu);
              {
                UIState curState = uiControllerInstance.menu->GetCurrentState(uiControllerInstance.menu);
                bool isEditing = uiControllerInstance.menu->IsEditing(uiControllerInstance.menu);
                if (curState == UI_STATE_ZIGBEE_PAIR && isEditing) {
                  // Only toggle button selection when not actively pairing
                  bool isPairing = (uiControllerInstance.zigbee && uiControllerInstance.zigbee->IsPairing && uiControllerInstance.zigbee->IsPairing(uiControllerInstance.zigbee));
                  if (!isPairing) {
                    uiControllerInstance.zbActionSelected = !uiControllerInstance.zbActionSelected;
                    ESP_LOGI(TAG, "[PAIRING] Selection toggled -> %s",
                             uiControllerInstance.zbActionSelected ? "[GERI]" : "[BASLAT]");
                  }
                } else {
                  uiControllerInstance.menu->ProcessEncoder(uiControllerInstance.menu, (int8_t)diff);
                }
              }
            }

            break;
          case UI_EVENT_ENCODER_CLICK:
            uiControllerInstance.buttonPressTracked = false;
            if (BootAnimation_IsRunning()) {
              ESP_LOGI(TAG, "Encoder clicked during boot animation -> skipping directly to main screen");
              BootAnimation_Skip();
              break;
            }
            if (uiControllerInstance.menu) {
              if (!uiControllerInstance.isScreenOn) {
                (void)UI_WakeToMonitor();
                uiControllerInstance.menu->UpdateActivity(uiControllerInstance.menu);
              } else {
                uiControllerInstance.menu->UpdateActivity(uiControllerInstance.menu);
                UIState curState = uiControllerInstance.menu->GetCurrentState(uiControllerInstance.menu);
                bool isEditing = uiControllerInstance.menu->IsEditing(uiControllerInstance.menu);

                if (curState == UI_STATE_ZIGBEE_PAIR && isEditing) {
                  if (!uiControllerInstance.zigbee) {
                    uiControllerInstance.zigbee = (IZigbee *)ServiceLocator_GetService(SVC_NAME_ZIGBEE);
                  }

                  bool isPairing = (uiControllerInstance.zigbee && uiControllerInstance.zigbee->IsPairing && uiControllerInstance.zigbee->IsPairing(uiControllerInstance.zigbee));

                  if (!uiControllerInstance.zigbee || !uiControllerInstance.zigbee->IsPairing) {
                    ESP_LOGE(TAG, "Pairing service unavailable");
                    break;
                  }
                  if (!uiControllerInstance.zbActionSelected) {
                    /* ConnectToNetwork owns any SDK reset needed for steering. */
                    int32_t result = isPairing
                        ? (uiControllerInstance.zigbee->DisconnectFromNetwork
                            ? uiControllerInstance.zigbee->DisconnectFromNetwork(uiControllerInstance.zigbee) : ESP_ERR_INVALID_STATE)
                        : (uiControllerInstance.zigbee->ConnectToNetwork
                            ? uiControllerInstance.zigbee->ConnectToNetwork(uiControllerInstance.zigbee) : ESP_ERR_INVALID_STATE);
                    if (result != ESP_OK) ESP_LOGE(TAG, "Pairing action rejected: %ld", (long)result);
                  } else {
                    if (isPairing) {
                      int32_t result = uiControllerInstance.zigbee->DisconnectFromNetwork
                          ? uiControllerInstance.zigbee->DisconnectFromNetwork(uiControllerInstance.zigbee) : ESP_ERR_INVALID_STATE;
                      if (result != ESP_OK) {
                        ESP_LOGE(TAG, "Pairing cancellation rejected: %ld", (long)result);
                        break;
                      }
                    }
                    if (!uiControllerInstance.menu->SetEditing || !uiControllerInstance.menu->SetState) break;
                    uiControllerInstance.zbActionSelected = false;
                    uiControllerInstance.menu->SetEditing(uiControllerInstance.menu, false);
                    uiControllerInstance.menu->SetState(uiControllerInstance.menu, UI_STATE_CAT_DEVICE);
                  }
                } else {
                  uiControllerInstance.menu->ProcessButton(uiControllerInstance.menu);
                }
              }
            }
            break;


          case UI_EVENT_ENCODER_PRESS:
            uiControllerInstance.buttonPressTracked = true;
            uiControllerInstance.buttonStartedAsleep = !uiControllerInstance.isScreenOn;
            uiControllerInstance.factoryResetIssued = false;
            break;
          case UI_EVENT_ENCODER_RELEASE:
            uiControllerInstance.buttonPressTracked = false;
            break;
          case UI_EVENT_ENCODER_HELD:
            if (uiControllerInstance.buttonPressTracked && uiControllerInstance.buttonStartedAsleep &&
                !uiControllerInstance.isScreenOn && !uiControllerInstance.factoryResetIssued &&
                event->data.encoder.difference >= UI_FACTORY_RESET_HOLD_MS) {
              /* One destructive request per physical hold, even if admission fails. */
              uiControllerInstance.factoryResetIssued = true;
              IApplicationManager *app = (IApplicationManager *)ServiceLocator_GetService(SVC_NAME_APPLICATION_MANAGER);
              if (!app || !app->TriggerFactoryReset || app->TriggerFactoryReset(app) != ESP_OK) {
                ESP_LOGE(TAG, "Factory reset request rejected");
              } else {
                uiControllerInstance.resetKeepsScreenOff = true;
                uiControllerInstance.wakeNavigationPending = false;
              }
            }
            break;
          case UI_EVENT_ENCODER_LONG_PRESS:
            if (uiControllerInstance.isScreenOn &&
                (!uiControllerInstance.buttonPressTracked || !uiControllerInstance.buttonStartedAsleep) && !BootAnimation_IsRunning() &&
                event->data.encoder.difference >= RE_LONG_PRESS_MS &&
                uiControllerInstance.menu && uiControllerInstance.menu->PostEvent) {
              MenuEvent shortcut = {.type = MENU_EVENT_BUTTON_LONG_PRESSED};
              if (uiControllerInstance.menu->PostEvent(uiControllerInstance.menu, &shortcut) == MENU_OK) {
                uiControllerInstance.zbActionSelected = false;
                uiControllerInstance.menu->UpdateActivity(uiControllerInstance.menu);
              }
            }
            break;
          case UI_EVENT_RELAY_STATE:
            // Handled dynamically during renderer updates
            break;
          case UI_EVENT_ZIGBEE_STATE:
            // Handled dynamically during renderer updates
            break;
          case UI_EVENT_SCREEN_WAKEUP:
            (void)UI_WakeToMonitor();
            if (uiControllerInstance.menu) {
              uiControllerInstance.menu->UpdateActivity(uiControllerInstance.menu);
            }
            break;
          case UI_EVENT_CONFIG_CHANGED:
            /* The normal worker snapshot/render pass applies settings. A remote
             * write is not local activity and must not wake or reset navigation. */
            UIRenderer_Invalidate();
            break;
          default:
            break;
        }
}

/**
 * @brief FreeRTOS task handling display updates, menu state machine and event dispatching.
 *
 * Runs periodically, waits for events asynchronously from the queue and updates
 * UI widgets via LVGL. Thread-safety is enforced using mutex locks.
 *
 * @param pvParameters Pointer to task parameters (unused).
 */
static void UIController_Task(void *pvParameters) {
  (void)pvParameters;
  ESP_LOGI(TAG, "UI Task Started");

  (void)UI_ApplyScreenPower(true);

  UIEvent event;
  float cachedObjectTemp = uiControllerInstance.cachedObjectTemp;
  float cachedAmbientTemp = uiControllerInstance.cachedAmbientTemp;
  float cachedCpuTemp = uiControllerInstance.cachedCpuTemp;
  bool startupPhaseKnown = false;
  bool startupPhaseRequested = false;

  while (1) {
    WatchdogWorker_Checkpoint(&uiControllerInstance.worker,
        (IWatchdog *)ServiceLocator_GetService(SVC_NAME_WATCHDOG),
        (uint64_t)esp_timer_get_time() / 1000ULL);
    uint32_t nowMs = (uint32_t)(esp_timer_get_time() / 1000ULL);

    // Wait for events asynchronously
    TickType_t waitTicks = UI_TimeoutTicks(uiControllerInstance.config.refreshIntervalMs);
    if (xQueueReceive(uiControllerInstance.eventQueue, &event, waitTicks) == pdTRUE) {
      if (xSemaphoreTake(uiControllerInstance.mutex, UI_TimeoutTicks(uiControllerInstance.config.mutexTimeoutMs)) == pdTRUE) {
        UIController_HandleEvent(&event);
        xSemaphoreGive(uiControllerInstance.mutex);
      }
    }

    /* ------------------------------------------------------------------ *
     * Second mutex block: gather render state, update services, then       *
     * release the mutex BEFORE calling UIRenderer_Update.                  *
     * This breaks the nested-lock pattern (UIController mutex → lvgl_mutex)*
     * that caused deadlocks and starved the watchdog feed.                 *
     * ------------------------------------------------------------------ */
    bool shouldRender = false;
    SystemSettings *renderSettings = NULL;
    SystemSettings snapshot; /* Remains alive through rendering after unlock. */

    if (xSemaphoreTake(uiControllerInstance.mutex, UI_TimeoutTicks(uiControllerInstance.config.mutexTimeoutMs)) == pdTRUE) {
      UI_UpdateResetScreenLatch();
      cachedObjectTemp = uiControllerInstance.cachedObjectTemp;
      cachedAmbientTemp = uiControllerInstance.cachedAmbientTemp;
      cachedCpuTemp = uiControllerInstance.cachedCpuTemp;

      if (uiControllerInstance.configMgr) {
        SystemSettings *settings = uiControllerInstance.configMgr->GetSnapshot &&
            uiControllerInstance.configMgr->GetSnapshot(uiControllerInstance.configMgr, &snapshot) == ESP_OK
            ? &snapshot : NULL;

        // Self-healing resolved services
        if (!uiControllerInstance.climateCtrl) {
          uiControllerInstance.climateCtrl = (IClimateController *)ServiceLocator_GetService(SVC_NAME_CLIMATE_CONTROLLER);
        }
        if (!uiControllerInstance.oled) {
          uiControllerInstance.oled = (IOLED *)ServiceLocator_GetService(SVC_NAME_OLED);
        }
        if (!uiControllerInstance.rgbLed) {
          uiControllerInstance.rgbLed = (IRGBLED *)ServiceLocator_GetService(SVC_NAME_RGB_LED);
        }
        if (!uiControllerInstance.builtinLed) {
          uiControllerInstance.builtinLed = (IBuiltinLED *)ServiceLocator_GetService(SVC_NAME_BUILTIN_LED);
        }
        if (!uiControllerInstance.relay) {
          uiControllerInstance.relay = (IRelay *)ServiceLocator_GetService(SVC_NAME_RELAY);
        }
        if (!uiControllerInstance.zigbee) {
          uiControllerInstance.zigbee = (IZigbee *)ServiceLocator_GetService(SVC_NAME_ZIGBEE);
        }

        // Display modes: automatic sleep or always on.
        uint8_t displayMode = settings ? settings->system.display_mode : 1;

        if (!BootAnimation_IsRunning()) {
            // Automatic inactivity can suspend, but never implicitly wake.
            uint32_t timeoutSec = (settings && settings->system.oled_timeout_sec >= OLED_TIMEOUT_MIN_SEC)
                                      ? settings->system.oled_timeout_sec
                                      : uiControllerInstance.config.screenTimeoutSec;
            if (timeoutSec < OLED_TIMEOUT_MIN_SEC) {
                timeoutSec = OLED_TIMEOUT_MIN_SEC;
            }
            uint32_t timeoutMs    = timeoutSec * 1000;
            bool isPairingActive = UI_UpdatePairingTimeout(nowMs);

            if (isPairingActive && uiControllerInstance.menu && uiControllerInstance.menu->UpdateActivity) {
                uiControllerInstance.menu->UpdateActivity(uiControllerInstance.menu);
            }
            if (!isPairingActive) UI_ApplyInactivityPower(timeoutMs, displayMode != 2,
                settings ? settings->system.menu_auto_exit_enabled != 0 : DEFAULT_MENU_AUTO_EXIT_ENABLED != 0);
        }


        if (displayMode != 0 && uiControllerInstance.wakeNavigationPending) (void)UI_WakeToMonitor();
        if (displayMode == 0) uiControllerInstance.wakeNavigationPending = false;

        // Capture render snapshot to use outside this mutex block
        shouldRender   = uiControllerInstance.isScreenOn;
        renderSettings = settings;

        // Builtin LED Status
        if (uiControllerInstance.builtinLed) {

          BuiltinLEDAnimationType targetAnim = BLINK_WHITE;

          uint8_t currentCond = BUILTIN_LED_COND_IDLE;
          ISystemController *sc = GetSystemController();
          SystemState sysState = sc ? sc->GetSystemState(sc) : SYSTEM_STATE_READY;

          bool isZigbeePairing = uiControllerInstance.zigbee && uiControllerInstance.zigbee->IsPairing
                                     ? uiControllerInstance.zigbee->IsPairing(uiControllerInstance.zigbee)
                                     : false;

          if (isZigbeePairing) {
            // When actively searching/pairing, use distinctive high-priority animation (Fade Blue / Blink White)
            targetAnim = FADE_BLUE;
          } else if (sysState == SYSTEM_STATE_ERROR || sysState == SYSTEM_STATE_RECOVERING || sysState == SYSTEM_STATE_ALARM) {
            currentCond = BUILTIN_LED_COND_FAULT;
          } else if (uiControllerInstance.zigbee && !uiControllerInstance.zigbee->IsConnected(uiControllerInstance.zigbee)) {
            currentCond = BUILTIN_LED_COND_CONNECTING;
          } else {
            currentCond = BUILTIN_LED_COND_IDLE;
          }

          BuiltinLEDAnimationType configuredAnim = BUILTIN_LED_ANIM_NONE;
          bool startupDesired = BootAnimation_IsRunning() && settings &&
              settings->hardware.builtin_led_anim_mode == 1 && !isZigbeePairing &&
              currentCond != BUILTIN_LED_COND_FAULT;
          if (uiControllerInstance.builtinLed->SetStartupActive &&
              (!startupPhaseKnown || startupPhaseRequested != startupDesired) &&
              uiControllerInstance.builtinLed->SetStartupActive(uiControllerInstance.builtinLed,startupDesired) == ESP_OK) {
            startupPhaseKnown = true;
            startupPhaseRequested = startupDesired;
          }
          if (isZigbeePairing && settings && settings->hardware.builtin_led_anim_mode == 1) {
            configuredAnim = FADE_BLUE;
          } else if (settings && settings->hardware.builtin_led_anim_mode == 1) {
            switch (currentCond) {
              case BUILTIN_LED_COND_FAULT:
                configuredAnim = (BuiltinLEDAnimationType)settings->hardware.builtin_led_fault_anim;
                break;
              case BUILTIN_LED_COND_CONNECTING:
                configuredAnim = (BuiltinLEDAnimationType)settings->hardware.builtin_led_connecting_anim;
                break;
              case BUILTIN_LED_COND_IDLE:
                configuredAnim = (BuiltinLEDAnimationType)settings->hardware.builtin_led_idle_anim;
                break;
              default:
                break;
            }
          }

          targetAnim = configuredAnim >= 0 && configuredAnim < BUILTIN_LED_ANIM_COUNT
                         ? configuredAnim : BUILTIN_LED_ANIM_NONE;

          BuiltinLEDStatus actual;
          if (!startupDesired && uiControllerInstance.builtinLed->GetStatus(uiControllerInstance.builtinLed, &actual) == ESP_OK &&
              actual.current_animation != targetAnim) {
            BuiltinLEDPriority prio = isZigbeePairing ? BUILTIN_LED_PRIO_HIGH : BUILTIN_LED_PRIO_LOW;
            uiControllerInstance.builtinLed->SetAnimation(uiControllerInstance.builtinLed, targetAnim, prio, 0);
          }
        }
      }
      xSemaphoreGive(uiControllerInstance.mutex);
    }

    /* OLED Refresh — called OUTSIDE the UIController mutex so that
     * oled->Lock (lvgl_mutex) does not deadlock against oled_lvgl_task. */
    if (shouldRender || uiControllerInstance.wakeNavigationPending) {
      UIRenderer_Update(nowMs, renderSettings, cachedObjectTemp, cachedAmbientTemp, cachedCpuTemp);
    }
  }
}


/**
 * @brief Post an event to the UIController's event queue.
 *
 * @param self Pointer to the IUIController instance.
 * @param event Pointer to the event payload.
 * @return int32_t 0 on success, or a non-zero error code.
 */
int32_t UIController_PostEvent(IUIController *self, const UIEvent *event) {
  if (self != &uiControllerInstance.base || !event || !uiControllerInstance.eventQueue ||
      !uiControllerInstance.lifecycleMutex || xSemaphoreTake(uiControllerInstance.lifecycleMutex, 0) != pdTRUE) return -1;
  int32_t result = -1;
  if (uiControllerInstance.state != SERVICE_STATE_STOPPED && uiControllerInstance.state != SERVICE_STATE_ERROR)
    result = xQueueSend(uiControllerInstance.eventQueue, event, 0) == pdTRUE ? 0 : -1;
  xSemaphoreGive(uiControllerInstance.lifecycleMutex);
  return result;
}

/**
 * @brief Initialize the UI controller service.
 *
 * @param self Pointer to the base IService structure.
 * @param registry Pointer to the dependency injection registry (unused).
 * @return int32_t 0 on success, or a non-zero error code.
 */
static int32_t UIController_Initialize(IService *self, IDIRegistry *registry) {
  (void)registry;
  if (uiControllerInstance.state >= SERVICE_STATE_INITIALIZED) {
    return 0;
  }

  uiControllerInstance.eventQueue = xQueueCreate(uiControllerInstance.config.eventQueueSize, sizeof(UIEvent));
  uiControllerInstance.mutex = xSemaphoreCreateMutex();
  uiControllerInstance.lifecycleMutex = xSemaphoreCreateMutex();

  if (!uiControllerInstance.eventQueue || !uiControllerInstance.mutex || !uiControllerInstance.lifecycleMutex ||
      WatchdogWorker_Init(&uiControllerInstance.worker) != 0) {
    if (uiControllerInstance.eventQueue) vQueueDelete(uiControllerInstance.eventQueue);
    if (uiControllerInstance.mutex) vSemaphoreDelete(uiControllerInstance.mutex);
    if (uiControllerInstance.lifecycleMutex) vSemaphoreDelete(uiControllerInstance.lifecycleMutex);
    uiControllerInstance.lifecycleMutex = NULL;
    uiControllerInstance.eventQueue = NULL;
    uiControllerInstance.mutex = NULL;
    ESP_LOGE(TAG, "Failed to create UI task control, queue or mutex");
    return -1;
  }

  uiControllerInstance.state = SERVICE_STATE_INITIALIZED;
  uiControllerInstance.isScreenOn = true;
  uiControllerInstance.cachedObjectTemp = NAN;
  uiControllerInstance.cachedAmbientTemp = NAN;
  uiControllerInstance.cachedCpuTemp = NAN;
  return 0;
}


static const char *uiEventNames[] = {EVENT_IR_TEMP_UPDATED, EVENT_INTERNAL_TEMP_UPDATED,
  RE_EVENT_ROTATED, RE_EVENT_BUTTON_CLICK, RE_EVENT_BUTTON_LONGPRESS, RE_EVENT_BUTTON_PRESSED, RE_EVENT_BUTTON_HELD, RE_EVENT_BUTTON_RELEASED, EVENT_RELAY_STATE_CHANGED, EVENT_CONFIG_CHANGED, "MENU_EVENT_ZIGBEE_ACTION"};
_Static_assert(sizeof(uiEventNames) / sizeof(uiEventNames[0]) <= 16, "UI subscription mask capacity");
static BusEventHandler uiEventHandlers[] = {UIController_OnIRTempUpdated, UIController_OnInternalTempUpdated,
  UIController_OnRotaryRotated, UIController_OnRotaryClicked, UIController_OnRotaryLongPressed, UIController_OnRotaryHold, UIController_OnRotaryHold, UIController_OnRotaryHold, UIController_OnRelayStateChanged,
  UIController_OnConfigChanged, UIController_OnZigbeeMenuAction};

static int32_t UI_StopOwned(ServiceState finalState) {
  uiControllerInstance.state = SERVICE_STATE_ERROR;
  if (WatchdogWorker_Stop(&uiControllerInstance.worker,
      pdMS_TO_TICKS(WATCHDOG_WORKER_CONTROL_TIMEOUT_MS)) != 0) return -1;
  uiControllerInstance.taskHandle = NULL;
  for (unsigned i = 0; i < sizeof(uiEventNames) / sizeof(uiEventNames[0]); ++i) {
    if (!(uiControllerInstance.subscriptions & (1U << i))) continue;
    IEventBus *bus = uiControllerInstance.ownedBus;
    if (!bus || !bus->Unsubscribe || bus->Unsubscribe(bus, uiEventNames[i], uiEventHandlers[i]) != 0) return -1;
    uiControllerInstance.subscriptions &= ~(1U << i);
  }
  uiControllerInstance.ownedBus = NULL;
  /* Persistent queue/mutex keep public producer references valid across restart. */
  UIEvent discarded;
  while (xQueueReceive(uiControllerInstance.eventQueue, &discarded, 0) == pdTRUE) {}
  uiControllerInstance.state = finalState;
  return 0;
}

static int32_t UI_StartOwned(IService *self) {
  ServiceState previous = uiControllerInstance.state;
  ESP_LOGI(TAG, "Starting UIController...");
  if (uiControllerInstance.state == SERVICE_STATE_STARTED) {
    return atomic_load(&uiControllerInstance.worker.stopRequested) ||
           !atomic_load(&uiControllerInstance.worker.task) ? -1 : 0;
  }
  if (uiControllerInstance.state != SERVICE_STATE_INITIALIZED &&
      uiControllerInstance.state != SERVICE_STATE_STOPPED) return -1;

  uiControllerInstance.configMgr = ConfigurationManager_GetInstance();
  uiControllerInstance.oled = (IOLED *)ServiceLocator_GetService(SVC_NAME_OLED);
  uiControllerInstance.rgbLed = (IRGBLED *)ServiceLocator_GetService(SVC_NAME_RGB_LED);
  uiControllerInstance.builtinLed = (IBuiltinLED *)ServiceLocator_GetService(SVC_NAME_BUILTIN_LED);
  uiControllerInstance.climateCtrl = (IClimateController *)ServiceLocator_GetService(SVC_NAME_CLIMATE_CONTROLLER);
  uiControllerInstance.diag = (IDiagnosticsManager *)ServiceLocator_GetService(SVC_NAME_DIAGNOSTICS_MANAGER);
  uiControllerInstance.zigbee = (IZigbee *)ServiceLocator_GetService(SVC_NAME_ZIGBEE);
  uiControllerInstance.relay = (IRelay *)ServiceLocator_GetService(SVC_NAME_RELAY);

  uiControllerInstance.menu = MenuManager_GetModule();
  if (uiControllerInstance.menu) {
    IMenuManager *menu = uiControllerInstance.menu;
    if (!menu->base.GetState) return -1;
    ServiceState menuState = menu->base.GetState(&menu->base);
    if (menuState == SERVICE_STATE_UNINITIALIZED) {
      MenuManagerConfig menuCfg = MenuManager_GetDefaultConfig();
      if (!menu->InitializeWithConfig || menu->InitializeWithConfig(menu, &menuCfg, NULL) != 0) return -1;
      menuState = menu->base.GetState(&menu->base);
    }
    if (menuState != SERVICE_STATE_STARTED) {
      if ((menuState != SERVICE_STATE_INITIALIZED && menuState != SERVICE_STATE_STOPPED) ||
          !menu->base.Start || menu->base.Start(&menu->base) != 0 ||
          menu->base.GetState(&menu->base) != SERVICE_STATE_STARTED) return -1;
    }
    ESP_LOGI(TAG, "MenuManager ready");
  } else {
    ESP_LOGW(TAG, "MenuManager not resolved");
  }

  uiControllerInstance.pairingTimeoutTracked = false;

  // Persistent screens survive Stop/Start; a partial first build is rolled back.
  if (!uiControllerInstance.mainScreen && !UIRenderer_CreateUI()) return -1;
  uiControllerInstance.state = SERVICE_STATE_STARTED;

  // Subscribe to EventBus events
  IEventBus *eb = ServiceLocator_GetEventBus();
  if (eb) {
    uiControllerInstance.ownedBus = eb;
    for (unsigned i = 0; i < sizeof(uiEventNames) / sizeof(uiEventNames[0]); ++i) {
      if (!eb->Subscribe || !eb->Unsubscribe || eb->Subscribe(eb, uiEventNames[i], uiEventHandlers[i]) != 0) {
        (void)UI_StopOwned(previous);
        return -1;
      }
      uiControllerInstance.subscriptions |= 1U << i;
    }
  }

  if (WatchdogWorker_Start(&uiControllerInstance.worker, UIController_Task, "UICtrl",
                           uiControllerInstance.config.taskStackSize, NULL,
                           uiControllerInstance.config.taskPriority) != 0) {
    (void)UI_StopOwned(previous);
    return -1;
  }
  uiControllerInstance.taskHandle = atomic_load(&uiControllerInstance.worker.task);

  return 0;
}

/**
 * @brief Stop the UI controller service, delete resources and FreeRTOS tasks.
 *
 * @param self Pointer to the base IService structure.
 * @return int32_t 0 on success, or a non-zero error code.
 */
static int32_t UIController_Start(IService *self) {
  if (self != &uiControllerInstance.base.base || !uiControllerInstance.lifecycleMutex ||
      xSemaphoreTake(uiControllerInstance.lifecycleMutex, pdMS_TO_TICKS(WATCHDOG_WORKER_CONTROL_TIMEOUT_MS)) != pdTRUE) return -1;
  int32_t result = UI_StartOwned(self);
  xSemaphoreGive(uiControllerInstance.lifecycleMutex);
  return result;
}

static int32_t UIController_Stop(IService *self) {
  TaskHandle_t task = atomic_load(&uiControllerInstance.worker.task);
  if (self != &uiControllerInstance.base.base || !uiControllerInstance.lifecycleMutex ||
      (task && task == xTaskGetCurrentTaskHandle()) ||
      xSemaphoreTake(uiControllerInstance.lifecycleMutex, pdMS_TO_TICKS(WATCHDOG_WORKER_CONTROL_TIMEOUT_MS)) != pdTRUE) return -1;
  int32_t result = UI_StopOwned(SERVICE_STATE_STOPPED);
  xSemaphoreGive(uiControllerInstance.lifecycleMutex);
  return result;
}

/**
 * @brief Get the current state of the UI controller service.
 *
 * @param self Pointer to the base IService structure.
 * @return ServiceState Current state.
 */
static ServiceState UIController_GetState(IService *self) {
  return uiControllerInstance.state;
}

/**
 * @brief Run a self-check to evaluate the health of the UI controller service.
 *
 * @param self Pointer to the base IService structure.
 * @return int32_t 0 on success, or a negative error code if unhealthy.
 */
static int32_t UIController_CheckHealth(IService *self) {
  if (uiControllerInstance.state != SERVICE_STATE_STARTED) {
    return -1;
  }
  if (!atomic_load(&uiControllerInstance.worker.task)) {
    return -2;
  }
  if (!uiControllerInstance.eventQueue) {
    return -3;
  }
  if (!uiControllerInstance.mutex) {
    return -4;
  }
  if (!WatchdogWorker_Healthy(&uiControllerInstance.worker)) return -5;
  return 0;
}

/**
 * @brief Initialize the UI controller with a specific configuration.
 *
 * @param self Pointer to the IUIController instance.
 * @param config Configuration structure for the UI controller.
 * @param registry Pointer to the dependency injection registry.
 * @return int32_t 0 on success, or a non-zero error code.
 */
static bool UI_ConfigValid(const UIControllerConfig *cfg) {
    if (cfg->taskPriority == 0 || cfg->taskPriority >= RACOOL_TASK_PRIORITY_THERMAL_SENSOR ||
        cfg->taskStackSize < UI_CONFIG_STACK_MIN_BYTES || cfg->taskStackSize > UI_CONFIG_STACK_MAX_BYTES ||
        cfg->eventQueueSize == 0 || cfg->eventQueueSize > UI_CONFIG_QUEUE_MAX_ITEMS ||
        cfg->refreshIntervalMs == 0 || cfg->refreshIntervalMs > UI_CONFIG_REFRESH_MAX_MS ||
        cfg->mutexTimeoutMs > UI_CONFIG_MUTEX_MAX_MS ||
        cfg->screenTimeoutSec < OLED_TIMEOUT_MIN_SEC || cfg->screenTimeoutSec > OLED_TIMEOUT_MAX_SEC ||
        cfg->healthCheckIntervalMs == 0 || cfg->healthCheckIntervalMs > UI_CONFIG_HEALTH_MAX_MS ||
        !isfinite(cfg->tempRefreshHysteresis) || cfg->tempRefreshHysteresis < 0) return false;
    const float boundaries[] = {cfg->tempFreeze, cfg->tempColdMin, cfg->tempColdMax,
        cfg->tempWarmMin, cfg->tempWarmMax, cfg->tempHotMin, cfg->tempHotMax, cfg->tempDanger};
    for (unsigned i = 0; i < sizeof(boundaries) / sizeof(boundaries[0]); ++i)
      if (!isfinite(boundaries[i]) || (i && boundaries[i] < boundaries[i - 1])) return false;
    return true;
  }

static int32_t UIController_InitializeWithConfig(IUIController *self,
                                                 UIControllerConfig config,
                                                 IDIRegistry *registry) {
  /* Startup-only configuration: persistent queue and worker resources must retain
   * the configuration they were created with. Initialize precedes concurrent use. */
  if (self != &uiControllerInstance.base ||
      uiControllerInstance.state != SERVICE_STATE_UNINITIALIZED ||
      !UI_ConfigValid(&config)) return -1;
  UIControllerConfig previous = uiControllerInstance.config;
  uiControllerInstance.config = config;
  int32_t result = UIController_Initialize(&self->base, registry);
  if (result != 0) uiControllerInstance.config = previous;
  return result;
}

/**
 * @brief Check if the screen is currently active and turned on.
 *
 * @param self Pointer to the IUIController instance.
 * @return true if screen is active, false if suspended.
 */
static bool UIController_IsScreenOn(IUIController *self) {
  return uiControllerInstance.isScreenOn;
}

/**
 * @brief Get the singleton instance of the UIController.
 *
 * @return IUIController* Pointer to UIController instance.
 */
IUIController *UIController_GetModule(void) {
  static bool initialized = false;
  if (!initialized) {
    uiControllerInstance.base.base.Initialize = UIController_Initialize;
    uiControllerInstance.base.base.Start = UIController_Start;
    uiControllerInstance.base.base.Stop = UIController_Stop;
    uiControllerInstance.base.base.GetState = UIController_GetState;
    uiControllerInstance.base.base.CheckHealth = UIController_CheckHealth;
    
    uiControllerInstance.base.InitializeWithConfig = UIController_InitializeWithConfig;
    uiControllerInstance.base.IsScreenOn = UIController_IsScreenOn;
    uiControllerInstance.base.PostEvent = UIController_PostEvent;

    uiControllerInstance.state = SERVICE_STATE_UNINITIALIZED;
    uiControllerInstance.taskHandle = NULL;
    if (uiControllerInstance.lifecycleMutex) vSemaphoreDelete(uiControllerInstance.lifecycleMutex);
    uiControllerInstance.lifecycleMutex = NULL;
    uiControllerInstance.eventQueue = NULL;
    uiControllerInstance.mutex = NULL;
    initialized = true;
  }
  return &uiControllerInstance.base;
}

/**
 * @brief Get the default configuration for UIController.
 *
 * @return UIControllerConfig Default settings.
 */
UIControllerConfig UIController_GetDefaultConfig(void) {
  UIControllerConfig cfg = {
      .taskStackSize = 8192,
      .taskPriority = RACOOL_TASK_PRIORITY_UI,
      .refreshIntervalMs = 20,
      .screenTimeoutSec = 30,
      .tempRefreshHysteresis = 0.2f,
      .tempColdMin = 17.0f,
      .tempColdMax = 19.0f,
      .tempWarmMin = 35.0f,
      .tempWarmMax = 37.0f,
      .tempHotMin = 50.0f,
      .tempHotMax = 52.0f,
      .tempDanger = 60.0f,
      .tempFreeze = 2.0f,
      .healthCheckIntervalMs = 5000,
      .eventQueueSize = 10,
      .mutexTimeoutMs = 100,
  };
  return cfg;
}


