/**
 * @file test_BuiltinLED.c
 * @brief Native unit tests for the BuiltinLED service.
 *
 * Compiled only when RACOOL_TEST_NATIVE is defined (gcc native build).
 *
 * Coverage:
 *   - Full service lifecycle (Initialize → Start → Stop)
 *   - All public API commands (SetAnimation, StopAnimation, TurnOn, TurnOff,
 *     SetBrightness, RegisterAnimation, GetStatus, InitModule)
 *   - Priority preemption
 *   - Timed animation expiry (duration countdown)
 *   - Plugin duplicate detection and overflow guard
 *   - SettingsChangedHandler (auto-mode and manual-mode)
 *   - EventBus subscribe/unsubscribe lifecycle
 *   - Console command registration count
 *   - Hardware render flag correctness (CMD_TURN_ON / CMD_TURN_OFF)
 *   - Brightness scaling and overflow clamp
 *   - Self-test: all 15 built-in animation plugins
 *   - FindPlugin miss path
 */

#ifdef RACOOL_TEST_NATIVE

#include <unity.h>
#include <string.h>
#include <stdlib.h>
#include <setjmp.h>
#include "led_strip.h"   /* led_strip.h mock — provides types used by mock stubs below */

/* Mock framework — declarations only; definitions are compiled by run_native_tests.ps1
 * via the separate $mockSrc argument to gcc. */
#include "../../Infrastructure/support/test_mocks.h"

/* Project headers pulled in before the source under test. */
#include "Hardware/BuiltinLED/IBuiltinLED.h"
#include "Hardware/BuiltinLED/IAnimationPlugin.h"
#include "Hardware/BuiltinLED/Animations/BuiltinAnimations.h"

/* ---------------------------------------------------------------------------
 * led_strip driver mock implementations
 * (declarations come from led_strip.h mock header)
 * ------------------------------------------------------------------------- */

static unsigned strip_delete_calls;
static esp_err_t strip_delete_result;
static esp_err_t strip_create_result, strip_clear_result;
static unsigned strip_pixel_calls, strip_refresh_calls;
static unsigned strip_pixel_failures, strip_refresh_failures;
esp_err_t led_strip_new_spi_device(const led_strip_config_t   *cfg,
                                    const led_strip_spi_config_t *spi,
                                    led_strip_handle_t           *ret) {
    (void)cfg; (void)spi;
    if (strip_create_result != ESP_OK) return strip_create_result;
    *ret = (led_strip_handle_t)(uintptr_t)0x1234;
    return ESP_OK;
}

esp_err_t led_strip_set_pixel(led_strip_handle_t s, uint32_t i,
                               uint32_t r, uint32_t g, uint32_t b) {
    (void)s; (void)i; (void)r; (void)g; (void)b;
    ++strip_pixel_calls;
    if (strip_pixel_failures) { --strip_pixel_failures; return ESP_FAIL; }
    return ESP_OK;
}

esp_err_t led_strip_refresh(led_strip_handle_t s) {
    (void)s; ++strip_refresh_calls;
    if (strip_refresh_failures) { --strip_refresh_failures; return ESP_FAIL; }
    return ESP_OK;
}
esp_err_t led_strip_clear(led_strip_handle_t s)   { (void)s; return strip_clear_result; }
esp_err_t led_strip_del(led_strip_handle_t s)      { (void)s; ++strip_delete_calls; return strip_delete_result; }

/* Override config constant for tests (avoid dependency on board config). */
#undef  BUILTIN_LED_PIN
#define BUILTIN_LED_PIN 8

/* Include source under test AFTER all stubs are defined so that every
 * function-like macro substitution (esp_timer_get_time → mocked…) applies. */
static unsigned trace_allocations, trace_frees, trace_fail_at;
static size_t trace_allocation_limit;
static void *trace_test_calloc(size_t count, size_t size) {
    ++trace_allocations;
    if ((trace_fail_at && trace_allocations == trace_fail_at) ||
        (trace_allocation_limit && count * size > trace_allocation_limit)) return NULL;
    return calloc(count, size);
}
static void trace_test_free(void *pointer) {
    if (pointer) ++trace_frees;
    free(pointer);
}
#define calloc trace_test_calloc
#define free trace_test_free
#include "../../../src/Hardware/BuiltinLED/BuiltinLED.c"
#undef calloc
#undef free
#include "../../../src/Hardware/BuiltinLED/BuiltinLEDConsoleCommands.c"

/* Include all animation plugin sources so their *_Get() symbols are resolved
 * by the linker (same pattern used in test_RGBLED_Native.c). */
#include "../../../src/Hardware/LEDAnimations/HeartbeatAnim.c"
#include "../../../src/Hardware/LEDAnimations/BlinkWhiteAnim.c"
#include "../../../src/Hardware/LEDAnimations/SOSRedAnim.c"
#include "../../../src/Hardware/LEDAnimations/BreathingGreenAnim.c"
#include "../../../src/Hardware/LEDAnimations/FadeBlueAnim.c"
#include "../../../src/Hardware/LEDAnimations/StrobeWhiteAnim.c"
#include "../../../src/Hardware/LEDAnimations/RainbowFlowAnim.c"
#include "../../../src/Hardware/LEDAnimations/PoliceStrobeAnim.c"
#include "../../../src/Hardware/LEDAnimations/SolidRedAnim.c"
#include "../../../src/Hardware/LEDAnimations/SolidGreenAnim.c"
#include "../../../src/Hardware/LEDAnimations/SolidBlueAnim.c"
#include "../../../src/Hardware/LEDAnimations/SolidWhiteAnim.c"
#include "../../../src/Hardware/LEDAnimations/BlinkRedAnim.c"
#include "../../../src/Hardware/LEDAnimations/BreathingRedAnim.c"
#include "../../../src/Hardware/LEDAnimations/DoubleBlinkWhiteAnim.c"

/* ---------------------------------------------------------------------------
 * Local mock state
 * ------------------------------------------------------------------------- */

static int mock_subscribe_count   = 0;
static int mock_unsubscribe_count = 0;
static int mock_publish_count     = 0;
static int mock_cmd_count         = 0;
static unsigned driver_error_events;
static int subscribe_fail_at, unsubscribe_fail_at;

static int32_t stub_subscribe(struct IEventBus *self, const char *e, BusEventHandler h) {
    (void)self; (void)e; (void)h;
    mock_subscribe_count++;
    return mock_subscribe_count == subscribe_fail_at ? -1 : 0;
}
static int32_t stub_unsubscribe(struct IEventBus *self, const char *e, BusEventHandler h) {
    (void)self; (void)e; (void)h;
    mock_unsubscribe_count++;
    return mock_unsubscribe_count == unsubscribe_fail_at ? EVENT_BUS_ERR_BUSY : 0;
}
static int32_t stub_publish(BusEvent event) {
    if (event.eventName && strcmp(event.eventName, EVENT_BUILTIN_LED_ERROR) == 0) driver_error_events++;
    mock_publish_count++;
    return 0;
}

static IEventBus stub_event_bus = {
    .Subscribe   = stub_subscribe,
    .Unsubscribe = stub_unsubscribe,
    .Publish     = stub_publish,
};

static SystemSettings fake_settings;

static esp_err_t stub_GetSnapshot(IConfigurationManager *self, SystemSettings *out) {
    (void)self; *out = fake_settings; return ESP_OK;
}

static SystemSettings *stub_GetSettings(IConfigurationManager *self) {
    (void)self;
    return &fake_settings;
}

static esp_err_t stub_RegisterCommandWithCategory(struct IDebugConsole *self,
                                                   const esp_console_cmd_t *cmd,
                                                   CommandCategory cat) {
    (void)self; (void)cmd; (void)cat;
    mock_cmd_count++;
    return ESP_OK;
}

static IDebugConsole         stub_console    = {.RegisterCommandWithCategory = stub_RegisterCommandWithCategory};
static IConfigurationManager stub_config_mgr;
static IPinManager *test_pin_manager;

/**
 * @brief Custom ServiceLocator_GetService fake.
 *
 * Returns the correct mock for each service name:
 *   - SVC_NAME_DEBUG_CONSOLE          → IDebugConsole stub
 *   - SVC_NAME_CONFIGURATION_MANAGER  → IConfigurationManager stub
 *   - Everything else (e.g. PIN_MANAGER) → NULL so that optional paths
 *     fall back to defaults instead of crashing on a bad cast.
 */
static IService *svc_locator_custom(const char *name) {
    if (name && strcmp(name, SVC_NAME_DEBUG_CONSOLE) == 0)
        return (IService *)&stub_console;
    if (name && strcmp(name, SVC_NAME_CONFIGURATION_MANAGER) == 0)
        return (IService *)&stub_config_mgr;
    if (name && strcmp(name, SVC_NAME_PIN_MANAGER) == 0)
        return (IService *)test_pin_manager;
    return NULL;
}

/* ---------------------------------------------------------------------------
 * Helper: reset all FFF fakes used by this suite
 * ------------------------------------------------------------------------- */

static void ResetAllMocks(void) {
    RESET_FAKE(mocked_xSemaphoreCreateMutex);
    RESET_FAKE(mocked_xSemaphoreTake);
    RESET_FAKE(mocked_xSemaphoreGive);
    RESET_FAKE(mocked_xQueueCreate);
    RESET_FAKE(mocked_xQueueSend);
    RESET_FAKE(mocked_xQueueReceive);
    RESET_FAKE(mocked_xTaskCreate);
    RESET_FAKE(mocked_vTaskDelete);
    RESET_FAKE(mocked_ServiceLocator_GetService);
    RESET_FAKE(mocked_ServiceLocator_GetEventBus);
    RESET_FAKE(mocked_ConfigurationManager_GetInstance);
    RESET_FAKE(mocked_esp_timer_get_time);
    FFF_RESET_HISTORY();
}

/* ---------------------------------------------------------------------------
 * Test fixture
 * ------------------------------------------------------------------------- */

static BaseType_t create_worker(TaskFunction_t fn, const char *const name,
    const uint32_t stack, void *const arg, UBaseType_t priority, TaskHandle_t *const task) {
    (void)fn; (void)name; (void)stack; (void)arg; (void)priority;
    if (mocked_xTaskCreate_fake.return_val != pdPASS) return pdFAIL;
    *task = (TaskHandle_t)(uintptr_t)0x1234;
    return pdPASS;
}
static BaseType_t receive_worker_ack(QueueHandle_t queue, void *out, TickType_t ticks) {
    if (queue == builtinLEDService.worker.stopped && ticks &&
        atomic_load(&builtinLEDService.worker.stopRequested)) {
        *(int32_t *)out = 0;
        return pdTRUE;
    }
    return pdFALSE;
}

void setUp(void) {
    trace_allocations = trace_frees = trace_fail_at = 0;
    trace_allocation_limit = 0;
    ResetAllMocks();
    strip_delete_calls = 0;
    strip_delete_result = ESP_OK;
    strip_create_result = strip_clear_result = ESP_OK;
    strip_pixel_calls = strip_refresh_calls = strip_pixel_failures = strip_refresh_failures = 0;
    driver_error_events = 0;
    test_pin_manager = NULL;
    atomic_store(&builtinLEDService.driverError, ESP_OK);
    builtinLEDService.driverCleanupFailed = false;
    builtinLEDService.subscriptions = 0;
    memset(&builtinLEDService.worker, 0, sizeof(builtinLEDService.worker));
    builtinLEDService.controlMutex = NULL;
    RESET_FAKE(mocked_xTaskGetCurrentTaskHandle);
    RESET_FAKE(mocked_xTaskGetTickCount);
    subscribe_fail_at = unsubscribe_fail_at = 0;

    mock_subscribe_count   = 0;
    mock_unsubscribe_count = 0;
    mock_publish_count     = 0;
    mock_cmd_count         = 0;

    memset(&fake_settings, 0, sizeof(fake_settings));
    stub_config_mgr.GetSettings = stub_GetSettings;
    stub_config_mgr.GetSnapshot = stub_GetSnapshot;
    fake_settings.hardware.builtin_led_brightness = DEFAULT_BUILTIN_LED_BRIGHTNESS;
    fake_settings.hardware.builtin_led_anim_mode = 1;

    /* Default mock return values. */
    mocked_ServiceLocator_GetEventBus_fake.return_val        = (IEventBus *)&stub_event_bus;
    mocked_ConfigurationManager_GetInstance_fake.return_val  = (IConfigurationManager *)&stub_config_mgr;
    /* Return the right service per name — avoids bad casts that cause ACCESS VIOLATION. */
    mocked_ServiceLocator_GetService_fake.custom_fake        = svc_locator_custom;

    mocked_xSemaphoreCreateMutex_fake.return_val = (SemaphoreHandle_t)(uintptr_t)0x5678;
    mocked_xSemaphoreTake_fake.return_val        = pdTRUE;
    mocked_xSemaphoreGive_fake.return_val        = pdTRUE;
    mocked_xQueueCreate_fake.return_val          = (QueueHandle_t)(uintptr_t)0x9abc;
    mocked_xQueueSend_fake.return_val            = pdTRUE;
    mocked_xQueueReceive_fake.return_val         = pdFALSE;
    mocked_xTaskCreate_fake.return_val           = pdPASS;
    mocked_xTaskCreate_fake.custom_fake = create_worker;
    mocked_xQueueReceive_fake.custom_fake = receive_worker_ack;
    mocked_esp_timer_get_time_fake.return_val    = 0;

    /* Reset singleton state so each test starts clean. */
    builtinLEDService.state          = SERVICE_STATE_UNINITIALIZED;
    builtinLEDService.animTaskHandle = NULL;
    builtinLEDService.plugin_count   = 0;
    builtinLEDService.led_strip      = NULL;
    builtinLEDService.eventBus       = NULL;
    builtinLEDService.disabled = false;
    memset(&builtinLEDService.status, 0, sizeof(BuiltinLEDStatus));
}

void tearDown(void) {
    ResetAllMocks();
}

/* ---------------------------------------------------------------------------
 * Test: Full lifecycle
 * ------------------------------------------------------------------------- */

/**
 * Tests Initialize → Start → CheckHealth → Stop.
 * Verifies plugin count, task creation, EventBus subscription, and
 * console command registration count.
 */
