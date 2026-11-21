// Native-only test suite for RGBLED module.
// Compiled only when RACOOL_TEST_NATIVE is defined (native PlatformIO env).
#ifdef RACOOL_TEST_NATIVE

#include <unity.h>
#include "Hardware/RGBLED/IRGBLED.h"
#include "Core/DI/IDIRegistry.h"
#include "Core/ServiceLocator/ServiceLocator.h"
#include "Core/ServiceLocator/ServiceNames.h"

// Mock framework -- header (declarations)
#include "../../Infrastructure/support/test_mocks.h"

FAKE_VALUE_FUNC1(BaseType_t, xQueueReset, QueueHandle_t);
FAKE_VALUE_FUNC3(esp_err_t, ledc_stop, ledc_mode_t, ledc_channel_t, uint32_t);
FAKE_VALUE_FUNC2(esp_err_t, ledc_fade_stop, ledc_mode_t, ledc_channel_t);
FAKE_VOID_FUNC0(ledc_fade_func_uninstall);

// Include source implementations directly so mock macros apply at compile time.
// Each .c file defines a static 'TAG'; redirect it to a unique name per file.
#define TAG TAG_rgbled
#include "../../../src/Hardware/RGBLED/RGBLED.c"
#undef TAG
#include "../../../src/Hardware/RGBLED/RGBLEDConsoleCommands.c"

#define TAG TAG_rgbled_hw
#include "../../../src/Hardware/RGBLED/RGBLEDHardware.c"
#undef TAG

#include "../../../src/Hardware/RGBLED/RGBLEDColorEngine.c"

#include "../../../src/Hardware/LEDAnimations/RainbowFlowAnim.c"

#define TAG TAG_rgbled_anim
#include "../../../src/Hardware/RGBLED/RGBLEDAnimEngine.c"
#undef TAG

#define TAG TAG_rgbled_cfg
#include "../../../src/Hardware/RGBLED/RGBLEDConfig.c"
#undef TAG

// Mock BootAnimation_IsRunning for native test build
bool s_mockBootAnimationIsRunning = false;
bool BootAnimation_IsRunning(void) {
    return s_mockBootAnimationIsRunning;
}


#include "../../../src/Management/Watchdog/WatchdogWorker.c"

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static void ResetAllMocks(void) {
    RESET_FAKE(mocked_xSemaphoreCreateRecursiveMutex);
    RESET_FAKE(mocked_xSemaphoreTakeRecursive);
    RESET_FAKE(mocked_xSemaphoreGiveRecursive);
    RESET_FAKE(mocked_xQueueCreate);
    RESET_FAKE(mocked_xQueueSend);
    RESET_FAKE(mocked_xQueueReceive);
    RESET_FAKE(mocked_xTaskCreate);
    RESET_FAKE(mocked_vTaskDelay);
    RESET_FAKE(mocked_vTaskDelete);
    RESET_FAKE(mocked_ServiceLocator_GetService);
    RESET_FAKE(mocked_ServiceLocator_GetEventBus);
    RESET_FAKE(mocked_ledc_timer_config);
    RESET_FAKE(mocked_ledc_channel_config);
    RESET_FAKE(mocked_ledc_set_duty);
    RESET_FAKE(mocked_ledc_update_duty);
    RESET_FAKE(mocked_ledc_set_fade_with_time);
    RESET_FAKE(mocked_ledc_fade_start);
    RESET_FAKE(mocked_ledc_fade_func_install);
    RESET_FAKE(mocked_esp_console_cmd_register);
    RESET_FAKE(ledc_stop);
    RESET_FAKE(ledc_fade_stop);
    RESET_FAKE(ledc_fade_func_uninstall);
    RESET_FAKE(mocked_gpio_reset_pin);
    RESET_FAKE(mocked_gpio_set_direction);
    FFF_RESET_HISTORY();
}

// ---------------------------------------------------------------------------
// Test lifecycle
// ---------------------------------------------------------------------------

static BaseType_t create_rgb_task(TaskFunction_t fn, const char *name, uint32_t stack,
    void *arg, UBaseType_t priority, TaskHandle_t *out) {
    (void)fn; (void)name; (void)stack; (void)arg; (void)priority;
    *out = (TaskHandle_t)5; return pdPASS;
}
static BaseType_t stop_rgb_ack(QueueHandle_t queue, void *out, TickType_t wait) {
    (void)queue; (void)wait; *(int32_t *)out = 0; return pdTRUE;
}

void setUp(void) {
    ResetAllMocks();
    memset(&instance, 0, sizeof(instance));
    mocked_xSemaphoreCreateMutex_fake.return_val = (SemaphoreHandle_t)3;
    mocked_xSemaphoreTake_fake.return_val = pdTRUE;
    mocked_xTaskGetCurrentTaskHandle_fake.return_val = (TaskHandle_t)7;
    mocked_xTaskCreate_fake.custom_fake = create_rgb_task;
    mocked_xQueueReceive_fake.custom_fake = stop_rgb_ack;
    // Return pdTRUE for semaphore operations so Start() succeeds
    mocked_xSemaphoreCreateRecursiveMutex_fake.return_val = (SemaphoreHandle_t)1;
    mocked_xSemaphoreTakeRecursive_fake.return_val        = pdTRUE;
    mocked_xSemaphoreGiveRecursive_fake.return_val        = pdTRUE;
    // Return a non-NULL fake queue handle
    mocked_xQueueCreate_fake.return_val = (QueueHandle_t)2;
    mocked_xQueueSend_fake.return_val   = pdTRUE;
    // Return pdPASS so task creation succeeds
    mocked_xTaskCreate_fake.return_val  = pdPASS;
}

