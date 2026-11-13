#include "Controllers/MenuManager/MenuEditSession.h"
#include "Management/ConfigurationManager/ZigbeeReportingSettings.h"
#include "Core/Localization/Localization.h"
#include "Controllers/SystemController/ISystemController.h"
#include "Controllers/SystemController/ISystemTelemetry.h" // Telemetry interface.
#include "Core/DI/IDIRegistry.h"
#include "Core/IService.h"
#include <esp_log.h>
#include <esp_sleep.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <esp_timer.h>
#include "Management/Watchdog/WatchdogWorker.h"


#include "Communication/Zigbee/IZigbee.h"
#include "Communication/Zigbee/ZigbeeConfig.h"
#include "Controllers/MenuManager/IMenuManager.h"
#include "Controllers/SystemController/SystemControllerConfig.h"
#include "Core/ServiceLocator/ServiceLocator.h"
#include "Core/ServiceLocator/ServiceNames.h"
#include "Hardware/BuiltinLED/IBuiltinLED.h"
#include "Hardware/RGBLED/IRGBLED.h"
#include "Management/ConfigurationManager/IConfigurationManager.h"
#include "Management/PowerManager/IPowerManager.h"
#include "Management/ApplicationManager/IApplicationManager.h"


static const char *TAG = "SystemController";


typedef struct {
  uint32_t eventId;
  uint32_t payload;
  bool shutdown;
  bool zigbeeTxProfile;
  bool menuAction;
} SystemEvent;

typedef struct {
  ISystemController base;
  _Atomic ServiceState serviceState;
  _Atomic SystemState systemState;
  SystemControllerConfig config;

  // Dependencies (DIP)
  IRGBLED *rgbLed;
  IBuiltinLED *builtinLed;
  IMenuManager *menuManager;
  ISystemTelemetry *telemetry;

  QueueHandle_t eventQueue;
  SemaphoreHandle_t controlMutex;
  WatchdogWorker worker;
  bool menuRegistered;
  _Atomic bool telemetryOwned;
  bool recoveryPending;
  uint64_t recoveryStartedUs;
  uint64_t recoveryLastAttemptUs;
  uint8_t recoveryAttempts;
  atomic_bool shutdownPending;
  bool shutdownScheduled;
  uint64_t shutdownStartedUs;
  uint32_t shutdownDelayMs;
} SystemControllerImpl;

static SystemControllerImpl controllerInstance;
static portMUX_TYPE systemStateLock = portMUX_INITIALIZER_UNLOCKED;
static void SC_ApplyState(SystemState state);
static int32_t SC_RequestShutdown(ISystemController *self, uint32_t delay_ms);

/**
 * @brief Update the status LEDs based on system state
 */
static void SC_UpdateStatusLED(SystemState state) {
  if ((unsigned)state >= sizeof(controllerInstance.config.indications) / sizeof(controllerInstance.config.indications[0]))
    return;

  SystemStateIndication *ind = &controllerInstance.config.indications[state];

  if (controllerInstance.builtinLed && ind->useBuiltin) {
    esp_err_t result = controllerInstance.builtinLed->SetAnimation(
        controllerInstance.builtinLed, ind->builtinAnim.type,
        ind->builtinAnim.priority, ind->builtinAnim.duration_ms);
    if (result != ESP_OK) ESP_LOGW(TAG, "System indicator update rejected: %d", result);
  }
}

/* Recovery is owned by the existing worker, so Stop also cancels its deadline. */
static void SC_CheckRecovery(void) {
  IConfigurationManager *config = ConfigurationManager_GetInstance();
  if (config && config->IsMutationAllowed && !config->IsMutationAllowed(config)) return;
  uint64_t nowUs = (uint64_t)esp_timer_get_time();
  portENTER_CRITICAL(&systemStateLock);
  uint64_t startedUs = controllerInstance.recoveryStartedUs;
  bool due = controllerInstance.recoveryPending &&
      nowUs >= startedUs &&
      nowUs - startedUs >= (uint64_t)SYSTEM_CONTROLLER_RECOVERY_DELAY_MS * 1000ULL &&
      (controllerInstance.recoveryAttempts == 0 ||
       (nowUs >= controllerInstance.recoveryLastAttemptUs &&
        nowUs - controllerInstance.recoveryLastAttemptUs >=
            (uint64_t)SYSTEM_CONTROLLER_RECOVERY_RETRY_MS * 1000ULL));
  if (due) {
    controllerInstance.recoveryLastAttemptUs = nowUs;
    ++controllerInstance.recoveryAttempts;
  }
  portEXIT_CRITICAL(&systemStateLock);
  if (due && controllerInstance.serviceState == SERVICE_STATE_STARTED) {
    IPowerManager *power = (IPowerManager *)ServiceLocator_GetService(SVC_NAME_POWER_MANAGER);
    int32_t result = power && power->RestartAsync ? power->RestartAsync(power, 0) : POWER_ERR_INVALID_STATE;
    portENTER_CRITICAL(&systemStateLock);
    bool exhausted = controllerInstance.recoveryAttempts >= SYSTEM_CONTROLLER_RECOVERY_MAX_ATTEMPTS;
    if (controllerInstance.recoveryStartedUs == startedUs && (result == POWER_OK || exhausted))
      controllerInstance.recoveryPending = false;
    portEXIT_CRITICAL(&systemStateLock);
    if (result != POWER_OK) {
      ESP_LOGE(TAG, "Recovery restart rejected: %ld%s", (long)result,
               exhausted ? "; retry budget exhausted" : "; retry scheduled");
      return;
    }
    ESP_LOGE(TAG, "Automatic recovery restart requested");
  }
}

