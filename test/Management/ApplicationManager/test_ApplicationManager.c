/**
 * @file test_ApplicationManager.c
 * @brief Native unit test suite for ApplicationManager facade and event queue.
 *
 * Tests cover singleton pattern, lifecycle state transitions, event posting with
 * type-safe payloads, invalid-arg validation, status retrieval, and factory-reset
 * event dispatch – all running against FFF mocks on the native host.
 *
 * @author RaCool Team
 * @date 2026
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "unity.h"

#include "Management/ApplicationManager/ApplicationManagerConfig.h"
#include "Management/ApplicationManager/IApplicationManager.h"
#include "Management/ApplicationManager/ApplicationManager.c"
#define TAG TAG_AppConsoleTest
#include "Management/ApplicationManager/ApplicationManagerConsoleCommands.c"
#undef TAG

/* -------------------------------------------------------------------------
 * setUp / tearDown
 * The ApplicationManager is a module-level singleton with a static guard flag;
 * the fixture resets private state while retaining the published interface.
 * ------------------------------------------------------------------------- */
static BaseType_t create_app_task(TaskFunction_t fn, const char *name, uint32_t stack, void *arg,
                                  uint32_t priority, TaskHandle_t *handle) {
    (void)fn; (void)name; (void)stack; (void)arg; (void)priority;
    *handle = (TaskHandle_t)3; return pdPASS;
}

static void reset_app_instance(void) {
    IApplicationManager base = *ApplicationManager_GetModule();
    memset(&appManager, 0, sizeof(appManager));
    appManager.base = base;
}

static int app_sub_calls, app_unsub_calls, app_sub_fail_at, app_unsub_fail_at;
static int32_t subscribe_app(IEventBus *bus, const char *name, BusEventHandler handler) {
    (void)bus; (void)name; (void)handler;
    return ++app_sub_calls == app_sub_fail_at ? -1 : 0;
}
static int32_t unsubscribe_app(IEventBus *bus, const char *name, BusEventHandler handler) {
    (void)bus; (void)name; (void)handler;
    return ++app_unsub_calls == app_unsub_fail_at ? EVENT_BUS_ERR_BUSY : 0;
}
static IEventBus app_bus = {.Subscribe = subscribe_app, .Unsubscribe = unsubscribe_app};

void setUp(void) {
    reset_app_instance();
    app_sub_calls = app_unsub_calls = app_sub_fail_at = app_unsub_fail_at = 0;
    RESET_FAKE(mocked_ServiceLocator_GetService);
    RESET_FAKE(mocked_ServiceLocator_GetEventBus);
    mocked_ServiceLocator_GetEventBus_fake.return_val = &app_bus;
    RESET_FAKE(mocked_xTaskCreate); RESET_FAKE(mocked_xQueueCreate);
    RESET_FAKE(mocked_xQueueReceive); RESET_FAKE(mocked_xQueueSend);
    RESET_FAKE(mocked_xSemaphoreCreateMutex); RESET_FAKE(mocked_xSemaphoreTake);
    RESET_FAKE(mocked_vTaskDelete); RESET_FAKE(mocked_vTaskSuspend);
    RESET_FAKE(mocked_xTaskGetCurrentTaskHandle);
    mocked_xTaskGetCurrentTaskHandle_fake.return_val = (TaskHandle_t)4;
    mocked_xTaskCreate_fake.custom_fake = create_app_task;
    /* xSemaphoreCreateMutex / xQueueCreate must return non-NULL to succeed. */
    mocked_xSemaphoreCreateMutex_fake.return_val = (SemaphoreHandle_t)1;
    mocked_xQueueCreate_fake.return_val          = (QueueHandle_t)1;

    /* xSemaphoreTake must succeed (pdTRUE = 1) for GetStatus / PostEvent. */
    mocked_xSemaphoreTake_fake.return_val = pdTRUE;
    mocked_xSemaphoreGive_fake.return_val = pdTRUE;

    /* xQueueSend must succeed (pdPASS = 1) so PostEvent returns ESP_OK. */
    mocked_xQueueSend_fake.return_val = pdPASS;

    /* uxQueueMessagesWaiting – return 0 (empty queue) by default. */
    mocked_uxQueueMessagesWaiting_fake.return_val = 0;
    TEST_ASSERT_EQUAL(0, App_Initialize((IService *)&appManager.base, NULL));
    TEST_ASSERT_EQUAL(0, App_Start((IService *)&appManager.base));
}

void tearDown(void) {}

/* =========================================================================
 * Tests
 * ========================================================================= */

/**
 * @brief Verifies that GetModule always returns the same singleton pointer.
 */
void test_ApplicationManager_GetModule_Singleton(void) {
    IApplicationManager *mgr1 = ApplicationManager_GetModule();
    IApplicationManager *mgr2 = ApplicationManager_GetModule();

    TEST_ASSERT_NOT_NULL(mgr1);
    TEST_ASSERT_EQUAL_PTR(mgr1, mgr2);
}

/**
 * @brief After module retrieval the state must be SERVICE_STATE_UNINITIALIZED.
 */
void test_ApplicationManager_InitialState(void) {
    IApplicationManager *mgr = ApplicationManager_GetModule();
    TEST_ASSERT_NOT_NULL(mgr);

    reset_app_instance();
    TEST_ASSERT_EQUAL(SERVICE_STATE_UNINITIALIZED, mgr->base.GetState((IService *)mgr));
}

/**
 * @brief Initialize + Start transitions must succeed and advance state correctly.
 */
