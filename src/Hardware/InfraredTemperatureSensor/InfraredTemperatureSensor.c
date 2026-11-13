#include "Hardware/InfraredTemperatureSensor/IInfraredTemperatureSensor.h"
#include "Hardware/InfraredTemperatureSensor/InfraredTemperatureSensorConfig.h"
#include "Hardware/InfraredTemperatureSensor/InfraredTemperatureSensorConsoleCommands.h"
#include "Hardware/I2CManager/II2CManager.h"
#include "Management/ConfigurationManager/IConfigurationManager.h"
#include "Core/EventBus/IEventBus.h"
#include "Core/ServiceLocator/ServiceLocator.h"
#include "Core/ServiceLocator/ServiceNames.h"
#include "Management/Watchdog/WatchdogWorker.h"
#include "Communication/DebugConsole/IDebugConsole.h"
#include "Communication/DebugConsole/Core/ConsoleColors.h"

#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <math.h>
#include <string.h>

static const char *TAG = "InfraredTemp";

#define LOGD(FUNC, ...) ESP_LOGD(TAG, "[" FUNC "] " __VA_ARGS__)
#define LOGI(FUNC, ...) ESP_LOGI(TAG, "[" FUNC "] " __VA_ARGS__)
#define LOGW(FUNC, ...) ESP_LOGW(TAG, "[" FUNC "] " __VA_ARGS__)
#define LOGE(FUNC, ...) ESP_LOGE(TAG, "[" FUNC "] " __VA_ARGS__)

typedef struct {
  IInfraredTemperatureSensor base;
  _Atomic ServiceState state;
  InfraredTemperatureSensorConfig config;
  float cached_object_temp;
  float cached_ambient_temp;
  IRTempSample sample;
  bool sample_valid;
  float last_published_obj_temp;
  float last_published_amb_temp;
  _Atomic esp_err_t last_i2c_error;
  atomic_uint consecutive_errors;
  IRTempStats stats;
  SemaphoreHandle_t cache_mutex;
  SemaphoreHandle_t control_mutex;
  SemaphoreHandle_t measurement_mutex;
  WatchdogWorker worker;
  atomic_bool stopping;
  atomic_uint polling_interval_ms;
  int64_t last_publish_time_us; /**< esp_timer_get_time() at last EVENT_IR_TEMP_UPDATED publish.
                                  *   Used to enforce max_publish_interval_ms heartbeat. */

  II2CManager *i2c_mgr;
  i2c_master_dev_handle_t i2c_handle;
} InfraredTempSensorImpl;

static InfraredTempSensorImpl irSensorInstance;

static bool IRTemp_IsInitialized(void) {
  ServiceState state = atomic_load(&irSensorInstance.state);
  return state == SERVICE_STATE_INITIALIZED || state == SERVICE_STATE_STARTED || state == SERVICE_STATE_STOPPED;
}

static void IRTemp_RecordFailure(IRTempFailureKind kind, esp_err_t error) {
  /* Only the measurement owner writes this counter; readers use atomic loads. */
  if (atomic_load(&irSensorInstance.consecutive_errors) < UINT32_MAX)
    atomic_fetch_add(&irSensorInstance.consecutive_errors, 1);
  if (irSensorInstance.cache_mutex && xSemaphoreTake(irSensorInstance.cache_mutex,
      pdMS_TO_TICKS(IRTEMP_CACHE_LOCK_TIMEOUT_MS)) == pdTRUE) {
    irSensorInstance.stats.last_failure_time_us = esp_timer_get_time();
    irSensorInstance.stats.last_failure_error = error;
    irSensorInstance.stats.last_failure_kind = kind;
    xSemaphoreGive(irSensorInstance.cache_mutex);
  }
}

/* Initialization is serialized by the service owner before publication.
 * Resources remain owned across Stop/Start and failed initialization retries.
 * Lock order: control -> measurement -> cache; the worker never takes control.
 * Stop acknowledges at a worker checkpoint outside measurement/cache locks. */

// Forward declarations
static void IRTemp_StartMeasurement(void);
static bool ProcessTemperature(uint16_t raw_data, bool is_object);
static uint8_t CalculatePEC(const uint8_t *data, size_t len);

/**
 * @brief Calculates SMBus CRC-8 (PEC) for MLX90614 validation.
 */
static uint8_t CalculatePEC(const uint8_t *data, size_t len) {
  uint8_t crc = 0;
  for (size_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (uint8_t j = 0; j < 8; j++) {
      if (crc & 0x80) {
        crc = (crc << 1) ^ IRTEMP_PEC_POLYNOMIAL;
      } else {
        crc <<= 1;
      }
    }
  }
  return crc;
}

/**
 * @brief Helper function to convert raw 16-bit register value to Celsius float.
 */
