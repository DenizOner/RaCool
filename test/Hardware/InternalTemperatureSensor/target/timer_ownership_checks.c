#include "unity.h"
#include "driver/temperature_sensor.h"
#include <stdatomic.h>
static esp_err_t TargetCpu_Read(temperature_sensor_handle_t handle,float *value);
#define InternalTemperatureSensor_GetModule TargetCpu_GetModule
#define GetDefaultInternalTemperatureSensorConfig TargetCpu_DefaultConfig
#define InternalTemperatureSensorConfig_IsOffsetValid TargetCpu_IsOffsetValid
#define InternalTemperatureSensorConfig_IsRangeValid TargetCpu_RangeValid
#define InternalTemperatureSensorConfig_IsValid TargetCpu_ConfigValid
#define ServiceLocator_GetService TargetCpu_GetService
#define temperature_sensor_get_celsius TargetCpu_Read
#include "../../../../src/Hardware/InternalTemperatureSensor/InternalTemperatureSensor.c"
#include "../../../../src/Hardware/InternalTemperatureSensor/InternalTemperatureSensorConfig.c"
#undef temperature_sensor_get_celsius
#undef ServiceLocator_GetService
#undef InternalTemperatureSensorConfig_IsOffsetValid
#undef InternalTemperatureSensorConfig_IsRangeValid
#undef InternalTemperatureSensorConfig_IsValid
#undef GetDefaultInternalTemperatureSensorConfig
#undef InternalTemperatureSensor_GetModule
static SemaphoreHandle_t cpuPublishEntered,cpuPublishRelease;
static atomic_bool cpuPublishEnabled,cpuBlockNextPublish;
static int32_t TargetCpu_Publish(BusEvent event) {
    (void)event;
    if (atomic_exchange(&cpuBlockNextPublish,false)) {
        xSemaphoreGive(cpuPublishEntered);
        xSemaphoreTake(cpuPublishRelease,portMAX_DELAY);
    }
    return 0;
}
static IEventBus cpuTargetBus={.Publish=TargetCpu_Publish};
IService *TargetCpu_GetService(const char *name) {
    return atomic_load(&cpuPublishEnabled) && strcmp(name,SVC_NAME_EVENT_BUS)==0 ? &cpuTargetBus.base : NULL;
}
static SemaphoreHandle_t cpuReadEntered,cpuReadRelease;
static atomic_bool cpuBlockNextRead;
static bool cpuTargetActive;
static esp_err_t TargetCpu_Read(temperature_sensor_handle_t handle,float *value) {
    if (atomic_exchange(&cpuBlockNextRead,false)) {
        xSemaphoreGive(cpuReadEntered);
        xSemaphoreTake(cpuReadRelease,portMAX_DELAY);
    }
    return temperature_sensor_get_celsius(handle,value);
}
void target_cpu_timer_cleanup(void) {
    if (!cpuTargetActive) return;
    if (cpuReadRelease) xSemaphoreGive(cpuReadRelease);
    if (cpuPublishRelease) xSemaphoreGive(cpuPublishRelease);
    ServiceState state=atomic_load(&cpuSensorInstance.state);
    if (state==SERVICE_STATE_STARTED || state==SERVICE_STATE_ERROR) TEST_ASSERT_EQUAL(0,InternalTemp_Stop(&cpuSensorInstance.base.base));
    if (cpuSensorInstance.timer_handle) {
        esp_err_t err=esp_timer_stop(cpuSensorInstance.timer_handle);
        TEST_ASSERT_TRUE(err==ESP_OK || err==ESP_ERR_INVALID_STATE);
        TEST_ASSERT_EQUAL(ESP_OK,esp_timer_delete(cpuSensorInstance.timer_handle));
        cpuSensorInstance.timer_handle=NULL;
    }
    if (cpuSensorInstance.tsens_handle) {
        esp_err_t err=temperature_sensor_disable(cpuSensorInstance.tsens_handle);
        TEST_ASSERT_TRUE(err==ESP_OK || err==ESP_ERR_INVALID_STATE);
        TEST_ASSERT_EQUAL(ESP_OK,temperature_sensor_uninstall(cpuSensorInstance.tsens_handle));
        cpuSensorInstance.tsens_handle=NULL;
    }
    /* Stop serialized with the injected SDK read before releasing these semaphores.
     * Module state/mutex remain alive for any already dispatched timer callback. */
    if (cpuReadEntered) {vSemaphoreDelete(cpuReadEntered);cpuReadEntered=NULL;}
    if (cpuReadRelease) {vSemaphoreDelete(cpuReadRelease);cpuReadRelease=NULL;}
    if (cpuPublishEntered) {vSemaphoreDelete(cpuPublishEntered);cpuPublishEntered=NULL;}
    if (cpuPublishRelease) {vSemaphoreDelete(cpuPublishRelease);cpuPublishRelease=NULL;}
    atomic_store(&cpuPublishEnabled,false);
    cpuTargetActive=false;
}
static ThermalStats wait_for_cpu_samples(IInternalTemperatureSensor *sensor,uint32_t previous) {
    ThermalStats stats={0};TickType_t start=xTaskGetTickCount();
    while (xTaskGetTickCount()-start<pdMS_TO_TICKS(2000)) {
        if (sensor->GetStats(sensor,&stats)==0 && stats.total_samples>previous) return stats;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    TEST_FAIL_MESSAGE("CPU timer did not acquire a fresh SDK sample");return stats;
}
void target_cpu_verify_timer_ownership(void) {
    IInternalTemperatureSensor *sensor=TargetCpu_GetModule();cpuTargetActive=true;
    cpuReadEntered=xSemaphoreCreateBinary();cpuReadRelease=xSemaphoreCreateBinary();
    TEST_ASSERT_NOT_NULL(cpuReadEntered);TEST_ASSERT_NOT_NULL(cpuReadRelease);
    InternalTemperatureSensorConfig config=TargetCpu_DefaultConfig();config.polling_interval_ms=500;
    TEST_ASSERT_EQUAL(0,sensor->InitializeWithConfig(sensor,config,NULL));
    temperature_sensor_handle_t handle=cpuSensorInstance.tsens_handle;
    atomic_store(&cpuBlockNextRead,true);
    TEST_ASSERT_EQUAL(0,sensor->base.Start(&sensor->base));
    TEST_ASSERT_EQUAL(pdTRUE,xSemaphoreTake(cpuReadEntered,pdMS_TO_TICKS(2000)));
    int64_t before=esp_timer_get_time();
    TEST_ASSERT_NOT_EQUAL(0,sensor->base.Stop(&sensor->base));
    TEST_ASSERT_TRUE(esp_timer_get_time()-before<100000);
    TEST_ASSERT_EQUAL(SERVICE_STATE_STARTED,sensor->base.GetState(&sensor->base));
    TEST_ASSERT_EQUAL_PTR(handle,cpuSensorInstance.tsens_handle);
    TEST_ASSERT_NOT_EQUAL(0,sensor->SetLimits(sensor,20,100));
    TEST_ASSERT_EQUAL_PTR(handle,cpuSensorInstance.tsens_handle);
    xSemaphoreGive(cpuReadRelease);
    ThermalStats stats=wait_for_cpu_samples(sensor,0);
    TEST_ASSERT_TRUE(isfinite(stats.avg_temp) && stats.avg_temp>=-40 && stats.avg_temp<=125);
    TEST_ASSERT_EQUAL(0,sensor->SetLimits(sensor,20,100));
    TEST_ASSERT_EQUAL(20,cpuSensorInstance.config.range_min);
    TEST_ASSERT_EQUAL(100,cpuSensorInstance.config.range_max);
    TEST_ASSERT_EQUAL(0,sensor->SetLimits(sensor,config.range_min,config.range_max));
    handle=cpuSensorInstance.tsens_handle;
    TEST_ASSERT_EQUAL(0,sensor->base.Stop(&sensor->base));
    TEST_ASSERT_EQUAL_PTR(handle,cpuSensorInstance.tsens_handle);
    TEST_ASSERT_EQUAL(0,sensor->base.Start(&sensor->base));
    stats=wait_for_cpu_samples(sensor,stats.total_samples);
    TEST_ASSERT_GREATER_THAN(1,stats.total_samples);
    InternalTemperatureStatus status={0};
    TEST_ASSERT_EQUAL(0,sensor->GetStatus(sensor,&status));
    TEST_ASSERT_TRUE(status.sample_valid && isfinite(status.last_temperature));
    cpuPublishEntered=xSemaphoreCreateBinary();cpuPublishRelease=xSemaphoreCreateBinary();
    TEST_ASSERT_NOT_NULL(cpuPublishEntered);TEST_ASSERT_NOT_NULL(cpuPublishRelease);
    atomic_store(&cpuBlockNextPublish,true);
    atomic_store(&cpuPublishEnabled,true);
    TEST_ASSERT_EQUAL(pdTRUE,xSemaphoreTake(cpuPublishEntered,pdMS_TO_TICKS(2000)));
    TEST_ASSERT_EQUAL(1,atomic_load(&cpuSensorInstance.activePolls));
    before=esp_timer_get_time();
    TEST_ASSERT_NOT_EQUAL(0,sensor->base.Stop(&sensor->base));
    int64_t elapsed=esp_timer_get_time()-before;
    TEST_ASSERT_TRUE(elapsed>=400000 && elapsed<800000);
    TEST_ASSERT_EQUAL(SERVICE_STATE_ERROR,sensor->base.GetState(&sensor->base));
    TEST_ASSERT_TRUE(atomic_load(&cpuSensorInstance.closing));
    TEST_ASSERT_EQUAL_PTR(handle,cpuSensorInstance.tsens_handle);
    TEST_ASSERT_NOT_EQUAL(0,sensor->base.Start(&sensor->base));
    TEST_ASSERT_TRUE(sensor->ReadTemperature(sensor)<-273.0f);
    TEST_ASSERT_NOT_EQUAL(0,sensor->StartPolling(sensor,500));
    xSemaphoreGive(cpuPublishRelease);
    TickType_t joinStart=xTaskGetTickCount();
    while (atomic_load(&cpuSensorInstance.activePolls) && xTaskGetTickCount()-joinStart<pdMS_TO_TICKS(1000))
        vTaskDelay(pdMS_TO_TICKS(10));
    TEST_ASSERT_EQUAL(0,atomic_load(&cpuSensorInstance.activePolls));
    TEST_ASSERT_EQUAL(0,sensor->base.Stop(&sensor->base));
    TEST_ASSERT_EQUAL(0,sensor->base.Start(&sensor->base));
    stats=wait_for_cpu_samples(sensor,stats.total_samples);
    TEST_ASSERT_TRUE(stats.total_samples>1);
    atomic_store(&cpuSensorInstance.pendingPollingMs,CPU_POLL_MIN_MS*2);
    TickType_t applyStart=xTaskGetTickCount();
    while (atomic_load(&cpuSensorInstance.pendingPollingMs) && xTaskGetTickCount()-applyStart<pdMS_TO_TICKS(2000))
        vTaskDelay(pdMS_TO_TICKS(10));
    TEST_ASSERT_EQUAL(0,atomic_load(&cpuSensorInstance.pendingPollingMs));
    TEST_ASSERT_EQUAL(pdTRUE,xSemaphoreTake(cpuSensorInstance.mutex,pdMS_TO_TICKS(100)));
    uint32_t appliedPeriod=cpuSensorInstance.config.polling_interval_ms;
    xSemaphoreGive(cpuSensorInstance.mutex);
    TEST_ASSERT_EQUAL(CPU_POLL_MIN_MS*2,appliedPeriod);
    TEST_ASSERT_EQUAL(0,sensor->GetStats(sensor,&stats));
    stats=wait_for_cpu_samples(sensor,stats.total_samples);
}