void test_ApplicationManager_Initialize_And_Start(void) {
    reset_app_instance();
    IApplicationManager *mgr = ApplicationManager_GetModule();
    TEST_ASSERT_NOT_NULL(mgr);

    int32_t initRes = mgr->base.Initialize((IService *)mgr, NULL);
    TEST_ASSERT_EQUAL_INT32(0, initRes);
    TEST_ASSERT_TRUE(mgr->base.GetState((IService *)mgr) >= SERVICE_STATE_INITIALIZED);

    int32_t startRes = mgr->base.Start((IService *)mgr);
    TEST_ASSERT_EQUAL_INT32(0, startRes);
    TEST_ASSERT_EQUAL(SERVICE_STATE_STARTED, mgr->base.GetState((IService *)mgr));
}

/**
 * @brief GetStatus must return ESP_OK and populate all status fields.
 */
void test_ApplicationManager_GetStatus(void) {
    IApplicationManager *mgr = ApplicationManager_GetModule();
    TEST_ASSERT_NOT_NULL(mgr);

    AppManagerStatus status = {0};
    esp_err_t err = mgr->GetStatus(mgr, &status);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_EQUAL(SERVICE_STATE_STARTED, status.state);
    TEST_ASSERT_EQUAL_UINT32(APP_EVENT_QUEUE_SIZE, status.queueCapacity);
}

/**
 * @brief GetStatus must reject a NULL output pointer.
 */
void test_ApplicationManager_GetStatus_NullArg(void) {
    IApplicationManager *mgr = ApplicationManager_GetModule();
    TEST_ASSERT_NOT_NULL(mgr);

    esp_err_t err = mgr->GetStatus(mgr, NULL);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, err);
}

/**
 * @brief All concrete event types must be accepted and return ESP_OK.
 */
void test_ApplicationManager_PostEvent_Types(void) {
    IApplicationManager *mgr = ApplicationManager_GetModule();
    TEST_ASSERT_NOT_NULL(mgr);

    /* Rotation event */
    AppManagerEvent rotEvt = {
        .id = APP_EVENT_ROTATION,
        .payload = { .rawValue = 5 },
        .timestamp_ms = 1000
    };
    TEST_ASSERT_EQUAL(ESP_OK, mgr->PostEvent(mgr, &rotEvt));

    /* Button click event */
    AppManagerEvent clickEvt = {
        .id = APP_EVENT_BUTTON_CLICK,
        .payload = { .rawValue = 1 },
        .timestamp_ms = 1001
    };
    TEST_ASSERT_EQUAL(ESP_OK, mgr->PostEvent(mgr, &clickEvt));

    /* Long press event */
    AppManagerEvent lpEvt = {
        .id = APP_EVENT_BUTTON_LONGPRESS,
        .payload = { .durationMs = 3000 },
        .timestamp_ms = 1002
    };
    TEST_ASSERT_EQUAL(ESP_OK, mgr->PostEvent(mgr, &lpEvt));

    /* Set target temperature event */
    AppManagerEvent tempEvt = {
        .id = APP_EVENT_SET_TARGET_TEMP,
        .payload = { .targetTemperature = 22.5f },
        .timestamp_ms = 1005
    };
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_SUPPORTED, mgr->PostEvent(mgr, &tempEvt));

    /* Relay toggle event */
    AppManagerEvent relayEvt = {
        .id = APP_EVENT_TOGGLE_RELAY,
        .payload = { .relayState = true },
        .timestamp_ms = 1010
    };
    TEST_ASSERT_EQUAL(ESP_OK, mgr->PostEvent(mgr, &relayEvt));

    /* Screen wake event */
    AppManagerEvent wakeEvt = {
        .id = APP_EVENT_SCREEN_WAKE,
        .payload = { .rawValue = 0 },
        .timestamp_ms = 1015
    };
    TEST_ASSERT_EQUAL(ESP_OK, mgr->PostEvent(mgr, &wakeEvt));

    /* Telemetry trigger event */
    AppManagerEvent telEvt = {
        .id = APP_EVENT_TRIGGER_TELEMETRY,
        .payload = { .rawValue = 0 },
        .timestamp_ms = 1020
    };
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_SUPPORTED, mgr->PostEvent(mgr, &telEvt));
}

/**
 * @brief NULL pointer and APP_EVENT_NONE must be rejected with ESP_ERR_INVALID_ARG.
 */
void test_ApplicationManager_PostEvent_InvalidArg(void) {
    IApplicationManager *mgr = ApplicationManager_GetModule();
    TEST_ASSERT_NOT_NULL(mgr);

    /* NULL event pointer */
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, mgr->PostEvent(mgr, NULL));

    /* APP_EVENT_NONE is explicitly invalid */
    AppManagerEvent noneEvt = { .id = APP_EVENT_NONE };
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, mgr->PostEvent(mgr, &noneEvt));
}

/**
 * @brief A full queue (xQueueSend returns pdFAIL) must yield ESP_ERR_TIMEOUT
 *        and increment the dropped-events counter.
 */
void test_ApplicationManager_PostEvent_QueueFull(void) {
    IApplicationManager *mgr = ApplicationManager_GetModule();
    TEST_ASSERT_NOT_NULL(mgr);

    /* Simulate full queue */
    mocked_xQueueSend_fake.return_val = pdFAIL;

    AppManagerStatus before = {0};
    mgr->GetStatus(mgr, &before);

    AppManagerEvent evt = {
        .id = APP_EVENT_ROTATION,
        .payload = { .rawValue = 1 },
        .timestamp_ms = 9000
    };
    esp_err_t err = mgr->PostEvent(mgr, &evt);
    TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT, err);

    AppManagerStatus after = {0};
    mgr->GetStatus(mgr, &after);
    TEST_ASSERT_EQUAL_UINT32(before.droppedEventsCount + 1, after.droppedEventsCount);

    /* Restore for other tests */
    mocked_xQueueSend_fake.return_val = pdPASS;
}

/**
 * @brief TriggerFactoryReset must dispatch APP_EVENT_FACTORY_RESET and return ESP_OK.
 */