static void AssertWorkerDeletedAfterAcknowledgment(TaskHandle_t task) {
    TEST_ASSERT_NOT_NULL(task);
    TEST_ASSERT_TRUE(atomic_load(&builtinLEDService.worker.stopRequested));
    TEST_ASSERT_EQUAL_PTR(builtinLEDService.worker.gate, mocked_xSemaphoreTake_fake.arg0_val);
}

void test_BuiltinLED_AnimationCliRejectsMalformedNumbersBeforeQueueAdmission(void) {
    const char *bad[] = {"abc", "1x", "-2", "256", "4294967296", "999999999999999999999", " 1", "+1", ""};
    for (size_t index = 0; index < sizeof(bad) / sizeof(bad[0]); ++index) {
        char *args[] = {"builtin_led_set", (char *)bad[index]};
        TEST_ASSERT_EQUAL(1, cmd_builtin_led_set(2, args));
    }
    char *priority[] = {"builtin_led_set", "0", "3"};
    char *duration[] = {"builtin_led_set", "0", "2", "4294967296"};
    TEST_ASSERT_EQUAL(1, cmd_builtin_led_set(3, priority));
    TEST_ASSERT_EQUAL(1, cmd_builtin_led_set(4, duration));
    TEST_ASSERT_EQUAL(0, mocked_xQueueSend_fake.call_count);
    IBuiltinLED *led = BuiltinLED_GetModule();
    TEST_ASSERT_EQUAL(0, led->base.Initialize(&led->base, NULL));
    char *valid[] = {"builtin_led_set", "20", "2", "4294967295"};
    TEST_ASSERT_EQUAL(0, cmd_builtin_led_set(4, valid));
    char *off[] = {"builtin_led_set", "-1"};
    TEST_ASSERT_EQUAL(0, cmd_builtin_led_set(2, off));
    TEST_ASSERT_EQUAL(2, mocked_xQueueSend_fake.call_count);
}

void test_BuiltinLED_Lifecycle(void) {
    IBuiltinLED *led     = BuiltinLED_GetModule();
    IService    *service = &led->base;

    TEST_ASSERT_EQUAL(0, service->Initialize(service, NULL));
    TEST_ASSERT_EQUAL(SERVICE_STATE_INITIALIZED, service->GetState(service));
    TEST_ASSERT_EQUAL(BUILTIN_LED_ANIM_COUNT, builtinLEDService.plugin_count);
    for (int type = 0; type < BUILTIN_LED_ANIM_COUNT; ++type) {
        TEST_ASSERT_NOT_NULL(builtinLEDService.plugins[type]);
        TEST_ASSERT_EQUAL(type, builtinLEDService.plugins[type]->type);
    }

    TEST_ASSERT_EQUAL(0, service->Start(service));
    TEST_ASSERT_EQUAL(SERVICE_STATE_STARTED, service->GetState(service));
    TEST_ASSERT_EQUAL(1, mocked_xTaskCreate_fake.call_count);
    TEST_ASSERT_EQUAL(7, mock_subscribe_count);  // Includes disconnect cancellation.
    TEST_ASSERT_EQUAL(9, mock_cmd_count);

    TEST_ASSERT_EQUAL(0, service->CheckHealth(service));

    builtinLEDService.animTaskHandle = (TaskHandle_t)(uintptr_t)0x1234;
    mocked_vTaskDelete_fake.custom_fake = AssertWorkerDeletedAfterAcknowledgment;
    TEST_ASSERT_EQUAL(0, service->Stop(service));
    TEST_ASSERT_EQUAL(SERVICE_STATE_STOPPED, service->GetState(service));
    TEST_ASSERT_EQUAL(1, mocked_vTaskDelete_fake.call_count);
    TEST_ASSERT_EQUAL(7, mock_unsubscribe_count);  // Includes disconnect cancellation.
}

/* ---------------------------------------------------------------------------
 * Test: All public API commands enqueue correctly
 * ------------------------------------------------------------------------- */

void test_BuiltinLED_Ops(void) {
    IBuiltinLED *led = BuiltinLED_GetModule();
    led->base.Initialize(&led->base, NULL);

    TEST_ASSERT_EQUAL(ESP_OK, led->TurnOn(led));
    TEST_ASSERT_EQUAL(1, mocked_xQueueSend_fake.call_count);

    TEST_ASSERT_EQUAL(ESP_OK, led->TurnOff(led));
    TEST_ASSERT_EQUAL(2, mocked_xQueueSend_fake.call_count);

    TEST_ASSERT_EQUAL(ESP_OK, led->SetAnimation(led, HEARTBEAT_RED, BUILTIN_LED_PRIO_HIGH, 0));
    TEST_ASSERT_EQUAL(3, mocked_xQueueSend_fake.call_count);

    TEST_ASSERT_EQUAL(ESP_OK, led->StopAnimation(led));
    TEST_ASSERT_EQUAL(4, mocked_xQueueSend_fake.call_count);

    TEST_ASSERT_EQUAL(ESP_OK, led->SetBrightness(led, 128));
    TEST_ASSERT_EQUAL(5, mocked_xQueueSend_fake.call_count);

    TEST_ASSERT_EQUAL(ESP_OK, led->InitModule(
        led, &(BuiltinLEDConfig){.pin = 8, .max_brightness = 40}));
    TEST_ASSERT_EQUAL(6, mocked_xQueueSend_fake.call_count);

    BuiltinLEDStatus st;
    TEST_ASSERT_EQUAL(ESP_OK, led->GetStatus(led, &st));
}

/* ---------------------------------------------------------------------------
 * Test: Queue full → ESP_ERR_TIMEOUT
 * ------------------------------------------------------------------------- */

void test_BuiltinLED_QueueFull_ReturnsTimeout(void) {
    IBuiltinLED *led = BuiltinLED_GetModule();
    led->base.Initialize(&led->base, NULL);
    mocked_xQueueSend_fake.return_val = pdFALSE;

    TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT, led->TurnOn(led));
    TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT, led->TurnOff(led));
    TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT, led->StopAnimation(led));
    TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT,
                      led->SetAnimation(led, BLINK_WHITE, BUILTIN_LED_PRIO_LOW, 0));
    TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT, led->SetBrightness(led, 50));
    TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT, led->RegisterAnimation(led, HeartbeatAnim_Get()));
}

/* ---------------------------------------------------------------------------
 * Test: NULL argument guards
 * ------------------------------------------------------------------------- */

void test_BuiltinLED_NullArgs(void) {
    IBuiltinLED *led = BuiltinLED_GetModule();
    led->base.Initialize(&led->base, NULL);

    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, led->GetStatus(led, NULL));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, led->InitModule(led, NULL));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, led->RegisterAnimation(led, NULL));
}

/* ---------------------------------------------------------------------------
 * Test: Priority preemption
 * ------------------------------------------------------------------------- */

/**
 * HIGH priority must override MEDIUM; LOW must NOT override MEDIUM.
 */
void test_BuiltinLED_Priority(void) {
    builtinLEDService.status.current_animation = BLINK_WHITE;
    builtinLEDService.status.current_priority  = BUILTIN_LED_PRIO_MEDIUM;
    builtinLEDService.status.is_on             = true;
    builtinLEDService.defaultAnimType          = BLINK_WHITE;

    bool clr, px; LEDColor col;

    BuiltinLEDCmd high_cmd = {
        .type                      = CMD_SET_ANIMATION,
        .data.set_anim.anim_type   = SOS_RED,
        .data.set_anim.priority    = BUILTIN_LED_PRIO_HIGH,
        .data.set_anim.duration_ms = 0,
    };
    ProcessCommand(&high_cmd, &clr, &px, &col);
    TEST_ASSERT_EQUAL(SOS_RED,              builtinLEDService.status.current_animation);
    TEST_ASSERT_EQUAL(BUILTIN_LED_PRIO_HIGH, builtinLEDService.status.current_priority);

    BuiltinLEDCmd low_cmd = {
        .type                      = CMD_SET_ANIMATION,
        .data.set_anim.anim_type   = HEARTBEAT_RED,
        .data.set_anim.priority    = BUILTIN_LED_PRIO_LOW,
        .data.set_anim.duration_ms = 0,
    };
    ProcessCommand(&low_cmd, &clr, &px, &col);
    TEST_ASSERT_EQUAL(SOS_RED, builtinLEDService.status.current_animation); /* Unchanged */
}

/* ---------------------------------------------------------------------------
 * Test: Timed animation expiry
 * ------------------------------------------------------------------------- */

void test_BuiltinLED_TimedAnimationExpiry(void) {
    builtinLEDService.eventBus                         = (IEventBus *)&stub_event_bus;
    builtinLEDService.status.current_animation         = SOS_RED;
    builtinLEDService.status.current_priority          = BUILTIN_LED_PRIO_HIGH;
    builtinLEDService.status.remaining_ms              = 100;
    builtinLEDService.status.is_on                     = true;
    builtinLEDService.defaultAnimType                  = BLINK_WHITE;
    builtinLEDService.plugins[0]                       = BlinkWhiteAnim_Get();
    builtinLEDService.plugin_count                     = 1;

    UpdateDurationCountdown(200); /* 200 ms > 100 ms → expiry */

    TEST_ASSERT_EQUAL(BLINK_WHITE,          builtinLEDService.status.current_animation);
    TEST_ASSERT_EQUAL(BUILTIN_LED_PRIO_LOW, builtinLEDService.status.current_priority);
    TEST_ASSERT_EQUAL(0,                    builtinLEDService.status.remaining_ms);
    TEST_ASSERT_TRUE(builtinLEDService.status.is_on);
    TEST_ASSERT_GREATER_THAN(0, mock_publish_count);
}

/* ---------------------------------------------------------------------------
 * Test: Plugin duplicate detection
 * ------------------------------------------------------------------------- */

void test_BuiltinLED_RegisterPlugin_NoDuplicate(void) {
    builtinLEDService.plugin_count = 0;
    RegisterPlugin_Direct(HeartbeatAnim_Get());
    RegisterPlugin_Direct(HeartbeatAnim_Get()); /* Duplicate — must be silently ignored. */
    TEST_ASSERT_EQUAL(1, builtinLEDService.plugin_count);
}

/* ---------------------------------------------------------------------------
 * Test: Plugin table overflow guard
 * ------------------------------------------------------------------------- */

void test_BuiltinLED_RegisterPlugin_Overflow(void) {
    builtinLEDService.plugin_count = BUILTIN_LED_MAX_PLUGINS;
    RegisterPlugin_Direct(HeartbeatAnim_Get()); /* Must be silently rejected. */
    TEST_ASSERT_EQUAL(BUILTIN_LED_MAX_PLUGINS, builtinLEDService.plugin_count);
}

/* ---------------------------------------------------------------------------
 * Test: SettingsChangedHandler
 * ------------------------------------------------------------------------- */

static BuiltinLEDCmd captured_settings_command;
static BaseType_t capture_settings_command(QueueHandle_t queue, const void *item, TickType_t wait) {
    (void)queue; (void)wait;
    captured_settings_command = *(const BuiltinLEDCmd *)item;
    return pdTRUE;
}
void test_BuiltinLED_GlobalDisableOverridesNotificationsAndReenableRestarts(void) {
    IBuiltinLED *led = BuiltinLED_GetModule();
    led->base.Initialize(&led->base, NULL);
    builtinLEDService.status.current_animation = SOLID_GREEN;
    builtinLEDService.status.current_priority = BUILTIN_LED_PRIO_HIGH;
    builtinLEDService.status.remaining_ms = 3000;
    builtinLEDService.status.is_on = true;
    mocked_xQueueSend_fake.custom_fake = capture_settings_command;
    fake_settings.hardware.builtin_led_anim_mode = 0;
    SettingsChangedHandler((BusEvent){.eventName = EVENT_CONFIG_CHANGED});
    bool clear = false, pixel = false;
    LEDColor color = {0};
    ProcessCommand(&captured_settings_command, &clear, &pixel, &color);
    TEST_ASSERT_FALSE(builtinLEDService.status.is_on);
    TEST_ASSERT_EQUAL(BUILTIN_LED_ANIM_NONE, builtinLEDService.status.current_animation);
    BuiltinLEDCmd notification = {.type = CMD_SET_ANIMATION,
        .data.set_anim = {.anim_type = POLICE_STROBE, .priority = BUILTIN_LED_PRIO_HIGH}};
    ProcessCommand(&notification, &clear, &pixel, &color);
    TEST_ASSERT_FALSE(builtinLEDService.status.is_on);
    fake_settings.hardware.builtin_led_anim_mode = 1;
    fake_settings.hardware.builtin_led_idle_anim = DOUBLE_BLINK_WHITE;
    SettingsChangedHandler((BusEvent){.eventName = EVENT_CONFIG_CHANGED});
    ProcessCommand(&captured_settings_command, &clear, &pixel, &color);
    TEST_ASSERT_TRUE(builtinLEDService.status.is_on);
    TEST_ASSERT_EQUAL(DOUBLE_BLINK_WHITE, builtinLEDService.status.current_animation);
    TEST_ASSERT_EQUAL(BUILTIN_LED_PRIO_LOW, builtinLEDService.status.current_priority);
    TEST_ASSERT_EQUAL(0, s_ctx.timer);
}

void test_BuiltinLED_PersistentBrightness(void) {
    fake_settings.hardware.builtin_led_brightness = 37;
    IBuiltinLED *led = BuiltinLED_GetModule();
    TEST_ASSERT_EQUAL(0, led->base.Initialize(&led->base, NULL));
    TEST_ASSERT_EQUAL(37, builtinLEDService.status.brightness);
    mocked_xQueueSend_fake.custom_fake = capture_settings_command;
    fake_settings.hardware.builtin_led_brightness = 0;
    SettingsChangedHandler((BusEvent){.eventName = EVENT_CONFIG_CHANGED});
    fake_settings.hardware.builtin_led_brightness = 200;
    bool clear = false, pixel = false;
    LEDColor color = {0};
    ProcessCommand(&captured_settings_command, &clear, &pixel, &color);
    TEST_ASSERT_EQUAL(0, builtinLEDService.status.brightness);
    TEST_ASSERT_EQUAL(0, ApplyBrightnessAt((LEDColor){255,255,255},builtinLEDService.max_brightness).r);
    SettingsChangedHandler((BusEvent){.eventName = EVENT_CONFIG_CHANGED});
    ProcessCommand(&captured_settings_command, &clear, &pixel, &color);
    TEST_ASSERT_EQUAL(200, ApplyBrightnessAt((LEDColor){BUILTIN_LED_DEFAULT_BRIGHTNESS,0,0},builtinLEDService.max_brightness).r);
}