static inline float RawToCelsius(uint16_t raw_data) {
  uint16_t val = raw_data & 0x7FFF;
  float temp_k = (float)val * IRTEMP_KELVIN_FACTOR;
  return temp_k - IRTEMP_KELVIN_OFFSET;
}

static bool ProcessTemperature(uint16_t raw_data, bool is_object) {
  if (raw_data & 0x8000) {
    LOGW("ProcessTemperature", "MLX90614 error flag bit set (raw=0x%04X), ignoring sample", raw_data);
    return false;
  }

  float temp = RawToCelsius(raw_data);

  if (temp < irSensorInstance.config.temp_range_min || temp > irSensorInstance.config.temp_range_max) {
    LOGW("ProcessTemperature", "Out-of-range temp sample ignored: %.2f C", temp);
    return false;
  }

  IConfigurationManager *config = (IConfigurationManager *)ServiceLocator_GetService(SVC_NAME_CONFIGURATION_MANAGER);
  if (config) {
    SystemSettings snapshot;
    const SystemSettings *settings = config->GetSnapshot &&
        config->GetSnapshot(config, &snapshot) == ESP_OK ? &snapshot : NULL;
    if (settings) {
      temp += is_object ? settings->calibration.ir_object_cal : settings->calibration.ir_ambient_cal;
    }
  }

  if (!isfinite(temp)) return false;

  if (irSensorInstance.cache_mutex) {
    if (xSemaphoreTake(irSensorInstance.cache_mutex, pdMS_TO_TICKS(IRTEMP_CACHE_LOCK_TIMEOUT_MS)) != pdTRUE) return false;
    float alpha = irSensorInstance.config.ema_alpha;
    if (is_object) {
      if (irSensorInstance.cached_object_temp <= IRTEMP_CACHE_UNINITIALIZED + 1.0f) {
        irSensorInstance.cached_object_temp = temp;
        irSensorInstance.stats.min_object_temp = temp;
        irSensorInstance.stats.max_object_temp = temp;
      } else {
        irSensorInstance.cached_object_temp = alpha * temp + (1.0f - alpha) * irSensorInstance.cached_object_temp;
        if (temp < irSensorInstance.stats.min_object_temp) irSensorInstance.stats.min_object_temp = temp;
        if (temp > irSensorInstance.stats.max_object_temp) irSensorInstance.stats.max_object_temp = temp;
      }
    } else {
      if (irSensorInstance.cached_ambient_temp <= IRTEMP_CACHE_UNINITIALIZED + 1.0f) {
        irSensorInstance.cached_ambient_temp = temp;
        irSensorInstance.stats.min_ambient_temp = temp;
        irSensorInstance.stats.max_ambient_temp = temp;
      } else {
        irSensorInstance.cached_ambient_temp = alpha * temp + (1.0f - alpha) * irSensorInstance.cached_ambient_temp;
        if (temp < irSensorInstance.stats.min_ambient_temp) irSensorInstance.stats.min_ambient_temp = temp;
        if (temp > irSensorInstance.stats.max_ambient_temp) irSensorInstance.stats.max_ambient_temp = temp;
      }
    }
    xSemaphoreGive(irSensorInstance.cache_mutex);
    return true;
  }
  return false;
}

