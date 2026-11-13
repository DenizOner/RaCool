#include "internal_temp_fixture.h"
#include "Infrastructure/support/console_output_capture.h"
#define printf ConsoleCapture_Printf
#include "../../../src/Hardware/InternalTemperatureSensor/InternalTemperatureSensorConsoleCommands.c"
#undef printf
void setUp(void) { InternalTempFixture_SetUp(); TEST_ASSERT_TRUE(Localization_Apply("en")); }
void tearDown(void) { InternalTempFixture_TearDown(); }

void test_InternalTemp_ConsoleAgeUsesAcquisitionTimeWithoutSamplingAgain(void) {
    IInternalTemperatureSensor *sensor=InternalTemperatureSensor_GetModule();
    TEST_ASSERT_EQUAL(0,sensor->base.Initialize(&sensor->base,NULL));
    TEST_ASSERT_EQUAL(0,sensor->base.Start(&sensor->base));
    mocked_esp_timer_get_time_fake.return_val=1000000;
    sensor->ReadTemperature(sensor);
    unsigned reads=temperature_sensor_get_celsius_fake.call_count;
    mocked_esp_timer_get_time_fake.return_val=1500000;
    ConsoleCapture_Clear();char *args[]={"int_temp","status"};
    TEST_ASSERT_EQUAL(0,cmd_int_temp_handler(2,args));
    TEST_ASSERT_NOT_NULL(strstr(console_output,"Sample Age : 500 ms"));
    TEST_ASSERT_EQUAL(reads,temperature_sensor_get_celsius_fake.call_count);
    InternalTemperatureStatus snapshot;TEST_ASSERT_EQUAL(0,sensor->GetStatus(sensor,&snapshot));
    TEST_ASSERT_TRUE(snapshot.last_sample_time_ms==1000);
    mocked_esp_timer_get_time_fake.return_val=999000;ConsoleCapture_Clear();
    TEST_ASSERT_EQUAL(0,cmd_int_temp_handler(2,args));
    TEST_ASSERT_NOT_NULL(strstr(console_output,"Sample Age : unavailable"));
    TEST_ASSERT_EQUAL(1,cmd_int_temp_handler(3,NULL));
    char *missing[]={"int_temp",NULL,"0"};TEST_ASSERT_EQUAL(1,cmd_int_temp_handler(3,missing));
}

void test_InternalTemp_MachineSnapshotPreservesAgeAcrossLanguages(void) {
    IInternalTemperatureSensor *sensor=InternalTemperatureSensor_GetModule();
    TEST_ASSERT_EQUAL(0,sensor->base.Initialize(&sensor->base,NULL));
    TEST_ASSERT_EQUAL(0,sensor->base.Start(&sensor->base));
    mocked_esp_timer_get_time_fake.return_val=1000000;
    sensor->ReadTemperature(sensor);
    unsigned reads=temperature_sensor_get_celsius_fake.call_count;
    mocked_esp_timer_get_time_fake.return_val=1500000;
    char *args[]={"int_temp","status","--machine"};
    ConsoleCapture_Clear();TEST_ASSERT_EQUAL(0,cmd_int_temp_handler(3,args));
    char english[1024];snprintf(english,sizeof(english),"%s",console_output);
    TEST_ASSERT_NOT_NULL(strstr(english,"type=cpu_sensor_status valid=1"));
    TEST_ASSERT_NOT_NULL(strstr(english,"sample_time_ms=1000 age_ms=500"));
    TEST_ASSERT_TRUE(Localization_Apply("tr"));
    ConsoleCapture_Clear();TEST_ASSERT_EQUAL(0,cmd_int_temp_handler(3,args));
    TEST_ASSERT_EQUAL_STRING(english,console_output);
    TEST_ASSERT_EQUAL(reads,temperature_sensor_get_celsius_fake.call_count);
    TEST_ASSERT_TRUE(Localization_Apply("en"));
}

void test_InternalTemp_ConsoleRejectsMissingCallbacksWithoutDriverCalls(void) {
    IInternalTemperatureSensor *sensor=InternalTemperatureSensor_GetModule();
    IInternalTemperatureSensor original=*sensor;
    sensor->ReadTemperature=NULL;sensor->GetStats=NULL;sensor->ResetStats=NULL;
    sensor->RunSelfTest=NULL;sensor->SetOffset=NULL;
    char *commands[]={"read","stats","reset_stats","selftest","offset"};
    int results[5];unsigned reads=temperature_sensor_get_celsius_fake.call_count;
    for (unsigned i=0;i<5;++i) {
        char *args[]={"int_temp",commands[i],"0","--machine"};
        if (i<4) args[2]="--machine";
        results[i]=cmd_int_temp_handler(i==4 ? 4 : 3,args);
    }
    *sensor=original;
    for (unsigned i=0;i<5;++i) TEST_ASSERT_NOT_EQUAL(0,results[i]);
    TEST_ASSERT_EQUAL(reads,temperature_sensor_get_celsius_fake.call_count);
}

void test_InternalTemp_OffsetBoundsPreserveConfiguration(void) {
    IInternalTemperatureSensor *sensor=InternalTemperatureSensor_GetModule();
    InternalTemperatureSensorConfig config=GetDefaultInternalTemperatureSensorConfig();
    config.temp_offset=10.0f;
    TEST_ASSERT_FALSE(InternalTemperatureSensorConfig_IsValid(&config));
    TEST_ASSERT_NOT_EQUAL(0,sensor->InitializeWithConfig(sensor,config,NULL));
    TEST_ASSERT_EQUAL(0,temperature_sensor_install_fake.call_count);
    TEST_ASSERT_EQUAL(0,sensor->base.Initialize(&sensor->base,NULL));
    TEST_ASSERT_EQUAL(0,sensor->SetOffset(sensor,0.123456789f));
    TEST_ASSERT_NOT_EQUAL(0,sensor->SetOffset(sensor,10.0f));
    TEST_ASSERT_NOT_EQUAL(0,sensor->SetOffset(sensor,-10.0f));
    TEST_ASSERT_NOT_EQUAL(0,sensor->SetOffset(sensor,NAN));
    TEST_ASSERT_EQUAL_FLOAT(0.123456789f,sensor->GetOffset(sensor));
    TEST_ASSERT_EQUAL(0,sensor->SetOffset(sensor,CALIBRATION_MIN));
    TEST_ASSERT_EQUAL_FLOAT(CALIBRATION_MIN,sensor->GetOffset(sensor));
    TEST_ASSERT_EQUAL(0,sensor->SetOffset(sensor,CALIBRATION_MAX));
    TEST_ASSERT_EQUAL_FLOAT(CALIBRATION_MAX,sensor->GetOffset(sensor));
}