static unsigned brightness_saves;
static esp_err_t brightness_save_result;
static esp_err_t save_brightness_field(IConfigurationManager *self, const char *block,
        const ConfigurationFieldUpdate *fields, size_t count, bool persist) {
    (void)self;
    TEST_ASSERT_EQUAL_STRING("hardware", block);
    TEST_ASSERT_EQUAL(1, count);
    TEST_ASSERT_TRUE(persist);
    TEST_ASSERT_EQUAL(offsetof(HardwareConfigs, builtin_led_brightness), fields[0].offset);
    TEST_ASSERT_EQUAL(1, fields[0].size);
    brightness_saves++;
    if (brightness_save_result == ESP_OK)
        fake_settings.hardware.builtin_led_brightness = *(const uint8_t *)fields[0].data;
    return brightness_save_result;
}
void test_BuiltinLED_BrightnessInputAndSaveFailure(void) {
    stub_config_mgr.UpdateFields = save_brightness_field;
    brightness_saves = 0;
    brightness_save_result = ESP_OK;
    const char *invalid[] = {"", "-1", "+2", "256", "999999999999", "2.5", " 20", "20x"};
    for (size_t i = 0; i < sizeof(invalid)/sizeof(invalid[0]); ++i)
        TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, BuiltinLED_SaveBrightnessText(invalid[i]));
    TEST_ASSERT_EQUAL(0, brightness_saves);
    TEST_ASSERT_EQUAL(ESP_OK, BuiltinLED_SaveBrightnessText("0"));
    TEST_ASSERT_EQUAL(0, fake_settings.hardware.builtin_led_brightness);
    TEST_ASSERT_EQUAL(ESP_OK, BuiltinLED_SaveBrightnessText("255"));
    TEST_ASSERT_EQUAL(255, fake_settings.hardware.builtin_led_brightness);
    brightness_save_result = ESP_FAIL;
    TEST_ASSERT_EQUAL(ESP_FAIL, BuiltinLED_SaveBrightnessText("10"));
    TEST_ASSERT_EQUAL(255, fake_settings.hardware.builtin_led_brightness);
    char *args[] = {"builtin_led_brightness", "10"};
    TEST_ASSERT_EQUAL(1, cmd_builtin_led_brightness(2, args));
    stub_config_mgr.UpdateFields = NULL;
}

static esp_err_t mode_save_result;
static unsigned mode_saves;
static esp_err_t save_mode_field(IConfigurationManager *self, const char *block,
        const ConfigurationFieldUpdate *fields, size_t count, bool persist) {
    (void)self;
    TEST_ASSERT_EQUAL_STRING("hardware", block);
    TEST_ASSERT_EQUAL(1, count);
    TEST_ASSERT_TRUE(persist);
    TEST_ASSERT_EQUAL(offsetof(HardwareConfigs, builtin_led_anim_mode), fields[0].offset);
    TEST_ASSERT_EQUAL(1, fields[0].size);
    mode_saves++;
    if (mode_save_result == ESP_OK)
        fake_settings.hardware.builtin_led_anim_mode = *(const uint8_t *)fields[0].data;
    return mode_save_result;
}
void test_BuiltinLED_GlobalModeCLIAndPersistenceErrors(void) {
    stub_config_mgr.UpdateFields = save_mode_field;
    mode_save_result = ESP_OK;
    mode_saves = 0;
    char *off[] = {"builtin_led_off"};
    char *on[] = {"builtin_led_on"};
    TEST_ASSERT_EQUAL(0, cmd_builtin_led_off(1, off));
    TEST_ASSERT_EQUAL(0, fake_settings.hardware.builtin_led_anim_mode);
    TEST_ASSERT_EQUAL(0, cmd_builtin_led_on(1, on));
    TEST_ASSERT_EQUAL(1, fake_settings.hardware.builtin_led_anim_mode);
    char *bled_off[] = {"bled", "off"};
    char *bled_on[] = {"bled", "on"};
    TEST_ASSERT_EQUAL(0, cmd_bled_handler(2, bled_off));
    TEST_ASSERT_EQUAL(0, fake_settings.hardware.builtin_led_anim_mode);
    TEST_ASSERT_EQUAL(0, cmd_bled_handler(2, bled_on));
    TEST_ASSERT_EQUAL(1, fake_settings.hardware.builtin_led_anim_mode);
    TEST_ASSERT_EQUAL(4, mode_saves);
    mode_save_result = ESP_FAIL;
    TEST_ASSERT_NOT_EQUAL(0, cmd_builtin_led_off(1, off));
    TEST_ASSERT_NOT_EQUAL(0, cmd_bled_handler(2, bled_off));
    TEST_ASSERT_EQUAL(1, fake_settings.hardware.builtin_led_anim_mode);
    stub_config_mgr.UpdateFields = NULL;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, BuiltinLED_SaveEnabled(false));
}
void test_BuiltinLED_StartupRespectsPersistedDisabledMode(void) {
    fake_settings.hardware.builtin_led_anim_mode = 0;
    fake_settings.hardware.builtin_led_idle_anim = DOUBLE_BLINK_WHITE;
    IBuiltinLED *led = BuiltinLED_GetModule();
    TEST_ASSERT_EQUAL(0, led->base.Initialize(&led->base, NULL));
    TEST_ASSERT_FALSE(builtinLEDService.status.is_on);
    TEST_ASSERT_TRUE(builtinLEDService.disabled);
    TEST_ASSERT_EQUAL(BUILTIN_LED_ANIM_NONE, builtinLEDService.status.current_animation);
    TEST_ASSERT_EQUAL(0, builtinLEDService.targetFrame.r);
}

static unsigned reset_count;
static void count_animation_reset(AnimationPlugin *plugin) { (void)plugin; reset_count++; }
void test_BuiltinLED_UnrelatedSettingsDuringOffFramePreservePhase(void) {
    IBuiltinLED *led=BuiltinLED_GetModule();
    TEST_ASSERT_EQUAL(0,led->base.Initialize(&led->base,NULL));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,led->SetStartupActive(NULL,true));
    mocked_xQueueSend_fake.return_val=pdFALSE;
    TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT,led->SetStartupActive(led,true));
    TEST_ASSERT_FALSE(builtinLEDService.startupActive);
    mocked_xQueueSend_fake.return_val=pdTRUE;
    TEST_ASSERT_EQUAL(ESP_OK,led->SetStartupActive(led,true));
    TEST_ASSERT_FALSE(builtinLEDService.startupActive); /* Admission is not worker application. */
    AnimationPlugin *plugin=FindPlugin(DOUBLE_BLINK_WHITE);
    TEST_ASSERT_NOT_NULL(plugin);
    void (*original_reset)(AnimationPlugin *)=plugin->Reset;
    plugin->Reset=count_animation_reset;
    builtinLEDService.defaultAnimType=DOUBLE_BLINK_WHITE;
    builtinLEDService.status.current_animation=DOUBLE_BLINK_WHITE;
    builtinLEDService.status.current_priority=BUILTIN_LED_PRIO_LOW;
    builtinLEDService.status.is_on=false; /* A valid OFF frame is still active playback. */
    reset_count=0;
    BuiltinLEDCmd cmd={.type=CMD_SETTINGS_CHANGED,.data.settings={.default_anim=DOUBLE_BLINK_WHITE,.brightness=71}};
    bool clear=false,pixel=false;LEDColor color={0};
    ProcessCommand(&cmd,&clear,&pixel,&color);
    unsigned observed=reset_count;plugin->Reset=original_reset;
    TEST_ASSERT_EQUAL(0,observed);
    TEST_ASSERT_EQUAL(DOUBLE_BLINK_WHITE,builtinLEDService.status.current_animation);
}
void test_BuiltinLED_BrightnessPreservesAnimationPhase(void) {
    IBuiltinLED *led = BuiltinLED_GetModule();
    led->base.Initialize(&led->base, NULL);
    AnimationPlugin *plugin = FindPlugin(SOLID_RED);
    TEST_ASSERT_NOT_NULL(plugin);
    void (*original_reset)(AnimationPlugin *) = plugin->Reset;
    plugin->Reset = count_animation_reset;
    builtinLEDService.defaultAnimType = SOLID_RED;
    builtinLEDService.status.current_animation = SOLID_RED;
    builtinLEDService.status.current_priority = BUILTIN_LED_PRIO_LOW;
    reset_count = 0;
    BuiltinLEDCmd cmd = {.type = CMD_SETTINGS_CHANGED,
        .data.settings = {.default_anim = SOLID_RED, .brightness = 71}};
    bool clear = false, pixel = false; LEDColor color = {0};
    ProcessCommand(&cmd, &clear, &pixel, &color);
    unsigned actual_resets = reset_count;
    plugin->Reset = original_reset;
    TEST_ASSERT_EQUAL(0, actual_resets);
    TEST_ASSERT_EQUAL(71, builtinLEDService.status.brightness);
    TEST_ASSERT_EQUAL(SOLID_RED, builtinLEDService.status.current_animation);
}

void test_BuiltinLED_SettingsChangedHandler(void) {
    IBuiltinLED *led = BuiltinLED_GetModule();
    led->base.Initialize(&led->base, NULL);

    BusEvent ev = {.eventName = EVENT_CONFIG_CHANGED};

    /* Auto mode → queues CMD_SETTINGS_CHANGED. */
    fake_settings.hardware.builtin_led_anim_mode = 0;
    SettingsChangedHandler(ev);
    TEST_ASSERT_EQUAL(1, mocked_xQueueSend_fake.call_count);

    /* Manual mode → queues CMD_SETTINGS_CHANGED with specified type. */
    fake_settings.hardware.builtin_led_anim_mode   = 1;
    fake_settings.hardware.builtin_led_startup_anim = SOS_RED;
    SettingsChangedHandler(ev);
    TEST_ASSERT_EQUAL(2, mocked_xQueueSend_fake.call_count);

    /* Wrong event name → must be completely ignored. */
    BusEvent wrong = {.eventName = "wrong.event"};
    SettingsChangedHandler(wrong);
    TEST_ASSERT_EQUAL(2, mocked_xQueueSend_fake.call_count); /* Unchanged */
}

/* ---------------------------------------------------------------------------
 * Test: Stop() unsubscribes from EventBus
 * ------------------------------------------------------------------------- */

void test_BuiltinLED_Stop_Unsubscribes(void) {
    IBuiltinLED *led     = BuiltinLED_GetModule();
    IService    *service = &led->base;

    service->Initialize(service, NULL);
    service->Start(service);
    builtinLEDService.animTaskHandle = (TaskHandle_t)(uintptr_t)0x1234;
    builtinLEDService.state          = SERVICE_STATE_STARTED;

    service->Stop(service);

    TEST_ASSERT_EQUAL(7, mock_unsubscribe_count);  // Includes disconnect cancellation.
}

/* ---------------------------------------------------------------------------
 * Test: Console commands registered
 * ------------------------------------------------------------------------- */

void test_BuiltinLED_ConsoleCommands_Registered(void) {
    IBuiltinLED *led = BuiltinLED_GetModule();
    led->base.Initialize(&led->base, NULL);
    led->base.Start(&led->base);

    TEST_ASSERT_EQUAL(9, mock_cmd_count);
}

/* ---------------------------------------------------------------------------
 * Test: ProcessCommand hardware flags
 * ------------------------------------------------------------------------- */

void test_BuiltinLED_TurnOn_TurnOff_HardwareFlags(void) {
    bool clr, px; LEDColor col;

    builtinLEDService.max_brightness = BUILTIN_LED_DEFAULT_BRIGHTNESS;

    BuiltinLEDCmd on_cmd  = {.type = CMD_TURN_ON};
    ProcessCommand(&on_cmd, &clr, &px, &col);
    TEST_ASSERT_TRUE(px);
    TEST_ASSERT_FALSE(clr);
    TEST_ASSERT_TRUE(builtinLEDService.status.is_on);

    BuiltinLEDCmd off_cmd = {.type = CMD_TURN_OFF};
    ProcessCommand(&off_cmd, &clr, &px, &col);
    TEST_ASSERT_TRUE(clr);
    TEST_ASSERT_FALSE(px);
    TEST_ASSERT_FALSE(builtinLEDService.status.is_on);
}

/* ---------------------------------------------------------------------------
 * Test: Brightness scaling
 * ------------------------------------------------------------------------- */

void test_BuiltinLED_ApplyBrightness(void) {
    builtinLEDService.max_brightness = BUILTIN_LED_DEFAULT_BRIGHTNESS * 2;

    LEDColor raw    = {BUILTIN_LED_DEFAULT_BRIGHTNESS, 0, 0};
    LEDColor scaled = ApplyBrightnessAt(raw,builtinLEDService.max_brightness);

    TEST_ASSERT_EQUAL((uint8_t)(BUILTIN_LED_DEFAULT_BRIGHTNESS * 2), scaled.r);
    TEST_ASSERT_EQUAL(0, scaled.g);
    TEST_ASSERT_EQUAL(0, scaled.b);
}

void test_BuiltinLED_ApplyBrightness_Clamp(void) {
    builtinLEDService.max_brightness = 255;

    LEDColor raw    = {BUILTIN_LED_DEFAULT_BRIGHTNESS, BUILTIN_LED_DEFAULT_BRIGHTNESS,
                       BUILTIN_LED_DEFAULT_BRIGHTNESS};
    LEDColor scaled = ApplyBrightnessAt(raw,builtinLEDService.max_brightness);

    TEST_ASSERT_LESS_OR_EQUAL(255, scaled.r);
    TEST_ASSERT_LESS_OR_EQUAL(255, scaled.g);
    TEST_ASSERT_LESS_OR_EQUAL(255, scaled.b);
}

/* ---------------------------------------------------------------------------
 * Test: Self-test — all 14 built-in plugins
 * ------------------------------------------------------------------------- */