void test_ApplicationManager_TriggerFactoryReset(void) {
    IApplicationManager *mgr = ApplicationManager_GetModule();
    TEST_ASSERT_NOT_NULL(mgr);

    esp_err_t err = mgr->TriggerFactoryReset(mgr);
    TEST_ASSERT_EQUAL(ESP_OK, err);
}

/**
 * @brief CheckHealth must return 0 while the service is in STARTED state.
 */
void test_ApplicationManager_CheckHealth_WhenStarted(void) {
    IApplicationManager *mgr = ApplicationManager_GetModule();
    TEST_ASSERT_NOT_NULL(mgr);

    int32_t health = mgr->base.CheckHealth((IService *)mgr);
    TEST_ASSERT_EQUAL_INT32(0, health);
}

/* =========================================================================
 * Test runner
 * ========================================================================= */

static int32_t app_ack;
static bool app_ack_ready;
static BaseType_t app_send_ack(QueueHandle_t queue, const void *value, TickType_t wait) {
    (void)queue; (void)wait; app_ack = *(const int32_t *)value; app_ack_ready = true; return pdTRUE;
}
static BaseType_t app_receive_ack(QueueHandle_t queue, void *value, TickType_t wait) {
    (void)queue;
    if (!wait) return pdFALSE; /* Event queue drain, not the control acknowledgment. */
    if (!app_ack_ready) {
        mocked_xTaskGetCurrentTaskHandle_fake.return_val = (TaskHandle_t)3;
        WatchdogWorker_Checkpoint(&appManager.worker, NULL, 0);
        mocked_xTaskGetCurrentTaskHandle_fake.return_val = (TaskHandle_t)4;
    }
    *(int32_t *)value = app_ack; app_ack_ready = false; return pdTRUE;
}
void test_ApplicationManager_StopTimeoutAndRestart(void) {
    IApplicationManager *mgr = ApplicationManager_GetModule();
    mocked_xQueueReceive_fake.return_val = pdFALSE;
    TEST_ASSERT_EQUAL(-1, mgr->base.Stop((IService *)mgr));
    TEST_ASSERT_EQUAL(0, mocked_vTaskDelete_fake.call_count);
    TEST_ASSERT_EQUAL(SERVICE_STATE_ERROR, mgr->base.GetState((IService *)mgr));
    TEST_ASSERT_EQUAL(-1, mgr->base.CheckHealth((IService *)mgr));
    app_ack_ready = false;
    mocked_xQueueSend_fake.custom_fake = app_send_ack;
    mocked_xQueueReceive_fake.custom_fake = app_receive_ack;
    TEST_ASSERT_EQUAL(0, mgr->base.Stop((IService *)mgr));
    TEST_ASSERT_EQUAL(1, mocked_vTaskDelete_fake.call_count);
    TEST_ASSERT_EQUAL(SERVICE_STATE_STOPPED, mgr->base.GetState((IService *)mgr));
    AppManagerEvent event = {.id = APP_EVENT_ROTATION};
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, mgr->PostEvent(mgr, &event));
    TEST_ASSERT_EQUAL(0, mgr->base.Start((IService *)mgr));
    TEST_ASSERT_EQUAL(0, mgr->base.CheckHealth((IService *)mgr));
}
void test_ApplicationManager_AllocationAndLaunchFailures(void) {
    reset_app_instance();
    IApplicationManager *mgr = ApplicationManager_GetModule();
    mocked_xSemaphoreCreateMutex_fake.return_val = NULL;
    TEST_ASSERT_EQUAL(-1, mgr->base.Initialize((IService *)mgr, NULL));
    TEST_ASSERT_EQUAL(SERVICE_STATE_UNINITIALIZED, mgr->base.GetState((IService *)mgr));
    TEST_ASSERT_NULL(appManager.mutex);
    TEST_ASSERT_NULL(appManager.eventQueue);
    mocked_xSemaphoreCreateMutex_fake.return_val = (SemaphoreHandle_t)1;
    TEST_ASSERT_EQUAL(0, mgr->base.Initialize((IService *)mgr, NULL));
    mocked_xTaskCreate_fake.custom_fake = NULL;
    mocked_xTaskCreate_fake.return_val = pdFAIL;
    TEST_ASSERT_EQUAL(-1, mgr->base.Start((IService *)mgr));
    TEST_ASSERT_EQUAL(SERVICE_STATE_INITIALIZED, mgr->base.GetState((IService *)mgr));
    TEST_ASSERT_NULL(atomic_load(&appManager.worker.task));
}

void test_ApplicationManager_SubscriptionFailureRollsBack(void) {
    reset_app_instance();
    IApplicationManager *mgr = ApplicationManager_GetModule();
    TEST_ASSERT_EQUAL(0, mgr->base.Initialize(&mgr->base, NULL));
    app_sub_calls = app_unsub_calls = 0;
    app_sub_fail_at = 2;
    TEST_ASSERT_NOT_EQUAL(0, mgr->base.Start(&mgr->base));
    TEST_ASSERT_EQUAL(1, app_unsub_calls);
    TEST_ASSERT_NULL(atomic_load(&appManager.worker.task));
    app_sub_fail_at = 0;
    TEST_ASSERT_EQUAL(0, mgr->base.Start(&mgr->base));
}
void test_ApplicationManager_UnsubscribeBusyRequiresRetry(void) {
    IApplicationManager *mgr = ApplicationManager_GetModule();
    app_ack_ready = false;
    mocked_xQueueSend_fake.custom_fake = app_send_ack;
    mocked_xQueueReceive_fake.custom_fake = app_receive_ack;
    app_unsub_fail_at = 2;
    TEST_ASSERT_NOT_EQUAL(0, mgr->base.Stop(&mgr->base));
    TEST_ASSERT_NOT_EQUAL(SERVICE_STATE_STOPPED, mgr->base.GetState(&mgr->base));
    TEST_ASSERT_NOT_EQUAL(0, mgr->base.Start(&mgr->base));
    AppManagerEvent event = {.id = APP_EVENT_ROTATION};
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, mgr->PostEvent(mgr, &event));
    app_unsub_fail_at = 0;
    TEST_ASSERT_EQUAL(0, mgr->base.Stop(&mgr->base));
    TEST_ASSERT_EQUAL(3, app_unsub_calls);
    TEST_ASSERT_EQUAL(0, mgr->base.Start(&mgr->base));
}

