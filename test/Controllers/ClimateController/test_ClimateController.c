/**
 * @file test_ClimateController.c
 * @brief Unit tests for the ClimateController module.
 *
 * Tests cover:
 *   - Service lifecycle (Initialize / Start / Stop / GetState / CheckHealth)
 *   - InitializeWithConfig with custom parameters
 *   - Thread-safe temperature getter sentinel values
 *   - Relay state getter with mock relay
 *   - LastSensorReadTime initial value
 *   - RunSelfTest with various dependency configurations
 *   - Relay logic: system disabled forces relay off
 *   - Relay logic: manual ON/OFF mode
 *   - Relay logic: auto mode open/close transitions
 *   - Relay logic: debounce protection
 *   - Relay logic: minimum switch-time enforcement
 *   - Relay logic: hysteresis dead-band (shouldOpen/Close)
 *   - CPU thermal protection: emergency override via EVENT_SYSTEM_THERMAL_ALERT
 *   - CPU thermal protection: override cleared when CPU cools
 *   - Stop(): EventBus unsubscribe called; relay/watchdog cleanup
 *   - Sensor stale detection: IsSensorStale() and event publication
 *   - GetFanMode(): configured sensing strategy or manual selection, independent of CPU override
 *   - GetConfig(): returns active config snapshot
 *   - GetRelayDebounceState(): reflects RSM pending flag
 *   - Temperature history ring buffer: entries appended and read correctly
 *   - MenuHandlerEx: set low/high temp via encoder
 *   - MenuHandlerEx: fan mode cycling
 *   - MenuHandlerEx: calibration offset adjustments
 *   - EventBus callbacks: IR temp update atomicity
 *   - EventBus callbacks: CPU temp update atomicity
 *   - Debug console: ClimateCommands_Register (smoke test)
 *
 * Architecture Note:
 *   Source files are included directly for white-box behavior checks without link-time
 *   dependencies on the full ESP-IDF SDK. The test_mocks.h header provides
 *   all required ESP-IDF and FreeRTOS stub implementations.
 */

#ifndef RACOOL_TEST_NATIVE
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#endif

#include "test_mocks.h"
#include "unity.h"

/* ── Direct source inclusion for white-box behavior checks ──────────────────────────── */
/* NOTE: ClimateConsoleCommands.c is NOT included here because it pulls in
 * IDebugConsole which requires additional mock infrastructure. Console command
 * registration is smoke-tested separately via a lightweight mock console. */
#include "Controllers/ClimateController/ClimateControllerConfig.c"

/* Prevent ClimateController.c from pulling in ClimateConsoleCommands.c via the
 * header — we provide a stub implementation below. */
#define CLIMATE_CONSOLE_COMMANDS_H  /* guard the real header */

/* Forward-declare IDebugConsole to match the real function signature without
 * pulling in the full IDebugConsole.h dependency chain. */
struct IDebugConsole;
static esp_err_t climate_registration_result;
static esp_err_t ClimateCommands_Register(struct IDebugConsole *console) {
    (void)console; return climate_registration_result;
}

#include "Management/Watchdog/WatchdogWorker.c"
#include "Controllers/ClimateController/ClimateController.c"

/* ── Internal struct access (white-box testing) ─────────────────────────── */
/* climateControllerInstance is defined as static in ClimateController.c;
 * we rely on the direct source include to access it. */

/* ═══════════════════════════════════════════════════════════════════════════
 * Mock Objects
 * ═══════════════════════════════════════════════════════════════════════════ */

/* ── Mock IRelay ─────────────────────────────────────────────────────────── */
static bool mock_relay_is_open_val = false;
static int  mock_relay_open_calls  = 0;
static int  mock_relay_close_calls = 0;

static esp_err_t relay_open_result;
static esp_err_t mock_relay_open(IRelay *self) { (void)self; ++mock_relay_open_calls; if (relay_open_result==ESP_OK) mock_relay_is_open_val=true; return relay_open_result; }
static esp_err_t mock_relay_close(IRelay *self) { (void)self; mock_relay_is_open_val = false; mock_relay_close_calls++; return ESP_OK; }
static bool      mock_relay_is_open(IRelay *self) { (void)self; return mock_relay_is_open_val; }

static IRelay mock_relay = {
    .base    = {0},  /* IService base — lifecycle not used in tests */
    .Open    = mock_relay_open,
    .Close   = mock_relay_close,
    .IsOpen  = mock_relay_is_open,
};

/* ── Mock IInfraredTemperatureSensor ─────────────────────────────────────── */
static float mock_ir_obj_temp = 25.0f;
static float mock_ir_amb_temp = 22.0f;

static float mock_ir_read_obj(IInfraredTemperatureSensor *self) { (void)self; return mock_ir_obj_temp; }
static float mock_ir_read_amb(IInfraredTemperatureSensor *self) { (void)self; return mock_ir_amb_temp; }
static void  mock_ir_trigger(IInfraredTemperatureSensor *self)  { (void)self; }

static IInfraredTemperatureSensor mock_ir_sensor = {
    .base                   = {0},  /* IService base */
    .ReadObjectTemperature  = mock_ir_read_obj,
    .ReadAmbientTemperature = mock_ir_read_amb,
    .TriggerMeasurement     = mock_ir_trigger,
};

/* ── Mock IInternalTemperatureSensor ─────────────────────────────────────── */
static float mock_cpu_temp = 45.0f;
static float mock_cpu_read(IInternalTemperatureSensor *self) { (void)self; return mock_cpu_temp; }

static IInternalTemperatureSensor mock_cpu_sensor = {
    .base            = {0},  /* IService base */
    .ReadTemperature = mock_cpu_read,
};

/* ── Mock IConfigurationManager ──────────────────────────────────────────── */
static SystemSettings mock_settings;

static SystemSettings *mock_cfg_get_settings(IConfigurationManager *self) {
    (void)self;
    return &mock_settings;
}
static esp_err_t mock_cfg_save(IConfigurationManager *self, const char *block) {
    (void)self; (void)block;
    return ESP_OK;
}

static esp_err_t menu_save_result;
static esp_err_t snapshot_result;
static bool snapshot_advances_clock;
static unsigned menu_save_calls;
static esp_err_t mock_cfg_snapshot(IConfigurationManager *self, SystemSettings *out) {
    (void)self;
    if (snapshot_result != ESP_OK) return snapshot_result;
    if (snapshot_advances_clock) mocked_xTaskGetTickCount_fake.return_val = 200;
    *out = mock_settings; return ESP_OK;
}
static esp_err_t mock_cfg_fields(IConfigurationManager *self, const char *block,
    const ConfigurationFieldUpdate *fields, size_t count, bool persist) {
    (void)self; menu_save_calls++;
    TEST_ASSERT_TRUE(persist);
    if (menu_save_result != ESP_OK) return menu_save_result;
    if (!strcmp(block,"control")) {
        ControlConfigs candidate=mock_settings.control;
        for (size_t i=0;i<count;++i) memcpy((char *)&candidate+fields[i].offset,fields[i].data,fields[i].size);
        if (!SystemConfigs_IsFanSourceConfigValid(&candidate) || !SystemConfigs_IsFanSetpointPairValid(candidate.relay_open_temp,candidate.relay_close_temp)) return ESP_ERR_INVALID_ARG;
    }
    void *target = strcmp(block, "control") == 0 ? (void *)&mock_settings.control :
        strcmp(block, "calibration") == 0 ? (void *)&mock_settings.calibration : (void *)&mock_settings.system;
    for (size_t i = 0; i < count; ++i) memcpy((uint8_t *)target + fields[i].offset, fields[i].data, fields[i].size);
    return ESP_OK;
}

static IConfigurationManager mock_config_mgr = {
    .base        = {0},  /* IService base */
    .GetSettings = mock_cfg_get_settings,
    .GetSnapshot = mock_cfg_snapshot,
    .UpdateFields = mock_cfg_fields,
    .Save        = mock_cfg_save,
};

/* ── Mock IEventBus ──────────────────────────────────────────────────────── */
static int subscription_failure_at;
static int unsubscribe_result;
static int mock_eventbus_publish_calls = 0;
static int mock_eventbus_subscribe_calls = 0;
static int mock_eventbus_unsubscribe_calls = 0;
static const char *mock_last_event_name = NULL;

static int32_t mock_eb_subscribe(IEventBus *self, const char *name, BusEventHandler h) {
    (void)self; (void)name; (void)h;
    mock_eventbus_subscribe_calls++;
    return mock_eventbus_subscribe_calls == subscription_failure_at ? -1 : 0;
}
static int32_t mock_eb_unsubscribe(IEventBus *self, const char *name, BusEventHandler h) {
    (void)self; (void)name; (void)h;
    mock_eventbus_unsubscribe_calls++;
    return unsubscribe_result;
}
static int32_t mock_eb_publish(BusEvent event) {
    mock_eventbus_publish_calls++;
    mock_last_event_name = event.eventName;
    return 0;
}

static IEventBus mock_event_bus = {
    /* base (IService) — all vtable slots NULL; tests never lifecycle the EventBus. */
    .base            = {0},
    .Subscribe       = mock_eb_subscribe,
    .Unsubscribe     = mock_eb_unsubscribe,
    .Publish         = mock_eb_publish,
    .PublishFromISR  = NULL,
    .InitializeWithConfig = NULL,
};


/* ── ServiceLocator stub redirections ────────────────────────────────────── */
/* mocked_ServiceLocator_GetService and mocked_ServiceLocator_GetEventBus are
 * provided by test_mocks.h / FFF. We configure their return values in setUp(). */

/* ═══════════════════════════════════════════════════════════════════════════
 * Helpers
 * ═══════════════════════════════════════════════════════════════════════════ */

/**
 * @brief Completely resets the ClimateController singleton for each test.
 *
 * Because climateControllerInstance is a file-scope static, we must reset it
 * manually between tests to prevent state leakage.
 */
static void reset_climate_instance(void) {
    memset(&climateControllerInstance, 0, sizeof(climateControllerInstance));
    climateControllerInstance.state   = SERVICE_STATE_UNINITIALIZED;
    climateControllerInstance.spinlock = (portMUX_TYPE)portMUX_INITIALIZER_UNLOCKED;
    /* Ensure the vtable is re-initialised on the next GetModule() call. */
    /* We reset vtable_initialized by including ClimateController.c directly,
     * so we rely on the fact that the static bool inside GetModule() was reset
     * via the memset above (it is part of the TU's static storage). */
}

/**
 * @brief Injects mock dependencies directly into the instance struct.
 */