void test_BuiltinLED_SelfTest_AllAnimations(void) {
    AnimationPlugin *plugins[] = {
        HeartbeatAnim_Get(),
        BlinkWhiteAnim_Get(),
        SOSRedAnim_Get(),
        BreathingGreenAnim_Get(),
        FadeBlueAnim_Get(),
        StrobeWhiteAnim_Get(),
        RainbowFlowAnim_Get(),
        PoliceStrobeAnim_Get(),
        SolidRedAnim_Get(),
        SolidGreenAnim_Get(),
        SolidBlueAnim_Get(),
        SolidWhiteAnim_Get(),
        BlinkRedAnim_Get(),
        BreathingRedAnim_Get(),
        DoubleBlinkWhiteAnim_Get(),
    };
    size_t count = sizeof(plugins) / sizeof(plugins[0]);

    for (size_t i = 0; i < count; i++) {
        AnimationPlugin *p = plugins[i];
        TEST_ASSERT_NOT_NULL(p);
        TEST_ASSERT_NOT_NULL(p->name);
        TEST_ASSERT_NOT_NULL(p->Reset);
        TEST_ASSERT_NOT_NULL(p->Update);

        p->Reset(p);
        LEDColor c = p->Update(p, 20);

        TEST_ASSERT_LESS_OR_EQUAL(255, c.r);
        TEST_ASSERT_LESS_OR_EQUAL(255, c.g);
        TEST_ASSERT_LESS_OR_EQUAL(255, c.b);
    }
}

/* ---------------------------------------------------------------------------
 * Test: FindPlugin miss path
 * ------------------------------------------------------------------------- */

void test_BuiltinLED_FindPlugin_NotFound(void) {
    builtinLEDService.plugin_count = 0;
    AnimationPlugin *p = FindPlugin(HEARTBEAT_RED);
    TEST_ASSERT_NULL(p);
}

/* ---------------------------------------------------------------------------
 * Entry point
 * ------------------------------------------------------------------------- */

void test_BuiltinLED_TraceCapturesEdgesCommandsAndGapsWithoutChangingAnimation(void) {
    BuiltinLED_GetModule();
    char *start[] = {"builtin_led_trace", "start"};
    char *stop[] = {"builtin_led_trace", "stop"};
    builtinLEDService.status.current_animation = DOUBLE_BLINK_WHITE;
    TEST_ASSERT_EQUAL(0, cmd_builtin_led_trace(2, start));
    LEDFrame on = {20, 20, 20, 255};
    TraceFrame(1000, 0, 0, -1, on, 2, 3);
    TraceFrame(21000, 20, 20, -1, on, 2, 3);
    TEST_ASSERT_EQUAL(1, ledTrace.count);
    TraceFrame(41000, 20, 0, CMD_SETTINGS_CHANGED, on, 2, 3);
    TraceFrame(101000, 60, 60, -1, on, 2, 3);
    TEST_ASSERT_EQUAL(3, ledTrace.count);
    TEST_ASSERT_EQUAL(4, ledTrace.samples);
    TEST_ASSERT_EQUAL(1, ledTrace.commands);
    TEST_ASSERT_EQUAL(80, ledTrace.advanced_ms);
    TEST_ASSERT_EQUAL(60000, ledTrace.max_gap_us);
    TEST_ASSERT_EQUAL(DOUBLE_BLINK_WHITE, builtinLEDService.status.current_animation);
    TEST_ASSERT_EQUAL(0, cmd_builtin_led_trace(2, stop));
    TEST_ASSERT_NULL(ledTrace.pages[0]);
    TEST_ASSERT_FALSE(ledTrace.enabled);
}
void test_BuiltinLED_TraceOverflowIsReportedAndBadCommandPreservesCapture(void) {
    BuiltinLED_GetModule();
    char *start[] = {"builtin_led_trace", "start"};
    char *invalid[] = {"builtin_led_trace", "bad"};
    char *stop[] = {"builtin_led_trace", "stop"};
    TEST_ASSERT_EQUAL(0, cmd_builtin_led_trace(2, start));
    for (unsigned i = 0; i < BUILTIN_LED_TRACE_CAPACITY + 3; ++i)
        TraceFrame(1000 + i * 20000, 20, 20, CMD_SETTINGS_CHANGED, (LEDFrame){0}, 0, 0);
    TEST_ASSERT_EQUAL(BUILTIN_LED_TRACE_CAPACITY, ledTrace.count);
    TEST_ASSERT_EQUAL(3, ledTrace.dropped);
    TEST_ASSERT_EQUAL(1, cmd_builtin_led_trace(2, invalid));
    TEST_ASSERT_TRUE(ledTrace.enabled);
    TEST_ASSERT_EQUAL(0, cmd_builtin_led_trace(2, stop));
}

void test_BuiltinLED_TraceStatusAndRepeatedStartPreserveOwnedCapture(void) {
    BuiltinLED_GetModule();
    char *start[] = {"builtin_led_trace", "start"};
    char *status[] = {"builtin_led_trace", "status"};
    char *stop[] = {"builtin_led_trace", "stop"};
    TEST_ASSERT_EQUAL(0, cmd_builtin_led_trace(2, start));
    TraceFrame(1000, 20, 20, CMD_SETTINGS_CHANGED, (LEDFrame){0}, 0, 0);
    BuiltinLEDTraceRow *rows = ledTrace.pages[0];
    uint32_t count = ledTrace.count;
    TEST_ASSERT_EQUAL(0, cmd_builtin_led_trace(2, status));
    TEST_ASSERT_EQUAL_PTR(rows, ledTrace.pages[0]);
    TEST_ASSERT_EQUAL(count, ledTrace.count);
    TEST_ASSERT_EQUAL(1, cmd_builtin_led_trace(2, start));
    TEST_ASSERT_EQUAL_PTR(rows, ledTrace.pages[0]);
    TEST_ASSERT_EQUAL(count, ledTrace.count);
    TEST_ASSERT_EQUAL(0, cmd_builtin_led_trace(2, stop));
    TEST_ASSERT_EQUAL(0, cmd_builtin_led_trace(2, status));
    TEST_ASSERT_NULL(ledTrace.pages[0]);
}

void test_BuiltinLED_TracePagesRollbackAndPreserveFullCapacity(void) {
    BuiltinLED_GetModule();
    char *start[] = {"builtin_led_trace", "start"};
    char *stop[] = {"builtin_led_trace", "stop"};
    trace_fail_at = 3;
    TEST_ASSERT_EQUAL(1, cmd_builtin_led_trace(2, start));
    TEST_ASSERT_EQUAL(2, trace_frees);
    TEST_ASSERT_NULL(ledTrace.pages[0]);
    TEST_ASSERT_FALSE(ledTrace.enabled);
    trace_fail_at = 0;
    trace_allocation_limit = BUILTIN_LED_TRACE_PAGE_ROWS * sizeof(BuiltinLEDTraceRow);
    TEST_ASSERT_EQUAL(0, cmd_builtin_led_trace(2, start));
    unsigned allocations = trace_allocations;
    TEST_ASSERT_EQUAL(1, cmd_builtin_led_trace(2, start));
    TEST_ASSERT_EQUAL(allocations, trace_allocations);
    for (unsigned i = 0; i < BUILTIN_LED_TRACE_CAPACITY; ++i)
        TraceFrame(1000 + i * 20000, 20, 20, CMD_SETTINGS_CHANGED, (LEDFrame){0}, 0, 0);
    TEST_ASSERT_EQUAL(BUILTIN_LED_TRACE_CAPACITY, ledTrace.count);
    TEST_ASSERT_EQUAL(0, ledTrace.dropped);
    TEST_ASSERT_TRUE(TraceRowAt(&ledTrace, BUILTIN_LED_TRACE_CAPACITY - 1)->time_us ==
        1000 + (BUILTIN_LED_TRACE_CAPACITY - 1) * 20000);
    TEST_ASSERT_EQUAL(0, cmd_builtin_led_trace(2, stop));
    TEST_ASSERT_EQUAL(2 + BUILTIN_LED_TRACE_PAGE_COUNT, trace_frees);
}

void test_BuiltinLED_DoubleBlinkDefinitionOneHundredCycles(void) {
    AnimationPlugin *plugin = DoubleBlinkWhiteAnim_Get();
    plugin->Reset(plugin);
    unsigned edges = 0;
    bool previous = true;
    for (unsigned time = 1; time <= 200000; ++time) {
        LEDColor color = plugin->Update(plugin, 1);
        unsigned phase = time % 2000;
        bool expected = phase < 80 || (phase >= 200 && phase < 280);
        bool on = color.r != 0;
        TEST_ASSERT_EQUAL(expected, on);
        TEST_ASSERT_EQUAL(color.r, color.g);
        TEST_ASSERT_EQUAL(color.r, color.b);
        if (on != previous) edges++;
        previous = on;
    }
    TEST_ASSERT_EQUAL(400, edges);
}

static jmp_buf timing_exit;
static unsigned timing_samples;
static unsigned timing_mode;
static BaseType_t timing_queue(QueueHandle_t queue, void *output, TickType_t wait) {
    (void)queue; (void)wait;
    if (timing_samples == 10000) longjmp(timing_exit, 1);
    timing_samples++;
    mocked_esp_timer_get_time_fake.return_val += 20500;
    if (timing_mode == 5 && timing_samples == 9000) {
        *(BuiltinLEDCmd *)output = (BuiltinLEDCmd){.type = CMD_TURN_OFF};
        return pdTRUE;
    }
    if (((timing_mode == 4 || timing_mode == 5) && timing_samples == 10000) ||
        (timing_mode == 2 && timing_samples % 97 == 0)) {
        *(BuiltinLEDCmd *)output = (BuiltinLEDCmd){.type = CMD_SET_ANIMATION,
            .data.set_anim = {.anim_type = DOUBLE_BLINK_WHITE, .priority = BUILTIN_LED_PRIO_LOW}};
        return pdTRUE;
    }
    if ((timing_mode == 1 || timing_mode == 3) && timing_samples % 97 == 0) {
        *(BuiltinLEDCmd *)output = (BuiltinLEDCmd){.type = CMD_SET_BRIGHTNESS,
            .data.set_brightness.brightness = BUILTIN_LED_DEFAULT_BRIGHTNESS};
        return pdTRUE;
    }
    return pdFALSE;
}
static void run_timing_worker(unsigned mode) {
    BuiltinLED_GetModule();
    builtinLEDService.plugin_count = 0;
    RegisterPlugin_Direct(DoubleBlinkWhiteAnim_Get());
    DoubleBlinkWhiteAnim_Get()->Reset(DoubleBlinkWhiteAnim_Get());
    builtinLEDService.status.is_on = true;
    builtinLEDService.status.current_animation = DOUBLE_BLINK_WHITE;
    builtinLEDService.status.remaining_ms = mode == 6 ? 50 : (mode == 3 ? 1000 : 0);
    builtinLEDService.status.current_priority = mode == 2 ? BUILTIN_LED_PRIO_HIGH : BUILTIN_LED_PRIO_LOW;
    builtinLEDService.defaultAnimType = mode == 6 ? BUILTIN_LED_ANIM_NONE : DOUBLE_BLINK_WHITE;
    builtinLEDService.max_brightness = BUILTIN_LED_DEFAULT_BRIGHTNESS;
    builtinLEDService.status.brightness = BUILTIN_LED_DEFAULT_BRIGHTNESS;
    timing_samples = 0;
    timing_mode = mode;
    mocked_esp_timer_get_time_fake.return_val = 0;
    mocked_xQueueReceive_fake.custom_fake = timing_queue;
    if (setjmp(timing_exit) == 0) BuiltinLED_AnimTask(NULL);
    TEST_ASSERT_EQUAL(10000, timing_samples);
    /* 205 seconds = 102 complete cycles plus phase 1000 ms. */
    if (mode == 6) {
        TEST_ASSERT_FALSE(builtinLEDService.status.is_on);
        TEST_ASSERT_EQUAL(0, builtinLEDService.targetFrame.r);
        TEST_ASSERT_EQUAL(0, builtinLEDService.targetFrame.g);
        TEST_ASSERT_EQUAL(0, builtinLEDService.targetFrame.b);
        return;
    }
    TEST_ASSERT_EQUAL(mode >= 3 ? 0 : 1000, s_ctx.timer);
    TEST_ASSERT_TRUE(builtinLEDService.status.is_on);
    TEST_ASSERT_EQUAL(DOUBLE_BLINK_WHITE, builtinLEDService.status.current_animation);
    if (mode == 2) TEST_ASSERT_EQUAL(BUILTIN_LED_PRIO_HIGH, builtinLEDService.status.current_priority);
    if (mode == 3) TEST_ASSERT_EQUAL(0, builtinLEDService.status.remaining_ms);
}
void test_BuiltinLED_WorkerExpiryRestoresDisabledDefault(void) { run_timing_worker(6); }
void test_BuiltinLED_WorkerIgnoresLowPriorityWithoutPausing(void) { run_timing_worker(2); }
void test_BuiltinLED_WorkerCountsOverrideTimeDuringCommands(void) { run_timing_worker(3); }
void test_BuiltinLED_WorkerNewSelectionStartsAtZero(void) { run_timing_worker(4); }
void test_BuiltinLED_WorkerOffThenSelectionStartsAtZero(void) { run_timing_worker(5); }
void test_BuiltinLED_WorkerPreservesFractionalElapsedTime(void) { run_timing_worker(false); }
void test_BuiltinLED_WorkerPreservesElapsedTimeOnCommands(void) { run_timing_worker(true); }

void test_BuiltinLED_AllTimedPatternsLongRunAndOverflow(void) {
    struct { AnimationPlugin *plugin; uint32_t period; } cases[] = {
        {HeartbeatAnim_Get(), 1000}, {BlinkWhiteAnim_Get(), 1000},
        {SOSRedAnim_Get(), 4800}, {BreathingGreenAnim_Get(), 4000},
        {FadeBlueAnim_Get(), 3000}, {StrobeWhiteAnim_Get(), 100},
        {RainbowFlowAnim_Get(), RAINBOW_CYCLE_DURATION_MS}, {PoliceStrobeAnim_Get(), 1000},
        {BlinkRedAnim_Get(), 1000}, {BreathingRedAnim_Get(), 4000},
        {DoubleBlinkWhiteAnim_Get(), 2000}
    };
    for (unsigned i = 0; i < sizeof(cases)/sizeof(cases[0]); ++i) {
        AnimationPlugin *plugin = cases[i].plugin;
        plugin->Reset(plugin);
        uint64_t elapsed = 0;
        /* Irregular sampling crosses both step and loop boundaries. */
        while (elapsed < 100ULL * cases[i].period) {
            plugin->Update(plugin, 37);
            elapsed += 37;
            uint32_t phase;
            memcpy(&phase, plugin->context, sizeof(phase));
            TEST_ASSERT_EQUAL_UINT32_MESSAGE(elapsed % cases[i].period, phase, plugin->name);
        }
        plugin->Update(plugin, UINT32_MAX);
        elapsed += UINT32_MAX;
        uint32_t phase;
        memcpy(&phase, plugin->context, sizeof(phase));
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(elapsed % cases[i].period, phase, plugin->name);
    }
}