static void IRTemp_MeasureLocked(void) {
  if (irSensorInstance.state != SERVICE_STATE_STARTED || !irSensorInstance.i2c_mgr || !irSensorInstance.i2c_handle) return;

  if (irSensorInstance.cache_mutex) {
    if (xSemaphoreTake(irSensorInstance.cache_mutex, pdMS_TO_TICKS(IRTEMP_CACHE_LOCK_TIMEOUT_MS)) != pdTRUE) return;
    irSensorInstance.stats.total_reads++;
    xSemaphoreGive(irSensorInstance.cache_mutex);
  }

  /* Read 3 bytes: Data Low, Data High, PEC */
  bool object_valid = false;
  bool ambient_valid = false;
  uint8_t data_obj[3] = {0};
  esp_err_t err = irSensorInstance.i2c_mgr->ReadRegister(irSensorInstance.i2c_mgr, irSensorInstance.i2c_handle, irSensorInstance.config.reg_object_temp, data_obj, 3);
  irSensorInstance.last_i2c_error = err;

  if (err == ESP_OK) {
    uint8_t addr_write = (irSensorInstance.config.i2c_address << 1);
    uint8_t cmd = irSensorInstance.config.reg_object_temp;
    uint8_t addr_read = addr_write | 1;
    uint8_t pec_buf[5] = {addr_write, cmd, addr_read, data_obj[0], data_obj[1]};
    uint8_t expected_pec = CalculatePEC(pec_buf, 5);

    if (data_obj[2] == expected_pec) {
      uint16_t raw = (data_obj[1] << 8) | data_obj[0];
      object_valid = ProcessTemperature(raw, true);
      if (!object_valid) IRTemp_RecordFailure(IRTEMP_FAILURE_VALUE, ESP_ERR_INVALID_RESPONSE);
    } else {
      LOGW("IRTemp_StartMeasurement", "Obj PEC validation failed. Expected 0x%02X, got 0x%02X", expected_pec, data_obj[2]);
      IRTemp_RecordFailure(IRTEMP_FAILURE_PEC, ESP_ERR_INVALID_RESPONSE);
      if (irSensorInstance.cache_mutex) {
        if (xSemaphoreTake(irSensorInstance.cache_mutex, pdMS_TO_TICKS(IRTEMP_CACHE_LOCK_TIMEOUT_MS)) != pdTRUE) return;
        irSensorInstance.stats.pec_errors++;
        xSemaphoreGive(irSensorInstance.cache_mutex);
      }
    }
  } else {
    IRTemp_RecordFailure(IRTEMP_FAILURE_I2C, err);
    if (irSensorInstance.cache_mutex) {
      if (xSemaphoreTake(irSensorInstance.cache_mutex, pdMS_TO_TICKS(IRTEMP_CACHE_LOCK_TIMEOUT_MS)) != pdTRUE) return;
      irSensorInstance.stats.i2c_errors++;
      xSemaphoreGive(irSensorInstance.cache_mutex);
    }
    static uint32_t log_throttle = 0;
    if (log_throttle++ % 10 == 0) {
      LOGW("IRTemp_StartMeasurement", "Obj read failed: %s (throttled)", esp_err_to_name(err));
    }
  }

  uint8_t data_amb[3] = {0};
  err = irSensorInstance.i2c_mgr->ReadRegister(irSensorInstance.i2c_mgr, irSensorInstance.i2c_handle, irSensorInstance.config.reg_ambient_temp, data_amb, 3);
  irSensorInstance.last_i2c_error = err;

  if (err == ESP_OK) {
    uint8_t addr_write = (irSensorInstance.config.i2c_address << 1);
    uint8_t cmd = irSensorInstance.config.reg_ambient_temp;
    uint8_t addr_read = addr_write | 1;
    uint8_t pec_buf[5] = {addr_write, cmd, addr_read, data_amb[0], data_amb[1]};
    uint8_t expected_pec = CalculatePEC(pec_buf, 5);

    if (data_amb[2] == expected_pec) {
      uint16_t raw = (data_amb[1] << 8) | data_amb[0];
      ambient_valid = ProcessTemperature(raw, false);
      if (!ambient_valid) IRTemp_RecordFailure(IRTEMP_FAILURE_VALUE, ESP_ERR_INVALID_RESPONSE);
    } else {
      LOGW("IRTemp_StartMeasurement", "Amb PEC validation failed. Expected 0x%02X, got 0x%02X", expected_pec, data_amb[2]);
      IRTemp_RecordFailure(IRTEMP_FAILURE_PEC, ESP_ERR_INVALID_RESPONSE);
      if (irSensorInstance.cache_mutex) {
        if (xSemaphoreTake(irSensorInstance.cache_mutex, pdMS_TO_TICKS(IRTEMP_CACHE_LOCK_TIMEOUT_MS)) != pdTRUE) return;
        irSensorInstance.stats.pec_errors++;
        xSemaphoreGive(irSensorInstance.cache_mutex);
      }
    }
  } else {
    IRTemp_RecordFailure(IRTEMP_FAILURE_I2C, err);
    if (irSensorInstance.cache_mutex) {
      if (xSemaphoreTake(irSensorInstance.cache_mutex, pdMS_TO_TICKS(IRTEMP_CACHE_LOCK_TIMEOUT_MS)) != pdTRUE) return;
      irSensorInstance.stats.i2c_errors++;
      xSemaphoreGive(irSensorInstance.cache_mutex);
    }
    static uint32_t amb_log_throttle = 0;
    if (amb_log_throttle++ % 10 == 0) {
      LOGW("IRTemp_StartMeasurement", "Amb read failed: %s (throttled)", esp_err_to_name(err));
    }
  }

  /* A cached value is not a new measurement. Publishing a partial/failed pair
   * would renew ClimateController's freshness timestamp with stale data.
   * Only a complete valid cycle can clear a persistent channel failure. */
  if (!object_valid || !ambient_valid) return;
  irSensorInstance.consecutive_errors = 0;

  /* Publish the timestamped complete measurement if:
   *  a) Temperature delta exceeds threshold (normal change-driven path), OR
   *  b) max_publish_interval_ms has elapsed since last publish (heartbeat path
   *     that prevents ClimateController stale detection on stable temperatures).
   */
  if (irSensorInstance.cache_mutex) {
    float temps[2];
    IRTempSample sample = {0};
    bool should_publish = false;
    if (xSemaphoreTake(irSensorInstance.cache_mutex, pdMS_TO_TICKS(IRTEMP_CACHE_LOCK_TIMEOUT_MS)) != pdTRUE) return;
    temps[0] = irSensorInstance.cached_object_temp;
    temps[1] = irSensorInstance.cached_ambient_temp;
    sample.temperatures[0] = temps[0];
    sample.temperatures[1] = temps[1];
    sample.measured_time_us = esp_timer_get_time();
    sample.measured_tick = xTaskGetTickCount();
    irSensorInstance.sample = sample;
    irSensorInstance.sample_valid = true;

    float delta_obj = fabsf(temps[0] - irSensorInstance.last_published_obj_temp);
    float delta_amb = fabsf(temps[1] - irSensorInstance.last_published_amb_temp);

    /* Delta-driven publish. */
    if (delta_obj >= irSensorInstance.config.event_delta_threshold ||
        delta_amb >= irSensorInstance.config.event_delta_threshold ||
        irSensorInstance.last_published_obj_temp <= IRTEMP_CACHE_UNINITIALIZED + 1.0f) {
      should_publish = true;
    }

    /* Heartbeat-driven publish: force publish if max interval elapsed. */
    if (!should_publish && irSensorInstance.config.max_publish_interval_ms > 0) {
      int64_t now_us    = esp_timer_get_time();
      int64_t elapsed_ms = (now_us - irSensorInstance.last_publish_time_us) / 1000;
      if (elapsed_ms >= (int64_t)irSensorInstance.config.max_publish_interval_ms) {
        should_publish = true;
      }
    }

    xSemaphoreGive(irSensorInstance.cache_mutex);

    if (should_publish) {
      IEventBus *eb = (IEventBus *)ServiceLocator_GetService(SVC_NAME_EVENT_BUS);
      if (eb) {
        BusEvent ev = {
          .eventName = EVENT_IR_TEMP_UPDATED,
          .type      = EVENT_TYPE_INFO,
          .data      = &sample,
          .dataSize  = sizeof(sample)
        };
        if (eb->Publish && eb->Publish(ev) == ESP_OK) {
          irSensorInstance.last_published_obj_temp = temps[0];
          irSensorInstance.last_published_amb_temp = temps[1];
          irSensorInstance.last_publish_time_us = esp_timer_get_time();
        }
      }
    }
  }
}