static bool fail_thermal_alert;
static unsigned thermal_alert_attempts;
static int32_t publish_committed_level(BusEvent event) {
    TEST_ASSERT_EQUAL(THERMAL_LEVEL_CRITICAL,cpuSensorInstance.current_level);
    if (!strcmp(event.eventName,EVENT_SYSTEM_THERMAL_ALERT)) {
        ++thermal_alert_attempts;return fail_thermal_alert ? -1 : 0;
    }
    return 0;
}
void test_InternalTemp_CommitsLevelBeforePublishAndRetriesFailedAlert(void) {
    IInternalTemperatureSensor *sensor=InternalTemperatureSensor_GetModule();
    TEST_ASSERT_EQUAL(0,sensor->base.Initialize(&sensor->base,NULL));TEST_ASSERT_EQUAL(0,sensor->base.Start(&sensor->base));
    mock_temp_val=85;fail_thermal_alert=true;thermal_alert_attempts=0;
    mock_eventBus.Publish=publish_committed_level;
    PollTimer_Callback(&cpuSensorInstance);
    TEST_ASSERT_TRUE(cpuSensorInstance.thermalAlertPending);TEST_ASSERT_EQUAL(1,thermal_alert_attempts);
    TEST_ASSERT_EQUAL(0,atomic_load(&cpuSensorInstance.activePolls));
    fail_thermal_alert=false;PollTimer_Callback(&cpuSensorInstance);
    TEST_ASSERT_FALSE(cpuSensorInstance.thermalAlertPending);TEST_ASSERT_EQUAL(2,thermal_alert_attempts);
    mock_eventBus.Publish=mock_publish;
}
static int32_t publish_attempts_self_stop(BusEvent event) {
    (void)event;TEST_ASSERT_NOT_EQUAL(0,InternalTemp_Stop(&cpuSensorInstance.base.base));return 0;
}
void test_InternalTemp_TimerPublicationCannotJoinItsOwnCallback(void) {
    IInternalTemperatureSensor *sensor=InternalTemperatureSensor_GetModule();
    TEST_ASSERT_EQUAL(0,sensor->base.Initialize(&sensor->base,NULL));TEST_ASSERT_EQUAL(0,sensor->base.Start(&sensor->base));
    mock_eventBus.Publish=publish_attempts_self_stop;
    PollTimer_Callback(&cpuSensorInstance);
    TEST_ASSERT_EQUAL(SERVICE_STATE_STARTED,cpuSensorInstance.state);TEST_ASSERT_FALSE(atomic_load(&cpuSensorInstance.closing));
    TEST_ASSERT_EQUAL(0,atomic_load(&cpuSensorInstance.activePolls));
    mock_eventBus.Publish=mock_publish;
}
void test_InternalTemp_ClosingRejectsPollingAndCalibrationWithoutSdkCalls(void) {
    IInternalTemperatureSensor *sensor=InternalTemperatureSensor_GetModule();
    TEST_ASSERT_EQUAL(0,sensor->base.Initialize(&sensor->base,NULL));TEST_ASSERT_EQUAL(0,sensor->base.Start(&sensor->base));
    atomic_store(&cpuSensorInstance.closing,true);
    unsigned reads=temperature_sensor_get_celsius_fake.call_count;
    PollTimer_Callback(&cpuSensorInstance);
    TEST_ASSERT_EQUAL(reads,temperature_sensor_get_celsius_fake.call_count);TEST_ASSERT_EQUAL(0,mock_event_published_count);
    TEST_ASSERT_NOT_EQUAL(0,sensor->SetOffset(sensor,1));TEST_ASSERT_NOT_EQUAL(0,sensor->StartPolling(sensor,1000));
    TEST_ASSERT_NOT_EQUAL(0,sensor->SetLimits(sensor,20,100));TEST_ASSERT_NOT_EQUAL(0,sensor->base.CheckHealth(&sensor->base));
}

void test_InternalTemp_StatusSnapshotPreservesOutputOnLockFailure(void) {
    IInternalTemperatureSensor *sensor=InternalTemperatureSensor_GetModule();
    TEST_ASSERT_EQUAL(0,sensor->base.Initialize(&sensor->base,NULL));TEST_ASSERT_EQUAL(0,sensor->base.Start(&sensor->base));
    TEST_ASSERT_EQUAL_FLOAT(25,sensor->ReadTemperature(sensor));TEST_ASSERT_EQUAL(0,sensor->SetOffset(sensor,0.123456789f));
    InternalTemperatureStatus status;TEST_ASSERT_EQUAL(0,sensor->GetStatus(sensor,&status));
    TEST_ASSERT_TRUE(status.sample_valid);TEST_ASSERT_EQUAL_FLOAT(25,status.last_temperature);
    TEST_ASSERT_EQUAL_FLOAT(0.123456789f,status.offset);TEST_ASSERT_EQUAL(SERVICE_STATE_STARTED,status.state);
    InternalTemperatureStatus before=status;mocked_xSemaphoreTake_fake.return_val=pdFALSE;
    TEST_ASSERT_NOT_EQUAL(0,sensor->GetStatus(sensor,&status));TEST_ASSERT_EQUAL_MEMORY(&before,&status,sizeof(status));
    char *args[]={"int_temp","status"};TEST_ASSERT_NOT_EQUAL(0,cmd_int_temp_handler(2,args));
}
void test_InternalTemp_StoppedReadCannotMasqueradeAsFreshMeasurement(void) {
    IInternalTemperatureSensor *sensor=InternalTemperatureSensor_GetModule();
    TEST_ASSERT_EQUAL(0,sensor->base.Initialize(&sensor->base,NULL));TEST_ASSERT_EQUAL(0,sensor->base.Start(&sensor->base));
    TEST_ASSERT_EQUAL_FLOAT(25,sensor->ReadTemperature(sensor));TEST_ASSERT_EQUAL(0,sensor->base.Stop(&sensor->base));
    unsigned reads=temperature_sensor_get_celsius_fake.call_count;
    TEST_ASSERT_FLOAT_WITHIN(0.001f,-273.15f,sensor->ReadTemperature(sensor));
    TEST_ASSERT_EQUAL(reads,temperature_sensor_get_celsius_fake.call_count);TEST_ASSERT_EQUAL_FLOAT(25,sensor->GetLastTemperature(sensor));
    char *args[]={"int_temp","read"};TEST_ASSERT_NOT_EQUAL(0,cmd_int_temp_handler(2,args));
}