void tearDown(void) {
    ResetAllMocks();
    (void)RGBLEDHardware_Deinit();
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

void test_RGBLED_Lifecycle(void) {
    IRGBLED *service = GetRGBLEDService();
    TEST_ASSERT_NOT_NULL(service);
    TEST_ASSERT_NOT_NULL(service->base.Start);
    TEST_ASSERT_NOT_NULL(service->base.Stop);

    int32_t res = service->base.Start((IService*)service);
    TEST_ASSERT_EQUAL(0, res);

    res = service->base.Stop((IService*)service);
    TEST_ASSERT_EQUAL(0, res);
}

void test_RGBLED_SetColor(void) {
    IRGBLED *service = GetRGBLEDService();
    service->base.Start((IService*)service);

    RGBColor color = {255, 128, 0};
    esp_err_t err = service->SetColorRGB(service, color);
    TEST_ASSERT_EQUAL(ESP_OK, err);

    service->base.Stop((IService*)service);
}

void test_RGBLED_Fade(void) {
    IRGBLED *service = GetRGBLEDService();
    service->base.Start((IService*)service);

    RGBColor target = {0, 255, 128};
    esp_err_t err = service->FadeToColor(service, target, 100);
    TEST_ASSERT_EQUAL(ESP_OK, err);

    service->base.Stop((IService*)service);
}

void test_RGBLED_Brightness(void) {
    IRGBLED *service = GetRGBLEDService();
    service->base.Start((IService*)service);

    esp_err_t err = service->SetBrightness(service, 150);
    TEST_ASSERT_EQUAL(ESP_OK, err);

    service->base.Stop((IService*)service);
}

void test_RGBLED_Animations(void) {
    IRGBLED *service = GetRGBLEDService();
    service->base.Start((IService*)service);

    esp_err_t err = service->StartAnimation(service, RGBLED_ANIM_BREATHING, RGBLED_PRIO_HIGH, 1000);
    TEST_ASSERT_EQUAL(ESP_OK, err);

    service->base.Stop((IService*)service);
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------


void test_RGBLED_PresetChromaticities(void) {
    const RGBColor expected[] = {{0,0,255},{0,255,255},{0,255,0},{255,255,0},
                                 {255,128,0},{255,0,0},{255,0,255},{255,255,255}};
    for (int i = 0; i < RGB_COLOR_COUNT; ++i) {
        uint16_t x, y;
        RGBLEDColorEngine_PresetToXY((PresetColorIndex)i, &x, &y);
        RGBColor color = RGBLEDColorEngine_XYtoRGB(x, y);
        TEST_ASSERT_INT_WITHIN(2, expected[i].r, color.r);
        TEST_ASSERT_INT_WITHIN(2, expected[i].g, color.g);
        TEST_ASSERT_INT_WITHIN(2, expected[i].b, color.b);
        TEST_ASSERT_EQUAL(i, RGBLEDColorEngine_XYtoNearestPreset(x, y));
    }
}
void test_RGBLED_HueNegativeDifferences(void) {
    TEST_ASSERT_EQUAL(300, RGBLEDColorEngine_RGBtoHSV((RGBColor){255,0,255}).h);
    TEST_ASSERT_EQUAL(30, RGBLEDColorEngine_RGBtoHSV((RGBColor){255,128,0}).h);
    TEST_ASSERT_EQUAL(180, RGBLEDColorEngine_RGBtoHSV((RGBColor){0,255,255}).h);
}
void test_RGBLED_PhysicalChannelMapping(void) {
    RGBLEDConfig config = {.red_pin=2, .green_pin=10, .blue_pin=11, .frequency=5000, .resolution=8};
    TEST_ASSERT_EQUAL(ESP_OK, RGBLEDHardware_Init(&config));
    RESET_FAKE(mocked_ledc_set_duty);
    TEST_ASSERT_EQUAL(ESP_OK, RGBLEDHardware_SetColorImmediate((RGBColor){255,0,0},255));
    TEST_ASSERT_EQUAL(RGBLED_LEDC_CHANNEL_R, mocked_ledc_set_duty_fake.arg1_history[0]);
    TEST_ASSERT_EQUAL(255, mocked_ledc_set_duty_fake.arg2_history[0]);
    TEST_ASSERT_EQUAL(RGBLED_LEDC_CHANNEL_G, mocked_ledc_set_duty_fake.arg1_history[1]);
    TEST_ASSERT_EQUAL(0, mocked_ledc_set_duty_fake.arg2_history[1]);
    RGBLEDHardware_Deinit();
}

void test_RGBLED_StopTimeoutRetainsOwnershipAndRejectsCommands(void) {
    IRGBLED *service = GetRGBLEDService();
    TEST_ASSERT_EQUAL(0, service->base.Start(&service->base));
    QueueHandle_t queue = instance.cmdQueue;
    SemaphoreHandle_t mutex = instance.stateMutex;
    mocked_xQueueReceive_fake.custom_fake = NULL;
    mocked_xQueueReceive_fake.return_val = pdFALSE;
    TEST_ASSERT_NOT_EQUAL(0, service->base.Stop(&service->base));
    TEST_ASSERT_EQUAL_PTR((TaskHandle_t)5, atomic_load(&instance.worker.task));
    TEST_ASSERT_EQUAL_PTR(queue, instance.cmdQueue);
    TEST_ASSERT_EQUAL_PTR(mutex, instance.stateMutex);
    TEST_ASSERT_EQUAL(0, mocked_vTaskDelete_fake.call_count);
    TEST_ASSERT_NOT_EQUAL(ESP_OK, service->SetBrightness(service, 10));
    TEST_ASSERT_NOT_EQUAL(0, service->base.Start(&service->base));
    mocked_xQueueReceive_fake.custom_fake = stop_rgb_ack;
    TEST_ASSERT_EQUAL(0, service->base.Stop(&service->base));
    TEST_ASSERT_NULL(atomic_load(&instance.worker.task));
    TEST_ASSERT_EQUAL(0, service->base.Start(&service->base));
    TEST_ASSERT_EQUAL(0, service->base.Stop(&service->base));
}

static unsigned subscription_calls, removal_calls;
static int32_t removal_result;
static int32_t rgb_subscribe(IEventBus *self, const char *name, BusEventHandler handler) {
    (void)self; (void)name; (void)handler;
    return ++subscription_calls == 2 ? -1 : 0;
}
static int32_t rgb_unsubscribe(IEventBus *self, const char *name, BusEventHandler handler) {
    (void)self; (void)name; (void)handler; removal_calls++; return removal_result;
}
static IEventBus partial_bus = {.Subscribe = rgb_subscribe, .Unsubscribe = rgb_unsubscribe};
static IService *partial_bus_locator(const char *name) {
    return strcmp(name, SVC_NAME_EVENT_BUS) == 0 ? (IService *)&partial_bus : NULL;
}
void test_RGBLED_PartialSubscriptionBusyPreservesOwnership(void) {
    subscription_calls = removal_calls = 0;
    removal_result = EVENT_BUS_ERR_BUSY;
    mocked_ServiceLocator_GetService_fake.custom_fake = partial_bus_locator;
    IRGBLED *service = GetRGBLEDService();
    TEST_ASSERT_NOT_EQUAL(0, service->base.Start(&service->base));
    TEST_ASSERT_EQUAL(1, instance.subscriptions);
    TEST_ASSERT_EQUAL_PTR(&partial_bus, instance.eventBus);
    TEST_ASSERT_TRUE(instance.hardwareOwned);
    TEST_ASSERT_NULL(atomic_load(&instance.worker.task));
    TEST_ASSERT_NOT_EQUAL(0, service->base.Start(&service->base));
    removal_result = 0;
    TEST_ASSERT_EQUAL(0, service->base.Stop(&service->base));
    TEST_ASSERT_EQUAL(0, instance.subscriptions);
    TEST_ASSERT_NULL(instance.eventBus);
    TEST_ASSERT_FALSE(instance.hardwareOwned);
    TEST_ASSERT_EQUAL(2, removal_calls);
}

void test_RGBLED_ElapsedClockWrapAndHundredPulseCycles(void) {
    RGBColor color;
    RGBLEDAnimEngine_Init();
    TEST_ASSERT_EQUAL(ESP_OK, StartEngine(RGBLED_ANIM_PULSE, (RGBColor){255,0,0}, RGBLED_PRIO_LOW, 30));
    instance.animationClockMs = UINT32_MAX - 10U;
    TEST_ASSERT_TRUE(TickEngine(9, &color));
    TEST_ASSERT_FALSE(TickEngine(19, &color));
    TEST_ASSERT_EQUAL(ESP_OK, StartEngine(RGBLED_ANIM_PULSE, (RGBColor){255,0,0}, RGBLED_PRIO_LOW, 0));
    uint32_t start = instance.animationClockMs;
    for (unsigned cycle = 0; cycle < 100; ++cycle) {
        TEST_ASSERT_TRUE(TickEngine(start + cycle * 500U, &color));
        TEST_ASSERT_EQUAL(255, color.r);
        TEST_ASSERT_TRUE(TickEngine(start + cycle * 500U + 99U, &color));
        TEST_ASSERT_EQUAL(255, color.r);
        TEST_ASSERT_TRUE(TickEngine(start + cycle * 500U + 100U, &color));
        TEST_ASSERT_EQUAL(0, color.r);
        TEST_ASSERT_TRUE(TickEngine(start + cycle * 500U + 499U, &color));
        TEST_ASSERT_EQUAL(0, color.r);
    }
}

static esp_err_t fail_second_channel(ledc_channel_config_t *config) {
    (void)config; return mocked_ledc_channel_config_fake.call_count == 2 ? ESP_FAIL : ESP_OK;
}
void test_RGBLED_HardwarePartialSetupRollsBackAndRetries(void) {
    RGBLEDConfig config = RGBLED_GetDefaultConfig();
    mocked_ledc_channel_config_fake.custom_fake = fail_second_channel;
    TEST_ASSERT_EQUAL(ESP_FAIL, RGBLEDHardware_Init(&config));
    TEST_ASSERT_EQUAL(0, owned_channels);
    TEST_ASSERT_EQUAL(0, prepared_pins);
    TEST_ASSERT_FALSE(hardware_initialized);
    TEST_ASSERT_EQUAL(2, ledc_stop_fake.call_count);
    mocked_ledc_channel_config_fake.custom_fake = NULL;
    TEST_ASSERT_EQUAL(ESP_OK, RGBLEDHardware_Init(&config));
    TEST_ASSERT_EQUAL(ESP_OK, RGBLEDHardware_Deinit());
}
void test_RGBLED_HardwareCleanupFailureKeepsOwnership(void) {
    RGBLEDConfig config = RGBLED_GetDefaultConfig();
    TEST_ASSERT_EQUAL(ESP_OK, RGBLEDHardware_Init(&config));
    ledc_stop_fake.return_val = ESP_FAIL;
    TEST_ASSERT_EQUAL(ESP_FAIL, RGBLEDHardware_Deinit());
    TEST_ASSERT_EQUAL(7, owned_channels);
    TEST_ASSERT_EQUAL(7, prepared_pins);
    TEST_ASSERT_TRUE(fade_service_owned);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, RGBLEDHardware_Init(&config));
    ledc_stop_fake.return_val = ESP_OK;
    TEST_ASSERT_EQUAL(ESP_OK, RGBLEDHardware_Deinit());
    TEST_ASSERT_EQUAL(0, owned_channels);
    TEST_ASSERT_EQUAL(0, prepared_pins);
    TEST_ASSERT_FALSE(fade_service_owned);
    TEST_ASSERT_EQUAL(1, ledc_fade_func_uninstall_fake.call_count);
    mocked_ledc_fade_func_install_fake.return_val = ESP_ERR_INVALID_STATE;
    TEST_ASSERT_EQUAL(ESP_OK, RGBLEDHardware_Init(&config));
    mocked_gpio_reset_pin_fake.return_val = ESP_FAIL;
    TEST_ASSERT_EQUAL(ESP_FAIL, RGBLEDHardware_Deinit());
    TEST_ASSERT_EQUAL(7, prepared_pins);
    TEST_ASSERT_EQUAL(0, owned_channels);
    mocked_gpio_reset_pin_fake.return_val = ESP_OK;
    TEST_ASSERT_EQUAL(ESP_OK, RGBLEDHardware_Deinit());
    TEST_ASSERT_EQUAL(0, prepared_pins);
    /* A borrowed global fade service must not be uninstalled. */
    TEST_ASSERT_EQUAL(1, ledc_fade_func_uninstall_fake.call_count);
}
void test_RGBLED_HardwareOutputErrorsAreReturned(void) {
    RGBLEDConfig config = RGBLED_GetDefaultConfig();
    TEST_ASSERT_EQUAL(ESP_OK, RGBLEDHardware_Init(&config));
    mocked_ledc_set_duty_fake.return_val = ESP_FAIL;
    RESET_FAKE(mocked_ledc_update_duty);
    TEST_ASSERT_EQUAL(ESP_FAIL, RGBLEDHardware_SetColorImmediate((RGBColor){1,2,3}, 255));
    TEST_ASSERT_EQUAL(0, mocked_ledc_update_duty_fake.call_count);
    mocked_ledc_set_duty_fake.return_val = ESP_OK;
    mocked_ledc_update_duty_fake.return_val = ESP_FAIL;
    TEST_ASSERT_EQUAL(ESP_FAIL, RGBLEDHardware_TurnOff());
    mocked_ledc_update_duty_fake.return_val = ESP_OK;
    mocked_ledc_set_fade_with_time_fake.return_val = ESP_FAIL;
    TEST_ASSERT_EQUAL(ESP_FAIL, RGBLEDHardware_FadeToColor((RGBColor){1,2,3}, 255, 100));
    TEST_ASSERT_EQUAL(0, mocked_ledc_fade_start_fake.call_count);
    mocked_ledc_set_fade_with_time_fake.return_val = ESP_OK;
    mocked_ledc_fade_start_fake.return_val = ESP_FAIL;
    TEST_ASSERT_EQUAL(ESP_FAIL, RGBLEDHardware_FadeToColor((RGBColor){1,2,3}, 255, 100));
    TEST_ASSERT_EQUAL(7, active_fades);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, RGBLEDHardware_FadeToColor((RGBColor){0}, 255, UINT32_MAX));
    TEST_ASSERT_EQUAL(ESP_OK, RGBLEDHardware_Deinit());
    TEST_ASSERT_EQUAL(3, ledc_fade_stop_fake.call_count);
}
void test_RGBLED_HardwareValidationAndGpioError(void) {
    RGBLEDConfig config = RGBLED_GetDefaultConfig();
    config.resolution = 10;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, RGBLEDHardware_Init(&config));
    config = RGBLED_GetDefaultConfig();
    mocked_gpio_set_direction_fake.return_val = ESP_FAIL;
    TEST_ASSERT_EQUAL(ESP_FAIL, RGBLEDHardware_Init(&config));
    TEST_ASSERT_EQUAL(0, prepared_pins);
    TEST_ASSERT_EQUAL(0, mocked_ledc_channel_config_fake.call_count);
}
void test_RGBLED_OutputFailureAffectsHealthAndReadbackUntilRecovery(void) {
    IRGBLED *service = GetRGBLEDService();
    TEST_ASSERT_EQUAL(0, service->base.Start(&service->base));
    mocked_ledc_update_duty_fake.return_val = ESP_FAIL;
    RenderFrame(0);
    RGBLEDState state;
    TEST_ASSERT_NOT_EQUAL(0, service->base.CheckHealth(&service->base));
    TEST_ASSERT_EQUAL(ESP_FAIL, service->GetLEDState(service, &state));
    mocked_ledc_update_duty_fake.return_val = ESP_OK;
    RenderFrame(20);
    TEST_ASSERT_EQUAL(0, service->base.CheckHealth(&service->base));
    TEST_ASSERT_EQUAL(ESP_OK, service->GetLEDState(service, &state));
    TEST_ASSERT_EQUAL(0, service->base.Stop(&service->base));
}

