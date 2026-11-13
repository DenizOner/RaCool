/**
 * @file Relay.c
 * @brief Modern, thread-safe Facade implementation for the Relay hardware module.
 *
 * Coordinates lower-level drivers, lifecycle management, statistics, and async timer functions.
 */

#include "Hardware/Relay/RelayInternal.h"
#include "Core/DI/IDIRegistry.h"
#include "Core/ServiceLocator/ServiceLocator.h"
#include "Core/ServiceLocator/ServiceNames.h"
#include "Hardware/PinManager/IPinManager.h"
#include "esp_log.h"
#include <string.h>

RelayServiceImpl relayService;
static portMUX_TYPE relayInstallMux = portMUX_INITIALIZER_UNLOCKED;

static bool Relay_EnsureMutex(void) {
  portENTER_CRITICAL(&relayInstallMux);
  SemaphoreHandle_t current = relayService.mutex;
  portEXIT_CRITICAL(&relayInstallMux);
  if (current) return true;
  SemaphoreHandle_t candidate = xSemaphoreCreateMutex();
  if (!candidate) return false;
  portENTER_CRITICAL(&relayInstallMux);
  if (!relayService.mutex) { relayService.mutex = candidate; candidate = NULL; }
  portEXIT_CRITICAL(&relayInstallMux);
  if (candidate) vSemaphoreDelete(candidate);
  return true;
}

// Forward Declaration for Stats Functions
esp_err_t Relay_GetStats(IRelay *self, RelayStats *out_stats);
esp_err_t Relay_ResetStats(IRelay *self);
uint32_t Relay_GetAntiShortCycleRemainingMs(IRelay *self);

/**
 * @brief Timer callback for asynchronous pulse and auto-off operations.
 */
static void Relay_TimerCallback(void *arg) {
  (void)arg;
  /* The shared ESP timer task must never wait for a relay owner or dwell time. */
  if (!relayService.mutex || xSemaphoreTake(relayService.mutex, 0) != pdTRUE) return;
  uint64_t now = esp_timer_get_time();
  if (relayService.state == SERVICE_STATE_STARTED && relayService.auto_off_pending &&
      now >= relayService.auto_off_deadline_us &&
      (!relayService.last_switch_time_us ||
       now - relayService.last_switch_time_us >= (uint64_t)RELAY_MIN_SWITCH_INTERVAL_MS * 1000ULL)) {
    bool changed = relayService.current_status != RELAY_STATE_CLOSED;
    esp_err_t result = !changed ? ESP_OK :
                       Relay_SetPhysicalStateImmediate(false);
    if (result == ESP_OK) {
      relayService.current_status = RELAY_STATE_CLOSED;
      if (changed) {
        int state = 0;
        Relay_PublishEvent(RELAY_EVENT_STATE_CHANGED, EVENT_TYPE_INFO, &state, sizeof(state));
      }
      /* Failed stop retains ownership and retries, without another physical edge. */
      Relay_CancelAutoOff();
    }
  }
  xSemaphoreGive(relayService.mutex);
}

static esp_err_t Relay_ScheduleAutoOff(uint32_t duration_ms, bool pulse) {
  esp_err_t result = Relay_Lock();
  if (result != ESP_OK) return result;
  result = Relay_Open_Internal(false);
  if (result == ESP_OK) {
    relayService.auto_off_deadline_us = (uint64_t)esp_timer_get_time() + (uint64_t)duration_ms * 1000ULL;
    result = esp_timer_start_periodic(relayService.async_timer,
                                     (uint64_t)RELAY_TIMER_CHECK_INTERVAL_MS * 1000ULL);
    if (result == ESP_OK) {
      relayService.auto_off_pending = true;
      if (pulse) relayService.current_status = RELAY_STATE_PULSING;
    } else {
      /* Scheduling failed after energizing: attempt a safe, checked close. */
      esp_err_t rollback = Relay_Close_Internal(false);
      if (rollback != ESP_OK) RELAY_LOGE("Relay_ScheduleAutoOff", "Close rollback failed: 0x%x", rollback);
    }
  }
  xSemaphoreGive(relayService.mutex);
  return result;
}

