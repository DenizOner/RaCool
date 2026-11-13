#ifdef RACOOL_TEST_NATIVE
#include "infrared_fixture.h"
#include <setjmp.h>
void setUp(void) { IRFixture_SetUp(); TEST_ASSERT_TRUE(Localization_Apply("en")); }
void tearDown(void) { IRFixture_TearDown(); }

void test_IRTemp_ConsoleAgeUsesCompleteCachedPairAndRejectsMissingSample(void) {
  IInfraredTemperatureSensor *sensor=InfraredTemperatureSensor_GetModule();
  TEST_ASSERT_EQUAL(0,sensor->base.Initialize(&sensor->base,NULL));
  TEST_ASSERT_EQUAL(0,sensor->base.Start(&sensor->base));
  char *args[]={"ir_temp","status"};ConsoleCapture_Clear();
  TEST_ASSERT_NOT_EQUAL(0,cmd_ir_temp_handler(2,args));
  TEST_ASSERT_NOT_NULL(strstr(console_output,"Complete sample unavailable"));
  mock_read_data[0]=0x3c;mock_read_data[1]=0x3a;mock_read_data[2]=0xff;
  mocked_esp_timer_get_time_fake.return_val=1000000;sensor->TriggerMeasurement(sensor);
  unsigned reads=mock_register_reads;
  mocked_esp_timer_get_time_fake.return_val=1500000;ConsoleCapture_Clear();
  TEST_ASSERT_EQUAL(0,cmd_ir_temp_handler(2,args));
  TEST_ASSERT_NOT_NULL(strstr(console_output,"Sample Age: 500 ms"));
  TEST_ASSERT_EQUAL(reads,mock_register_reads);
  IRTempSample snapshot;TEST_ASSERT_EQUAL(ESP_OK,sensor->ReadSample(sensor,&snapshot));
  TEST_ASSERT_TRUE(snapshot.measured_time_us==1000000);
  mocked_esp_timer_get_time_fake.return_val=999000;ConsoleCapture_Clear();
  TEST_ASSERT_EQUAL(0,cmd_ir_temp_handler(2,args));
  TEST_ASSERT_NOT_NULL(strstr(console_output,"Sample Age: unavailable"));
  mocked_xSemaphoreTake_fake.return_val=pdFALSE;ConsoleCapture_Clear();
  TEST_ASSERT_NOT_EQUAL(0,cmd_ir_temp_handler(2,args));
  TEST_ASSERT_NOT_NULL(strstr(console_output,"Complete sample unavailable"));
}

void test_IRTemp_MachineSnapshotPreservesAcquisitionAcrossLanguages(void) {
  IInfraredTemperatureSensor *sensor=InfraredTemperatureSensor_GetModule();
  TEST_ASSERT_EQUAL(0,sensor->base.Initialize(&sensor->base,NULL));
  TEST_ASSERT_EQUAL(0,sensor->base.Start(&sensor->base));
  mock_read_data[0]=0x3c;mock_read_data[1]=0x3a;mock_read_data[2]=0xff;
  mocked_esp_timer_get_time_fake.return_val=1000000;sensor->TriggerMeasurement(sensor);
  unsigned reads=mock_register_reads;mocked_esp_timer_get_time_fake.return_val=1500000;
  char *args[]={"ir_temp","status","--machine"};ConsoleCapture_Clear();
  TEST_ASSERT_EQUAL(0,cmd_ir_temp_handler(3,args));
  char english[1024];snprintf(english,sizeof(english),"%s",console_output);
  TEST_ASSERT_NOT_NULL(strstr(english,"type=ir_sensor_status object_c="));
  TEST_ASSERT_NOT_NULL(strstr(english,"measured_time_us=1000000 age_ms=500"));
  TEST_ASSERT_TRUE(Localization_Apply("tr"));ConsoleCapture_Clear();
  TEST_ASSERT_EQUAL(0,cmd_ir_temp_handler(3,args));
  TEST_ASSERT_EQUAL_STRING(english,console_output);TEST_ASSERT_EQUAL(reads,mock_register_reads);
  TEST_ASSERT_TRUE(Localization_Apply("en"));
}

void test_IRTemp_ConsoleMissingCallbacksRejectBeforeSdkCalls(void) {
  IInfraredTemperatureSensor *sensor=InfraredTemperatureSensor_GetModule();
  IInfraredTemperatureSensor original=*sensor;
  sensor->GetPollingInterval=NULL;sensor->TriggerMeasurement=NULL;sensor->GetStats=NULL;
  sensor->SelfTest=NULL;sensor->GetLastError=NULL;sensor->ReadRawRegisters=NULL;
  char *commands[]={"status","trigger","stats","selftest","lasterr","raw"};int results[6];
  unsigned reads=mock_register_reads;
  for (unsigned i=0;i<6;++i) {char *args[]={"ir_temp",commands[i]};results[i]=cmd_ir_temp_handler(2,args);}
  *sensor=original;
  for (unsigned i=0;i<6;++i) TEST_ASSERT_NOT_EQUAL(0,results[i]);
  TEST_ASSERT_EQUAL(reads,mock_register_reads);
}

static SystemSettings calibration_snapshot;
static esp_err_t calibration_snapshot_result;
static esp_err_t read_calibration_snapshot(IConfigurationManager *self, SystemSettings *out) {
  (void)self;
  if (calibration_snapshot_result == ESP_OK) *out = calibration_snapshot;
  return calibration_snapshot_result;
}