static SystemSettings rgb_settings;
static esp_err_t rgb_snapshot_result, rgb_save_result;
static unsigned rgb_save_calls;
static esp_err_t rgb_snapshot(IConfigurationManager *self, SystemSettings *out) {
    (void)self;
    if (rgb_snapshot_result != ESP_OK) return rgb_snapshot_result;
    *out = rgb_settings; return ESP_OK;
}
static esp_err_t rgb_update_fields(IConfigurationManager *self, const char *block,
    const ConfigurationFieldUpdate *fields, size_t count, bool persist) {
    (void)self; rgb_save_calls++;
    TEST_ASSERT_EQUAL_STRING("hardware", block);
    TEST_ASSERT_TRUE(persist);
    for (size_t i = 0; i < count; i++) {
        TEST_ASSERT_TRUE(fields[i].offset == offsetof(HardwareConfigs, ext_led_mode) ||
                         fields[i].offset == offsetof(HardwareConfigs, ext_led_state) ||
                         fields[i].offset == offsetof(HardwareConfigs, ext_led_brightness));
        TEST_ASSERT_EQUAL(1, fields[i].size);
        if (rgb_save_result == ESP_OK) {
            uint8_t *setting = (uint8_t *)&rgb_settings.hardware + fields[i].offset;
            *setting = *(const uint8_t *)fields[i].data;
        }
    }
    return rgb_save_result;
}
static IConfigurationManager rgb_config = {.GetSnapshot=rgb_snapshot, .UpdateFields=rgb_update_fields};
static IService *rgb_config_locator(const char *name) {
    return strcmp(name, SVC_NAME_CONFIGURATION_MANAGER) == 0 ? (IService *)&rgb_config : NULL;
}
static void setup_rgb_settings(void) {
    memset(&rgb_settings, 0, sizeof(rgb_settings));
    rgb_settings.system.system_enabled = true;
    rgb_settings.hardware.ext_led_state = true;
    rgb_settings.hardware.ext_led_brightness = 123;
    rgb_settings.hardware.ext_led_startup_anim = RGBLED_ANIM_RAINBOW;
    rgb_snapshot_result = rgb_save_result = ESP_OK; rgb_save_calls = 0;
    mocked_ServiceLocator_GetService_fake.custom_fake = rgb_config_locator;
    s_mockBootAnimationIsRunning = false;
}
static RGBLEDCommand startup_command;
static BaseType_t capture_startup_command(QueueHandle_t queue, const void *item, TickType_t wait) {
    (void)queue; (void)wait;
    startup_command = *(const RGBLEDCommand *)item;
    return pdTRUE;
}
void test_RGBLED_BootRespectsGlobalGateAndConfiguredSelection(void) {
    setup_rgb_settings();
    s_mockBootAnimationIsRunning = true;
    rgb_settings.hardware.ext_led_mode = EXT_LED_MODE_OFF;
    rgb_settings.hardware.ext_led_startup_anim = RGBLED_ANIM_RAINBOW;
    mocked_xQueueSend_fake.custom_fake = capture_startup_command;
    IRGBLED *service = GetRGBLEDService();
    TEST_ASSERT_EQUAL(0, service->base.Start(&service->base));
    TEST_ASSERT_EQUAL(RGBLED_CMD_APPLY_SETTINGS, startup_command.type);
    HandleCommand(startup_command);
    TEST_ASSERT_FALSE(instance.state.isOn);
    TEST_ASSERT_FALSE(RGBLEDAnimEngine_IsActive());
    rgb_settings.hardware.ext_led_mode = EXT_LED_MODE_ANIMATION;
    rgb_settings.system.system_enabled = false;
    HandleCommand(startup_command);
    TEST_ASSERT_FALSE(instance.state.isOn);
    TEST_ASSERT_FALSE(RGBLEDAnimEngine_IsActive());
    rgb_settings.system.system_enabled = true;
    rgb_settings.hardware.ext_led_startup_anim = RGBLED_ANIM_NONE;
    HandleCommand(startup_command);
    TEST_ASSERT_FALSE(instance.state.isOn);
    TEST_ASSERT_FALSE(RGBLEDAnimEngine_IsActive());
    rgb_settings.hardware.ext_led_startup_anim = RGBLED_ANIM_PULSE;
    HandleCommand(startup_command);
    TEST_ASSERT_TRUE(instance.state.isOn);
    TEST_ASSERT_EQUAL(RGBLED_ANIM_PULSE, RGBLEDAnimEngine_GetCurrentType());
    TEST_ASSERT_EQUAL(123, instance.state.brightness);
    TEST_ASSERT_EQUAL(0, service->base.Stop(&service->base));
    s_mockBootAnimationIsRunning = false;
}
void test_RGBLED_AllBootEffectsRenderFromAnUninitializedBlackColor(void) {
    setup_rgb_settings();
    s_mockBootAnimationIsRunning = true;
    rgb_settings.hardware.ext_led_mode = EXT_LED_MODE_ANIMATION;
    mocked_xQueueSend_fake.custom_fake = capture_startup_command;
    IRGBLED *service = GetRGBLEDService();
    TEST_ASSERT_EQUAL(0, service->base.Start(&service->base));
    for (unsigned type = 1; type < RGBLED_ANIM_COUNT; ++type) {
        if (type == RGBLED_ANIM_TEMPERATURE) continue;
        TEST_ASSERT_EQUAL(ESP_OK, RGBLEDAnimEngine_Stop());
        instance.state.currentColor = (RGBColor){0, 0, 0};
        instance.settingsApplied = false;
        rgb_settings.hardware.ext_led_startup_anim = type;
        HandleCommand(startup_command);
        TEST_ASSERT_TRUE(instance.state.isOn);
        TEST_ASSERT_EQUAL(type, RGBLEDAnimEngine_GetCurrentType());
        bool visible = false;
        for (unsigned frame = 0; frame < 500; ++frame) {
            RGBColor color = {0};
            TEST_ASSERT_TRUE(RGBLEDAnimEngine_Tick(20, &color));
            visible |= color.r || color.g || color.b;
        }
        TEST_ASSERT_TRUE_MESSAGE(visible, "Every selectable boot effect must render visible frames from initial black");
    }
    TEST_ASSERT_EQUAL(ESP_OK, RGBLEDAnimEngine_Stop());
    RGBColor selected = {64, 128, 192};
    TEST_ASSERT_EQUAL(ESP_OK, StartEngine(RGBLED_ANIM_BREATHING, selected, RGBLED_PRIO_HIGH, 0));
    TEST_ASSERT_EQUAL_MEMORY(&selected, &current_anim.baseColor, sizeof(selected));
    rgb_settings.hardware.ext_led_brightness = 0;
    instance.settingsApplied = false;
    HandleCommand(startup_command);
    TEST_ASSERT_EQUAL(0, instance.state.brightness);
    rgb_settings.hardware.ext_led_mode = EXT_LED_MODE_OFF;
    HandleCommand(startup_command);
    TEST_ASSERT_FALSE(instance.state.isOn);
    TEST_ASSERT_FALSE(RGBLEDAnimEngine_IsActive());
    TEST_ASSERT_EQUAL(0, service->base.Stop(&service->base));
    s_mockBootAnimationIsRunning = false;
}