static void inject_mocks(void) {
    climateControllerInstance.relay        = &mock_relay;
    climateControllerInstance.tempSensor   = &mock_ir_sensor;
    climateControllerInstance.internalTemp = &mock_cpu_sensor;
    climateControllerInstance.configManager = &mock_config_mgr;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * setUp / tearDown
 * ═══════════════════════════════════════════════════════════════════════════ */

static int32_t climate_stop_ack;
static BaseType_t create_climate_task(TaskFunction_t fn, const char *name, uint32_t stack,
                                     void *arg, uint32_t priority, TaskHandle_t *task) {
    (void)fn; (void)name; (void)stack; (void)arg; (void)priority;
    *task = (TaskHandle_t)3; return pdPASS;
}
static BaseType_t climate_ack_send(QueueHandle_t queue, const void *value, TickType_t wait) {
    (void)queue; (void)wait; climate_stop_ack = *(const int32_t *)value; return pdTRUE;
}
static BaseType_t climate_ack_receive(QueueHandle_t queue, void *value, TickType_t wait) {
    (void)queue; (void)wait;
    mocked_xTaskGetCurrentTaskHandle_fake.return_val = (TaskHandle_t)3;
    WatchdogWorker_Checkpoint(&climateControllerInstance.worker, NULL, 0);
    mocked_xTaskGetCurrentTaskHandle_fake.return_val = (TaskHandle_t)4;
    *(int32_t *)value = climate_stop_ack;
    return pdTRUE;
}

void setUp(void) {
    climate_registration_result = ESP_OK;
    snapshot_advances_clock = false;
    RESET_FAKE(mocked_xTaskGetTickCount);
    subscription_failure_at = 0; unsubscribe_result = 0;
    RESET_FAKE(mocked_ServiceLocator_GetService);
    menu_save_result = ESP_OK; menu_save_calls = 0; snapshot_result = ESP_OK;
    RESET_FAKE(mocked_xQueueCreate);
    RESET_FAKE(mocked_xQueueSend);
    RESET_FAKE(mocked_xQueueReceive);
    RESET_FAKE(mocked_xTaskGetCurrentTaskHandle);
    RESET_FAKE(mocked_xTaskCreate);
    mocked_xQueueCreate_fake.return_val = (QueueHandle_t)2;
    mocked_xTaskCreate_fake.custom_fake = create_climate_task;
    mocked_xQueueSend_fake.custom_fake = climate_ack_send;
    mocked_xQueueReceive_fake.custom_fake = climate_ack_receive;
    mocked_xTaskGetCurrentTaskHandle_fake.return_val = (TaskHandle_t)4;
    /* Reset counters. */
    mock_relay_is_open_val       = false;
    mock_relay_open_calls        = 0;
    relay_open_result=ESP_OK;
    mock_relay_close_calls       = 0;
    mock_eventbus_publish_calls   = 0;
    mock_eventbus_subscribe_calls = 0;
    mock_eventbus_unsubscribe_calls = 0;
    mock_last_event_name         = NULL;

    /* Reset settings to known defaults. */
    memset(&mock_settings, 0, sizeof(mock_settings));
    mock_settings.system.system_enabled         = true;
    mock_settings.control.fan_delta_on_decic=20;mock_settings.control.fan_delta_off_decic=10;
    mock_settings.control.relay_open_temp        = 35.0f;
    mock_settings.control.relay_close_temp       = 28.0f;
    mock_settings.control.fan_manual_mode        = false;
    mock_settings.control.fan_manual_state       = false;
    mock_settings.calibration.ir_object_cal      = 0.0f;
    mock_settings.calibration.ir_ambient_cal     = 0.0f;
    mock_settings.thermal.cpu_temp_warning       = 80.0f;
    mock_settings.thermal.cpu_temp_critical      = 100.0f;
    mock_settings.thermal.cpu_temp_emergency     = 115.0f;

    /* Reset temperatures. */
    mock_ir_obj_temp = 25.0f;
    mock_ir_amb_temp = 22.0f;
    mock_cpu_temp    = 45.0f;

    /* Reset singleton. */
    reset_climate_instance();

    /* Wire up ServiceLocator stubs. */
    mocked_ServiceLocator_GetEventBus_fake.return_val = &mock_event_bus;
    mocked_ConfigurationManager_GetInstance_fake.return_val = &mock_config_mgr;
    mocked_ServiceLocator_GetService_fake.return_val = NULL; /* default: not found */

    /* FreeRTOS defaults — task creation must succeed (pdPASS = 1). */
    mocked_xTaskCreate_fake.return_val         = 1; /* pdPASS */
    mocked_xSemaphoreCreateMutex_fake.return_val = (SemaphoreHandle_t)1;
    mocked_xSemaphoreTake_fake.return_val      = 1; /* pdPASS */
    mocked_xSemaphoreGive_fake.return_val      = 1; /* pdPASS */

    /* Re-initialise vtable. */
    ClimateController_GetModule();
}


void tearDown(void) {}

/* ═══════════════════════════════════════════════════════════════════════════
 * Test Cases
 * ═══════════════════════════════════════════════════════════════════════════ */

/* ── Lifecycle ───────────────────────────────────────────────────────────── */

/**
 * @brief Verifies the full Initialize → Start → Stop lifecycle.
 */
void test_ClimateController_Lifecycle(void) {
    IClimateController *cc = ClimateController_GetModule();
    IService           *svc = &cc->base;

    TEST_ASSERT_EQUAL(SERVICE_STATE_UNINITIALIZED, svc->GetState(svc));

    TEST_ASSERT_EQUAL(0, svc->Initialize(svc, NULL));
    TEST_ASSERT_EQUAL(SERVICE_STATE_INITIALIZED, svc->GetState(svc));

    TEST_ASSERT_EQUAL(0, svc->Start(svc));
    TEST_ASSERT_EQUAL(SERVICE_STATE_STARTED, svc->GetState(svc));
    TEST_ASSERT_EQUAL(0, svc->CheckHealth(svc));

    TEST_ASSERT_EQUAL(0, svc->Stop(svc));
    TEST_ASSERT_EQUAL(SERVICE_STATE_STOPPED, svc->GetState(svc));
    TEST_ASSERT_NOT_EQUAL(0, svc->CheckHealth(svc));
}

/**
 * @brief Verifies idempotency: calling Initialize and Start twice is safe.
 */
void test_ClimateController_Lifecycle_Idempotent(void) {
    IClimateController *cc  = ClimateController_GetModule();
    IService           *svc = &cc->base;

    svc->Initialize(svc, NULL);
    TEST_ASSERT_EQUAL(0, svc->Initialize(svc, NULL)); /* Second call is a no-op. */
    TEST_ASSERT_EQUAL(SERVICE_STATE_INITIALIZED, svc->GetState(svc));

    svc->Start(svc);
    TEST_ASSERT_EQUAL(0, svc->Start(svc)); /* Second call is a no-op. */
    TEST_ASSERT_EQUAL(SERVICE_STATE_STARTED, svc->GetState(svc));
}

/**
 * @brief Verifies InitializeWithConfig stores and applies the custom config.
 */
void test_ClimateController_InitializeWithConfig(void) {
    IClimateController *cc = ClimateController_GetModule();

    ClimateControllerConfig custom = {
        .taskStackSize            = 8192,
        .taskPriority             = 3,
        .mainLoopDelayMs          = 500,
        .relayDebounceMs          = 200,
        .relayMinSwitchMs         = 2000,
        .watchdogTimeoutMs        = 10000,
        .enableEventDrivenSensors = false,
        .hysteresisDeadband       = 1.0f,
    };

    TEST_ASSERT_EQUAL(0, cc->InitializeWithConfig(cc, custom, NULL));
    TEST_ASSERT_EQUAL(SERVICE_STATE_INITIALIZED,
                      cc->base.GetState(&cc->base));

    TEST_ASSERT_EQUAL(8192,    climateControllerInstance.config.taskStackSize);
    TEST_ASSERT_EQUAL(500,     climateControllerInstance.config.mainLoopDelayMs);
    TEST_ASSERT_EQUAL(200,     climateControllerInstance.config.relayDebounceMs);
    TEST_ASSERT_EQUAL(2000,    climateControllerInstance.config.relayMinSwitchMs);
    TEST_ASSERT_EQUAL(false,   climateControllerInstance.config.enableEventDrivenSensors);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f,
                             climateControllerInstance.config.hysteresisDeadband);
}

/* ── Temperature Getters ─────────────────────────────────────────────────── */

/**
 * @brief Sentinel values are returned before any sensor reads occur.
 */
void test_ClimateController_Temperatures_SentinelValues(void) {
    IClimateController *cc = ClimateController_GetModule();
    cc->base.Initialize(&cc->base, NULL);

    TEST_ASSERT_FLOAT_WITHIN(0.001f, -127.0f, cc->GetCurrentObjectTemperature(cc));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, -127.0f, cc->GetCurrentAmbientTemperature(cc));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, -127.0f, cc->GetCurrentCpuTemperature(cc));
    TEST_ASSERT_EQUAL(0u, cc->GetLastSensorReadTime(cc));
}

/**
 * @brief Temperatures are updated atomically by the IR EventBus callback.
 */
void test_ClimateController_IRTempUpdated_EventCallback(void) {
    IClimateController *cc = ClimateController_GetModule();
    cc->base.Initialize(&cc->base, NULL);

    /* Set tick count to non-zero so GetLastSensorReadTime passes. */
    mocked_xTaskGetTickCount_fake.return_val = 1000;

    /* Simulate the EventBus callback with a timestamped measurement payload. */
    IRTempSample payload = {.temperatures = {38.5f, 24.1f}, .measured_time_us = 1000, .measured_tick = mocked_xTaskGetTickCount_fake.return_val};
    BusEvent ev = {
        .eventName = EVENT_IR_TEMP_UPDATED,
        .type      = EVENT_TYPE_INFO,
        .data      = &payload,
        .dataSize  = sizeof(payload),
    };
    climateControllerInstance.state = SERVICE_STATE_STARTED;
    Climate_OnIRTempUpdated(ev);

    TEST_ASSERT_FLOAT_WITHIN(0.001f, 38.5f, cc->GetCurrentObjectTemperature(cc));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 24.1f, cc->GetCurrentAmbientTemperature(cc));
    TEST_ASSERT_NOT_EQUAL(0u, cc->GetLastSensorReadTime(cc));
}


/**
 * @brief CPU temperature is updated by the internal temp EventBus callback.
 */
static IRTempSample polling_sample;
static esp_err_t polling_result;
static esp_err_t ReadPollingSample(IInfraredTemperatureSensor *self, IRTempSample *out) {
    (void)self;
    if (polling_result == ESP_OK) *out = polling_sample;
    return polling_result;
}

void test_Climate_SnapshotDoesNotRenewStaleData(void) {
    IClimateController *cc = ClimateController_GetModule();
    cc->base.Initialize(&cc->base, NULL);
    climateControllerInstance.config = ClimateController_GetDefaultConfig();
    climateControllerInstance.config.sensorStaleTimeoutMs = 1000;
    climateControllerInstance.history.capacity = 5;
    IInfraredTemperatureSensor sensor = {.ReadSample = ReadPollingSample};
    climateControllerInstance.tempSensor = &sensor;
    polling_sample = (IRTempSample){.temperatures = {30, 20}, .measured_time_us = 0, .measured_tick = 0};
    polling_result = ESP_OK;
    mocked_xTaskGetTickCount_fake.return_val = 0;
    Climate_PollIRSample();
    TEST_ASSERT_FALSE(cc->IsSensorStale(cc));
    mocked_xTaskGetTickCount_fake.return_val = pdMS_TO_TICKS(2000);
    Climate_PollIRSample();
    TEST_ASSERT_TRUE(cc->IsSensorStale(cc));
    TEST_ASSERT_EQUAL(1, climateControllerInstance.history.count);
    TEST_ASSERT_EQUAL(0, cc->GetLastSensorReadTime(cc));
    polling_result = ESP_ERR_TIMEOUT;
    Climate_PollIRSample();
    TEST_ASSERT_TRUE(cc->IsSensorStale(cc));
    polling_result = ESP_OK;
    polling_sample.measured_time_us = 2000000;
    polling_sample.measured_tick = mocked_xTaskGetTickCount_fake.return_val;
    Climate_PollIRSample();
    TEST_ASSERT_FALSE(cc->IsSensorStale(cc));
    TEST_ASSERT_EQUAL(2, climateControllerInstance.history.count);
    IRTempSample delayed = {.temperatures = {90, 50}, .measured_time_us = 1000000, .measured_tick = 1};
    BusEvent ev = {.data = &delayed, .dataSize = sizeof(delayed)};
    climateControllerInstance.state = SERVICE_STATE_STARTED;
    Climate_OnIRTempUpdated(ev);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 30, cc->GetCurrentObjectTemperature(cc));
    TEST_ASSERT_EQUAL(2, climateControllerInstance.history.count);
    delayed.measured_time_us = 3000000; delayed.temperatures[0] = NAN;
    climateControllerInstance.state = SERVICE_STATE_STARTED;
    Climate_OnIRTempUpdated(ev);
    TEST_ASSERT_EQUAL(2, climateControllerInstance.history.count);
}

void test_ClimateController_CpuTempUpdated_EventCallback(void) {
    IClimateController *cc = ClimateController_GetModule();
    cc->base.Initialize(&cc->base, NULL);

    float cpu = 67.3f;
    BusEvent ev = {
        .eventName = EVENT_INTERNAL_TEMP_UPDATED,
        .type      = EVENT_TYPE_INFO,
        .data      = &cpu,
        .dataSize  = sizeof(float),
    };
    climateControllerInstance.state = SERVICE_STATE_STARTED;
    Climate_OnInternalTempUpdated(ev);

    TEST_ASSERT_FLOAT_WITHIN(0.001f, 67.3f, cc->GetCurrentCpuTemperature(cc));
}

/**
 * @brief IR callback with NULL or undersized data is silently ignored.
 */
void test_ClimateController_IRTempCallback_NullData_Ignored(void) {
    IClimateController *cc = ClimateController_GetModule();
    cc->base.Initialize(&cc->base, NULL);

    BusEvent ev = {.eventName = EVENT_IR_TEMP_UPDATED, .data = NULL, .dataSize = 0};
    climateControllerInstance.state = SERVICE_STATE_STARTED;
    Climate_OnIRTempUpdated(ev); /* Must not crash. */

    /* Sentinel should still be present. */
    TEST_ASSERT_FLOAT_WITHIN(0.001f, -127.0f, cc->GetCurrentObjectTemperature(cc));
}

/* ── Relay State Getter ───────────────────────────────────────────────────── */

/**
 * @brief GetRelayState reflects the mock relay's physical state.
 */
void test_ClimateController_GetRelayState(void) {
    IClimateController *cc = ClimateController_GetModule();
    cc->base.Initialize(&cc->base, NULL);
    inject_mocks();

    mock_relay_is_open_val = false;
    TEST_ASSERT_FALSE(cc->GetRelayState(cc));

    mock_relay_is_open_val = true;
    TEST_ASSERT_TRUE(cc->GetRelayState(cc));
}

/**
 * @brief GetRelayState returns false when no relay is available.
 */
