#include "Hardware/InternalTemperatureSensor/IInternalTemperatureSensor.h"
#include "Hardware/InternalTemperatureSensor/InternalTemperatureSensorConfig.h"
#include "Core/ServiceLocator/ServiceLocator.h"
#include "Core/ServiceLocator/ServiceNames.h"
#include "Core/EventBus/IEventBus.h"
#include "Management/ConfigurationManager/IConfigurationManager.h"
#include "esp_log.h"
#include "esp_check.h"
#include "driver/temperature_sensor.h"
#include <esp_timer.h>
#include <string.h>
#include <math.h>
#include <stdatomic.h>
#include <limits.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

/**
 * @file InternalTemperatureSensor.c
 * @brief Thread-safe, async, event-driven implementation of ESP32-C6 Internal Temperature Sensor.
 */

static const char *TAG = "IntTemp";

#define EVENT_THERMAL_SHOCK_WARNING "EVENT_THERMAL_SHOCK_WARNING"

typedef struct {
    IInternalTemperatureSensor base;
    InternalTemperatureSensorConfig config;
    _Atomic(ServiceState) state;
    bool initializedSuccessfully;
    bool sensorNeedsCleanup;
    atomic_bool closing;
    atomic_uint activePolls;
    atomic_uint pendingPollingMs;
    _Atomic(TaskHandle_t) pollTask;
    bool thermalAlertPending;
    SemaphoreHandle_t controlMutex;
    float last_temp;
    float filtered_temp;
    ThermalLevel current_level;
    ThermalTrend current_trend;
    ThermalStats stats;
    SemaphoreHandle_t mutex;
    esp_timer_handle_t timer_handle;
    temperature_sensor_handle_t tsens_handle;
} InternalTempSensorImpl;

static InternalTempSensorImpl cpuSensorInstance;

// Forward declarations
static int32_t InternalTemp_Initialize(IService *self, IDIRegistry *registry);
static int32_t InternalTemp_Start(IService *self);
static int32_t InternalTemp_Stop(IService *self);
static ServiceState InternalTemp_GetState(IService *self);
static int32_t InternalTemp_CheckHealth(IService *self);

static int32_t InternalTemp_InitializeWithConfig(IInternalTemperatureSensor *self,
                                                InternalTemperatureSensorConfig config,
                                                IDIRegistry *registry);
static float InternalTemp_ReadTemperature(IInternalTemperatureSensor *self);
static float InternalTemp_GetLastTemperature(IInternalTemperatureSensor *self);
static int32_t InternalTemp_StartPolling(IInternalTemperatureSensor *self, uint32_t interval_ms);
static int32_t InternalTemp_StopPolling(IInternalTemperatureSensor *self);
static ThermalLevel InternalTemp_GetThermalLevel(IInternalTemperatureSensor *self);
static ThermalTrend InternalTemp_GetThermalTrend(IInternalTemperatureSensor *self);
static int32_t InternalTemp_SetOffset(IInternalTemperatureSensor *self, float offset_c);
static float InternalTemp_GetOffset(IInternalTemperatureSensor *self);
static int32_t InternalTemp_GetStats(IInternalTemperatureSensor *self, ThermalStats *out_stats);
static int32_t InternalTemp_ResetStats(IInternalTemperatureSensor *self);
static int32_t InternalTemp_RunSelfTest(IInternalTemperatureSensor *self);
static int32_t InternalTemp_SetLimits(IInternalTemperatureSensor *self, int16_t range_min, int16_t range_max);

static void PollTimer_Callback(void *arg);
static void OnConfigChanged(BusEvent event);
static void InternalTemp_ApplyPendingPolling(void);

/* Called only for incomplete initialization, before concurrent consumers.
 * Preserve SDK handles whenever deletion fails so retry cannot leak ownership. */
static int32_t InternalTemp_CleanupInitialization(void) {
    int32_t result = 0;
    if (cpuSensorInstance.timer_handle) {
        if (esp_timer_delete(cpuSensorInstance.timer_handle) == ESP_OK) cpuSensorInstance.timer_handle = NULL;
        else result = -1;
    }
    if (cpuSensorInstance.tsens_handle) {
        if (temperature_sensor_uninstall(cpuSensorInstance.tsens_handle) == ESP_OK) {
            cpuSensorInstance.tsens_handle = NULL;
            cpuSensorInstance.sensorNeedsCleanup = false;
        } else {
            cpuSensorInstance.sensorNeedsCleanup = true;
            result = -1;
        }
    }
    cpuSensorInstance.state = result ? SERVICE_STATE_ERROR : SERVICE_STATE_UNINITIALIZED;
    return result;
}