void test_IRTemp_CalibrationUsesSnapshotAndFailureRetainsRawMeasurement(void) {
  IInfraredTemperatureSensor *sensor = InfraredTemperatureSensor_GetModule();
  TEST_ASSERT_EQUAL(0, sensor->base.Initialize(&sensor->base, NULL));
  IConfigurationManager config = {.GetSnapshot = read_calibration_snapshot};
  mocked_ServiceLocator_GetService_fake.custom_fake = NULL;
  mocked_ServiceLocator_GetService_fake.return_val = &config.base;
  calibration_snapshot = (SystemSettings){0};
  calibration_snapshot.calibration.ir_object_cal = 1.2f;
  calibration_snapshot.calibration.ir_ambient_cal = -0.4f;
  calibration_snapshot_result = ESP_OK;
  irSensorInstance.cached_object_temp = IRTEMP_CACHE_UNINITIALIZED;
  irSensorInstance.cached_ambient_temp = IRTEMP_CACHE_UNINITIALIZED;
  uint16_t raw = 14907;
  TEST_ASSERT_TRUE(ProcessTemperature(raw, true));
  TEST_ASSERT_TRUE(ProcessTemperature(raw, false));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, RawToCelsius(raw) + 1.2f, irSensorInstance.cached_object_temp);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, RawToCelsius(raw) - 0.4f, irSensorInstance.cached_ambient_temp);
  calibration_snapshot_result = ESP_FAIL;
  irSensorInstance.cached_object_temp = IRTEMP_CACHE_UNINITIALIZED;
  TEST_ASSERT_TRUE(ProcessTemperature(raw, true));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, RawToCelsius(raw), irSensorInstance.cached_object_temp);
}

void test_IRTemp_Lifecycle(void) {
  IInfraredTemperatureSensor *sensor = InfraredTemperatureSensor_GetModule();
  IService *service = &(sensor->base);

  TEST_ASSERT_EQUAL(0, service->Initialize(service, NULL));
  TEST_ASSERT_EQUAL(SERVICE_STATE_INITIALIZED, irSensorInstance.state);

  TEST_ASSERT_EQUAL(0, service->Start(service));
  TEST_ASSERT_EQUAL(SERVICE_STATE_STARTED, irSensorInstance.state);

  TEST_ASSERT_EQUAL(0, service->CheckHealth(service));

  TEST_ASSERT_EQUAL(0, service->Stop(service));
  TEST_ASSERT_EQUAL(SERVICE_STATE_STOPPED, irSensorInstance.state);
}

void test_IRTemp_ReadTemp(void) {
  IInfraredTemperatureSensor *sensor = InfraredTemperatureSensor_GetModule();
  irSensorInstance.state = SERVICE_STATE_UNINITIALIZED;
  sensor->base.Initialize(&sensor->base, NULL);
  sensor->base.Start(&sensor->base);

  mock_read_data[0] = 0x3C;
  mock_read_data[1] = 0x3A;
  mock_read_data[2] = 0xFF;

  sensor->TriggerMeasurement(sensor);

  float temp_obj = sensor->ReadObjectTemperature(sensor);
  TEST_ASSERT_FLOAT_WITHIN(0.2f, 25.01f, temp_obj);

  float temp_amb = sensor->ReadAmbientTemperature(sensor);
  TEST_ASSERT_FLOAT_WITHIN(0.2f, 25.01f, temp_amb);
  TEST_ASSERT_TRUE(event_published);
}

void test_IRTemp_PECValidationFailure(void) {
  IInfraredTemperatureSensor *sensor = InfraredTemperatureSensor_GetModule();
  irSensorInstance.state = SERVICE_STATE_UNINITIALIZED;
  sensor->base.Initialize(&sensor->base, NULL);
  sensor->base.Start(&sensor->base);

  mock_read_data[0] = 0x3C;
  mock_read_data[1] = 0x3A;
  mock_read_data[2] = 0xDE;

  sensor->TriggerMeasurement(sensor);

  TEST_ASSERT_EQUAL_FLOAT(IRTEMP_CACHE_UNINITIALIZED, sensor->ReadObjectTemperature(sensor));
  TEST_ASSERT_EQUAL(2, irSensorInstance.consecutive_errors);
  
  IRTempStats stats;
  TEST_ASSERT_EQUAL(ESP_OK, sensor->GetStats(sensor, &stats));
  TEST_ASSERT_EQUAL(2, stats.pec_errors);
  TEST_ASSERT_EQUAL(IRTEMP_FAILURE_PEC, stats.last_failure_kind);
  TEST_ASSERT_EQUAL(ESP_ERR_INVALID_RESPONSE, stats.last_failure_error);
}

void test_IRTemp_SelfTestAndErrorHandling(void) {
  IInfraredTemperatureSensor *sensor = InfraredTemperatureSensor_GetModule();
  irSensorInstance.state = SERVICE_STATE_UNINITIALIZED;
  sensor->base.Initialize(&sensor->base, NULL);
  sensor->base.Start(&sensor->base);

  mock_read_data[0] = 0x3C;
  mock_read_data[1] = 0x3A;
  mock_read_data[2] = 0xFF;

  TEST_ASSERT_EQUAL(ESP_OK, sensor->SelfTest(sensor));
  TEST_ASSERT_EQUAL(ESP_OK, sensor->GetLastError(sensor));
}