void test_ClimateController_GetRelayState_NoRelay(void) {
    IClimateController *cc = ClimateController_GetModule();
    cc->base.Initialize(&cc->base, NULL);
    climateControllerInstance.relay = NULL;

    TEST_ASSERT_FALSE(cc->GetRelayState(cc));
}

/* ── Relay Control Logic ─────────────────────────────────────────────────── */

/**
 * @brief System disabled forces relay to close immediately.
 */
void test_RelayLogic_SystemDisabled_ForcesRelayOff(void) {
    IClimateController *cc = ClimateController_GetModule();
    cc->base.Initialize(&cc->base, NULL);
    inject_mocks();
    climateControllerInstance.config = ClimateController_GetDefaultConfig();
    climateControllerInstance.is_config_set = true;

    mock_settings.system.system_enabled = false;
    mock_relay_is_open_val = true; /* Relay currently open. */

    Climate_EvaluateRelay(&mock_settings, 1000u);

    TEST_ASSERT_EQUAL(1, mock_relay_close_calls);
    TEST_ASSERT_FALSE(mock_relay_is_open_val);
}

/**
 * @brief System disabled does not issue redundant Close() when already closed.
 */
void test_RelayLogic_SystemDisabled_AlreadyOff_NoAction(void) {
    IClimateController *cc = ClimateController_GetModule();
    cc->base.Initialize(&cc->base, NULL);
    inject_mocks();
    climateControllerInstance.config = ClimateController_GetDefaultConfig();

    mock_settings.system.system_enabled = false;
    mock_relay_is_open_val = false; /* Already closed. */

    Climate_EvaluateRelay(&mock_settings, 1000u);

    TEST_ASSERT_EQUAL(0, mock_relay_close_calls);
}

/**
 * @brief Manual ON mode opens the relay immediately.
 */
void test_RelayLogic_ManualON_OpensRelay(void) {
    IClimateController *cc = ClimateController_GetModule();
    cc->base.Initialize(&cc->base, NULL);
    inject_mocks();
    climateControllerInstance.config = ClimateController_GetDefaultConfig();

    mock_settings.system.system_enabled     = true;
    mock_settings.control.fan_manual_mode   = true;
    mock_settings.control.fan_manual_state  = true;
    mock_relay_is_open_val = false;

    Climate_EvaluateRelay(&mock_settings, 1000u);

    TEST_ASSERT_EQUAL(1, mock_relay_open_calls);
    TEST_ASSERT_TRUE(mock_relay_is_open_val);
    /* Relay change event should have been published. */
    TEST_ASSERT_EQUAL(1, mock_eventbus_publish_calls);
    TEST_ASSERT_EQUAL_STRING(EVENT_RELAY_STATE_CHANGED, mock_last_event_name);
}

/**
 * @brief Manual OFF mode closes the relay immediately.
 */
void test_RelayLogic_ManualOFF_ClosesRelay(void) {
    IClimateController *cc = ClimateController_GetModule();
    cc->base.Initialize(&cc->base, NULL);
    inject_mocks();
    climateControllerInstance.config = ClimateController_GetDefaultConfig();

    mock_settings.system.system_enabled     = true;
    mock_settings.control.fan_manual_mode   = true;
    mock_settings.control.fan_manual_state  = false;
    mock_relay_is_open_val = true;

    Climate_EvaluateRelay(&mock_settings, 1000u);

    TEST_ASSERT_EQUAL(1, mock_relay_close_calls);
    TEST_ASSERT_FALSE(mock_relay_is_open_val);
}

/**
 * @brief Object-sensitive mode opens after debounce above the open setpoint.
 */
void test_RelayLogic_ObjectSensitive_OpensAfterDebounce(void) {
    IClimateController *cc = ClimateController_GetModule();
    cc->base.Initialize(&cc->base, NULL);
    inject_mocks();

    ClimateControllerConfig cfg = ClimateController_GetDefaultConfig();
    cfg.relayDebounceMs  = 500u;
    cfg.relayMinSwitchMs = 0u;  /* No min-switch delay for this test. */
    climateControllerInstance.config = cfg;

    mock_settings.system.system_enabled    = true;
    mock_settings.control.fan_manual_mode  = false;
    mock_settings.control.relay_open_temp  = 35.0f;
    mock_settings.control.relay_close_temp = 28.0f;
    mock_relay_is_open_val = false;

    /* Inject a temperature above the open setpoint. */
    IRTempSample payload = {.temperatures = {38.0f, 22.0f}, .measured_time_us = 1000, .measured_tick = mocked_xTaskGetTickCount_fake.return_val};
    BusEvent ev = {.data = &payload, .dataSize = sizeof(payload)};
    climateControllerInstance.state = SERVICE_STATE_STARTED;
    Climate_OnIRTempUpdated(ev);

    /* First evaluation — debounce starts. */
    Climate_EvaluateRelay(&mock_settings, 1000u);
    TEST_ASSERT_EQUAL(0, mock_relay_open_calls); /* Not yet — debouncing. */
    TEST_ASSERT_TRUE(climateControllerInstance.rsm.isConditionPending);

    /* Second evaluation after debounce window. */
    Climate_EvaluateRelay(&mock_settings, 1000u + cfg.relayDebounceMs + 1u);
    TEST_ASSERT_EQUAL(1, mock_relay_open_calls);
    TEST_ASSERT_FALSE(climateControllerInstance.rsm.isConditionPending);
}

/**
 * @brief Object-sensitive mode closes after debounce below the close setpoint.
 */
void test_RelayLogic_ObjectSensitive_ClosesAfterDebounce(void) {
    IClimateController *cc = ClimateController_GetModule();
    cc->base.Initialize(&cc->base, NULL);
    inject_mocks();

    ClimateControllerConfig cfg = ClimateController_GetDefaultConfig();
    cfg.relayDebounceMs  = 100u;
    cfg.relayMinSwitchMs = 0u;
    climateControllerInstance.config = cfg;

    mock_settings.system.system_enabled    = true;
    mock_settings.control.fan_manual_mode  = false;
    mock_settings.control.relay_open_temp  = 35.0f;
    mock_settings.control.relay_close_temp = 28.0f;
    mock_relay_is_open_val = true; /* Relay currently open. */

    /* Inject a temperature below the close setpoint. */
    IRTempSample payload = {.temperatures = {25.0f, 20.0f}, .measured_time_us = 1000, .measured_tick = mocked_xTaskGetTickCount_fake.return_val};
    BusEvent ev = {.data = &payload, .dataSize = sizeof(payload)};
    climateControllerInstance.state = SERVICE_STATE_STARTED;
    Climate_OnIRTempUpdated(ev);

    /* First evaluation — debounce starts. */
    Climate_EvaluateRelay(&mock_settings, 5000u);
    TEST_ASSERT_EQUAL(0, mock_relay_close_calls);

    /* Second evaluation after debounce. */
    Climate_EvaluateRelay(&mock_settings, 5000u + cfg.relayDebounceMs + 10u);
    TEST_ASSERT_EQUAL(1, mock_relay_close_calls);
}

/**
 * @brief Debounce is reset when the required action changes direction mid-flight.
 */
void test_RelayLogic_Debounce_ResetOnDirectionChange(void) {
    IClimateController *cc = ClimateController_GetModule();
    cc->base.Initialize(&cc->base, NULL);
    inject_mocks();

    ClimateControllerConfig cfg = ClimateController_GetDefaultConfig();
    cfg.relayDebounceMs  = 1000u;
    cfg.relayMinSwitchMs = 0u;
    climateControllerInstance.config = cfg;

    mock_settings.system.system_enabled   = true;
    mock_settings.control.fan_manual_mode = false;
    mock_settings.control.relay_open_temp  = 35.0f;
    mock_settings.control.relay_close_temp = 28.0f;
    mock_relay_is_open_val = false;

    /* Start debounce for OPEN action. */
    IRTempSample payload_high = {.temperatures = {40.0f, 22.0f}, .measured_time_us = 1};
    BusEvent ev_high = {.data = &payload_high, .dataSize = sizeof(payload_high)};
    climateControllerInstance.state = SERVICE_STATE_STARTED;
    Climate_OnIRTempUpdated(ev_high);
    Climate_EvaluateRelay(&mock_settings, 1000u);
    TEST_ASSERT_TRUE(climateControllerInstance.rsm.isConditionPending);
    TEST_ASSERT_TRUE(climateControllerInstance.rsm.pendingActionOpen);

    /* Temperature drops — pending action should flip to CLOSE (already closed, so action clears). */
    IRTempSample payload_low = {.temperatures = {20.0f, 18.0f}, .measured_time_us = 2};
    BusEvent ev_low = {.data = &payload_low, .dataSize = sizeof(payload_low)};
    climateControllerInstance.state = SERVICE_STATE_STARTED;
    Climate_OnIRTempUpdated(ev_low);
    Climate_EvaluateRelay(&mock_settings, 1100u);
    TEST_ASSERT_FALSE(climateControllerInstance.rsm.isConditionPending);
}

/**
 * @brief Object-sensitive mode holds its state between setpoints.
 */
void test_RelayLogic_ObjectSensitive_NoActionBetweenSetpoints(void) {
    IClimateController *cc = ClimateController_GetModule();
    cc->base.Initialize(&cc->base, NULL);
    inject_mocks();
    climateControllerInstance.config = ClimateController_GetDefaultConfig();

    mock_settings.system.system_enabled    = true;
    mock_settings.control.fan_manual_mode  = false;
    mock_settings.control.relay_open_temp  = 35.0f;
    mock_settings.control.relay_close_temp = 28.0f;
    mock_relay_is_open_val = false;

    /* Temperature in the dead zone between setpoints. */
    IRTempSample payload = {.temperatures = {31.0f, 22.0f}, .measured_time_us = 1000, .measured_tick = mocked_xTaskGetTickCount_fake.return_val};
    BusEvent ev = {.data = &payload, .dataSize = sizeof(payload)};
    climateControllerInstance.state = SERVICE_STATE_STARTED;
    Climate_OnIRTempUpdated(ev);

    Climate_EvaluateRelay(&mock_settings, 1000u);
    Climate_EvaluateRelay(&mock_settings, 2000u);

    TEST_ASSERT_EQUAL(0, mock_relay_open_calls);
    TEST_ASSERT_EQUAL(0, mock_relay_close_calls);
}

/* ── MenuHandlerEx ───────────────────────────────────────────────────────── */

/**
 * @brief Encoder CW in UI_STATE_SET_HIGH_TEMP increments relay_open_temp.
 */
void test_MenuHandler_SetHighTemp_Increment(void) {
    climateControllerInstance.configManager = &mock_config_mgr;

    float before = mock_settings.control.relay_open_temp;
    MenuPluginEvent ev = {
        .state     = UI_STATE_SET_HIGH_TEMP,
        .action    = MENU_PLUGIN_ACTION_ENCODER,
        .direction = 1,
    };
    climateControllerInstance.state = SERVICE_STATE_STARTED;
    Climate_MenuHandlerEx(&ev);

    TEST_ASSERT_FLOAT_WITHIN(0.001f, before + 0.5f,
                             mock_settings.control.relay_open_temp);
    TEST_ASSERT_EQUAL(1, menu_save_calls);
    /* ConfigurationManager owns publication after successful persistence. */
    TEST_ASSERT_EQUAL(0, mock_eventbus_publish_calls);
}

/**
 * @brief Encoder CCW in UI_STATE_SET_LOW_TEMP decrements relay_close_temp.
 */
void test_MenuHandler_SetLowTemp_Decrement(void) {
    climateControllerInstance.configManager = &mock_config_mgr;
    float before = mock_settings.control.relay_close_temp;
    MenuPluginEvent ev={.state=UI_STATE_SET_LOW_TEMP,.action=MENU_PLUGIN_ACTION_ENCODER,.direction=0};
    climateControllerInstance.state=SERVICE_STATE_STARTED;
    Climate_MenuHandlerEx(&ev);
    ev.direction=-1;Climate_MenuHandlerEx(&ev);
    TEST_ASSERT_FLOAT_WITHIN(0.001f,before,mock_settings.control.relay_close_temp);
    int16_t low,high;unsigned phase;
    TEST_ASSERT_TRUE(MenuRangeEditor_Read(ev.state,&low,&high,&phase));
    TEST_ASSERT_EQUAL((int)(before*10)-1,low);
    ev.action=MENU_PLUGIN_ACTION_BUTTON;
    Climate_MenuHandlerEx(&ev);Climate_MenuHandlerEx(&ev);
    TEST_ASSERT_FLOAT_WITHIN(0.001f,before-0.1f,mock_settings.control.relay_close_temp);
}

/**
 * @brief Encoder CW in UI_STATE_CALIBRATE_OBJECT increments ir_object_cal.
 */