void test_InternalTemp_FailedSnapshotPreservesPollingConfiguration(void) {
    IInternalTemperatureSensor *sensor = InternalTemperatureSensor_GetModule();
    TEST_ASSERT_EQUAL(0, sensor->base.Initialize(&sensor->base, NULL));
    InternalTemperatureSensorConfig original = cpuSensorInstance.config;
    sensor_snapshot_result = ESP_FAIL;
    mock_settings.thermal.cpu_polling_interval_ms = 1234;
    OnConfigChanged((BusEvent){0});
    TEST_ASSERT_EQUAL_MEMORY(&original, &cpuSensorInstance.config, sizeof(original));
    sensor_snapshot_result = ESP_OK;
    OnConfigChanged((BusEvent){0});
    TEST_ASSERT_EQUAL(1234, cpuSensorInstance.config.polling_interval_ms);
}

// --- Test Cases ---

void test_InternalTemp_Initialize_Success(void) {
    IInternalTemperatureSensor *sensor = InternalTemperatureSensor_GetModule();
    IService *service = &(sensor->base);

    int32_t res = service->Initialize(service, NULL);

    TEST_ASSERT_EQUAL(0, res);
    TEST_ASSERT_EQUAL(SERVICE_STATE_INITIALIZED, cpuSensorInstance.state);
    TEST_ASSERT_EQUAL(1, temperature_sensor_install_fake.call_count);
}

void test_InternalTemp_StartStop(void) {
    IInternalTemperatureSensor *sensor = InternalTemperatureSensor_GetModule();
    IService *service = &(sensor->base);

    service->Initialize(service, NULL);
    int32_t res = service->Start(service);

    TEST_ASSERT_EQUAL(0, res);
    TEST_ASSERT_EQUAL(SERVICE_STATE_STARTED, cpuSensorInstance.state);
    TEST_ASSERT_EQUAL(1, temperature_sensor_enable_fake.call_count);
    TEST_ASSERT_EQUAL(1, esp_timer_start_periodic_fake.call_count);
    
    res = service->Stop(service);
    TEST_ASSERT_EQUAL(0, res);
    TEST_ASSERT_EQUAL(SERVICE_STATE_STOPPED, cpuSensorInstance.state);
}

void test_InternalTemp_Polling_Logic(void) {
    IInternalTemperatureSensor *sensor = InternalTemperatureSensor_GetModule();
    IService *service = &(sensor->base);
    service->Initialize(service, NULL);
    service->Start(service);

    mock_temp_val = 35.5f;
    PollTimer_Callback(&cpuSensorInstance);

    TEST_ASSERT_EQUAL_FLOAT(35.5f, cpuSensorInstance.last_temp);
    TEST_ASSERT_EQUAL(1, mock_event_published_count);
    TEST_ASSERT_EQUAL_STRING(EVENT_INTERNAL_TEMP_UPDATED, last_event_name);
}

void test_InternalTemp_ThermalAlerts(void) {
    IInternalTemperatureSensor *sensor = InternalTemperatureSensor_GetModule();
    IService *service = &(sensor->base);
    service->Initialize(service, NULL);
    service->Start(service);

    // 1. Warning
    mock_temp_val = 72.0f;
    PollTimer_Callback(&cpuSensorInstance);
    TEST_ASSERT_EQUAL(THERMAL_LEVEL_WARNING, cpuSensorInstance.current_level);
    TEST_ASSERT_EQUAL_STRING(EVENT_SYSTEM_THERMAL_ALERT, last_event_name);

    // 2. Critical
    mock_temp_val = 82.0f;
    PollTimer_Callback(&cpuSensorInstance);
    TEST_ASSERT_EQUAL(THERMAL_LEVEL_CRITICAL, cpuSensorInstance.current_level);

    // 3. Emergency
    mock_temp_val = 92.0f;
    PollTimer_Callback(&cpuSensorInstance);
    TEST_ASSERT_EQUAL(THERMAL_LEVEL_EMERGENCY, cpuSensorInstance.current_level);

    // 4. Hysteresis: remain in emergency until the temperature drops below 88 C.
    mock_temp_val = 91.0f;
    PollTimer_Callback(&cpuSensorInstance);
    TEST_ASSERT_EQUAL(THERMAL_LEVEL_EMERGENCY, cpuSensorInstance.current_level);

    // 5. Normal
    mock_temp_val = 60.0f;
    PollTimer_Callback(&cpuSensorInstance);
    TEST_ASSERT_EQUAL(THERMAL_LEVEL_NORMAL, cpuSensorInstance.current_level);
}

void test_InternalTemp_ConfigChanged(void) {
    IInternalTemperatureSensor *sensor = InternalTemperatureSensor_GetModule();
    IService *service = &(sensor->base);
    service->Initialize(service, NULL);
    service->Start(service);

    // Change the polling interval.
    mock_settings.thermal.cpu_polling_interval_ms = 2000;
    
    BusEvent event = {.eventName = EVENT_CONFIG_CHANGED};
    OnConfigChanged(event);

    TEST_ASSERT_EQUAL(2000, cpuSensorInstance.config.polling_interval_ms);
    // The timer restarts after the configuration change.
    TEST_ASSERT_EQUAL(2, esp_timer_start_periodic_fake.call_count);
}

