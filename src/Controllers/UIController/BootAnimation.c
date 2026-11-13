#include "Controllers/UIController/BootAnimation.h"
#include "Core/Localization/Localization.h"
#include "Controllers/UIController/UIControllerInternal.h"
#include "Core/SystemInitializer/SystemInitializerStartup.h"
#include "Hardware/RGBLED/IRGBLED.h"
#include "Hardware/BuiltinLED/IBuiltinLED.h"
#include "Hardware/RGBLED/Animations/RainbowFlowAnim.h"
#include "Core/ServiceLocator/ServiceLocator.h"
#include "Core/ServiceLocator/ServiceNames.h"
#include "Core/SystemInitializer/SystemInitializerConfig.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <string.h>
#include <stdio.h>
#include <stdatomic.h>

static const char *BOOT_TAG = "BootAnimation";

// Stage Durations (ms)
#define BOOT_FLASH_DURATION_MS       400UL
#define BOOT_LOGO_DURATION_MS       1800UL
#if defined(RACOOL_BOOT_FRAME_DIAGNOSTIC)
/* A bounded diagnostic window lets the USB console read the real splash pixels.
 * Production timing and the startup worker are otherwise unchanged. */
#define BOOT_LOGO_STAGE_MS         18000UL
#define BOOT_MAX_DURATION_MS       30000UL
#else
#define BOOT_LOGO_STAGE_MS          BOOT_LOGO_DURATION_MS
#define BOOT_MAX_DURATION_MS       SYSTEM_INITIALIZER_WORKER_TIMEOUT_MS
#endif
#define BOOT_TAGLINE_DURATION_MS    1400UL
#define BOOT_PROGRESS_MIN_MS        2500UL
#define BOOT_PROGRESS_MAX_MS        4500UL
#define BOOT_DISSOLVE_DURATION_MS   1200UL

static _Atomic BootAnimState s_animState = BOOT_ANIM_STATE_IDLE;
static uint32_t s_bootStartMs = 0;
static uint32_t s_stateStartMs = 0;
static uint32_t s_lastLogMs = 0;
static atomic_bool s_systemReady = false;
static bool s_isSkipped = false;
static bool s_rgbBootResolved = false;

#define STAGE_BIT(stage) (1U << (stage))
typedef struct {
  uint32_t mask;
  LocalizationMessage preparing;
  LocalizationMessage ready;
} BootProgressGroup;
static const BootProgressGroup s_progressGroups[] = {
  {STAGE_BIT(STARTUP_STAGE_CORE)|STAGE_BIT(STARTUP_STAGE_CONFIGURATION)|STAGE_BIT(STARTUP_STAGE_DISPLAY)|STAGE_BIT(STARTUP_STAGE_ENCODER)|STAGE_BIT(STARTUP_STAGE_MENU), LOC_BOOT_GROUP_CORE_LOADING, LOC_BOOT_GROUP_CORE_READY},
  {STAGE_BIT(STARTUP_STAGE_LEDS)|STAGE_BIT(STARTUP_STAGE_FAN), LOC_BOOT_GROUP_LIGHTS_LOADING, LOC_BOOT_GROUP_LIGHTS_READY},
  {STAGE_BIT(STARTUP_STAGE_INFRARED)|STAGE_BIT(STARTUP_STAGE_CPU_SENSOR), LOC_BOOT_GROUP_SENSORS_LOADING, LOC_BOOT_GROUP_SENSORS_READY},
  {STAGE_BIT(STARTUP_STAGE_ZIGBEE), LOC_BOOT_STAGE_ZIGBEE, LOC_BOOT_GROUP_ZIGBEE_READY},
  {STAGE_BIT(STARTUP_STAGE_MONITORING)|STAGE_BIT(STARTUP_STAGE_CONTROLLERS)|STAGE_BIT(STARTUP_STAGE_APPLICATION), LOC_BOOT_GROUP_CONTROL_LOADING, LOC_BOOT_GROUP_CONTROL_READY},
};
#define BOOT_PROGRESS_GROUP_COUNT (sizeof(s_progressGroups)/sizeof(s_progressGroups[0]))
static uint8_t s_progressQueue[BOOT_PROGRESS_GROUP_COUNT];
static unsigned s_progressCount, s_progressCursor;
static uint32_t s_progressSeen, s_progressMessageAt;
static bool s_progressMessageValid;
static LocalizationMessage s_progressMessage;
static bool s_progressDrained;