/* Only the existing event worker owns the shutdown deadline and calls the power service. */
static void SC_ProcessEvent(const SystemEvent *event) {
  if (event->menuAction) {
    if (event->payload == UI_STATE_RESTART) {
      IPowerManager *power = (IPowerManager *)ServiceLocator_GetService(SVC_NAME_POWER_MANAGER);
      int32_t result = power && power->RestartAsync ? power->RestartAsync(power, 0) : POWER_ERR_INVALID_STATE;
      if (result != POWER_OK) ESP_LOGE(TAG, "Menu restart request failed: %ld", (long)result);
      return;
    }
    IZigbee *radio = (IZigbee *)ServiceLocator_GetService(SVC_NAME_ZIGBEE);
    if (event->payload == UI_STATE_ZIGBEE_PAIR) {
      int32_t result = radio && radio->ConnectToNetwork ? radio->ConnectToNetwork(radio) : ESP_ERR_INVALID_STATE;
      if (result != ESP_OK) ESP_LOGE(TAG, "Menu pairing request failed: %ld", (long)result);
      return;
    }
    if (event->payload != UI_STATE_FACTORY_RESET) return;
    IApplicationManager *app = (IApplicationManager *)ServiceLocator_GetService(SVC_NAME_APPLICATION_MANAGER);
    esp_err_t result = app && app->TriggerFactoryReset ? app->TriggerFactoryReset(app) : ESP_ERR_INVALID_STATE;
    if (result != ESP_OK) ESP_LOGE(TAG, "Factory reset failed: %d", result);
    /* The application worker owns completion and checked automatic restart. */
    return;
  }
  if (event->zigbeeTxProfile) {
    IZigbee *radio = (IZigbee *)ServiceLocator_GetService(SVC_NAME_ZIGBEE);
    int32_t result = radio && radio->SetTxProfile ?
        radio->SetTxProfile(radio, (ZigbeeTxProfile)event->payload) : ESP_ERR_INVALID_STATE;
    if (result != ESP_OK) ESP_LOGE(TAG, "Queued Zigbee TX profile failed: %ld", (long)result);
    else ESP_LOGI(TAG, "Applied Zigbee TX profile: %lu", (unsigned long)event->payload);
    ZigbeeTxStatus status;
    if (radio && radio->GetTxStatus && radio->QueueAttributeUpdate &&
        radio->GetTxStatus(radio, &status) == ESP_OK) {
      uint8_t actual = (uint8_t)status.profile;
      int32_t refresh = radio->QueueAttributeUpdate(radio, ZIGBEE_EP_SYSTEM,
          ZIGBEE_SYSTEM_CONFIG_CLUSTER, ZIGBEE_ATTR_TX_PROFILE, &actual, sizeof(actual));
      if (refresh != 0) ESP_LOGE(TAG, "TX profile readback queue failed: %ld", (long)refresh);
    }
    return;
  }
  if (event->shutdown) {
    controllerInstance.shutdownScheduled = true;
    controllerInstance.shutdownStartedUs = (uint64_t)esp_timer_get_time();
    controllerInstance.shutdownDelayMs = event->payload;
    portENTER_CRITICAL(&systemStateLock);
    controllerInstance.recoveryPending = false;
    portEXIT_CRITICAL(&systemStateLock);
    return;
  }
  for (size_t i = 0; i < controllerInstance.config.eventMappingCount &&
       i < sizeof(controllerInstance.config.eventMappings) / sizeof(controllerInstance.config.eventMappings[0]); ++i) {
    if (controllerInstance.config.eventMappings[i].eventId == event->eventId) {
      SC_ApplyState(controllerInstance.config.eventMappings[i].targetState);
      break;
    }
  }
}

static void SC_CheckShutdown(void) {
  if (!controllerInstance.shutdownScheduled || controllerInstance.serviceState != SERVICE_STATE_STARTED) return;
  uint64_t elapsed = (uint64_t)esp_timer_get_time() - controllerInstance.shutdownStartedUs;
  if (elapsed < (uint64_t)controllerInstance.shutdownDelayMs * 1000u) return;
  controllerInstance.shutdownScheduled = false;
  IPowerManager *power = (IPowerManager *)ServiceLocator_GetService(SVC_NAME_POWER_MANAGER);
  int32_t result = power && power->EnterDeepSleep ? power->EnterDeepSleep(power, 0) : POWER_ERR_INVALID_STATE;
  if (result != POWER_OK) ESP_LOGE(TAG, "Queued shutdown preparation failed: %ld", (long)result);
  atomic_store(&controllerInstance.shutdownPending, false);
}

static void SC_EventLoop(void *params) {
  (void)params;
  SystemEvent event;
  TickType_t wait = pdMS_TO_TICKS(SYSTEM_CONTROLLER_WORKER_POLL_MS);
  if (!wait) wait = 1;
  for (;;) {
    WatchdogWorker_Checkpoint(&controllerInstance.worker, NULL, (uint64_t)esp_timer_get_time() / 1000ULL);
    BaseType_t received = xQueueReceive(controllerInstance.eventQueue, &event, wait);
    WatchdogWorker_Checkpoint(&controllerInstance.worker, NULL, (uint64_t)esp_timer_get_time() / 1000ULL);
    if (controllerInstance.serviceState != SERVICE_STATE_STARTED) continue;
    if (received == pdPASS) SC_ProcessEvent(&event);
    SC_CheckShutdown();
    SC_CheckRecovery();
  }
}