void test_InternalTemp_Offset_And_Stats(void) {
    IInternalTemperatureSensor *sensor = InternalTemperatureSensor_GetModule();
    IService *service = &(sensor->base);
    service->Initialize(service, NULL);
    service->Start(service);

    // Test Offset
    TEST_ASSERT_EQUAL_FLOAT(0.0f, sensor->GetOffset(sensor));
    sensor->SetOffset(sensor, 2.5f);
    TEST_ASSERT_EQUAL_FLOAT(2.5f, sensor->GetOffset(sensor));

    mock_temp_val = 30.0f; // Raw temp 30.0 + 2.5 offset = 32.5 C
    float current = sensor->ReadTemperature(sensor);
    TEST_ASSERT_EQUAL_FLOAT(32.5f, current);

    // Test Stats
    ThermalStats stats;
    TEST_ASSERT_EQUAL(0, sensor->GetStats(sensor, &stats));
    TEST_ASSERT_EQUAL_FLOAT(32.5f, stats.min_temp);
    TEST_ASSERT_EQUAL_FLOAT(32.5f, stats.max_temp);
    TEST_ASSERT_EQUAL_FLOAT(32.5f, stats.avg_temp);
    TEST_ASSERT_EQUAL(1, stats.total_samples);

    // Second reading
    mock_temp_val = 40.0f; // Raw temp 40.0 + 2.5 offset = 42.5 C
    sensor->ReadTemperature(sensor);

    sensor->GetStats(sensor, &stats);
    TEST_ASSERT_EQUAL_FLOAT(32.5f, stats.min_temp);
    TEST_ASSERT_EQUAL_FLOAT(42.5f, stats.max_temp);
    TEST_ASSERT_EQUAL(2, stats.total_samples);

    // Reset Stats
    sensor->ResetStats(sensor);
    sensor->GetStats(sensor, &stats);
    TEST_ASSERT_EQUAL(0, stats.total_samples);
}

void test_InternalTemp_Trend_SelfTest_Limits(void) {
    IInternalTemperatureSensor *sensor = InternalTemperatureSensor_GetModule();
    IService *service = &(sensor->base);
    service->Initialize(service, NULL);
    service->Start(service);

    // Test Trend initial state
    TEST_ASSERT_EQUAL(THERMAL_TREND_STABLE, sensor->GetThermalTrend(sensor));

    // Test Self-Test
    TEST_ASSERT_EQUAL(0, sensor->RunSelfTest(sensor));

    // Test Set Limits
    TEST_ASSERT_EQUAL(0, sensor->SetLimits(sensor, 20, 100));
    TEST_ASSERT_EQUAL(-1, sensor->SetLimits(sensor, 50, 20)); // Invalid range
}

void test_InternalTemp_UninitializedLifecycleDoesNotTakeNullMutex(void) {
    IService *service = &InternalTemperatureSensor_GetModule()->base;
    TEST_ASSERT_EQUAL(SERVICE_STATE_UNINITIALIZED, service->GetState(service));
    TEST_ASSERT_NOT_EQUAL(0, service->Start(service));
    TEST_ASSERT_NOT_EQUAL(0, service->Stop(service));
    TEST_ASSERT_EQUAL(0, mocked_xSemaphoreTake_fake.call_count);
    TEST_ASSERT_EQUAL(0, temperature_sensor_enable_fake.call_count);
}

void test_InternalTemp_UninitializedAccessorsDoNotUseSdkOrNullMutex(void) {
    IInternalTemperatureSensor *sensor=InternalTemperatureSensor_GetModule();
    ThermalStats stats={.total_samples=123};
    TEST_ASSERT_EQUAL_FLOAT(-273.15f,sensor->GetLastTemperature(sensor));
    TEST_ASSERT_EQUAL(THERMAL_LEVEL_NORMAL,sensor->GetThermalLevel(sensor));
    TEST_ASSERT_EQUAL(THERMAL_TREND_STABLE,sensor->GetThermalTrend(sensor));
    TEST_ASSERT_EQUAL_FLOAT(0.0f,sensor->GetOffset(sensor));
    TEST_ASSERT_NOT_EQUAL(0,sensor->GetStats(sensor,&stats));
    TEST_ASSERT_EQUAL(123,stats.total_samples);
    TEST_ASSERT_NOT_EQUAL(0,sensor->ResetStats(sensor));
    TEST_ASSERT_NOT_EQUAL(0,sensor->StartPolling(sensor,1000));
    TEST_ASSERT_NOT_EQUAL(0,sensor->StopPolling(sensor));
    TEST_ASSERT_NOT_EQUAL(0,sensor->SetLimits(sensor,-10,80));
    TEST_ASSERT_EQUAL(0,mocked_xSemaphoreTake_fake.call_count);
    TEST_ASSERT_EQUAL(0,esp_timer_start_periodic_fake.call_count);
    TEST_ASSERT_EQUAL(0,esp_timer_stop_fake.call_count);
    TEST_ASSERT_EQUAL(0,temperature_sensor_enable_fake.call_count);
    TEST_ASSERT_EQUAL(0,temperature_sensor_install_fake.call_count);
}

static unsigned timer_restart_calls;
static esp_err_t fail_new_period_restore_old(esp_timer_handle_t timer,uint64_t period) {
    (void)timer;
    ++timer_restart_calls;
    if (timer_restart_calls == 1) { TEST_ASSERT_EQUAL(1000000,period); return ESP_FAIL; }
    TEST_ASSERT_EQUAL(5000000,period); return ESP_OK;
}
void test_InternalTemp_PollingFailureRestoresAppliedPeriod(void) {
    IInternalTemperatureSensor *sensor=InternalTemperatureSensor_GetModule();
    TEST_ASSERT_EQUAL(0,sensor->base.Initialize(&sensor->base,NULL));
    TEST_ASSERT_EQUAL(0,sensor->base.Start(&sensor->base));
    timer_restart_calls=0;
    esp_timer_start_periodic_fake.custom_fake=fail_new_period_restore_old;
    TEST_ASSERT_NOT_EQUAL(0,sensor->StartPolling(sensor,1000));
    TEST_ASSERT_EQUAL(2,timer_restart_calls);
    TEST_ASSERT_EQUAL(5000,cpuSensorInstance.config.polling_interval_ms);
    TEST_ASSERT_EQUAL(SERVICE_STATE_STARTED,cpuSensorInstance.state);
    unsigned stops=esp_timer_stop_fake.call_count;
    TEST_ASSERT_NOT_EQUAL(0,sensor->StartPolling(sensor,0));
    TEST_ASSERT_EQUAL(stops,esp_timer_stop_fake.call_count);
    esp_timer_stop_fake.return_val=ESP_FAIL;
    TEST_ASSERT_NOT_EQUAL(0,sensor->StopPolling(sensor));
    TEST_ASSERT_NOT_EQUAL(0,sensor->StartPolling(sensor,1000));
    TEST_ASSERT_EQUAL(2,timer_restart_calls);
}