static void BootAnimation_StopLEDs(void) {
  /* End both owners here. Waiting for the UI polling loop leaves the built-in
   * startup effect running after the external worker has already stopped. */
  IBuiltinLED *builtin = uiControllerInstance.builtinLed;
  if (!builtin) builtin = (IBuiltinLED *)ServiceLocator_GetService(SVC_NAME_BUILTIN_LED);
  if (builtin && builtin->SetStartupActive) {
    esp_err_t result = builtin->SetStartupActive(builtin, false);
    if (result != ESP_OK)
      ESP_LOGW(BOOT_TAG, "Built-in startup release admission failed: %d; UI owner will retry", (int)result);
  }
  RainbowFlowAnim_EndSynchronizedPhase();
  IRGBLED *rgb = (IRGBLED *)ServiceLocator_GetService(SVC_NAME_RGB_LED);
  if (!rgb && uiControllerInstance.rgbLed) {
    rgb = uiControllerInstance.rgbLed;
  }
  if (rgb) {
    ESP_LOGI(BOOT_TAG, "[BOOT-RGB] Stopping boot animation on External RGB LED");
    esp_err_t result = rgb->StopAnimation(rgb);
    if (result != ESP_OK)
      ESP_LOGW(BOOT_TAG, "External startup stop admission failed: %d; worker will reconcile", (int)result);
  }
  s_rgbBootResolved = false;
}

static void BootAnimation_TryStartRGB(void) {
  if (s_rgbBootResolved) return;
  IConfigurationManager *config = ConfigurationManager_GetInstance();
  SystemSettings snapshot;
  if (!config || !config->GetSnapshot || config->GetSnapshot(config, &snapshot) != ESP_OK)
    return; // Retry when persisted settings become available.
  const SystemSettings *settings = &snapshot;
  RGBLEDAnimType animation = (RGBLEDAnimType)settings->hardware.ext_led_anim;
  if (animation == RGBLED_ANIM_NONE || settings->hardware.ext_led_mode == EXT_LED_MODE_OFF) {
    s_rgbBootResolved = true;
    return;
  }
  IRGBLED *rgb = (IRGBLED *)ServiceLocator_GetService(SVC_NAME_RGB_LED);
  if (!rgb) rgb = uiControllerInstance.rgbLed;
  if (!rgb) return;
  /* RGBLEDTask also selects the configured animation while the splash is
   * active. Reuse that worker state instead of resetting the same animation
   * when the UI boot tick later retries this readiness check. */
  RGBLEDState current;
  if (rgb->GetLEDState && rgb->GetLEDState(rgb, &current) == ESP_OK &&
      current.isOn && current.colorMode == RGBLED_COLOR_MODE_ANIMATION &&
      current.activeAnim == animation && current.animPriority == RGBLED_PRIO_HIGH) {
    s_rgbBootResolved = true;
    return;
  }
  if (rgb->StartAnimation(rgb, animation, RGBLED_PRIO_HIGH, 0) == ESP_OK) {
    s_rgbBootResolved = true;
    ESP_LOGI(BOOT_TAG, "[BOOT-RGB] Started configured animation (%d)", (int)animation);
  }
}

void BootAnimation_Start(void) {
  s_animState = BOOT_ANIM_STATE_FLASH;
  s_bootStartMs = 0;
  s_stateStartMs = 0;
  s_lastLogMs = 0;
  s_systemReady = false;
  s_isSkipped = false;
  s_rgbBootResolved = false;
  s_progressCount=s_progressCursor=0;
  s_progressSeen=0;
  s_progressMessageValid=s_progressDrained=false;
  RainbowFlowAnim_BeginSynchronizedPhase(
      (uint32_t)((uint64_t)esp_timer_get_time() / 1000ULL));
  ESP_LOGI(BOOT_TAG, "[BOOT-START] Animated splash sequence initiated");

  BootAnimation_TryStartRGB();
}

bool BootAnimation_IsRunning(void) {
  return (s_animState != BOOT_ANIM_STATE_IDLE && s_animState != BOOT_ANIM_STATE_DONE);
}