void test_MenuHandler_CalibrateObject_Increment(void) {
    climateControllerInstance.configManager = &mock_config_mgr;

    float before = mock_settings.calibration.ir_object_cal;
    MenuPluginEvent ev = {
        .state     = UI_STATE_CALIBRATE_OBJECT,
        .action    = MENU_PLUGIN_ACTION_ENCODER,
        .direction = 1,
    };
    climateControllerInstance.state = SERVICE_STATE_STARTED;
    Climate_MenuHandlerEx(&ev);

    TEST_ASSERT_FLOAT_WITHIN(0.001f, before + 0.1f,
                             mock_settings.calibration.ir_object_cal);
}

/**
 * @brief Encoder in UI_STATE_FAN_MODE: auto → manual ON when rotating CW.
 */
void test_MenuHandler_FanMode_ObjectToAmbientDelta(void) {
    climateControllerInstance.configManager = &mock_config_mgr;
    mock_settings.control.fan_manual_mode  = false;
    mock_settings.control.fan_manual_state = false;

    MenuPluginEvent ev = {
        .state     = UI_STATE_FAN_MODE,
        .action    = MENU_PLUGIN_ACTION_ENCODER,
        .direction = 1,
    };
    climateControllerInstance.state = SERVICE_STATE_STARTED;
    Climate_MenuHandlerEx(&ev);

    TEST_ASSERT_FALSE(mock_settings.control.fan_manual_mode);
    TEST_ASSERT_EQUAL(1,mock_settings.control.fan_strategy);
}

/**
 * @brief Encoder direction 0 in any state is a no-op (no event published).
 */
void test_MenuHandler_NoRotation_NoOp(void) {
    climateControllerInstance.configManager = &mock_config_mgr;

    MenuPluginEvent ev = {
        .state     = UI_STATE_SET_HIGH_TEMP,
        .action    = MENU_PLUGIN_ACTION_ENCODER,
        .direction = 0,
    };
    climateControllerInstance.state = SERVICE_STATE_STARTED;
    Climate_MenuHandlerEx(&ev);

    TEST_ASSERT_EQUAL(0, mock_eventbus_publish_calls);
}

/**
 * @brief Non-encoder action is silently ignored (no event published).
 */
void test_MenuHandler_ButtonAction_Ignored(void) {
    climateControllerInstance.configManager = &mock_config_mgr;

    MenuPluginEvent ev = {
        .state     = UI_STATE_SET_HIGH_TEMP,
        .action    = MENU_PLUGIN_ACTION_BUTTON,
        .direction = 0,
    };
    climateControllerInstance.state = SERVICE_STATE_STARTED;
    Climate_MenuHandlerEx(&ev);

    TEST_ASSERT_EQUAL(0, mock_eventbus_publish_calls);
}

/* ── Self-Test ───────────────────────────────────────────────────────────── */

/**
 * @brief RunSelfTest passes when service is started and all dependencies available.
 */
void test_SelfTest_AllDepsAvailable_Passes(void) {
    IClimateController *cc = ClimateController_GetModule();
    cc->base.Initialize(&cc->base, NULL);
    climateControllerInstance.state = SERVICE_STATE_STARTED;
    inject_mocks();

    /* Inject plausible temperatures directly into cache. */
    climateControllerInstance.objectTemp = 30.0f;
    climateControllerInstance.ambientTemp = 22.0f;
    climateControllerInstance.cpuTemp = 45.0f;
    climateControllerInstance.lastSensorReadTick = 1000;

    int32_t result = cc->RunSelfTest(cc);
    TEST_ASSERT_EQUAL(0, result);
}

/**
 * @brief RunSelfTest fails when the relay dependency is missing.
 */
void test_SelfTest_NoRelay_Fails(void) {
    IClimateController *cc = ClimateController_GetModule();
    cc->base.Initialize(&cc->base, NULL);
    climateControllerInstance.state = SERVICE_STATE_STARTED;
    inject_mocks();
    climateControllerInstance.relay = NULL; /* Remove relay. */

    /* Inject plausible temperatures directly into cache. */
    climateControllerInstance.objectTemp = 30.0f;
    climateControllerInstance.ambientTemp = 22.0f;
    climateControllerInstance.cpuTemp = 45.0f;
    climateControllerInstance.lastSensorReadTick = 1000;

    int32_t result = cc->RunSelfTest(cc);
    TEST_ASSERT_LESS_THAN(0, result);
}

/**
 * @brief RunSelfTest fails when temperature is an implausible sentinel value.
 */
void test_SelfTest_SentinelTemp_Fails(void) {
    IClimateController *cc = ClimateController_GetModule();
    cc->base.Initialize(&cc->base, NULL);
    climateControllerInstance.state = SERVICE_STATE_STARTED;
    inject_mocks();

    /* Do NOT inject temperatures — they remain at -127°C sentinel. */
    climateControllerInstance.objectTemp = -127.0f;
    climateControllerInstance.ambientTemp = -127.0f;
    climateControllerInstance.cpuTemp = -127.0f;

    int32_t result = cc->RunSelfTest(cc);
    TEST_ASSERT_LESS_THAN(0, result);
}


/* ── Default Config ──────────────────────────────────────────────────────── */

/**
 * @brief Default config uses SystemConfigs.h constants, not hardcoded values.
 */
void test_DefaultConfig_UsesSystemConfigConstants(void) {
    ClimateControllerConfig cfg = ClimateController_GetDefaultConfig();

    TEST_ASSERT_EQUAL(CLIMATE_CTRL_TASK_STACK_SIZE, cfg.taskStackSize);
    TEST_ASSERT_EQUAL(CLIMATE_CTRL_TASK_PRIORITY,   cfg.taskPriority);
    TEST_ASSERT_EQUAL(APP_MAIN_LOOP_DELAY_MS,        cfg.mainLoopDelayMs);
    TEST_ASSERT_EQUAL(RELAY_DEBOUNCE_MS,             cfg.relayDebounceMs);
    TEST_ASSERT_EQUAL(RELAY_MIN_SWITCH_MS,           cfg.relayMinSwitchMs);
    TEST_ASSERT_TRUE(cfg.enableEventDrivenSensors);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, cfg.hysteresisDeadband);
}


/* ═══════════════════════════════════════════════════════════════════════════
 * Relay Logic — Minimum Switch-Time & Hysteresis (New Tests)
 * ═══════════════════════════════════════════════════════════════════════════ */

/**
 * @brief Relay does not toggle a second time if min-switch-time has not elapsed.
 *
 * Sets up a relay in OPEN state. Simulates a close condition (low temp) but
 * applies it before relayMinSwitchMs has elapsed since the relay was last
 * changed. The relay must remain open.
 */
void test_RelayLogic_MinSwitchTime_PreventsRapidToggle(void) {
    IClimateController *cc = ClimateController_GetModule();
    cc->base.Initialize(&cc->base, NULL);
    inject_mocks();
    climateControllerInstance.config = ClimateController_GetDefaultConfig();
    climateControllerInstance.config.relayDebounceMs  = 0;
    climateControllerInstance.config.relayMinSwitchMs = 2000;
    climateControllerInstance.is_config_set = true;

    mock_relay_is_open_val = true; /* Relay currently open. */
    mock_settings.system.system_enabled = true;
    mock_settings.control.fan_manual_mode = false;

    /* Set temperature below close threshold. */
    climateControllerInstance.objectTemp = 20.0f; /* Well below close=28 */
    climateControllerInstance.hasSensorSample = true;
    climateControllerInstance.lastSensorReadTick = 1000 / portTICK_PERIOD_MS;

    /* Relay changed 500ms ago — min-switch (2000ms) not elapsed. */
    climateControllerInstance.rsm.relayStateChangeTime = 500u;

    Climate_EvaluateRelay(&mock_settings, 1000u); /* now=1000, elapsed=500 */

    /* Relay must NOT have closed — min-switch-time still active. */
    TEST_ASSERT_EQUAL(0, mock_relay_close_calls);
}

/**
 * @brief With hysteresis dead-band, a CLOSED relay should NOT open until
 *        the temperature exceeds (openSetpoint - deadband).
 *
 * open_temp = 35.0, db = 2.0 => effective open threshold = 33.0
 * objTemp = 33.5 => above 33.0 so debounce starts (no immediate open).
 * objTemp = 32.5 => below 33.0 so relay stays closed (no action).
 */
void test_RelayLogic_Hysteresis_ClosedRelayNotOpenBelowDeadband(void) {
    IClimateController *cc = ClimateController_GetModule();
    cc->base.Initialize(&cc->base, NULL);
    inject_mocks();
    climateControllerInstance.config = ClimateController_GetDefaultConfig();
    climateControllerInstance.config.relayDebounceMs    = 0;
    climateControllerInstance.config.relayMinSwitchMs   = 0;
    climateControllerInstance.config.hysteresisDeadband = 2.0f;
    climateControllerInstance.is_config_set = true;

    mock_relay_is_open_val = false; /* Relay CLOSED. */
    mock_settings.system.system_enabled   = true;
    mock_settings.control.fan_manual_mode = false;
    mock_settings.control.relay_open_temp  = 35.0f;
    mock_settings.control.relay_close_temp = 28.0f;

    /* 32.5 < (35.0 - 2.0) = 33.0 => should NOT trigger open. */
    climateControllerInstance.objectTemp = 32.5f;
    climateControllerInstance.hasSensorSample = true;
    climateControllerInstance.lastSensorReadTick = 1000 / portTICK_PERIOD_MS;
    Climate_EvaluateRelay(&mock_settings, 1000u);

    TEST_ASSERT_EQUAL(0, mock_relay_open_calls);
    /* A nonzero legacy modifier must affect the actual switching boundary. */
    climateControllerInstance.objectTemp = 33.5f;
    Climate_EvaluateRelay(&mock_settings, 1001u);
    Climate_EvaluateRelay(&mock_settings, 1002u);
    TEST_ASSERT_EQUAL(1, mock_relay_open_calls);
}

/**
 * @brief With hysteresis dead-band, an OPEN relay should NOT close until
 *        the temperature drops below (closeSetpoint + deadband).
 *
 * close_temp = 28.0, db = 2.0 => effective close threshold = 30.0
 * objTemp = 29.5 => below 30.0 => close not triggered.
 * This test verifies the relay stays open.
 */