void test_BuiltinLED_RainbowIsIndependentOfExternalLED(void) {
    AnimationPlugin *plugin = RainbowFlowAnim_Get();
    plugin->Reset(plugin);
    LEDColor expected = plugin->Update(plugin, 300);
    plugin->Reset(plugin);
    RainbowFlowAnim_Update(701, 255);
    LEDColor actual = plugin->Update(plugin, 300);
    TEST_ASSERT_EQUAL_MEMORY(&expected, &actual, sizeof(expected));
    RainbowFlowAnim_Reset();
    expected = plugin->Update(plugin, 0);
    TEST_ASSERT_EQUAL_MEMORY(&actual, &expected, sizeof(actual));
}

void test_BuiltinLED_SOSContainsNinePulsesForOneHundredCycles(void) {
    AnimationPlugin *plugin = SOSRedAnim_Get();
    plugin->Reset(plugin);
    for (unsigned cycle = 0; cycle < 100; ++cycle) {
        unsigned pulses = 0;
        bool previous = true;
        for (unsigned t = 0; t < 4800; ++t) {
            LEDColor color = plugin->Update(plugin, 1);
            bool on = color.r != 0;
            if (on && !previous) pulses++;
            previous = on;
        }
        /* Count rising edges after the initial frame, including the loop boundary. */
        TEST_ASSERT_EQUAL(9, pulses);
    }
}

void test_BuiltinLED_AllocationFailureDoesNotPublishInitialized(void) {
    IBuiltinLED *led = BuiltinLED_GetModule();
    builtinLEDService.mutex = NULL;
    builtinLEDService.cmd_queue = NULL;
    mocked_xSemaphoreCreateMutex_fake.return_val = NULL;
    TEST_ASSERT_NOT_EQUAL(0, led->base.Initialize(&led->base, NULL));
    TEST_ASSERT_EQUAL(SERVICE_STATE_UNINITIALIZED, builtinLEDService.state);
}

void test_BuiltinLED_TaskCreationFailureReleasesDriver(void) {
    IBuiltinLED *led = BuiltinLED_GetModule();
    TEST_ASSERT_EQUAL(0, led->base.Initialize(&led->base, NULL));
    mocked_xTaskCreate_fake.return_val = pdFAIL;
    TEST_ASSERT_NOT_EQUAL(0, led->base.Start(&led->base));
    TEST_ASSERT_NULL(builtinLEDService.led_strip);
    TEST_ASSERT_EQUAL(1, strip_delete_calls);
    TEST_ASSERT_EQUAL(SERVICE_STATE_INITIALIZED, builtinLEDService.state);
}

void test_BuiltinLED_UnsubscribeBusyPreservesResourcesForRetry(void) {
    IBuiltinLED *led = BuiltinLED_GetModule();
    TEST_ASSERT_EQUAL(0, led->base.Initialize(&led->base, NULL));
    TEST_ASSERT_EQUAL(0, led->base.Start(&led->base));
    builtinLEDService.animTaskHandle = (TaskHandle_t)(uintptr_t)0x1234;
    unsubscribe_fail_at = 2;
    TEST_ASSERT_NOT_EQUAL(0, led->base.Stop(&led->base));
    TEST_ASSERT_NOT_NULL(builtinLEDService.led_strip);
    /* The worker acknowledged stop; callback ownership still retains the driver. */
    TEST_ASSERT_NULL(builtinLEDService.animTaskHandle);
    TEST_ASSERT_EQUAL(0, strip_delete_calls);
    TEST_ASSERT_EQUAL(1, mocked_vTaskDelete_fake.call_count);
    TEST_ASSERT_NOT_EQUAL(0, led->base.Start(&led->base));
    unsubscribe_fail_at = 0;
    TEST_ASSERT_EQUAL(0, led->base.Stop(&led->base));
    TEST_ASSERT_EQUAL(8, mock_unsubscribe_count);
    TEST_ASSERT_EQUAL(1, strip_delete_calls);
}

void test_BuiltinLED_SubscriptionFailureRollsBackOnlyOwnedEntries(void) {
    IBuiltinLED *led = BuiltinLED_GetModule();
    TEST_ASSERT_EQUAL(0, led->base.Initialize(&led->base, NULL));
    subscribe_fail_at = 3;
    TEST_ASSERT_NOT_EQUAL(0, led->base.Start(&led->base));
    TEST_ASSERT_EQUAL(3, mock_subscribe_count);
    TEST_ASSERT_EQUAL(2, mock_unsubscribe_count);
    TEST_ASSERT_NULL(builtinLEDService.led_strip);
    TEST_ASSERT_NOT_EQUAL(SERVICE_STATE_STARTED, builtinLEDService.state);
    subscribe_fail_at = 0;
    TEST_ASSERT_EQUAL(0, led->base.Start(&led->base));
}

void test_BuiltinLED_PartialDriverTeardownCannotBeRetried(void) {
    IBuiltinLED *led = BuiltinLED_GetModule();
    TEST_ASSERT_EQUAL(0, led->base.Initialize(&led->base, NULL));
    TEST_ASSERT_EQUAL(0, led->base.Start(&led->base));
    strip_delete_result = ESP_FAIL;
    TEST_ASSERT_NOT_EQUAL(0, led->base.Stop(&led->base));
    TEST_ASSERT_EQUAL(SERVICE_STATE_ERROR, builtinLEDService.state);
    TEST_ASSERT_NOT_EQUAL(0, led->base.Stop(&led->base));
    TEST_ASSERT_NOT_EQUAL(0, led->base.Start(&led->base));
    TEST_ASSERT_EQUAL(1, strip_delete_calls);
}

void test_BuiltinLED_StopWithoutWorkerAcknowledgmentPreservesOwnership(void) {
    IBuiltinLED *led = BuiltinLED_GetModule();
    TEST_ASSERT_EQUAL(0, led->base.Initialize(&led->base, NULL));
    TEST_ASSERT_EQUAL(0, led->base.Start(&led->base));
    builtinLEDService.animTaskHandle = (TaskHandle_t)(uintptr_t)0x1234;
    mocked_xQueueReceive_fake.custom_fake = NULL;
    mocked_xQueueReceive_fake.return_val = pdFALSE;
    TEST_ASSERT_NOT_EQUAL(0, led->base.Stop(&led->base));
    TEST_ASSERT_NOT_NULL(builtinLEDService.led_strip);
    TEST_ASSERT_NOT_NULL(builtinLEDService.animTaskHandle);
    TEST_ASSERT_EQUAL(0, mocked_vTaskDelete_fake.call_count);
    TEST_ASSERT_NOT_EQUAL(ESP_OK, led->TurnOn(led));
    TEST_ASSERT_NOT_EQUAL(0, led->base.Start(&led->base));
    mocked_xQueueReceive_fake.custom_fake = receive_worker_ack;
    TEST_ASSERT_EQUAL(0, led->base.Stop(&led->base));
    TEST_ASSERT_NULL(builtinLEDService.animTaskHandle);
    TEST_ASSERT_EQUAL(0, led->base.Start(&led->base));
}


#include "Infrastructure/support/console_registration_fixture.h"
static esp_err_t builtin_registration_adapter(IDebugConsole *console) {
    mocked_ServiceLocator_GetService_fake.custom_fake = NULL;
    mocked_ServiceLocator_GetService_fake.return_val = (IService *)console;
    return RegisterConsoleCommands();
}
void test_BuiltinInternalRegistration_PreservesFailuresAndRetries(void) {
    check_registration_failures(builtin_registration_adapter, 9);
}
void test_BuiltinInternalRegistration_FailurePreventsWorkerCreation(void) {
    IService *service = (IService *)BuiltinLED_GetModule();
    TEST_ASSERT_EQUAL(0, service->Initialize(service, NULL));
    mocked_ServiceLocator_GetService_fake.custom_fake = NULL;
    mocked_ServiceLocator_GetService_fake.return_val = (IService *)&registration_console;
    registration_calls = 0;
    registration_fail_at = 1;
    registration_failure = ESP_ERR_NO_MEM;
    unsigned tasks = mocked_xTaskCreate_fake.call_count;
    TEST_ASSERT_EQUAL(ESP_ERR_NO_MEM, service->Start(service));
    TEST_ASSERT_EQUAL(tasks, mocked_xTaskCreate_fake.call_count);
    TEST_ASSERT_NOT_EQUAL(SERVICE_STATE_STARTED, service->GetState(service));
}

void test_BuiltinLED_StatusLockFailurePreservesCallerOutput(void) {
    IBuiltinLED *led=BuiltinLED_GetModule();
    TEST_ASSERT_EQUAL(0,led->base.Initialize(&led->base,NULL));
    BuiltinLEDStatus before,output;memset(&before,0xA5,sizeof(before));output=before;
    unsigned gives=mocked_xSemaphoreGive_fake.call_count;
    mocked_xSemaphoreTake_fake.return_val=pdFALSE;
    TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT,led->GetStatus(led,&output));
    mocked_xSemaphoreTake_fake.return_val=pdTRUE;
    TEST_ASSERT_EQUAL_MEMORY(&before,&output,sizeof(before));
    TEST_ASSERT_EQUAL(gives,mocked_xSemaphoreGive_fake.call_count);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,led->GetStatus(NULL,&output));
    TEST_ASSERT_EQUAL_MEMORY(&before,&output,sizeof(before));
    esp_err_t error=123;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,led->GetDriverError(NULL,&error));
    TEST_ASSERT_EQUAL(123,error);
}

void test_BuiltinLED_RejectsInvalidEnumOwnerAndGpioBeforeQueue(void) {
    IBuiltinLED *led=BuiltinLED_GetModule();
    TEST_ASSERT_EQUAL(0,led->base.Initialize(&led->base,NULL));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,led->SetAnimation(led,(BuiltinLEDAnimationType)-2,BUILTIN_LED_PRIO_LOW,0));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,led->SetAnimation(led,BUILTIN_LED_ANIM_COUNT,BUILTIN_LED_PRIO_LOW,0));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,led->SetAnimation(led,BLINK_WHITE,(BuiltinLEDPriority)-1,0));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,led->SetAnimation(led,BLINK_WHITE,(BuiltinLEDPriority)3,0));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,led->SetAnimation(NULL,BLINK_WHITE,BUILTIN_LED_PRIO_LOW,0));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,led->TurnOn(NULL));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,led->TurnOff(NULL));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,led->StopAnimation(NULL));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,led->SetBrightness(NULL,100));
    BuiltinLEDConfig config={.pin=-1,.max_brightness=40};
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,led->InitModule(led,&config));
    config.pin=7;
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_SUPPORTED,led->InitModule(led,&config));
    TEST_ASSERT_EQUAL(0,mocked_xQueueSend_fake.call_count);
}

void test_BuiltinLED_RejectsIncompletePluginBeforeQueue(void) {
    IBuiltinLED *led=BuiltinLED_GetModule();
    TEST_ASSERT_EQUAL(0,led->base.Initialize(&led->base,NULL));
    AnimationPlugin plugin=*HeartbeatAnim_Get();
    plugin.Reset=NULL;TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,led->RegisterAnimation(led,&plugin));
    plugin=*HeartbeatAnim_Get();plugin.Update=NULL;TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,led->RegisterAnimation(led,&plugin));
    plugin=*HeartbeatAnim_Get();plugin.type=BUILTIN_LED_ANIM_COUNT;TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,led->RegisterAnimation(led,&plugin));
    plugin=*HeartbeatAnim_Get();plugin.name=NULL;TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,led->RegisterAnimation(led,&plugin));
    char name[BUILTIN_LED_LABEL_CAPACITY+1];memset(name,'x',sizeof(name));name[sizeof(name)-1]=0;
    plugin=*HeartbeatAnim_Get();plugin.name=name;TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,led->RegisterAnimation(led,&plugin));
    TEST_ASSERT_EQUAL(0,mocked_xQueueSend_fake.call_count);
}

void test_BuiltinLED_AllCliSpellingsRejectMalformedArgumentsWithoutAdmission(void) {
    int (*handlers[])(int,char **)={cmd_builtin_led_info,cmd_builtin_led_list,cmd_builtin_led_set,
        cmd_builtin_led_stop,cmd_builtin_led_on,cmd_builtin_led_off,cmd_builtin_led_brightness,
        cmd_builtin_led_trace,cmd_bled_handler};
    char *missing[]={"bled",NULL};
    char *extra[]={"bled","status","extra","extra","extra"};
    for(size_t i=0;i<sizeof(handlers)/sizeof(handlers[0]);++i) {
        TEST_ASSERT_NOT_EQUAL(0,handlers[i](2,NULL));
        TEST_ASSERT_NOT_EQUAL(0,handlers[i](2,missing));
        TEST_ASSERT_NOT_EQUAL(0,handlers[i](5,extra));
    }
    TEST_ASSERT_EQUAL(0,mocked_xQueueSend_fake.call_count);
}

void test_BuiltinLED_MachineStatusAndRejectedMutationPreserveWorkerState(void) {
    IBuiltinLED *led = BuiltinLED_GetModule();
    TEST_ASSERT_EQUAL(0, led->base.Initialize(&led->base, NULL));
    char *info[] = {"builtin_led_info", "--machine"};
    char *status[] = {"bled", "status", "--machine"};
    char *bad[] = {"bled", "brightness", "256", "--machine"};
    BuiltinLEDStatus before, after;
    TEST_ASSERT_EQUAL(ESP_OK, led->GetStatus(led, &before));
    TEST_ASSERT_EQUAL(0, cmd_builtin_led_info(2, info));
    TEST_ASSERT_EQUAL(0, cmd_bled_handler(3, status));
    TEST_ASSERT_NOT_EQUAL(0, cmd_bled_handler(4, bad));
    TEST_ASSERT_EQUAL(ESP_OK, led->GetStatus(led, &after));
    TEST_ASSERT_EQUAL(before.brightness, after.brightness);
    TEST_ASSERT_EQUAL(before.current_animation, after.current_animation);
    TEST_ASSERT_EQUAL(before.enabled, after.enabled);
    TEST_ASSERT_EQUAL(0, mocked_xQueueSend_fake.call_count);
}