void test_InternalTemp_StopSdkFailureRetainsOwnershipForRetry(void) {
    IInternalTemperatureSensor *sensor=InternalTemperatureSensor_GetModule();
    TEST_ASSERT_EQUAL(0,sensor->base.Initialize(&sensor->base,NULL));
    TEST_ASSERT_EQUAL(0,sensor->base.Start(&sensor->base));
    temperature_sensor_handle_t handle=cpuSensorInstance.tsens_handle;
    temperature_sensor_disable_fake.return_val=ESP_FAIL;
    TEST_ASSERT_NOT_EQUAL(0,sensor->base.Stop(&sensor->base));
    TEST_ASSERT_EQUAL(SERVICE_STATE_ERROR,cpuSensorInstance.state);
    TEST_ASSERT_EQUAL_PTR(handle,cpuSensorInstance.tsens_handle);
    temperature_sensor_disable_fake.return_val=ESP_OK;
    TEST_ASSERT_EQUAL(0,sensor->base.Stop(&sensor->base));
    TEST_ASSERT_EQUAL(SERVICE_STATE_STOPPED,cpuSensorInstance.state);
}
void test_InternalTemp_LimitFailurePreservesOriginalConfigAndHandle(void) {
    IInternalTemperatureSensor *sensor=InternalTemperatureSensor_GetModule();
    TEST_ASSERT_EQUAL(0,sensor->base.Initialize(&sensor->base,NULL));
    TEST_ASSERT_EQUAL(0,sensor->base.Start(&sensor->base));
    temperature_sensor_handle_t handle=cpuSensorInstance.tsens_handle;
    temperature_sensor_disable_fake.return_val=ESP_FAIL;
    TEST_ASSERT_NOT_EQUAL(0,sensor->SetLimits(sensor,20,100));
    TEST_ASSERT_EQUAL(-10,cpuSensorInstance.config.range_min);
    TEST_ASSERT_EQUAL(80,cpuSensorInstance.config.range_max);
    TEST_ASSERT_EQUAL_PTR(handle,cpuSensorInstance.tsens_handle);
    TEST_ASSERT_EQUAL(0,mocked_temperature_sensor_uninstall_fake.call_count);
    temperature_sensor_disable_fake.return_val=ESP_OK;
    mocked_temperature_sensor_uninstall_fake.return_val=ESP_FAIL;
    TEST_ASSERT_NOT_EQUAL(0,sensor->SetLimits(sensor,20,100));
    TEST_ASSERT_EQUAL_PTR(handle,cpuSensorInstance.tsens_handle);
    TEST_ASSERT_EQUAL(SERVICE_STATE_ERROR,cpuSensorInstance.state);
    TEST_ASSERT_TRUE(cpuSensorInstance.sensorNeedsCleanup);
    TEST_ASSERT_NOT_EQUAL(0,sensor->base.Start(&sensor->base));
    TEST_ASSERT_NOT_EQUAL(0,sensor->base.Stop(&sensor->base));
    mocked_temperature_sensor_uninstall_fake.return_val=ESP_OK;
    TEST_ASSERT_EQUAL(0,sensor->base.Stop(&sensor->base));
    TEST_ASSERT_NULL(cpuSensorInstance.tsens_handle);
    TEST_ASSERT_FALSE(cpuSensorInstance.sensorNeedsCleanup);
    TEST_ASSERT_EQUAL(0,sensor->base.Start(&sensor->base));
    TEST_ASSERT_EQUAL(-10,cpuSensorInstance.config.range_min);
}

void test_InternalTemp_FailedTimerRollbackMarksUnhealthy(void) {
    IInternalTemperatureSensor *sensor=InternalTemperatureSensor_GetModule();
    TEST_ASSERT_EQUAL(0,sensor->base.Initialize(&sensor->base,NULL));
    TEST_ASSERT_EQUAL(0,sensor->base.Start(&sensor->base));
    esp_timer_start_periodic_fake.return_val=ESP_FAIL;
    TEST_ASSERT_NOT_EQUAL(0,sensor->StartPolling(sensor,1000));
    TEST_ASSERT_EQUAL(5000,cpuSensorInstance.config.polling_interval_ms);
    TEST_ASSERT_EQUAL(SERVICE_STATE_ERROR,cpuSensorInstance.state);
    TEST_ASSERT_NOT_EQUAL(0,sensor->base.CheckHealth(&sensor->base));
    TEST_ASSERT_EQUAL(0,sensor->base.Stop(&sensor->base));
}

static unsigned limit_install_calls;
static esp_err_t reject_new_limits_restore_old(const temperature_sensor_config_t *config,temperature_sensor_handle_t *out) {
    ++limit_install_calls;
    if (limit_install_calls == 1) return ESP_FAIL;
    return install_sensor(config,out);
}
void test_InternalTemp_LimitInstallFailureRestoresPreviousSensor(void) {
    IInternalTemperatureSensor *sensor=InternalTemperatureSensor_GetModule();
    TEST_ASSERT_EQUAL(0,sensor->base.Initialize(&sensor->base,NULL));
    TEST_ASSERT_EQUAL(0,sensor->base.Start(&sensor->base));
    limit_install_calls=0;
    temperature_sensor_install_fake.custom_fake=reject_new_limits_restore_old;
    TEST_ASSERT_NOT_EQUAL(0,sensor->SetLimits(sensor,20,100));
    TEST_ASSERT_EQUAL(2,limit_install_calls);
    TEST_ASSERT_NOT_NULL(cpuSensorInstance.tsens_handle);
    TEST_ASSERT_EQUAL(SERVICE_STATE_STARTED,cpuSensorInstance.state);
    TEST_ASSERT_EQUAL(-10,cpuSensorInstance.config.range_min);
    TEST_ASSERT_EQUAL(80,cpuSensorInstance.config.range_max);
    TEST_ASSERT_EQUAL_FLOAT(25.0f,sensor->ReadTemperature(sensor));
}
void test_InternalTemp_LimitRollbackFailureRejectsReads(void) {
    IInternalTemperatureSensor *sensor=InternalTemperatureSensor_GetModule();
    TEST_ASSERT_EQUAL(0,sensor->base.Initialize(&sensor->base,NULL));
    TEST_ASSERT_EQUAL(0,sensor->base.Start(&sensor->base));
    temperature_sensor_install_fake.custom_fake=NULL;
    temperature_sensor_install_fake.return_val=ESP_FAIL;
    TEST_ASSERT_NOT_EQUAL(0,sensor->SetLimits(sensor,20,100));
    TEST_ASSERT_EQUAL(SERVICE_STATE_ERROR,cpuSensorInstance.state);
    TEST_ASSERT_NULL(cpuSensorInstance.tsens_handle);
    unsigned reads=temperature_sensor_get_celsius_fake.call_count;
    sensor->ReadTemperature(sensor);
    TEST_ASSERT_EQUAL(reads,temperature_sensor_get_celsius_fake.call_count);
    TEST_ASSERT_EQUAL(-10,cpuSensorInstance.config.range_min);
}