void test_RGBLED_BuiltinAndExternalRainbowShareBootEpoch(void) {
    LEDAnimationInstance builtin;
    TEST_ASSERT_EQUAL(ESP_OK, LEDAnimations_CreateInstance(RAINBOW_FLOW, &builtin));
    builtin.plugin.render_brightness = 255U;
    RainbowFlowAnim_BeginSynchronizedPhase(1000U);

    mocked_esp_timer_get_time_fake.return_val = 1511000;
    LEDColor builtin_color = builtin.plugin.Update(&builtin.plugin, 511U);
    mocked_esp_timer_get_time_fake.return_val = 1519000;
    RGBColor external_color = RainbowFlowAnim_Update(519U, 255U);
    TEST_ASSERT_EQUAL_UINT8(builtin_color.r, external_color.r);
    TEST_ASSERT_EQUAL_UINT8(builtin_color.g, external_color.g);
    TEST_ASSERT_EQUAL_UINT8(builtin_color.b, external_color.b);

    mocked_esp_timer_get_time_fake.return_val = 1520000;
    builtin_color = builtin.plugin.Update(&builtin.plugin, 9U);
    external_color = RainbowFlowAnim_Update(1U, 255U);
    TEST_ASSERT_EQUAL_UINT8(builtin_color.r, external_color.r);
    TEST_ASSERT_EQUAL_UINT8(builtin_color.g, external_color.g);
    TEST_ASSERT_EQUAL_UINT8(builtin_color.b, external_color.b);

    RainbowFlowAnim_EndSynchronizedPhase();
}