static void IRTemp_StartMeasurement(void) {
  if (atomic_load(&irSensorInstance.stopping) || !irSensorInstance.measurement_mutex) return;
  if (xSemaphoreTake(irSensorInstance.measurement_mutex,
      pdMS_TO_TICKS(IRTEMP_MEASUREMENT_LOCK_TIMEOUT_MS)) != pdTRUE) return;
  if (!atomic_load(&irSensorInstance.stopping)) IRTemp_MeasureLocked();
  xSemaphoreGive(irSensorInstance.measurement_mutex);
}

static void IRTemp_Worker(void *arg) {
  (void)arg;
  int64_t last_poll_us = esp_timer_get_time();
  for (;;) {
    WatchdogWorker_Checkpoint(&irSensorInstance.worker, NULL, 0);
    int64_t now_us = esp_timer_get_time();
    uint32_t interval_ms = atomic_load(&irSensorInstance.polling_interval_ms);
    if (now_us - last_poll_us >= (int64_t)interval_ms * 1000) {
      last_poll_us = now_us;
      IRTemp_StartMeasurement();
    }
    /* Bounded sleep keeps Stop responsive even for a 60-second poll period.
     * Missed measurement periods coalesce; no burst of queued timer callbacks. */
    vTaskDelay(pdMS_TO_TICKS(IRTEMP_WORKER_CHECKPOINT_MS));
  }
}

/* Lifecycle control and measurement ownership must be held by the caller. */
static esp_err_t IRTemp_AttachDevice(void) {
  if (irSensorInstance.i2c_handle) return ESP_OK;
  if (!irSensorInstance.i2c_mgr || !irSensorInstance.i2c_mgr->AddDevice ||
      !irSensorInstance.i2c_mgr->RemoveDevice) return ESP_ERR_INVALID_STATE;
  const i2c_device_config_t config = {
    .device_address = irSensorInstance.config.i2c_address,
    .scl_speed_hz = irSensorInstance.config.i2c_freq_hz,
  };
  i2c_master_dev_handle_t handle = NULL;
  esp_err_t result = irSensorInstance.i2c_mgr->AddDevice(irSensorInstance.i2c_mgr, &config, &handle);
  if (result == ESP_OK && !handle) result = ESP_ERR_INVALID_STATE;
  if (result == ESP_OK) irSensorInstance.i2c_handle = handle;
  return result;
}

