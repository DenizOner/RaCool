#include "Controllers/SystemController/ISystemTelemetry.h"
#include "Controllers/SystemController/SystemControllerConfig.h"
#include "Core/DI/IDIRegistry.h"
#include "Core/ServiceLocator/ServiceLocator.h"
#include "Core/ServiceLocator/ServiceNames.h"
#include "Communication/Zigbee/IZigbee.h"
#include "Controllers/ClimateController/IClimateController.h"
#include "Management/DiagnosticsManager/IDiagnosticsManager.h"
#include "Management/ConfigurationManager/SystemConfigs.h"
#include <esp_log.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string.h>
#include <esp_timer.h>
#include "Management/Watchdog/WatchdogWorker.h"

static const char *TAG = "SystemTelemetry";

typedef struct {
  ISystemTelemetry interface;
  _Atomic ServiceState state;
  _Atomic uint32_t intervalMs;
  
  // Dependencies (DIP)
  IZigbee *zigbee;
  IClimateController *climate;
  IDiagnosticsManager *diagnostics;
  
  SemaphoreHandle_t controlMutex;
  WatchdogWorker worker;
} SystemTelemetryImpl;

static SystemTelemetryImpl telemetryInstance;

static void Telemetry_Publish(IEventBus *bus, BusEvent event) {
  if (bus->Publish(event) != 0) ESP_LOGW(TAG, "Telemetry event rejected: %s", event.eventName);
}

static void Telemetry_PublishMetrics(SystemTelemetryImpl *self) {
  IEventBus *bus = ServiceLocator_GetEventBus();
  if (!self || !bus || !bus->Publish) return;
  if (self->climate && self->climate->GetCurrentCpuTemperature) {
    float cpuTemp = self->climate->GetCurrentCpuTemperature(self->climate);
    Telemetry_Publish(bus, (BusEvent){.eventName = EVENT_INTERNAL_TEMP_UPDATED,
        .type = EVENT_TYPE_INFO, .data = &cpuTemp, .dataSize = sizeof(cpuTemp)});
  }
  if (self->diagnostics && self->diagnostics->GetLastTestResult) {
    DiagnosticsTestResult result = self->diagnostics->GetLastTestResult(self->diagnostics);
    if (result.completed && result.services_checked) {
      int32_t health = result.health_score;
      Telemetry_Publish(bus, (BusEvent){.eventName = "diag.health.updated",
          .type = EVENT_TYPE_INFO, .data = &health, .dataSize = sizeof(health)});
    }
  }
  uint32_t freeHeap = esp_get_free_heap_size();
  Telemetry_Publish(bus, (BusEvent){.eventName = "sys.memory.updated",
      .type = EVENT_TYPE_INFO, .data = &freeHeap, .dataSize = sizeof(freeHeap)});
  uint32_t uptime = (uint32_t)(esp_timer_get_time() / 1000000ULL);
  Telemetry_Publish(bus, (BusEvent){.eventName = "sys.uptime.updated",
      .type = EVENT_TYPE_INFO, .data = &uptime, .dataSize = sizeof(uptime)});
}

static void Telemetry_Task(void *context) {
  SystemTelemetryImpl *self = context;
  uint64_t lastReportMs = (uint64_t)esp_timer_get_time() / 1000ULL;
  TickType_t wait = pdMS_TO_TICKS(SYSTEM_TELEMETRY_WORKER_POLL_MS);
  if (!wait) wait = 1;
  for (;;) {
    WatchdogWorker_Checkpoint(&self->worker, NULL, (uint64_t)esp_timer_get_time() / 1000ULL);
    BaseType_t requested = xTaskNotifyWait(0, UINT32_MAX, NULL, wait);
    uint64_t nowMs = (uint64_t)esp_timer_get_time() / 1000ULL;
    WatchdogWorker_Checkpoint(&self->worker, NULL, nowMs);
    uint32_t interval = atomic_load(&self->intervalMs);
    if (requested == pdTRUE || (interval != 0 && nowMs - lastReportMs >= interval)) {
      Telemetry_PublishMetrics(self);
      lastReportMs = nowMs;
    }
  }
}

static void Telemetry_SetReportingInterval(ISystemTelemetry *self, uint32_t interval_ms) {
  if (self != &telemetryInstance.interface) return;
  if (interval_ms && interval_ms < TELEMETRY_ACTIVE_MIN_MS) interval_ms = TELEMETRY_ACTIVE_MIN_MS;
  if (interval_ms > TELEMETRY_MAX_MS) interval_ms = TELEMETRY_MAX_MS;
  atomic_store(&telemetryInstance.intervalMs, interval_ms);
}

static uint32_t Telemetry_GetReportingInterval(ISystemTelemetry *self) {
  return self == &telemetryInstance.interface ? atomic_load(&telemetryInstance.intervalMs) : 0;
}

static void Telemetry_TriggerImmediateReport(ISystemTelemetry *self) {
  SystemTelemetryImpl *impl = &telemetryInstance;
  if (self != &impl->interface || !impl->controlMutex ||
      xSemaphoreTake(impl->controlMutex, 0) != pdTRUE) return;
  TaskHandle_t task = atomic_load(&impl->worker.task);
  if (impl->state == SERVICE_STATE_STARTED && task && xTaskNotifyGive(task) != pdPASS)
    ESP_LOGW(TAG, "Immediate telemetry notification failed");
  xSemaphoreGive(impl->controlMutex);
}