void test_IRTemp_StatsAndLimits(void) {
  IInfraredTemperatureSensor *sensor = InfraredTemperatureSensor_GetModule();
  irSensorInstance.state = SERVICE_STATE_UNINITIALIZED;
  sensor->base.Initialize(&sensor->base, NULL);
  sensor->base.Start(&sensor->base);

  TEST_ASSERT_EQUAL(ESP_OK, sensor->SetLimits(sensor, -10.0f, 100.0f));
  TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, sensor->SetLimits(sensor, 50.0f, 20.0f));

  TEST_ASSERT_EQUAL(ESP_OK, sensor->SetPollingInterval(sensor, 500));
  TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, sensor->SetPollingInterval(sensor, 10));

  IRTempStats stats;
  TEST_ASSERT_EQUAL(ESP_OK, sensor->GetStats(sensor, &stats));
  TEST_ASSERT_EQUAL(0, stats.total_reads);

  mock_read_data[0] = 0x3C;
  mock_read_data[1] = 0x3A;
  mock_read_data[2] = 0xFF;

  sensor->TriggerMeasurement(sensor);
  TEST_ASSERT_EQUAL(ESP_OK, sensor->GetStats(sensor, &stats));
  TEST_ASSERT_EQUAL(1, stats.total_reads);

  TEST_ASSERT_EQUAL(ESP_OK, sensor->ResetStats(sensor));
  TEST_ASSERT_EQUAL(ESP_OK, sensor->GetStats(sensor, &stats));
  TEST_ASSERT_EQUAL(0, stats.total_reads);
}

void test_IRTemp_ZeroPecMustMatchChecksum(void) {
  IInfraredTemperatureSensor *sensor = InfraredTemperatureSensor_GetModule();
  TEST_ASSERT_EQUAL(0, sensor->base.Initialize(&sensor->base, NULL));
  TEST_ASSERT_EQUAL(0, sensor->base.Start(&sensor->base));
  mock_read_data[0] = 0x3C;
  mock_read_data[1] = 0x3A;
  mock_read_data[2] = 0;
  sensor->TriggerMeasurement(sensor);
  IRTempStats stats;
  TEST_ASSERT_EQUAL(ESP_OK, sensor->GetStats(sensor, &stats));
  TEST_ASSERT_EQUAL(2, stats.pec_errors);
  TEST_ASSERT_EQUAL(IRTEMP_FAILURE_PEC, stats.last_failure_kind);
  TEST_ASSERT_EQUAL(ESP_ERR_INVALID_RESPONSE, stats.last_failure_error);
  TEST_ASSERT_EQUAL_FLOAT(IRTEMP_CACHE_UNINITIALIZED, sensor->ReadObjectTemperature(sensor));
  TEST_ASSERT_EQUAL_FLOAT(IRTEMP_CACHE_UNINITIALIZED, sensor->ReadAmbientTemperature(sensor));
  TEST_ASSERT_EQUAL(ESP_ERR_INVALID_RESPONSE, sensor->SelfTest(sensor));

  // CRC-8 vectors: B4 07 B5 F9 39 -> 00; B4 06 B5 EC 39 -> 00.
  mock_read_data[0] = 0xF9;
  mock_read_data[1] = 0x39;
  sensor->TriggerMeasurement(sensor);
  TEST_ASSERT_FLOAT_WITHIN(0.02f, 23.67f, sensor->ReadObjectTemperature(sensor));
  TEST_ASSERT_EQUAL(ESP_OK, sensor->SelfTest(sensor));
  mock_read_data[0] = 0xEC;
  sensor->TriggerMeasurement(sensor);
  TEST_ASSERT_FLOAT_WITHIN(0.02f, 23.41f, sensor->ReadAmbientTemperature(sensor));
}

void test_IRTemp_SelfTestRejectsCorruptionAndSensorError(void) {
  IInfraredTemperatureSensor *sensor = InfraredTemperatureSensor_GetModule();
  TEST_ASSERT_EQUAL(0, sensor->base.Initialize(&sensor->base, NULL));
  mock_read_data[0] = 0x3C;
  mock_read_data[1] = 0x3A;
  mock_read_data[2] = 0xDE;
  TEST_ASSERT_EQUAL(ESP_ERR_INVALID_RESPONSE, sensor->SelfTest(sensor));
  mock_read_data[1] = 0xBA;
  mock_read_data[2] = 0xFF; // Fixture supplies the correct CRC for the error-marked sample.
  TEST_ASSERT_EQUAL(ESP_ERR_INVALID_RESPONSE, sensor->SelfTest(sensor));
  mock_read_register_err = ESP_ERR_TIMEOUT;
  TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT, sensor->SelfTest(sensor));
}

void test_IRTemp_FailedPairDoesNotPublishCachedHeartbeat(void) {
  IInfraredTemperatureSensor *sensor = InfraredTemperatureSensor_GetModule();
  TEST_ASSERT_EQUAL(0, sensor->base.Initialize(&sensor->base, NULL));
  TEST_ASSERT_EQUAL(0, sensor->base.Start(&sensor->base));
  mock_read_data[0] = 0x3C;
  mock_read_data[1] = 0x3A;
  mock_read_data[2] = 0xFF;
  mocked_esp_timer_get_time_fake.return_val = 1000000;
  sensor->TriggerMeasurement(sensor);
  TEST_ASSERT_TRUE(event_published);
  event_published = false;
  mocked_esp_timer_get_time_fake.return_val = 7000000;
  mock_object_read_err = ESP_ERR_TIMEOUT;
  sensor->TriggerMeasurement(sensor);
  TEST_ASSERT_FALSE(event_published);
  TEST_ASSERT_EQUAL_INT64(1000000, irSensorInstance.last_publish_time_us);
  mock_object_read_err = ESP_OK;
  mock_ambient_read_err = ESP_ERR_TIMEOUT;
  sensor->TriggerMeasurement(sensor);
  TEST_ASSERT_FALSE(event_published);
  TEST_ASSERT_EQUAL_INT64(1000000, irSensorInstance.last_publish_time_us);
  mock_ambient_read_err = ESP_OK;
  sensor->TriggerMeasurement(sensor);
  TEST_ASSERT_TRUE(event_published);
  TEST_ASSERT_EQUAL_INT64(7000000, irSensorInstance.last_publish_time_us);
}