void test_BuiltinLED_BledRejectsMalformedNumbersBeforeQueue(void) {
    const char *bad[]={"abc","1x","-1","4294967296","999999999999999999999"," 1","+1",""};
    for(size_t i=0;i<sizeof(bad)/sizeof(bad[0]);++i) {
        char *on[]={"bled","on",(char *)bad[i]};
        char *anim[]={"bled","anim","14",(char *)bad[i]};
        char *state[]={"bled","state","IDLE",(char *)bad[i]};
        TEST_ASSERT_NOT_EQUAL(0,cmd_bled_handler(3,on));
        TEST_ASSERT_NOT_EQUAL(0,cmd_bled_handler(4,anim));
        TEST_ASSERT_NOT_EQUAL(0,cmd_bled_handler(4,state));
    }
    char *unknown[]={"bled","anim","21"};
    TEST_ASSERT_NOT_EQUAL(0,cmd_bled_handler(3,unknown));
    TEST_ASSERT_EQUAL(0,mocked_xQueueSend_fake.call_count);
}

void test_BuiltinLED_BledReturnsQueueAndStatusFailures(void) {
    IBuiltinLED *led=BuiltinLED_GetModule();
    TEST_ASSERT_EQUAL(0,led->base.Initialize(&led->base,NULL));
    mocked_xQueueSend_fake.return_val=pdFALSE;
    char *on[]={"bled","on","1000"};char *anim[]={"bled","anim","14","1000"};char *state[]={"bled","state","IDLE"};
    TEST_ASSERT_NOT_EQUAL(0,cmd_bled_handler(3,on));
    TEST_ASSERT_NOT_EQUAL(0,cmd_bled_handler(4,anim));
    TEST_ASSERT_NOT_EQUAL(0,cmd_bled_handler(3,state));
    mocked_xSemaphoreTake_fake.return_val=pdFALSE;
    char *status[]={"bled","status"};
    TEST_ASSERT_NOT_EQUAL(0,cmd_bled_handler(2,status));
    mocked_xSemaphoreTake_fake.return_val=pdTRUE;
}

void test_BuiltinLED_MaximumDurationIsFiniteAndZeroHasNoExpiry(void) {
    IBuiltinLED *led=BuiltinLED_GetModule();
    TEST_ASSERT_EQUAL(0,led->base.Initialize(&led->base,NULL));
    BuiltinLEDCmd cmd={.type=CMD_SET_ANIMATION,.data.set_anim={.anim_type=DOUBLE_BLINK_WHITE,.priority=BUILTIN_LED_PRIO_HIGH,.duration_ms=UINT32_MAX}};
    bool clear,pixel;LEDColor color;
    ProcessCommand(&cmd,&clear,&pixel,&color);
    UpdateDurationCountdown(1);
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX-1,builtinLEDService.status.remaining_ms);
    UpdateDurationCountdown(UINT32_MAX-1);
    TEST_ASSERT_EQUAL(BUILTIN_LED_PRIO_LOW,builtinLEDService.status.current_priority);
    cmd.data.set_anim.duration_ms=0;ProcessCommand(&cmd,&clear,&pixel,&color);
    UpdateDurationCountdown(UINT32_MAX);
    TEST_ASSERT_EQUAL(DOUBLE_BLINK_WHITE,builtinLEDService.status.current_animation);
    TEST_ASSERT_EQUAL(BUILTIN_LED_PRIO_HIGH,builtinLEDService.status.current_priority);
    TEST_ASSERT_EQUAL(0,builtinLEDService.status.remaining_ms);
}

void test_BuiltinLED_InitialClearFailureRollsBackBeforeWorkerAndCanRetry(void) {
    IBuiltinLED *led=BuiltinLED_GetModule();TEST_ASSERT_EQUAL(0,led->base.Initialize(&led->base,NULL));
    unsigned tasks=mocked_xTaskCreate_fake.call_count;strip_clear_result=ESP_FAIL;
    TEST_ASSERT_NOT_EQUAL(0,led->base.Start(&led->base));
    TEST_ASSERT_EQUAL(tasks,mocked_xTaskCreate_fake.call_count);
    TEST_ASSERT_EQUAL(1,strip_delete_calls);TEST_ASSERT_NULL(builtinLEDService.led_strip);
    TEST_ASSERT_EQUAL(SERVICE_STATE_INITIALIZED,builtinLEDService.state);
    esp_err_t error;TEST_ASSERT_EQUAL(ESP_OK,led->GetDriverError(led,&error));TEST_ASSERT_EQUAL(ESP_FAIL,error);
    strip_clear_result=ESP_OK;TEST_ASSERT_EQUAL(0,led->base.Start(&led->base));
    TEST_ASSERT_EQUAL(ESP_OK,led->GetDriverError(led,&error));TEST_ASSERT_EQUAL(ESP_OK,error);
}

void test_BuiltinLED_InitialClearRollbackDeletionFailureRetainsUnsafeOwnership(void) {
    IBuiltinLED *led=BuiltinLED_GetModule();TEST_ASSERT_EQUAL(0,led->base.Initialize(&led->base,NULL));
    strip_clear_result=strip_delete_result=ESP_FAIL;
    TEST_ASSERT_NOT_EQUAL(0,led->base.Start(&led->base));
    TEST_ASSERT_EQUAL(SERVICE_STATE_ERROR,builtinLEDService.state);
    TEST_ASSERT_TRUE(builtinLEDService.driverCleanupFailed);TEST_ASSERT_NOT_NULL(builtinLEDService.led_strip);
    TEST_ASSERT_NOT_EQUAL(0,led->base.Start(&led->base));TEST_ASSERT_NOT_EQUAL(0,led->base.Stop(&led->base));
    TEST_ASSERT_EQUAL(1,strip_delete_calls);
}

static jmp_buf driver_test_exit;
static unsigned driver_iterations,driver_expected_failures;
static bool driver_fails_refresh;
static BaseType_t driver_retry_queue(QueueHandle_t queue,void *output,TickType_t wait) {
    (void)queue;
    if(driver_iterations>0) {
        esp_err_t error;
        TEST_ASSERT_EQUAL(ESP_OK,BuiltinLED_GetDriverError(&builtinLEDService.base,&error));
        if(driver_iterations<=driver_expected_failures) {
            TEST_ASSERT_TRUE(wait>0 && wait<portMAX_DELAY);
            TEST_ASSERT_EQUAL(ESP_FAIL,error);
            TEST_ASSERT_NOT_EQUAL(0,BuiltinLED_CheckHealth(&builtinLEDService.base.base));
            if(!driver_fails_refresh)TEST_ASSERT_EQUAL(0,strip_refresh_calls);
        } else {
            TEST_ASSERT_EQUAL(ESP_OK,error);TEST_ASSERT_EQUAL(0,BuiltinLED_CheckHealth(&builtinLEDService.base.base));
            longjmp(driver_test_exit,1);
        }
    }
    ++driver_iterations;mocked_esp_timer_get_time_fake.return_val+=20000;
    if(driver_iterations==1) {
        *(BuiltinLEDCmd *)output=(BuiltinLEDCmd){.type=CMD_SET_BRIGHTNESS,.data.set_brightness.brightness=BUILTIN_LED_DEFAULT_BRIGHTNESS};
        return pdTRUE;
    }
    return pdFALSE;
}

static void verify_driver_retry(bool refresh,unsigned failures) {
    IBuiltinLED *led=BuiltinLED_GetModule();TEST_ASSERT_EQUAL(0,led->base.Initialize(&led->base,NULL));
    TEST_ASSERT_EQUAL(0,led->base.Start(&led->base));
    builtinLEDService.status.is_on=true;builtinLEDService.status.current_animation=BUILTIN_LED_ANIM_NONE;
    builtinLEDService.targetFrame=(LEDFrame){20,20,20,255};
    LEDTransition_Reset(&builtinLEDService.transition,builtinLEDService.targetFrame);
    driver_iterations=0;driver_expected_failures=failures;driver_fails_refresh=refresh;
    if(refresh)strip_refresh_failures=failures;else strip_pixel_failures=failures;
    mocked_xQueueReceive_fake.custom_fake=driver_retry_queue;
    if(setjmp(driver_test_exit)==0)BuiltinLED_AnimTask(NULL);
    mocked_xQueueReceive_fake.custom_fake=receive_worker_ack;
    TEST_ASSERT_EQUAL(failures+1,strip_pixel_calls);
    TEST_ASSERT_EQUAL(refresh?failures+1:1,strip_refresh_calls);
    TEST_ASSERT_EQUAL(1,driver_error_events);
    TEST_ASSERT_EQUAL(BUILTIN_LED_ANIM_NONE,builtinLEDService.status.current_animation);
}
void test_BuiltinLED_SetPixelFailureSkipsRefreshAndRetriesStaticOutput(void){verify_driver_retry(false,1);}
void test_BuiltinLED_RefreshFailureRetriesStaticOutput(void){verify_driver_retry(true,1);}
void test_BuiltinLED_RepeatedRenderFailurePublishesOnceAndRecovers(void){verify_driver_retry(false,2);}

void test_BuiltinLED_SdkCreationFailurePreservesStoppedDriverOwnership(void) {
    IBuiltinLED *led=BuiltinLED_GetModule();TEST_ASSERT_EQUAL(0,led->base.Initialize(&led->base,NULL));
    strip_create_result=ESP_FAIL;
    unsigned tasks=mocked_xTaskCreate_fake.call_count;
    TEST_ASSERT_NOT_EQUAL(0,led->base.Start(&led->base));
    TEST_ASSERT_EQUAL(tasks,mocked_xTaskCreate_fake.call_count);
    TEST_ASSERT_NULL(builtinLEDService.led_strip);TEST_ASSERT_EQUAL(0,strip_delete_calls);
    esp_err_t error;TEST_ASSERT_EQUAL(ESP_OK,led->GetDriverError(led,&error));TEST_ASSERT_EQUAL(ESP_FAIL,error);
    strip_create_result=ESP_OK;TEST_ASSERT_EQUAL(0,led->base.Start(&led->base));
}

static gpio_num_t injected_pin_value;
static gpio_num_t injected_pin_getter(IPinManager *self,PinFunction_t function) {
    (void)self;(void)function;return injected_pin_value;
}
void test_BuiltinLED_MissingPinCallbackDoesNotPublishInitialized(void) {
    IPinManager pins={0};test_pin_manager=&pins;
    IBuiltinLED *led=BuiltinLED_GetModule();
    int result=led->base.Initialize(&led->base,NULL);test_pin_manager=NULL;
    TEST_ASSERT_NOT_EQUAL(0,result);TEST_ASSERT_EQUAL(SERVICE_STATE_UNINITIALIZED,builtinLEDService.state);
    TEST_ASSERT_EQUAL(0,led->base.Initialize(&led->base,NULL));
}
void test_BuiltinLED_InvalidMappedPinDoesNotTruncateOrUseFallback(void) {
    IPinManager pins={.GetPin=injected_pin_getter};test_pin_manager=&pins;
    IBuiltinLED *led=BuiltinLED_GetModule();
    injected_pin_value=GPIO_NUM_NC;
    TEST_ASSERT_NOT_EQUAL(0,led->base.Initialize(&led->base,NULL));
    TEST_ASSERT_EQUAL(SERVICE_STATE_UNINITIALIZED,builtinLEDService.state);
    injected_pin_value=(gpio_num_t)256;
    TEST_ASSERT_NOT_EQUAL(0,led->base.Initialize(&led->base,NULL));
    TEST_ASSERT_EQUAL(SERVICE_STATE_UNINITIALIZED,builtinLEDService.state);
    injected_pin_value=GPIO_NUM_8;
    int result=led->base.Initialize(&led->base,NULL);test_pin_manager=NULL;
    TEST_ASSERT_EQUAL(0,result);TEST_ASSERT_EQUAL(8,builtinLEDService.pin);
}

static void custom_renderer_reset(AnimationPlugin *self){(void)self;}
static LEDColor custom_renderer_update(AnimationPlugin *self,uint32_t elapsed){(void)self;(void)elapsed;return (LEDColor){20,20,20};}
static AnimationPlugin custom_renderer={.type=(BuiltinLEDAnimationType)40000,.name="Custom test renderer",.Reset=custom_renderer_reset,.Update=custom_renderer_update};
void test_BuiltinLED_CustomPluginSelectionAndTracePreserveFullWidthId(void) {
    IBuiltinLED *led=BuiltinLED_GetModule();TEST_ASSERT_EQUAL(0,led->base.Initialize(&led->base,NULL));
    mocked_xQueueSend_fake.custom_fake=capture_settings_command;
    TEST_ASSERT_EQUAL(ESP_OK,led->RegisterAnimation(led,&custom_renderer));
    BuiltinLEDCmd registration=captured_settings_command;
    TEST_ASSERT_EQUAL_PTR(&custom_renderer,registration.data.reg_plugin.plugin);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,led->SetAnimation(led,custom_renderer.type,BUILTIN_LED_PRIO_HIGH,0));
    bool clear,pixel;LEDColor color;ProcessCommand(&registration,&clear,&pixel,&color);
    TEST_ASSERT_EQUAL(ESP_OK,led->SetAnimation(led,custom_renderer.type,BUILTIN_LED_PRIO_HIGH,0));
    ProcessCommand(&captured_settings_command,&clear,&pixel,&color);
    char *start[]={"builtin_led_trace","start"},*stop[]={"builtin_led_trace","stop"};
    TEST_ASSERT_EQUAL(0,cmd_builtin_led_trace(2,start));
    TraceFrame(123000,20,20,-1,(LEDFrame){20,20,20,255},0,0);
    TEST_ASSERT_EQUAL(40000,TraceRowAt(&ledTrace,0)->animation);
    TEST_ASSERT_EQUAL(0,cmd_builtin_led_trace(2,stop));
    mocked_xQueueSend_fake.custom_fake=NULL;
}