static int32_t InternalTemp_InitializeWithConfig(IInternalTemperatureSensor *self,
                                                InternalTemperatureSensorConfig config,
                                                IDIRegistry *registry) {
    (void)registry;
    if (!self || !InternalTemperatureSensorConfig_IsValid(&config)) return -1;
    if (!cpuSensorInstance.controlMutex) cpuSensorInstance.controlMutex=xSemaphoreCreateMutex();
    if (!cpuSensorInstance.controlMutex || xSemaphoreTake(cpuSensorInstance.controlMutex,pdMS_TO_TICKS(INTERNAL_TEMP_LOCK_MS))!=pdTRUE) return -1;
    if (!cpuSensorInstance.mutex) cpuSensorInstance.mutex = xSemaphoreCreateMutex();
    if (!cpuSensorInstance.mutex || xSemaphoreTake(cpuSensorInstance.mutex,pdMS_TO_TICKS(INTERNAL_TEMP_LOCK_MS)) != pdTRUE) {
        xSemaphoreGive(cpuSensorInstance.controlMutex);return -1;
    }
    int32_t result = -1;
    if (cpuSensorInstance.initializedSuccessfully) {
        if (cpuSensorInstance.state != SERVICE_STATE_ERROR) result = 0;
        goto done;
    }
    if (InternalTemp_CleanupInitialization() != 0) goto done;
    cpuSensorInstance.config = config;
    cpuSensorInstance.thermalAlertPending=false;
    atomic_store(&cpuSensorInstance.closing,false);
    cpuSensorInstance.current_level = THERMAL_LEVEL_NORMAL;
    cpuSensorInstance.current_trend = THERMAL_TREND_STABLE;
    cpuSensorInstance.last_temp = -273.15f;
    cpuSensorInstance.filtered_temp = -273.15f;
    cpuSensorInstance.stats = (ThermalStats){.min_temp=999.0f,.max_temp=-273.15f};
    temperature_sensor_config_t tsens_config = TEMPERATURE_SENSOR_CONFIG_DEFAULT(config.range_min,config.range_max);
    if (temperature_sensor_install(&tsens_config,&cpuSensorInstance.tsens_handle) != ESP_OK) goto rollback;
    esp_timer_create_args_t timer_args = {.callback=PollTimer_Callback,.arg=&cpuSensorInstance,.name="int_temp_poll"};
    if (esp_timer_create(&timer_args,&cpuSensorInstance.timer_handle) != ESP_OK) goto rollback;
    IEventBus *eb = (IEventBus *)ServiceLocator_GetService(SVC_NAME_EVENT_BUS);
    if (eb && (!eb->Subscribe || eb->Subscribe(eb,EVENT_CONFIG_CHANGED,OnConfigChanged) != 0)) goto rollback;
    /* Successful subscription and its state storage live for the process.
     * Stop retains resources; repeated Initialize must not subscribe twice. */
    cpuSensorInstance.initializedSuccessfully = true;
    cpuSensorInstance.state = SERVICE_STATE_INITIALIZED;
    result = 0;
    goto done;
rollback:
    InternalTemp_CleanupInitialization();
done:
    xSemaphoreGive(cpuSensorInstance.mutex);
    xSemaphoreGive(cpuSensorInstance.controlMutex);
    return result;
}

static int32_t InternalTemp_Start(IService *self) {
    if (!self || !cpuSensorInstance.mutex || !cpuSensorInstance.controlMutex) return -1;
    if (xSemaphoreTake(cpuSensorInstance.controlMutex,pdMS_TO_TICKS(INTERNAL_TEMP_LOCK_MS))!=pdTRUE) return -1;
    if (xSemaphoreTake(cpuSensorInstance.mutex, pdMS_TO_TICKS(INTERNAL_TEMP_LOCK_MS)) != pdTRUE) {
        xSemaphoreGive(cpuSensorInstance.controlMutex);return -1;
    }
    int32_t result = -1;
    if (!atomic_load(&cpuSensorInstance.closing) && cpuSensorInstance.initializedSuccessfully && (cpuSensorInstance.state == SERVICE_STATE_INITIALIZED || cpuSensorInstance.state == SERVICE_STATE_STOPPED) && !cpuSensorInstance.sensorNeedsCleanup && cpuSensorInstance.timer_handle) {
        if (!cpuSensorInstance.tsens_handle) {
            temperature_sensor_config_t cfg = TEMPERATURE_SENSOR_CONFIG_DEFAULT(cpuSensorInstance.config.range_min,cpuSensorInstance.config.range_max);
            if (temperature_sensor_install(&cfg,&cpuSensorInstance.tsens_handle) != ESP_OK) {
                goto done;
            }
        }
        esp_err_t err = temperature_sensor_enable(cpuSensorInstance.tsens_handle);
        if (err == ESP_OK) {
            unsigned pending=atomic_load(&cpuSensorInstance.pendingPollingMs);
            uint32_t period=pending ? pending : cpuSensorInstance.config.polling_interval_ms;
            err = esp_timer_start_periodic(cpuSensorInstance.timer_handle, (uint64_t)period * 1000);
            if (err == ESP_OK) {
                cpuSensorInstance.config.polling_interval_ms=period;
                if (pending) atomic_compare_exchange_strong(&cpuSensorInstance.pendingPollingMs,&pending,0);
                cpuSensorInstance.state = SERVICE_STATE_STARTED;
                result = 0;
            } else if (temperature_sensor_disable(cpuSensorInstance.tsens_handle) != ESP_OK) {
                cpuSensorInstance.state = SERVICE_STATE_ERROR;
            }
        }
    }
done:
    xSemaphoreGive(cpuSensorInstance.mutex);
    xSemaphoreGive(cpuSensorInstance.controlMutex);
    return result;
}