void test_IRTemp_InvalidFirstSamplesNeverPublish(void) {
  IInfraredTemperatureSensor *sensor = InfraredTemperatureSensor_GetModule();
  TEST_ASSERT_EQUAL(0, sensor->base.Initialize(&sensor->base, NULL));
  TEST_ASSERT_EQUAL(0, sensor->base.Start(&sensor->base));
  mock_read_register_err = ESP_ERR_TIMEOUT;
  sensor->TriggerMeasurement(sensor);
  TEST_ASSERT_FALSE(event_published);
  mock_read_register_err = ESP_OK;
  mock_read_data[0] = 0x3C;
  mock_read_data[1] = 0x3A;
  mock_read_data[2] = 0;
  sensor->TriggerMeasurement(sensor);
  TEST_ASSERT_FALSE(event_published);
  mock_read_data[2] = 0xFF;
  mock_read_data[1] = 0xBA;
  sensor->TriggerMeasurement(sensor);
  TEST_ASSERT_FALSE(event_published);
  mock_read_data[1] = 0x7F;
  mock_read_data[0] = 0xFF;
  sensor->TriggerMeasurement(sensor);
  TEST_ASSERT_FALSE(event_published);
  TEST_ASSERT_EQUAL_INT64(0, irSensorInstance.last_publish_time_us);
}

void test_IRTemp_PersistentAmbientFailureRemainsUnhealthy(void) {
  IInfraredTemperatureSensor *sensor = InfraredTemperatureSensor_GetModule();
  TEST_ASSERT_EQUAL(0, sensor->base.Initialize(&sensor->base, NULL));
  TEST_ASSERT_EQUAL(0, sensor->base.Start(&sensor->base));
  mock_read_data[0] = 0x3C;
  mock_read_data[1] = 0x3A;
  mock_read_data[2] = 0xFF;
  mock_ambient_read_err = ESP_ERR_TIMEOUT;
  for (int i = 0; i < 6; ++i) sensor->TriggerMeasurement(sensor);
  TEST_ASSERT_EQUAL(-2, sensor->base.CheckHealth(&sensor->base));
  TEST_ASSERT_FALSE(event_published);
  mock_ambient_read_err = ESP_OK;
  sensor->TriggerMeasurement(sensor);
  TEST_ASSERT_EQUAL(0, sensor->base.CheckHealth(&sensor->base));
  TEST_ASSERT_TRUE(event_published);
}

void test_IRTemp_InitializationFailuresAreRetryable(void) {
  for (unsigned failure = 0; failure < 4; ++failure) {
    IRFixture_SetUp();
    SemaphoreHandle_t values[4] = {(void *)0x10, (void *)0x20, (void *)0x30, (void *)0x40};
    values[failure] = NULL;
    SET_RETURN_SEQ(mocked_xSemaphoreCreateMutex, values, 4);
    IInfraredTemperatureSensor *sensor = InfraredTemperatureSensor_GetModule();
    TEST_ASSERT_EQUAL(-1, sensor->base.Initialize(&sensor->base, NULL));
    TEST_ASSERT_EQUAL(SERVICE_STATE_UNINITIALIZED, sensor->base.GetState(&sensor->base));
    TEST_ASSERT_EQUAL(0, mock_add_calls);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, sensor->SelfTest(sensor));
    RESET_FAKE(mocked_xSemaphoreCreateMutex);
    mocked_xSemaphoreCreateMutex_fake.return_val = (void *)0x5678;
    TEST_ASSERT_EQUAL(0, sensor->base.Initialize(&sensor->base, NULL));
    TEST_ASSERT_EQUAL(1, mock_add_calls);
    TEST_ASSERT_EQUAL(0, sensor->base.Initialize(&sensor->base, NULL));
    TEST_ASSERT_EQUAL(1, mock_add_calls);
  }
}

void test_IRTemp_QueueAndDeviceFailurePreserveRetry(void) {
  IInfraredTemperatureSensor *sensor = InfraredTemperatureSensor_GetModule();
  mocked_xQueueCreate_fake.return_val = NULL;
  TEST_ASSERT_EQUAL(-1, sensor->base.Initialize(&sensor->base, NULL));
  TEST_ASSERT_EQUAL(0, mock_add_calls);
  mocked_xQueueCreate_fake.return_val = (void *)0x4321;
  mock_add_result = ESP_ERR_NO_MEM;
  TEST_ASSERT_EQUAL(-1, sensor->base.Initialize(&sensor->base, NULL));
  TEST_ASSERT_EQUAL(SERVICE_STATE_UNINITIALIZED, sensor->base.GetState(&sensor->base));
  mock_add_result = ESP_OK;
  TEST_ASSERT_EQUAL(0, sensor->base.Initialize(&sensor->base, NULL));
  TEST_ASSERT_EQUAL(2, mock_add_calls);
}

