#include "Controllers/MenuManager/MenuEditSession.h"
#include "Controllers/MenuManager/MenuRangeEditor.h"
#include "Controllers/MenuManager/MenuManagerEvents.h"
#include "Communication/Zigbee/Endpoints/FanSourceAttributes.h"
/**
 * @file ClimateController.c
 * @brief ClimateController service implementation for the RaCool ESP32-C6 device.
 *
 * The ClimateController is the central orchestrator for temperature monitoring
 * and fan (relay) control. It provides:
 *
 *  - Thread-safe, cached access to object, ambient, and CPU temperature readings.
 *  - Event-driven sensor updates via EventBus subscriptions
 *    (EVENT_IR_TEMP_UPDATED, EVENT_INTERNAL_TEMP_UPDATED, EVENT_SYSTEM_THERMAL_ALERT).
 *  - Object-sensitive and ambient-delta fan control with validated setpoints,
 *    hysteresis dead-band, debounce protection, and minimum switch-time enforcement.
 *  - CPU thermal protection: forces fan ON when CPU temperature is critical.
 *  - Manual fan-override mode settable from the OLED menu or Zigbee/HA.
 *  - Sensor stale detection with EventBus notification.
 *  - Temperature history ring buffer (last N object/ambient/CPU triplets).
 *  - OLED menu plugins for temperature setpoints, fan mode, and calibration.
 *  - Debug console commands for runtime inspection and control.
 *  - FreeRTOS task watchdog integration with proper cleanup on Stop().
 *  - Full SOLID compliance: SRP (relay logic separated into helper),
 *    OCP (plugin-based menu extension), DIP (service dependencies via
 *    ServiceLocator with self-healing lazy resolution).
 *
 * Bug Fixes vs. previous version:
 *  - objectTemp is now read under spinlock inside Climate_EvaluateRelay().
 *  - Hysteresis dead-band applied correctly per relay direction.
 *  - Stop() unsubscribes from EventBus and removes task from watchdog.
 *  - Watchdog registration race condition eliminated.
 *  - watchdogTimeoutMs sourced from config (no hardcoded value).
 *  - configManager is resolved lazily like other dependencies.
 *
 * @note All public temperature getters are protected by a FreeRTOS spinlock
 *       and are safe to call from any task or ISR context.
 */

#include "Controllers/ClimateController/ClimateControllerConfig.h"
#include "Controllers/ClimateController/IClimateController.h"
#include "Controllers/ClimateController/FanCommands.h"
#include "Core/EventBus/IEventBus.h"
#include "Core/ServiceLocator/ServiceLocator.h"
#include "Core/ServiceLocator/ServiceNames.h"
#include "Hardware/InfraredTemperatureSensor/IInfraredTemperatureSensor.h"
#include "Hardware/InternalTemperatureSensor/IInternalTemperatureSensor.h"
#include "Hardware/Relay/IRelay.h"
#include "Hardware/Relay/RelayConfig.h"
#include "Management/ConfigurationManager/IConfigurationManager.h"
#include "Management/ConfigurationManager/SystemConfigs.h"
#include "Management/Watchdog/IWatchdog.h"
#include "Management/Watchdog/WatchdogWorker.h"
#include "Controllers/MenuManager/IMenuManager.h"
#include "Communication/DebugConsole/IDebugConsole.h"
#include "Controllers/ClimateController/ClimateConsoleCommands.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <stdint.h>
#include <string.h>
#include <math.h>

static const char *TAG = "ClimateController";

/* ═══════════════════════════════════════════════════════════════════════════
 * Temperature History Ring Buffer
 * Stores the last N (object, ambient, cpu) temperature triplets with
 * associated tick timestamps for diagnostic and trend analysis.
 * ═══════════════════════════════════════════════════════════════════════════ */

/** @brief Maximum ring buffer capacity. Actual size is config.historyBufferSize. */
#define CLIMATE_HISTORY_MAX CLIMATE_HISTORY_BUFFER_SIZE

/**
 * @brief Ring buffer for temperature history.
 */
typedef struct {
  ClimateTempHistoryEntry entries[CLIMATE_HISTORY_MAX]; /**< Circular storage. */
  uint8_t                 head;                         /**< Next write position. */
  uint8_t                 count;                        /**< Number of valid entries. */
  uint8_t                 capacity;                     /**< Active capacity (≤ CLIMATE_HISTORY_MAX). */
} TempHistory;

/* ═══════════════════════════════════════════════════════════════════════════
 * Relay State Machine Context
 * Encapsulates all mutable state of the relay debounce / min-switch logic so
 * that it lives in the instance struct (not static locals), making it safe
 * for re-entrance and unit testing.
 * ═══════════════════════════════════════════════════════════════════════════ */

/**
 * @brief Mutable state for the relay debounce and min-switch-time logic.
 *
 * Moved from static local variables into the instance struct to eliminate
 * re-entry hazards and to enable unit-testing without function-scoped statics.
 */
typedef struct {
  uint32_t relayStateChangeTime;  /**< Tick-ms when the relay last changed. */
  uint32_t conditionMetStartTime; /**< Tick-ms when the pending action was first seen. */
  bool     pendingActionOpen;     /**< Direction of the pending relay action. */
  bool     isConditionPending;    /**< Whether a debounce countdown is in progress. */
} RelayStateMachine;

/* ═══════════════════════════════════════════════════════════════════════════
 * Implementation Struct
 * ═══════════════════════════════════════════════════════════════════════════ */

typedef struct {
  IClimateController base;
  _Atomic(ServiceState) state;
  WatchdogWorker worker;
  SemaphoreHandle_t lifecycleMutex;
  IMenuManager *ownedMenu;
  IEventBus *ownedBus;
  uint8_t subscriptions;
  TaskHandle_t       taskHandle;

  /* Thread-safe temperature cache — protected by spinlock. */
  portMUX_TYPE spinlock;
  float        objectTemp;
  float        ambientTemp;
  float        cpuTemp;
  ExternalTemperatureSlot externalSlots[3];
  FanSourceDiagnostics lastSourceDiagnostics;
  bool hasSourceDiagnostics;
  uint32_t lastSourcePublicationMs;
  FanActionStatus fanAction;
  uint64_t fanActionDeadlineUs;
  uint8_t fanActionPhase;
  uint32_t     lastSensorReadTick; /**< xTaskGetTickCount() of last successful IR read. */
  int64_t lastSensorSampleUs;
  bool hasSensorSample;
  bool sensorContinuityLost; /**< Measurement gap consumed by the relay worker. */

  /* CPU thermal protection: set true when CPU triggers forced fan-on. */
  atomic_bool cpuThermalOverride;

  /* Sensor stale tracking. */
  bool sensorStaleNotified; /**< True after EVENT_CLIMATE_SENSOR_STALE was published. */

  /* Temperature history ring buffer (spinlock-protected). */
  TempHistory history;

  /* Service dependencies (resolved lazily with self-healing). */
  IConfigurationManager          *configManager;
  IInfraredTemperatureSensor     *tempSensor;
  IInternalTemperatureSensor     *internalTemp;
  IRelay                         *relay;
  IWatchdog                      *watchdog;

  /* Relay state machine context. */
  RelayStateMachine rsm;

  ControlConfigs lastDecisionConfig;
  bool hasDecisionConfig;
  uint8_t lastEffectiveSources;

  /* Module configuration. */
  ClimateControllerConfig config;
  bool                    is_config_set;

  /* Vtable lifecycle — reset to false by memset in tests. */
  bool vtable_initialized;
} ClimateControllerImpl;

static ClimateControllerImpl climateControllerInstance;

/* ═══════════════════════════════════════════════════════════════════════════
 * Thread-safe temperature cache helpers
 * ═══════════════════════════════════════════════════════════════════════════ */

/**
 * @brief Atomically updates the cached IR temperature readings and appends
 *        a history entry.
 *
 * Called from the EventBus callback or the polling task. The spinlock ensures
 * that readers (GetCurrentObjectTemperature etc.) never observe a partial write.
 *
 * @param sample Complete valid measurement with its original acquisition time.
 */
static void Climate_UpdateIRSample(const IRTempSample *sample) {
  float obj = sample->temperatures[0], amb = sample->temperatures[1];
  uint32_t tick = sample->measured_tick;
  if (!isfinite(obj) || !isfinite(amb) || sample->measured_time_us < 0) return;
  portENTER_CRITICAL(&climateControllerInstance.spinlock);
  if (climateControllerInstance.hasSensorSample &&
      sample->measured_time_us <= climateControllerInstance.lastSensorSampleUs) {
    portEXIT_CRITICAL(&climateControllerInstance.spinlock);
    return;
  }
  if (climateControllerInstance.hasSensorSample &&
      climateControllerInstance.config.sensorStaleTimeoutMs &&
      (uint64_t)(sample->measured_time_us - climateControllerInstance.lastSensorSampleUs) >=
          (uint64_t)climateControllerInstance.config.sensorStaleTimeoutMs * 1000U) {
    climateControllerInstance.sensorContinuityLost = true;
  }
  climateControllerInstance.hasSensorSample = true;
  climateControllerInstance.lastSensorSampleUs = sample->measured_time_us;
  climateControllerInstance.objectTemp         = obj;
  climateControllerInstance.ambientTemp        = amb;
  climateControllerInstance.lastSensorReadTick = tick;
  climateControllerInstance.sensorStaleNotified = false; /* Reset stale flag on fresh data. */

  /* Append to history ring buffer. */
  TempHistory *hist = &climateControllerInstance.history;
  if (hist->capacity > 0) {
    hist->entries[hist->head].objectTemp  = obj;
    hist->entries[hist->head].ambientTemp = amb;
    hist->entries[hist->head].cpuTemp     = climateControllerInstance.cpuTemp;
    hist->entries[hist->head].tickMs      = (uint32_t)(tick * portTICK_PERIOD_MS);
    hist->head = (uint8_t)((hist->head + 1) % hist->capacity);
    if (hist->count < hist->capacity) {
      hist->count++;
    }
  }
  portEXIT_CRITICAL(&climateControllerInstance.spinlock);
}