static int32_t InternalTemp_Stop(IService *self) {
    if (atomic_load(&cpuSensorInstance.activePolls) && atomic_load(&cpuSensorInstance.pollTask)==xTaskGetCurrentTaskHandle()) return -1;
    if (!self || !cpuSensorInstance.mutex || !cpuSensorInstance.controlMutex) return -1;
    if (xSemaphoreTake(cpuSensorInstance.controlMutex,pdMS_TO_TICKS(INTERNAL_TEMP_LOCK_MS))!=pdTRUE) return -1;
    int32_t result=-1;
    if (xSemaphoreTake(cpuSensorInstance.mutex,pdMS_TO_TICKS(INTERNAL_TEMP_LOCK_MS))!=pdTRUE) goto control_done;
    if (cpuSensorInstance.state!=SERVICE_STATE_STARTED && cpuSensorInstance.state!=SERVICE_STATE_ERROR) {
        xSemaphoreGive(cpuSensorInstance.mutex);goto control_done;
    }
    atomic_store(&cpuSensorInstance.closing,true);
    esp_err_t err=cpuSensorInstance.timer_handle ? esp_timer_stop(cpuSensorInstance.timer_handle) : ESP_ERR_INVALID_STATE;
    xSemaphoreGive(cpuSensorInstance.mutex);
    if (err!=ESP_OK && err!=ESP_ERR_INVALID_STATE) goto failed;
    /* Never hold the SDK/state lock while joining an admitted timer callback.
     * Timer stop prevents future deadlines; it does not join an existing one. */
    TickType_t started=xTaskGetTickCount();
    while (atomic_load(&cpuSensorInstance.activePolls)) {
        if (xTaskGetTickCount()-started>=pdMS_TO_TICKS(INTERNAL_TEMP_STOP_MS)) goto failed;
        TickType_t delay=pdMS_TO_TICKS(INTERNAL_TEMP_POLL_JOIN_MS);vTaskDelay(delay ? delay : 1);
    }
    if (xSemaphoreTake(cpuSensorInstance.mutex,pdMS_TO_TICKS(INTERNAL_TEMP_LOCK_MS))!=pdTRUE) goto failed;
    err=cpuSensorInstance.tsens_handle ? temperature_sensor_disable(cpuSensorInstance.tsens_handle) : ESP_ERR_INVALID_STATE;
    if (err==ESP_OK || err==ESP_ERR_INVALID_STATE) {
        if (cpuSensorInstance.sensorNeedsCleanup && cpuSensorInstance.tsens_handle) {
            if (temperature_sensor_uninstall(cpuSensorInstance.tsens_handle)==ESP_OK) {
                cpuSensorInstance.tsens_handle=NULL;cpuSensorInstance.sensorNeedsCleanup=false;result=0;
            }
        } else result=0;
    }
    if (result==0) {cpuSensorInstance.state=SERVICE_STATE_STOPPED;atomic_store(&cpuSensorInstance.closing,false);}
    xSemaphoreGive(cpuSensorInstance.mutex);
    if (result!=0) goto failed;
    goto control_done;
failed:
    cpuSensorInstance.state=SERVICE_STATE_ERROR;
control_done:
    xSemaphoreGive(cpuSensorInstance.controlMutex);return result;
}