void test_InternalTemp_InvalidConfigDoesNotAllocateOrInstall(void) {
    IInternalTemperatureSensor *sensor=InternalTemperatureSensor_GetModule();
    InternalTemperatureSensorConfig config=GetDefaultInternalTemperatureSensorConfig();
    config.polling_interval_ms=0;
    TEST_ASSERT_NOT_EQUAL(0,sensor->InitializeWithConfig(sensor,config,NULL));
    config=GetDefaultInternalTemperatureSensorConfig();config.ema_alpha=NAN;
    TEST_ASSERT_NOT_EQUAL(0,sensor->InitializeWithConfig(sensor,config,NULL));
    config=GetDefaultInternalTemperatureSensorConfig();config.temp_offset=INFINITY;
    TEST_ASSERT_NOT_EQUAL(0,sensor->InitializeWithConfig(sensor,config,NULL));
    TEST_ASSERT_EQUAL(0,mocked_xSemaphoreCreateMutex_fake.call_count);
    TEST_ASSERT_EQUAL(0,temperature_sensor_install_fake.call_count);
}
void test_InternalTemp_SubscriptionFailureRollsBackResourcesAndRetries(void) {
    IInternalTemperatureSensor *sensor=InternalTemperatureSensor_GetModule();
    sensor_subscribe_result=-1;
    TEST_ASSERT_NOT_EQUAL(0,sensor->base.Initialize(&sensor->base,NULL));
    TEST_ASSERT_NULL(cpuSensorInstance.timer_handle);
    TEST_ASSERT_NULL(cpuSensorInstance.tsens_handle);
    TEST_ASSERT_FALSE(cpuSensorInstance.initializedSuccessfully);
    TEST_ASSERT_EQUAL(1,mocked_esp_timer_delete_fake.call_count);
    sensor_subscribe_result=0;
    TEST_ASSERT_EQUAL(0,sensor->base.Initialize(&sensor->base,NULL));
    TEST_ASSERT_TRUE(cpuSensorInstance.initializedSuccessfully);
}
void test_InternalTemp_InitCleanupFailurePreservesSensorUntilRetry(void) {
    IInternalTemperatureSensor *sensor=InternalTemperatureSensor_GetModule();
    esp_timer_create_fake.custom_fake=NULL;esp_timer_create_fake.return_val=ESP_FAIL;
    mocked_temperature_sensor_uninstall_fake.return_val=ESP_FAIL;
    TEST_ASSERT_NOT_EQUAL(0,sensor->base.Initialize(&sensor->base,NULL));
    TEST_ASSERT_NOT_NULL(cpuSensorInstance.tsens_handle);
    TEST_ASSERT_EQUAL(SERVICE_STATE_ERROR,cpuSensorInstance.state);
    TEST_ASSERT_NOT_EQUAL(0,sensor->base.Initialize(&sensor->base,NULL));
    TEST_ASSERT_EQUAL(1,temperature_sensor_install_fake.call_count);
    mocked_temperature_sensor_uninstall_fake.return_val=ESP_OK;
    esp_timer_create_fake.custom_fake=create_timer;
    TEST_ASSERT_EQUAL(0,sensor->base.Initialize(&sensor->base,NULL));
    TEST_ASSERT_EQUAL(2,temperature_sensor_install_fake.call_count);
}
void test_InternalTemp_InitCleanupFailurePreservesTimerUntilRetry(void) {
    IInternalTemperatureSensor *sensor=InternalTemperatureSensor_GetModule();
    sensor_subscribe_result=-1;mocked_esp_timer_delete_fake.return_val=ESP_FAIL;
    TEST_ASSERT_NOT_EQUAL(0,sensor->base.Initialize(&sensor->base,NULL));
    TEST_ASSERT_NOT_NULL(cpuSensorInstance.timer_handle);
    TEST_ASSERT_EQUAL(SERVICE_STATE_ERROR,cpuSensorInstance.state);
    mocked_esp_timer_delete_fake.return_val=ESP_OK;sensor_subscribe_result=0;
    TEST_ASSERT_EQUAL(0,sensor->base.Initialize(&sensor->base,NULL));
    TEST_ASSERT_EQUAL(2,mocked_esp_timer_delete_fake.call_count);
}
void test_InternalTemp_FailedPollingChangePreservesPreviouslyStoppedTimer(void) {
    IInternalTemperatureSensor *sensor=InternalTemperatureSensor_GetModule();
    TEST_ASSERT_EQUAL(0,sensor->base.Initialize(&sensor->base,NULL));
    TEST_ASSERT_EQUAL(0,sensor->base.Start(&sensor->base));
    esp_timer_stop_fake.return_val=ESP_ERR_INVALID_STATE;
    esp_timer_start_periodic_fake.return_val=ESP_FAIL;
    unsigned starts=esp_timer_start_periodic_fake.call_count;
    TEST_ASSERT_NOT_EQUAL(0,sensor->StartPolling(sensor,1000));
    TEST_ASSERT_EQUAL(starts+1,esp_timer_start_periodic_fake.call_count);
    TEST_ASSERT_EQUAL(SERVICE_STATE_STARTED,cpuSensorInstance.state);
    TEST_ASSERT_EQUAL(5000,cpuSensorInstance.config.polling_interval_ms);
}