/**
 * @brief Atomically updates the cached CPU temperature reading.
 *
 * @param cpu  New CPU temperature in °C.
 */
static void Climate_UpdateCpuTemp(float cpu) {
  portENTER_CRITICAL(&climateControllerInstance.spinlock);
  climateControllerInstance.cpuTemp = cpu;
  portEXIT_CRITICAL(&climateControllerInstance.spinlock);
}


/* ═══════════════════════════════════════════════════════════════════════════
 * EventBus Callbacks (event-driven sensor update path)
 * ═══════════════════════════════════════════════════════════════════════════ */

/**
 * @brief EventBus handler for EVENT_IR_TEMP_UPDATED.
 *
 * Triggered by InfraredTemperatureSensor after each successful I2C read.
 * Updates the object and ambient temperature cache atomically.
 *
 * @param event BusEvent containing an IRTempSample.
 */
static void Climate_OnIRTempUpdated(BusEvent event) {
  if (climateControllerInstance.state != SERVICE_STATE_STARTED) return;
  if (!event.data || event.dataSize < sizeof(IRTempSample)) return;
  IRTempSample sample;
  memcpy(&sample, event.data, sizeof(sample));
  Climate_UpdateIRSample(&sample);
}

static void Climate_PollIRSample(void) {
  IInfraredTemperatureSensor *sensor = climateControllerInstance.tempSensor;
  IRTempSample sample;
  if (sensor && sensor->ReadSample && sensor->ReadSample(sensor, &sample) == ESP_OK)
    Climate_UpdateIRSample(&sample);
}

/**
 * @brief EventBus handler for EVENT_INTERNAL_TEMP_UPDATED.
 *
 * Triggered by InternalTemperatureSensor after each polling cycle.
 *
 * @param event BusEvent whose data points to a single float (CPU °C).
 */
static void Climate_OnInternalTempUpdated(BusEvent event) {
  if (climateControllerInstance.state != SERVICE_STATE_STARTED) return;
  if (!event.data || event.dataSize < sizeof(float)) return;
  float cpu = *(const float *)event.data;
  Climate_UpdateCpuTemp(cpu);
}

/**
 * @brief EventBus handler for EVENT_SYSTEM_THERMAL_ALERT.
 *
 * Triggered by the thermal monitoring subsystem when the CPU temperature
 * reaches a critical level. Forces the relay ON (fan ON) to provide emergency
 * cooling, regardless of fan mode or temperature setpoints.
 *
 * The override is cleared when the CPU temperature drops below the warning
 * threshold (handled in the main task loop).
 *
 * Only active when config.cpuTempProtectionEnabled is true.
 *
 * @param event BusEvent (data unused — alert presence is the signal).
 */