void test_RGBLED_RainbowRejoinsSharedTimelineAfterThreeSecondInterruption(void) {
    LEDAnimationInstance builtin;
    TEST_ASSERT_EQUAL(ESP_OK,LEDAnimations_CreateInstance(RAINBOW_FLOW,&builtin));
    builtin.plugin.render_brightness=255;
    RainbowFlowAnim_BeginSynchronizedPhase(1000U);
    /* A reset after an informational overlay must not shift the shared clock.
     * Cover more than 100 complete configured cycles and uint32 time wrap. */
    for (uint32_t elapsed=0;elapsed<101U*RAINBOW_CYCLE_DURATION_MS;elapsed+=20U) {
        if (elapsed%3000U==0) builtin.plugin.Reset(&builtin.plugin);
        mocked_esp_timer_get_time_fake.return_val=(int64_t)(1000U+elapsed)*1000;
        LEDColor internal=builtin.plugin.Update(&builtin.plugin,0);
        RGBColor external=RainbowFlowAnim_Update(3000,255);
        TEST_ASSERT_EQUAL_UINT8(internal.r,external.r);
        TEST_ASSERT_EQUAL_UINT8(internal.g,external.g);
        TEST_ASSERT_EQUAL_UINT8(internal.b,external.b);
    }
    RainbowFlowAnim_BeginSynchronizedPhase(UINT32_MAX-999U);
    mocked_esp_timer_get_time_fake.return_val=1000000;
    LEDColor internal=builtin.plugin.Update(&builtin.plugin,0);
    RGBColor external=RainbowFlowAnim_Update(UINT32_MAX,255);
    TEST_ASSERT_EQUAL_UINT8(internal.r,external.r);
    TEST_ASSERT_EQUAL_UINT8(internal.g,external.g);
    TEST_ASSERT_EQUAL_UINT8(internal.b,external.b);
    RainbowFlowAnim_EndSynchronizedPhase();
}
void test_RGBLED_RainbowCoversSixColorsAtConfiguredSpeed(void) {
    const RGBColor expected[] = {
        {255,0,0}, {255,255,0}, {0,255,0},
        {0,255,255}, {0,0,255}, {255,0,255}, {255,0,0}
    };
    RainbowFlowAnim_EndSynchronizedPhase();
    for (unsigned sector=0;sector<7U;++sector) {
        RainbowFlowAnim_Reset();
        RGBColor actual=RainbowFlowAnim_Update(sector*RAINBOW_CYCLE_DURATION_MS/6U,255U);
        TEST_ASSERT_EQUAL_UINT8(expected[sector].r,actual.r);
        TEST_ASSERT_EQUAL_UINT8(expected[sector].g,actual.g);
        TEST_ASSERT_EQUAL_UINT8(expected[sector].b,actual.b);
    }
    RainbowFlowAnim_Reset();
    RGBColor orange=RainbowFlowAnim_Update(RAINBOW_CYCLE_DURATION_MS/12U,255U);
    TEST_ASSERT_EQUAL_UINT8(255,orange.r);
    TEST_ASSERT_EQUAL_UINT8(128,orange.g);
    TEST_ASSERT_EQUAL_UINT8(0,orange.b);
    RainbowFlowAnim_Reset();
    RGBColor large=RainbowFlowAnim_Update(UINT32_MAX,255U);
    RainbowFlowAnim_Reset();
    RGBColor reduced=RainbowFlowAnim_Update(UINT32_MAX%RAINBOW_CYCLE_DURATION_MS,255U);
    TEST_ASSERT_EQUAL_MEMORY(&reduced,&large,sizeof(large));
}
void test_RGBLED_CliRejectsMalformedValuesAndQueueFailure(void) {
    setup_rgb_settings();
    IRGBLED *service = GetRGBLEDService();
    TEST_ASSERT_EQUAL(0, service->base.Start(&service->base));
    RESET_FAKE(mocked_xQueueSend);
    mocked_xQueueSend_fake.custom_fake = capture_startup_command;
    char *bad[] = {"", "-1", "+1", "256", "1x", " 1", "42949672960"};
    for (unsigned i=0; i<sizeof(bad)/sizeof(bad[0]); ++i) {
        char *brightness[] = {"rgbled_brightness", bad[i]};
        char *color[] = {"rgbled_set", "0", bad[i], "255"};
        TEST_ASSERT_NOT_EQUAL(0, cmd_rgbled_brightness(2, brightness));
        TEST_ASSERT_NOT_EQUAL(0, cmd_rgbled_set(4, color));
    }
    char *animation_bad[] = {"rgbled_anim", "256", "0"};
    TEST_ASSERT_NOT_EQUAL(0, cmd_rgbled_anim(3, animation_bad));
    char *duration_bad[] = {"rgbled_anim", "1", "4294967296"};
    TEST_ASSERT_NOT_EQUAL(0, cmd_rgbled_anim(3, duration_bad));
    char *brightness[] = {"rgbled_brightness", "255", "extra"};
    TEST_ASSERT_NOT_EQUAL(0, cmd_rgbled_brightness(3, brightness));
    TEST_ASSERT_EQUAL(0, mocked_xQueueSend_fake.call_count);
    TEST_ASSERT_EQUAL(0, cmd_rgbled_brightness(2, brightness));
    TEST_ASSERT_EQUAL(255, rgb_settings.hardware.ext_led_brightness);
    TEST_ASSERT_EQUAL(0, mocked_xQueueSend_fake.call_count);
    char *animation[] = {"rgbled_anim", "1", "4294967295"};
    TEST_ASSERT_EQUAL(0, cmd_rgbled_anim(3, animation));
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, startup_command.payload.animation.duration_ms);
    char *color[] = {"rgbled_set", "0", "127", "255"};
    TEST_ASSERT_EQUAL(0, cmd_rgbled_set(4, color));
    mocked_xQueueSend_fake.custom_fake = NULL;
    mocked_xQueueSend_fake.return_val = pdFALSE;
    TEST_ASSERT_NOT_EQUAL(0, cmd_rgbled_set(4, color));
    rgb_save_result = ESP_FAIL;
    TEST_ASSERT_NOT_EQUAL(0, cmd_rgbled_brightness(2, brightness));
    TEST_ASSERT_NOT_EQUAL(0, cmd_rgbled_anim(3, animation));
    TEST_ASSERT_EQUAL(0, service->base.Stop(&service->base));
}
void test_RGBLED_AlternateCliValidationAndPartialQueueResult(void) {
    setup_rgb_settings();
    IRGBLED *service = GetRGBLEDService();
    TEST_ASSERT_EQUAL(0, service->base.Start(&service->base));
    RESET_FAKE(mocked_xQueueSend);
    char *bad_color[] = {"rgb", "set", "256", "0", "0"};
    char *bad_brightness[] = {"rgb", "brightness", "-1"};
    char *bad_animation[] = {"rgb", "anim", "1", "4294967296"};
    char *bad_off[] = {"rgb", "off", "extra"};
    char *bad_status[] = {"rgb", "status", "extra"};
    TEST_ASSERT_NOT_EQUAL(0, cmd_rgb_handler(5, bad_color));
    TEST_ASSERT_NOT_EQUAL(0, cmd_rgb_handler(3, bad_brightness));
    TEST_ASSERT_NOT_EQUAL(0, cmd_rgb_handler(4, bad_animation));
    TEST_ASSERT_NOT_EQUAL(0, cmd_rgb_handler(3, bad_off));
    TEST_ASSERT_NOT_EQUAL(0, cmd_rgb_handler(3, bad_status));
    TEST_ASSERT_EQUAL(0, mocked_xQueueSend_fake.call_count);
    char *color[] = {"rgb", "set", "0", "127", "255", "100"};
    BaseType_t results[] = {pdTRUE, pdFALSE};
    SET_RETURN_SEQ(mocked_xQueueSend, results, 2);
    TEST_ASSERT_NOT_EQUAL(0, cmd_rgb_handler(6, color));
    TEST_ASSERT_EQUAL(2, mocked_xQueueSend_fake.call_count);
    RESET_FAKE(mocked_xQueueSend);
    mocked_xQueueSend_fake.return_val = pdFALSE;
    TEST_ASSERT_NOT_EQUAL(0, cmd_rgb_handler(6, color));
    TEST_ASSERT_EQUAL(1, mocked_xQueueSend_fake.call_count);
    char *brightness[] = {"rgb", "brightness", "255"};
    char *animation[] = {"rgb", "anim", "1", "0"};
    char *off[] = {"rgb", "off"};
    rgb_save_result = ESP_FAIL;
    TEST_ASSERT_NOT_EQUAL(0, cmd_rgb_handler(3, brightness));
    TEST_ASSERT_NOT_EQUAL(0, cmd_rgb_handler(4, animation));
    TEST_ASSERT_NOT_EQUAL(0, cmd_rgb_handler(2, off));
    mocked_xQueueSend_fake.return_val = pdTRUE;
    rgb_save_result = ESP_OK;
    TEST_ASSERT_EQUAL(0, cmd_rgb_handler(3, brightness));
    TEST_ASSERT_EQUAL(255, rgb_settings.hardware.ext_led_brightness);
    TEST_ASSERT_EQUAL(0, cmd_rgb_handler(4, animation));
    TEST_ASSERT_EQUAL(0, cmd_rgb_handler(2, off));
    TEST_ASSERT_EQUAL(0, service->base.Stop(&service->base));
    char *status[] = {"rgb", "status"};
    TEST_ASSERT_NOT_EQUAL(0, cmd_rgb_handler(2, status));
}
void test_RGBLED_AlternateCliRejectsMissingArgumentsAndCallbacks(void) {
    setup_rgb_settings();
    char *missing[] = {"rgb", NULL};
    TEST_ASSERT_NOT_EQUAL(0, cmd_rgb_handler(0, NULL));
    TEST_ASSERT_NOT_EQUAL(0, cmd_rgb_handler(2, missing));
    TEST_ASSERT_NOT_EQUAL(0, cmd_rgb_handler(7, missing));
    TEST_ASSERT_NOT_EQUAL(0, cmd_rgbled_set(4, NULL));
    TEST_ASSERT_NOT_EQUAL(0, cmd_rgbled_mode(2, NULL));
    TEST_ASSERT_NOT_EQUAL(0, cmd_rgbled_brightness(2, NULL));
    TEST_ASSERT_NOT_EQUAL(0, cmd_rgbled_anim(3, NULL));
    IRGBLED *service = GetRGBLEDService();
    IRGBLED saved = *service;
    char *status[] = {"rgb", "status"};
    char *color[] = {"rgb", "set", "1", "2", "3"};
    char *brightness[] = {"rgb", "brightness", "40"};
    char *animation[] = {"rgb", "anim", "1"};
    char *off[] = {"rgb", "off"};
    service->GetLEDState = NULL;
    service->SetColorRGB = NULL;
    service->SetBrightness = NULL;
    service->StartAnimation = NULL;
    service->TurnOff = NULL;
    TEST_ASSERT_NOT_EQUAL(0, cmd_rgb_handler(2, status));
    TEST_ASSERT_NOT_EQUAL(0, cmd_rgb_handler(5, color));
    TEST_ASSERT_EQUAL(0, cmd_rgb_handler(3, brightness));
    TEST_ASSERT_EQUAL(40, rgb_settings.hardware.ext_led_brightness);
    TEST_ASSERT_NOT_EQUAL(0, cmd_rgb_handler(3, animation));
    TEST_ASSERT_NOT_EQUAL(0, cmd_rgb_handler(2, off));
    service->SetColorRGB = saved.SetColorRGB;
    TEST_ASSERT_NOT_EQUAL(0, cmd_rgb_handler(5, color));
    TEST_ASSERT_EQUAL(0, mocked_xQueueSend_fake.call_count);
    *service = saved;
}
void test_RGBLED_ModePersistenceRejectsInvalidOrFailedWrites(void) {
    setup_rgb_settings();
    IRGBLED *service = GetRGBLEDService();
    TEST_ASSERT_EQUAL(ESP_OK, service->base.Start(&service->base));
    mocked_xQueueSend_fake.custom_fake = capture_startup_command;
    char *bad[] = {"", "-1", "256", "1x", "3"};
    for (unsigned i=0; i<5; ++i) {
        char *argv[] = {"rgbled_mode", bad[i]};
        TEST_ASSERT_NOT_EQUAL(0, cmd_rgbled_mode(2, argv));
    }
    TEST_ASSERT_EQUAL(0, rgb_save_calls);
    char *argv[] = {"rgbled_mode", "2"};
    rgb_save_result = ESP_FAIL;
    TEST_ASSERT_NOT_EQUAL(0, cmd_rgbled_mode(2, argv));
    TEST_ASSERT_EQUAL(0, rgb_settings.hardware.ext_led_mode);
    rgb_save_result = ESP_OK;
    TEST_ASSERT_EQUAL(0, cmd_rgbled_mode(2, argv));
    TEST_ASSERT_EQUAL(2, rgb_settings.hardware.ext_led_mode);
    TEST_ASSERT_FALSE(rgb_settings.hardware.ext_led_state);
    TEST_ASSERT_TRUE(startup_command.payload.force_mode);
    char *argv_anim[] = {"rgbled_mode", "1"};
    TEST_ASSERT_EQUAL(0, cmd_rgbled_mode(2, argv_anim));
    TEST_ASSERT_EQUAL(1, rgb_settings.hardware.ext_led_mode);
    TEST_ASSERT_TRUE(rgb_settings.hardware.ext_led_state);
    TEST_ASSERT_EQUAL(ESP_OK, service->base.Stop(&service->base));
}