static int32_t IRTemp_Start(IService *self) {
  if (self != &irSensorInstance.base.base || !irSensorInstance.control_mutex) return -1;
  if (xSemaphoreTake(irSensorInstance.control_mutex,
      pdMS_TO_TICKS(IRTEMP_CONTROL_TIMEOUT_MS)) != pdTRUE) return -1;
  int32_t result = -1;
  ServiceState previous = atomic_load(&irSensorInstance.state);
  if (atomic_load(&irSensorInstance.stopping)) goto done;
  if (previous == SERVICE_STATE_STARTED) { result = 0; goto done; }
  if (previous != SERVICE_STATE_INITIALIZED && previous != SERVICE_STATE_STOPPED) goto done;
  if (xSemaphoreTake(irSensorInstance.measurement_mutex,
      pdMS_TO_TICKS(IRTEMP_MEASUREMENT_LOCK_TIMEOUT_MS)) != pdTRUE) goto done;
  esp_err_t attached = IRTemp_AttachDevice();
  xSemaphoreGive(irSensorInstance.measurement_mutex);
  if (attached != ESP_OK) { irSensorInstance.last_i2c_error = attached; goto done; }

  if (WatchdogWorker_Start(&irSensorInstance.worker, IRTemp_Worker, "IRTemp",
      IRTEMP_WORKER_STACK_SIZE, NULL, IRTEMP_WORKER_PRIORITY) != 0) {
    goto done;
  }
  atomic_store(&irSensorInstance.state, SERVICE_STATE_STARTED);
  result = 0;
done:
  xSemaphoreGive(irSensorInstance.control_mutex);
  return result;
}

static int32_t IRTemp_Stop(IService *self) {
  if (self != &irSensorInstance.base.base || !irSensorInstance.control_mutex) return -1;
  if (xSemaphoreTake(irSensorInstance.control_mutex,
      pdMS_TO_TICKS(IRTEMP_CONTROL_TIMEOUT_MS)) != pdTRUE) return -1;
  int32_t result = -1;
  ServiceState state = atomic_load(&irSensorInstance.state);
  if (state != SERVICE_STATE_STARTED && state != SERVICE_STATE_INITIALIZED &&
      state != SERVICE_STATE_STOPPED) goto done;
  atomic_store(&irSensorInstance.stopping, true);
  if (WatchdogWorker_Stop(&irSensorInstance.worker,
      pdMS_TO_TICKS(IRTEMP_CONTROL_TIMEOUT_MS)) != 0) goto done;
  /* Also wait for a synchronous TriggerMeasurement/SelfTest caller. No new
   * measurement can enter while stopping is set, and nothing is freed on timeout. */
  if (xSemaphoreTake(irSensorInstance.measurement_mutex,
      pdMS_TO_TICKS(IRTEMP_MEASUREMENT_LOCK_TIMEOUT_MS)) != pdTRUE) goto done;
  if (irSensorInstance.i2c_handle) {
    esp_err_t removed = irSensorInstance.i2c_mgr->RemoveDevice(
        irSensorInstance.i2c_mgr, irSensorInstance.i2c_handle);
    if (removed != ESP_OK) {
      irSensorInstance.last_i2c_error = removed;
      xSemaphoreGive(irSensorInstance.measurement_mutex);
      goto done;
    }
    irSensorInstance.i2c_handle = NULL;
  }
  atomic_store(&irSensorInstance.state, SERVICE_STATE_STOPPED);
  atomic_store(&irSensorInstance.stopping, false);
  xSemaphoreGive(irSensorInstance.measurement_mutex);
  result = 0;
done:
  xSemaphoreGive(irSensorInstance.control_mutex);
  return result;
}

static ServiceState IRTemp_GetState(IService *self) {
  return irSensorInstance.state;
}

static int32_t IRTemp_CheckHealth(IService *self) {
  if (irSensorInstance.state != SERVICE_STATE_STARTED || atomic_load(&irSensorInstance.stopping) ||
      !WatchdogWorker_Healthy(&irSensorInstance.worker)) {
    return -1;
  }
  if (irSensorInstance.consecutive_errors > 5) {
    return -2;
  }
  return 0;
}

static esp_err_t IRTemp_SelfTestLocked(void) {
  if (!irSensorInstance.i2c_mgr || !irSensorInstance.i2c_handle) {
    return ESP_ERR_INVALID_STATE;
  }
  uint8_t data[3] = {0};
  esp_err_t err = irSensorInstance.i2c_mgr->ReadRegister(irSensorInstance.i2c_mgr, irSensorInstance.i2c_handle, irSensorInstance.config.reg_object_temp, data, 3);
  if (err != ESP_OK) {
    return err;
  }
  uint8_t addr_write = irSensorInstance.config.i2c_address << 1;
  uint8_t pec_buf[5] = {addr_write, irSensorInstance.config.reg_object_temp,
                        (uint8_t)(addr_write | 1), data[0], data[1]};
  if (data[2] != CalculatePEC(pec_buf, sizeof(pec_buf))) {
    return ESP_ERR_INVALID_RESPONSE;
  }
  uint16_t raw = (data[1] << 8) | data[0];
  if (raw == 0 || (raw & 0x8000)) {
    return ESP_ERR_INVALID_RESPONSE;
  }
  return ESP_OK;
}