void BootAnimation_Skip(void) {
  if (s_animState == BOOT_ANIM_STATE_DONE || s_animState == BOOT_ANIM_STATE_IDLE) {
    return;
  }
  ESP_LOGI(BOOT_TAG, "[BOOT-SKIP] User interaction detected (Encoder Click). Smooth transition to Main Screen.");
  s_isSkipped = true;
  s_animState = BOOT_ANIM_STATE_DONE;
  BootAnimation_StopLEDs();
  
  if (uiControllerInstance.oled) {
    if (!uiControllerInstance.oled->Lock(uiControllerInstance.oled)) return;
  }
  if (uiControllerInstance.mainScreen) {
    lv_screen_load_anim(uiControllerInstance.mainScreen, LV_SCREEN_LOAD_ANIM_MOVE_LEFT, 250, 0, false);
  }
  if (uiControllerInstance.oled) {
    uiControllerInstance.oled->Unlock(uiControllerInstance.oled);
  }
}

void BootAnimation_NotifyReady(void) {
  s_systemReady = true;
  ESP_LOGI(BOOT_TAG, "[BOOT-READY] Application service startup completed.");
}

/* Fixed-capacity UI queue: startup workers only publish atomic history.
 * Every distinct visible message receives a full dwell; no worker is delayed. */
static void BootAnimation_UpdateProgressText(uint32_t nowMs) {
  SystemStartupProgress progress=SystemInitializer_GetStartupProgress();
  uint32_t observed=progress.startedStages|progress.activeStages|progress.completedStages;
  for (unsigned i=0;i<BOOT_PROGRESS_GROUP_COUNT;++i) {
    if ((observed&s_progressGroups[i].mask) && !(s_progressSeen&(1U<<i))) {
      s_progressQueue[s_progressCount++]=(uint8_t)i;
      s_progressSeen|=1U<<i;
    }
  }
  LocalizationMessage message=LOC_BOOT_INITIALIZING;
  bool ready=atomic_load(&s_systemReady);
  if (progress.failed) {
    atomic_store(&s_systemReady,false);
    message=LOC_BOOT_FAILED;
    s_progressDrained=false;
  } else {
    bool dwell=s_progressMessageValid && (uint32_t)(nowMs-s_progressMessageAt)>=UI_STARTUP_MESSAGE_MIN_MS;
    if (s_progressCursor<s_progressCount) {
      const BootProgressGroup *group=&s_progressGroups[s_progressQueue[s_progressCursor]];
      bool complete=!(progress.activeStages&group->mask) &&
          ((progress.completedStages&group->mask)==group->mask || ready);
      message=complete ? group->ready : group->preparing;
      /* Publish a completion message before retiring this queue entry. */
      if (dwell && message==s_progressMessage) {
        ++s_progressCursor;
        if (s_progressCursor<s_progressCount) {
          group=&s_progressGroups[s_progressQueue[s_progressCursor]];
          complete=!(progress.activeStages&group->mask) &&
              ((progress.completedStages&group->mask)==group->mask || ready);
          message=complete ? group->ready : group->preparing;
        } else message=ready ? LOC_BOOT_READY : message;
      }
    } else if (ready) message=LOC_BOOT_READY;
    else {
      for(unsigned i=0;i<BOOT_PROGRESS_GROUP_COUNT;++i)
        if(progress.activeStages&s_progressGroups[i].mask) { message=s_progressGroups[i].preparing;break; }
    }
    /* Do not replace a visible message before its minimum dwell, even when
     * its service completes. Errors above intentionally bypass this gate. */
    if(s_progressMessageValid && !dwell) message=s_progressMessage;
    s_progressDrained=ready && s_progressCursor==s_progressCount &&
        s_progressMessageValid && message==LOC_BOOT_READY &&
        s_progressMessage==LOC_BOOT_READY && dwell;
  }
  if(!s_progressMessageValid || message!=s_progressMessage) {
    s_progressMessage=message;s_progressMessageAt=nowMs;s_progressMessageValid=true;
    ESP_LOGI(BOOT_TAG,"[BOOT-MESSAGE] t=%lu id=%u pending=%u",(unsigned long)nowMs,
             (unsigned)message,s_progressCount-s_progressCursor);
  }
  const LocalizationCatalog *catalog=Localization_Snapshot();
  if(uiControllerInstance.bootTaglineLabel)
    lv_label_set_text(uiControllerInstance.bootTaglineLabel,
        message==LOC_BOOT_READY ? "" : Localization_Text(catalog,message));
  if(uiControllerInstance.bootStatusLabel)
    lv_label_set_text(uiControllerInstance.bootStatusLabel,Localization_Text(catalog,
        progress.failed ? LOC_BOOT_FAILED : ready ? LOC_BOOT_READY : LOC_BOOT_INITIALIZING));
}