static void System_MenuHandler(UIState state, int8_t direction,
                               bool isButtonPressed) {
  if (controllerInstance.serviceState != SERVICE_STATE_STARTED) return;
  if (!isButtonPressed)
    return;

  if (state == UI_STATE_SHUTDOWN) {
    int32_t result = SC_RequestShutdown(&controllerInstance.base, 0);
    if (result != ESP_OK) ESP_LOGE(TAG, "Shutdown queue rejected: %ld", (long)result);
    return;
  }
  if (state != UI_STATE_RESTART && state != UI_STATE_ZIGBEE_PAIR && state != UI_STATE_FACTORY_RESET) return;
  SystemEvent event = {.menuAction = true, .payload = (uint32_t)state};
  if (!controllerInstance.eventQueue || xQueueSend(controllerInstance.eventQueue, &event, 0) != pdPASS)
    ESP_LOGE(TAG, "Menu action queue rejected: %u", (unsigned)state);
}

/**
 * @brief Configuration menu for telemetry interval (New Feature)
 */
static void Telemetry_ConfigHandler(UIState state, int8_t direction, bool pressed) {
  if(pressed && state==UI_STATE_SET_TELEMETRY) {
    IConfigurationManager *owner=(IConfigurationManager *)ServiceLocator_GetService(SVC_NAME_CONFIGURATION_MANAGER);
    SystemSettings snapshot;
    esp_err_t result=owner && owner->GetSnapshot ? owner->GetSnapshot(owner,&snapshot) : ESP_ERR_INVALID_STATE;
    if(result==ESP_OK && controllerInstance.serviceState==SERVICE_STATE_STARTED &&
       controllerInstance.telemetry && controllerInstance.telemetry->SetReportingInterval &&
       controllerInstance.telemetry->GetReportingInterval) {
      controllerInstance.telemetry->SetReportingInterval(controllerInstance.telemetry,snapshot.system.telemetry_interval_ms);
      if(controllerInstance.telemetry->GetReportingInterval(controllerInstance.telemetry)!=snapshot.system.telemetry_interval_ms)
        result=ESP_ERR_INVALID_STATE;
    }
    else result=ESP_ERR_INVALID_STATE;
    MenuEditSession_ReportResult(state,result);
    return;
  }
  if (controllerInstance.serviceState != SERVICE_STATE_STARTED ||
      state != UI_STATE_SET_TELEMETRY || pressed || !direction || !controllerInstance.telemetry) return;
  IConfigurationManager *config = MenuEditSession_Config((IConfigurationManager *)ServiceLocator_GetService(SVC_NAME_CONFIGURATION_MANAGER));
  SystemSettings snapshot;
  if (!config || !config->GetSnapshot || !config->UpdateFields ||
      config->GetSnapshot(config, &snapshot) != ESP_OK) return;
  int64_t next = (int64_t)snapshot.system.telemetry_interval_ms +
      (int64_t)direction * SYSTEM_TELEMETRY_MENU_STEP_MS;
  if (next < TELEMETRY_MIN_MS) next = TELEMETRY_MIN_MS;
  if (next > TELEMETRY_MAX_MS) next = TELEMETRY_MAX_MS;
  uint32_t value = (uint32_t)next;
  ConfigurationFieldUpdate field = {offsetof(SystemBaseConfigs, telemetry_interval_ms), &value, sizeof(value)};
  if (value != snapshot.system.telemetry_interval_ms &&
      config->UpdateFields(config, "system", &field, 1, true) != ESP_OK) {
    ESP_LOGE(TAG, "Telemetry interval save failed");
    return;
  }
  if(MenuEditSession_State()!=state)controllerInstance.telemetry->SetReportingInterval(controllerInstance.telemetry, value);
}

/**
 * @brief Configuration menu for Zigbee reporting intervals in seconds
 */
static void ZigbeeRep_ConfigHandler(UIState state, int8_t direction, bool isButtonPressed) {
  if (controllerInstance.serviceState != SERVICE_STATE_STARTED) return;
  if(isButtonPressed) {
    IZigbee *radio=(IZigbee *)ServiceLocator_GetService(SVC_NAME_ZIGBEE);
    esp_err_t result=radio && radio->ReconfigureAllReporting ? radio->ReconfigureAllReporting(radio) : ESP_ERR_INVALID_STATE;
    MenuEditSession_ReportResult(state,result);
    return;
  }
  if (!direction) return;
  ZigbeeReportKind kind;
  switch (state) {
    case UI_STATE_ZB_REP_FAN: kind = ZIGBEE_REPORT_FAN; break;
    case UI_STATE_ZB_REP_AMBIENT: kind = ZIGBEE_REPORT_AMBIENT; break;
    case UI_STATE_ZB_REP_CPU: kind = ZIGBEE_REPORT_CPU; break;
    case UI_STATE_ZB_REP_HEALTH: kind = ZIGBEE_REPORT_HEALTH; break;
    case UI_STATE_ZB_REP_MEMORY: kind = ZIGBEE_REPORT_MEMORY; break;
    case UI_STATE_ZB_REP_UPTIME: kind = ZIGBEE_REPORT_UPTIME; break;
    default: return;
  }
  IConfigurationManager *config = MenuEditSession_Config((IConfigurationManager *)ServiceLocator_GetService(SVC_NAME_CONFIGURATION_MANAGER));
  uint16_t current;
  esp_err_t result = ZigbeeReporting_GetInterval(config, kind, &current);
  if (result != ESP_OK) { ESP_LOGE(TAG, "Reporting settings read failed: %d", result); return; }
  int32_t next = (int32_t)current + direction;
  if (next < 0) next = 0;
  if (next > ZB_REPORT_MAX_S) next = ZB_REPORT_MAX_S;
  result = ZigbeeReporting_SetInterval(config, kind, (uint32_t)next);
  if (result != ESP_OK) { ESP_LOGE(TAG, "Reporting settings save failed: %d", result); return; }
  if(MenuEditSession_State()==state)return;
  IZigbee *zigbee = (IZigbee *)ServiceLocator_GetService(SVC_NAME_ZIGBEE);
  if (zigbee && zigbee->ReconfigureAllReporting && zigbee->ReconfigureAllReporting(zigbee) != 0) {
    ESP_LOGE(TAG, "Reporting value saved but refresh scheduling failed");
    return;
  }
  ESP_LOGI(TAG, "Reporting interval saved: %ld s (0 disables reports)", (long)next);
}