static float InternalTemp_ReadTemperature(IInternalTemperatureSensor *self) {
    if (self == NULL || cpuSensorInstance.mutex == NULL || atomic_load(&cpuSensorInstance.closing)) return -273.15f;
    ServiceState current_state = SERVICE_STATE_UNINITIALIZED;
    float offset = 0.0f;
    float alpha = 0.2f;

    if (xSemaphoreTake(cpuSensorInstance.mutex, pdMS_TO_TICKS(INTERNAL_TEMP_LOCK_MS)) == true) {
        current_state = cpuSensorInstance.state;
        offset = cpuSensorInstance.config.temp_offset;
        alpha = cpuSensorInstance.config.ema_alpha;
        xSemaphoreGive(cpuSensorInstance.mutex);
    }

    if (current_state != SERVICE_STATE_STARTED) return -273.15f;

    float tsens_out = -273.15f;
    if (xSemaphoreTake(cpuSensorInstance.mutex, pdMS_TO_TICKS(INTERNAL_TEMP_LOCK_MS)) != pdTRUE) return -273.15f;
    esp_err_t err = cpuSensorInstance.state == SERVICE_STATE_STARTED && !atomic_load(&cpuSensorInstance.closing) && cpuSensorInstance.tsens_handle
        ? temperature_sensor_get_celsius(cpuSensorInstance.tsens_handle, &tsens_out) : ESP_ERR_INVALID_STATE;
    xSemaphoreGive(cpuSensorInstance.mutex);
    if (err == ESP_OK && isfinite(tsens_out) && isfinite(tsens_out + offset)) {
        tsens_out += offset;
        uint64_t now_ms = esp_timer_get_time() / 1000ULL;

        if (xSemaphoreTake(cpuSensorInstance.mutex, pdMS_TO_TICKS(INTERNAL_TEMP_LOCK_MS)) == true) {
            if (cpuSensorInstance.state!=SERVICE_STATE_STARTED || atomic_load(&cpuSensorInstance.closing)) {
                xSemaphoreGive(cpuSensorInstance.mutex);return -273.15f;
            }
            float prev_temp = cpuSensorInstance.last_temp;
            uint64_t prev_time_ms = cpuSensorInstance.stats.last_sample_time_ms;

            cpuSensorInstance.last_temp = tsens_out;

            // Apply EMA Filter
            if (cpuSensorInstance.filtered_temp < -270.0f) {
                cpuSensorInstance.filtered_temp = tsens_out;
            } else {
                cpuSensorInstance.filtered_temp = (alpha * tsens_out) + ((1.0f - alpha) * cpuSensorInstance.filtered_temp);
            }

            // Update stats
            if (tsens_out < cpuSensorInstance.stats.min_temp) {
                cpuSensorInstance.stats.min_temp = tsens_out;
            }
            if (tsens_out > cpuSensorInstance.stats.max_temp) {
                cpuSensorInstance.stats.max_temp = tsens_out;
            }
            cpuSensorInstance.stats.total_samples++;
            cpuSensorInstance.stats.avg_temp = cpuSensorInstance.filtered_temp;
            cpuSensorInstance.stats.last_sample_time_ms = now_ms;

            // Calculate rate of change (°C / min)
            if (cpuSensorInstance.stats.total_samples > 1 && prev_time_ms > 0 && now_ms > prev_time_ms) {
                float delta_t_min = (float)(now_ms - prev_time_ms) / 60000.0f;
                if (delta_t_min > 0.0001f) {
                    float rate = (tsens_out - prev_temp) / delta_t_min;
                    cpuSensorInstance.stats.rate_of_change_c_per_min = rate;

                    if (rate > cpuSensorInstance.config.rate_alert_threshold_c_per_min) {
                        cpuSensorInstance.current_trend = THERMAL_TREND_RAPID_RISE;
                    } else if (rate > 0.5f) {
                        cpuSensorInstance.current_trend = THERMAL_TREND_RISING;
                    } else if (rate < -0.5f) {
                        cpuSensorInstance.current_trend = THERMAL_TREND_FALLING;
                    } else {
                        cpuSensorInstance.current_trend = THERMAL_TREND_STABLE;
                    }
                }
            }

            xSemaphoreGive(cpuSensorInstance.mutex);
        }
        return tsens_out;
    }

    return -273.15f;
}

static float InternalTemp_GetLastTemperature(IInternalTemperatureSensor *self) {
    if (self == NULL || cpuSensorInstance.mutex == NULL) return -273.15f;
    float last = -273.15f;
    if (xSemaphoreTake(cpuSensorInstance.mutex, pdMS_TO_TICKS(INTERNAL_TEMP_LOCK_MS)) == true) {
        last = cpuSensorInstance.last_temp;
        xSemaphoreGive(cpuSensorInstance.mutex);
    }
    return last;
}

static int32_t InternalTemp_StartPolling(IInternalTemperatureSensor *self, uint32_t interval_ms) {
    if (!self || !cpuSensorInstance.mutex || interval_ms < CPU_POLL_MIN_MS || interval_ms > CPU_POLL_MAX_MS) return -1;
    if (xSemaphoreTake(cpuSensorInstance.mutex, pdMS_TO_TICKS(INTERNAL_TEMP_LOCK_MS)) != pdTRUE) return -1;
    int32_t result = -1;
    if (cpuSensorInstance.state == SERVICE_STATE_STARTED && !atomic_load(&cpuSensorInstance.closing) && cpuSensorInstance.timer_handle) {
        uint32_t previous = cpuSensorInstance.config.polling_interval_ms;
        esp_err_t err = esp_timer_stop(cpuSensorInstance.timer_handle);
        bool wasActive = err == ESP_OK;
        if (err == ESP_OK || err == ESP_ERR_INVALID_STATE) {
            err = esp_timer_start_periodic(cpuSensorInstance.timer_handle, (uint64_t)interval_ms * 1000);
            if (err == ESP_OK) {
                cpuSensorInstance.config.polling_interval_ms = interval_ms;
                result = 0;
            } else if (wasActive && esp_timer_start_periodic(cpuSensorInstance.timer_handle, (uint64_t)previous * 1000) != ESP_OK) {
                cpuSensorInstance.state = SERVICE_STATE_ERROR;
            }
        }
    }
    xSemaphoreGive(cpuSensorInstance.mutex);
    return result;
}

static int32_t InternalTemp_StopPolling(IInternalTemperatureSensor *self) {
    if (!self || !cpuSensorInstance.mutex) return -1;
    if (xSemaphoreTake(cpuSensorInstance.mutex, pdMS_TO_TICKS(INTERNAL_TEMP_LOCK_MS)) != pdTRUE) return -1;
    esp_err_t err = !atomic_load(&cpuSensorInstance.closing) && cpuSensorInstance.timer_handle ? esp_timer_stop(cpuSensorInstance.timer_handle) : ESP_ERR_INVALID_STATE;
    xSemaphoreGive(cpuSensorInstance.mutex);
    return err == ESP_OK ? 0 : -1;
}

static ThermalLevel InternalTemp_GetThermalLevel(IInternalTemperatureSensor *self) {
    if (self == NULL || cpuSensorInstance.mutex == NULL) return THERMAL_LEVEL_NORMAL;
    ThermalLevel level = THERMAL_LEVEL_NORMAL;
    if (xSemaphoreTake(cpuSensorInstance.mutex, pdMS_TO_TICKS(INTERNAL_TEMP_LOCK_MS)) == true) {
        level = cpuSensorInstance.current_level;
        xSemaphoreGive(cpuSensorInstance.mutex);
    }
    return level;
}