void test_RelayLogic_Hysteresis_OpenRelayNotClosedAboveDeadband(void) {
    IClimateController *cc = ClimateController_GetModule();
    cc->base.Initialize(&cc->base, NULL);
    inject_mocks();
    climateControllerInstance.config = ClimateController_GetDefaultConfig();
    climateControllerInstance.config.relayDebounceMs    = 0;
    climateControllerInstance.config.relayMinSwitchMs   = 0;
    climateControllerInstance.config.hysteresisDeadband = 2.0f;
    climateControllerInstance.is_config_set = true;

    mock_relay_is_open_val = true; /* Relay OPEN. */
    mock_settings.system.system_enabled   = true;
    mock_settings.control.fan_manual_mode = false;
    mock_settings.control.relay_open_temp  = 35.0f;
    mock_settings.control.relay_close_temp = 28.0f;

    /* 29.5 > (28.0 + 2.0) = 30.0 => above effective close threshold, NO close. */
    climateControllerInstance.objectTemp = 30.5f;
    climateControllerInstance.hasSensorSample = true;
    climateControllerInstance.lastSensorReadTick = 1000 / portTICK_PERIOD_MS;
    Climate_EvaluateRelay(&mock_settings, 1000u);

    TEST_ASSERT_EQUAL(0, mock_relay_close_calls);
    climateControllerInstance.objectTemp = 29.5f;
    Climate_EvaluateRelay(&mock_settings, 1001u);
    Climate_EvaluateRelay(&mock_settings, 1002u);
    TEST_ASSERT_EQUAL(1, mock_relay_close_calls);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * CPU Thermal Protection (New Tests)
 * ═══════════════════════════════════════════════════════════════════════════ */

/**
 * @brief EVENT_SYSTEM_THERMAL_ALERT forces relay ON when cpuTempProtection is enabled.
 */
void test_CpuThermalProtection_AlertForcesRelayOn(void) {
    IClimateController *cc = ClimateController_GetModule();
    cc->base.Initialize(&cc->base, NULL);
    inject_mocks();
    climateControllerInstance.config = ClimateController_GetDefaultConfig();
    climateControllerInstance.config.cpuTempProtectionEnabled = true;
    climateControllerInstance.is_config_set = true;
    mock_relay_is_open_val = false; /* Relay starts closed. */

    /* Simulate receiving the thermal alert event. */
    BusEvent ev = {
        .eventName = EVENT_SYSTEM_THERMAL_ALERT,
        .type      = EVENT_TYPE_WARNING,
        .data      = NULL,
        .dataSize  = 0,
    };
    climateControllerInstance.state = SERVICE_STATE_STARTED;
    Climate_OnThermalAlert(ev);

    TEST_ASSERT_TRUE(climateControllerInstance.cpuThermalOverride);
    TEST_ASSERT_EQUAL(0, mock_relay_open_calls); /* Callback only flags ownership. */
    Climate_EvaluateRelay(&mock_settings,1000);
    TEST_ASSERT_EQUAL(1, mock_relay_open_calls);
}

/**
 * @brief CPU thermal override is cleared once CPU temperature drops below warning.
 */
void test_CpuThermalProtection_OverrideClearedWhenCpuCools(void) {
    IClimateController *cc = ClimateController_GetModule();
    cc->base.Initialize(&cc->base, NULL);
    inject_mocks();
    climateControllerInstance.config = ClimateController_GetDefaultConfig();
    climateControllerInstance.config.cpuTempProtectionEnabled = true;
    climateControllerInstance.is_config_set = true;

    /* Set override active with a high CPU temp that should then cool. */
    climateControllerInstance.cpuThermalOverride = true;
    climateControllerInstance.cpuTemp = 60.0f;             /* Below 80 warning. */
    mock_settings.thermal.cpu_temp_warning = 80.0f;

    Climate_CheckCpuThermalOverride(&mock_settings);

    TEST_ASSERT_FALSE(climateControllerInstance.cpuThermalOverride);
}

/**
 * @brief When cpuTempProtection is disabled, the thermal alert is ignored.
 */
void test_CpuThermalProtection_DisabledFlag_AlertIgnored(void) {
    IClimateController *cc = ClimateController_GetModule();
    cc->base.Initialize(&cc->base, NULL);
    inject_mocks();
    climateControllerInstance.config = ClimateController_GetDefaultConfig();
    climateControllerInstance.config.cpuTempProtectionEnabled = false;
    climateControllerInstance.is_config_set = true;
    mock_relay_is_open_val = false;

    BusEvent ev = {
        .eventName = EVENT_SYSTEM_THERMAL_ALERT,
        .type      = EVENT_TYPE_WARNING,
        .data      = NULL,
        .dataSize  = 0,
    };
    climateControllerInstance.state = SERVICE_STATE_STARTED;
    Climate_OnThermalAlert(ev);

    /* Override flag must NOT be set and relay must not open. */
    TEST_ASSERT_FALSE(climateControllerInstance.cpuThermalOverride);
    TEST_ASSERT_EQUAL(0, mock_relay_open_calls);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Stop / EventBus Cleanup (New Test)
 * ═══════════════════════════════════════════════════════════════════════════ */

/**
 * @brief Stop() calls Unsubscribe on the EventBus for all registered events.
 */
void test_Stop_UnsubscribesEventBus(void) {
    IClimateController *cc = ClimateController_GetModule();
    IService           *svc = &cc->base;

    svc->Initialize(svc, NULL);
    svc->Start(svc);

    mock_eventbus_unsubscribe_calls = 0; /* Reset counter after Start. */

    svc->Stop(svc);

    /* Stop() must unsubscribe at least the IR temp, internal temp, and thermal
     * alert events (3 subscriptions registered during Start()). */
    TEST_ASSERT_GREATER_THAN(0, mock_eventbus_unsubscribe_calls);
    TEST_ASSERT_EQUAL(SERVICE_STATE_STOPPED, svc->GetState(svc));
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Sensor Stale Detection (New Tests)
 * ═══════════════════════════════════════════════════════════════════════════ */

/**
 * @brief IsSensorStale() returns true when no reading has been received yet.
 */
void test_SensorStale_ReturnsTrueWhenNoReading(void) {
    IClimateController *cc = ClimateController_GetModule();
    cc->base.Initialize(&cc->base, NULL);
    climateControllerInstance.config = ClimateController_GetDefaultConfig();
    climateControllerInstance.config.sensorStaleTimeoutMs = 5000;
    climateControllerInstance.is_config_set = true;
    climateControllerInstance.lastSensorReadTick = 0; /* No reading. */

    TEST_ASSERT_TRUE(cc->IsSensorStale(cc));
}

/**
 * @brief IsSensorStale() returns false immediately after a fresh IR reading.
 */
void test_SensorStale_ReturnsFalseAfterFreshReading(void) {
    IClimateController *cc = ClimateController_GetModule();
    cc->base.Initialize(&cc->base, NULL);
    climateControllerInstance.config = ClimateController_GetDefaultConfig();
    climateControllerInstance.config.sensorStaleTimeoutMs = 5000;
    climateControllerInstance.is_config_set = true;

    /* xTaskGetTickCount() must return non-zero so lastSensorReadTick != 0. */
    mocked_xTaskGetTickCount_fake.return_val = 1000;

    IRTempSample payload = {.temperatures = {30.0f, 22.0f}, .measured_time_us = 1000, .measured_tick = mocked_xTaskGetTickCount_fake.return_val};
    BusEvent ev = {
        .eventName = EVENT_IR_TEMP_UPDATED,
        .type      = EVENT_TYPE_INFO,
        .data      = &payload,
        .dataSize  = sizeof(payload),
    };
    climateControllerInstance.state = SERVICE_STATE_STARTED;
    Climate_OnIRTempUpdated(ev);

    /* After a fresh reading, IsSensorStale must return false. */
    TEST_ASSERT_FALSE(cc->IsSensorStale(cc));
}

/**
 * @brief IsSensorStale() always returns false when sensorStaleTimeoutMs == 0.
 */
void test_SensorStale_ZeroTimeoutDisablesDetection(void) {
    IClimateController *cc = ClimateController_GetModule();
    cc->base.Initialize(&cc->base, NULL);
    climateControllerInstance.config = ClimateController_GetDefaultConfig();
    climateControllerInstance.config.sensorStaleTimeoutMs = 0; /* Disabled. */
    climateControllerInstance.is_config_set = true;
    climateControllerInstance.lastSensorReadTick = 0; /* No reading. */

    TEST_ASSERT_FALSE(cc->IsSensorStale(cc));
}

/* ═══════════════════════════════════════════════════════════════════════════
 * New Getter Tests
 * ═══════════════════════════════════════════════════════════════════════════ */

/**
 * @brief GetFanMode() returns CLIMATE_FAN_OBJECT_SENSITIVE when fan_manual_mode is false.
 */
void test_GetFanMode_RejectsUnconfirmedConfiguration(void) {
    IClimateController *cc=ClimateController_GetModule();
    TEST_ASSERT_EQUAL(0,cc->base.Initialize(&cc->base,NULL)); inject_mocks();
    snapshot_result=ESP_FAIL;
    TEST_ASSERT_EQUAL(CLIMATE_FAN_UNKNOWN,cc->GetFanMode(cc));
    snapshot_result=ESP_OK;
    climateControllerInstance.configManager=NULL;
    TEST_ASSERT_EQUAL(CLIMATE_FAN_UNKNOWN,cc->GetFanMode(cc));
    inject_mocks();
    TEST_ASSERT_EQUAL(CLIMATE_FAN_OBJECT_SENSITIVE,cc->GetFanMode(cc));
}

void test_GetFanMode_ReturnsObjectSensitive(void) {
    IClimateController *cc = ClimateController_GetModule();
    cc->base.Initialize(&cc->base, NULL);
    inject_mocks();

    mock_settings.control.fan_manual_mode  = false;
    mock_settings.control.fan_manual_state = false;

    TEST_ASSERT_EQUAL(CLIMATE_FAN_OBJECT_SENSITIVE, cc->GetFanMode(cc));
}

/**
 * @brief GetFanMode() returns CLIMATE_FAN_MANUAL_ON when manual mode + state=true.
 */
void test_GetFanMode_ReturnsManualOn(void) {
    IClimateController *cc = ClimateController_GetModule();
    cc->base.Initialize(&cc->base, NULL);
    inject_mocks();

    mock_settings.control.fan_manual_mode  = true;
    mock_settings.control.fan_manual_state = true;

    TEST_ASSERT_EQUAL(CLIMATE_FAN_MANUAL_ON, cc->GetFanMode(cc));
}

/**
 * @brief GetFanMode() returns CLIMATE_FAN_MANUAL_OFF when manual mode + state=false.
 */
void test_GetFanMode_ReturnsManualOff(void) {
    IClimateController *cc = ClimateController_GetModule();
    cc->base.Initialize(&cc->base, NULL);
    inject_mocks();

    mock_settings.control.fan_manual_mode  = true;
    mock_settings.control.fan_manual_state = false;

    TEST_ASSERT_EQUAL(CLIMATE_FAN_MANUAL_OFF, cc->GetFanMode(cc));
}

/**
 * @brief GetConfig() returns the active configuration snapshot.
 */
void test_GetConfig_ReturnsActiveConfig(void) {
    IClimateController *cc = ClimateController_GetModule();
    cc->base.Initialize(&cc->base, NULL);

    ClimateControllerConfig custom = ClimateController_GetDefaultConfig();
    custom.mainLoopDelayMs   = 750;
    custom.relayDebounceMs   = 123;
    custom.hysteresisDeadband = 1.5f;
    cc->InitializeWithConfig(cc, custom, NULL);

    ClimateControllerConfig got = cc->GetConfig(cc);
    TEST_ASSERT_EQUAL(750,  got.mainLoopDelayMs);
    TEST_ASSERT_EQUAL(123,  got.relayDebounceMs);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.5f, got.hysteresisDeadband);
}

/**
 * @brief GetRelayDebounceState() returns false when no condition is pending.
 */
void test_GetRelayDebounceState_FalseInitially(void) {
    IClimateController *cc = ClimateController_GetModule();
    cc->base.Initialize(&cc->base, NULL);

    climateControllerInstance.rsm.isConditionPending = false;
    TEST_ASSERT_FALSE(cc->GetRelayDebounceState(cc));
}

/**
 * @brief GetRelayDebounceState() returns true while a debounce is in progress.
 */
void test_GetRelayDebounceState_TrueWhilePending(void) {
    IClimateController *cc = ClimateController_GetModule();
    cc->base.Initialize(&cc->base, NULL);
    inject_mocks();
    climateControllerInstance.config = ClimateController_GetDefaultConfig();
    climateControllerInstance.config.relayDebounceMs  = 5000; /* Long debounce. */
    climateControllerInstance.config.relayMinSwitchMs = 0;
    climateControllerInstance.is_config_set = true;

    mock_relay_is_open_val = false;
    mock_settings.system.system_enabled   = true;
    mock_settings.control.fan_manual_mode = false;
    climateControllerInstance.objectTemp  = 40.0f; /* Above open threshold. */
    climateControllerInstance.hasSensorSample = true;
    climateControllerInstance.lastSensorReadTick = 1000 / portTICK_PERIOD_MS;

    /* First call starts the debounce countdown. */
    Climate_EvaluateRelay(&mock_settings, 1000u);

    TEST_ASSERT_TRUE(cc->GetRelayDebounceState(cc));
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Temperature History Ring Buffer (New Tests)
 * ═══════════════════════════════════════════════════════════════════════════ */

/**
 * @brief GetHistory() returns 0 entries immediately after initialization.
 */
void test_History_EmptyOnInit(void) {
    IClimateController *cc = ClimateController_GetModule();
    cc->base.Initialize(&cc->base, NULL);
    climateControllerInstance.config = ClimateController_GetDefaultConfig();
    climateControllerInstance.history.capacity = CLIMATE_HISTORY_BUFFER_SIZE;
    climateControllerInstance.is_config_set = true;

    ClimateTempHistoryEntry out[CLIMATE_HISTORY_BUFFER_SIZE];
    uint8_t count = ClimateController_GetHistory(out, CLIMATE_HISTORY_BUFFER_SIZE);
    TEST_ASSERT_EQUAL(0, count);
}

/**
 * @brief An IR update event appends a history entry with correct values.
 */
void test_History_AppendedOnIRUpdate(void) {
    IClimateController *cc = ClimateController_GetModule();
    cc->base.Initialize(&cc->base, NULL);
    climateControllerInstance.config = ClimateController_GetDefaultConfig();
    climateControllerInstance.history.capacity = CLIMATE_HISTORY_BUFFER_SIZE;
    climateControllerInstance.is_config_set = true;

    IRTempSample payload = {.temperatures = {42.5f, 23.1f}, .measured_time_us = 1000, .measured_tick = mocked_xTaskGetTickCount_fake.return_val};
    BusEvent ev = {
        .eventName = EVENT_IR_TEMP_UPDATED,
        .type      = EVENT_TYPE_INFO,
        .data      = &payload,
        .dataSize  = sizeof(payload),
    };
    climateControllerInstance.state = SERVICE_STATE_STARTED;
    Climate_OnIRTempUpdated(ev);

    ClimateTempHistoryEntry out[CLIMATE_HISTORY_BUFFER_SIZE];
    uint8_t count = ClimateController_GetHistory(out, CLIMATE_HISTORY_BUFFER_SIZE);
    TEST_ASSERT_EQUAL(1, count);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 42.5f, out[0].objectTemp);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 23.1f, out[0].ambientTemp);
}

/**
 * @brief After filling the ring buffer, oldest entries are overwritten (wrap).
 */
void test_History_RingBufferWraps(void) {
    IClimateController *cc = ClimateController_GetModule();
    cc->base.Initialize(&cc->base, NULL);
    climateControllerInstance.config = ClimateController_GetDefaultConfig();
    climateControllerInstance.history.capacity = 3; /* Tiny buffer for quick wrap. */
    climateControllerInstance.is_config_set = true;

    /* Push 4 entries — oldest (25°C) should be evicted. */
    float temps[4] = {25.0f, 30.0f, 35.0f, 40.0f};
    for (int i = 0; i < 4; i++) {
        IRTempSample p = {.temperatures = {temps[i], 20.0f}, .measured_time_us = i + 1, .measured_tick = (uint32_t)i};
        BusEvent ev = {
            .eventName = EVENT_IR_TEMP_UPDATED,
            .type      = EVENT_TYPE_INFO,
            .data      = &p,
            .dataSize  = sizeof(p),
        };
        climateControllerInstance.state = SERVICE_STATE_STARTED;
        Climate_OnIRTempUpdated(ev);
    }

    ClimateTempHistoryEntry out[10];
    uint8_t count = ClimateController_GetHistory(out, 10);
    TEST_ASSERT_EQUAL(3, count);                               /* Buffer holds 3. */
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 30.0f, out[0].objectTemp); /* 25 evicted. */
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 35.0f, out[1].objectTemp);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 40.0f, out[2].objectTemp);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Test Runner Entry Point
 * ═══════════════════════════════════════════════════════════════════════════ */


