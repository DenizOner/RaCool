#include "unity.h"
#define ServiceLocator_GetService IRTarget_GetService
#define InfraredTemperatureSensor_RegisterConsoleCommands IRTarget_RegisterConsoleCommands
#include "../../../../src/Hardware/InfraredTemperatureSensor/InfraredTemperatureSensor.c"
#undef ServiceLocator_GetService
#undef InfraredTemperatureSensor_RegisterConsoleCommands
#include "../../../../src/Hardware/InfraredTemperatureSensor/InfraredTemperatureSensorConfig.c"

/* Real FreeRTOS worker/locks with an injected blocking peripheral boundary.
 * This case does not claim physical MLX90614 or I2C wire verification. */
static SemaphoreHandle_t read_release;
static atomic_bool read_entered, block_read;
static atomic_uint read_calls, delivered_events;
static bool sensor_initialized;

static esp_err_t target_add_device(II2CManager *self, const i2c_device_config_t *config,
                                  i2c_master_dev_handle_t *out) {
  (void)self; (void)config;
  *out = (i2c_master_dev_handle_t)(uintptr_t)1;
  return ESP_OK;
}

static esp_err_t target_read_register(II2CManager *self, i2c_master_dev_handle_t device,
                                     uint8_t reg, uint8_t *data, size_t size) {
  (void)self; (void)device;
  atomic_fetch_add(&read_calls, 1);
  if (atomic_load(&block_read) && reg == 0x07) {
    atomic_store(&read_entered, true);
    xSemaphoreTake(read_release, portMAX_DELAY);
  }
  if (!data || size != 3) return ESP_ERR_INVALID_ARG;
  data[0] = 0x3C;
  data[1] = 0x3A;
  uint8_t bytes[] = {0xB4, reg, 0xB5, data[0], data[1]};
  data[2] = CalculatePEC(bytes, sizeof(bytes));
  return ESP_OK;
}

static int32_t target_publish(BusEvent event) {
  if (event.data && event.dataSize == sizeof(IRTempSample) &&
      strcmp(event.eventName, EVENT_IR_TEMP_UPDATED) == 0)
    atomic_fetch_add(&delivered_events, 1);
  return ESP_OK;
}
static esp_err_t target_remove_device(II2CManager *self, i2c_master_dev_handle_t handle) {
  (void)self; (void)handle;
  return ESP_OK;
}
static II2CManager target_i2c = {.AddDevice = target_add_device, .RemoveDevice = target_remove_device, .ReadRegister = target_read_register};
static IEventBus target_bus = {.Publish = target_publish};

IService *IRTarget_GetService(const char *name) {
  if (strcmp(name, SVC_NAME_I2C_MANAGER) == 0) return (IService *)&target_i2c;
  if (strcmp(name, SVC_NAME_EVENT_BUS) == 0) return (IService *)&target_bus;
  return NULL;
}
esp_err_t IRTarget_RegisterConsoleCommands(void) { return ESP_OK; }

void target_infrared_worker_cleanup(void) {
  atomic_store(&block_read, false);
  if (read_release) xSemaphoreGive(read_release);
  IInfraredTemperatureSensor *sensor = InfraredTemperatureSensor_GetModule();
  int32_t result = sensor_initialized ? sensor->base.Stop(&sensor->base) : 0;
  if (result == 0 && read_release) {
    vSemaphoreDelete(read_release);
    read_release = NULL;
  }
  TEST_ASSERT_EQUAL(0, result);
}

void target_infrared_worker_verify_shutdown(void) {
  IInfraredTemperatureSensor *sensor = InfraredTemperatureSensor_GetModule();
  read_release = xSemaphoreCreateBinary();
  TEST_ASSERT_NOT_NULL(read_release);
  atomic_store(&read_entered, false);
  atomic_store(&block_read, true);
  atomic_store(&read_calls, 0);
  atomic_store(&delivered_events, 0);
  TEST_ASSERT_EQUAL(0, sensor->base.Initialize(&sensor->base, NULL));
  sensor_initialized = true;
  TEST_ASSERT_EQUAL(0, sensor->base.Start(&sensor->base));
  TickType_t start = xTaskGetTickCount();
  while (!atomic_load(&read_entered) && xTaskGetTickCount() - start < pdMS_TO_TICKS(2000)) vTaskDelay(1);
  TEST_ASSERT_TRUE(atomic_load(&read_entered));
  TaskHandle_t worker = atomic_load(&irSensorInstance.worker.task);
  SemaphoreHandle_t cache = irSensorInstance.cache_mutex;
  TEST_ASSERT_EQUAL(-1, sensor->base.Stop(&sensor->base));
  TEST_ASSERT_EQUAL_PTR(worker, atomic_load(&irSensorInstance.worker.task));
  TEST_ASSERT_EQUAL_PTR(cache, irSensorInstance.cache_mutex);
  TEST_ASSERT_EQUAL(-1, sensor->base.Start(&sensor->base));
  unsigned reads = atomic_load(&read_calls);
  sensor->TriggerMeasurement(sensor);
  TEST_ASSERT_EQUAL(reads, atomic_load(&read_calls));
  TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, sensor->SelfTest(sensor));
  atomic_store(&block_read, false);
  xSemaphoreGive(read_release);
  TEST_ASSERT_EQUAL(0, sensor->base.Stop(&sensor->base));
  TEST_ASSERT_NULL(atomic_load(&irSensorInstance.worker.task));
  TEST_ASSERT_EQUAL_PTR(cache, irSensorInstance.cache_mutex);
  TEST_ASSERT_EQUAL(0, sensor->base.Start(&sensor->base));
  reads = atomic_load(&read_calls);
  start = xTaskGetTickCount();
  while (atomic_load(&read_calls) < reads + 2 && xTaskGetTickCount() - start < pdMS_TO_TICKS(2000)) vTaskDelay(1);
  TEST_ASSERT_GREATER_OR_EQUAL(reads + 2, atomic_load(&read_calls));
  TEST_ASSERT_EQUAL(0, sensor->base.Stop(&sensor->base));
  TEST_ASSERT_GREATER_THAN(0, atomic_load(&delivered_events));
  IRTempSample sample, repeated;
  TEST_ASSERT_EQUAL(ESP_OK, sensor->ReadSample(sensor, &sample));
  TEST_ASSERT_TRUE(sample.measured_time_us > 0);
  TEST_ASSERT_TRUE(sample.measured_time_us <= esp_timer_get_time());
  TEST_ASSERT_FLOAT_WITHIN(0.1f, 25.01f, sample.temperatures[0]);
  TEST_ASSERT_FLOAT_WITHIN(0.1f, 25.01f, sample.temperatures[1]);
  vTaskDelay(pdMS_TO_TICKS(20));
  TEST_ASSERT_EQUAL(ESP_OK, sensor->ReadSample(sensor, &repeated));
  TEST_ASSERT_TRUE(sample.measured_time_us == repeated.measured_time_us);
  TEST_ASSERT_EQUAL(sample.measured_tick, repeated.measured_tick);
  IRTempStats stats;
  TEST_ASSERT_EQUAL(ESP_OK, sensor->GetStats(sensor, &stats));
  TEST_ASSERT_GREATER_OR_EQUAL(2, stats.total_reads);
  TEST_ASSERT_EQUAL(0, stats.i2c_errors);
  TEST_ASSERT_EQUAL(0, stats.pec_errors);
}