void test_IRTemp_StartFailureAndStopTimeoutKeepOwnership(void) {
  IInfraredTemperatureSensor *sensor = InfraredTemperatureSensor_GetModule();
  TEST_ASSERT_EQUAL(0, sensor->base.Initialize(&sensor->base, NULL));
  mocked_xTaskCreate_fake.custom_fake = NULL;
  mocked_xTaskCreate_fake.return_val = pdFAIL;
  TEST_ASSERT_EQUAL(-1, sensor->base.Start(&sensor->base));
  TEST_ASSERT_EQUAL(SERVICE_STATE_INITIALIZED, sensor->base.GetState(&sensor->base));
  mocked_xTaskCreate_fake.custom_fake = my_create_worker;
  TEST_ASSERT_EQUAL(0, sensor->base.Start(&sensor->base));
  unsigned created = mocked_xTaskCreate_fake.call_count;
  TEST_ASSERT_EQUAL(0, sensor->base.Start(&sensor->base));
  TEST_ASSERT_EQUAL(created, mocked_xTaskCreate_fake.call_count);
  SemaphoreHandle_t cache = irSensorInstance.cache_mutex;
  TaskHandle_t worker = atomic_load(&irSensorInstance.worker.task);
  mocked_xQueueReceive_fake.custom_fake = NULL;
  mocked_xQueueReceive_fake.return_val = pdFALSE;
  TEST_ASSERT_EQUAL(-1, sensor->base.Stop(&sensor->base));
  TEST_ASSERT_EQUAL_PTR(worker, atomic_load(&irSensorInstance.worker.task));
  TEST_ASSERT_EQUAL_PTR(cache, irSensorInstance.cache_mutex);
  TEST_ASSERT_EQUAL(0, mocked_vTaskDelete_fake.call_count);
  TEST_ASSERT_EQUAL(-1, sensor->base.Start(&sensor->base));
  TEST_ASSERT_EQUAL(-1, sensor->base.CheckHealth(&sensor->base));
  sensor->TriggerMeasurement(sensor);
  TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, sensor->SelfTest(sensor));
  TEST_ASSERT_EQUAL(0, mock_register_reads);
  mocked_xQueueReceive_fake.custom_fake = my_stop_ack;
  TEST_ASSERT_EQUAL(0, sensor->base.Stop(&sensor->base));
  TEST_ASSERT_EQUAL(1, mocked_vTaskDelete_fake.call_count);
  TEST_ASSERT_EQUAL_PTR(cache, irSensorInstance.cache_mutex);
  TEST_ASSERT_EQUAL(SERVICE_STATE_STOPPED, sensor->base.GetState(&sensor->base));
  TEST_ASSERT_EQUAL(0, sensor->base.Start(&sensor->base));
  TEST_ASSERT_EQUAL(2, mock_add_calls);
  TEST_ASSERT_EQUAL(1, mock_remove_calls);
  TEST_ASSERT_EQUAL(0, mocked_esp_timer_create_fake.call_count);
}

void test_IRTemp_LockFailuresDoNotMutateState(void) {
  IInfraredTemperatureSensor *sensor = InfraredTemperatureSensor_GetModule();
  TEST_ASSERT_EQUAL(0, sensor->base.Initialize(&sensor->base, NULL));
  IRTempStats stats = {.total_reads = 123};
  mocked_xSemaphoreTake_fake.return_val = pdFALSE;
  TEST_ASSERT_EQUAL(-1, sensor->base.Start(&sensor->base));
  TEST_ASSERT_EQUAL(SERVICE_STATE_INITIALIZED, sensor->base.GetState(&sensor->base));
  TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT, sensor->GetStats(sensor, &stats));
  TEST_ASSERT_EQUAL(123, stats.total_reads);
  TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT, sensor->ResetStats(sensor));
  TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT, sensor->SelfTest(sensor));
  TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT, sensor->SetPollingInterval(sensor, 500));
  TEST_ASSERT_EQUAL(100, atomic_load(&irSensorInstance.polling_interval_ms));
  TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT, sensor->SetLimits(sensor, 0, 50));
  TEST_ASSERT_EQUAL_FLOAT(-70, irSensorInstance.config.temp_range_min);
  TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, sensor->SetLimits(sensor, NAN, 50));
  TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, sensor->SetLimits(sensor, 0, INFINITY));
  TEST_ASSERT_EQUAL(0, mock_register_reads);
}

void test_IRTemp_FailedPublishIsRetriedOnNextValidPair(void) {
  IInfraredTemperatureSensor *sensor = InfraredTemperatureSensor_GetModule();
  TEST_ASSERT_EQUAL(0, sensor->base.Initialize(&sensor->base, NULL));
  TEST_ASSERT_EQUAL(0, sensor->base.Start(&sensor->base));
  mock_read_data[0] = 0x3C;
  mock_read_data[1] = 0x3A;
  mock_read_data[2] = 0xFF;
  mock_publish_result = ESP_ERR_TIMEOUT;
  mocked_esp_timer_get_time_fake.return_val = 1000000;
  sensor->TriggerMeasurement(sensor);
  TEST_ASSERT_FALSE(event_published);
  TEST_ASSERT_EQUAL(1, mock_publish_attempts);
  TEST_ASSERT_EQUAL_INT64(0, irSensorInstance.last_publish_time_us);
  TEST_ASSERT_EQUAL_FLOAT(IRTEMP_CACHE_UNINITIALIZED, irSensorInstance.last_published_obj_temp);
  mock_publish_result = ESP_OK;
  sensor->TriggerMeasurement(sensor);
  TEST_ASSERT_TRUE(event_published);
  TEST_ASSERT_EQUAL(2, mock_publish_attempts);
  TEST_ASSERT_EQUAL_INT64(1000000, irSensorInstance.last_publish_time_us);
}

static jmp_buf worker_exit;
static unsigned worker_clock_calls;
static int64_t worker_clock(void) { return worker_clock_calls++ ? 100000 : 0; }
static void worker_delay_exit(TickType_t ticks) {
  TEST_ASSERT_EQUAL(pdMS_TO_TICKS(IRTEMP_WORKER_CHECKPOINT_MS), ticks);
  TEST_ASSERT_EQUAL_PTR(irSensorInstance.measurement_mutex, mocked_xSemaphoreGive_fake.arg0_val);
  longjmp(worker_exit, 1);
}