// --- Public Interface Methods ---

static esp_err_t Relay_Open(IRelay *self) {
  return Relay_Open_Internal(true);
}

static esp_err_t Relay_Close(IRelay *self) {
  return Relay_Close_Internal(true);
}

static esp_err_t Relay_Toggle(IRelay *self) {
  esp_err_t admission = Relay_Lock();
  if (admission != ESP_OK) return admission;
  esp_err_t ret;
  if (relayService.current_status == RELAY_STATE_OPEN || relayService.current_status == RELAY_STATE_PULSING) {
    ret = Relay_Close_Internal(false);
  } else {
    ret = Relay_Open_Internal(false);
  }
  if (relayService.mutex) {
    xSemaphoreGive(relayService.mutex);
  }
  return ret;
}

static esp_err_t Relay_PulseAsync(IRelay *self, uint32_t duration_ms) {
  if (duration_ms == 0) {
    duration_ms = RELAY_DEFAULT_PULSE_DURATION_MS;
  }
  if (duration_ms > RELAY_MAX_PULSE_DURATION_MS) {
    return ESP_ERR_INVALID_ARG;
  }

  return Relay_ScheduleAutoOff(duration_ms, true);
}

static esp_err_t Relay_SetAutoOff(IRelay *self, uint32_t timeout_ms) {
  if (timeout_ms == 0) {
    timeout_ms = RELAY_DEFAULT_AUTO_OFF_TIMEOUT_MS;
  }

  return Relay_ScheduleAutoOff(timeout_ms, false);
}

static bool Relay_IsOpen(IRelay *self) {
  if (Relay_Lock() != ESP_OK) return false;
  bool open = (relayService.current_status == RELAY_STATE_OPEN || relayService.current_status == RELAY_STATE_PULSING);
  if (relayService.mutex) {
    xSemaphoreGive(relayService.mutex);
  }
  return open;
}

static esp_err_t Relay_ReadState(IRelay *self, bool *energized) {
  if (self != &relayService.base || !energized) return ESP_ERR_INVALID_ARG;
  if (!relayService.mutex) return ESP_ERR_INVALID_STATE;
  if (xSemaphoreTake(relayService.mutex, 0) != pdTRUE) return ESP_ERR_TIMEOUT;
  esp_err_t result = ESP_OK;
  if (relayService.state != SERVICE_STATE_STARTED) result = ESP_ERR_INVALID_STATE;
  else if (relayService.current_status != RELAY_STATE_CLOSED &&
           relayService.current_status != RELAY_STATE_OPEN &&
           relayService.current_status != RELAY_STATE_PULSING) result = ESP_ERR_INVALID_STATE;
  else *energized = relayService.current_status != RELAY_STATE_CLOSED;
  xSemaphoreGive(relayService.mutex);
  return result;
}

static RelayState Relay_GetStatus(IRelay *self) {
  if (Relay_Lock() != ESP_OK) return RELAY_STATE_CLOSED;
  RelayState st = relayService.current_status;
  if (relayService.mutex) {
    xSemaphoreGive(relayService.mutex);
  }
  return st;
}

static esp_err_t Relay_SetMode(IRelay *self, RelayMode mode) {
  if (mode != RELAY_MODE_MANUAL && mode != RELAY_MODE_AUTOMATIC) {
    return ESP_ERR_INVALID_ARG;
  }
  esp_err_t admission = Relay_Lock();
  if (admission != ESP_OK) return admission;
  relayService.current_mode = mode;
  RELAY_LOGI("Relay_SetMode", "Relay mode changed to %d", mode);
  int mode_val = (int)mode;
  Relay_PublishEvent(RELAY_EVENT_MODE_CHANGED, EVENT_TYPE_INFO, &mode_val, sizeof(mode_val));
  if (relayService.mutex) {
    xSemaphoreGive(relayService.mutex);
  }
  return ESP_OK;
}