static IService *app_cli_lookup(const char *name) {
    return strcmp(name, SVC_NAME_APPLICATION_MANAGER) == 0 ? &appManager.base.base : NULL;
}
void test_ApplicationManager_CLIRejectsMalformedAndExtraArguments(void) {
    mocked_ServiceLocator_GetService_fake.custom_fake = app_cli_lookup;
    const char *bad[] = {"junk", "1x", "-1", "2", "999999999999999999999999999"};
    unsigned before = mocked_xQueueSend_fake.call_count;
    for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        char *args[] = {"app", "toggle_relay", (char *)bad[i]};
        TEST_ASSERT_NOT_EQUAL(0, cmd_app_handler(3, args));
    }
    char *extra[] = {"app", "status", "extra"};
    TEST_ASSERT_NOT_EQUAL(0, cmd_app_handler(3, extra));
    char *event[] = {"app", "post_event", "8junk"};
    TEST_ASSERT_NOT_EQUAL(0, cmd_app_handler(3, event));
    TEST_ASSERT_EQUAL(before, mocked_xQueueSend_fake.call_count);
}
void test_ApplicationManager_CLIPropagatesQueueAndStatusFailure(void) {
    mocked_ServiceLocator_GetService_fake.custom_fake = app_cli_lookup;
    mocked_xQueueSend_fake.return_val = pdFALSE;
    char *relay[] = {"app", "toggle_relay", "1"};
    TEST_ASSERT_NOT_EQUAL(0, cmd_app_handler(3, relay));
    char *reset[] = {"app", "reset_factory"};
    TEST_ASSERT_NOT_EQUAL(0, cmd_app_handler(2, reset));
    mocked_xSemaphoreTake_fake.return_val = pdFALSE;
    char *status[] = {"app", "status"};
    TEST_ASSERT_NOT_EQUAL(0, cmd_app_handler(2, status));
}
void test_ApplicationManager_StatusUsesActualWatchdogRegistration(void) {
    appManager.watchdog = (IWatchdog *)(uintptr_t)1;
    AppManagerStatus status;
    TEST_ASSERT_EQUAL(ESP_OK, appManager.base.GetStatus(&appManager.base, &status));
    TEST_ASSERT_FALSE(status.isWatchdogRegistered);
    atomic_store(&appManager.worker.registered, true);
    TEST_ASSERT_EQUAL(ESP_OK, appManager.base.GetStatus(&appManager.base, &status));
    TEST_ASSERT_TRUE(status.isWatchdogRegistered);
}

static AppManagerEvent app_cli_event;
static BaseType_t capture_app_cli_event(QueueHandle_t queue, const void *payload, TickType_t ticks) {
    (void)queue; (void)ticks;
    memcpy(&app_cli_event, payload, sizeof(app_cli_event));
    return pdTRUE;
}
void test_ApplicationManager_CLIPreservesTypedPayloads(void) {
    mocked_ServiceLocator_GetService_fake.custom_fake = app_cli_lookup;
    mocked_xQueueSend_fake.custom_fake = capture_app_cli_event;
    char *rotation[] = {"app", "post_event", "1", "-2"};
    TEST_ASSERT_EQUAL(0, cmd_app_handler(4, rotation));
    TEST_ASSERT_EQUAL(APP_EVENT_ROTATION, app_cli_event.id);
    TEST_ASSERT_EQUAL(-2, app_cli_event.payload.rawValue);
    char *duration[] = {"app", "post_event", "3", "4000000000"};
    TEST_ASSERT_EQUAL(0, cmd_app_handler(4, duration));
    TEST_ASSERT_EQUAL_UINT32(4000000000U, app_cli_event.payload.durationMs);
    char *relay[] = {"app", "post_event", "6", "1"};
    TEST_ASSERT_EQUAL(0, cmd_app_handler(4, relay));
    TEST_ASSERT_TRUE(app_cli_event.payload.relayState);
}
void test_ApplicationManager_RejectsUnknownAndUnimplementedEvents(void) {
    unsigned before = mocked_xQueueSend_fake.call_count;
    AppManagerEvent event = {.id = (AppManagerEventId)999};
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, appManager.base.PostEvent(&appManager.base, &event));
    event.id = APP_EVENT_TRIGGER_TELEMETRY;
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_SUPPORTED, appManager.base.PostEvent(&appManager.base, &event));
    event.id = APP_EVENT_SET_TARGET_TEMP;
    event.payload.targetTemperature = 25;
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_SUPPORTED, appManager.base.PostEvent(&appManager.base, &event));
    TEST_ASSERT_EQUAL(before, mocked_xQueueSend_fake.call_count);
}