void test_IRTemp_WorkerMeasuresOutsideSharedTimer(void) {
  IInfraredTemperatureSensor *sensor = InfraredTemperatureSensor_GetModule();
  TEST_ASSERT_EQUAL(0, sensor->base.Initialize(&sensor->base, NULL));
  TEST_ASSERT_EQUAL(0, sensor->base.Start(&sensor->base));
  irSensorInstance.measurement_mutex = (void *)0x888;
  mock_read_data[0] = 0x3C;
  mock_read_data[1] = 0x3A;
  mock_read_data[2] = 0xFF;
  worker_clock_calls = 0;
  mocked_esp_timer_get_time_fake.custom_fake = worker_clock;
  mocked_vTaskDelay_fake.custom_fake = worker_delay_exit;
  if (setjmp(worker_exit) == 0) IRTemp_Worker(NULL);
  TEST_ASSERT_EQUAL(2, mock_register_reads);
  TEST_ASSERT_TRUE(event_published);
  TEST_ASSERT_EQUAL(0, mocked_esp_timer_create_fake.call_count);
  TEST_ASSERT_EQUAL(0, mocked_esp_timer_start_periodic_fake.call_count);
}

void test_IRTemp_ConsolePollingRejectsMalformedValues(void) {
  IInfraredTemperatureSensor *sensor = InfraredTemperatureSensor_GetModule();
  TEST_ASSERT_EQUAL(0, sensor->GetPollingInterval(sensor));
  TEST_ASSERT_EQUAL(0, sensor->base.Initialize(&sensor->base, NULL));
  const char *invalid[] = {"", "-1", "+50", "50x", " 50", "4294967296", "999999999999999999999", "0", "49", "60001"};
  for (unsigned i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
    char *args[] = {"ir_temp", "poll", (char *)invalid[i]};
    TEST_ASSERT_EQUAL(-1, cmd_ir_temp_handler(3, args));
    TEST_ASSERT_EQUAL(100, sensor->GetPollingInterval(sensor));
  }
  char *args[] = {"ir_temp", "poll", "50", "extra"};
  TEST_ASSERT_EQUAL(-1, cmd_ir_temp_handler(4, args));
  TEST_ASSERT_EQUAL(-1, cmd_ir_temp_handler(2, args));
  TEST_ASSERT_EQUAL(-1, cmd_ir_temp_handler(3, NULL));
  TEST_ASSERT_EQUAL(0, cmd_ir_temp_handler(3, args));
  TEST_ASSERT_EQUAL(50, sensor->GetPollingInterval(sensor));
  args[2] = "60000";
  TEST_ASSERT_EQUAL(0, cmd_ir_temp_handler(3, args));
  TEST_ASSERT_EQUAL(60000, sensor->GetPollingInterval(sensor));
}

void test_IRTemp_RawSnapshotPreservesOutputOnFailure(void) {
  IInfraredTemperatureSensor *sensor = InfraredTemperatureSensor_GetModule();
  TEST_ASSERT_EQUAL(0, sensor->base.Initialize(&sensor->base, NULL));
  IRTempRawRegisters output, before;
  memset(&output, 0xA5, sizeof(output));
  before = output;
  mock_object_read_err = ESP_ERR_TIMEOUT;
  TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT, sensor->ReadRawRegisters(sensor, &output));
  TEST_ASSERT_EQUAL_MEMORY(&before, &output, sizeof(output));
  TEST_ASSERT_EQUAL(4, mock_register_reads);
  TEST_ASSERT_EQUAL(1, mock_add_calls);
  mock_object_read_err = ESP_OK;
  mock_register_reads = 0;
  mock_read_data[0] = 0x3C;
  mock_read_data[1] = 0x3A;
  mock_read_data[2] = 0xFF;
  TEST_ASSERT_EQUAL(ESP_OK, sensor->ReadRawRegisters(sensor, &output));
  TEST_ASSERT_EQUAL(7, mock_register_reads);
  TEST_ASSERT_EQUAL_HEX8(0xA5, output.object1[2]);
  TEST_ASSERT_EQUAL_HEX8(0xB3, output.ambient[2]);
  TEST_ASSERT_EQUAL(1, mock_add_calls);
  TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, sensor->ReadRawRegisters(sensor, NULL));
  mocked_xSemaphoreTake_fake.return_val = pdFALSE;
  before = output;
  TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT, sensor->ReadRawRegisters(sensor, &output));
  TEST_ASSERT_EQUAL_MEMORY(&before, &output, sizeof(output));
  TEST_ASSERT_EQUAL(7, mock_register_reads);
}

void test_IRTemp_ConsolePropagatesOperationFailures(void) {
  IInfraredTemperatureSensor *sensor = InfraredTemperatureSensor_GetModule();
  TEST_ASSERT_EQUAL(0, sensor->base.Initialize(&sensor->base, NULL));
  char *args[] = {"ir_temp", "selftest"};
  mock_read_register_err = ESP_ERR_TIMEOUT;
  TEST_ASSERT_EQUAL(-1, cmd_ir_temp_handler(2, args));
  args[1] = "raw";
  TEST_ASSERT_EQUAL(-1, cmd_ir_temp_handler(2, args));
  mocked_xSemaphoreTake_fake.return_val = pdFALSE;
  args[1] = "reset";
  TEST_ASSERT_EQUAL(-1, cmd_ir_temp_handler(2, args));
  args[1] = "stats";
  TEST_ASSERT_EQUAL(-1, cmd_ir_temp_handler(2, args));
  mocked_xSemaphoreTake_fake.return_val = pdTRUE;
  args[1] = "reset";
  TEST_ASSERT_EQUAL(0, cmd_ir_temp_handler(2, args));
}