void test_ClimateController_StopTimeoutAndRestart(void) {
    IClimateController *cc = ClimateController_GetModule();
    TEST_ASSERT_EQUAL(0, cc->base.Initialize(&cc->base, NULL));
    TEST_ASSERT_EQUAL(0, cc->base.Start(&cc->base));
    RESET_FAKE(mocked_vTaskDelete);
    mocked_xQueueReceive_fake.custom_fake = NULL;
    mocked_xQueueReceive_fake.return_val = pdFALSE;
    TEST_ASSERT_EQUAL(-1, cc->base.Stop(&cc->base));
    TEST_ASSERT_EQUAL(0, mocked_vTaskDelete_fake.call_count);
    TEST_ASSERT_EQUAL(-1, cc->base.CheckHealth(&cc->base));
    mocked_xQueueReceive_fake.custom_fake = climate_ack_receive;
    TEST_ASSERT_EQUAL(0, cc->base.Stop(&cc->base));
    TEST_ASSERT_EQUAL(1, mocked_vTaskDelete_fake.call_count);
    TEST_ASSERT_EQUAL(0, cc->base.Start(&cc->base));
    TEST_ASSERT_EQUAL(0, cc->base.CheckHealth(&cc->base));
}
void test_ClimateController_AllocationAndLaunchFailures(void) {
    IClimateController *cc = ClimateController_GetModule();
    mocked_xQueueCreate_fake.return_val = NULL;
    TEST_ASSERT_EQUAL(-1, cc->InitializeWithConfig(cc, ClimateController_GetDefaultConfig(), NULL));
    TEST_ASSERT_EQUAL(SERVICE_STATE_UNINITIALIZED, cc->base.GetState(&cc->base));
    TEST_ASSERT_NULL(climateControllerInstance.worker.gate);
    mocked_xQueueCreate_fake.return_val = (QueueHandle_t)2;
    TEST_ASSERT_EQUAL(0, cc->base.Initialize(&cc->base, NULL));
    mocked_xTaskCreate_fake.custom_fake = NULL;
    mocked_xTaskCreate_fake.return_val = pdFAIL;
    TEST_ASSERT_EQUAL(-1, cc->base.Start(&cc->base));
    TEST_ASSERT_EQUAL(SERVICE_STATE_INITIALIZED, cc->base.GetState(&cc->base));
    TEST_ASSERT_NULL(atomic_load(&climateControllerInstance.worker.task));
}

void test_Climate_ConfigCannotChangeAfterStart(void) {
    IClimateController *cc = ClimateController_GetModule();
    ClimateControllerConfig original = ClimateController_GetDefaultConfig();
    TEST_ASSERT_EQUAL(0, cc->InitializeWithConfig(cc, original, NULL));
    TEST_ASSERT_EQUAL(0, cc->base.Start(&cc->base));
    ClimateControllerConfig changed = original;
    changed.mainLoopDelayMs++;
    TEST_ASSERT_NOT_EQUAL(0, cc->InitializeWithConfig(cc, changed, NULL));
    TEST_ASSERT_EQUAL(original.mainLoopDelayMs, cc->GetConfig(cc).mainLoopDelayMs);
    TEST_ASSERT_EQUAL(0, cc->base.Stop(&cc->base));
    TEST_ASSERT_NOT_EQUAL(0, cc->InitializeWithConfig(cc, changed, NULL));
    TEST_ASSERT_EQUAL(original.mainLoopDelayMs, cc->GetConfig(cc).mainLoopDelayMs);
}

void test_Climate_ConfigLockFailurePreservesSettings(void) {
    IClimateController *cc = ClimateController_GetModule();
    ClimateControllerConfig original = ClimateController_GetDefaultConfig();
    TEST_ASSERT_EQUAL(0, cc->InitializeWithConfig(cc, original, NULL));
    ClimateControllerConfig changed = original;
    changed.mainLoopDelayMs++;
    mocked_xSemaphoreTake_fake.return_val = pdFALSE;
    TEST_ASSERT_NOT_EQUAL(0, cc->InitializeWithConfig(cc, changed, NULL));
    TEST_ASSERT_EQUAL(original.mainLoopDelayMs, cc->GetConfig(cc).mainLoopDelayMs);
    TEST_ASSERT_NOT_EQUAL(0, cc->InitializeWithConfig(NULL, changed, NULL));
}

void test_Climate_SettingsSnapshotFailureDoesNotActuate(void) {
    IClimateController *cc = ClimateController_GetModule();
    cc->base.Initialize(&cc->base, NULL);
    inject_mocks();
    snapshot_result = ESP_ERR_TIMEOUT;
    TEST_ASSERT_FALSE(Climate_EvaluateSettingsSnapshot());
    TEST_ASSERT_EQUAL(0, mock_relay_open_calls);
    TEST_ASSERT_EQUAL(0, mock_relay_close_calls);
    TEST_ASSERT_EQUAL(CLIMATE_FAN_UNKNOWN, cc->GetFanMode(cc));
    snapshot_result = ESP_OK;
    mock_settings.control.fan_manual_mode = true;
    mock_settings.control.fan_manual_state = true;
    TEST_ASSERT_EQUAL(CLIMATE_FAN_MANUAL_ON, cc->GetFanMode(cc));
    TEST_ASSERT_TRUE(Climate_EvaluateSettingsSnapshot());
}