void test_RGBLED_BrightnessSettingsPreserveManualColorUntilExplicitModeReselection(void) {
    setup_rgb_settings();
    instance.state.lastTemperature = 25;
    ApplySettings();
    HandleCommand((RGBLEDCommand){.type=RGBLED_CMD_SET_COLOR_RGB,.payload.rgb={255,0,0}});
    rgb_settings.hardware.ext_led_brightness = 37;
    ApplySettings();
    TEST_ASSERT_EQUAL(RGBLED_COLOR_MODE_RGB, instance.state.colorMode);
    TEST_ASSERT_EQUAL(255, instance.targetFrame.r);
    TEST_ASSERT_EQUAL(0, instance.targetFrame.g);
    TEST_ASSERT_EQUAL(37, instance.targetFrame.brightness);
    rgb_settings.system.zigbee_tx_profile = 2;
    ApplySettings();
    TEST_ASSERT_EQUAL(RGBLED_COLOR_MODE_RGB, instance.state.colorMode);
    TEST_ASSERT_EQUAL(255, instance.targetFrame.r);
    HandleCommand((RGBLEDCommand){.type=RGBLED_CMD_APPLY_SETTINGS,.payload.force_mode=true});
    TEST_ASSERT_EQUAL(RGBLED_COLOR_MODE_TEMPERATURE, instance.state.colorMode);
}

void test_RGBLED_GlobalOffCannotBeOverriddenByColorOrAnimationCommands(void) {
    setup_rgb_settings();
    rgb_settings.hardware.ext_led_mode = EXT_LED_MODE_OFF;
    ApplySettings();
    HandleCommand((RGBLEDCommand){.type=RGBLED_CMD_SET_COLOR_RGB,.payload.rgb={255,0,0}});
    TEST_ASSERT_FALSE(instance.state.isOn);
    TEST_ASSERT_EQUAL(0, instance.targetFrame.brightness);
    HandleCommand((RGBLEDCommand){.type=RGBLED_CMD_SET_COLOR_HSV,.payload.hsv={120,255,255}});
    TEST_ASSERT_FALSE(instance.state.isOn);
    TEST_ASSERT_EQUAL(0, instance.targetFrame.brightness);
    HandleCommand((RGBLEDCommand){.type=RGBLED_CMD_SET_ANIMATION,
        .payload.animation={RGBLED_ANIM_ALERT,RGBLED_PRIO_HIGH,1000}});
    TEST_ASSERT_FALSE(RGBLEDAnimEngine_IsActive());
    TEST_ASSERT_FALSE(instance.state.isOn);
}

void test_RGBLED_SettingsBrightnessDoesNotResetAnimationClock(void) {
    setup_rgb_settings();
    RGBLEDAnimEngine_Stop();
    rgb_settings.hardware.ext_led_mode = EXT_LED_MODE_ANIMATION;
    rgb_settings.hardware.ext_led_anim = RGBLED_ANIM_DOUBLE_BLINK_WHITE;
    mocked_esp_timer_get_time_fake.return_val = 100000;
    ApplySettings();
    RGBColor color;
    TEST_ASSERT_TRUE(TickEngine(250, &color));
    uint32_t clock = instance.animationClockMs;
    rgb_settings.hardware.ext_led_brightness = 37;
    mocked_esp_timer_get_time_fake.return_val = 900000;
    ApplySettings();
    TEST_ASSERT_EQUAL(clock, instance.animationClockMs);
    TEST_ASSERT_EQUAL(RGBLED_ANIM_DOUBLE_BLINK_WHITE, instance.state.activeAnim);
    TEST_ASSERT_EQUAL(37, instance.state.brightness);
    RGBLEDAnimEngine_Stop();
}