static void DisplayMode_ConfigHandler(UIState state, int8_t direction, bool pressed) {
  if (controllerInstance.serviceState != SERVICE_STATE_STARTED ||
      state != UI_STATE_SET_DISPLAY_MODE || pressed || !direction) return;
  IConfigurationManager *config = MenuEditSession_Config((IConfigurationManager *)ServiceLocator_GetService(SVC_NAME_CONFIGURATION_MANAGER));
  SystemSettings snapshot;
  if (!config || !config->GetSnapshot || !config->UpdateFields || config->GetSnapshot(config,&snapshot)!=ESP_OK) return;
  uint8_t value=snapshot.system.display_mode==1 ? 2 : 1;
  ConfigurationFieldUpdate field={offsetof(SystemBaseConfigs,display_mode),&value,sizeof(value)};
  esp_err_t result=config->UpdateFields(config,"system",&field,1,true);
  if(result!=ESP_OK) ESP_LOGE(TAG,"Display mode save failed: %d",result);
}

static void MenuAutoExit_ConfigHandler(UIState state, int8_t direction, bool pressed) {
    if (controllerInstance.serviceState!=SERVICE_STATE_STARTED || state!=UI_STATE_SET_MENU_AUTO_EXIT || pressed || !direction) return;
    IConfigurationManager *config=MenuEditSession_Config((IConfigurationManager *)ServiceLocator_GetService(SVC_NAME_CONFIGURATION_MANAGER));
    SystemSettings snapshot;
    if (!config || !config->GetSnapshot || !config->UpdateFields || config->GetSnapshot(config,&snapshot)!=ESP_OK) return;
    uint8_t value=snapshot.system.menu_auto_exit_enabled ? 0 : 1;
    ConfigurationFieldUpdate field={offsetof(SystemBaseConfigs,menu_auto_exit_enabled),&value,sizeof(value)};
    esp_err_t result=config->UpdateFields(config,"system",&field,1,true);
    if(result!=ESP_OK) ESP_LOGE(TAG,"Menu auto-exit save failed: %d",result);
    MenuEditSession_ReportResult(state,result);
}

static void DisplayTimeout_ConfigHandler(UIState state, int8_t direction, bool pressed) {
  if (controllerInstance.serviceState != SERVICE_STATE_STARTED) return;
  if (state != UI_STATE_SET_TIMEOUT || pressed || direction == 0) return;
  IConfigurationManager *config = MenuEditSession_Config((IConfigurationManager *)ServiceLocator_GetService(SVC_NAME_CONFIGURATION_MANAGER));
  SystemSettings snapshot;
  if (!config || !config->GetSnapshot || !config->UpdateFields ||
      config->GetSnapshot(config, &snapshot) != ESP_OK) return;
  int64_t next = (int64_t)snapshot.system.oled_timeout_sec + direction;
  if (next < OLED_SLEEP_ZB_MIN_SEC) next = OLED_SLEEP_ZB_MIN_SEC;
  if (next > OLED_SLEEP_ZB_MAX_SEC) next = OLED_SLEEP_ZB_MAX_SEC;
  uint32_t value = (uint32_t)next;
  if (value == snapshot.system.oled_timeout_sec) return;
  ConfigurationFieldUpdate field = {offsetof(SystemBaseConfigs, oled_timeout_sec), &value, sizeof(value)};
  esp_err_t result = config->UpdateFields(config, "system", &field, 1, true);
  if (result != ESP_OK) ESP_LOGE(TAG, "Display timeout save failed: %d", result);
}