void test_MenuHandler_FailedPersistencePreservesEverySetting(void);
void test_MenuHandler_TemperatureUnitCyclesThroughPersistentWrite(void);
void test_ClimateController_All(void) {
    RUN_TEST(test_MenuHandler_FailedPersistencePreservesEverySetting);
    RUN_TEST(test_MenuHandler_TemperatureUnitCyclesThroughPersistentWrite);

    RUN_TEST(test_ClimateController_StopTimeoutAndRestart);
    RUN_TEST(test_ClimateController_AllocationAndLaunchFailures);
    /* Lifecycle */
    RUN_TEST(test_ClimateController_Lifecycle);
    RUN_TEST(test_ClimateController_Lifecycle_Idempotent);
    RUN_TEST(test_ClimateController_InitializeWithConfig);

    /* Temperature getters */
    RUN_TEST(test_ClimateController_Temperatures_SentinelValues);
    RUN_TEST(test_ClimateController_IRTempUpdated_EventCallback);
    RUN_TEST(test_Climate_SnapshotDoesNotRenewStaleData);
    RUN_TEST(test_ClimateController_CpuTempUpdated_EventCallback);
    RUN_TEST(test_ClimateController_IRTempCallback_NullData_Ignored);

    /* Relay state */
    RUN_TEST(test_ClimateController_GetRelayState);
    RUN_TEST(test_ClimateController_GetRelayState_NoRelay);

    /* Relay logic */
    RUN_TEST(test_RelayLogic_SystemDisabled_ForcesRelayOff);
    RUN_TEST(test_RelayLogic_SystemDisabled_AlreadyOff_NoAction);
    RUN_TEST(test_RelayLogic_ManualON_OpensRelay);
    RUN_TEST(test_RelayLogic_ManualOFF_ClosesRelay);
    RUN_TEST(test_RelayLogic_ObjectSensitive_OpensAfterDebounce);
    RUN_TEST(test_RelayLogic_ObjectSensitive_ClosesAfterDebounce);
    RUN_TEST(test_RelayLogic_Debounce_ResetOnDirectionChange);
    RUN_TEST(test_RelayLogic_ObjectSensitive_NoActionBetweenSetpoints);
    RUN_TEST(test_RelayLogic_MinSwitchTime_PreventsRapidToggle);
    RUN_TEST(test_RelayLogic_Hysteresis_ClosedRelayNotOpenBelowDeadband);
    RUN_TEST(test_RelayLogic_Hysteresis_OpenRelayNotClosedAboveDeadband);

    /* CPU thermal protection */
    RUN_TEST(test_CpuThermalProtection_AlertForcesRelayOn);
    RUN_TEST(test_CpuThermalProtection_OverrideClearedWhenCpuCools);
    RUN_TEST(test_CpuThermalProtection_DisabledFlag_AlertIgnored);

    /* Stop / cleanup */
    RUN_TEST(test_Stop_UnsubscribesEventBus);

    /* Sensor stale */
    RUN_TEST(test_SensorStale_ReturnsTrueWhenNoReading);
    RUN_TEST(test_SensorStale_ReturnsFalseAfterFreshReading);
    RUN_TEST(test_SensorStale_ZeroTimeoutDisablesDetection);

    /* New getters */
    RUN_TEST(test_Climate_ConfigCannotChangeAfterStart);
    RUN_TEST(test_Climate_ConfigLockFailurePreservesSettings);
    RUN_TEST(test_Climate_SettingsSnapshotFailureDoesNotActuate);
    RUN_TEST(test_GetFanMode_RejectsUnconfirmedConfiguration);
    RUN_TEST(test_GetFanMode_ReturnsObjectSensitive);
    RUN_TEST(test_GetFanMode_ReturnsManualOn);
    RUN_TEST(test_GetFanMode_ReturnsManualOff);
    RUN_TEST(test_GetConfig_ReturnsActiveConfig);
    RUN_TEST(test_GetRelayDebounceState_FalseInitially);
    RUN_TEST(test_GetRelayDebounceState_TrueWhilePending);

    /* Temperature history */
    RUN_TEST(test_History_EmptyOnInit);
    RUN_TEST(test_History_AppendedOnIRUpdate);
    RUN_TEST(test_History_RingBufferWraps);

    /* Menu handler */
    RUN_TEST(test_MenuHandler_SetHighTemp_Increment);
    RUN_TEST(test_MenuHandler_SetLowTemp_Decrement);
    RUN_TEST(test_MenuHandler_CalibrateObject_Increment);
    RUN_TEST(test_MenuHandler_FanMode_ObjectToAmbientDelta);
    RUN_TEST(test_MenuHandler_NoRotation_NoOp);
    RUN_TEST(test_MenuHandler_ButtonAction_Ignored);

    /* Self-test */
    RUN_TEST(test_SelfTest_AllDepsAvailable_Passes);
    RUN_TEST(test_SelfTest_NoRelay_Fails);
    RUN_TEST(test_SelfTest_SentinelTemp_Fails);

    /* Default config */
    RUN_TEST(test_DefaultConfig_UsesSystemConfigConstants);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Entry Points
 * ═══════════════════════════════════════════════════════════════════════════ */

#ifdef RACOOL_TEST_NATIVE
/**
 * @brief Native (gcc) test runner entry point.
 *
 * Used when compiled with run_native_tests.ps1. Runs all test cases and
 * returns 0 on success, non-zero on failure.
 */
void test_MenuHandler_FailedPersistencePreservesEverySetting(void) {
    const UIState states[] = {UI_STATE_SET_LOW_TEMP, UI_STATE_SET_HIGH_TEMP,
        UI_STATE_CALIBRATE_OBJECT, UI_STATE_CALIBRATE_AMBIENT, UI_STATE_FAN_MODE, UI_STATE_SET_TEMP_UNIT};
    SystemSettings before = mock_settings;
    menu_save_result = ESP_FAIL;
    for (unsigned i = 0; i < sizeof(states)/sizeof(states[0]); ++i) {
        MenuPluginEvent event = {.state = states[i], .action = MENU_PLUGIN_ACTION_ENCODER, .direction = 1};
        climateControllerInstance.state = SERVICE_STATE_STARTED;
        if (states[i]==UI_STATE_SET_LOW_TEMP) {
            event.direction=0;Climate_MenuHandlerEx(&event);event.direction=1;Climate_MenuHandlerEx(&event);
            event.action=MENU_PLUGIN_ACTION_BUTTON;
            Climate_MenuHandlerEx(&event);Climate_MenuHandlerEx(&event);
            int16_t low,high;unsigned phase;
            TEST_ASSERT_TRUE(MenuRangeEditor_Read(event.state,&low,&high,&phase));TEST_ASSERT_EQUAL(3,phase);
        } else Climate_MenuHandlerEx(&event);
        TEST_ASSERT_EQUAL_MEMORY(&before, &mock_settings, sizeof(before));
    }
    TEST_ASSERT_EQUAL(6, menu_save_calls);
    TEST_ASSERT_EQUAL(0, mock_eventbus_publish_calls);
}
void test_MenuHandler_TemperatureUnitCyclesThroughPersistentWrite(void) {
    MenuPluginEvent event = {.state = UI_STATE_SET_TEMP_UNIT, .action = MENU_PLUGIN_ACTION_ENCODER, .direction = 1};
    climateControllerInstance.state = SERVICE_STATE_STARTED;
    Climate_MenuHandlerEx(&event); TEST_ASSERT_EQUAL(1, mock_settings.system.temp_unit);
    event.direction = -1;
    climateControllerInstance.state = SERVICE_STATE_STARTED;
    Climate_MenuHandlerEx(&event); TEST_ASSERT_EQUAL(0, mock_settings.system.temp_unit);
    TEST_ASSERT_EQUAL(2, menu_save_calls);
}

static MenuManagerError climate_registration_result;
static unsigned climate_registration_calls;
static MenuManagerError register_climate_menu(IMenuManager *self, const MenuPluginRegistration *entries, size_t count) {
    (void)self;
    climate_registration_calls++;
    TEST_ASSERT_EQUAL(10, count);
    for (size_t i = 0; i < count; ++i) TEST_ASSERT_NOT_NULL(entries[i].modern);
    return climate_registration_result;
}
static MenuManagerError climate_removal_result;
static MenuManagerError unregister_climate_menu(IMenuManager *self, const MenuPluginRegistration *entries, size_t count) {
    (void)self; (void)entries; (void)count; return climate_removal_result;
}
static IMenuManager climate_menu = {.RegisterPluginBatch = register_climate_menu, .UnregisterPluginBatch = unregister_climate_menu};
static IService *climate_menu_service(const char *name) {
    return strcmp(name, SVC_NAME_MENU_MANAGER) == 0 ? (IService *)&climate_menu : NULL;
}
void test_ClimateMenuFailurePreventsSubscriptionsAndWorkerUntilRetry(void) {
    IClimateController *cc = ClimateController_GetModule();
    TEST_ASSERT_EQUAL(0, cc->base.Initialize(&cc->base, NULL));
    mocked_ServiceLocator_GetService_fake.custom_fake = climate_menu_service;
    climate_registration_calls = 0;
    climate_registration_result = MENU_ERR_BUSY;
    TEST_ASSERT_NOT_EQUAL(0, cc->base.Start(&cc->base));
    TEST_ASSERT_EQUAL(SERVICE_STATE_INITIALIZED, cc->base.GetState(&cc->base));
    TEST_ASSERT_EQUAL(0, mocked_xTaskCreate_fake.call_count);
    TEST_ASSERT_EQUAL(0, mock_eventbus_subscribe_calls);
    TEST_ASSERT_EQUAL(1, climate_registration_calls);
    climate_registration_result = MENU_OK;
    TEST_ASSERT_EQUAL(0, cc->base.Start(&cc->base));
    TEST_ASSERT_EQUAL(2, climate_registration_calls);
    TEST_ASSERT_EQUAL(1, mocked_xTaskCreate_fake.call_count);
}


void test_ClimatePartialSubscriptionFailureRollsBackMenuAndPriorSubscriptions(void) {
    IClimateController *cc = ClimateController_GetModule();
    TEST_ASSERT_EQUAL(0, cc->base.Initialize(&cc->base, NULL));
    mocked_ServiceLocator_GetService_fake.custom_fake = climate_menu_service;
    climate_registration_result = MENU_OK; climate_removal_result = MENU_OK;
    subscription_failure_at = 2;
    TEST_ASSERT_NOT_EQUAL(0, cc->base.Start(&cc->base));
    TEST_ASSERT_EQUAL(SERVICE_STATE_INITIALIZED, cc->base.GetState(&cc->base));
    TEST_ASSERT_NULL(climateControllerInstance.ownedMenu);
    TEST_ASSERT_EQUAL(0, climateControllerInstance.subscriptions);
    TEST_ASSERT_EQUAL(1, mock_eventbus_unsubscribe_calls);
    TEST_ASSERT_EQUAL(0, mocked_xTaskCreate_fake.call_count);
    subscription_failure_at = 0;
    TEST_ASSERT_EQUAL(0, cc->base.Start(&cc->base));
}

void test_ClimateStopBusyKeepsOwnersAndRejectsLateCallbacksUntilRetry(void) {
    IClimateController *cc = ClimateController_GetModule();
    TEST_ASSERT_EQUAL(0, cc->base.Initialize(&cc->base, NULL));
    mocked_ServiceLocator_GetService_fake.custom_fake = climate_menu_service;
    climate_registration_result = MENU_OK; climate_removal_result = MENU_OK;
    TEST_ASSERT_EQUAL(0, cc->base.Start(&cc->base));
    climate_removal_result = MENU_ERR_BUSY;
    TEST_ASSERT_NOT_EQUAL(0, cc->base.Stop(&cc->base));
    TEST_ASSERT_EQUAL_PTR(&climate_menu, climateControllerInstance.ownedMenu);
    TEST_ASSERT_NOT_EQUAL(0, cc->base.Start(&cc->base));
    climate_removal_result = MENU_OK;
    unsubscribe_result = EVENT_BUS_ERR_BUSY;
    TEST_ASSERT_NOT_EQUAL(0, cc->base.Stop(&cc->base));
    TEST_ASSERT_NULL(climateControllerInstance.ownedMenu);
    TEST_ASSERT_NOT_EQUAL(0, climateControllerInstance.subscriptions);
    float cpu = 99;
    Climate_OnInternalTempUpdated((BusEvent){.data=&cpu,.dataSize=sizeof(cpu)});
    TEST_ASSERT_FLOAT_WITHIN(0.001f, -127, cc->GetCurrentCpuTemperature(cc));
    MenuPluginEvent event = {.state=UI_STATE_SET_TEMP_UNIT,.action=MENU_PLUGIN_ACTION_ENCODER,.direction=1};
    Climate_MenuHandlerEx(&event);
    TEST_ASSERT_EQUAL(0, menu_save_calls);
    unsubscribe_result = 0;
    TEST_ASSERT_EQUAL(0, cc->base.Stop(&cc->base));
    TEST_ASSERT_EQUAL(0, climateControllerInstance.subscriptions);
    TEST_ASSERT_NULL(climateControllerInstance.ownedBus);
    TEST_ASSERT_EQUAL(0, cc->base.Start(&cc->base));
}

static void prepare_stale_policy(void) {
    IClimateController *cc = ClimateController_GetModule();
    TEST_ASSERT_EQUAL(0, cc->base.Initialize(&cc->base, NULL));
    inject_mocks();
    climateControllerInstance.config = ClimateController_GetDefaultConfig();
    climateControllerInstance.config.sensorStaleTimeoutMs = 100;
    climateControllerInstance.config.relayDebounceMs = 50;
    climateControllerInstance.config.relayMinSwitchMs = 0;
    climateControllerInstance.hasSensorSample = true;
    climateControllerInstance.lastSensorReadTick = 0;
    climateControllerInstance.objectTemp = 40;
}

void test_ClimateStaleCancelsPendingAndFreshSampleRestartsDebounce(void) {
    prepare_stale_policy();
    Climate_EvaluateRelay(&mock_settings,90);
    TEST_ASSERT_TRUE(climateControllerInstance.rsm.isConditionPending);
    Climate_EvaluateRelay(&mock_settings,100);
    TEST_ASSERT_FALSE(climateControllerInstance.rsm.isConditionPending);
    TEST_ASSERT_TRUE(mock_relay_is_open_val); TEST_ASSERT_EQUAL(1,mock_relay_open_calls);
    Climate_EvaluateRelay(&mock_settings,1000);
    TEST_ASSERT_EQUAL(1,mock_relay_open_calls); /* No repeated ON actuation. */
    IRTempSample fresh={.temperatures={20,22},.measured_tick=1000,.measured_time_us=1000000};
    Climate_UpdateIRSample(&fresh);
    Climate_EvaluateRelay(&mock_settings,1000);
    TEST_ASSERT_TRUE(climateControllerInstance.rsm.isConditionPending);
    TEST_ASSERT_EQUAL(0,mock_relay_close_calls);
    Climate_EvaluateRelay(&mock_settings,1050);
    TEST_ASSERT_EQUAL(1,mock_relay_close_calls); /* Fresh normal hysteresis resumes. */

}

void test_ClimateStalePreservesOpenFanAndMissingMeasurementCancelsPending(void) {
    prepare_stale_policy();
    mock_relay_is_open_val = true;
    climateControllerInstance.objectTemp = 20;
    climateControllerInstance.rsm.isConditionPending = true;
    Climate_EvaluateRelay(&mock_settings, 100);
    TEST_ASSERT_TRUE(mock_relay_is_open_val);
    TEST_ASSERT_EQUAL(0, mock_relay_close_calls);
    TEST_ASSERT_FALSE(climateControllerInstance.rsm.isConditionPending);
    climateControllerInstance.hasSensorSample = false;
    climateControllerInstance.config.sensorStaleTimeoutMs = 0;
    climateControllerInstance.rsm.isConditionPending = true;
    Climate_EvaluateRelay(&mock_settings, 200);
    TEST_ASSERT_FALSE(climateControllerInstance.rsm.isConditionPending);
    TEST_ASSERT_TRUE(mock_relay_is_open_val);
}

void test_ClimateFreshArrivalAfterGapCannotCompleteOldDebounce(void) {
    prepare_stale_policy();
    Climate_EvaluateRelay(&mock_settings, 90);
    TEST_ASSERT_TRUE(climateControllerInstance.rsm.isConditionPending);
    /* The callback may receive fresh data before the next stale check runs. */
    IRTempSample fresh = {.temperatures={40,22}, .measured_tick=200, .measured_time_us=200000};
    Climate_UpdateIRSample(&fresh);
    Climate_EvaluateRelay(&mock_settings, 200);
    TEST_ASSERT_EQUAL(0, mock_relay_open_calls);
    TEST_ASSERT_TRUE(climateControllerInstance.rsm.isConditionPending);
    TEST_ASSERT_EQUAL_UINT32(200, climateControllerInstance.rsm.conditionMetStartTime);
    Climate_EvaluateRelay(&mock_settings, 250);
    TEST_ASSERT_EQUAL(1, mock_relay_open_calls);
}

void test_ClimateSnapshotDelayUsesCurrentAgeBeforeRelayDecision(void) {
    prepare_stale_policy();
    mocked_xTaskGetTickCount_fake.return_val = 90;
    climateControllerInstance.rsm.isConditionPending = true;
    climateControllerInstance.rsm.pendingActionOpen = true;
    climateControllerInstance.rsm.conditionMetStartTime = 0;
    snapshot_advances_clock = true;
    TEST_ASSERT_TRUE(Climate_EvaluateSettingsSnapshot());
    TEST_ASSERT_EQUAL(1, mock_relay_open_calls);
    TEST_ASSERT_FALSE(climateControllerInstance.rsm.isConditionPending);
}

void test_ClimateStaleDeadlineWrapAndDisabledTimeout(void) {
    prepare_stale_policy();
    climateControllerInstance.lastSensorReadTick = UINT32_MAX - 49;
    Climate_EvaluateRelay(&mock_settings, 49);
    TEST_ASSERT_TRUE(climateControllerInstance.rsm.isConditionPending);
    Climate_EvaluateRelay(&mock_settings, 50);
    TEST_ASSERT_FALSE(climateControllerInstance.rsm.isConditionPending);
    TEST_ASSERT_EQUAL(1, mock_relay_open_calls);
    climateControllerInstance.config.sensorStaleTimeoutMs = 0;
    Climate_EvaluateRelay(&mock_settings, 1000);
    Climate_EvaluateRelay(&mock_settings, 1050);
    TEST_ASSERT_EQUAL(1, mock_relay_open_calls);
}

void test_ClimateStaleDoesNotOverrideManualOrCpuSafety(void) {
    prepare_stale_policy();
    climateControllerInstance.hasSensorSample = false;
    mock_settings.control.fan_manual_mode = true;
    mock_settings.control.fan_manual_state = true;
    Climate_EvaluateRelay(&mock_settings, 1000);
    TEST_ASSERT_TRUE(mock_relay_is_open_val);
    mock_settings.control.fan_manual_state = false;
    Climate_EvaluateRelay(&mock_settings, 1001);
    TEST_ASSERT_FALSE(mock_relay_is_open_val);
    climateControllerInstance.cpuThermalOverride = true;
    Climate_EvaluateRelay(&mock_settings, 1002);
    TEST_ASSERT_TRUE(mock_relay_is_open_val);
}


void test_ClimateRegistration_FailureDoesNotCreateWorkerOrPublishStarted(void) {
    IClimateController *controller = ClimateController_GetModule();
    TEST_ASSERT_EQUAL(0, controller->base.Initialize(&controller->base, NULL));
    mocked_ServiceLocator_GetService_fake.custom_fake = NULL;
    mocked_ServiceLocator_GetService_fake.return_val = (IService *)1;
    climate_registration_result = ESP_ERR_NO_MEM;
    unsigned tasks = mocked_xTaskCreate_fake.call_count;
    TEST_ASSERT_EQUAL(ESP_ERR_NO_MEM, controller->base.Start(&controller->base));
    TEST_ASSERT_EQUAL(tasks, mocked_xTaskCreate_fake.call_count);
    TEST_ASSERT_EQUAL(SERVICE_STATE_INITIALIZED, controller->base.GetState(&controller->base));
}

void test_ClimateCpuOverrideDistinguishesManualFanOnAndRecovery(void) {
    IClimateController *controller = ClimateController_GetModule();
    climateControllerInstance.config.cpuTempProtectionEnabled = true;
    climateControllerInstance.cpuThermalOverride = false;
    TEST_ASSERT_FALSE(controller->IsCpuThermalOverrideActive(controller));
    climateControllerInstance.cpuThermalOverride = true;
    TEST_ASSERT_TRUE(controller->IsCpuThermalOverrideActive(controller));
    climateControllerInstance.config.cpuTempProtectionEnabled = false;
    TEST_ASSERT_FALSE(controller->IsCpuThermalOverrideActive(controller));
    climateControllerInstance.config.cpuTempProtectionEnabled = true;
    climateControllerInstance.cpuThermalOverride = false;
    TEST_ASSERT_FALSE(controller->IsCpuThermalOverrideActive(controller));
}

void test_ClimateSourcesExternalInjectionLifecycleAndFallback(void) {
    IClimateController *controller=ClimateController_GetModule();
    TEST_ASSERT_EQUAL(0,controller->base.Initialize(&controller->base,NULL));inject_mocks();
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE,controller->SetExternalTemperature(controller,1,20));
    climateControllerInstance.state=SERVICE_STATE_STARTED;
    mocked_xTaskGetTickCount_fake.return_val=100;
    TEST_ASSERT_EQUAL(ESP_OK,controller->SetExternalTemperature(controller,1,20));
    TEST_ASSERT_EQUAL(ESP_OK,controller->SetExternalTemperature(controller,2,23));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,controller->SetExternalTemperature(controller,0,20));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,controller->SetExternalTemperature(controller,1,NAN));
    mock_settings.control.object_temperature_source=2;mock_settings.control.ambient_temperature_source=1;
    TemperatureSourceSnapshot snapshot;
    TEST_ASSERT_EQUAL(ESP_OK,controller->GetTemperatureSources(controller,&snapshot));
    TEST_ASSERT_EQUAL(2,snapshot.object.effective_source);TEST_ASSERT_EQUAL(1,snapshot.ambient.effective_source);
    TEST_ASSERT_FLOAT_WITHIN(.001,3,snapshot.temperature_difference);
    mocked_xTaskGetTickCount_fake.return_val=100+EXTERNAL_TEMPERATURE_STALE_MS;
    TEST_ASSERT_EQUAL(ESP_OK,controller->GetTemperatureSources(controller,&snapshot));
    TEST_ASSERT_FALSE(snapshot.object.valid);TEST_ASSERT_EQUAL(TEMPERATURE_STALE,snapshot.object.configured_state);
    atomic_store(&climateControllerInstance.worker.stopRequested,true);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE,controller->SetExternalTemperature(controller,1,20));
    atomic_store(&climateControllerInstance.worker.stopRequested,false);
}
void test_ClimateMenuRejectsInvalidDeltaAndSourceUsesSharedCommands(void) {
    climateControllerInstance.state=SERVICE_STATE_STARTED;
    MenuPluginEvent event={.state=UI_STATE_FAN_DELTA_OFF,.action=MENU_PLUGIN_ACTION_ENCODER,.direction=1};
    mock_settings.control.fan_delta_off_decic=19;
    Climate_MenuHandlerEx(&event);
    TEST_ASSERT_EQUAL(19,mock_settings.control.fan_delta_off_decic);
    event.state=UI_STATE_OBJECT_SOURCE;
    Climate_MenuHandlerEx(&event);TEST_ASSERT_EQUAL(1,mock_settings.control.object_temperature_source);
    event.state=UI_STATE_AMBIENT_SOURCE;event.direction=-1;
    Climate_MenuHandlerEx(&event);TEST_ASSERT_EQUAL(3,mock_settings.control.ambient_temperature_source);
}
void test_ClimateAmbientDeltaWorkerUsesExternalPairAndFreshRecovery(void) {
    prepare_stale_policy();
    mock_settings.control.fan_strategy=1;mock_settings.control.object_temperature_source=2;mock_settings.control.ambient_temperature_source=1;
    climateControllerInstance.externalSlots[0]=(ExternalTemperatureSlot){20,true,1000};
    climateControllerInstance.externalSlots[1]=(ExternalTemperatureSlot){22,true,1000};
    Climate_EvaluateRelay(&mock_settings,1000);TEST_ASSERT_TRUE(climateControllerInstance.rsm.isConditionPending);
    Climate_EvaluateRelay(&mock_settings,1050);TEST_ASSERT_TRUE(mock_relay_is_open_val);
    climateControllerInstance.externalSlots[1].value=21.1f;
    Climate_EvaluateRelay(&mock_settings,1100);TEST_ASSERT_TRUE(mock_relay_is_open_val);
    climateControllerInstance.externalSlots[1].value=21;
    Climate_EvaluateRelay(&mock_settings,1150);Climate_EvaluateRelay(&mock_settings,1200);
    TEST_ASSERT_FALSE(mock_relay_is_open_val);
    Climate_EvaluateRelay(&mock_settings,1000+EXTERNAL_TEMPERATURE_STALE_MS);
    TEST_ASSERT_TRUE(mock_relay_is_open_val);
}