void test_RGBLED_SettingsRefreshSurvivesFullCommandQueueAndReportsSnapshotFailure(void) {
    setup_rgb_settings();
    IRGBLED *service = GetRGBLEDService();
    TEST_ASSERT_EQUAL(ESP_OK, service->base.Start(&service->base));
    mocked_xQueueSend_fake.return_val = pdFALSE;
    unsigned sends = mocked_xQueueSend_fake.call_count;
    OnSettingsChanged((BusEvent){0});
    OnSettingsChanged((BusEvent){0});
    TEST_ASSERT_EQUAL(sends, mocked_xQueueSend_fake.call_count);
    TEST_ASSERT_TRUE(atomic_exchange(&instance.settingsRefreshPending, false));
    rgb_snapshot_result = ESP_ERR_TIMEOUT;
    ApplySettings();
    TEST_ASSERT_TRUE(atomic_load(&instance.settingsRefreshPending));
    TEST_ASSERT_NOT_EQUAL(0, service->base.CheckHealth(&service->base));
    RGBLEDState state;
    TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT, service->GetLEDState(service, &state));
    rgb_snapshot_result = ESP_OK;
    instance.state.lastTemperature = 25;
    ApplySettings();
    TEST_ASSERT_EQUAL(0, service->base.CheckHealth(&service->base));
    TEST_ASSERT_EQUAL(ESP_OK, service->GetLEDState(service, &state));
    TEST_ASSERT_EQUAL(ESP_OK, service->base.Stop(&service->base));
}

void test_RGBLED_FrameDeadlineIncludesYieldAndTickWrap(void) {
    TEST_ASSERT_EQUAL(2, FrameWait(10,10,2));
    TEST_ASSERT_EQUAL(1, FrameWait(10,11,2));
    TEST_ASSERT_EQUAL(0, FrameWait(10,12,2));
    TEST_ASSERT_EQUAL(0, FrameWait(10,20,2));
    TEST_ASSERT_EQUAL(1, FrameWait((TickType_t)~0u,0,2));
}
void test_RGBLED_SettingsFailureAndMissingSensorDoNotFabricateState(void) {
    setup_rgb_settings();
    instance.state.lastTemperature = -127.0f;
    instance.state.brightness = 42;
    rgb_snapshot_result = ESP_ERR_TIMEOUT;
    ApplySettings();
    TEST_ASSERT_EQUAL(42, instance.state.brightness);
    rgb_snapshot_result = ESP_OK;
    ApplySettings();
    TEST_ASSERT_FLOAT_WITHIN(0.001f, -127.0f, instance.state.lastTemperature);
    TEST_ASSERT_FALSE(instance.state.isOn);
    TEST_ASSERT_EQUAL(0, instance.targetFrame.brightness);
    rgb_settings.hardware.ext_led_mode = EXT_LED_MODE_ANIMATION;
    rgb_settings.hardware.ext_led_anim = RGBLED_ANIM_BREATHING;
    TEST_ASSERT_EQUAL(ESP_OK, StartEngine(RGBLED_ANIM_ALERT, (RGBColor){255,0,0}, RGBLED_PRIO_CRITICAL, 0));
    instance.state.activeAnim = RGBLED_ANIM_ALERT;
    instance.state.animPriority = RGBLED_PRIO_CRITICAL;
    ApplySettings();
    TEST_ASSERT_EQUAL(RGBLED_ANIM_ALERT, instance.state.activeAnim);
    TEST_ASSERT_EQUAL(RGBLED_PRIO_CRITICAL, instance.state.animPriority);
    TEST_ASSERT_EQUAL(RGBLED_ANIM_ALERT, RGBLEDAnimEngine_GetCurrentType());
    RGBLEDAnimEngine_Stop();
}


#include "Infrastructure/support/console_registration_fixture.h"
static esp_err_t register_uncategorized(IDebugConsole *console, const esp_console_cmd_t *command) {
    return register_command(console, command, CMD_CAT_UNCATEGORIZED);
}
static esp_err_t rgb_registration_adapter(IDebugConsole *console) {
    console->RegisterCommand = register_uncategorized;
    mocked_ServiceLocator_GetService_fake.custom_fake = NULL;
    mocked_ServiceLocator_GetService_fake.return_val = (IService *)console;
    return RegisterConsoleCommands();
}
void test_RGBInternalRegistration_PreservesFailuresAndRetries(void) {
    check_registration_failures(rgb_registration_adapter, 5);
}
void test_RGBInternalRegistration_FailurePreventsWorkerCreation(void) {
    registration_console.RegisterCommand = register_uncategorized;
    IService *service = (IService *)GetRGBLEDService();
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

void test_RGBLED_PreviewOverlayExpiresAcrossWrapWithoutChangingPlayback(void) {
    setup_rgb_settings(); RGBLEDAnimEngine_Stop();
    rgb_settings.hardware.ext_led_mode=EXT_LED_MODE_ANIMATION;
    rgb_settings.hardware.ext_led_anim=RGBLED_ANIM_DOUBLE_BLINK_WHITE;
    ApplySettings();
    uint32_t clock=instance.animationClockMs;
    RGBLEDAnimType animation=instance.state.activeAnim;
    mocked_esp_timer_get_time_fake.return_val=(int64_t)(UINT32_MAX-100u)*1000;
    HandleCommand((RGBLEDCommand){.type=RGBLED_CMD_PREVIEW_COLOR,.payload.preview={{255,0,0},500}});
    TEST_ASSERT_TRUE(instance.previewActive);
    RenderFrame(250);
    TEST_ASSERT_TRUE(instance.previewActive);
    TEST_ASSERT_EQUAL(255,instance.renderedFrame.r);
    TEST_ASSERT_EQUAL(0,instance.renderedFrame.g);
    TEST_ASSERT_EQUAL(clock,instance.animationClockMs);
    TEST_ASSERT_EQUAL(animation,instance.state.activeAnim);
    RenderFrame(400);
    TEST_ASSERT_FALSE(instance.previewActive);
    TEST_ASSERT_EQUAL(animation,instance.state.activeAnim);
    RGBLEDAnimEngine_Stop();
}
void test_RGBLED_PreviewRejectsInvalidDurationsAndCannotOverrideGlobalOffOrCriticalAnimation(void) {
    IRGBLED *service=GetRGBLEDService();
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,service->PreviewColor(service,(RGBColor){255,0,0},0));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,service->PreviewColor(service,(RGBColor){255,0,0},RGBLED_COLOR_PREVIEW_MAX_MS+1));
    setup_rgb_settings(); RGBLEDAnimEngine_Stop();
    rgb_settings.hardware.ext_led_mode=EXT_LED_MODE_OFF; ApplySettings();
    RGBLEDCommand preview={.type=RGBLED_CMD_PREVIEW_COLOR,.payload.preview={{255,0,0},3000}};
    HandleCommand(preview); TEST_ASSERT_FALSE(instance.previewActive);
    rgb_settings.hardware.ext_led_mode=EXT_LED_MODE_TEMPERATURE; ApplySettings();
    HandleCommand((RGBLEDCommand){.type=RGBLED_CMD_SET_ANIMATION,.payload.animation={RGBLED_ANIM_ALERT,RGBLED_PRIO_CRITICAL,1000}});
    HandleCommand(preview); TEST_ASSERT_FALSE(instance.previewActive);
    RGBLEDAnimEngine_Stop();
}

void test_RGBLED_AnimationPreviewOwnsCatalogStateAndPreservesBasePlayback(void) {
    setup_rgb_settings(); RGBLEDAnimEngine_Stop();
    rgb_settings.hardware.ext_led_mode=EXT_LED_MODE_ANIMATION;
    rgb_settings.hardware.ext_led_anim=RGBLED_ANIM_DOUBLE_BLINK_WHITE; ApplySettings();
    uint32_t clock=instance.animationClockMs;
    mocked_esp_timer_get_time_fake.return_val=100000;
    HandleCommand((RGBLEDCommand){.type=RGBLED_CMD_PREVIEW_ANIMATION,.payload.animation={RGBLED_ANIM_SOLID_RED,RGBLED_PRIO_LOW,3000}});
    TEST_ASSERT_TRUE(instance.previewActive); TEST_ASSERT_TRUE(instance.previewAnimated);
    RenderFrame(450); TEST_ASSERT_EQUAL(255,instance.renderedFrame.r); TEST_ASSERT_EQUAL(0,instance.renderedFrame.g);
    TEST_ASSERT_EQUAL(RGBLED_ANIM_DOUBLE_BLINK_WHITE,instance.state.activeAnim);
    TEST_ASSERT_EQUAL(clock,instance.animationClockMs);
    RenderFrame(3100); TEST_ASSERT_FALSE(instance.previewActive);
    TEST_ASSERT_EQUAL(RGBLED_ANIM_DOUBLE_BLINK_WHITE,instance.state.activeAnim);
    IRGBLED *service=GetRGBLEDService();
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,service->PreviewAnimation(service,RGBLED_ANIM_TEMPERATURE,3000));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,service->PreviewAnimation(service,RGBLED_ANIM_SOLID_RED,0));
    RGBLEDAnimEngine_Stop();
}