static RelayMode Relay_GetMode(IRelay *self) {
  if (Relay_Lock() != ESP_OK) return RELAY_MODE_MANUAL;
  RelayMode m = relayService.current_mode;
  if (relayService.mutex) {
    xSemaphoreGive(relayService.mutex);
  }
  return m;
}

static esp_err_t Relay_RunSelfTest(IRelay *self) {
  RELAY_LOGI("Relay_RunSelfTest", "Starting self-test sequence...");
  esp_err_t ret = Relay_Open(self);
  if (ret != ESP_OK) return ret;
  vTaskDelay(pdMS_TO_TICKS(100));

  if (!Relay_IsOpen(self)) return ESP_FAIL;

  ret = Relay_Close(self);
  if (ret != ESP_OK) return ret;
  vTaskDelay(pdMS_TO_TICKS(100));

  if (Relay_IsOpen(self)) return ESP_FAIL;

  RELAY_LOGI("Relay_RunSelfTest", "Self-test passed successfully!");
  return ESP_OK;
}

// --- Lifecycle Implementation ---

static int32_t Relay_Initialize(IService *self, IDIRegistry *registry) {
  RELAY_LOGI("Relay_Initialize", "Initializing Relay service...");
  if (!Relay_EnsureMutex() || Relay_Lock() != ESP_OK) return -1;
  if (relayService.state == SERVICE_STATE_INITIALIZED || relayService.state == SERVICE_STATE_STARTED ||
      relayService.state == SERVICE_STATE_STOPPED) {
    esp_err_t commands = Relay_RegisterCommands();
    xSemaphoreGive(relayService.mutex);
    return commands == ESP_OK || commands == ESP_ERR_NOT_FOUND ? 0 : -1;
  }

  esp_timer_create_args_t timer_args = {
      .callback = &Relay_TimerCallback,
      .arg = NULL,
      .name = "relay_async_timer"
  };
  if (relayService.async_timer == NULL) {
    esp_err_t result = esp_timer_create(&timer_args, &relayService.async_timer);
    if (result != ESP_OK || !relayService.async_timer) {
      xSemaphoreGive(relayService.mutex); return -1;
    }
  }

  relayService.state = SERVICE_STATE_INITIALIZED;
  relayService.current_status = RELAY_STATE_CLOSED;
  relayService.current_mode = (RelayMode)RELAY_DEFAULT_CONTROL_MODE;
  relayService.eventBus = ServiceLocator_GetEventBus();
  relayService.last_switch_time_us = 0;
  relayService.energized_start_time_us = 0;
  relayService.auto_off_pending = false;
  memset(&relayService.stats, 0, sizeof(RelayStats));

  IPinManager *pinMgr = (IPinManager *)ServiceLocator_GetService(SVC_NAME_PIN_MANAGER);
  if (pinMgr) {
    relayService.relay_pin = pinMgr->GetPin(pinMgr, PIN_FUNC_RELAY);
  } else {
    relayService.relay_pin = GPIO_NUM_NC;
  }

  esp_err_t commands = Relay_RegisterCommands();
  xSemaphoreGive(relayService.mutex);
  if (commands != ESP_OK && commands != ESP_ERR_NOT_FOUND) return -1;
  RELAY_LOGI("Relay_Initialize", "Relay service initialized successfully.");
  return 0;
}

