#ifndef INFRARED_TEST_FIXTURE_H
#define INFRARED_TEST_FIXTURE_H
#include <unity.h>
#include <string.h>
#include <stdlib.h>
#include "test_mocks.h"

#include "Hardware/InfraredTemperatureSensor/IInfraredTemperatureSensor.h"
#include "Hardware/InfraredTemperatureSensor/InfraredTemperatureSensorConsoleCommands.h"
#include "Core/EventBus/IEventBus.h"

#include "Hardware/InfraredTemperatureSensor/InfraredTemperatureSensorConfig.c"
#include "Infrastructure/support/console_output_capture.h"
#define printf ConsoleCapture_Printf
#include "Hardware/InfraredTemperatureSensor/InfraredTemperatureSensorConsoleCommands.c"
#undef printf
#include "Management/Watchdog/WatchdogWorker.c"
#include "Hardware/InfraredTemperatureSensor/InfraredTemperatureSensor.c"


static uint8_t mock_read_data[3];
static esp_err_t mock_read_register_err = ESP_OK;
static esp_err_t mock_object_read_err = ESP_OK;
static esp_err_t mock_ambient_read_err = ESP_OK;
static bool event_published = false;
static float published_temps[2] = {0};
static esp_err_t mock_publish_result = ESP_OK;
static unsigned mock_publish_attempts;
static unsigned mock_register_reads;
static esp_err_t mock_add_result = ESP_OK;
static unsigned mock_add_calls;
static unsigned mock_remove_calls;
static esp_err_t mock_remove_result;
static esp_err_t mock_console_result;
static unsigned mock_console_calls;

static esp_err_t my_i2c_read_register(II2CManager *self, i2c_master_dev_handle_t dev_handle, uint8_t reg_addr, uint8_t *data, size_t len) {
  mock_register_reads++;
  if (len >= 2) {
    data[0] = mock_read_data[0];
    data[1] = mock_read_data[1];
    if (len >= 3) {
      if (mock_read_data[2] != 0xFF) {
        data[2] = mock_read_data[2];
      } else {
        uint8_t pec_buf[5] = {(0x5A << 1), reg_addr, (uint8_t)((0x5A << 1) | 1), data[0], data[1]};
        data[2] = CalculatePEC(pec_buf, 5);
      }
    }
  }
  if (mock_read_register_err != ESP_OK) return mock_read_register_err;
  return reg_addr == 0x07 ? mock_object_read_err : mock_ambient_read_err;
}

static esp_err_t my_i2c_add_device(II2CManager *self, const i2c_device_config_t *dev_cfg, i2c_master_dev_handle_t *ret_handle) {
    mock_add_calls++;
    if (mock_add_result != ESP_OK) return mock_add_result;
    if (ret_handle) {
        *ret_handle = (i2c_master_dev_handle_t)(uintptr_t)0x1234;
    }
    return ESP_OK;
}

static esp_err_t my_i2c_remove_device(II2CManager *self, i2c_master_dev_handle_t handle) {
    (void)self; (void)handle;
    mock_remove_calls++;
    return mock_remove_result;
}

static II2CManager mock_i2c_manager_inst = {
    .ReadRegister = my_i2c_read_register,
    .AddDevice = my_i2c_add_device,
    .RemoveDevice = my_i2c_remove_device
};

static esp_err_t my_console_reg(IDebugConsole *self, const esp_console_cmd_t *cmd) {
    mock_console_calls++;
    return mock_console_result;
}

static esp_err_t my_console_reg_cat(IDebugConsole *self, const esp_console_cmd_t *cmd, CommandCategory cat) {
    mock_console_calls++;
    return mock_console_result;
}

static int32_t my_eventbus_publish(BusEvent event) {
    mock_publish_attempts++;
    if (mock_publish_result != ESP_OK) return mock_publish_result;
    if (strcmp(event.eventName, EVENT_IR_TEMP_UPDATED) == 0 && event.data && event.dataSize == sizeof(IRTempSample)) {
        event_published = true;
        memcpy(published_temps, event.data, sizeof(published_temps));
    }
    return ESP_OK;
}

static BaseType_t my_create_worker(TaskFunction_t fn, const char *name, uint32_t stack,
    void *arg, UBaseType_t priority, TaskHandle_t *out) {
  (void)fn; (void)name; (void)stack; (void)arg; (void)priority;
  *out = (TaskHandle_t)(uintptr_t)0x9999;
  return pdPASS;
}

static BaseType_t my_stop_ack(QueueHandle_t queue, void *data, TickType_t timeout) {
  (void)queue; (void)timeout;
  *(int32_t *)data = 0;
  return pdTRUE;
}