void BootAnimation_Tick(uint32_t nowMs) {
  if (s_animState == BOOT_ANIM_STATE_IDLE || s_animState == BOOT_ANIM_STATE_DONE) {
    return;
  }

  if (s_bootStartMs == 0) {
    s_bootStartMs = nowMs;
    s_stateStartMs = nowMs;
    s_lastLogMs = nowMs;
  }

  uint32_t totalElapsed = (nowMs >= s_bootStartMs) ? (nowMs - s_bootStartMs) : 0;
  uint32_t stateElapsed = (nowMs >= s_stateStartMs) ? (nowMs - s_stateStartMs) : 0;

  // Periodic heartbeat log for runtime inspection (every 1s)
  if (nowMs - s_lastLogMs >= 1000) {
    s_lastLogMs = nowMs;
    ESP_LOGI(BOOT_TAG, "[BOOT-TICK] State: %d, Total: %lums / %lums, SysReady: %d",
             (int)s_animState, (unsigned long)totalElapsed, (unsigned long)BOOT_MAX_DURATION_MS, s_systemReady ? 1 : 0);
  }

  /* Only the startup coordinator's completion notification authorizes Ready. */

  /* Service startup retains its own deadline. Only the UI may spend an extra
   * bounded dwell budget draining messages after late readiness. */
  uint32_t displayBudget=BOOT_MAX_DURATION_MS;
  if(atomic_load(&s_systemReady))
    displayBudget+=(2U*BOOT_PROGRESS_GROUP_COUNT+1U)*UI_STARTUP_MESSAGE_MIN_MS;
  if (totalElapsed >= displayBudget) {
    ESP_LOGW(BOOT_TAG, "[BOOT-TIMEOUT] %lums maximum limit reached -> Terminating animation.", (unsigned long)BOOT_MAX_DURATION_MS);
    BootAnimation_Skip();
    return;
  }

  BootAnimation_TryStartRGB();

  if (uiControllerInstance.oled) {
    if (!uiControllerInstance.oled->Lock(uiControllerInstance.oled)) return;
  }

  switch (s_animState) {
    case BOOT_ANIM_STATE_FLASH: {
      // White flash for 150ms, then black canvas
      if (uiControllerInstance.bootScreen) {
        if (stateElapsed < 150) {
          lv_obj_set_style_bg_color(uiControllerInstance.bootScreen, lv_color_hex(0xFFFFFF), 0);
        } else {
          lv_obj_set_style_bg_color(uiControllerInstance.bootScreen, lv_color_hex(0x000000), 0);
        }
      }
      if (stateElapsed >= BOOT_FLASH_DURATION_MS) {
        s_animState = BOOT_ANIM_STATE_LOGO;
        s_stateStartMs = nowMs;
        ESP_LOGI(BOOT_TAG, "[STAGE: LOGO] Unveiling RaCool branding logo.");
        if (uiControllerInstance.bootLogoLabel) {
          lv_obj_set_hidden(uiControllerInstance.bootLogoLabel, false);
        }
        if (uiControllerInstance.bootTopLine) {
          lv_obj_set_hidden(uiControllerInstance.bootTopLine, false);
        }
        if (uiControllerInstance.bootVersionLabel) {
          lv_obj_set_hidden(uiControllerInstance.bootVersionLabel, false);
        }
      }
      break;
    }

    case BOOT_ANIM_STATE_LOGO: {
      // Horizontal expanding divider line
      if (uiControllerInstance.bootTopLine) {
        int32_t width = (stateElapsed * 128) / BOOT_LOGO_DURATION_MS;
        if (width > 128) width = 128;
        lv_obj_set_size(uiControllerInstance.bootTopLine, width, 1);
      }

      if (stateElapsed >= BOOT_LOGO_STAGE_MS) {
        s_animState = BOOT_ANIM_STATE_TAGLINE;
        s_stateStartMs = nowMs;
        ESP_LOGI(BOOT_TAG, "[STAGE: TAGLINE] Showing active startup services.");
        if (uiControllerInstance.bootTaglineLabel) {
          lv_obj_set_hidden(uiControllerInstance.bootTaglineLabel, false);
        }
        if (uiControllerInstance.bootBottomLine) {
          lv_obj_set_hidden(uiControllerInstance.bootBottomLine, false);
        }
      }
      break;
    }

    case BOOT_ANIM_STATE_TAGLINE: {
      if (stateElapsed >= BOOT_TAGLINE_DURATION_MS) {
        s_animState = BOOT_ANIM_STATE_PROGRESS;
        s_stateStartMs = nowMs;
        ESP_LOGI(BOOT_TAG, "[STAGE: PROGRESS] Starting service loading progress bar.");
        if (uiControllerInstance.bootProgressBar) {
          lv_obj_set_hidden(uiControllerInstance.bootProgressBar, false);
        }
        if (uiControllerInstance.bootStatusLabel) {
          lv_obj_set_hidden(uiControllerInstance.bootStatusLabel, false);
        }
      }
      break;
    }

    case BOOT_ANIM_STATE_PROGRESS: {
      // Progress calculation between 0 - 100%
      int32_t progress = (stateElapsed * 100) / BOOT_PROGRESS_MAX_MS;
      if (progress > 95 && !s_systemReady) {
        progress = 95; // Hold at 95% until all services are healthy
      }

      if (s_systemReady && s_progressDrained && stateElapsed >= BOOT_PROGRESS_MIN_MS) {
        progress = 100;
      }

      if (uiControllerInstance.bootProgressBar) {
        lv_bar_set_value(uiControllerInstance.bootProgressBar, progress > 100 ? 100 : progress, LV_ANIM_OFF);
      }

      // A progress estimate must not finish the splash before actual readiness.
      if ((s_systemReady && s_progressDrained && stateElapsed >= BOOT_PROGRESS_MIN_MS) ||
          (!s_systemReady && totalElapsed >= (BOOT_MAX_DURATION_MS - BOOT_DISSOLVE_DURATION_MS))) {
        s_animState = BOOT_ANIM_STATE_DISSOLVE;
        s_stateStartMs = nowMs;
        ESP_LOGI(BOOT_TAG, "[STAGE: DISSOLVE] Dissolving splash into Main Screen.");
        if (uiControllerInstance.bootDissolveOverlay) {
          lv_obj_set_hidden(uiControllerInstance.bootDissolveOverlay, false);
        }
      }
      break;
    }

    case BOOT_ANIM_STATE_DISSOLVE: {
      // Hold on READY for 300ms, then slide in Main Screen smoothly
      if (stateElapsed >= 300) {
        if (s_systemReady) {
          ESP_LOGI(BOOT_TAG, "[BOOT-COMPLETE] Startup ready; transition to Main Screen.");
        } else {
          ESP_LOGE(BOOT_TAG, "[BOOT-INCOMPLETE] Splash finished without startup readiness; transition to Main Screen.");
        }
        s_animState = BOOT_ANIM_STATE_DONE;
        BootAnimation_StopLEDs();
        if (uiControllerInstance.mainScreen) {
          lv_screen_load_anim(uiControllerInstance.mainScreen, LV_SCREEN_LOAD_ANIM_MOVE_LEFT, 400, 0, false);
        }
      }
      break;
    }

    case BOOT_ANIM_STATE_IDLE:
    case BOOT_ANIM_STATE_DONE:
    default:
      break;
  }

  if(s_animState==BOOT_ANIM_STATE_TAGLINE || s_animState==BOOT_ANIM_STATE_PROGRESS)
    BootAnimation_UpdateProgressText(nowMs);

  if (uiControllerInstance.oled) {
    uiControllerInstance.oled->Unlock(uiControllerInstance.oled);
  }
}