static esp_err_t IRTemp_SelfTest(IInfraredTemperatureSensor *self) {
  if (self != &irSensorInstance.base) return ESP_ERR_INVALID_ARG;
  if (!IRTemp_IsInitialized() || !irSensorInstance.measurement_mutex || atomic_load(&irSensorInstance.stopping)) return ESP_ERR_INVALID_STATE;
  if (xSemaphoreTake(irSensorInstance.measurement_mutex,
      pdMS_TO_TICKS(IRTEMP_MEASUREMENT_LOCK_TIMEOUT_MS)) != pdTRUE) return ESP_ERR_TIMEOUT;
  esp_err_t result = atomic_load(&irSensorInstance.stopping)
      ? ESP_ERR_INVALID_STATE : IRTemp_SelfTestLocked();
  xSemaphoreGive(irSensorInstance.measurement_mutex);
  return result;
}

static esp_err_t IRTemp_GetLastError(IInfraredTemperatureSensor *self) {
  return irSensorInstance.last_i2c_error;
}

static esp_err_t IRTemp_GetStats(IInfraredTemperatureSensor *self, IRTempStats *stats) {
  if (self != &irSensorInstance.base || !stats) return ESP_ERR_INVALID_ARG;
  if (!IRTemp_IsInitialized() || !irSensorInstance.cache_mutex) return ESP_ERR_INVALID_STATE;
  if (xSemaphoreTake(irSensorInstance.cache_mutex,
      pdMS_TO_TICKS(IRTEMP_CACHE_LOCK_TIMEOUT_MS)) != pdTRUE) return ESP_ERR_TIMEOUT;
  *stats = irSensorInstance.stats;
  xSemaphoreGive(irSensorInstance.cache_mutex);
  return ESP_OK;
}

static esp_err_t IRTemp_ResetStats(IInfraredTemperatureSensor *self) {
  if (self != &irSensorInstance.base) return ESP_ERR_INVALID_ARG;
  if (!IRTemp_IsInitialized() || !irSensorInstance.cache_mutex) return ESP_ERR_INVALID_STATE;
  if (xSemaphoreTake(irSensorInstance.cache_mutex,
      pdMS_TO_TICKS(IRTEMP_CACHE_LOCK_TIMEOUT_MS)) != pdTRUE) return ESP_ERR_TIMEOUT;
  memset(&irSensorInstance.stats, 0, sizeof(IRTempStats));
  xSemaphoreGive(irSensorInstance.cache_mutex);
  return ESP_OK;
}

static esp_err_t IRTemp_SetLimits(IInfraredTemperatureSensor *self, float min_temp, float max_temp) {
  if (self != &irSensorInstance.base || !isfinite(min_temp) || !isfinite(max_temp) || min_temp >= max_temp)
    return ESP_ERR_INVALID_ARG;
  if (!IRTemp_IsInitialized() || atomic_load(&irSensorInstance.stopping) || !irSensorInstance.measurement_mutex) return ESP_ERR_INVALID_STATE;
  if (xSemaphoreTake(irSensorInstance.measurement_mutex,
      pdMS_TO_TICKS(IRTEMP_MEASUREMENT_LOCK_TIMEOUT_MS)) != pdTRUE) return ESP_ERR_TIMEOUT;
  irSensorInstance.config.temp_range_min = min_temp;
  irSensorInstance.config.temp_range_max = max_temp;
  xSemaphoreGive(irSensorInstance.measurement_mutex);
  return ESP_OK;
}

static esp_err_t IRTemp_SetPollingInterval(IInfraredTemperatureSensor *self, uint32_t interval_ms) {
  if (self != &irSensorInstance.base || interval_ms < IRTEMP_POLL_MIN_MS || interval_ms > IRTEMP_POLL_MAX_MS)
    return ESP_ERR_INVALID_ARG;
  if (!IRTemp_IsInitialized() || atomic_load(&irSensorInstance.stopping) || !irSensorInstance.measurement_mutex) return ESP_ERR_INVALID_STATE;
  if (xSemaphoreTake(irSensorInstance.measurement_mutex,
      pdMS_TO_TICKS(IRTEMP_MEASUREMENT_LOCK_TIMEOUT_MS)) != pdTRUE) return ESP_ERR_TIMEOUT;
  irSensorInstance.config.polling_interval_ms = interval_ms;
  atomic_store(&irSensorInstance.polling_interval_ms, interval_ms);
  xSemaphoreGive(irSensorInstance.measurement_mutex);
  return ESP_OK;
}