void test_IRTemp_ConsoleRegistrationPreservesFailure(void) {
  mock_console_result = ESP_ERR_NO_MEM;
  TEST_ASSERT_EQUAL(ESP_ERR_NO_MEM, InfraredTemperatureSensor_RegisterConsoleCommands());
  TEST_ASSERT_EQUAL(1, mock_console_calls);
  mock_console_inst.RegisterCommandWithCategory = NULL;
  TEST_ASSERT_EQUAL(ESP_ERR_NO_MEM, InfraredTemperatureSensor_RegisterConsoleCommands());
  TEST_ASSERT_EQUAL(2, mock_console_calls);
  mock_console_inst.RegisterCommand = NULL;
  TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, InfraredTemperatureSensor_RegisterConsoleCommands());
  TEST_ASSERT_EQUAL(2, mock_console_calls);
  mocked_ServiceLocator_GetService_fake.custom_fake = NULL;
  mocked_ServiceLocator_GetService_fake.return_val = NULL;
  unsigned sdk_calls = mocked_esp_console_cmd_register_fake.call_count;
  TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, InfraredTemperatureSensor_RegisterConsoleCommands());
  TEST_ASSERT_EQUAL(sdk_calls, mocked_esp_console_cmd_register_fake.call_count);
}

void test_IRTemp_SampleRetainsAcquisitionTimeAcrossFailures(void) {
  IInfraredTemperatureSensor *sensor = InfraredTemperatureSensor_GetModule();
  TEST_ASSERT_EQUAL(0, sensor->base.Initialize(&sensor->base, NULL));
  IRTempSample sample, before;
  memset(&sample, 0xA5, sizeof(sample)); before = sample;
  TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, sensor->ReadSample(sensor, &sample));
  TEST_ASSERT_EQUAL_MEMORY(&before, &sample, sizeof(sample));
  TEST_ASSERT_EQUAL(0, sensor->base.Start(&sensor->base));
  mock_read_data[0] = 0x3C; mock_read_data[1] = 0x3A; mock_read_data[2] = 0xFF;
  mocked_esp_timer_get_time_fake.return_val = 1000000;
  mocked_xTaskGetTickCount_fake.return_val = 100;
  sensor->TriggerMeasurement(sensor);
  TEST_ASSERT_EQUAL(ESP_OK, sensor->ReadSample(sensor, &sample));
  TEST_ASSERT_EQUAL_INT64(1000000, sample.measured_time_us);
  TEST_ASSERT_EQUAL(100, sample.measured_tick);
  before = sample;
  mocked_esp_timer_get_time_fake.return_val = 9000000;
  mocked_xTaskGetTickCount_fake.return_val = 900;
  mock_object_read_err = ESP_ERR_TIMEOUT;
  sensor->TriggerMeasurement(sensor);
  TEST_ASSERT_EQUAL(ESP_OK, sensor->ReadSample(sensor, &sample));
  TEST_ASSERT_EQUAL_MEMORY(&before, &sample, sizeof(sample));
  mock_object_read_err = ESP_OK; mock_ambient_read_err = ESP_ERR_TIMEOUT;
  sensor->TriggerMeasurement(sensor);
  TEST_ASSERT_EQUAL(ESP_OK, sensor->ReadSample(sensor, &sample));
  TEST_ASSERT_EQUAL_MEMORY(&before, &sample, sizeof(sample));
  mocked_xSemaphoreTake_fake.return_val = pdFALSE;
  TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT, sensor->ReadSample(sensor, &sample));
  TEST_ASSERT_EQUAL_MEMORY(&before, &sample, sizeof(sample));
  mocked_xSemaphoreTake_fake.return_val = pdTRUE;
  mock_ambient_read_err = ESP_OK;
  sensor->TriggerMeasurement(sensor);
  TEST_ASSERT_EQUAL(ESP_OK, sensor->ReadSample(sensor, &sample));
  TEST_ASSERT_EQUAL_INT64(9000000, sample.measured_time_us);
  TEST_ASSERT_EQUAL(900, sample.measured_tick);
  TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, sensor->ReadSample(sensor, NULL));
}

void test_IRTemp_DetachFailurePreservesOwnershipUntilRetry(void) {
  IInfraredTemperatureSensor *sensor = InfraredTemperatureSensor_GetModule();
  TEST_ASSERT_EQUAL(0, sensor->base.Initialize(&sensor->base, NULL));
  TEST_ASSERT_EQUAL(0, sensor->base.Start(&sensor->base));
  i2c_master_dev_handle_t handle = irSensorInstance.i2c_handle;
  mock_remove_result = ESP_ERR_TIMEOUT;
  TEST_ASSERT_EQUAL(-1, sensor->base.Stop(&sensor->base));
  TEST_ASSERT_EQUAL_PTR(handle, irSensorInstance.i2c_handle);
  TEST_ASSERT_TRUE(atomic_load(&irSensorInstance.stopping));
  TEST_ASSERT_EQUAL(-1, sensor->base.Start(&sensor->base));
  TEST_ASSERT_EQUAL(-1, sensor->base.CheckHealth(&sensor->base));
  unsigned reads = mock_register_reads;
  sensor->TriggerMeasurement(sensor);
  TEST_ASSERT_EQUAL(reads, mock_register_reads);
  mock_remove_result = ESP_OK;
  TEST_ASSERT_EQUAL(0, sensor->base.Stop(&sensor->base));
  TEST_ASSERT_NULL(irSensorInstance.i2c_handle);
  TEST_ASSERT_EQUAL(2, mock_remove_calls);
  TEST_ASSERT_EQUAL(0, sensor->base.Stop(&sensor->base));
  TEST_ASSERT_EQUAL(2, mock_remove_calls);
  mock_add_result = ESP_ERR_NO_MEM;
  TEST_ASSERT_EQUAL(-1, sensor->base.Start(&sensor->base));
  TEST_ASSERT_NULL(irSensorInstance.i2c_handle);
  TEST_ASSERT_EQUAL(SERVICE_STATE_STOPPED, sensor->base.GetState(&sensor->base));
  mock_add_result = ESP_OK;
  TEST_ASSERT_EQUAL(0, sensor->base.Start(&sensor->base));
  TEST_ASSERT_NOT_NULL(irSensorInstance.i2c_handle);
}

