#ifndef INTERNAL_TEMP_TEST_FIXTURE_H
#define INTERNAL_TEMP_TEST_FIXTURE_H
#ifndef RACOOL_TEST_NATIVE
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#endif
#include "Hardware/InternalTemperatureSensor/IInternalTemperatureSensor.h"
#include "Management/ConfigurationManager/IConfigurationManager.h"
#include "Management/ConfigurationManager/SystemConfigs.h"
#include "Core/EventBus/IEventBus.h"
#include "Core/ServiceLocator/ServiceNames.h"
#include "test_mocks.h"
#include "unity.h"

// --- Mock Redirections ---
#define temperature_sensor_install_fake mocked_temperature_sensor_install_fake
#define temperature_sensor_enable_fake mocked_temperature_sensor_enable_fake
#define temperature_sensor_disable_fake mocked_temperature_sensor_disable_fake
#define temperature_sensor_get_celsius_fake mocked_temperature_sensor_get_celsius_fake
#define esp_timer_create_fake mocked_esp_timer_create_fake
#define esp_timer_start_periodic_fake mocked_esp_timer_start_periodic_fake
#define esp_timer_stop_fake mocked_esp_timer_stop_fake
#define ServiceLocator_GetService_fake mocked_ServiceLocator_GetService_fake

// Source include for white-box testing
#include "Hardware/InternalTemperatureSensor/InternalTemperatureSensorConfig.c"
#include "Hardware/InternalTemperatureSensor/InternalTemperatureSensor.c"

// --- Fakes & Mocks ---

static SystemSettings mock_settings;
static esp_err_t sensor_snapshot_result;
static esp_err_t mock_get_settings(IConfigurationManager* self, SystemSettings *out) {
    (void)self;
    if (sensor_snapshot_result == ESP_OK) *out = mock_settings;
    return sensor_snapshot_result;
}
static IConfigurationManager mock_configManager = {
    .GetSnapshot = mock_get_settings
};

static int mock_event_published_count = 0;
static char last_event_name[64];
static int32_t mock_publish(BusEvent event) {
    mock_event_published_count++;
    strncpy(last_event_name, event.eventName, sizeof(last_event_name));
    return 0;
}
static int32_t sensor_subscribe_result;
static int32_t mock_subscribe(IEventBus *self, const char *eventName, BusEventHandler handler) {
    (void)self; (void)eventName; (void)handler;
    return sensor_subscribe_result;
}
static IEventBus mock_eventBus = {
    .Publish = mock_publish,
    .Subscribe = mock_subscribe,
    .Unsubscribe = (int32_t (*)(IEventBus *, const char *, BusEventHandler))mock_subscribe
};

static float mock_temp_val = 25.0f;
static esp_err_t my_get_celsius(temperature_sensor_handle_t h, float *out) {
    *out = mock_temp_val;
    return ESP_OK;
}

static IService* my_get_service(const char *name) {
    if (strcmp(name, SVC_NAME_CONFIGURATION_MANAGER) == 0) return (IService *)&mock_configManager;
    if (strcmp(name, SVC_NAME_EVENT_BUS) == 0) return (IService *)&mock_eventBus;
    return NULL;
}

// --- Test Setup ---

static esp_err_t install_sensor(const temperature_sensor_config_t *config, temperature_sensor_handle_t *out) {
    (void)config;
    *out = (temperature_sensor_handle_t)(uintptr_t)0x100;
    return ESP_OK;
}
static esp_err_t create_timer(const esp_timer_create_args_t *config, esp_timer_handle_t *out) {
    (void)config;
    *out = (esp_timer_handle_t)(uintptr_t)0x200;
    return ESP_OK;
}

static void InternalTempFixture_SetUp(void) {
    RESET_FAKE(mocked_xSemaphoreCreateMutex);
    RESET_FAKE(mocked_xSemaphoreTake);
    RESET_FAKE(mocked_xSemaphoreGive);
    RESET_FAKE(mocked_temperature_sensor_install);
    RESET_FAKE(mocked_temperature_sensor_enable);
    RESET_FAKE(mocked_temperature_sensor_disable);
    RESET_FAKE(mocked_temperature_sensor_uninstall);
    RESET_FAKE(mocked_temperature_sensor_get_celsius);
    RESET_FAKE(mocked_esp_timer_create);
    RESET_FAKE(mocked_esp_timer_delete);
    RESET_FAKE(mocked_esp_timer_start_periodic);
    RESET_FAKE(mocked_esp_timer_stop);
    mocked_xSemaphoreCreateMutex_fake.return_val = (SemaphoreHandle_t)1;
    mocked_xSemaphoreTake_fake.return_val = pdTRUE;
    mocked_xSemaphoreGive_fake.return_val = pdTRUE;
    mock_eventBus.Publish=mock_publish;
    mock_event_published_count = 0;
    memset(last_event_name, 0, sizeof(last_event_name));
    mock_temp_val = 25.0f;

    memset(&mock_settings, 0, sizeof(SystemSettings));
    sensor_snapshot_result = ESP_OK;
    sensor_subscribe_result = 0;
    mock_settings.thermal.cpu_polling_interval_ms = 5000;
    mock_settings.thermal.cpu_temp_warning = 70.0f;
    mock_settings.thermal.cpu_temp_critical = 80.0f;
    mock_settings.thermal.cpu_temp_emergency = 90.0f;

    ServiceLocator_GetService_fake.custom_fake = my_get_service;
    temperature_sensor_install_fake.custom_fake = install_sensor;
    temperature_sensor_enable_fake.return_val = ESP_OK;
    temperature_sensor_get_celsius_fake.custom_fake = my_get_celsius;
    esp_timer_create_fake.custom_fake = create_timer;
    esp_timer_start_periodic_fake.return_val = ESP_OK;
    esp_timer_stop_fake.return_val = ESP_OK;

    // Reset the singleton.
    IInternalTemperatureSensor interface = *InternalTemperatureSensor_GetModule();
    memset(&cpuSensorInstance, 0, sizeof(cpuSensorInstance));
    cpuSensorInstance.base = interface;
    cpuSensorInstance.filtered_temp = -273.15f;
    cpuSensorInstance.state = SERVICE_STATE_UNINITIALIZED;
    cpuSensorInstance.last_temp = -273.15f;
    cpuSensorInstance.current_level = THERMAL_LEVEL_NORMAL;
}

static void InternalTempFixture_TearDown(void) {}


#endif