void test_BuiltinLED_SmoothRenderUsesOutputResolutionAndPreservesMetadata(void) {
    AnimationPlugin *plugin = GenericFadeAnim_Get();
    AnimationPlugin metadata = *plugin;
    builtinLEDService.max_brightness = 255;
    plugin->Reset(plugin);
    unsigned changes = 0;
    LEDColor previous = RenderAnimation(plugin, 0);
    for (unsigned frame = 0; frame < 75; ++frame) {
        LEDColor next = RenderAnimation(plugin, 20);
        TEST_ASSERT_TRUE(next.r >= previous.r);
        if (next.r != previous.r) changes++;
        previous = next;
    }
    TEST_ASSERT_EQUAL(75, changes);
    TEST_ASSERT_EQUAL(255, previous.r);
    TEST_ASSERT_EQUAL_MEMORY(&metadata, plugin, sizeof(metadata));
    builtinLEDService.max_brightness = 0;
    LEDColor dark = RenderAnimation(plugin, 20);
    TEST_ASSERT_EQUAL(0, dark.r | dark.g | dark.b);
    builtinLEDService.max_brightness = 255;
    LEDColor resumed = RenderAnimation(plugin, 0);
    TEST_ASSERT_TRUE(resumed.r < previous.r);
}

void test_BuiltinLED_StartupPhasePreservesRuntimeDefaultAndNotifications(void) {
    fake_settings.hardware.builtin_led_idle_anim=SOLID_GREEN;
    fake_settings.hardware.builtin_led_startup_anim=DOUBLE_BLINK_WHITE;
    IBuiltinLED *led=BuiltinLED_GetModule();
    TEST_ASSERT_EQUAL(0,led->base.Initialize(&led->base,NULL));
    bool clear=false,pixel=false;LEDColor color={0};
    BuiltinLEDCmd phase={.type=CMD_SET_STARTUP,.data.startup.active=true};
    ProcessCommand(&phase,&clear,&pixel,&color);
    TEST_ASSERT_EQUAL(DOUBLE_BLINK_WHITE,builtinLEDService.status.current_animation);
    TEST_ASSERT_EQUAL(SOLID_GREEN,builtinLEDService.defaultAnimType);
    uint32_t generation=animationGeneration;
    builtinLEDService.status.is_on=false;
    BuiltinLEDCmd settings={.type=CMD_SETTINGS_CHANGED,.data.settings={
        .default_anim=SOLID_GREEN,.startup_anim=DOUBLE_BLINK_WHITE,.brightness=127}};
    ProcessCommand(&settings,&clear,&pixel,&color);
    TEST_ASSERT_EQUAL(generation,animationGeneration);
    TEST_ASSERT_EQUAL(DOUBLE_BLINK_WHITE,builtinLEDService.status.current_animation);
    BuiltinLEDCmd notification={.type=CMD_SET_ANIMATION,.data.set_anim={SOS_RED,BUILTIN_LED_PRIO_HIGH,20}};
    ProcessCommand(&notification,&clear,&pixel,&color);
    ProcessCommand(&settings,&clear,&pixel,&color);
    TEST_ASSERT_EQUAL(SOS_RED,builtinLEDService.status.current_animation);
    UpdateDurationCountdown(20);
    TEST_ASSERT_EQUAL(DOUBLE_BLINK_WHITE,builtinLEDService.status.current_animation);
    phase.data.startup.active=false;ProcessCommand(&phase,&clear,&pixel,&color);
    TEST_ASSERT_EQUAL(SOLID_GREEN,builtinLEDService.status.current_animation);
    BuiltinLEDCmd connecting={.type=CMD_SET_ANIMATION,.data.set_anim={HEARTBEAT_RED,BUILTIN_LED_PRIO_LOW,0}};
    ProcessCommand(&connecting,&clear,&pixel,&color);
    generation=animationGeneration;
    ProcessCommand(&settings,&clear,&pixel,&color);
    TEST_ASSERT_EQUAL(HEARTBEAT_RED,builtinLEDService.status.current_animation);
    TEST_ASSERT_EQUAL(generation,animationGeneration);
    settings.data.settings.disabled=true;ProcessCommand(&settings,&clear,&pixel,&color);
    TEST_ASSERT_TRUE(clear);
    phase.data.startup.active=true;ProcessCommand(&phase,&clear,&pixel,&color);
    TEST_ASSERT_EQUAL(BUILTIN_LED_ANIM_NONE,builtinLEDService.status.current_animation);
    builtinLEDService.disabled=false;
    builtinLEDService.startupAnimType=BUILTIN_LED_ANIM_NONE;
    builtinLEDService.status.current_animation=BUILTIN_LED_ANIM_NONE;
    builtinLEDService.status.is_on=true; /* Static white is not an inactive NONE selection. */
    ProcessCommand(&phase,&clear,&pixel,&color);
    TEST_ASSERT_TRUE(clear);
    TEST_ASSERT_FALSE(builtinLEDService.status.is_on);
}

static void deliver_connection_event(const char *name) {
    mocked_xQueueSend_fake.custom_fake = capture_settings_command;
    unsigned sent = mocked_xQueueSend_fake.call_count;
    ZigbeeEventHandler((BusEvent){.eventName=name,.type=EVENT_TYPE_INFO});
    TEST_ASSERT_EQUAL(sent+1, mocked_xQueueSend_fake.call_count);
    bool clear=false,pixel=false; LEDColor color={0};
    ProcessCommand(&captured_settings_command,&clear,&pixel,&color);
}

void test_BuiltinLED_ConnectionNoticeDoesNotInterruptStartupOrRestartOnDuplicate(void) {
    fake_settings.hardware.builtin_led_startup_anim=RAINBOW_FLOW;
    IBuiltinLED *led=BuiltinLED_GetModule();
    TEST_ASSERT_EQUAL(0,led->base.Initialize(&led->base,NULL));
    bool clear=false,pixel=false; LEDColor color={0};
    BuiltinLEDCmd phase={.type=CMD_SET_STARTUP,.data.startup.active=true};
    ProcessCommand(&phase,&clear,&pixel,&color);
    uint32_t generation=animationGeneration;
    deliver_connection_event("zigbee_connected");
    deliver_connection_event("zigbee_pairing_stop");
    TEST_ASSERT_EQUAL(RAINBOW_FLOW,builtinLEDService.status.current_animation);
    TEST_ASSERT_EQUAL(generation,animationGeneration);
    phase.data.startup.active=false;
    ProcessCommand(&phase,&clear,&pixel,&color);
    TEST_ASSERT_EQUAL(SOLID_GREEN,builtinLEDService.status.current_animation);
    TEST_ASSERT_EQUAL(3000,builtinLEDService.status.remaining_ms);
    UpdateDurationCountdown(1000);
    deliver_connection_event("zigbee_connected");
    TEST_ASSERT_EQUAL(2000,builtinLEDService.status.remaining_ms);
    UpdateDurationCountdown(2000);
    TEST_ASSERT_EQUAL(builtinLEDService.defaultAnimType,builtinLEDService.status.current_animation);
}

void test_BuiltinLED_RainbowUsesFullResolutionAndPwmEquivalentTransfer(void) {
    IBuiltinLED *led=BuiltinLED_GetModule();
    TEST_ASSERT_EQUAL(0,led->base.Initialize(&led->base,NULL));
    RainbowFlowAnim_EndSynchronizedPhase();
    builtinLEDService.max_brightness=255;
    AnimationPlugin *plugin=FindPlugin(RAINBOW_FLOW);
    plugin->Reset(plugin);
    LEDColor color=RenderAnimation(plugin,RAINBOW_CYCLE_DURATION_MS/12U);
    TEST_ASSERT_EQUAL(255,color.r);
    TEST_ASSERT_EQUAL(37,color.g);
    TEST_ASSERT_EQUAL(0,color.b);
    for (unsigned brightness=0;brightness<=255;brightness+=51) {
        builtinLEDService.max_brightness=brightness;
        for (uint32_t t=0;t<100U*RAINBOW_CYCLE_DURATION_MS;t+=20U) {
            plugin->Reset(plugin);
            LEDColor actual=RenderAnimation(plugin,t);
            RainbowFlowAnim_Reset();
            RGBColor raw=RainbowFlowAnim_Update(t,255);
            TEST_ASSERT_EQUAL_UINT8((uint32_t)LEDColor_ApplyOutputCurve(raw.r)*brightness/255,actual.r);
            TEST_ASSERT_EQUAL_UINT8((uint32_t)LEDColor_ApplyOutputCurve(raw.g)*brightness/255,actual.g);
            TEST_ASSERT_EQUAL_UINT8((uint32_t)LEDColor_ApplyOutputCurve(raw.b)*brightness/255,actual.b);
        }
    }
}

void test_BuiltinLED_FirstFrameAlreadyOwnsStartupSelection(void) {
    fake_settings.hardware.builtin_led_startup_anim=RAINBOW_FLOW;
    fake_settings.hardware.builtin_led_idle_anim=DOUBLE_BLINK_WHITE;
    RainbowFlowAnim_BeginSynchronizedPhase(1000);
    IBuiltinLED *led=BuiltinLED_GetModule();
    int result=led->base.Initialize(&led->base,NULL);
    bool startup=builtinLEDService.startupActive;
    int animation=builtinLEDService.status.current_animation;
    RainbowFlowAnim_EndSynchronizedPhase();
    TEST_ASSERT_EQUAL(0,result);
    TEST_ASSERT_TRUE(startup);
    TEST_ASSERT_EQUAL(RAINBOW_FLOW,animation);
    TEST_ASSERT_EQUAL(DOUBLE_BLINK_WHITE,builtinLEDService.defaultAnimType);
}

void test_BuiltinLED_SystemInitializationAndReadyCannotPreemptStartup(void) {
    fake_settings.hardware.builtin_led_startup_anim=RAINBOW_FLOW;
    IBuiltinLED *led=BuiltinLED_GetModule();
    TEST_ASSERT_EQUAL(0,led->base.Initialize(&led->base,NULL));
    bool clear=false,pixel=false; LEDColor color={0};
    BuiltinLEDCmd phase={.type=CMD_SET_STARTUP,.data.startup.active=true};
    ProcessCommand(&phase,&clear,&pixel,&color);
    uint32_t generation=animationGeneration;
    BuiltinLEDCmd initializing={.type=CMD_SET_ANIMATION,.data.set_anim={BREATHING_GREEN,BUILTIN_LED_PRIO_MEDIUM,10000}};
    ProcessCommand(&initializing,&clear,&pixel,&color);
    BuiltinLEDCmd ready={.type=CMD_SET_ANIMATION,.data.set_anim={DOUBLE_BLINK_WHITE,BUILTIN_LED_PRIO_LOW,0}};
    ProcessCommand(&ready,&clear,&pixel,&color);
    TEST_ASSERT_EQUAL(RAINBOW_FLOW,builtinLEDService.status.current_animation);
    TEST_ASSERT_EQUAL(generation,animationGeneration);
    TEST_ASSERT_EQUAL(DOUBLE_BLINK_WHITE,builtinLEDService.defaultAnimType);
    deliver_connection_event("zigbee_connected");
    ProcessCommand(&initializing,&clear,&pixel,&color);
    phase.data.startup.active=false;ProcessCommand(&phase,&clear,&pixel,&color);
    TEST_ASSERT_EQUAL(SOLID_GREEN,builtinLEDService.status.current_animation);
    UpdateDurationCountdown(BUILTIN_LED_CONNECTED_NOTICE_MS);
    TEST_ASSERT_EQUAL(DOUBLE_BLINK_WHITE,builtinLEDService.status.current_animation);
}

void test_BuiltinLED_DeferredNoticeCancelsOnDisconnectAndPriorityOverride(void) {
    fake_settings.hardware.builtin_led_startup_anim=RAINBOW_FLOW;
    IBuiltinLED *led=BuiltinLED_GetModule();
    TEST_ASSERT_EQUAL(0,led->base.Initialize(&led->base,NULL));
    bool clear=false,pixel=false; LEDColor color={0};
    BuiltinLEDCmd phase={.type=CMD_SET_STARTUP,.data.startup.active=true};
    ProcessCommand(&phase,&clear,&pixel,&color);
    deliver_connection_event("zigbee_connected");
    deliver_connection_event("zigbee_disconnected");
    TEST_ASSERT_FALSE(builtinLEDService.connectionNoticePending);
    phase.data.startup.active=false;ProcessCommand(&phase,&clear,&pixel,&color);
    TEST_ASSERT_FALSE(builtinLEDService.connectionNoticeActive);
    phase.data.startup.active=true;ProcessCommand(&phase,&clear,&pixel,&color);
    deliver_connection_event("zigbee_connected");
    BuiltinLEDCmd fault={.type=CMD_SET_ANIMATION,.data.set_anim={SOS_RED,BUILTIN_LED_PRIO_HIGH,0}};
    ProcessCommand(&fault,&clear,&pixel,&color);
    phase.data.startup.active=false;ProcessCommand(&phase,&clear,&pixel,&color);
    TEST_ASSERT_EQUAL(SOS_RED,builtinLEDService.status.current_animation);
    TEST_ASSERT_FALSE(builtinLEDService.connectionNoticePending);
    TEST_ASSERT_FALSE(builtinLEDService.connectionNoticeActive);
    builtinLEDService.disabled=true;
    deliver_connection_event("zigbee_connected");
    TEST_ASSERT_FALSE(builtinLEDService.connectionNoticePending);
    TEST_ASSERT_FALSE(builtinLEDService.connectionNoticeActive);
}

static esp_err_t startup_save_result;
static unsigned startup_save_calls;
static esp_err_t save_startup_field(IConfigurationManager *self,const char *block,const ConfigurationFieldUpdate *fields,size_t count,bool persist) {
    (void)self;TEST_ASSERT_EQUAL_STRING("hardware",block);TEST_ASSERT_EQUAL(1,count);TEST_ASSERT_TRUE(persist);
    TEST_ASSERT_EQUAL(offsetof(HardwareConfigs,builtin_led_startup_anim),fields[0].offset);
    ++startup_save_calls;
    if(startup_save_result==ESP_OK)fake_settings.hardware.builtin_led_startup_anim=*(const uint8_t*)fields[0].data;
    return startup_save_result;
}
void test_BuiltinLED_BootCliValidatesAndPersistsOnlyIndependentSelection(void) {
    stub_config_mgr.UpdateFields=save_startup_field;
    startup_save_result=ESP_OK;startup_save_calls=0;
    fake_settings.hardware.ext_led_anim=RGBLED_ANIM_RAINBOW;
    char *read[]={"builtin_led_boot"},*off[]={"builtin_led_boot","255"},*invalid[]={"builtin_led_boot","250"};
    TEST_ASSERT_EQUAL(0,cmd_builtin_led_boot(1,read));TEST_ASSERT_EQUAL(0,startup_save_calls);
    TEST_ASSERT_EQUAL(0,cmd_builtin_led_boot(2,off));TEST_ASSERT_EQUAL(255,fake_settings.hardware.builtin_led_startup_anim);
    TEST_ASSERT_EQUAL(RGBLED_ANIM_RAINBOW,fake_settings.hardware.ext_led_anim);
    TEST_ASSERT_EQUAL(1,cmd_builtin_led_boot(2,invalid));TEST_ASSERT_EQUAL(1,startup_save_calls);
    startup_save_result=ESP_FAIL;off[1]="0";
    TEST_ASSERT_EQUAL(1,cmd_builtin_led_boot(2,off));TEST_ASSERT_EQUAL(255,fake_settings.hardware.builtin_led_startup_anim);
    stub_config_mgr.UpdateFields=NULL;
}