void test_IRTemp_InitializedDeviceCanDetachBeforeFirstStart(void) {
  IInfraredTemperatureSensor *sensor = InfraredTemperatureSensor_GetModule();
  TEST_ASSERT_EQUAL(0, sensor->base.Initialize(&sensor->base, NULL));
  TEST_ASSERT_EQUAL(0, sensor->base.Stop(&sensor->base));
  TEST_ASSERT_EQUAL(1, mock_remove_calls);
  TEST_ASSERT_NULL(irSensorInstance.i2c_handle);
  TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, sensor->SelfTest(sensor));
  TEST_ASSERT_EQUAL(0, sensor->base.Start(&sensor->base));
  TEST_ASSERT_EQUAL(2, mock_add_calls);
}

void test_IRTempFailureHistorySurvivesSuccessAndClearsWithStats(void) {
  IInfraredTemperatureSensor *sensor = InfraredTemperatureSensor_GetModule();
  TEST_ASSERT_EQUAL(ESP_OK, sensor->base.Initialize(&sensor->base, NULL));
  TEST_ASSERT_EQUAL(ESP_OK, sensor->base.Start(&sensor->base));
  mocked_esp_timer_get_time_fake.return_val = 9000000000LL;
  IRTemp_RecordFailure(IRTEMP_FAILURE_PEC, ESP_ERR_INVALID_RESPONSE);
  IRTempStats before, after;
  TEST_ASSERT_EQUAL(ESP_OK, sensor->GetStats(sensor, &before));
  TEST_ASSERT_TRUE(before.last_failure_time_us == 9000000000LL);
  TEST_ASSERT_EQUAL(IRTEMP_FAILURE_PEC, before.last_failure_kind);
  mock_read_data[0]=0x3c; mock_read_data[1]=0x3a; mock_read_data[2]=0xff;
  mocked_esp_timer_get_time_fake.return_val = 9001000000LL;
  sensor->TriggerMeasurement(sensor);
  TEST_ASSERT_EQUAL(ESP_OK, sensor->GetStats(sensor, &after));
  TEST_ASSERT_TRUE(after.last_failure_time_us == before.last_failure_time_us);
  TEST_ASSERT_EQUAL(before.last_failure_kind, after.last_failure_kind);
  TEST_ASSERT_EQUAL(ESP_OK, sensor->ResetStats(sensor));
  TEST_ASSERT_EQUAL(ESP_OK, sensor->GetStats(sensor, &after));
  TEST_ASSERT_EQUAL(IRTEMP_FAILURE_NONE, after.last_failure_kind);
  TEST_ASSERT_TRUE(after.last_failure_time_us == 0);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_IRTemp_MachineSnapshotPreservesAcquisitionAcrossLanguages);
  RUN_TEST(test_IRTemp_ConsoleMissingCallbacksRejectBeforeSdkCalls);
  RUN_TEST(test_IRTemp_ConsoleAgeUsesCompleteCachedPairAndRejectsMissingSample);
  RUN_TEST(test_IRTemp_DetachFailurePreservesOwnershipUntilRetry);
  RUN_TEST(test_IRTemp_InitializedDeviceCanDetachBeforeFirstStart);
  RUN_TEST(test_IRTemp_SampleRetainsAcquisitionTimeAcrossFailures);
  RUN_TEST(test_IRTemp_Lifecycle);
  RUN_TEST(test_IRTemp_CalibrationUsesSnapshotAndFailureRetainsRawMeasurement);
  RUN_TEST(test_IRTemp_ReadTemp);
  RUN_TEST(test_IRTemp_PECValidationFailure);
  RUN_TEST(test_IRTemp_SelfTestAndErrorHandling);
  RUN_TEST(test_IRTemp_StatsAndLimits);
  RUN_TEST(test_IRTemp_ZeroPecMustMatchChecksum);
  RUN_TEST(test_IRTemp_SelfTestRejectsCorruptionAndSensorError);
  RUN_TEST(test_IRTemp_FailedPairDoesNotPublishCachedHeartbeat);
  RUN_TEST(test_IRTemp_InvalidFirstSamplesNeverPublish);
  RUN_TEST(test_IRTemp_PersistentAmbientFailureRemainsUnhealthy);
  RUN_TEST(test_IRTemp_InitializationFailuresAreRetryable);
  RUN_TEST(test_IRTemp_QueueAndDeviceFailurePreserveRetry);
  RUN_TEST(test_IRTemp_StartFailureAndStopTimeoutKeepOwnership);
  RUN_TEST(test_IRTemp_LockFailuresDoNotMutateState);
  RUN_TEST(test_IRTemp_FailedPublishIsRetriedOnNextValidPair);
  RUN_TEST(test_IRTemp_WorkerMeasuresOutsideSharedTimer);
  RUN_TEST(test_IRTemp_ConsolePollingRejectsMalformedValues);
  RUN_TEST(test_IRTemp_RawSnapshotPreservesOutputOnFailure);
  RUN_TEST(test_IRTemp_ConsolePropagatesOperationFailures);
  RUN_TEST(test_IRTemp_ConsoleRegistrationPreservesFailure);
  RUN_TEST(test_IRTempFailureHistorySurvivesSuccessAndClearsWithStats);
  return UNITY_END();
}

#endif // RACOOL_TEST_NATIVE