static unsigned encoderBindCalls, encoderReleaseCalls, resetCalls;
static esp_err_t encoderBindResult, encoderReleaseResult;
static esp_err_t bind_rotation(IRotaryEncoder *self, RotaryEncoderRotationCallback callback, void *ctx) {
    (void)self; (void)ctx;
    TEST_ASSERT_EQUAL_PTR(App_OnRotationDirect, callback);
    encoderBindCalls++;
    return encoderBindResult;
}
static esp_err_t release_rotation(IRotaryEncoder *self, RotaryEncoderRotationCallback callback) {
    (void)self;
    TEST_ASSERT_EQUAL_PTR(App_OnRotationDirect, callback);
    encoderReleaseCalls++;
    return encoderReleaseResult;
}
static int32_t count_reset(IZigbee *self) { (void)self; resetCalls++; return 0; }
void test_ApplicationManager_EncoderOwnershipRequiresSuccessfulRemoval(void) {
    IRotaryEncoder encoder = {.OnRotation = bind_rotation, .UnsubscribeRotation = release_rotation};
    encoderBindCalls = encoderReleaseCalls = 0;
    encoderBindResult = ESP_OK;
    encoderReleaseResult = ESP_ERR_INVALID_STATE;
    appManager.encoder = &encoder;
    TEST_ASSERT_EQUAL(ESP_OK, App_BindEncoderRotation());
    TEST_ASSERT_EQUAL_PTR(&encoder, appManager.ownedEncoder);
    app_ack_ready = false;
    mocked_xQueueSend_fake.custom_fake = app_send_ack;
    mocked_xQueueReceive_fake.custom_fake = app_receive_ack;
    TEST_ASSERT_EQUAL(-1, App_Stop(&appManager.base.base));
    TEST_ASSERT_EQUAL_PTR(&encoder, appManager.ownedEncoder);
    TEST_ASSERT_EQUAL(-1, App_Start(&appManager.base.base));
    TEST_ASSERT_EQUAL(-1, App_Initialize(&appManager.base.base, NULL));
    encoderReleaseResult = ESP_OK;
    TEST_ASSERT_EQUAL(0, App_Stop(&appManager.base.base));
    TEST_ASSERT_NULL(appManager.ownedEncoder);
    TEST_ASSERT_EQUAL(1, encoderBindCalls);
    TEST_ASSERT_EQUAL(2, encoderReleaseCalls);
}
void test_ApplicationManager_EncoderBindingFailuresAffectHealth(void) {
    IRotaryEncoder encoder = {.OnRotation = bind_rotation};
    appManager.encoder = &encoder;
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_SUPPORTED, App_BindEncoderRotation());
    TEST_ASSERT_NULL(appManager.ownedEncoder);
    TEST_ASSERT_EQUAL(-1, App_CheckHealth(&appManager.base.base));
    encoder.UnsubscribeRotation = release_rotation;
    encoderBindResult = ESP_ERR_NO_MEM;
    TEST_ASSERT_EQUAL(ESP_ERR_NO_MEM, App_BindEncoderRotation());
    TEST_ASSERT_NULL(appManager.ownedEncoder);
    appManager.state = SERVICE_STATE_ERROR;
    unsigned before = encoderBindCalls;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, App_BindEncoderRotation());
    TEST_ASSERT_EQUAL(before, encoderBindCalls);
}
static unsigned app_reset_settings_calls;
static esp_err_t app_reset_settings_result;
static esp_err_t app_reset_settings(IConfigurationManager *self, const char *block) {
    (void)self; TEST_ASSERT_NULL(block); ++app_reset_settings_calls; return app_reset_settings_result;
}
static esp_err_t app_reset_snapshot(IConfigurationManager *self, SystemSettings *out) {
    (void)self; memset(out, 0, sizeof(*out)); out->system.power_profile = 1; out->system.zigbee_tx_profile = 1; return ESP_OK;
}
static int32_t app_reset_cpu(IPowerManager *self, PowerMode_t mode) { (void)self; TEST_ASSERT_EQUAL(1, mode); return POWER_OK; }
static int32_t app_reset_tx(IZigbee *self, ZigbeeTxProfile mode) { (void)self; TEST_ASSERT_EQUAL(1, mode); return ESP_OK; }
static PowerMode_t app_reset_mode(IPowerManager *self) { (void)self; return POWER_MODE_BALANCED; }
static bool app_reset_power_pending(IPowerManager *self) { (void)self; return false; }
static esp_err_t app_reset_guard(IConfigurationManager *self, bool active) { (void)self; (void)active; return ESP_OK; }
static int32_t app_reset_selftest(IPowerManager *self) { (void)self; return POWER_OK; }
static int32_t app_reset_restart(IPowerManager *self, uint32_t delay) { (void)self; (void)delay; return POWER_OK; }
static int32_t app_reset_tx_status(IZigbee *self, ZigbeeTxStatus *out) {
    (void)self; *out = (ZigbeeTxStatus){.profile = ZIGBEE_TX_NORMAL, .dbm = 10}; return ESP_OK;
}
static bool app_reset_pairing(IZigbee *self) { (void)self; return false; }
static int32_t app_reset_begin(IZigbee *self, uint32_t *receipt) { *receipt = 42; return count_reset(self); }
static int32_t app_reset_network_status(IZigbee *self, ZigbeeFactoryResetStatus *out) {
    (void)self; *out = (ZigbeeFactoryResetStatus){.connected = true}; return ESP_OK;
}
static IPowerManager app_reset_power = {.SetPowerMode = app_reset_cpu, .GetPowerMode = app_reset_mode,
    .SelfTest = app_reset_selftest, .RestartAsync = app_reset_restart, .IsTransitionPending = app_reset_power_pending};
static IConfigurationManager app_reset_config = {.ResetToDefaults = app_reset_settings,
    .GetSnapshot = app_reset_snapshot, .VerifyDefaults = app_reset_snapshot, .SetFactoryResetGuard = app_reset_guard};