static void PowerProfile_ConfigHandler(UIState state, int8_t direction, bool isButtonPressed) {
  if(controllerInstance.serviceState==SERVICE_STATE_STARTED && state==UI_STATE_SET_POWER_PROFILE &&
      (isButtonPressed || MenuEditSession_State()==state)) {
    IConfigurationManager *config=MenuEditSession_Config((IConfigurationManager *)ServiceLocator_GetService(SVC_NAME_CONFIGURATION_MANAGER));
    SystemSettings snapshot;
    if(!config || !config->GetSnapshot || config->GetSnapshot(config,&snapshot)!=ESP_OK) {MenuEditSession_ReportResult(state,ESP_ERR_INVALID_STATE);return;}
    esp_err_t result=ESP_OK;
    if(isButtonPressed) {
      IPowerManager *power=(IPowerManager *)ServiceLocator_GetService(SVC_NAME_POWER_MANAGER);
      result=power && power->SetPowerMode ? power->SetPowerMode(power,(PowerMode_t)snapshot.system.power_profile) : ESP_ERR_INVALID_STATE;
    } else if(direction) {
      int next=((int)snapshot.system.power_profile-direction)%POWER_PROFILE_COUNT;
      if(next<0)next+=POWER_PROFILE_COUNT;
      uint8_t value=(uint8_t)next;
      ConfigurationFieldUpdate field={offsetof(SystemBaseConfigs,power_profile),&value,sizeof(value)};
      result=config->UpdateFields(config,"system",&field,1,true);
    }
    MenuEditSession_ReportResult(state,result);return;
  }
  if (controllerInstance.serviceState != SERVICE_STATE_STARTED) return;
  if (state != UI_STATE_SET_POWER_PROFILE || isButtonPressed || direction == 0) return;
  IPowerManager *power = (IPowerManager *)ServiceLocator_GetService(SVC_NAME_POWER_MANAGER);
  if (!power || !power->GetPowerMode || !power->SetPowerMode) return;
  PowerMode_t current = power->GetPowerMode(power);
  if (current < POWER_MODE_PERFORMANCE || current > POWER_MODE_LOW_POWER) return;
  int next = ((int)current + direction) % POWER_PROFILE_COUNT;
  if (next < 0) next += POWER_PROFILE_COUNT;
  if (next == (int)current) return;
  int32_t result = power->SetPowerMode(power, (PowerMode_t)next);
  if (result != POWER_OK) ESP_LOGE(TAG, "Power profile update failed: %ld", (long)result);
}

static void TxProfile_ConfigHandler(UIState state, int8_t direction, bool isButtonPressed) {
  if(controllerInstance.serviceState==SERVICE_STATE_STARTED && state==UI_STATE_SET_ZIGBEE_TX_PROFILE &&
      (isButtonPressed || MenuEditSession_State()==state)) {
    IConfigurationManager *config=MenuEditSession_Config((IConfigurationManager *)ServiceLocator_GetService(SVC_NAME_CONFIGURATION_MANAGER));
    SystemSettings snapshot;
    if(!config || !config->GetSnapshot || config->GetSnapshot(config,&snapshot)!=ESP_OK) {MenuEditSession_ReportResult(state,ESP_ERR_INVALID_STATE);return;}
    esp_err_t result=ESP_OK;
    if(isButtonPressed) {
      IZigbee *radio=(IZigbee *)ServiceLocator_GetService(SVC_NAME_ZIGBEE);
      result=radio && radio->SetTxProfile ? radio->SetTxProfile(radio,(ZigbeeTxProfile)snapshot.system.zigbee_tx_profile) : ESP_ERR_INVALID_STATE;
    } else if(direction) {
      int next=((int)snapshot.system.zigbee_tx_profile+direction)%ZIGBEE_TX_PROFILE_COUNT;
      if(next<0)next+=ZIGBEE_TX_PROFILE_COUNT;
      uint8_t value=(uint8_t)next;
      ConfigurationFieldUpdate field={offsetof(SystemBaseConfigs,zigbee_tx_profile),&value,sizeof(value)};
      result=config->UpdateFields(config,"system",&field,1,true);
    }
    MenuEditSession_ReportResult(state,result);return;
  }
  if (controllerInstance.serviceState != SERVICE_STATE_STARTED ||
      state != UI_STATE_SET_ZIGBEE_TX_PROFILE || isButtonPressed || direction == 0) return;
  IZigbee *radio = (IZigbee *)ServiceLocator_GetService(SVC_NAME_ZIGBEE);
  ZigbeeTxStatus status;
  if (!radio || !radio->GetTxStatus || !radio->SetTxProfile ||
      radio->GetTxStatus(radio, &status) != ESP_OK) return;
  int next = ((int)status.profile + direction) % ZIGBEE_TX_PROFILE_COUNT;
  if (next < 0) next += ZIGBEE_TX_PROFILE_COUNT;
  if (next == (int)status.profile) return;
  int32_t result = radio->SetTxProfile(radio, (ZigbeeTxProfile)next);
  if (result != ESP_OK) ESP_LOGE(TAG, "Menu Zigbee TX profile failed: %ld", (long)result);
}

static void Language_ConfigHandler(UIState state,int8_t direction,bool pressed) {
  if(controllerInstance.serviceState!=SERVICE_STATE_STARTED ||
     state!=UI_STATE_SET_LANGUAGE || pressed || !direction) return;
  IConfigurationManager *config=MenuEditSession_Config((IConfigurationManager *)ServiceLocator_GetService(SVC_NAME_CONFIGURATION_MANAGER));
  SystemSettings snapshot;
  if(!config || !config->GetSnapshot || !config->UpdateFields ||
     config->GetSnapshot(config,&snapshot)!=ESP_OK) return;
  size_t count=Localization_Count();
  if(count<2) return;
  const LocalizationCatalog *current=Localization_Find(snapshot.localization.locale);
  if(!current) current=Localization_Snapshot();
  size_t index=0;
  while(index<count && Localization_At(index)!=current) ++index;
  if(index==count) return;
  int next=((int)index+direction)%(int)count;
  if(next<0) next+=(int)count;
  const LocalizationCatalog *selected=Localization_At((size_t)next);
  char code[sizeof(snapshot.localization.locale)]={0};
  size_t length=strlen(selected->code);
  if(length>=sizeof(code)) return;
  memcpy(code,selected->code,length);
  ConfigurationFieldUpdate field={offsetof(LocalizationConfigs,locale),code,sizeof(code)};
  esp_err_t result=config->UpdateFields(config,"localization",&field,1,true);
  if(result!=ESP_OK) ESP_LOGE(TAG,"Language save failed: %d",result);
}