void test_ClimateActuatorFailureCannotPublishAppliedState(void) {
    prepare_stale_policy();climateControllerInstance.hasSensorSample=false;
    relay_open_result=ESP_FAIL;unsigned events=mock_eventbus_publish_calls;
    Climate_EvaluateRelay(&mock_settings,1000);
    TEST_ASSERT_FALSE(mock_relay_is_open_val);TEST_ASSERT_EQUAL(events,mock_eventbus_publish_calls);
    relay_open_result=ESP_OK;Climate_EvaluateRelay(&mock_settings,1001);
    TEST_ASSERT_TRUE(mock_relay_is_open_val);TEST_ASSERT_EQUAL(events+1,mock_eventbus_publish_calls);
}
void test_ClimateTimedActionHasCopiedAdmissionCheckedCompletionAndCpuPriority(void) {
    IClimateController *controller=ClimateController_GetModule();
    TEST_ASSERT_EQUAL(0,controller->base.Initialize(&controller->base,NULL));inject_mocks();
    climateControllerInstance.state=SERVICE_STATE_STARTED;
    climateControllerInstance.config=ClimateController_GetDefaultConfig();
    mocked_esp_timer_get_time_fake.return_val=0;
    TEST_ASSERT_EQUAL(ESP_OK,controller->RequestFanAction(controller,FAN_ACTION_PULSE,500));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE,controller->RequestFanAction(controller,FAN_ACTION_PULSE,500));
    Climate_EvaluateRelay(&mock_settings,0);TEST_ASSERT_TRUE(mock_relay_is_open_val);
    FanActionStatus status;TEST_ASSERT_EQUAL(ESP_OK,controller->GetFanActionStatus(controller,&status));
    TEST_ASSERT_FALSE(status.pending);TEST_ASSERT_TRUE(status.active);TEST_ASSERT_EQUAL(ESP_ERR_NOT_FINISHED,status.result);
    mocked_esp_timer_get_time_fake.return_val=500000;
    Climate_EvaluateRelay(&mock_settings,500);TEST_ASSERT_FALSE(mock_relay_is_open_val);
    controller->GetFanActionStatus(controller,&status);TEST_ASSERT_FALSE(status.active);TEST_ASSERT_EQUAL(ESP_OK,status.result);
    TEST_ASSERT_EQUAL(ESP_OK,controller->RequestFanAction(controller,FAN_ACTION_SELF_TEST,0));
    Climate_EvaluateRelay(&mock_settings,501);TEST_ASSERT_TRUE(mock_relay_is_open_val);
    climateControllerInstance.cpuThermalOverride=true;
    mocked_esp_timer_get_time_fake.return_val=1000000;Climate_EvaluateRelay(&mock_settings,1000);
    TEST_ASSERT_TRUE(mock_relay_is_open_val);controller->GetFanActionStatus(controller,&status);
    TEST_ASSERT_FALSE(status.active);TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE,status.result);
    TEST_ASSERT_EQUAL(CLIMATE_FAN_OBJECT_SENSITIVE,controller->GetFanMode(controller));
    TEST_ASSERT_TRUE(controller->IsCpuThermalOverrideActive(controller));
}
void test_ClimateExternalClearRestoresEmptyWithoutMeasurementAndRejectsClosing(void) {
    IClimateController *controller=ClimateController_GetModule();
    TEST_ASSERT_EQUAL(0,controller->base.Initialize(&controller->base,NULL));inject_mocks();
    climateControllerInstance.state=SERVICE_STATE_STARTED;
    TEST_ASSERT_EQUAL(ESP_OK,controller->SetExternalTemperature(controller,1,21.7f));
    TEST_ASSERT_EQUAL(ESP_OK,controller->ClearExternalTemperature(controller,1));
    TEST_ASSERT_FALSE(climateControllerInstance.externalSlots[0].valid);
    TEST_ASSERT_EQUAL(0,climateControllerInstance.externalSlots[0].last_update_ms);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,controller->ClearExternalTemperature(controller,4));
    atomic_store(&climateControllerInstance.worker.stopRequested,true);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE,controller->ClearExternalTemperature(controller,1));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE,controller->RequestFanAction(controller,FAN_ACTION_PULSE,500));
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0); /* unbuffered — crash diagnosis */
    UNITY_BEGIN();
    RUN_TEST(test_ClimateStaleCancelsPendingAndFreshSampleRestartsDebounce);
    RUN_TEST(test_ClimateStalePreservesOpenFanAndMissingMeasurementCancelsPending);
    RUN_TEST(test_ClimateFreshArrivalAfterGapCannotCompleteOldDebounce);
    RUN_TEST(test_ClimateSnapshotDelayUsesCurrentAgeBeforeRelayDecision);
    RUN_TEST(test_ClimateStaleDeadlineWrapAndDisabledTimeout);
    RUN_TEST(test_ClimateStaleDoesNotOverrideManualOrCpuSafety);
    RUN_TEST(test_ClimateMenuFailurePreventsSubscriptionsAndWorkerUntilRetry);
    RUN_TEST(test_ClimatePartialSubscriptionFailureRollsBackMenuAndPriorSubscriptions);
    RUN_TEST(test_ClimateStopBusyKeepsOwnersAndRejectsLateCallbacksUntilRetry);
    test_ClimateController_All();
    RUN_TEST(test_ClimateRegistration_FailureDoesNotCreateWorkerOrPublishStarted);
    RUN_TEST(test_ClimateCpuOverrideDistinguishesManualFanOnAndRecovery);
    RUN_TEST(test_ClimateSourcesExternalInjectionLifecycleAndFallback);
    RUN_TEST(test_ClimateMenuRejectsInvalidDeltaAndSourceUsesSharedCommands);
    RUN_TEST(test_ClimateAmbientDeltaWorkerUsesExternalPairAndFreshRecovery);
    RUN_TEST(test_ClimateActuatorFailureCannotPublishAppliedState);
    RUN_TEST(test_ClimateTimedActionHasCopiedAdmissionCheckedCompletionAndCpuPriority);
    RUN_TEST(test_ClimateExternalClearRestoresEmptyWithoutMeasurementAndRejectsClosing);
    return UNITY_END();
}

#else
/**
 * @brief PlatformIO on-device (ESP32) test runner entry point.
 *
 * Called by the Unity test framework when flashed to hardware.
 */
void app_main(void) {
    UNITY_BEGIN();
    test_ClimateController_All();
    UNITY_END();
}
#endif /* RACOOL_TEST_NATIVE */