static IService *app_reset_resolve(const char *name) {
    if (!strcmp(name, SVC_NAME_CONFIGURATION_MANAGER)) return (IService *)&app_reset_config;
    if (!strcmp(name, SVC_NAME_POWER_MANAGER)) return (IService *)&app_reset_power;
    return NULL;
}
void test_ApplicationManager_ClosingDiscardsQueuedActions(void) {
    IZigbee zigbee = {.ResetToFactorySettings = count_reset, .SetTxProfile = app_reset_tx};
    appManager.zigbee = &zigbee;
    mocked_ServiceLocator_GetService_fake.custom_fake = app_reset_resolve;
    app_reset_settings_result = ESP_OK;
    resetCalls = 0;
    AppManagerEvent event = {.id = APP_EVENT_FACTORY_RESET};
    TEST_ASSERT_EQUAL(ESP_OK, appManager.base.TriggerFactoryReset(&appManager.base));
    TEST_ASSERT_TRUE(App_DispatchQueuedEvent(&event));
    TEST_ASSERT_EQUAL(0, resetCalls);
    uint32_t processed = appManager.processedEvents;
    appManager.state = SERVICE_STATE_ERROR;
    TEST_ASSERT_FALSE(App_DispatchQueuedEvent(&event));
    event.id = APP_EVENT_TOGGLE_RELAY;
    TEST_ASSERT_FALSE(App_DispatchQueuedEvent(&event));
    TEST_ASSERT_EQUAL(0, resetCalls);
    TEST_ASSERT_EQUAL(processed, appManager.processedEvents);
}

void test_ApplicationManager_FactoryFailurePreservesNetwork(void) {
    IZigbee radio = {.BeginFactoryReset = app_reset_begin, .GetFactoryResetStatus = app_reset_network_status,
        .IsPairing = app_reset_pairing, .SetTxProfile = app_reset_tx, .GetTxStatus = app_reset_tx_status};
    appManager.zigbee = &radio;
    mocked_ServiceLocator_GetService_fake.custom_fake = app_reset_resolve;
    app_reset_settings_calls = resetCalls = 0; app_reset_settings_result = ESP_FAIL;
    TEST_ASSERT_EQUAL(ESP_OK, appManager.base.TriggerFactoryReset(&appManager.base));
    App_BeginFactoryReset(); App_AdvanceFactoryReset(); App_AdvanceFactoryReset();
    TEST_ASSERT_EQUAL(FACTORY_RESET_FAILED, appManager.factoryReset.phase);
    TEST_ASSERT_EQUAL(ESP_FAIL, appManager.factoryReset.error);
    TEST_ASSERT_FALSE(atomic_load(&appManager.factoryResetBusy));
    TEST_ASSERT_EQUAL(1, app_reset_settings_calls); TEST_ASSERT_EQUAL(0, resetCalls);
    radio.BeginFactoryReset = NULL;
    TEST_ASSERT_EQUAL(ESP_OK, appManager.base.TriggerFactoryReset(&appManager.base));
    App_BeginFactoryReset(); App_AdvanceFactoryReset();
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, appManager.factoryReset.error);
    TEST_ASSERT_EQUAL(1, app_reset_settings_calls);
}
void test_ApplicationManager_LongPressCannotFactoryReset(void) {
    IZigbee radio = {.ResetToFactorySettings = count_reset, .SetTxProfile = app_reset_tx}; appManager.zigbee = &radio;
    mocked_ServiceLocator_GetService_fake.custom_fake = app_reset_resolve;
    app_reset_settings_calls = resetCalls = 0; app_reset_settings_result = ESP_OK;
    AppManagerEvent event = {.id = APP_EVENT_BUTTON_LONGPRESS, .payload.durationMs = APP_FACTORY_RESET_HOLD_DURATION_MS - 1u};
    App_ProcessEvent(&event); TEST_ASSERT_EQUAL(0, resetCalls);
    event.payload.durationMs++; App_ProcessEvent(&event);
    TEST_ASSERT_EQUAL(0, app_reset_settings_calls); TEST_ASSERT_EQUAL(0, resetCalls);
}
void test_ApplicationManager_MissingRelayMethodsAndMenuStateAreSafe(void) {
    IRelay relay = {0}; appManager.relay = &relay;
    IMenuManager menu = {0}; appManager.menu = &menu;
    AppManagerEvent event = {.id = APP_EVENT_TOGGLE_RELAY, .payload.relayState = true};
    App_ProcessEvent(&event); event.payload.relayState = false; App_ProcessEvent(&event);
    TEST_ASSERT_NULL(App_GetMenuManager());
}

static IApplicationManager app_empty_interface;
static IService *app_empty_resolve(const char *name) { (void)name; return (IService *)&app_empty_interface; }
void test_ApplicationManager_CLIRejectsMalformedArgumentsAndMissingCallbacks(void) {
    char *bad[] = {"app", NULL}; TEST_ASSERT_EQUAL(1, cmd_app_handler(2, bad));
    bad[1] = ""; TEST_ASSERT_EQUAL(1, cmd_app_handler(2, bad));
    TEST_ASSERT_EQUAL(1, cmd_app_handler(0, NULL));
    mocked_ServiceLocator_GetService_fake.custom_fake = app_empty_resolve;
    char *status[] = {"app", "status"}; TEST_ASSERT_EQUAL(1, cmd_app_handler(2, status));
    char *reset[] = {"app", "reset_factory"}; TEST_ASSERT_EQUAL(1, cmd_app_handler(2, reset));
    char *relay[] = {"app", "toggle_relay", "1"}; TEST_ASSERT_EQUAL(1, cmd_app_handler(3, relay));
    char *evt[] = {"app", "post_event", "1", "1"}; TEST_ASSERT_EQUAL(1, cmd_app_handler(4, evt));
}
void test_ApplicationManager_PublicForeignOwnerPreservesQueueAndOutput(void) {
    AppManagerEvent event = {.id = APP_EVENT_FACTORY_RESET};
    unsigned calls = mocked_xQueueSend_fake.call_count;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, appManager.base.PostEvent(&app_empty_interface, &event));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, appManager.base.TriggerFactoryReset(&app_empty_interface));
    TEST_ASSERT_EQUAL(calls, mocked_xQueueSend_fake.call_count);
    AppManagerStatus output = {.processedEventsCount = 777};
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, appManager.base.GetStatus(&app_empty_interface, &output));
    TEST_ASSERT_EQUAL(777, output.processedEventsCount);
}