static ThermalTrend InternalTemp_GetThermalTrend(IInternalTemperatureSensor *self) {
    if (self == NULL || cpuSensorInstance.mutex == NULL) return THERMAL_TREND_STABLE;
    ThermalTrend trend = THERMAL_TREND_STABLE;
    if (xSemaphoreTake(cpuSensorInstance.mutex, pdMS_TO_TICKS(INTERNAL_TEMP_LOCK_MS)) == true) {
        trend = cpuSensorInstance.current_trend;
        xSemaphoreGive(cpuSensorInstance.mutex);
    }
    return trend;
}

static int32_t InternalTemp_SetOffset(IInternalTemperatureSensor *self, float offset_c) {
    if (self == NULL || cpuSensorInstance.mutex == NULL || !InternalTemperatureSensorConfig_IsOffsetValid(offset_c)) return -1;
    if (xSemaphoreTake(cpuSensorInstance.mutex, pdMS_TO_TICKS(INTERNAL_TEMP_LOCK_MS)) == true) {
        if (atomic_load(&cpuSensorInstance.closing)) {xSemaphoreGive(cpuSensorInstance.mutex);return -1;}
        cpuSensorInstance.config.temp_offset = offset_c;
        xSemaphoreGive(cpuSensorInstance.mutex);
        return 0;
    }
    return -1;
}

static float InternalTemp_GetOffset(IInternalTemperatureSensor *self) {
    if (self == NULL || cpuSensorInstance.mutex == NULL) return 0.0f;
    float offset = 0.0f;
    if (xSemaphoreTake(cpuSensorInstance.mutex, pdMS_TO_TICKS(INTERNAL_TEMP_LOCK_MS)) == true) {
        offset = cpuSensorInstance.config.temp_offset;
        xSemaphoreGive(cpuSensorInstance.mutex);
    }
    return offset;
}

static int32_t InternalTemp_GetStatus(IInternalTemperatureSensor *self,InternalTemperatureStatus *out) {
    if (!self || !out || !cpuSensorInstance.mutex) return -1;
    if (xSemaphoreTake(cpuSensorInstance.mutex,pdMS_TO_TICKS(INTERNAL_TEMP_LOCK_MS))!=pdTRUE) return -1;
    InternalTemperatureStatus status={.last_temperature=cpuSensorInstance.last_temp,
        .offset=cpuSensorInstance.config.temp_offset,.level=cpuSensorInstance.current_level,
        .trend=cpuSensorInstance.current_trend,.state=cpuSensorInstance.state,
        .last_sample_time_ms=cpuSensorInstance.stats.last_sample_time_ms,
        .sample_valid=cpuSensorInstance.stats.total_samples>0 && isfinite(cpuSensorInstance.last_temp) && cpuSensorInstance.last_temp>-273.0f};
    xSemaphoreGive(cpuSensorInstance.mutex);*out=status;return 0;
}

static int32_t InternalTemp_GetStats(IInternalTemperatureSensor *self, ThermalStats *out_stats) {
    if (self == NULL || cpuSensorInstance.mutex == NULL) return -1;
    if (out_stats == NULL) {
        return -1;
    }
    if (xSemaphoreTake(cpuSensorInstance.mutex, pdMS_TO_TICKS(INTERNAL_TEMP_LOCK_MS)) == true) {
        *out_stats = cpuSensorInstance.stats;
        xSemaphoreGive(cpuSensorInstance.mutex);
        return 0;
    }
    return -1;
}

static int32_t InternalTemp_ResetStats(IInternalTemperatureSensor *self) {
    if (self == NULL || cpuSensorInstance.mutex == NULL) return -1;
    if (xSemaphoreTake(cpuSensorInstance.mutex, pdMS_TO_TICKS(INTERNAL_TEMP_LOCK_MS)) == true) {
        cpuSensorInstance.stats.min_temp = 999.0f;
        cpuSensorInstance.stats.max_temp = -273.15f;
        cpuSensorInstance.stats.avg_temp = 0.0f;
        cpuSensorInstance.stats.rate_of_change_c_per_min = 0.0f;
        cpuSensorInstance.stats.total_samples = 0;
        cpuSensorInstance.stats.last_sample_time_ms = 0;
        cpuSensorInstance.current_trend = THERMAL_TREND_STABLE;
        xSemaphoreGive(cpuSensorInstance.mutex);
        return 0;
    }
    return -1;
}

static int32_t InternalTemp_RunSelfTest(IInternalTemperatureSensor *self) {
    if (self == NULL) {
        return -1;
    }

    ServiceState state = InternalTemp_GetState(&self->base);
    if (state != SERVICE_STATE_STARTED) {
        ESP_LOGE(TAG, "Self-test failed: Service not started (State: %d)", state);
        return -1;
    }

    float temp = InternalTemp_ReadTemperature(self);
    if (!isfinite(temp) || temp < -40.0f || temp > 125.0f) {
        ESP_LOGE(TAG, "Self-test failed: Temperature out of physical bounds (%.2f C)", temp);
        return -1;
    }

    ESP_LOGI(TAG, "Internal Temperature Sensor Self-Test PASSED (Current Temp: %.2f C)", temp);
    return 0;
}