static void IRTemp_TriggerMeasurement(IInfraredTemperatureSensor *self) {
  if (self == &irSensorInstance.base) IRTemp_StartMeasurement();
}

static uint32_t IRTemp_GetPollingInterval(IInfraredTemperatureSensor *self) {
  return self == &irSensorInstance.base && IRTemp_IsInitialized()
      ? atomic_load(&irSensorInstance.polling_interval_ms) : 0;
}

static esp_err_t IRTemp_ReadRawRegisters(IInfraredTemperatureSensor *self, IRTempRawRegisters *out) {
  if (self != &irSensorInstance.base || !out) return ESP_ERR_INVALID_ARG;
  if (!IRTemp_IsInitialized() || !irSensorInstance.measurement_mutex ||
      atomic_load(&irSensorInstance.stopping)) return ESP_ERR_INVALID_STATE;
  if (xSemaphoreTake(irSensorInstance.measurement_mutex,
      pdMS_TO_TICKS(IRTEMP_MEASUREMENT_LOCK_TIMEOUT_MS)) != pdTRUE) return ESP_ERR_TIMEOUT;
  esp_err_t result = ESP_ERR_INVALID_STATE;
  if (!atomic_load(&irSensorInstance.stopping) && irSensorInstance.i2c_mgr && irSensorInstance.i2c_handle) {
    IRTempRawRegisters candidate;
    const uint8_t regs[] = {0x00, 0x01, irSensorInstance.config.reg_ambient_temp,
                           irSensorInstance.config.reg_object_temp, 0x08, 0x23, 0x24};
    uint8_t *values[] = {candidate.raw_ir1, candidate.raw_ir2, candidate.ambient,
                        candidate.object1, candidate.object2, candidate.config1, candidate.emissivity};
    result = ESP_OK;
    for (size_t i = 0; i < sizeof(regs) / sizeof(regs[0]); ++i) {
      result = irSensorInstance.i2c_mgr->ReadRegister(irSensorInstance.i2c_mgr,
          irSensorInstance.i2c_handle, regs[i], values[i], 3);
      if (result != ESP_OK) break;
    }
    if (result == ESP_OK) *out = candidate;
  }
  xSemaphoreGive(irSensorInstance.measurement_mutex);
  return result;
}

static float IRTemp_ReadCached(IInfraredTemperatureSensor *self, bool object) {
  if (self != &irSensorInstance.base || !IRTemp_IsInitialized() || !irSensorInstance.cache_mutex) return IRTEMP_CACHE_UNINITIALIZED;
  if (xSemaphoreTake(irSensorInstance.cache_mutex,
      pdMS_TO_TICKS(IRTEMP_CACHE_LOCK_TIMEOUT_MS)) != pdTRUE) return irSensorInstance.config.error_temperature;
  float temp = object ? irSensorInstance.cached_object_temp : irSensorInstance.cached_ambient_temp;
  xSemaphoreGive(irSensorInstance.cache_mutex);
  return temp;
}

static float IRTemp_ReadObjectTemperature(IInfraredTemperatureSensor *self) {
  return IRTemp_ReadCached(self, true);
}

static esp_err_t IRTemp_ReadSample(IInfraredTemperatureSensor *self, IRTempSample *out) {
  if (self != &irSensorInstance.base || !out) return ESP_ERR_INVALID_ARG;
  if (!IRTemp_IsInitialized() || !irSensorInstance.cache_mutex) return ESP_ERR_INVALID_STATE;
  if (xSemaphoreTake(irSensorInstance.cache_mutex,
      pdMS_TO_TICKS(IRTEMP_CACHE_LOCK_TIMEOUT_MS)) != pdTRUE) return ESP_ERR_TIMEOUT;
  esp_err_t result = ESP_ERR_INVALID_STATE;
  if (irSensorInstance.sample_valid) {
    *out = irSensorInstance.sample;
    result = ESP_OK;
  }
  xSemaphoreGive(irSensorInstance.cache_mutex);
  return result;
}

static float IRTemp_ReadAmbientTemperature(IInfraredTemperatureSensor *self) {
  return IRTemp_ReadCached(self, false);
}