void test_ApplicationManager_StatusBeforeInitializationPreservesCallerOutput(void) {
    SemaphoreHandle_t saved = appManager.mutex;
    appManager.mutex = NULL;
    AppManagerStatus output = {.processedEventsCount = 888};
    esp_err_t result = appManager.base.GetStatus(&appManager.base, &output);
    appManager.mutex = saved;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, result);
    TEST_ASSERT_EQUAL(888, output.processedEventsCount);
}

void test_ApplicationManager_FactoryAdmissionIsSingleFlightForAllEventPaths(void) {
    TEST_ASSERT_EQUAL(ESP_OK, appManager.base.TriggerFactoryReset(&appManager.base));
    unsigned calls = mocked_xQueueSend_fake.call_count;
    AppManagerEvent event = {.id = APP_EVENT_FACTORY_RESET};
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FINISHED, appManager.base.PostEvent(&appManager.base, &event));
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FINISHED, appManager.base.TriggerFactoryReset(&appManager.base));
    TEST_ASSERT_EQUAL(calls, mocked_xQueueSend_fake.call_count);
    AppManagerStatus status;
    TEST_ASSERT_EQUAL(ESP_OK, appManager.base.GetStatus(&appManager.base, &status));
    TEST_ASSERT_TRUE(status.factoryResetBusy);
    TEST_ASSERT_EQUAL(1, status.factoryResetId);
    TEST_ASSERT_EQUAL(FACTORY_RESET_IDLE, status.factoryResetPhase);
}
void test_ApplicationManager_FactoryQueueFailureReleasesAdmission(void) {
    mocked_xQueueSend_fake.return_val = pdFALSE;
    TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT, appManager.base.TriggerFactoryReset(&appManager.base));
    TEST_ASSERT_FALSE(atomic_load(&appManager.factoryResetBusy));
    TEST_ASSERT_EQUAL(0, appManager.factoryResetId);
    mocked_xQueueSend_fake.return_val = pdTRUE;
    TEST_ASSERT_EQUAL(ESP_OK, appManager.base.TriggerFactoryReset(&appManager.base));
    TEST_ASSERT_TRUE(atomic_load(&appManager.factoryResetBusy));
    TEST_ASSERT_EQUAL(1, appManager.factoryResetId);
}
static BaseType_t app_factory_queue_interleave(QueueHandle_t queue, const void *event, TickType_t wait) {
    (void)queue; (void)event; (void)wait;
    TEST_ASSERT_EQUAL(FACTORY_RESET_IDLE, appManager.factoryReset.phase);
    App_AdvanceFactoryReset();
    TEST_ASSERT_TRUE(atomic_load(&appManager.factoryResetBusy));
    return pdTRUE;
}
void test_ApplicationManager_FactoryQueueReservationHidesPreviousFailedOperationFromWorker(void) {
    appManager.factoryResetId = 3;
    appManager.factoryReset = (FactoryResetOperation){.phase = FACTORY_RESET_FAILED, .error = ESP_FAIL};
    mocked_xQueueSend_fake.custom_fake = app_factory_queue_interleave;
    TEST_ASSERT_EQUAL(ESP_OK, appManager.base.TriggerFactoryReset(&appManager.base));
    TEST_ASSERT_TRUE(atomic_load(&appManager.factoryResetBusy));
    TEST_ASSERT_EQUAL(4, appManager.factoryResetId);
    TEST_ASSERT_EQUAL(FACTORY_RESET_IDLE, appManager.factoryReset.phase);
    TEST_ASSERT_EQUAL(ESP_OK, appManager.factoryReset.error);
}
void test_ApplicationManager_RejectedFactoryQueueRestoresPreviousStatusAndId(void) {
    appManager.factoryResetId = 3;
    appManager.factoryReset = (FactoryResetOperation){.phase = FACTORY_RESET_FAILED, .error = ESP_FAIL};
    mocked_xQueueSend_fake.return_val = pdFALSE;
    TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT, appManager.base.TriggerFactoryReset(&appManager.base));
    TEST_ASSERT_FALSE(atomic_load(&appManager.factoryResetBusy));
    TEST_ASSERT_EQUAL(3, appManager.factoryResetId);
    TEST_ASSERT_EQUAL(FACTORY_RESET_FAILED, appManager.factoryReset.phase);
    TEST_ASSERT_EQUAL(ESP_FAIL, appManager.factoryReset.error);
}
void test_ApplicationManager_StopPreservesQueuedFactoryOperationAndWorker(void) {
    TEST_ASSERT_EQUAL(ESP_OK, appManager.base.TriggerFactoryReset(&appManager.base));
    TaskHandle_t task = atomic_load(&appManager.worker.task);
    TEST_ASSERT_NOT_EQUAL(0, appManager.base.base.Stop(&appManager.base.base));
    TEST_ASSERT_EQUAL_PTR(task, atomic_load(&appManager.worker.task));
    TEST_ASSERT_EQUAL(SERVICE_STATE_STARTED, appManager.state);
    TEST_ASSERT_TRUE(atomic_load(&appManager.factoryResetBusy));
    TEST_ASSERT_EQUAL(0, mocked_vTaskDelete_fake.call_count);
}
static bool app_late_network_pending;
static esp_err_t app_guard_release_result;
static unsigned app_guard_release_calls;
static int32_t app_late_network_status(IZigbee *self, ZigbeeFactoryResetStatus *out) {
    (void)self;
    *out = (ZigbeeFactoryResetStatus){.operation_id = 42, .pending = app_late_network_pending,
        .factory_new = !app_late_network_pending};
    return ESP_OK;
}
static esp_err_t app_late_release_guard(IConfigurationManager *self, bool active) {
    (void)self; TEST_ASSERT_FALSE(active); ++app_guard_release_calls; return app_guard_release_result;
}
void test_ApplicationManager_TimeoutRetainsSdkAndGuardUntilLateCompletionAndCheckedRelease(void) {
    IZigbee radio = {.GetFactoryResetStatus = app_late_network_status};
    IConfigurationManager config = {.SetFactoryResetGuard = app_late_release_guard};
    appManager.factoryReset = (FactoryResetOperation){.phase = FACTORY_RESET_FAILED,
        .error = ESP_ERR_TIMEOUT, .network_receipt = 42, .guard_held = true, .radio = &radio, .config = &config};
    atomic_store(&appManager.factoryResetBusy, true);
    app_late_network_pending = true; app_guard_release_result = ESP_ERR_TIMEOUT; app_guard_release_calls = 0;
    App_AdvanceFactoryReset();
    TEST_ASSERT_TRUE(atomic_load(&appManager.factoryResetBusy));
    TEST_ASSERT_EQUAL(0, app_guard_release_calls);
    app_late_network_pending = false;
    App_AdvanceFactoryReset();
    TEST_ASSERT_TRUE(atomic_load(&appManager.factoryResetBusy));
    TEST_ASSERT_TRUE(appManager.factoryReset.guard_held);
    TEST_ASSERT_EQUAL(1, app_guard_release_calls);
    app_guard_release_result = ESP_OK;
    App_AdvanceFactoryReset();
    TEST_ASSERT_FALSE(atomic_load(&appManager.factoryResetBusy));
    TEST_ASSERT_FALSE(appManager.factoryReset.guard_held);
    TEST_ASSERT_EQUAL(FACTORY_RESET_FAILED, appManager.factoryReset.phase);
    TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT, appManager.factoryReset.error);
    TEST_ASSERT_EQUAL(2, app_guard_release_calls);
}