static int32_t InternalTemp_SetLimits(IInternalTemperatureSensor *self, int16_t range_min, int16_t range_max) {
    if (!self || !cpuSensorInstance.mutex || !InternalTemperatureSensorConfig_IsRangeValid(range_min,range_max)) return -1;
    if (xSemaphoreTake(cpuSensorInstance.mutex, pdMS_TO_TICKS(INTERNAL_TEMP_LOCK_MS)) != pdTRUE) return -1;
    if (atomic_load(&cpuSensorInstance.closing)) {xSemaphoreGive(cpuSensorInstance.mutex);return -1;}
    int32_t result = -1;
    ServiceState previous = cpuSensorInstance.state;
    bool running = previous == SERVICE_STATE_STARTED;
    if (!cpuSensorInstance.tsens_handle || (previous != SERVICE_STATE_INITIALIZED && previous != SERVICE_STATE_STOPPED && !running)) goto done;
    if (running && temperature_sensor_disable(cpuSensorInstance.tsens_handle) != ESP_OK) goto done;
    if (temperature_sensor_uninstall(cpuSensorInstance.tsens_handle) != ESP_OK) {
        cpuSensorInstance.sensorNeedsCleanup = true;
        cpuSensorInstance.state = SERVICE_STATE_ERROR;
        goto done;
    }
    cpuSensorInstance.tsens_handle = NULL;
    temperature_sensor_config_t requested = TEMPERATURE_SENSOR_CONFIG_DEFAULT(range_min,range_max);
    esp_err_t err = temperature_sensor_install(&requested,&cpuSensorInstance.tsens_handle);
    if (err == ESP_OK && (!running || temperature_sensor_enable(cpuSensorInstance.tsens_handle) == ESP_OK)) {
        cpuSensorInstance.config.range_min = range_min;
        cpuSensorInstance.config.range_max = range_max;
        result = 0;
        goto done;
    }
    if (cpuSensorInstance.tsens_handle) {
        if (temperature_sensor_uninstall(cpuSensorInstance.tsens_handle) != ESP_OK) {
            cpuSensorInstance.sensorNeedsCleanup = true;
            cpuSensorInstance.state = SERVICE_STATE_ERROR;
            goto done;
        }
        cpuSensorInstance.tsens_handle = NULL;
    }
    temperature_sensor_config_t original = TEMPERATURE_SENSOR_CONFIG_DEFAULT(cpuSensorInstance.config.range_min,cpuSensorInstance.config.range_max);
    err = temperature_sensor_install(&original,&cpuSensorInstance.tsens_handle);
    if (err != ESP_OK || (running && temperature_sensor_enable(cpuSensorInstance.tsens_handle) != ESP_OK)) cpuSensorInstance.state = SERVICE_STATE_ERROR;
done:
    xSemaphoreGive(cpuSensorInstance.mutex);
    return result;
}

static ServiceState InternalTemp_GetState(IService *self) {
    return self ? atomic_load(&cpuSensorInstance.state) : SERVICE_STATE_ERROR;
}

static int32_t InternalTemp_CheckHealth(IService *self) {
    return (InternalTemp_GetState(self) == SERVICE_STATE_STARTED && !atomic_load(&cpuSensorInstance.closing)) ? 0 : -1;
}

static int32_t InternalTemp_Initialize(IService *self, IDIRegistry *registry) {
    InternalTemperatureSensorConfig config = GetDefaultInternalTemperatureSensorConfig();
    IConfigurationManager *manager = (IConfigurationManager *)ServiceLocator_GetService(SVC_NAME_CONFIGURATION_MANAGER);
    if (manager) {
        SystemSettings settings;
        if (!manager->GetSnapshot || manager->GetSnapshot(manager,&settings) != ESP_OK) return -1;
        config.polling_interval_ms = settings.thermal.cpu_polling_interval_ms;
        config.warning_threshold = settings.thermal.cpu_temp_warning;
        config.critical_threshold = settings.thermal.cpu_temp_critical;
        config.emergency_threshold = settings.thermal.cpu_temp_emergency;
    }
    return InternalTemp_InitializeWithConfig((IInternalTemperatureSensor *)self,config,registry);
}