static const MenuPluginRegistration systemMenuEntries[] = {
  {UI_STATE_SET_DISPLAY_MODE, "Display mode", NULL, DisplayMode_ConfigHandler},
  {UI_STATE_SET_MENU_AUTO_EXIT, "Menu auto exit", NULL, MenuAutoExit_ConfigHandler},
  {UI_STATE_SET_TIMEOUT, "Ekran Uyku", NULL, DisplayTimeout_ConfigHandler},
  {UI_STATE_SET_LANGUAGE, "Language", NULL, Language_ConfigHandler},
  {UI_STATE_SET_POWER_PROFILE, "Guc Profili", NULL, PowerProfile_ConfigHandler},
  {UI_STATE_SET_ZIGBEE_TX_PROFILE, "Zigbee Gucu", NULL, TxProfile_ConfigHandler},
  {UI_STATE_RESTART, "Restart", NULL, System_MenuHandler},
  {UI_STATE_SHUTDOWN, "Shutdown", NULL, System_MenuHandler},
  {UI_STATE_FACTORY_RESET, "Fabrika Sifir", NULL, System_MenuHandler},
  {UI_STATE_SET_TELEMETRY, "Telemetri Ayar", NULL, Telemetry_ConfigHandler},
  {UI_STATE_ZB_REP_FAN, "ZB Fan", NULL, ZigbeeRep_ConfigHandler},
  {UI_STATE_ZB_REP_AMBIENT, "ZB Amb", NULL, ZigbeeRep_ConfigHandler},
  {UI_STATE_ZB_REP_CPU, "ZB CPU", NULL, ZigbeeRep_ConfigHandler},
  {UI_STATE_ZB_REP_HEALTH, "ZB Hea", NULL, ZigbeeRep_ConfigHandler},
  {UI_STATE_ZB_REP_MEMORY, "ZB Mem", NULL, ZigbeeRep_ConfigHandler},
  {UI_STATE_ZB_REP_UPTIME, "ZB Upt", NULL, ZigbeeRep_ConfigHandler},
};

/* Called with controlMutex held. Failure retains all remaining ownership for retry. */
static int32_t SC_StopOwned(ServiceState finalState) {
  controllerInstance.serviceState = SERVICE_STATE_ERROR;
  if (WatchdogWorker_Stop(&controllerInstance.worker,
      pdMS_TO_TICKS(SYSTEM_CONTROLLER_CONTROL_TIMEOUT_MS)) != 0) return -1;
  if (controllerInstance.menuRegistered) {
    IMenuManager *menu = controllerInstance.menuManager;
    if (!menu || !menu->UnregisterPluginBatch || menu->UnregisterPluginBatch(menu,
        systemMenuEntries, sizeof(systemMenuEntries) / sizeof(systemMenuEntries[0])) != MENU_OK) return -1;
    controllerInstance.menuRegistered = false;
  }
  if (controllerInstance.telemetryOwned) {
    IService *telemetry = &controllerInstance.telemetry->base;
    if (!telemetry->Stop || telemetry->Stop(telemetry) != 0) return -1;
    controllerInstance.telemetryOwned = false;
  }
  SystemEvent discarded;
  while (xQueueReceive(controllerInstance.eventQueue, &discarded, 0) == pdPASS) {}
  portENTER_CRITICAL(&systemStateLock);
  controllerInstance.recoveryPending = false;
  portEXIT_CRITICAL(&systemStateLock);
  controllerInstance.shutdownScheduled = false;
  atomic_store(&controllerInstance.shutdownPending, false);
  controllerInstance.serviceState = finalState;
  return 0;
}