static int32_t Telemetry_Initialize(IService *self, IDIRegistry *registry) {
  SystemTelemetryImpl *impl = &telemetryInstance;
  if (self != &impl->interface.base) return -1;
  if (impl->state != SERVICE_STATE_UNINITIALIZED) return 0;
  SemaphoreHandle_t control = xSemaphoreCreateMutex();
  if (!control) return -1;
  if (WatchdogWorker_Init(&impl->worker) != 0) {
    vSemaphoreDelete(control);
    return -1;
  }
  impl->controlMutex = control;
  if (registry && registry->Resolve) {
    impl->zigbee = (IZigbee *)registry->Resolve(registry, SVC_NAME_ZIGBEE);
    impl->climate = (IClimateController *)registry->Resolve(registry, SVC_NAME_CLIMATE_CONTROLLER);
    impl->diagnostics = (IDiagnosticsManager *)registry->Resolve(registry, SVC_NAME_DIAGNOSTICS_MANAGER);
  }
  if (!impl->zigbee) impl->zigbee = (IZigbee *)ServiceLocator_GetService(SVC_NAME_ZIGBEE);
  if (!impl->climate) impl->climate = (IClimateController *)ServiceLocator_GetService(SVC_NAME_CLIMATE_CONTROLLER);
  if (!impl->diagnostics) impl->diagnostics = (IDiagnosticsManager *)ServiceLocator_GetService(SVC_NAME_DIAGNOSTICS_MANAGER);
  impl->intervalMs = DEFAULT_TELEMETRY_INTERVAL_MS;
  impl->state = SERVICE_STATE_INITIALIZED;
  return 0;
}

static int32_t Telemetry_Start(IService *self) {
  SystemTelemetryImpl *impl = &telemetryInstance;
  if (self != &impl->interface.base || !impl->controlMutex ||
      xSemaphoreTake(impl->controlMutex, pdMS_TO_TICKS(SYSTEM_TELEMETRY_CONTROL_TIMEOUT_MS)) != pdTRUE) return -1;
  ServiceState previous = impl->state;
  int32_t result = -1;
  if (previous == SERVICE_STATE_STARTED) result = WatchdogWorker_Healthy(&impl->worker) ? 0 : -1;
  else if (previous == SERVICE_STATE_INITIALIZED || previous == SERVICE_STATE_STOPPED) {
    impl->state = SERVICE_STATE_STARTED;
    result = WatchdogWorker_Start(&impl->worker, Telemetry_Task, "SysTelemetry",
        SYSTEM_TELEMETRY_TASK_STACK_SIZE, impl, SYSTEM_TELEMETRY_TASK_PRIORITY);
    if (result != 0) impl->state = previous;
  }
  xSemaphoreGive(impl->controlMutex);
  return result;
}

static int32_t Telemetry_Stop(IService *self) {
  SystemTelemetryImpl *impl = &telemetryInstance;
  if (self != &impl->interface.base || !impl->controlMutex) return -1;
  TaskHandle_t task = atomic_load(&impl->worker.task);
  if (task && task == xTaskGetCurrentTaskHandle()) return -1;
  if (xSemaphoreTake(impl->controlMutex, pdMS_TO_TICKS(SYSTEM_TELEMETRY_CONTROL_TIMEOUT_MS)) != pdTRUE) return -1;
  impl->state = SERVICE_STATE_ERROR;
  int32_t result = WatchdogWorker_Stop(&impl->worker, pdMS_TO_TICKS(SYSTEM_TELEMETRY_CONTROL_TIMEOUT_MS));
  if (result == 0) impl->state = SERVICE_STATE_STOPPED;
  xSemaphoreGive(impl->controlMutex);
  return result;
}

static ServiceState Telemetry_GetState(IService *self) {
  return self == &telemetryInstance.interface.base ? atomic_load(&telemetryInstance.state) : SERVICE_STATE_ERROR;
}

static int32_t Telemetry_CheckHealth(IService *self) {
  return self == &telemetryInstance.interface.base && telemetryInstance.state == SERVICE_STATE_STARTED &&
         WatchdogWorker_Healthy(&telemetryInstance.worker) ? 0 : -1;
}

ISystemTelemetry *GetSystemTelemetry(void) {
  static bool initialized = false;
  if (!initialized) {
    telemetryInstance.interface.base.Initialize = Telemetry_Initialize;
    telemetryInstance.interface.base.Start = Telemetry_Start;
    telemetryInstance.interface.base.Stop = Telemetry_Stop;
    telemetryInstance.interface.base.GetState = Telemetry_GetState;
    telemetryInstance.interface.base.CheckHealth = Telemetry_CheckHealth;
    
    telemetryInstance.interface.SetReportingInterval = Telemetry_SetReportingInterval;
    telemetryInstance.interface.GetReportingInterval = Telemetry_GetReportingInterval;
    telemetryInstance.interface.TriggerImmediateReport = Telemetry_TriggerImmediateReport;
    
    telemetryInstance.state = SERVICE_STATE_UNINITIALIZED;
    initialized = true;
  }
  return &telemetryInstance.interface;
}