static ThermalLevel InternalTemp_CalculateLevel(float temp,const InternalTemperatureSensorConfig *config,ThermalLevel current) {
    ThermalLevel next=current;
    if (temp>=config->emergency_threshold) next=THERMAL_LEVEL_EMERGENCY;
    else if (temp>=config->critical_threshold && current<THERMAL_LEVEL_CRITICAL) next=THERMAL_LEVEL_CRITICAL;
    else if (temp>=config->warning_threshold && current<THERMAL_LEVEL_WARNING) next=THERMAL_LEVEL_WARNING;
    const float hysteresis=2.0f;
    if (current==THERMAL_LEVEL_EMERGENCY && temp<config->emergency_threshold-hysteresis) next=THERMAL_LEVEL_CRITICAL;
    if (next==THERMAL_LEVEL_CRITICAL && temp<config->critical_threshold-hysteresis) next=THERMAL_LEVEL_WARNING;
    if (next==THERMAL_LEVEL_WARNING && temp<config->warning_threshold-hysteresis) next=THERMAL_LEVEL_NORMAL;
    return next;
}
static int32_t InternalTemp_Publish(IEventBus *bus,BusEvent event) {
    if (!bus || !bus->Publish || atomic_load(&cpuSensorInstance.closing)) return -1;
    int32_t result=bus->Publish(event);
    if (result!=0) ESP_LOGE(TAG,"Event publication failed: %s (%ld)",event.eventName,(long)result);
    return result;
}
static void PollTimer_Callback(void *arg) {
    InternalTemp_ApplyPendingPolling();
    InternalTempSensorImpl *impl=(InternalTempSensorImpl *)arg;
    if (!impl || !impl->mutex || xSemaphoreTake(impl->mutex,pdMS_TO_TICKS(INTERNAL_TEMP_LOCK_MS))!=pdTRUE) return;
    if (impl->state!=SERVICE_STATE_STARTED || atomic_load(&impl->closing) || atomic_load(&impl->activePolls)==UINT_MAX) {
        xSemaphoreGive(impl->mutex);return;
    }
    if (!atomic_load(&impl->activePolls)) atomic_store(&impl->pollTask,xTaskGetCurrentTaskHandle());
    atomic_fetch_add(&impl->activePolls,1);
    InternalTemperatureSensorConfig config;
    xSemaphoreGive(impl->mutex);
    float temp=InternalTemp_ReadTemperature((IInternalTemperatureSensor *)impl);
    if (!isfinite(temp) || temp<-273.0f) goto done;
    if (xSemaphoreTake(impl->mutex,pdMS_TO_TICKS(INTERNAL_TEMP_LOCK_MS))!=pdTRUE) goto done;
    if (impl->state!=SERVICE_STATE_STARTED || atomic_load(&impl->closing)) {xSemaphoreGive(impl->mutex);goto done;}
    config=impl->config;
    xSemaphoreGive(impl->mutex);
    IConfigurationManager *manager=(IConfigurationManager *)ServiceLocator_GetService(SVC_NAME_CONFIGURATION_MANAGER);
    if (manager && manager->GetSnapshot) {
        SystemSettings settings;
        if (manager->GetSnapshot(manager,&settings)==ESP_OK &&
            isfinite(settings.thermal.cpu_temp_warning) && isfinite(settings.thermal.cpu_temp_critical) &&
            isfinite(settings.thermal.cpu_temp_emergency) && settings.thermal.cpu_temp_warning<settings.thermal.cpu_temp_critical &&
            settings.thermal.cpu_temp_critical<settings.thermal.cpu_temp_emergency) {
            config.warning_threshold=settings.thermal.cpu_temp_warning;
            config.critical_threshold=settings.thermal.cpu_temp_critical;
            config.emergency_threshold=settings.thermal.cpu_temp_emergency;
        }
    }
    if (xSemaphoreTake(impl->mutex,pdMS_TO_TICKS(INTERNAL_TEMP_LOCK_MS))!=pdTRUE) goto done;
    if (impl->state!=SERVICE_STATE_STARTED || atomic_load(&impl->closing)) {xSemaphoreGive(impl->mutex);goto done;}
    ThermalLevel previous=impl->current_level;
    ThermalLevel level=InternalTemp_CalculateLevel(temp,&config,previous);
    bool changed=level!=previous;
    if (changed) {impl->current_level=level;impl->thermalAlertPending=true;}
    bool alertPending=impl->thermalAlertPending;
    ThermalTrend trend=impl->current_trend;
    xSemaphoreGive(impl->mutex);
    /* Commit thermal state before notifying consumers. External publication is
     * outside the state mutex, with a reference retained until it returns. */
    if (changed) ESP_LOGW(TAG,"Thermal level changed from %d to %d (Temp: %.2f C)",previous,level,temp);
    IEventBus *bus=(IEventBus *)ServiceLocator_GetService(SVC_NAME_EVENT_BUS);
    if (config.enable_events) {
        BusEvent event={.eventName=EVENT_INTERNAL_TEMP_UPDATED,.type=EVENT_TYPE_INFO,.data=&temp,.dataSize=sizeof(temp)};
        InternalTemp_Publish(bus,event);
    }
    if (trend==THERMAL_TREND_RAPID_RISE) {
        BusEvent event={.eventName=EVENT_THERMAL_SHOCK_WARNING,.type=EVENT_TYPE_WARNING,.data=&temp,.dataSize=sizeof(temp)};
        InternalTemp_Publish(bus,event);
    }
    if (alertPending) {
        EventType type=level==THERMAL_LEVEL_WARNING ? EVENT_TYPE_WARNING :
            level>=THERMAL_LEVEL_CRITICAL ? EVENT_TYPE_ERROR : EVENT_TYPE_INFO;
        BusEvent event={.eventName=EVENT_SYSTEM_THERMAL_ALERT,.type=type,.data=&temp,.dataSize=sizeof(temp)};
        if (InternalTemp_Publish(bus,event)==0 && xSemaphoreTake(impl->mutex,pdMS_TO_TICKS(INTERNAL_TEMP_LOCK_MS))==pdTRUE) {
            if (impl->current_level==level) impl->thermalAlertPending=false;
            xSemaphoreGive(impl->mutex);
        }
    }
done:
    /* ESP_TIMER_TASK serializes this timer's invocations. The reference is the
     * Stop barrier; queued EventBus delivery has its own independent lifetime. */
    atomic_fetch_sub(&impl->activePolls,1);
}