void test_InternalTemp_CliRejectsMalformedOffsetAndReturnsFailures(void) {
    char *bad[]={"int_temp","offset","nan"};
    TEST_ASSERT_NOT_EQUAL(0,cmd_int_temp_handler(3,bad));
    bad[2]="1.0junk";TEST_ASSERT_NOT_EQUAL(0,cmd_int_temp_handler(3,bad));
    bad[2]="1e999";TEST_ASSERT_NOT_EQUAL(0,cmd_int_temp_handler(3,bad));
    char *stats[]={"int_temp","stats"};
    TEST_ASSERT_NOT_EQUAL(0,cmd_int_temp_handler(2,stats));
    char *test[]={"int_temp","selftest"};
    TEST_ASSERT_NOT_EQUAL(0,cmd_int_temp_handler(2,test));
    TEST_ASSERT_EQUAL(0,mocked_xSemaphoreTake_fake.call_count);
    IInternalTemperatureSensor *sensor=InternalTemperatureSensor_GetModule();
    TEST_ASSERT_EQUAL(0,sensor->base.Initialize(&sensor->base,NULL));
    bad[2]="0.1";TEST_ASSERT_EQUAL(0,cmd_int_temp_handler(3,bad));
    TEST_ASSERT_FLOAT_WITHIN(0.0001f,0.1f,sensor->GetOffset(sensor));
}

void test_InternalTemp_BaseInitAppliesPersistedThermalSnapshot(void) {
    IInternalTemperatureSensor *sensor=InternalTemperatureSensor_GetModule();
    mock_settings.thermal.cpu_polling_interval_ms=1234;
    mock_settings.thermal.cpu_temp_warning=50;
    mock_settings.thermal.cpu_temp_critical=60;
    mock_settings.thermal.cpu_temp_emergency=70;
    TEST_ASSERT_EQUAL(0,sensor->base.Initialize(&sensor->base,NULL));
    TEST_ASSERT_EQUAL(1234,cpuSensorInstance.config.polling_interval_ms);
    TEST_ASSERT_EQUAL_FLOAT(50,cpuSensorInstance.config.warning_threshold);
    TEST_ASSERT_EQUAL(0,sensor->base.Start(&sensor->base));
    TEST_ASSERT_TRUE(esp_timer_start_periodic_fake.arg1_val==1234000ULL);
}
void test_InternalTemp_BaseInitSnapshotFailureDoesNotInstall(void) {
    IInternalTemperatureSensor *sensor=InternalTemperatureSensor_GetModule();
    sensor_snapshot_result=ESP_FAIL;
    TEST_ASSERT_NOT_EQUAL(0,sensor->base.Initialize(&sensor->base,NULL));
    TEST_ASSERT_EQUAL(0,temperature_sensor_install_fake.call_count);
    TEST_ASSERT_EQUAL(0,mocked_xSemaphoreCreateMutex_fake.call_count);
}

void test_InternalTemp_UnsupportedSdkRangeRejectsBeforeMutation(void) {
    IInternalTemperatureSensor *sensor=InternalTemperatureSensor_GetModule();
    TEST_ASSERT_EQUAL(0,sensor->base.Initialize(&sensor->base,NULL));
    TEST_ASSERT_EQUAL(0,sensor->base.Start(&sensor->base));
    unsigned disables=temperature_sensor_disable_fake.call_count;
    TEST_ASSERT_NOT_EQUAL(0,sensor->SetLimits(sensor,-20,100));
    TEST_ASSERT_NOT_EQUAL(0,sensor->SetLimits(sensor,-100,200));
    TEST_ASSERT_EQUAL(disables,temperature_sensor_disable_fake.call_count);
    TEST_ASSERT_EQUAL(0,mocked_temperature_sensor_uninstall_fake.call_count);
    TEST_ASSERT_EQUAL(SERVICE_STATE_STARTED,sensor->base.GetState(&sensor->base));
}

void test_InternalTemp_InitializeLockFailureDoesNotInstall(void) {
    IService *service = &InternalTemperatureSensor_GetModule()->base;
    mocked_xSemaphoreTake_fake.return_val = pdFALSE;
    TEST_ASSERT_NOT_EQUAL(0, service->Initialize(service, NULL));
    TEST_ASSERT_EQUAL(0, temperature_sensor_install_fake.call_count);
}

void test_InternalTemp_InvalidSampleDoesNotPublishCachedTemperature(void) {
    IInternalTemperatureSensor *sensor = InternalTemperatureSensor_GetModule();
    TEST_ASSERT_EQUAL(0, sensor->base.Initialize(&sensor->base, NULL));
    TEST_ASSERT_EQUAL(0, sensor->base.Start(&sensor->base));
    PollTimer_Callback(&cpuSensorInstance);
    TEST_ASSERT_EQUAL(1, mock_event_published_count);
    mock_temp_val = NAN;
    PollTimer_Callback(&cpuSensorInstance);
    TEST_ASSERT_EQUAL(1, mock_event_published_count);
    TEST_ASSERT_EQUAL_FLOAT(25.0f, sensor->GetLastTemperature(sensor));
    TEST_ASSERT_EQUAL(1, cpuSensorInstance.stats.total_samples);
    TEST_ASSERT_NOT_EQUAL(0, sensor->SetOffset(sensor, INFINITY));
    temperature_sensor_get_celsius_fake.custom_fake = NULL;
    temperature_sensor_get_celsius_fake.return_val = ESP_FAIL;
    PollTimer_Callback(&cpuSensorInstance);
    TEST_ASSERT_EQUAL(1, mock_event_published_count);
    TEST_ASSERT_NOT_EQUAL(0, sensor->RunSelfTest(sensor));
}