void test_RGBLED_EveryCatalogPreviewUsesIndependentStateIncludingOff(void) {
    setup_rgb_settings(); RGBLEDAnimEngine_Stop();
    rgb_settings.hardware.ext_led_mode=EXT_LED_MODE_TEMPERATURE; ApplySettings();
    size_t count; const LEDAnimationCatalogEntry *entries=LEDAnimationCatalog_Entries(&count);
    for (size_t i=0;i<count;++i) {
        mocked_esp_timer_get_time_fake.return_val=100000;
        HandleCommand((RGBLEDCommand){.type=RGBLED_CMD_PREVIEW_ANIMATION,.payload.animation={(RGBLEDAnimType)entries[i].external_id,RGBLED_PRIO_LOW,3000}});
        TEST_ASSERT_TRUE(instance.previewActive);
        for (uint32_t t=100;t<3100;t+=20) RenderFrame(t);
        RenderFrame(3100); TEST_ASSERT_FALSE(instance.previewActive);
        TEST_ASSERT_EQUAL(RGBLED_COLOR_MODE_TEMPERATURE,instance.state.colorMode);
    }
}

void test_RGBLED_BrightnessPreviewPreservesOwnerAndExpires(void) {
    setup_rgb_settings();RGBLEDAnimEngine_Stop();rgb_settings.hardware.ext_led_mode=EXT_LED_MODE_TEMPERATURE;
    rgb_settings.hardware.ext_led_brightness=0;ApplySettings();
    instance.targetFrame=(LEDFrame){30,80,200,0};instance.renderedFrame=instance.targetFrame;
    mocked_esp_timer_get_time_fake.return_val=100000;
    HandleCommand((RGBLEDCommand){.type=RGBLED_CMD_PREVIEW_BRIGHTNESS,.payload.brightness_preview={200,500}});
    TEST_ASSERT_TRUE(instance.previewActive);TEST_ASSERT_TRUE(instance.previewBrightnessOnly);
    RenderFrame(400);TEST_ASSERT_EQUAL(200,instance.renderedFrame.brightness);
    TEST_ASSERT_EQUAL(0,instance.state.brightness);TEST_ASSERT_EQUAL(0,rgb_settings.hardware.ext_led_brightness);
    RenderFrame(600);TEST_ASSERT_FALSE(instance.previewActive);
    rgb_settings.hardware.ext_led_mode=EXT_LED_MODE_OFF;ApplySettings();
    HandleCommand((RGBLEDCommand){.type=RGBLED_CMD_PREVIEW_BRIGHTNESS,.payload.brightness_preview={200,500}});
    TEST_ASSERT_FALSE(instance.previewActive);
    /* Also verify that ext_led_state == false gates previews even if mode is not OFF */
    rgb_settings.hardware.ext_led_mode=EXT_LED_MODE_TEMPERATURE;
    rgb_settings.hardware.ext_led_state=false;
    ApplySettings();
    HandleCommand((RGBLEDCommand){.type=RGBLED_CMD_PREVIEW_BRIGHTNESS,.payload.brightness_preview={200,500}});
    TEST_ASSERT_FALSE(instance.previewActive);
    rgb_settings.hardware.ext_led_state=true;
    rgb_settings.hardware.ext_led_mode=EXT_LED_MODE_TEMPERATURE;ApplySettings();
    HandleCommand((RGBLEDCommand){.type=RGBLED_CMD_SET_ANIMATION,.payload.animation={RGBLED_ANIM_ALERT,RGBLED_PRIO_CRITICAL,1000}});
    HandleCommand((RGBLEDCommand){.type=RGBLED_CMD_PREVIEW_BRIGHTNESS,.payload.brightness_preview={200,500}});
    TEST_ASSERT_FALSE(instance.previewActive);
    RGBLEDAnimEngine_Stop();
    IRGBLED *service=GetRGBLEDService();
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,service->PreviewBrightness(service,200,0));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,service->PreviewBrightness(service,200,RGBLED_COLOR_PREVIEW_MAX_MS+1));
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_RGBLED_BrightnessPreviewPreservesOwnerAndExpires);
    RUN_TEST(test_RGBLED_EveryCatalogPreviewUsesIndependentStateIncludingOff);
    RUN_TEST(test_RGBLED_AnimationPreviewOwnsCatalogStateAndPreservesBasePlayback);
    RUN_TEST(test_RGBLED_AlternateCliValidationAndPartialQueueResult);
    RUN_TEST(test_RGBLED_AlternateCliRejectsMissingArgumentsAndCallbacks);
    RUN_TEST(test_RGBLED_CliRejectsMalformedValuesAndQueueFailure);
    RUN_TEST(test_RGBLED_BootRespectsGlobalGateAndConfiguredSelection);
    RUN_TEST(test_RGBLED_AllBootEffectsRenderFromAnUninitializedBlackColor);
    RUN_TEST(test_RGBLED_BuiltinAndExternalRainbowShareBootEpoch);
    RUN_TEST(test_RGBLED_RainbowRejoinsSharedTimelineAfterThreeSecondInterruption);
    RUN_TEST(test_RGBLED_RainbowCoversSixColorsAtConfiguredSpeed);
    RUN_TEST(test_RGBLED_HardwarePartialSetupRollsBackAndRetries);
    RUN_TEST(test_RGBLED_HardwareCleanupFailureKeepsOwnership);
    RUN_TEST(test_RGBLED_HardwareOutputErrorsAreReturned);
    RUN_TEST(test_RGBLED_HardwareValidationAndGpioError);
    RUN_TEST(test_RGBLED_OutputFailureAffectsHealthAndReadbackUntilRecovery);
    RUN_TEST(test_RGBLED_ModePersistenceRejectsInvalidOrFailedWrites);
    RUN_TEST(test_RGBLED_BrightnessSettingsPreserveManualColorUntilExplicitModeReselection);
    RUN_TEST(test_RGBLED_GlobalOffCannotBeOverriddenByColorOrAnimationCommands);
    RUN_TEST(test_RGBLED_SettingsBrightnessDoesNotResetAnimationClock);
    RUN_TEST(test_RGBLED_SettingsRefreshSurvivesFullCommandQueueAndReportsSnapshotFailure);
    RUN_TEST(test_RGBLED_FrameDeadlineIncludesYieldAndTickWrap);
    RUN_TEST(test_RGBLED_SettingsFailureAndMissingSensorDoNotFabricateState);
    RUN_TEST(test_RGBLED_PresetChromaticities);
    RUN_TEST(test_RGBLED_HueNegativeDifferences);
    RUN_TEST(test_RGBLED_PhysicalChannelMapping);
    RUN_TEST(test_RGBLED_ElapsedClockWrapAndHundredPulseCycles);
    RUN_TEST(test_RGBLED_StopTimeoutRetainsOwnershipAndRejectsCommands);
    RUN_TEST(test_RGBLED_PartialSubscriptionBusyPreservesOwnership);
    RUN_TEST(test_RGBLED_PreviewOverlayExpiresAcrossWrapWithoutChangingPlayback);
    RUN_TEST(test_RGBLED_PreviewRejectsInvalidDurationsAndCannotOverrideGlobalOffOrCriticalAnimation);
    RUN_TEST(test_RGBLED_Lifecycle);
    RUN_TEST(test_RGBLED_SetColor);
    RUN_TEST(test_RGBLED_Fade);
    RUN_TEST(test_RGBLED_Brightness);
    RUN_TEST(test_RGBLED_Animations);
    RUN_TEST(test_RGBInternalRegistration_PreservesFailuresAndRetries);
    RUN_TEST(test_RGBInternalRegistration_FailurePreventsWorkerCreation);
    return UNITY_END();
}

#endif // RACOOL_TEST_NATIVE