static void InternalTemp_ApplyPendingPolling(void) {
    unsigned pending=atomic_load(&cpuSensorInstance.pendingPollingMs);
    if (!pending || atomic_load(&cpuSensorInstance.state)!=SERVICE_STATE_STARTED || atomic_load(&cpuSensorInstance.closing)) return;
    if (InternalTemp_StartPolling(&cpuSensorInstance.base,pending)==0)
        atomic_compare_exchange_strong(&cpuSensorInstance.pendingPollingMs,&pending,0);
}
static void OnConfigChanged(BusEvent event) {
    (void)event;
    IConfigurationManager *manager=(IConfigurationManager *)ServiceLocator_GetService(SVC_NAME_CONFIGURATION_MANAGER);
    SystemSettings settings;
    if (!manager || !manager->GetSnapshot || manager->GetSnapshot(manager,&settings)!=ESP_OK) return;
    uint32_t requested=settings.thermal.cpu_polling_interval_ms;
    if (requested<CPU_POLL_MIN_MS || requested>CPU_POLL_MAX_MS) return;
    /* Latest desired period survives SDK/lock failures. Retry on the existing
     * timer, or consume it on Start after cooperative recovery. */
    atomic_store(&cpuSensorInstance.pendingPollingMs,requested);
    if (cpuSensorInstance.mutex && xSemaphoreTake(cpuSensorInstance.mutex,pdMS_TO_TICKS(INTERNAL_TEMP_LOCK_MS))==pdTRUE) {
        if (isfinite(settings.thermal.cpu_temp_warning) && isfinite(settings.thermal.cpu_temp_critical) &&
            isfinite(settings.thermal.cpu_temp_emergency) && settings.thermal.cpu_temp_warning<settings.thermal.cpu_temp_critical &&
            settings.thermal.cpu_temp_critical<settings.thermal.cpu_temp_emergency) {
            cpuSensorInstance.config.warning_threshold=settings.thermal.cpu_temp_warning;
            cpuSensorInstance.config.critical_threshold=settings.thermal.cpu_temp_critical;
            cpuSensorInstance.config.emergency_threshold=settings.thermal.cpu_temp_emergency;
        }
        if (cpuSensorInstance.state!=SERVICE_STATE_STARTED && !atomic_load(&cpuSensorInstance.closing))
            cpuSensorInstance.config.polling_interval_ms=requested;
        if (cpuSensorInstance.config.polling_interval_ms==requested)
            atomic_compare_exchange_strong(&cpuSensorInstance.pendingPollingMs,&requested,0);
        xSemaphoreGive(cpuSensorInstance.mutex);
    }
    InternalTemp_ApplyPendingPolling();
}

IInternalTemperatureSensor *InternalTemperatureSensor_GetModule(void) {
    static bool initialized = false;
    if (!initialized) {
        memset(&cpuSensorInstance, 0, sizeof(InternalTempSensorImpl));
        cpuSensorInstance.base.base.Initialize = InternalTemp_Initialize;
        cpuSensorInstance.base.base.Start = InternalTemp_Start;
        cpuSensorInstance.base.base.Stop = InternalTemp_Stop;
        cpuSensorInstance.base.base.GetState = InternalTemp_GetState;
        cpuSensorInstance.base.base.CheckHealth = InternalTemp_CheckHealth;
        cpuSensorInstance.base.InitializeWithConfig = InternalTemp_InitializeWithConfig;
        cpuSensorInstance.base.ReadTemperature = InternalTemp_ReadTemperature;
        cpuSensorInstance.base.GetLastTemperature = InternalTemp_GetLastTemperature;
        cpuSensorInstance.base.StartPolling = InternalTemp_StartPolling;
        cpuSensorInstance.base.StopPolling = InternalTemp_StopPolling;
        cpuSensorInstance.base.GetThermalLevel = InternalTemp_GetThermalLevel;
        cpuSensorInstance.base.GetThermalTrend = InternalTemp_GetThermalTrend;
        cpuSensorInstance.base.SetOffset = InternalTemp_SetOffset;
        cpuSensorInstance.base.GetOffset = InternalTemp_GetOffset;
        cpuSensorInstance.base.GetStats = InternalTemp_GetStats;
        cpuSensorInstance.base.ResetStats = InternalTemp_ResetStats;
        cpuSensorInstance.base.RunSelfTest = InternalTemp_RunSelfTest;
        cpuSensorInstance.base.SetLimits = InternalTemp_SetLimits;
        cpuSensorInstance.base.GetStatus = InternalTemp_GetStatus;
        
        cpuSensorInstance.state = SERVICE_STATE_UNINITIALIZED;
        cpuSensorInstance.last_temp = -273.15f;
        cpuSensorInstance.filtered_temp = -273.15f;
        cpuSensorInstance.current_level = THERMAL_LEVEL_NORMAL;
        cpuSensorInstance.current_trend = THERMAL_TREND_STABLE;
        initialized = true;
    }
    return &cpuSensorInstance.base;
}