static IEventBus mock_eventbus_inst = {
    .Publish = my_eventbus_publish
};

static IDebugConsole mock_console_inst = {
    .RegisterCommand = my_console_reg,
    .RegisterCommandWithCategory = my_console_reg_cat
};

static IService* my_service_locator(const char* name) {
    if (name && strcmp(name, SVC_NAME_I2C_MANAGER) == 0) return (IService*)&mock_i2c_manager_inst;
    if (name && strcmp(name, SVC_NAME_EVENT_BUS) == 0) return (IService*)&mock_eventbus_inst;
    if (name && strcmp(name, SVC_NAME_DEBUG_CONSOLE) == 0) return (IService*)&mock_console_inst;
    return NULL;
}

static void IRFixture_SetUp(void) {
  RESET_FAKE(mocked_xSemaphoreCreateMutex);
  RESET_FAKE(mocked_xSemaphoreTake);
  RESET_FAKE(mocked_xSemaphoreGive);
  RESET_FAKE(mocked_ServiceLocator_GetService);
  RESET_FAKE(mocked_esp_timer_create);
  RESET_FAKE(mocked_esp_timer_start_periodic);
  RESET_FAKE(mocked_esp_timer_stop);
  RESET_FAKE(mocked_esp_timer_delete);
  RESET_FAKE(mocked_esp_timer_get_time);
  RESET_FAKE(mocked_xTaskCreate);
  RESET_FAKE(mocked_xQueueCreate);
  RESET_FAKE(mocked_xQueueReceive);
  RESET_FAKE(mocked_xTaskGetCurrentTaskHandle);
  RESET_FAKE(mocked_xTaskGetTickCount);
  RESET_FAKE(mocked_vTaskDelete);
  RESET_FAKE(mocked_vTaskDelay);
  FFF_RESET_HISTORY();

  mocked_ServiceLocator_GetService_fake.custom_fake = my_service_locator;
  mocked_xTaskCreate_fake.custom_fake = my_create_worker;
  mocked_xQueueCreate_fake.return_val = (QueueHandle_t)(uintptr_t)0x4321;
  mocked_xQueueReceive_fake.custom_fake = my_stop_ack;
  mocked_esp_timer_start_periodic_fake.return_val = ESP_OK;
  mocked_esp_timer_stop_fake.return_val = ESP_OK;
  mocked_esp_timer_delete_fake.return_val = ESP_OK;
  mock_read_register_err = ESP_OK;
  mock_object_read_err = ESP_OK;
  mock_ambient_read_err = ESP_OK;
  mocked_xSemaphoreCreateMutex_fake.return_val = (SemaphoreHandle_t)(uintptr_t)0x5678;
  mocked_xSemaphoreTake_fake.return_val = pdTRUE;
  mocked_xSemaphoreGive_fake.return_val = pdTRUE;

  InfraredTemperatureSensor_GetModule();
  memset(mock_read_data, 0, sizeof(mock_read_data));
  irSensorInstance.state = SERVICE_STATE_UNINITIALIZED;
  irSensorInstance.cached_object_temp = IRTEMP_CACHE_UNINITIALIZED;
  irSensorInstance.cached_ambient_temp = IRTEMP_CACHE_UNINITIALIZED;
  irSensorInstance.last_published_obj_temp = IRTEMP_CACHE_UNINITIALIZED;
  irSensorInstance.last_published_amb_temp = IRTEMP_CACHE_UNINITIALIZED;
  irSensorInstance.last_i2c_error = ESP_OK;
  irSensorInstance.consecutive_errors = 0;
  memset(&irSensorInstance.worker, 0, sizeof(irSensorInstance.worker));
  irSensorInstance.control_mutex = NULL;
  irSensorInstance.measurement_mutex = NULL;
  atomic_store(&irSensorInstance.stopping, false);
  atomic_store(&irSensorInstance.polling_interval_ms, 100);
  irSensorInstance.i2c_handle = NULL;
  irSensorInstance.cache_mutex = NULL;
  memset(&irSensorInstance.stats, 0, sizeof(IRTempStats));

  event_published = false;
  mock_publish_result = ESP_OK;
  mock_publish_attempts = 0;
  mock_register_reads = 0;
  mock_add_result = ESP_OK;
  mock_add_calls = 0;
  mock_remove_calls = 0;
  mock_remove_result = ESP_OK;
  mock_console_result = ESP_OK;
  mock_console_calls = 0;
  mock_console_inst.RegisterCommand = my_console_reg;
  mock_console_inst.RegisterCommandWithCategory = my_console_reg_cat;
  published_temps[0] = 0.0f;
  published_temps[1] = 0.0f;
}

static void IRFixture_TearDown(void) {}


#endif