static int32_t IRTemp_Initialize(IService *self, IDIRegistry *registry) {
  (void)registry;
  if (self != &irSensorInstance.base.base) return -1;
  if (!irSensorInstance.control_mutex) irSensorInstance.control_mutex = xSemaphoreCreateMutex();
  if (!irSensorInstance.control_mutex) return -1;
  if (xSemaphoreTake(irSensorInstance.control_mutex,
      pdMS_TO_TICKS(IRTEMP_CONTROL_TIMEOUT_MS)) != pdTRUE) return -1;
  int32_t result = -1;
  ServiceState state = atomic_load(&irSensorInstance.state);
  if (state == SERVICE_STATE_INITIALIZED || state == SERVICE_STATE_STARTED || state == SERVICE_STATE_STOPPED) {
    result = 0;
    goto done;
  }
  if (!irSensorInstance.cache_mutex) irSensorInstance.cache_mutex = xSemaphoreCreateMutex();
  if (!irSensorInstance.measurement_mutex) irSensorInstance.measurement_mutex = xSemaphoreCreateMutex();
  if (!irSensorInstance.cache_mutex || !irSensorInstance.measurement_mutex ||
      WatchdogWorker_Init(&irSensorInstance.worker) != 0) goto done;
  LOGD("IRTemp_Initialize", "Getting default config...");
  irSensorInstance.config = IRTempSensor_GetDefaultConfig();
  irSensorInstance.cached_object_temp = IRTEMP_CACHE_UNINITIALIZED;
  irSensorInstance.cached_ambient_temp = IRTEMP_CACHE_UNINITIALIZED;
  irSensorInstance.sample_valid = false;
  irSensorInstance.last_published_obj_temp = IRTEMP_CACHE_UNINITIALIZED;
  irSensorInstance.last_published_amb_temp = IRTEMP_CACHE_UNINITIALIZED;
  irSensorInstance.last_i2c_error = ESP_OK;
  irSensorInstance.consecutive_errors = 0;
  irSensorInstance.last_publish_time_us = 0; /* Force first publish immediately. */
  memset(&irSensorInstance.stats, 0, sizeof(IRTempStats));
  atomic_store(&irSensorInstance.stopping, false);
  atomic_store(&irSensorInstance.polling_interval_ms, irSensorInstance.config.polling_interval_ms);

  LOGD("IRTemp_Initialize", "Getting I2CManager...");
  irSensorInstance.i2c_mgr = (II2CManager *)ServiceLocator_GetService(SVC_NAME_I2C_MANAGER);
  if (!irSensorInstance.i2c_mgr || !irSensorInstance.i2c_mgr->ReadRegister || !irSensorInstance.i2c_mgr->AddDevice || !irSensorInstance.i2c_mgr->RemoveDevice) {
    LOGE("IRTemp_Initialize", "I2CManager not found!");
    goto done;
  }

  /* Initialization occurs before publication; no measurement can run yet. */
  esp_err_t err = IRTemp_AttachDevice();
  if (err != ESP_OK || !irSensorInstance.i2c_handle) {
    LOGE("IRTemp_Initialize", "Failed to add I2C device: %s", esp_err_to_name(err));
    goto done;
  }

  irSensorInstance.state = SERVICE_STATE_INITIALIZED;

  esp_err_t console_result = InfraredTemperatureSensor_RegisterConsoleCommands();
  if (console_result != ESP_OK)
    LOGW("IRTemp_Initialize", "Sensor CLI unavailable: %s; measurement service remains initialized",
         esp_err_to_name(console_result));

  const InfraredTemperatureSensorConfig *cfg = &irSensorInstance.config;
  LOGI("IRTemp_Initialize", "Infrared sensor initialized using I2CManager, addr=0x%02X", cfg->i2c_address);
  result = 0;
done:
  xSemaphoreGive(irSensorInstance.control_mutex);
  return result;
}

IInfraredTemperatureSensor *InfraredTemperatureSensor_GetModule(void) {
  if (irSensorInstance.base.base.Initialize == NULL) {
    irSensorInstance.base.base.Initialize = IRTemp_Initialize;
    irSensorInstance.base.base.Start = IRTemp_Start;
    irSensorInstance.base.base.Stop = IRTemp_Stop;
    irSensorInstance.base.base.GetState = IRTemp_GetState;
    irSensorInstance.base.base.CheckHealth = IRTemp_CheckHealth;
    irSensorInstance.base.ReadObjectTemperature = IRTemp_ReadObjectTemperature;
    irSensorInstance.base.ReadAmbientTemperature = IRTemp_ReadAmbientTemperature;
    irSensorInstance.base.TriggerMeasurement = IRTemp_TriggerMeasurement;
    irSensorInstance.base.SelfTest = IRTemp_SelfTest;
    irSensorInstance.base.GetLastError = IRTemp_GetLastError;
    irSensorInstance.base.GetStats = IRTemp_GetStats;
    irSensorInstance.base.ResetStats = IRTemp_ResetStats;
    irSensorInstance.base.SetLimits = IRTemp_SetLimits;
    irSensorInstance.base.SetPollingInterval = IRTemp_SetPollingInterval;
    irSensorInstance.base.GetPollingInterval = IRTemp_GetPollingInterval;
    irSensorInstance.base.ReadRawRegisters = IRTemp_ReadRawRegisters;
    irSensorInstance.base.ReadSample = IRTemp_ReadSample;
    LOGI("InfraredTemperatureSensor_GetModule", "Infrared temperature sensor module instance created");
  }
  return &irSensorInstance.base;
}