static void Climate_OnThermalAlert(BusEvent event) {
  if (climateControllerInstance.state != SERVICE_STATE_STARTED) return;
  (void)event;
  if (!climateControllerInstance.config.cpuTempProtectionEnabled) return;

  if (!climateControllerInstance.cpuThermalOverride) {
    climateControllerInstance.cpuThermalOverride = true;
    ESP_LOGW(TAG, "CPU thermal alert received — forcing fan ON for emergency cooling");

    /* Only the control worker actuates the relay. */
  }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Sensor Stale Detection
 * ═══════════════════════════════════════════════════════════════════════════ */

/**
 * @brief Checks whether the IR sensor reading is stale and publishes an event
 *        the first time staleness is detected.
 *
 * Called once per main loop iteration. After the first notification the flag
 * is set and subsequent calls are no-ops until fresh data resets it.
 *
 * @param nowMs Current time in milliseconds.
 */
static void Climate_CheckSensorStale(uint32_t nowMs) {
  if (climateControllerInstance.config.sensorStaleTimeoutMs == 0) return;

  uint32_t lastTick;
  portENTER_CRITICAL(&climateControllerInstance.spinlock);
  lastTick = climateControllerInstance.lastSensorReadTick;
  bool hasSample = climateControllerInstance.hasSensorSample;
  bool alreadyNotified = climateControllerInstance.sensorStaleNotified;
  portEXIT_CRITICAL(&climateControllerInstance.spinlock);

  if (!hasSample) return; /* No completed measurement yet. */
  if (alreadyNotified) return;

  uint32_t lastTickMs = (uint32_t)(lastTick * portTICK_PERIOD_MS);
  if ((nowMs - lastTickMs) < climateControllerInstance.config.sensorStaleTimeoutMs) return;

  /* Mark as notified and publish event. */
  portENTER_CRITICAL(&climateControllerInstance.spinlock);
  climateControllerInstance.sensorStaleNotified = true;
  portEXIT_CRITICAL(&climateControllerInstance.spinlock);

  ESP_LOGW(TAG, "IR sensor stale — no update for %lu ms",
           (unsigned long)(nowMs - lastTickMs));

  IEventBus *eb = ServiceLocator_GetEventBus();
  if (eb) {
    BusEvent ev = {
        .eventName = EVENT_CLIMATE_SENSOR_STALE,
        .type      = EVENT_TYPE_WARNING,
        .data      = NULL,
        .dataSize  = 0,
    };
    eb->Publish(ev);
  }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * CPU Thermal Override Clearance
 * ═══════════════════════════════════════════════════════════════════════════ */

/**
 * @brief Clears the CPU thermal override flag once CPU temperature is safe.
 *
 * The override is released when the CPU temperature drops below
 * cpu_temp_warning (from ThermalConfigs), which provides hysteresis between
 * the emergency trigger and the recovery.
 *
 * @param settings Pointer to current SystemSettings.
 */
static void Climate_CheckCpuThermalOverride(SystemSettings *settings) {
  if (!climateControllerInstance.cpuThermalOverride) return;
  if (!climateControllerInstance.config.cpuTempProtectionEnabled) {
    climateControllerInstance.cpuThermalOverride = false;
    return;
  }

  float cpu;
  portENTER_CRITICAL(&climateControllerInstance.spinlock);
  cpu = climateControllerInstance.cpuTemp;
  portEXIT_CRITICAL(&climateControllerInstance.spinlock);

  if (cpu < settings->thermal.cpu_temp_warning) {
    climateControllerInstance.cpuThermalOverride = false;
    ESP_LOGI(TAG, "CPU temperature recovered (%.1f°C < %.1f°C) — releasing thermal override",
             cpu, settings->thermal.cpu_temp_warning);
  }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Menu Plugin Handler (new MenuPluginHandlerEx signature)
 * ═══════════════════════════════════════════════════════════════════════════ */

/**
 * @brief OLED menu plugin callback for all ClimateController menu states.
 *
 * Handles encoder rotation events for temperature setpoint and calibration
 * adjustments, and encoder rotation for fan mode selection.
 * Settings changes are persisted by publishing EVENT_CONFIG_CHANGED so that
 * all subscribers (ConfigurationManager, ZigbeePlugin, etc.) are notified.
 *
 * Registered via IMenuManager::RegisterPluginEx for:
 *   UI_STATE_SET_LOW_TEMP, UI_STATE_SET_HIGH_TEMP,
 *   UI_STATE_FAN_MODE,
 *   UI_STATE_CALIBRATE_OBJECT, UI_STATE_CALIBRATE_AMBIENT
 *
 * @param event Pointer to the MenuPluginEvent describing the interaction.
 */
static void Climate_MenuHandlerEx(const MenuPluginEvent *event) {
  if (climateControllerInstance.state != SERVICE_STATE_STARTED) return;
  if (!event) return;

  if (event->state==UI_STATE_SET_LOW_TEMP || event->state==UI_STATE_FAN_DELTA_OFF) {
    IConfigurationManager *owner=MenuEditSession_Config(ConfigurationManager_GetInstance());
    IMenuManager *menu=climateControllerInstance.ownedMenu;
    SystemSettings current;
    if (!owner || !owner->GetSnapshot || !owner->UpdateFields || owner->GetSnapshot(owner,&current)!=ESP_OK) return;
    if (event->action==MENU_PLUGIN_ACTION_ENCODER) {
      if (!event->direction) MenuRangeEditor_Begin(event->state,&current);
      else MenuRangeEditor_Rotate(event->state,event->direction);
    } else if (event->action==MENU_PLUGIN_ACTION_BUTTON && MenuRangeEditor_Advance(event->state)) {
      int16_t low,high; unsigned phase; float oldLow,oldHigh; size_t a,b;
      if (!MenuRangeEditor_Read(event->state,&low,&high,&phase) ||
          !MenuRangeEditor_Values(event->state,&current,&oldLow,&oldHigh,&a,&b)) return;
      if (!MenuRangeEditor_Unchanged(event->state,&current)) {
        MenuRangeEditor_Failed(event->state);
        ESP_LOGE(TAG,"Fan range changed externally; refusing stale draft");
      } else {
        float lower=low/10.0f,upper=high/10.0f;
        bool delta=event->state==UI_STATE_FAN_DELTA_OFF;
        ConfigurationFieldUpdate fields[]={{a,delta ? (const void *)&low : (const void *)&lower,delta ? sizeof(low) : sizeof(lower)},
          {b,delta ? (const void *)&high : (const void *)&upper,delta ? sizeof(high) : sizeof(upper)}};
        esp_err_t result=owner->UpdateFields(owner,"control",fields,2,true);
        if (result!=ESP_OK) {
          MenuRangeEditor_Failed(event->state); ESP_LOGE(TAG,"Fan range save failed: 0x%x",(unsigned)result);
        } else {
          MenuRangeEditor_Clear();
          if (menu && menu->SetEditing) menu->SetEditing(menu,false);
          IEventBus *bus=ServiceLocator_GetEventBus();
          if (bus && bus->Publish) bus->Publish((BusEvent){.eventName=EVENT_MENU_EDITING_FINISHED});
        }
      }
    }
    if (menu && menu->UpdateActivity) menu->UpdateActivity(menu);
    return;
  }

  /* Only react to encoder rotation events in this handler. */
  if (event->action != MENU_PLUGIN_ACTION_ENCODER) return;
  if (event->direction == 0) return;

  IConfigurationManager *config = MenuEditSession_Config(ConfigurationManager_GetInstance());
  SystemSettings snapshot;
  if (!config || !config->GetSnapshot || !config->UpdateFields ||
      config->GetSnapshot(config, &snapshot) != ESP_OK) return;
  SystemSettings *settings = &snapshot;

  const int8_t dir = event->direction;

  FanCommand command = {0};
  bool fanCommand = true;
  switch (event->state) {
  case UI_STATE_SET_HIGH_TEMP: command = (FanCommand){FAN_COMMAND_OBJECT_ON, settings->control.relay_open_temp + dir * 0.5f, 0}; break;
  case UI_STATE_SET_LOW_TEMP: command = (FanCommand){FAN_COMMAND_OBJECT_OFF, settings->control.relay_close_temp + dir * 0.5f, 0}; break;
  case UI_STATE_FAN_DELTA_ON: command = (FanCommand){FAN_COMMAND_DELTA_ON, (settings->control.fan_delta_on_decic + dir) / 10.0f, 0}; break;
  case UI_STATE_FAN_DELTA_OFF: command = (FanCommand){FAN_COMMAND_DELTA_OFF, (settings->control.fan_delta_off_decic + dir) / 10.0f, 0}; break;
  case UI_STATE_OBJECT_SOURCE: command = (FanCommand){FAN_COMMAND_OBJECT_SOURCE, (settings->control.object_temperature_source + dir + 4) % 4, 0}; break;
  case UI_STATE_AMBIENT_SOURCE: command = (FanCommand){FAN_COMMAND_AMBIENT_SOURCE, (settings->control.ambient_temperature_source + dir + 4) % 4, 0}; break;
  case UI_STATE_FAN_MODE: {
    /* Display order keeps both sensing strategies together. */
    int index = settings->control.fan_manual_mode ? (settings->control.fan_manual_state ? 2 : 3) : settings->control.fan_strategy;
    const uint8_t modes[] = {CLIMATE_FAN_OBJECT_SENSITIVE, CLIMATE_FAN_AMBIENT_DELTA, CLIMATE_FAN_MANUAL_ON, CLIMATE_FAN_MANUAL_OFF};
    command = (FanCommand){FAN_COMMAND_MODE, modes[(index + dir + 4) % 4], 0}; break;
  }
  default: fanCommand = false; break;
  }
  if (fanCommand) {
    esp_err_t result = FanCommands_Dispatch(config, command, true);
    if (result != ESP_OK) ESP_LOGW(TAG, "Fan setting rejected (bounds or ON > OFF required): %d", result);
    return;
  }

  switch (event->state) {
    case UI_STATE_SET_TEMP_UNIT:
      settings->system.temp_unit = settings->system.temp_unit == 0 ? 1 : 0;
      break;
    case UI_STATE_CALIBRATE_OBJECT:
      settings->calibration.ir_object_cal += (float)dir * 0.1f;
      if (settings->calibration.ir_object_cal < CALIBRATION_MIN)
          settings->calibration.ir_object_cal = CALIBRATION_MIN;
      if (settings->calibration.ir_object_cal > CALIBRATION_MAX)
          settings->calibration.ir_object_cal = CALIBRATION_MAX;
      break;

    case UI_STATE_CALIBRATE_AMBIENT:
      settings->calibration.ir_ambient_cal += (float)dir * 0.1f;
      if (settings->calibration.ir_ambient_cal < CALIBRATION_MIN)
          settings->calibration.ir_ambient_cal = CALIBRATION_MIN;
      if (settings->calibration.ir_ambient_cal > CALIBRATION_MAX)
          settings->calibration.ir_ambient_cal = CALIBRATION_MAX;
      break;

    default:
      return; /* Unhandled state — no event published. */
  }

  ConfigurationFieldUpdate fields[2];
  size_t count = 1;
  const char *block;
  switch (event->state) {
    case UI_STATE_CALIBRATE_OBJECT:
      block = "calibration";
      fields[0] = (ConfigurationFieldUpdate){offsetof(CalibrationConfigs, ir_object_cal), &settings->calibration.ir_object_cal, sizeof(settings->calibration.ir_object_cal)};
      break;
    case UI_STATE_CALIBRATE_AMBIENT:
      block = "calibration";
      fields[0] = (ConfigurationFieldUpdate){offsetof(CalibrationConfigs, ir_ambient_cal), &settings->calibration.ir_ambient_cal, sizeof(settings->calibration.ir_ambient_cal)};
      break;
    case UI_STATE_SET_TEMP_UNIT:
      block = "system";
      fields[0] = (ConfigurationFieldUpdate){offsetof(SystemBaseConfigs, temp_unit), &settings->system.temp_unit, sizeof(settings->system.temp_unit)};
      break;
    default: return;
  }
  esp_err_t result = config->UpdateFields(config, block, fields, count, true);
  if (result != ESP_OK) ESP_LOGE(TAG, "Menu setting persistence failed: %d", result);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Relay Control Logic
 * ═══════════════════════════════════════════════════════════════════════════ */

/**
 * @brief Evaluates and applies the relay control logic for one loop iteration.
 *
 * Applies these priorities in the existing worker:
 *  1. CPU thermal override — relay forced ON regardless of other modes.
 *  2. System disabled      — relay forced OFF.
 *  3. Diagnostic action   — checked bounded operation, canceled by safety/stop.
 *  4. Manual selection    — relay follows fan_manual_state without delay.
 *  5. Sensing strategy    — effective object temperature or object-minus-ambient
 *                            difference crosses its validated hysteresis pair.
 *                            Invalid required inputs fail safe ON immediately.
 *
 * Switching boundaries are inclusive; the existing elapsed debounce and minimum
 * switch interval apply to valid sensing decisions, not immediate safety actions.
 *
 * Object temperature is read under spinlock to ensure thread safety.
 *
 * When the relay state actually changes, EVENT_RELAY_STATE_CHANGED is
 * published on the EventBus with inline bool data (no dangling pointer).
 *
 * @param settings Pointer to the current SystemSettings (must not be NULL).
 * @param nowMs    Current time in milliseconds (from xTaskGetTickCount()).
 */
static esp_err_t ClimateController_SetExternalTemperature(IClimateController *self, uint8_t slot, float value) {
  if (self != &climateControllerInstance.base || slot < 1 || slot > 3 || !TemperatureSources_IsInputValid(value)) return ESP_ERR_INVALID_ARG;
  portENTER_CRITICAL(&climateControllerInstance.spinlock);
  if (climateControllerInstance.state != SERVICE_STATE_STARTED || atomic_load(&climateControllerInstance.worker.stopRequested)) {
    portEXIT_CRITICAL(&climateControllerInstance.spinlock); return ESP_ERR_INVALID_STATE;
  }
  climateControllerInstance.externalSlots[slot - 1] = (ExternalTemperatureSlot){value, true,
      (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS)};
  portEXIT_CRITICAL(&climateControllerInstance.spinlock);
  return ESP_OK;
}

static esp_err_t ClimateController_ClearExternalTemperature(IClimateController *self,uint8_t slot) {
  if (self!=&climateControllerInstance.base || slot<1 || slot>3) return ESP_ERR_INVALID_ARG;
  portENTER_CRITICAL(&climateControllerInstance.spinlock);
  if (climateControllerInstance.state!=SERVICE_STATE_STARTED || atomic_load(&climateControllerInstance.worker.stopRequested)) {
    portEXIT_CRITICAL(&climateControllerInstance.spinlock);return ESP_ERR_INVALID_STATE;
  }
  climateControllerInstance.externalSlots[slot-1]=(ExternalTemperatureSlot){0};
  portEXIT_CRITICAL(&climateControllerInstance.spinlock);return ESP_OK;
}
static void Climate_ResolveSources(const ControlConfigs *config, uint32_t now, TemperatureSourceSnapshot *out) {
  out->config=*config;
  portENTER_CRITICAL(&climateControllerInstance.spinlock);
  float object = climateControllerInstance.objectTemp, ambient = climateControllerInstance.ambientTemp;
  bool hasSample = climateControllerInstance.hasSensorSample;
  uint32_t sampleMs = climateControllerInstance.lastSensorReadTick * portTICK_PERIOD_MS;
  memcpy(out->slots, climateControllerInstance.externalSlots, sizeof(out->slots));
  portEXIT_CRITICAL(&climateControllerInstance.spinlock);
  uint32_t age = (uint32_t)(now - sampleMs);
  bool fresh = hasSample && (!climateControllerInstance.config.sensorStaleTimeoutMs || age < climateControllerInstance.config.sensorStaleTimeoutMs);
  out->now_ms = now;
  out->object = TemperatureSources_Resolve(config->object_temperature_source, object, fresh, age, out->slots, now);
  out->ambient = TemperatureSources_Resolve(config->ambient_temperature_source, ambient, fresh, age, out->slots, now);
  TemperatureSources_ComputeDifference(out);
}
static esp_err_t ClimateController_GetTemperatureSources(IClimateController *self, TemperatureSourceSnapshot *out) {
  if (self != &climateControllerInstance.base || !out) return ESP_ERR_INVALID_ARG;
  IConfigurationManager *owner = ConfigurationManager_GetInstance();
  SystemSettings settings;
  if (!owner || !owner->GetSnapshot) return ESP_ERR_INVALID_STATE;
  esp_err_t result = owner->GetSnapshot(owner, &settings);
  if (result != ESP_OK) return result;
  Climate_ResolveSources(&settings.control, (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS), out);
  return ESP_OK;
}

static esp_err_t Climate_SetRelayTarget(bool target, uint32_t nowMs) {
  IRelay *relay=climateControllerInstance.relay;
  if (!relay || !relay->IsOpen) return ESP_ERR_INVALID_STATE;
  if (relay->IsOpen(relay)==target) return ESP_OK;
  esp_err_t result=target ? (relay->Open ? relay->Open(relay) : ESP_ERR_INVALID_STATE) :
      (relay->Close ? relay->Close(relay) : ESP_ERR_INVALID_STATE);
  if (result!=ESP_OK) { ESP_LOGE(TAG,"Fan actuator failed: target=%u result=%d",target,result);return result; }
  climateControllerInstance.rsm.relayStateChangeTime=nowMs;
  IEventBus *bus=ServiceLocator_GetEventBus();
  if (bus && bus->Publish) {
    BusEvent event={.eventName=EVENT_RELAY_STATE_CHANGED,.type=EVENT_TYPE_INFO,.dataSize=sizeof(bool)};
    event.inlineData[0]=target;event.data=event.inlineData;
    if (bus->Publish(event)!=0) ESP_LOGW(TAG,"Fan state event admission failed");
  }
  ESP_LOGI(TAG,"Fan output %s",target ? "ON" : "OFF");
  return ESP_OK;
}
static esp_err_t ClimateController_RequestFanAction(IClimateController *self,FanActionType type,uint32_t value) {
  if (self!=&climateControllerInstance.base || type>FAN_ACTION_SELF_TEST || type<0 ||
      (type==FAN_ACTION_PULSE && (!value || value>RELAY_MAX_PULSE_DURATION_MS)) ||
      (type==FAN_ACTION_AUTO_OFF && !value) || (type==FAN_ACTION_RELAY_MODE && value>1)) return ESP_ERR_INVALID_ARG;
  portENTER_CRITICAL(&climateControllerInstance.spinlock);
  if (climateControllerInstance.state!=SERVICE_STATE_STARTED || atomic_load(&climateControllerInstance.worker.stopRequested) ||
      climateControllerInstance.fanAction.pending || climateControllerInstance.fanAction.active || atomic_load(&climateControllerInstance.cpuThermalOverride)) {
    portEXIT_CRITICAL(&climateControllerInstance.spinlock);return ESP_ERR_INVALID_STATE;
  }
  uint32_t sequence=climateControllerInstance.fanAction.sequence+1;
  climateControllerInstance.fanAction=(FanActionStatus){.type=type,.value=value,.sequence=sequence,.pending=true,.result=ESP_ERR_NOT_FINISHED};
  portEXIT_CRITICAL(&climateControllerInstance.spinlock);return ESP_OK;
}
static esp_err_t ClimateController_GetFanActionStatus(IClimateController *self,FanActionStatus *out) {
  if (self!=&climateControllerInstance.base || !out) return ESP_ERR_INVALID_ARG;
  portENTER_CRITICAL(&climateControllerInstance.spinlock);*out=climateControllerInstance.fanAction;
  portEXIT_CRITICAL(&climateControllerInstance.spinlock);return ESP_OK;
}
static void Climate_CancelFanAction(void) {
  portENTER_CRITICAL(&climateControllerInstance.spinlock);
  if (climateControllerInstance.fanAction.pending || climateControllerInstance.fanAction.active) {
    climateControllerInstance.fanAction.pending=false;climateControllerInstance.fanAction.active=false;
    climateControllerInstance.fanAction.result=ESP_ERR_INVALID_STATE;
  }
  portEXIT_CRITICAL(&climateControllerInstance.spinlock);
}
/* No timer callback actuates the fan. Timed diagnostics use the existing worker,
 * elapsed deadlines and CPU priority; status separates admission from completion. */
static bool Climate_ProcessFanAction(uint32_t nowMs) {
  FanActionStatus action;
  ClimateController_GetFanActionStatus(&climateControllerInstance.base,&action);
  if (!action.pending && !action.active) return false;
  IRelay *relay=climateControllerInstance.relay;
  uint64_t now=(uint64_t)esp_timer_get_time();
  if (action.pending) {
    action.pending=false;
    if (action.type==FAN_ACTION_RESET_STATS) action.result=relay->ResetStats ? relay->ResetStats(relay) : ESP_ERR_INVALID_STATE;
    else if (action.type==FAN_ACTION_RELAY_MODE) action.result=relay->SetMode ? relay->SetMode(relay,(RelayMode)action.value) : ESP_ERR_INVALID_STATE;
    else {
      action.result=Climate_SetRelayTarget(true,nowMs);
      if (action.result==ESP_OK) {
        action.active=true;action.result=ESP_ERR_NOT_FINISHED;
        climateControllerInstance.fanActionPhase=0;
        climateControllerInstance.fanActionDeadlineUs=now+(uint64_t)(action.type==FAN_ACTION_SELF_TEST ? FAN_SELF_TEST_PHASE_MS : action.value)*1000;
      }
    }
  } else if (now>=climateControllerInstance.fanActionDeadlineUs) {
    if (action.type==FAN_ACTION_SELF_TEST && climateControllerInstance.fanActionPhase==1) {
      action.result=relay->IsOpen(relay) ? ESP_FAIL : ESP_OK;action.active=false;
    } else {
      action.result=Climate_SetRelayTarget(false,nowMs);
      if (action.type==FAN_ACTION_SELF_TEST && action.result==ESP_OK) {
        climateControllerInstance.fanActionPhase=1;climateControllerInstance.fanActionDeadlineUs=now+(uint64_t)FAN_SELF_TEST_PHASE_MS*1000;
        action.result=ESP_ERR_NOT_FINISHED;
      } else action.active=false;
    }
  }
  portENTER_CRITICAL(&climateControllerInstance.spinlock);climateControllerInstance.fanAction=action;
  portEXIT_CRITICAL(&climateControllerInstance.spinlock);
  return true;
}
static void Climate_EvaluateRelay(SystemSettings *settings,uint32_t nowMs) {
  if (!climateControllerInstance.relay) return;
  RelayStateMachine *rsm=&climateControllerInstance.rsm;
  if (atomic_load(&climateControllerInstance.cpuThermalOverride) && climateControllerInstance.config.cpuTempProtectionEnabled) {
    Climate_CancelFanAction();rsm->isConditionPending=false;
    Climate_SetRelayTarget(true,nowMs);return;
  }
  if (!settings->system.system_enabled) {
    Climate_CancelFanAction();rsm->isConditionPending=false;
    Climate_SetRelayTarget(false,nowMs);return;
  }
  if (Climate_ProcessFanAction(nowMs)) { rsm->isConditionPending=false;return; }
  if (settings->control.fan_manual_mode) {
    rsm->isConditionPending=false;Climate_SetRelayTarget(settings->control.fan_manual_state,nowMs);return;
  }
  TemperatureSourceSnapshot temperatures;
  Climate_ResolveSources(&settings->control,nowMs,&temperatures);
  uint8_t sources=temperatures.object.effective_source | (temperatures.ambient.effective_source<<4);
  if (!climateControllerInstance.hasDecisionConfig || memcmp(&settings->control,&climateControllerInstance.lastDecisionConfig,sizeof(ControlConfigs))!=0 || sources!=climateControllerInstance.lastEffectiveSources)
    rsm->isConditionPending=false;
  climateControllerInstance.lastDecisionConfig=settings->control;
  climateControllerInstance.lastEffectiveSources=sources;climateControllerInstance.hasDecisionConfig=true;
  bool invalid=!temperatures.object.valid || (settings->control.fan_strategy==1 && !temperatures.difference_valid);
  portENTER_CRITICAL(&climateControllerInstance.spinlock);
  bool continuityLost=climateControllerInstance.sensorContinuityLost;
  climateControllerInstance.sensorContinuityLost=false;
  portEXIT_CRITICAL(&climateControllerInstance.spinlock);
  if (continuityLost) rsm->isConditionPending=false;
  if (invalid) { rsm->isConditionPending=false;Climate_SetRelayTarget(true,nowMs);return; }
  bool current=climateControllerInstance.relay->IsOpen(climateControllerInstance.relay);
  bool target=FanControl_Evaluate(current,&settings->control,&temperatures);
  if (settings->control.fan_strategy==0 && climateControllerInstance.config.hysteresisDeadband!=0.0f) {
    /* Preserve the optional legacy object-mode modifier. Delta thresholds keep
     * their canonical inclusive boundaries and do not inherit this modifier. */
    float deadband=climateControllerInstance.config.hysteresisDeadband;
    target=FanControl_EvaluateObjectSensitive(current,temperatures.object.value,
        settings->control.relay_open_temp-deadband,settings->control.relay_close_temp+deadband);
  }
  if (current==target) { rsm->isConditionPending=false;return; }
  if (!rsm->isConditionPending || rsm->pendingActionOpen!=target) {
    rsm->isConditionPending=true;rsm->pendingActionOpen=target;rsm->conditionMetStartTime=nowMs;return;
  }
  if ((uint32_t)(nowMs-rsm->conditionMetStartTime)<climateControllerInstance.config.relayDebounceMs ||
      (uint32_t)(nowMs-rsm->relayStateChangeTime)<climateControllerInstance.config.relayMinSwitchMs) return;
  if (Climate_SetRelayTarget(target,nowMs)==ESP_OK) rsm->isConditionPending=false;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * FreeRTOS Task
 * ═══════════════════════════════════════════════════════════════════════════ */

_Static_assert(sizeof(FanSourceDiagnostics) <= BUS_EVENT_INLINE_DATA_SIZE, "Source diagnostics must be queue-owned inline");
static void Climate_PublishSources(const ControlConfigs *config, uint32_t now) {
  TemperatureSourceSnapshot snapshot;
  Climate_ResolveSources(config, now, &snapshot);
  FanSourceDiagnostics data = {0};
  for (unsigned i=0; i<3; ++i) {
    TemperatureSlotState state = TemperatureSources_SlotState(&snapshot.slots[i], now);
    data.slot_states |= (uint8_t)state << (i*2);
    data.temperatures[i] = state == TEMPERATURE_VALID ? (int16_t)lroundf(snapshot.slots[i].value*100) : INT16_MIN;
  }
  data.temperatures[3] = snapshot.object.valid ? (int16_t)lroundf(snapshot.object.value*100) : INT16_MIN;
  data.temperatures[4] = snapshot.ambient.valid ? (int16_t)lroundf(snapshot.ambient.value*100) : INT16_MIN;
  data.temperatures[5] = snapshot.difference_valid ? (int16_t)lroundf(snapshot.temperature_difference*100) : INT16_MIN;
  data.sources = snapshot.object.effective_source | (snapshot.ambient.effective_source << 4);
  data.status = (uint8_t)snapshot.object.status | ((uint8_t)snapshot.ambient.status << 4);
  if (!climateControllerInstance.hasSourceDiagnostics || data.sources!=climateControllerInstance.lastSourceDiagnostics.sources || data.status!=climateControllerInstance.lastSourceDiagnostics.status) {
    ESP_LOGI(TAG,"Temperature sources: object configured=%u effective=%u status=%u; ambient configured=%u effective=%u status=%u",
        snapshot.object.configured_source,snapshot.object.effective_source,(unsigned)snapshot.object.status,
        snapshot.ambient.configured_source,snapshot.ambient.effective_source,(unsigned)snapshot.ambient.status);
  }
  if (climateControllerInstance.hasSourceDiagnostics && memcmp(&data, &climateControllerInstance.lastSourceDiagnostics, sizeof(data)) == 0 &&
      (uint32_t)(now-climateControllerInstance.lastSourcePublicationMs) < FAN_SOURCE_DIAGNOSTIC_REFRESH_MS) return;
  IEventBus *bus = ServiceLocator_GetEventBus();
  if (!bus || !bus->Publish) return;
  BusEvent event = {.eventName = EVENT_TEMPERATURE_SOURCES_UPDATED, .type = EVENT_TYPE_INFO, .dataSize = sizeof(data)};
  memcpy(event.inlineData, &data, sizeof(data)); event.data = event.inlineData;
  if (bus->Publish(event) == 0) {
    climateControllerInstance.lastSourceDiagnostics = data;
    climateControllerInstance.lastSourcePublicationMs = now;
    climateControllerInstance.hasSourceDiagnostics = true;
  }
}

static bool Climate_EvaluateSettingsSnapshot(void) {
  IConfigurationManager *cfg = climateControllerInstance.configManager;
  SystemSettings settings;
  if (!cfg || !cfg->GetSnapshot || cfg->GetSnapshot(cfg, &settings) != ESP_OK)
    return false;
  Climate_CheckCpuThermalOverride(&settings);
  uint32_t nowMs = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
  Climate_EvaluateRelay(&settings, nowMs);
  Climate_PublishSources(&settings.control, nowMs);
  return true;
}

/**
 * @brief Main ClimateController FreeRTOS task.
 *
 * When event-driven sensor mode is enabled (config.enableEventDrivenSensors),
 * the task only drives relay evaluation and diagnostics — sensor data is
 * pushed via EventBus callbacks registered in ClimateController_Start().
 *
 * When event-driven mode is disabled (legacy/fallback), the task also polls
 * the IR and CPU temperature sensors directly.
 *
 * Self-healing: if a service dependency was not available at Start() time, the
 * task attempts to resolve it from the ServiceLocator on every iteration.
 *
 * @param pvParameters Unused.
 */
static void ClimateController_Task(void *pvParameters) {
  (void)pvParameters;
  ESP_LOGI(TAG,
           "Climate task started (loop=%lu ms, debounce=%lu ms, "
           "minSwitch=%lu ms, eventDriven=%d, staleTimeout=%lu ms)",
           (unsigned long)climateControllerInstance.config.mainLoopDelayMs,
           (unsigned long)climateControllerInstance.config.relayDebounceMs,
           (unsigned long)climateControllerInstance.config.relayMinSwitchMs,
           (int)climateControllerInstance.config.enableEventDrivenSensors,
           (unsigned long)climateControllerInstance.config.sensorStaleTimeoutMs);

  while (1) {
    /* Feed the task watchdog before any potentially blocking work. */
    WatchdogWorker_Checkpoint(&climateControllerInstance.worker,
        (IWatchdog *)ServiceLocator_GetService(SVC_NAME_WATCHDOG),
        (uint64_t)esp_timer_get_time() / 1000ULL);

    /* ── Self-healing: lazy service resolution ─────────────────────────── */
    if (!climateControllerInstance.configManager) {
      climateControllerInstance.configManager = ConfigurationManager_GetInstance();
    }
    if (!climateControllerInstance.tempSensor) {
      climateControllerInstance.tempSensor =
          (IInfraredTemperatureSensor *)ServiceLocator_GetService(
              SVC_NAME_IR_TEMP_SENSOR);
    }
    if (!climateControllerInstance.internalTemp) {
      climateControllerInstance.internalTemp =
          (IInternalTemperatureSensor *)ServiceLocator_GetService(
              SVC_NAME_INTERNAL_TEMP_SENSOR);
    }
    if (!climateControllerInstance.relay) {
      climateControllerInstance.relay =
          (IRelay *)ServiceLocator_GetService(SVC_NAME_RELAY);
    }

    /* ── Legacy polling path (used when event-driven mode is disabled) ──── */
    if (!climateControllerInstance.config.enableEventDrivenSensors || !climateControllerInstance.ownedBus) {
      Climate_PollIRSample();
      if (climateControllerInstance.internalTemp) {
        float cpu = climateControllerInstance.internalTemp->ReadTemperature(
            climateControllerInstance.internalTemp);
        Climate_UpdateCpuTemp(cpu);
      }
    }

    /* ── Stale sensor detection ────────────────────────────────────────── */
    uint32_t nowMs = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    Climate_CheckSensorStale(nowMs);

    /* ── Relay control evaluation ──────────────────────────────────────── */
    Climate_EvaluateSettingsSnapshot();

    uint32_t delayMs=climateControllerInstance.config.mainLoopDelayMs;
    FanActionStatus action;ClimateController_GetFanActionStatus(&climateControllerInstance.base,&action);
    if (action.active) {
      uint64_t nowUs=(uint64_t)esp_timer_get_time();
      uint64_t remainingMs=climateControllerInstance.fanActionDeadlineUs>nowUs ?
          (climateControllerInstance.fanActionDeadlineUs-nowUs+999)/1000 : 0;
      if (remainingMs<delayMs) delayMs=(uint32_t)remainingMs;
    }
    TickType_t wait = pdMS_TO_TICKS(delayMs);
    vTaskDelay(wait ? wait : 1);
  }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * IService Implementation
 * ═══════════════════════════════════════════════════════════════════════════ */

/**
 * @brief Initialises the ClimateController service (IService::Initialize).
 *
 * Resets temperature cache to sentinel values (-127°C) and marks the service
 * as initialised. Idempotent — safe to call multiple times.
 *
 * @param self     Unused (singleton implementation).
 * @param registry Unused at this stage.
 * @return         0 on success.
 */
static int32_t ClimateController_Initialize(IService *self,
                                             IDIRegistry *registry) {
  (void)self;
  (void)registry;
  if (climateControllerInstance.state >= SERVICE_STATE_INITIALIZED) return 0;

  SemaphoreHandle_t control = xSemaphoreCreateMutex();
  if (!control) return -1;
  if (WatchdogWorker_Init(&climateControllerInstance.worker) != 0) {
    vSemaphoreDelete(control);
    return -1;
  }
  climateControllerInstance.lifecycleMutex = control;
  climateControllerInstance.state              = SERVICE_STATE_INITIALIZED;
  climateControllerInstance.objectTemp         = -127.0f;
  climateControllerInstance.ambientTemp        = -127.0f;
  climateControllerInstance.cpuTemp            = -127.0f;
  climateControllerInstance.lastSensorReadTick = 0;
  climateControllerInstance.hasSensorSample = false;
  memset(climateControllerInstance.externalSlots, 0, sizeof(climateControllerInstance.externalSlots));
  climateControllerInstance.hasSourceDiagnostics = false;
  climateControllerInstance.hasDecisionConfig=false;
  memset(&climateControllerInstance.fanAction,0,sizeof(climateControllerInstance.fanAction));
  climateControllerInstance.lastSensorSampleUs = 0;
  climateControllerInstance.is_config_set      = false;
  climateControllerInstance.cpuThermalOverride = false;
  climateControllerInstance.sensorStaleNotified = false;

  /* Initialise the relay state machine context. */
  memset(&climateControllerInstance.rsm, 0, sizeof(RelayStateMachine));

  /* Initialise the history ring buffer (capacity configured later in Start). */
  memset(&climateControllerInstance.history, 0, sizeof(TempHistory));

  return 0;
}

/**
 * @brief Initialises with a caller-supplied configuration (IClimateController extension).
 *
 * @param self     Pointer to the IClimateController interface.
 * @param config   Caller-supplied configuration.
 * @param registry Dependency injection registry.
 * @return         0 on success.
 */
static int32_t ClimateController_InitializeWithConfig(IClimateController *self,
                                                       ClimateControllerConfig config,
                                                       IDIRegistry *registry) {
  if (self != &climateControllerInstance.base ||
      ClimateController_Initialize((IService *)self, registry) != 0) return -1;
  if (xSemaphoreTake(climateControllerInstance.lifecycleMutex,
      pdMS_TO_TICKS(WATCHDOG_WORKER_CONTROL_TIMEOUT_MS)) != pdTRUE) return -1;
  /* Startup configuration must not change beneath workers or callbacks. */
  if (climateControllerInstance.state != SERVICE_STATE_INITIALIZED ||
      atomic_load(&climateControllerInstance.worker.task) ||
      climateControllerInstance.ownedBus || climateControllerInstance.ownedMenu) {
    xSemaphoreGive(climateControllerInstance.lifecycleMutex);
    return -1;
  }
  climateControllerInstance.config        = config;
  climateControllerInstance.is_config_set = true;
  xSemaphoreGive(climateControllerInstance.lifecycleMutex);
  ESP_LOGI(TAG, "Initialized with custom config (eventDriven=%d, hysteresis=%.2f, "
           "staleTimeout=%lu ms, cpuProtect=%d)",
           (int)config.enableEventDrivenSensors, config.hysteresisDeadband,
           (unsigned long)config.sensorStaleTimeoutMs,
           (int)config.cpuTempProtectionEnabled);
  return 0;
}

/**
 * @brief Starts the ClimateController service (IService::Start).
 *
 * 1. Applies default config if InitializeWithConfig was not called.
 * 2. Resolves service dependencies from ServiceLocator.
 * 3. Subscribes to EventBus events (if event-driven mode is enabled).
 * 4. Registers OLED menu plugins.
 * 5. Registers debug console commands.
 * 6. Creates the FreeRTOS task.
 * 7. Registers the task with the watchdog (after taskHandle is valid).
 *
 * @param self Unused.
 * @return     0 on success.
 */
static const MenuPluginRegistration menuEntries[] = {
    {UI_STATE_SET_LOW_TEMP, "Temp Low", Climate_MenuHandlerEx, NULL},
    {UI_STATE_SET_HIGH_TEMP, "Temp High", Climate_MenuHandlerEx, NULL},
    {UI_STATE_SET_TEMP_UNIT, "Temp Unit", Climate_MenuHandlerEx, NULL},
    {UI_STATE_FAN_MODE, "Fan Mode", Climate_MenuHandlerEx, NULL},
    {UI_STATE_FAN_DELTA_ON, "Fan Delta ON", Climate_MenuHandlerEx, NULL},
    {UI_STATE_FAN_DELTA_OFF, "Fan Delta OFF", Climate_MenuHandlerEx, NULL},
    {UI_STATE_OBJECT_SOURCE, "Object Source", Climate_MenuHandlerEx, NULL},
    {UI_STATE_AMBIENT_SOURCE, "Ambient Source", Climate_MenuHandlerEx, NULL},
    {UI_STATE_CALIBRATE_OBJECT, "Cal Radiator", Climate_MenuHandlerEx, NULL},
    {UI_STATE_CALIBRATE_AMBIENT, "Cal Ambient", Climate_MenuHandlerEx, NULL},
  };

static const char *climateEvents[] = {EVENT_IR_TEMP_UPDATED, EVENT_INTERNAL_TEMP_UPDATED, EVENT_SYSTEM_THERMAL_ALERT};
static BusEventHandler climateHandlers[] = {Climate_OnIRTempUpdated, Climate_OnInternalTempUpdated, Climate_OnThermalAlert};

/* Called with lifecycleMutex held. Failed removal preserves the remaining owners. */
static int32_t Climate_StopOwned(ServiceState finalState) {
  climateControllerInstance.state = SERVICE_STATE_ERROR;
  if (WatchdogWorker_Stop(&climateControllerInstance.worker,
      pdMS_TO_TICKS(WATCHDOG_WORKER_CONTROL_TIMEOUT_MS)) != 0) return -1;
  climateControllerInstance.taskHandle = NULL;
  Climate_CancelFanAction();
  IMenuManager *menu = climateControllerInstance.ownedMenu;
  if (menu) {
    if (!menu->UnregisterPluginBatch || menu->UnregisterPluginBatch(menu, menuEntries,
        sizeof(menuEntries) / sizeof(menuEntries[0])) != MENU_OK) return -1;
    climateControllerInstance.ownedMenu = NULL;
  }
  for (unsigned i = 0; i < 3; ++i) {
    if (!(climateControllerInstance.subscriptions & (1U << i))) continue;
    IEventBus *bus = climateControllerInstance.ownedBus;
    if (!bus || !bus->Unsubscribe || bus->Unsubscribe(bus, climateEvents[i], climateHandlers[i]) != 0) return -1;
    climateControllerInstance.subscriptions &= ~(1U << i);
  }
  climateControllerInstance.ownedBus = NULL;
  climateControllerInstance.state = finalState;
  return 0;
}

static int32_t Climate_StartOwned(IService *self) {
  ServiceState previous = climateControllerInstance.state;
  (void)self;
  if (climateControllerInstance.state == SERVICE_STATE_STARTED)
    return atomic_load(&climateControllerInstance.worker.stopRequested) ||
           !atomic_load(&climateControllerInstance.worker.task) ? -1 : 0;
  if (climateControllerInstance.state != SERVICE_STATE_INITIALIZED &&
      climateControllerInstance.state != SERVICE_STATE_STOPPED) return -1;

  if (!climateControllerInstance.is_config_set) {
    climateControllerInstance.config        = ClimateController_GetDefaultConfig();
    climateControllerInstance.is_config_set = true;
    ESP_LOGI(TAG, "No config provided — using defaults");
  }

  /* Reject a busy menu group before subscriptions or worker startup. */
  IDebugConsole *console = (IDebugConsole *)ServiceLocator_GetService(SVC_NAME_DEBUG_CONSOLE);
  if (console) {
    esp_err_t result = ClimateCommands_Register(console);
    if (result != ESP_OK) return result;
    ESP_LOGI(TAG, "Debug console commands registered");
  }
  IMenuManager *menu = (IMenuManager *)ServiceLocator_GetService(SVC_NAME_MENU_MANAGER);
  if (menu) {
    MenuManagerError result = menu->RegisterPluginBatch && menu->UnregisterPluginBatch
        ? menu->RegisterPluginBatch(menu, menuEntries, sizeof(menuEntries) / sizeof(menuEntries[0]))
        : MENU_ERR_INVALID_STATE;
    if (result != MENU_OK) {
      ESP_LOGE(TAG, "Climate menu registration failed: %d", result);
      return -1;
    }
    climateControllerInstance.ownedMenu = menu;
  }

  /* Configure history ring buffer capacity from config. */
  uint8_t histCap = climateControllerInstance.config.historyBufferSize;
  if (histCap > CLIMATE_HISTORY_MAX) histCap = CLIMATE_HISTORY_MAX;
  climateControllerInstance.history.capacity = histCap;

  /* ── Resolve dependencies ───────────────────────────────────────────────── */
  climateControllerInstance.configManager =
      ConfigurationManager_GetInstance();
  climateControllerInstance.tempSensor =
      (IInfraredTemperatureSensor *)ServiceLocator_GetService(
          SVC_NAME_IR_TEMP_SENSOR);
  climateControllerInstance.internalTemp =
      (IInternalTemperatureSensor *)ServiceLocator_GetService(
          SVC_NAME_INTERNAL_TEMP_SENSOR);
  climateControllerInstance.relay =
      (IRelay *)ServiceLocator_GetService(SVC_NAME_RELAY);

  climateControllerInstance.state = SERVICE_STATE_STARTED;

  /* ── EventBus subscriptions (event-driven sensor path) ─────────────────── */
  IEventBus *eb = ServiceLocator_GetEventBus();
  if (eb) {
    climateControllerInstance.ownedBus = eb;
    uint8_t wanted = (climateControllerInstance.config.enableEventDrivenSensors ? 3U : 0U) |
        (climateControllerInstance.config.cpuTempProtectionEnabled ? 4U : 0U);
    for (unsigned i = 0; i < 3; ++i) {
      if (!(wanted & (1U << i))) continue;
      if (!eb->Subscribe || !eb->Unsubscribe || eb->Subscribe(eb, climateEvents[i], climateHandlers[i]) != 0) {
        (void)Climate_StopOwned(previous);
        return -1;
      }
      climateControllerInstance.subscriptions |= 1U << i;
    }
  } else if (climateControllerInstance.config.enableEventDrivenSensors) {
    ESP_LOGW(TAG, "EventBus unavailable; using sensor polling");
  }

  /* ── FreeRTOS task creation ─────────────────────────────────────────────── */
  if (WatchdogWorker_Start(&climateControllerInstance.worker, ClimateController_Task,
                           "ClimateCtrl", climateControllerInstance.config.taskStackSize,
                           NULL, climateControllerInstance.config.taskPriority) != 0) {
    ESP_LOGE(TAG, "Failed to create ClimateController task");
    (void)Climate_StopOwned(previous);
    return -1;
  }
  climateControllerInstance.taskHandle = atomic_load(&climateControllerInstance.worker.task);

  ESP_LOGI(TAG, "ClimateController started successfully");
  return 0;
}

/**
 * @brief Stops the ClimateController service (IService::Stop).
 *
 * Unsubscribes from EventBus, removes task from watchdog, deletes the
 * FreeRTOS task, and marks the service as stopped.
 *
 * @param self Unused.
 * @return     0 on success.
 */
static int32_t ClimateController_Start(IService *self) {
  if (self != &climateControllerInstance.base.base || !climateControllerInstance.lifecycleMutex ||
      xSemaphoreTake(climateControllerInstance.lifecycleMutex, pdMS_TO_TICKS(WATCHDOG_WORKER_CONTROL_TIMEOUT_MS)) != pdTRUE) return -1;
  int32_t result = Climate_StartOwned(self);
  xSemaphoreGive(climateControllerInstance.lifecycleMutex);
  return result;
}

static int32_t ClimateController_Stop(IService *self) {
  TaskHandle_t task = atomic_load(&climateControllerInstance.worker.task);
  if (self != &climateControllerInstance.base.base || !climateControllerInstance.lifecycleMutex ||
      (task && task == xTaskGetCurrentTaskHandle()) ||
      xSemaphoreTake(climateControllerInstance.lifecycleMutex, pdMS_TO_TICKS(WATCHDOG_WORKER_CONTROL_TIMEOUT_MS)) != pdTRUE) return -1;
  int32_t result = Climate_StopOwned(SERVICE_STATE_STOPPED);
  xSemaphoreGive(climateControllerInstance.lifecycleMutex);
  return result;
}

/**
 * @brief Returns the current service lifecycle state (IService::GetState).
 *
 * @param self Unused.
 * @return     Current ServiceState value.
 */
static ServiceState ClimateController_GetState(IService *self) {
  (void)self;
  return climateControllerInstance.state;
}

/**
 * @brief Health check — returns 0 if the service is running (IService::CheckHealth).
 *
 * @param self Unused.
 * @return     0 if SERVICE_STATE_STARTED, -1 otherwise.
 */
static int32_t ClimateController_CheckHealth(IService *self) {
  (void)self;
  return (climateControllerInstance.state == SERVICE_STATE_STARTED &&
          WatchdogWorker_Healthy(&climateControllerInstance.worker)) ? 0 : -1;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * IClimateController Getter Implementations
 * ═══════════════════════════════════════════════════════════════════════════ */

/**
 * @brief Thread-safe getter for the cached object temperature.
 */
static float ClimateController_GetObjectTemp(IClimateController *self) {
  (void)self;
  portENTER_CRITICAL(&climateControllerInstance.spinlock);
  float v = climateControllerInstance.objectTemp;
  portEXIT_CRITICAL(&climateControllerInstance.spinlock);
  return v;
}

/**
 * @brief Thread-safe getter for the cached ambient temperature.
 */
static float ClimateController_GetAmbientTemp(IClimateController *self) {
  (void)self;
  portENTER_CRITICAL(&climateControllerInstance.spinlock);
  float v = climateControllerInstance.ambientTemp;
  portEXIT_CRITICAL(&climateControllerInstance.spinlock);
  return v;
}

/**
 * @brief Thread-safe getter for the cached CPU temperature.
 */
static float ClimateController_GetCpuTemp(IClimateController *self) {
  (void)self;
  portENTER_CRITICAL(&climateControllerInstance.spinlock);
  float v = climateControllerInstance.cpuTemp;
  portEXIT_CRITICAL(&climateControllerInstance.spinlock);
  return v;
}

/**
 * @brief Returns the current relay open/closed state.
 */
static bool ClimateController_GetRelayState(IClimateController *self) {
  (void)self;
  if (!climateControllerInstance.relay) return false;
  return climateControllerInstance.relay->IsOpen(climateControllerInstance.relay);
}

/**
 * @brief Returns the FreeRTOS tick count of the last successful IR sensor read.
 */
static uint32_t ClimateController_GetLastSensorReadTime(IClimateController *self) {
  (void)self;
  portENTER_CRITICAL(&climateControllerInstance.spinlock);
  uint32_t t = climateControllerInstance.lastSensorReadTick;
  portEXIT_CRITICAL(&climateControllerInstance.spinlock);
  return t;
}

/**
 * @brief Returns the current fan/relay operating mode.
 */
static bool ClimateController_IsCpuThermalOverrideActive(IClimateController *self) {
  (void)self;
  return atomic_load(&climateControllerInstance.cpuThermalOverride) &&
         climateControllerInstance.config.cpuTempProtectionEnabled;
}

static ClimateFanMode ClimateController_GetFanMode(IClimateController *self) {
  (void)self;
  IConfigurationManager *cfg = climateControllerInstance.configManager;
  SystemSettings snapshot;
  if (!cfg || !cfg->GetSnapshot || cfg->GetSnapshot(cfg, &snapshot) != ESP_OK)
    return CLIMATE_FAN_UNKNOWN;
  const SystemSettings *s = &snapshot;
  if (!s->control.fan_manual_mode) return s->control.fan_strategy == 1 ? CLIMATE_FAN_AMBIENT_DELTA : CLIMATE_FAN_OBJECT_SENSITIVE;
  return s->control.fan_manual_state ? CLIMATE_FAN_MANUAL_ON : CLIMATE_FAN_MANUAL_OFF;
}

/**
 * @brief Returns a snapshot of the active ClimateController configuration.
 */
static ClimateControllerConfig ClimateController_GetConfig(IClimateController *self) {
  (void)self;
  return climateControllerInstance.config;
}

/**
 * @brief Returns whether the relay debounce countdown is currently active.
 */
static bool ClimateController_GetRelayDebounceState(IClimateController *self) {
  (void)self;
  return climateControllerInstance.rsm.isConditionPending;
}

/**
 * @brief Returns whether the IR sensor reading is considered stale.
 */
static bool ClimateController_IsSensorStale(IClimateController *self) {
  (void)self;
  if (climateControllerInstance.config.sensorStaleTimeoutMs == 0) return false;

  uint32_t lastTick;
  portENTER_CRITICAL(&climateControllerInstance.spinlock);
  lastTick = climateControllerInstance.lastSensorReadTick;
  bool hasSample = climateControllerInstance.hasSensorSample;
  portEXIT_CRITICAL(&climateControllerInstance.spinlock);

  if (!hasSample) return true; /* Tick zero is a valid measurement timestamp. */

  uint32_t nowMs    = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
  uint32_t lastMs   = (uint32_t)(lastTick * portTICK_PERIOD_MS);
  return (nowMs - lastMs) >= climateControllerInstance.config.sensorStaleTimeoutMs;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Self-Test
 * ═══════════════════════════════════════════════════════════════════════════ */

/**
 * @brief Runs an in-place self-test of the ClimateController subsystem.
 *
 * Checks:
 *  1.  Service is in STARTED state.
 *  2.  IR temperature sensor is reachable.
 *  3.  Relay is reachable.
 *  4.  Configuration manager is reachable.
 *  5.  Object temperature is within the physically plausible range [-50, 300] °C.
 *  6.  Ambient temperature is within the physically plausible range [-50, 100] °C.
 *  7.  CPU temperature is within the physically plausible range [0, 150] °C.
 *  8.  Relay open setpoint is strictly greater than close setpoint.
 *  9.  IR sensor reading is not stale (if stale detection is enabled).
 *  10. Watchdog is reachable.
 *
 * Results are logged at INFO level. Failures are logged at ERROR level.
 *
 * @param self Pointer to IClimateController.
 * @return     0 if all checks pass, negative count of failed checks otherwise.
 */
static int32_t ClimateController_RunSelfTest(IClimateController *self) {
  int32_t failures = 0;

  ESP_LOGI(TAG, "=== ClimateController Self-Test ===");

  /* Check 1: Service state. */
  if (climateControllerInstance.state != SERVICE_STATE_STARTED) {
    ESP_LOGE(TAG, "[FAIL] Service is not in STARTED state (state=%d)",
             (int)climateControllerInstance.state);
    failures++;
  } else {
    ESP_LOGI(TAG, "[PASS] Service state: STARTED");
  }

  /* Check 2: IR temperature sensor. */
  if (!climateControllerInstance.tempSensor) {
    ESP_LOGE(TAG, "[FAIL] IR temperature sensor not available");
    failures++;
  } else {
    ESP_LOGI(TAG, "[PASS] IR temperature sensor: available");
  }

  /* Check 3: Relay. */
  if (!climateControllerInstance.relay) {
    ESP_LOGE(TAG, "[FAIL] Relay not available");
    failures++;
  } else {
    ESP_LOGI(TAG, "[PASS] Relay: available (state=%s)",
             climateControllerInstance.relay->IsOpen(
                 climateControllerInstance.relay)
                 ? "OPEN"
                 : "CLOSED");
  }

  /* Check 4: Configuration manager. */
  if (!climateControllerInstance.configManager) {
    ESP_LOGE(TAG, "[FAIL] Configuration manager not available");
    failures++;
  } else {
    ESP_LOGI(TAG, "[PASS] Configuration manager: available");
  }

  /* Check 5: Object temperature plausibility. */
  float objTemp = ClimateController_GetObjectTemp(self);
  if (objTemp < -50.0f || objTemp > 300.0f) {
    ESP_LOGE(TAG, "[FAIL] Radiator temp out of plausible range: %.2f°C", objTemp);
    failures++;
  } else {
    ESP_LOGI(TAG, "[PASS] Radiator temperature: %.2f°C", objTemp);
  }

  /* Check 6: Ambient temperature plausibility. */
  float ambTemp = ClimateController_GetAmbientTemp(self);
  if (ambTemp < -50.0f || ambTemp > 100.0f) {
    ESP_LOGE(TAG, "[FAIL] Ambient temp out of plausible range: %.2f°C", ambTemp);
    failures++;
  } else {
    ESP_LOGI(TAG, "[PASS] Ambient temperature: %.2f°C", ambTemp);
  }

  /* Check 7: CPU temperature plausibility. */
  float cpuTemp = ClimateController_GetCpuTemp(self);
  if (cpuTemp < 0.0f || cpuTemp > 150.0f) {
    ESP_LOGE(TAG, "[FAIL] CPU temp out of plausible range: %.2f°C", cpuTemp);
    failures++;
  } else {
    ESP_LOGI(TAG, "[PASS] CPU temperature: %.2f°C", cpuTemp);
  }

  /* Check 8: Setpoint consistency (open > close). */
  if (climateControllerInstance.configManager) {
    IConfigurationManager *cfg = climateControllerInstance.configManager;
    SystemSettings snapshot;
    const SystemSettings *s = &snapshot;
    if (cfg->GetSnapshot && cfg->GetSnapshot(cfg, &snapshot) == ESP_OK) {
      if (s->control.relay_open_temp <= s->control.relay_close_temp) {
        ESP_LOGE(TAG, "[FAIL] Open setpoint (%.2f) <= close setpoint (%.2f) — relay will oscillate",
                 s->control.relay_open_temp, s->control.relay_close_temp);
        failures++;
      } else {
        ESP_LOGI(TAG, "[PASS] Setpoints: open=%.2f°C, close=%.2f°C",
                 s->control.relay_open_temp, s->control.relay_close_temp);
      }
    } else {
      ESP_LOGE(TAG, "[FAIL] Configuration snapshot unavailable");
      failures++;
    }
  }

  /* Check 9: Sensor stale (only if stale detection is configured). */
  if (climateControllerInstance.config.sensorStaleTimeoutMs > 0) {
    bool stale = ClimateController_IsSensorStale(self);
    if (stale) {
      ESP_LOGE(TAG, "[FAIL] IR sensor reading is stale");
      failures++;
    } else {
      ESP_LOGI(TAG, "[PASS] IR sensor reading is fresh");
    }
  }

  /* Check 10: Watchdog. */
  if (!climateControllerInstance.watchdog) {
    ESP_LOGW(TAG, "[WARN] Watchdog not available (non-critical)");
    /* Not counted as a failure — watchdog is optional during tests. */
  } else {
    ESP_LOGI(TAG, "[PASS] Watchdog: available");
  }

  if (failures == 0) {
    ESP_LOGI(TAG, "=== Self-Test PASSED (all checks OK) ===");
  } else {
    ESP_LOGE(TAG, "=== Self-Test FAILED (%ld check(s) failed) ===",
             (long)failures);
  }

  return -failures;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * History Ring Buffer Read
 * ═══════════════════════════════════════════════════════════════════════════ */

/**
 * @brief Returns a snapshot of the temperature history ring buffer.
 *
 * Copies up to @p maxEntries entries (oldest first) from the ring buffer
 * into the caller-supplied @p out array. Protected by spinlock.
 *
 * @param out       Destination array (must have capacity for at least maxEntries items).
 * @param maxEntries Maximum number of entries to copy.
 * @return          Number of entries actually copied (0 if history is empty or disabled).
 */
uint8_t ClimateController_GetHistory(ClimateTempHistoryEntry *out, uint8_t maxEntries) {
  if (!out || maxEntries == 0) return 0;

  portENTER_CRITICAL(&climateControllerInstance.spinlock);
  TempHistory *hist = &climateControllerInstance.history;
  uint8_t count     = hist->count;
  uint8_t cap       = hist->capacity;

  if (count == 0 || cap == 0) {
    portEXIT_CRITICAL(&climateControllerInstance.spinlock);
    return 0;
  }

  if (count > maxEntries) count = maxEntries;

  /* Oldest entry is at (head - count + cap) % cap. */
  uint8_t start = (uint8_t)((hist->head + cap - hist->count) % cap);
  for (uint8_t i = 0; i < count; i++) {
    out[i].objectTemp  = hist->entries[(start + i) % cap].objectTemp;
    out[i].ambientTemp = hist->entries[(start + i) % cap].ambientTemp;
    out[i].cpuTemp     = hist->entries[(start + i) % cap].cpuTemp;
    out[i].tickMs      = hist->entries[(start + i) % cap].tickMs;
  }
  portEXIT_CRITICAL(&climateControllerInstance.spinlock);
  return count;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Singleton Factory
 * ═══════════════════════════════════════════════════════════════════════════ */

/**
 * @brief Returns the singleton IClimateController instance.
 *
 * Initialises the vtable on the first call and returns the same pointer on
 * every subsequent call. Thread-safe by design — called once during system
 * boot before any tasks are created.
 *
 * @return Pointer to the global ClimateController IClimateController interface.
 */
IClimateController *ClimateController_GetModule(void) {
  if (!climateControllerInstance.vtable_initialized) {
    /* IService base vtable. */
    climateControllerInstance.base.base.Initialize  = ClimateController_Initialize;
    climateControllerInstance.base.base.Start        = ClimateController_Start;
    climateControllerInstance.base.base.Stop         = ClimateController_Stop;
    climateControllerInstance.base.base.GetState     = ClimateController_GetState;
    climateControllerInstance.base.base.CheckHealth  = ClimateController_CheckHealth;

    /* IClimateController extended vtable. */
    climateControllerInstance.base.InitializeWithConfig      = ClimateController_InitializeWithConfig;
    climateControllerInstance.base.GetCurrentObjectTemperature  = ClimateController_GetObjectTemp;
    climateControllerInstance.base.GetCurrentAmbientTemperature = ClimateController_GetAmbientTemp;
    climateControllerInstance.base.GetCurrentCpuTemperature     = ClimateController_GetCpuTemp;
    climateControllerInstance.base.GetRelayState                = ClimateController_GetRelayState;
    climateControllerInstance.base.GetLastSensorReadTime        = ClimateController_GetLastSensorReadTime;
    climateControllerInstance.base.RequestFanAction=ClimateController_RequestFanAction;
    climateControllerInstance.base.GetFanActionStatus=ClimateController_GetFanActionStatus;
    climateControllerInstance.base.ClearExternalTemperature=ClimateController_ClearExternalTemperature;
    climateControllerInstance.base.SetExternalTemperature = ClimateController_SetExternalTemperature;
    climateControllerInstance.base.GetTemperatureSources = ClimateController_GetTemperatureSources;
    climateControllerInstance.base.GetFanMode                   = ClimateController_GetFanMode;
    climateControllerInstance.base.IsCpuThermalOverrideActive = ClimateController_IsCpuThermalOverrideActive;
    climateControllerInstance.base.GetConfig                    = ClimateController_GetConfig;
    climateControllerInstance.base.GetRelayDebounceState        = ClimateController_GetRelayDebounceState;
    climateControllerInstance.base.IsSensorStale                = ClimateController_IsSensorStale;
    climateControllerInstance.base.RunSelfTest                  = ClimateController_RunSelfTest;

    /* Initial service state. */
    climateControllerInstance.state = SERVICE_STATE_UNINITIALIZED;

    /* Initialise spinlock for temperature cache. */
    climateControllerInstance.spinlock = (portMUX_TYPE)portMUX_INITIALIZER_UNLOCKED;

    climateControllerInstance.vtable_initialized = true;
  }
  return &climateControllerInstance.base;
}