static int32_t SC_Start(IService *self) {
  if (self != &controllerInstance.base.base || !controllerInstance.controlMutex ||
      xSemaphoreTake(controllerInstance.controlMutex, pdMS_TO_TICKS(SYSTEM_CONTROLLER_CONTROL_TIMEOUT_MS)) != pdTRUE) return -1;
  ServiceState previous = controllerInstance.serviceState;
  int32_t result = -1;
  if (previous == SERVICE_STATE_STARTED) {
    result = WatchdogWorker_Healthy(&controllerInstance.worker) ? 0 : -1;
    goto done;
  }
  if (previous != SERVICE_STATE_INITIALIZED && previous != SERVICE_STATE_STOPPED) goto done;
  uint32_t interval = DEFAULT_TELEMETRY_INTERVAL_MS;
  IConfigurationManager *config = (IConfigurationManager *)ServiceLocator_GetService(SVC_NAME_CONFIGURATION_MANAGER);
  if (config) {
    SystemSettings snapshot;
    if (!config->GetSnapshot || config->GetSnapshot(config, &snapshot) != ESP_OK) goto done;
    interval = snapshot.system.telemetry_interval_ms;
  }
  if (controllerInstance.menuManager) {
    IMenuManager *menu = controllerInstance.menuManager;
    if (!menu->RegisterPluginBatch || !menu->UnregisterPluginBatch ||
        menu->RegisterPluginBatch(menu, systemMenuEntries, sizeof(systemMenuEntries) / sizeof(systemMenuEntries[0])) != MENU_OK) goto done;
    controllerInstance.menuRegistered = true;
  }
  if (controllerInstance.telemetry) {
    ISystemTelemetry *telemetry = controllerInstance.telemetry;
    controllerInstance.telemetryOwned = true;
    telemetry->SetReportingInterval(telemetry, interval);
    if (!telemetry->base.Start || telemetry->base.Start(&telemetry->base) != 0) goto rollback;
  }
  controllerInstance.serviceState = SERVICE_STATE_STARTED;
  result = WatchdogWorker_Start(&controllerInstance.worker, SC_EventLoop, "SC_EventLoop",
      SYSTEM_CONTROLLER_TASK_STACK_SIZE, NULL, SYSTEM_CONTROLLER_TASK_PRIORITY);
  if (result != 0) goto rollback;
  SC_UpdateStatusLED(controllerInstance.systemState);
  goto done;
rollback:
  result = -1;
  (void)SC_StopOwned(previous);
done:
  xSemaphoreGive(controllerInstance.controlMutex);
  return result;
}

static int32_t SC_Stop(IService *self) {
  if (self != &controllerInstance.base.base || !controllerInstance.controlMutex) return -1;
  TaskHandle_t task = atomic_load(&controllerInstance.worker.task);
  if (task && xTaskGetCurrentTaskHandle() == task) return -1;
  if (xSemaphoreTake(controllerInstance.controlMutex, pdMS_TO_TICKS(SYSTEM_CONTROLLER_CONTROL_TIMEOUT_MS)) != pdTRUE) return -1;
  int32_t result = SC_StopOwned(SERVICE_STATE_STOPPED);
  xSemaphoreGive(controllerInstance.controlMutex);
  return result;
}

static ServiceState SC_GetState(IService *self) {
  return self == &controllerInstance.base.base ? atomic_load(&controllerInstance.serviceState) : SERVICE_STATE_ERROR;
}

static int32_t SC_CheckHealth(IService *self) {
  if (self != &controllerInstance.base.base || controllerInstance.serviceState != SERVICE_STATE_STARTED ||
      !WatchdogWorker_Healthy(&controllerInstance.worker)) return -1;
  if (controllerInstance.telemetryOwned && controllerInstance.telemetry->base.CheckHealth)
    return controllerInstance.telemetry->base.CheckHealth(&controllerInstance.telemetry->base);
  return 0;
}

static void SC_ApplyState(SystemState state) {
  if ((unsigned)state > SYSTEM_STATE_ERROR) return;
  uint64_t nowUs = (uint64_t)esp_timer_get_time();
  portENTER_CRITICAL(&systemStateLock);
  SystemState previous = controllerInstance.systemState;
  if (previous == state) { portEXIT_CRITICAL(&systemStateLock); return; }
  SystemState visible = state;
  if (state == SYSTEM_STATE_ERROR) {
    if (previous != SYSTEM_STATE_RECOVERING) {
      controllerInstance.recoveryStartedUs = nowUs;
      controllerInstance.recoveryLastAttemptUs = 0;
      controllerInstance.recoveryAttempts = 0;
    }
    if (previous == SYSTEM_STATE_RECOVERING && !controllerInstance.recoveryPending) {
      portEXIT_CRITICAL(&systemStateLock);
      return; /* Repeated errors cannot replenish an exhausted budget. */
    }
    controllerInstance.recoveryPending = true;
    visible = SYSTEM_STATE_RECOVERING;
  } else controllerInstance.recoveryPending = false;
  controllerInstance.systemState = visible;
  portEXIT_CRITICAL(&systemStateLock);
  ESP_LOGI(TAG, "System state transition: %d -> %d", previous, visible);
  SC_UpdateStatusLED(visible);
}

static void SC_SetSystemState(ISystemController *self, SystemState state) {
  if (self != &controllerInstance.base || (unsigned)state > SYSTEM_STATE_ERROR || !controllerInstance.controlMutex ||
      xSemaphoreTake(controllerInstance.controlMutex, pdMS_TO_TICKS(SYSTEM_CONTROLLER_CONTROL_TIMEOUT_MS)) != pdTRUE) return;
  if (controllerInstance.serviceState == SERVICE_STATE_STARTED) SC_ApplyState(state);
  xSemaphoreGive(controllerInstance.controlMutex);
}

static SystemState SC_GetSystemState(ISystemController *self) {
  return self == &controllerInstance.base ? atomic_load(&controllerInstance.systemState) : SYSTEM_STATE_ERROR;
}

static void SC_HandleSystemEvent(ISystemController *self, uint32_t eventId) {
  if (self != &controllerInstance.base || !controllerInstance.controlMutex ||
      xSemaphoreTake(controllerInstance.controlMutex, 0) != pdTRUE) return;
  if (controllerInstance.serviceState == SERVICE_STATE_STARTED) {
    SystemEvent event = {.eventId = eventId, .payload = 0};
    if (xQueueSend(controllerInstance.eventQueue, &event, 0) != pdPASS)
      ESP_LOGW(TAG, "System event queue rejected %lu", (unsigned long)eventId);
  }
  xSemaphoreGive(controllerInstance.controlMutex);
}