void test_BuiltinLED_BrightnessPreviewPreservesOwnerAndPriority(void);
void test_BuiltinLED_PreviewKeepsDefaultAndNotificationState(void);
void test_BuiltinLED_EveryCatalogPreviewPreservesSavedAndRuntimeDefaults(void);
int main(int argc, char **argv) {
    (void)argc; (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_BuiltinLED_EveryCatalogPreviewPreservesSavedAndRuntimeDefaults);
    RUN_TEST(test_BuiltinLED_BrightnessPreviewPreservesOwnerAndPriority);
    RUN_TEST(test_BuiltinLED_PreviewKeepsDefaultAndNotificationState);
    RUN_TEST(test_BuiltinLED_BootCliValidatesAndPersistsOnlyIndependentSelection);
    RUN_TEST(test_BuiltinLED_StartupPhasePreservesRuntimeDefaultAndNotifications);
    RUN_TEST(test_BuiltinLED_ConnectionNoticeDoesNotInterruptStartupOrRestartOnDuplicate);
    RUN_TEST(test_BuiltinLED_RainbowUsesFullResolutionAndPwmEquivalentTransfer);
    RUN_TEST(test_BuiltinLED_FirstFrameAlreadyOwnsStartupSelection);
    RUN_TEST(test_BuiltinLED_SystemInitializationAndReadyCannotPreemptStartup);
    RUN_TEST(test_BuiltinLED_DeferredNoticeCancelsOnDisconnectAndPriorityOverride);
    RUN_TEST(test_BuiltinLED_StopWithoutWorkerAcknowledgmentPreservesOwnership);
    RUN_TEST(test_BuiltinLED_PartialDriverTeardownCannotBeRetried);
    RUN_TEST(test_BuiltinLED_UnsubscribeBusyPreservesResourcesForRetry);
    RUN_TEST(test_BuiltinLED_SubscriptionFailureRollsBackOnlyOwnedEntries);
    RUN_TEST(test_BuiltinLED_AllocationFailureDoesNotPublishInitialized);
    RUN_TEST(test_BuiltinLED_TaskCreationFailureReleasesDriver);
    RUN_TEST(test_BuiltinLED_GlobalModeCLIAndPersistenceErrors);
    RUN_TEST(test_BuiltinLED_StartupRespectsPersistedDisabledMode);
    RUN_TEST(test_BuiltinLED_GlobalDisableOverridesNotificationsAndReenableRestarts);
    RUN_TEST(test_BuiltinLED_WorkerExpiryRestoresDisabledDefault);
    RUN_TEST(test_BuiltinLED_WorkerIgnoresLowPriorityWithoutPausing);
    RUN_TEST(test_BuiltinLED_WorkerCountsOverrideTimeDuringCommands);
    RUN_TEST(test_BuiltinLED_WorkerNewSelectionStartsAtZero);
    RUN_TEST(test_BuiltinLED_WorkerOffThenSelectionStartsAtZero);
    RUN_TEST(test_BuiltinLED_AllTimedPatternsLongRunAndOverflow);
    RUN_TEST(test_BuiltinLED_RainbowIsIndependentOfExternalLED);
    RUN_TEST(test_BuiltinLED_SOSContainsNinePulsesForOneHundredCycles);
    RUN_TEST(test_BuiltinLED_DoubleBlinkDefinitionOneHundredCycles);
    RUN_TEST(test_BuiltinLED_WorkerPreservesFractionalElapsedTime);
    RUN_TEST(test_BuiltinLED_WorkerPreservesElapsedTimeOnCommands);
    RUN_TEST(test_BuiltinLED_TraceCapturesEdgesCommandsAndGapsWithoutChangingAnimation);
    RUN_TEST(test_BuiltinLED_TraceOverflowIsReportedAndBadCommandPreservesCapture);
    RUN_TEST(test_BuiltinLED_TraceStatusAndRepeatedStartPreserveOwnedCapture);
    RUN_TEST(test_BuiltinLED_TracePagesRollbackAndPreserveFullCapacity);
    RUN_TEST(test_BuiltinLED_Lifecycle);
    RUN_TEST(test_BuiltinLED_AnimationCliRejectsMalformedNumbersBeforeQueueAdmission);
    RUN_TEST(test_BuiltinLED_Ops);
    RUN_TEST(test_BuiltinLED_QueueFull_ReturnsTimeout);
    RUN_TEST(test_BuiltinLED_NullArgs);
    RUN_TEST(test_BuiltinLED_Priority);
    RUN_TEST(test_BuiltinLED_TimedAnimationExpiry);
    RUN_TEST(test_BuiltinLED_RegisterPlugin_NoDuplicate);
    RUN_TEST(test_BuiltinLED_RegisterPlugin_Overflow);
    RUN_TEST(test_BuiltinLED_SettingsChangedHandler);
    RUN_TEST(test_BuiltinLED_PersistentBrightness);
    RUN_TEST(test_BuiltinLED_BrightnessPreservesAnimationPhase);
    RUN_TEST(test_BuiltinLED_UnrelatedSettingsDuringOffFramePreservePhase);
    RUN_TEST(test_BuiltinLED_BrightnessInputAndSaveFailure);
    RUN_TEST(test_BuiltinLED_Stop_Unsubscribes);
    RUN_TEST(test_BuiltinLED_ConsoleCommands_Registered);
    RUN_TEST(test_BuiltinLED_TurnOn_TurnOff_HardwareFlags);
    RUN_TEST(test_BuiltinLED_ApplyBrightness);
    RUN_TEST(test_BuiltinLED_SmoothRenderUsesOutputResolutionAndPreservesMetadata);
    RUN_TEST(test_BuiltinLED_ApplyBrightness_Clamp);
    RUN_TEST(test_BuiltinLED_SelfTest_AllAnimations);
    RUN_TEST(test_BuiltinLED_FindPlugin_NotFound);
    RUN_TEST(test_BuiltinInternalRegistration_PreservesFailuresAndRetries);
    RUN_TEST(test_BuiltinInternalRegistration_FailurePreventsWorkerCreation);
    RUN_TEST(test_BuiltinLED_StatusLockFailurePreservesCallerOutput);
    RUN_TEST(test_BuiltinLED_RejectsInvalidEnumOwnerAndGpioBeforeQueue);
    RUN_TEST(test_BuiltinLED_RejectsIncompletePluginBeforeQueue);
    RUN_TEST(test_BuiltinLED_AllCliSpellingsRejectMalformedArgumentsWithoutAdmission);
    RUN_TEST(test_BuiltinLED_MachineStatusAndRejectedMutationPreserveWorkerState);
    RUN_TEST(test_BuiltinLED_BledRejectsMalformedNumbersBeforeQueue);
    RUN_TEST(test_BuiltinLED_BledReturnsQueueAndStatusFailures);
    RUN_TEST(test_BuiltinLED_MaximumDurationIsFiniteAndZeroHasNoExpiry);
    RUN_TEST(test_BuiltinLED_InitialClearFailureRollsBackBeforeWorkerAndCanRetry);
    RUN_TEST(test_BuiltinLED_InitialClearRollbackDeletionFailureRetainsUnsafeOwnership);
    RUN_TEST(test_BuiltinLED_SetPixelFailureSkipsRefreshAndRetriesStaticOutput);
    RUN_TEST(test_BuiltinLED_RefreshFailureRetriesStaticOutput);
    RUN_TEST(test_BuiltinLED_RepeatedRenderFailurePublishesOnceAndRecovers);
    RUN_TEST(test_BuiltinLED_SdkCreationFailurePreservesStoppedDriverOwnership);
    RUN_TEST(test_BuiltinLED_MissingPinCallbackDoesNotPublishInitialized);
    RUN_TEST(test_BuiltinLED_InvalidMappedPinDoesNotTruncateOrUseFallback);
    RUN_TEST(test_BuiltinLED_CustomPluginSelectionAndTracePreserveFullWidthId);
    return UNITY_END();
}

#endif /* RACOOL_TEST_NATIVE */

void test_BuiltinLED_PreviewKeepsDefaultAndNotificationState(void) {
    bool clear,pixel; LEDColor color;
    builtinLEDService.defaultAnimType=BLINK_WHITE;
    builtinLEDService.status.current_animation=BLINK_WHITE;
    builtinLEDService.status.current_priority=BUILTIN_LED_PRIO_LOW;
    BuiltinLEDCmd command={.type=CMD_PREVIEW_ANIMATION,.data.set_anim={SOLID_RED,BUILTIN_LED_PRIO_LOW,3000}};
    ProcessCommand(&command,&clear,&pixel,&color);
    TEST_ASSERT_TRUE(builtinLEDService.previewActive);
    TEST_ASSERT_EQUAL(BLINK_WHITE,builtinLEDService.status.current_animation);
    TEST_ASSERT_EQUAL(BLINK_WHITE,builtinLEDService.defaultAnimType);
    TEST_ASSERT_NOT_EQUAL(FindPlugin(SOLID_RED),&builtinLEDService.previewInstance.plugin);
    builtinLEDService.previewActive=false;
    builtinLEDService.status.current_priority=BUILTIN_LED_PRIO_MEDIUM;
    builtinLEDService.status.remaining_ms=765;
    ProcessCommand(&command,&clear,&pixel,&color);
    TEST_ASSERT_FALSE(builtinLEDService.previewActive);
    TEST_ASSERT_EQUAL(765,builtinLEDService.status.remaining_ms);
    builtinLEDService.status.current_priority=BUILTIN_LED_PRIO_LOW;
    builtinLEDService.disabled=true;
    ProcessCommand(&command,&clear,&pixel,&color);
    TEST_ASSERT_FALSE(builtinLEDService.previewActive);
}

void test_BuiltinLED_EveryCatalogPreviewPreservesSavedAndRuntimeDefaults(void) {
    builtinLEDService.disabled=false;
    builtinLEDService.status.current_priority=BUILTIN_LED_PRIO_LOW;
    builtinLEDService.defaultAnimType=BLINK_WHITE;
    builtinLEDService.configuredDefaultAnim=BREATHING_GREEN;
    size_t count; const LEDAnimationCatalogEntry *entries=LEDAnimationCatalog_Entries(&count);
    for (size_t i=0;i<count;++i) {
        bool clear,pixel; LEDColor color;
        BuiltinLEDCmd command={.type=CMD_PREVIEW_ANIMATION,.data.set_anim={(BuiltinLEDAnimationType)entries[i].builtin_id,BUILTIN_LED_PRIO_LOW,3000}};
        ProcessCommand(&command,&clear,&pixel,&color);
        TEST_ASSERT_TRUE(builtinLEDService.previewActive);
        if (builtinLEDService.previewAnimated) {
            TEST_ASSERT_EQUAL(entries[i].builtin_id,builtinLEDService.previewInstance.plugin.type);
            for (unsigned frame=0;frame<150;++frame)
                (void)RenderAnimation(&builtinLEDService.previewInstance.plugin,20);
        } else TEST_ASSERT_EQUAL(255,entries[i].builtin_id);
        TEST_ASSERT_EQUAL(BLINK_WHITE,builtinLEDService.defaultAnimType);
        TEST_ASSERT_EQUAL(BREATHING_GREEN,builtinLEDService.configuredDefaultAnim);
    }
    IBuiltinLED *service=BuiltinLED_GetModule();
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,service->PreviewAnimation(service,SOLID_RED,0));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,service->PreviewAnimation(service,SOLID_RED,BUILTIN_LED_PREVIEW_MAX_MS+1));
}

void test_BuiltinLED_BrightnessPreviewPreservesOwnerAndPriority(void) {
    bool clear,pixel;LEDColor color;
    builtinLEDService.disabled=false;builtinLEDService.max_brightness=0;
    builtinLEDService.status.brightness=0;builtinLEDService.status.current_priority=BUILTIN_LED_PRIO_LOW;
    builtinLEDService.brightnessPreviewActive=false;
    BuiltinLEDCmd cmd={.type=CMD_PREVIEW_BRIGHTNESS,.data.brightness_preview={200,3000}};
    ProcessCommand(&cmd,&clear,&pixel,&color);
    TEST_ASSERT_TRUE(builtinLEDService.brightnessPreviewActive);TEST_ASSERT_EQUAL(200,builtinLEDService.previewBrightness);
    TEST_ASSERT_EQUAL(0,builtinLEDService.max_brightness);TEST_ASSERT_EQUAL(0,builtinLEDService.status.brightness);
    LEDAnimationInstance animation;
    TEST_ASSERT_EQUAL(ESP_OK,LEDAnimations_CreateInstance(SOLID_WHITE,&animation));
    AnimationPlugin *plugin=&animation.plugin;
    TEST_ASSERT_EQUAL(0,RenderAnimationAt(plugin,0,0).r);TEST_ASSERT_GREATER_THAN(0,RenderAnimationAt(plugin,0,200).r);
    builtinLEDService.brightnessPreviewActive=false;builtinLEDService.status.current_priority=BUILTIN_LED_PRIO_HIGH;
    ProcessCommand(&cmd,&clear,&pixel,&color);TEST_ASSERT_FALSE(builtinLEDService.brightnessPreviewActive);
    builtinLEDService.status.current_priority=BUILTIN_LED_PRIO_LOW;builtinLEDService.disabled=true;
    ProcessCommand(&cmd,&clear,&pixel,&color);TEST_ASSERT_FALSE(builtinLEDService.brightnessPreviewActive);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,BuiltinLED_GetModule()->PreviewBrightness(BuiltinLED_GetModule(),1,0));
}