static unsigned rotation_activity_calls;
static ServiceState rotation_menu_state(IService *self) { (void)self; return SERVICE_STATE_STARTED; }
static void rotation_activity(IMenuManager *self) { (void)self; ++rotation_activity_calls; }
void test_ApplicationManager_RotationObserverDoesNotWakeDisplay(void) {
    IMenuManager menu = {.base = {.GetState = rotation_menu_state}, .UpdateActivity = rotation_activity};
    appManager.menu = &menu;
    rotation_activity_calls = 0;
    for (int direction = -1; direction <= 1; direction += 2) {
        AppManagerEvent event = {.id = APP_EVENT_ROTATION, .payload = {.rawValue = direction}};
        App_ProcessEvent(&event);
    }
    TEST_ASSERT_EQUAL(0, rotation_activity_calls);
    AppManagerEvent click = {.id = APP_EVENT_BUTTON_CLICK};
    App_ProcessEvent(&click);
    TEST_ASSERT_EQUAL(1, rotation_activity_calls);
    appManager.menu = NULL;
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_ApplicationManager_RotationObserverDoesNotWakeDisplay);
    RUN_TEST(test_ApplicationManager_StatusBeforeInitializationPreservesCallerOutput);
    RUN_TEST(test_ApplicationManager_CLIRejectsMalformedArgumentsAndMissingCallbacks);
    RUN_TEST(test_ApplicationManager_PublicForeignOwnerPreservesQueueAndOutput);
    RUN_TEST(test_ApplicationManager_FactoryFailurePreservesNetwork);
    RUN_TEST(test_ApplicationManager_LongPressCannotFactoryReset);
    RUN_TEST(test_ApplicationManager_MissingRelayMethodsAndMenuStateAreSafe);
    RUN_TEST(test_ApplicationManager_EncoderOwnershipRequiresSuccessfulRemoval);
    RUN_TEST(test_ApplicationManager_EncoderBindingFailuresAffectHealth);
    RUN_TEST(test_ApplicationManager_ClosingDiscardsQueuedActions);
    RUN_TEST(test_ApplicationManager_CLIPreservesTypedPayloads);
    RUN_TEST(test_ApplicationManager_RejectsUnknownAndUnimplementedEvents);
    RUN_TEST(test_ApplicationManager_CLIRejectsMalformedAndExtraArguments);
    RUN_TEST(test_ApplicationManager_CLIPropagatesQueueAndStatusFailure);
    RUN_TEST(test_ApplicationManager_StatusUsesActualWatchdogRegistration);
    RUN_TEST(test_ApplicationManager_SubscriptionFailureRollsBack);
    RUN_TEST(test_ApplicationManager_UnsubscribeBusyRequiresRetry);
    RUN_TEST(test_ApplicationManager_StopTimeoutAndRestart);
    RUN_TEST(test_ApplicationManager_AllocationAndLaunchFailures);
    RUN_TEST(test_ApplicationManager_GetModule_Singleton);
    RUN_TEST(test_ApplicationManager_InitialState);
    RUN_TEST(test_ApplicationManager_Initialize_And_Start);
    RUN_TEST(test_ApplicationManager_GetStatus);
    RUN_TEST(test_ApplicationManager_GetStatus_NullArg);
    RUN_TEST(test_ApplicationManager_PostEvent_Types);
    RUN_TEST(test_ApplicationManager_PostEvent_InvalidArg);
    RUN_TEST(test_ApplicationManager_PostEvent_QueueFull);
    RUN_TEST(test_ApplicationManager_TriggerFactoryReset);
    RUN_TEST(test_ApplicationManager_FactoryAdmissionIsSingleFlightForAllEventPaths);
    RUN_TEST(test_ApplicationManager_FactoryQueueFailureReleasesAdmission);
    RUN_TEST(test_ApplicationManager_FactoryQueueReservationHidesPreviousFailedOperationFromWorker);
    RUN_TEST(test_ApplicationManager_RejectedFactoryQueueRestoresPreviousStatusAndId);
    RUN_TEST(test_ApplicationManager_StopPreservesQueuedFactoryOperationAndWorker);
    RUN_TEST(test_ApplicationManager_TimeoutRetainsSdkAndGuardUntilLateCompletionAndCheckedRelease);
    RUN_TEST(test_ApplicationManager_CheckHealth_WhenStarted);
    return UNITY_END();
}