void test_InternalTemp_DesiredPollingRetriesAfterSdkFailure(void) {
    IInternalTemperatureSensor *sensor=InternalTemperatureSensor_GetModule();
    TEST_ASSERT_EQUAL(0,sensor->base.Initialize(&sensor->base,NULL));TEST_ASSERT_EQUAL(0,sensor->base.Start(&sensor->base));
    timer_restart_calls=0;esp_timer_start_periodic_fake.custom_fake=fail_new_period_restore_old;
    mock_settings.thermal.cpu_polling_interval_ms=1000;OnConfigChanged((BusEvent){0});
    TEST_ASSERT_EQUAL(5000,cpuSensorInstance.config.polling_interval_ms);
    TEST_ASSERT_EQUAL(1000,atomic_load(&cpuSensorInstance.pendingPollingMs));
    esp_timer_start_periodic_fake.custom_fake=NULL;
    PollTimer_Callback(&cpuSensorInstance);
    TEST_ASSERT_EQUAL(1000,cpuSensorInstance.config.polling_interval_ms);
    TEST_ASSERT_EQUAL(0,atomic_load(&cpuSensorInstance.pendingPollingMs));
}
void test_InternalTemp_LatestPollingSurvivesLockFailureAndRestart(void) {
    IInternalTemperatureSensor *sensor=InternalTemperatureSensor_GetModule();
    TEST_ASSERT_EQUAL(0,sensor->base.Initialize(&sensor->base,NULL));TEST_ASSERT_EQUAL(0,sensor->base.Start(&sensor->base));
    mocked_xSemaphoreTake_fake.return_val=pdFALSE;
    mock_settings.thermal.cpu_polling_interval_ms=1000;OnConfigChanged((BusEvent){0});
    mock_settings.thermal.cpu_polling_interval_ms=2000;OnConfigChanged((BusEvent){0});
    TEST_ASSERT_EQUAL(5000,cpuSensorInstance.config.polling_interval_ms);
    TEST_ASSERT_EQUAL(2000,atomic_load(&cpuSensorInstance.pendingPollingMs));
    mocked_xSemaphoreTake_fake.return_val=pdTRUE;
    TEST_ASSERT_EQUAL(0,sensor->base.Stop(&sensor->base));TEST_ASSERT_EQUAL(0,sensor->base.Start(&sensor->base));
    TEST_ASSERT_EQUAL(2000,cpuSensorInstance.config.polling_interval_ms);
    TEST_ASSERT_EQUAL(0,atomic_load(&cpuSensorInstance.pendingPollingMs));
}

void test_InternalTemperatureSensor_All(void) {
    RUN_TEST(test_InternalTemp_MachineSnapshotPreservesAgeAcrossLanguages);
    RUN_TEST(test_InternalTemp_ConsoleRejectsMissingCallbacksWithoutDriverCalls);
    RUN_TEST(test_InternalTemp_ConsoleAgeUsesAcquisitionTimeWithoutSamplingAgain);
    RUN_TEST(test_InternalTemp_InvalidSampleDoesNotPublishCachedTemperature);
    RUN_TEST(test_InternalTemp_UninitializedLifecycleDoesNotTakeNullMutex);
    RUN_TEST(test_InternalTemp_UninitializedAccessorsDoNotUseSdkOrNullMutex);
    RUN_TEST(test_InternalTemp_PollingFailureRestoresAppliedPeriod);
    RUN_TEST(test_InternalTemp_StopSdkFailureRetainsOwnershipForRetry);
    RUN_TEST(test_InternalTemp_FailedTimerRollbackMarksUnhealthy);
    RUN_TEST(test_InternalTemp_LimitFailurePreservesOriginalConfigAndHandle);
    RUN_TEST(test_InternalTemp_LimitInstallFailureRestoresPreviousSensor);
    RUN_TEST(test_InternalTemp_LimitRollbackFailureRejectsReads);
    RUN_TEST(test_InternalTemp_InvalidConfigDoesNotAllocateOrInstall);
    RUN_TEST(test_InternalTemp_StatusSnapshotPreservesOutputOnLockFailure);
    RUN_TEST(test_InternalTemp_StoppedReadCannotMasqueradeAsFreshMeasurement);
    RUN_TEST(test_InternalTemp_CommitsLevelBeforePublishAndRetriesFailedAlert);
    RUN_TEST(test_InternalTemp_TimerPublicationCannotJoinItsOwnCallback);
    RUN_TEST(test_InternalTemp_ClosingRejectsPollingAndCalibrationWithoutSdkCalls);
    RUN_TEST(test_InternalTemp_OffsetBoundsPreserveConfiguration);
    RUN_TEST(test_InternalTemp_BaseInitAppliesPersistedThermalSnapshot);
    RUN_TEST(test_InternalTemp_BaseInitSnapshotFailureDoesNotInstall);
    RUN_TEST(test_InternalTemp_UnsupportedSdkRangeRejectsBeforeMutation);
    RUN_TEST(test_InternalTemp_CliRejectsMalformedOffsetAndReturnsFailures);
    RUN_TEST(test_InternalTemp_SubscriptionFailureRollsBackResourcesAndRetries);
    RUN_TEST(test_InternalTemp_InitCleanupFailurePreservesSensorUntilRetry);
    RUN_TEST(test_InternalTemp_InitCleanupFailurePreservesTimerUntilRetry);
    RUN_TEST(test_InternalTemp_FailedPollingChangePreservesPreviouslyStoppedTimer);
    RUN_TEST(test_InternalTemp_InitializeLockFailureDoesNotInstall);
    RUN_TEST(test_InternalTemp_DesiredPollingRetriesAfterSdkFailure);
    RUN_TEST(test_InternalTemp_LatestPollingSurvivesLockFailureAndRestart);
    RUN_TEST(test_InternalTemp_Initialize_Success);
    RUN_TEST(test_InternalTemp_StartStop);
    RUN_TEST(test_InternalTemp_Polling_Logic);
    RUN_TEST(test_InternalTemp_ThermalAlerts);
    RUN_TEST(test_InternalTemp_ConfigChanged);
    RUN_TEST(test_InternalTemp_FailedSnapshotPreservesPollingConfiguration);
    RUN_TEST(test_InternalTemp_Offset_And_Stats);
    RUN_TEST(test_InternalTemp_Trend_SelfTest_Limits);
}

#ifndef RACOOL_TEST_NATIVE
void app_main(void) {
    UNITY_BEGIN();
    test_InternalTemperatureSensor_All();
    UNITY_END();
}
#else
int main(void) {
    UNITY_BEGIN();
    test_InternalTemperatureSensor_All();
    return UNITY_END();
}
#endif