static int32_t Relay_Start(IService *self) {
  if (Relay_Lock() != ESP_OK) return -1;
  if (relayService.state == SERVICE_STATE_STARTED) { xSemaphoreGive(relayService.mutex); return 0; }
  if ((relayService.state != SERVICE_STATE_INITIALIZED && relayService.state != SERVICE_STATE_STOPPED) ||
      relayService.relay_pin < 0 || relayService.relay_pin >= GPIO_NUM_MAX) {
    RELAY_LOGE("Relay_Start", "Relay pin not configured!");
    xSemaphoreGive(relayService.mutex); return -1;
  }

  RELAY_LOGI("Relay_Start", "Starting Relay service on GPIO %d", relayService.relay_pin);
  if (gpio_reset_pin(relayService.relay_pin) != ESP_OK) {
    xSemaphoreGive(relayService.mutex); return -1;
  }

  gpio_config_t io_conf = {
      .pin_bit_mask = (1ULL << relayService.relay_pin),
      .mode = GPIO_MODE_INPUT_OUTPUT,
      .pull_up_en = GPIO_PULLUP_DISABLE,
      .pull_down_en = GPIO_PULLDOWN_ENABLE,
      .intr_type = GPIO_INTR_DISABLE,
  };
  esp_err_t gpio_ret = gpio_config(&io_conf);
  if (gpio_ret != ESP_OK) {
    RELAY_LOGE("Relay_Start", "GPIO Config failed for pin %d: 0x%x", relayService.relay_pin, gpio_ret);
    xSemaphoreGive(relayService.mutex); return -1;
  }

  if (Relay_SetPhysicalState(false) != ESP_OK) {
    xSemaphoreGive(relayService.mutex); return -1;
  }
  relayService.current_status = RELAY_STATE_CLOSED;
  relayService.state = SERVICE_STATE_STARTED;
  xSemaphoreGive(relayService.mutex);

  RELAY_LOGI("Relay_Start", "Relay service started successfully.");
  return 0;
}

static int32_t Relay_Stop(IService *self) {
  RELAY_LOGI("Relay_Stop", "Stopping Relay service...");
  if (Relay_Lock() != ESP_OK) return -1;
  if (relayService.state != SERVICE_STATE_STARTED) { xSemaphoreGive(relayService.mutex); return 0; }
  esp_err_t result = Relay_CancelAutoOff();
  if (result == ESP_OK) result = Relay_SetPhysicalState(false);
  if (result != ESP_OK) { xSemaphoreGive(relayService.mutex); return -1; }
  relayService.current_status = RELAY_STATE_CLOSED;
  relayService.state = SERVICE_STATE_STOPPED;
  xSemaphoreGive(relayService.mutex);
  RELAY_LOGI("Relay_Stop", "Relay service stopped.");
  return 0;
}

static ServiceState Relay_GetServiceState(IService *self) {
  if (Relay_Lock() != ESP_OK) return SERVICE_STATE_ERROR;
  ServiceState state = relayService.state;
  xSemaphoreGive(relayService.mutex);
  return state;
}

static int32_t Relay_CheckHealth(IService *self) {
  return Relay_GetServiceState(self) == SERVICE_STATE_STARTED ? 0 : -1;
}

IRelay *Relay_GetModule(void) {
  static bool initialized = false;
  portENTER_CRITICAL(&relayInstallMux);
  if (!initialized) {
    relayService.base.base.Initialize = Relay_Initialize;
    relayService.base.base.Start = Relay_Start;
    relayService.base.base.Stop = Relay_Stop;
    relayService.base.base.GetState = Relay_GetServiceState;
    relayService.base.base.CheckHealth = Relay_CheckHealth;

    relayService.base.Open = Relay_Open;
    relayService.base.Close = Relay_Close;
    relayService.base.Toggle = Relay_Toggle;
    relayService.base.PulseAsync = Relay_PulseAsync;
    relayService.base.SetAutoOff = Relay_SetAutoOff;
    relayService.base.IsOpen = Relay_IsOpen;
    relayService.base.GetStatus = Relay_GetStatus;
    relayService.base.ReadState = Relay_ReadState;
    relayService.base.SetMode = Relay_SetMode;
    relayService.base.GetMode = Relay_GetMode;
    relayService.base.GetStats = Relay_GetStats;
    relayService.base.ResetStats = Relay_ResetStats;
    relayService.base.GetAntiShortCycleRemainingMs = Relay_GetAntiShortCycleRemainingMs;
    relayService.base.RunSelfTest = Relay_RunSelfTest;

    relayService.state = SERVICE_STATE_UNINITIALIZED;
    initialized = true;
  }
  portEXIT_CRITICAL(&relayInstallMux);
  return &relayService.base;
}