static int32_t SC_RequestShutdown(ISystemController *self, uint32_t delay_ms) {
  if (self != &controllerInstance.base || delay_ms > SYSTEM_CONTROLLER_SHUTDOWN_MAX_DELAY_MS) return ESP_ERR_INVALID_ARG;
  if (!controllerInstance.controlMutex || xSemaphoreTake(controllerInstance.controlMutex, 0) != pdTRUE)
    return ESP_ERR_INVALID_STATE;
  int32_t result = ESP_ERR_INVALID_STATE;
  if (controllerInstance.serviceState != SERVICE_STATE_STARTED || !controllerInstance.eventQueue) goto done;
  if (atomic_load(&controllerInstance.shutdownPending)) goto done;
  IPowerManager *power = (IPowerManager *)ServiceLocator_GetService(SVC_NAME_POWER_MANAGER);
  if (!power || !power->EnterDeepSleep) goto done;
  SystemEvent event = {.payload = delay_ms, .shutdown = true};
  atomic_store(&controllerInstance.shutdownPending, true);
  if (xQueueSend(controllerInstance.eventQueue, &event, 0) != pdPASS) {
    atomic_store(&controllerInstance.shutdownPending, false);
    result = ESP_ERR_NO_MEM;
    goto done;
  }
  result = ESP_OK;
done:
  xSemaphoreGive(controllerInstance.controlMutex);
  return result;
}

static int32_t SC_RequestZigbeeTxProfile(ISystemController *self, uint8_t profile) {
  if (self != &controllerInstance.base || profile >= ZIGBEE_TX_PROFILE_COUNT) return ESP_ERR_INVALID_ARG;
  if (!controllerInstance.controlMutex || xSemaphoreTake(controllerInstance.controlMutex, 0) != pdTRUE)
    return ESP_ERR_INVALID_STATE;
  esp_err_t result = ESP_ERR_INVALID_STATE;
  if (controllerInstance.serviceState == SERVICE_STATE_STARTED && controllerInstance.eventQueue &&
      !atomic_load(&controllerInstance.shutdownPending)) {
    SystemEvent event = {.payload=profile, .zigbeeTxProfile=true};
    result = xQueueSend(controllerInstance.eventQueue, &event, 0) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
  }
  xSemaphoreGive(controllerInstance.controlMutex);
  return result;
}

static int32_t SC_Initialize(IService *self, IDIRegistry *registry) {
  if (self != &controllerInstance.base.base) return -1;
  if (controllerInstance.serviceState != SERVICE_STATE_UNINITIALIZED) return 0;
  SemaphoreHandle_t control = xSemaphoreCreateMutex();
  if (!control) return -1;
  QueueHandle_t queue = xQueueCreate(SYSTEM_CONTROLLER_EVENT_QUEUE_SIZE, sizeof(SystemEvent));
  if (!queue || WatchdogWorker_Init(&controllerInstance.worker) != 0) {
    if (queue) vQueueDelete(queue);
    vSemaphoreDelete(control);
    return -1;
  }
  controllerInstance.config = SystemController_GetDefaultConfig();
  if (registry && registry->Resolve) {
    controllerInstance.rgbLed = (IRGBLED *)registry->Resolve(registry, SVC_NAME_RGB_LED);
    controllerInstance.builtinLed = (IBuiltinLED *)registry->Resolve(registry, SVC_NAME_BUILTIN_LED);
    controllerInstance.menuManager = (IMenuManager *)registry->Resolve(registry, SVC_NAME_MENU_MANAGER);
  }
  controllerInstance.telemetry = GetSystemTelemetry();
  if (controllerInstance.telemetry && (!controllerInstance.telemetry->base.Initialize ||
      controllerInstance.telemetry->base.Initialize(&controllerInstance.telemetry->base, registry) != 0)) {
    /* No task or public service exists yet; these private allocations can be released. */
    vQueueDelete(controllerInstance.worker.stopped);
    vSemaphoreDelete(controllerInstance.worker.gate);
    memset(&controllerInstance.worker, 0, sizeof(controllerInstance.worker));
    vQueueDelete(queue);
    vSemaphoreDelete(control);
    return -1;
  }
  controllerInstance.controlMutex = control;
  controllerInstance.eventQueue = queue;
  controllerInstance.systemState = SYSTEM_STATE_INITIALIZING;
  controllerInstance.serviceState = SERVICE_STATE_INITIALIZED;
  return 0;
}

ISystemController *GetSystemController(void) {
  static bool initialized = false;
  if (!initialized) {
    controllerInstance.base.base.Initialize = SC_Initialize;
    controllerInstance.base.base.Start = SC_Start;
    controllerInstance.base.base.Stop = SC_Stop;
    controllerInstance.base.base.GetState = SC_GetState;
    controllerInstance.base.base.CheckHealth = SC_CheckHealth;
    controllerInstance.base.SetSystemState = SC_SetSystemState;
    controllerInstance.base.GetSystemState = SC_GetSystemState;
    controllerInstance.base.HandleSystemEvent = SC_HandleSystemEvent;
    controllerInstance.base.RequestShutdown = SC_RequestShutdown;
    controllerInstance.base.RequestZigbeeTxProfile = SC_RequestZigbeeTxProfile;
    controllerInstance.serviceState = SERVICE_STATE_UNINITIALIZED;
    initialized = true;
  }
  return &controllerInstance.base;
}
