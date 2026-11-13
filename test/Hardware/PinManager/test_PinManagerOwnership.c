#include "Infrastructure/support/test_mocks.h"
#include "unity.h"
#include <string.h>
#include "../../../src/Hardware/PinManager/PinManager.c"
#include "../../../src/Hardware/PinManager/PinManagerConfig.c"

static PinManagerConfig config;
static unsigned callback_count, publication_count;
static int callback_argument;
static int32_t fixture_publish(BusEvent event, bool *wake) {
    TEST_ASSERT_EQUAL_UINT32(1, pinManagerInstance.pinMap[PIN_FUNC_ROTARY_SW].inFlight);
    TEST_ASSERT_EQUAL(sizeof(PinChangedEvent_t), event.dataSize);
    publication_count++;
    *wake = false;
    return 0;
}
static IEventBus bus = {.PublishFromISR = fixture_publish};
static void fixture_callback(PinFunction_t function, bool level, void *argument) {
    (void)level;
    TEST_ASSERT_EQUAL(PIN_FUNC_ROTARY_SW, function);
    TEST_ASSERT_EQUAL_PTR(&callback_argument, argument);
    TEST_ASSERT_EQUAL_UINT32(1, pinManagerInstance.pinMap[function].inFlight);
    callback_count++;
}
void setUp(void) {
    RESET_FAKE(mocked_xSemaphoreCreateMutex);
    RESET_FAKE(mocked_xSemaphoreTake);
    RESET_FAKE(mocked_xSemaphoreGive);
    RESET_FAKE(mocked_gpio_config);
    RESET_FAKE(mocked_gpio_set_level);
    RESET_FAKE(mocked_gpio_get_level);
    RESET_FAKE(mocked_gpio_set_intr_type);
    RESET_FAKE(mocked_gpio_isr_handler_add);
    RESET_FAKE(mocked_gpio_isr_handler_remove);
    RESET_FAKE(mocked_gpio_install_isr_service);
    RESET_FAKE(mocked_esp_timer_get_time);
    mocked_xSemaphoreCreateMutex_fake.return_val = (SemaphoreHandle_t)0x100;
    mocked_xSemaphoreTake_fake.return_val = pdTRUE;
    TEST_ASSERT_NOT_NULL(PinManager_GetInstance());
    /* Isolated native SDK boundaries own no physical callbacks between cases. */
    memset(pinManagerInstance.pinMap, 0, sizeof(pinManagerInstance.pinMap));
    pinManagerInstance.resourceRegistry = NULL;
    pinManagerInstance.eventBus = NULL;
    pinManagerInstance.state = SERVICE_STATE_UNINITIALIZED;
    atomic_store(&pinInitializing, false);
    atomic_store(&pinConfigurationApplied, false);
    config = PinManagerConfig_GetDefault();
    for (int i = 1; i < PIN_FUNC_MAX_COUNT; ++i) config.entries[i].pin = GPIO_NUM_NC;
    config.entries[PIN_FUNC_ROTARY_SW].pin = GPIO_NUM_3;
    config.entries[PIN_FUNC_RELAY].pin = GPIO_NUM_0;
    TEST_ASSERT_EQUAL(0, PinManager_InitializeWithConfig(&pinManagerInstance.base, config, NULL));
    pinManagerInstance.eventBus = &bus;
    callback_count = publication_count = 0;
}
void tearDown(void) {}
static void attach(void) {
    TEST_ASSERT_EQUAL(0, PinManager_AttachInterrupt(&pinManagerInstance.base, PIN_FUNC_ROTARY_SW,
        PIN_INTR_ANYEDGE, fixture_callback, &callback_argument));
}
void test_PinManagerOwnership_CallbackSnapshotAndWideDebounce(void) {
    attach();
    TEST_ASSERT_EQUAL(0, PinManager_SetDebounce(&pinManagerInstance.base, PIN_FUNC_ROTARY_SW, UINT32_MAX));
    gpio_isr_handler((void *)(uintptr_t)PIN_FUNC_ROTARY_SW);
    TEST_ASSERT_EQUAL_UINT(1, callback_count);
    TEST_ASSERT_EQUAL_UINT(1, publication_count);
    TEST_ASSERT_EQUAL_UINT32(0, pinManagerInstance.pinMap[PIN_FUNC_ROTARY_SW].inFlight);
    mocked_esp_timer_get_time_fake.return_val = (int64_t)((uint64_t)UINT32_MAX * 1000ULL - 1);
    gpio_isr_handler((void *)(uintptr_t)PIN_FUNC_ROTARY_SW);
    TEST_ASSERT_EQUAL_UINT(1, callback_count);
    mocked_esp_timer_get_time_fake.return_val++;
    gpio_isr_handler((void *)(uintptr_t)PIN_FUNC_ROTARY_SW);
    TEST_ASSERT_EQUAL_UINT(2, callback_count);
}
void test_PinManagerOwnership_AttachFailureDoesNotPublishArguments(void) {
    mocked_gpio_isr_handler_add_fake.return_val = ESP_FAIL;
    TEST_ASSERT_EQUAL(-3, PinManager_AttachInterrupt(&pinManagerInstance.base, PIN_FUNC_ROTARY_SW,
        PIN_INTR_ANYEDGE, fixture_callback, &callback_argument));
    PinInternalInfo_t *info = &pinManagerInstance.pinMap[PIN_FUNC_ROTARY_SW];
    TEST_ASSERT_NULL(info->handler);
    TEST_ASSERT_NULL(info->handlerArg);
    TEST_ASSERT_FALSE(info->dispatchEnabled);
    gpio_isr_handler((void *)(uintptr_t)PIN_FUNC_ROTARY_SW);
    TEST_ASSERT_EQUAL_UINT(0, callback_count);
    mocked_gpio_isr_handler_add_fake.return_val = ESP_OK;
    attach();
}
void test_PinManagerOwnership_DetachSDKFailureAndRetry(void) {
    attach();
    mocked_gpio_isr_handler_remove_fake.return_val = ESP_FAIL;
    TEST_ASSERT_EQUAL(-3, PinManager_DetachInterrupt(&pinManagerInstance.base, PIN_FUNC_ROTARY_SW));
    TEST_ASSERT_TRUE(pinManagerInstance.pinMap[PIN_FUNC_ROTARY_SW].dispatchEnabled);
    gpio_isr_handler((void *)(uintptr_t)PIN_FUNC_ROTARY_SW);
    TEST_ASSERT_EQUAL_UINT(1, callback_count);
    mocked_gpio_isr_handler_remove_fake.return_val = ESP_OK;
    mocked_gpio_set_intr_type_fake.return_val = ESP_FAIL;
    TEST_ASSERT_EQUAL(-3, PinManager_DetachInterrupt(&pinManagerInstance.base, PIN_FUNC_ROTARY_SW));
    TEST_ASSERT_FALSE(pinManagerInstance.pinMap[PIN_FUNC_ROTARY_SW].interruptAttached);
    TEST_ASSERT_NOT_NULL(pinManagerInstance.pinMap[PIN_FUNC_ROTARY_SW].handlerArg);
    gpio_isr_handler((void *)(uintptr_t)PIN_FUNC_ROTARY_SW);
    TEST_ASSERT_EQUAL_UINT(1, callback_count);
    mocked_gpio_set_intr_type_fake.return_val = ESP_OK;
    TEST_ASSERT_EQUAL(0, PinManager_DetachInterrupt(&pinManagerInstance.base, PIN_FUNC_ROTARY_SW));
    TEST_ASSERT_EQUAL_UINT(2, mocked_gpio_isr_handler_remove_fake.call_count);
    TEST_ASSERT_NULL(pinManagerInstance.pinMap[PIN_FUNC_ROTARY_SW].handlerArg);
}
void test_PinManagerOwnership_CapturedCallbackRetainsLifetime(void) {
    attach();
    PinInternalInfo_t *info = &pinManagerInstance.pinMap[PIN_FUNC_ROTARY_SW];
    /* Inject a captured reference; this is not a blocked physical ISR. */
    info->inFlight = 1;
    TEST_ASSERT_EQUAL(-4, PinManager_DetachInterrupt(&pinManagerInstance.base, PIN_FUNC_ROTARY_SW));
    TEST_ASSERT_EQUAL_PTR(&callback_argument, info->handlerArg);
    TEST_ASSERT_EQUAL(-4, PinManager_Stop(&pinManagerInstance.base.base));
    TEST_ASSERT_EQUAL(-4, PinManager_InitializeWithConfig(&pinManagerInstance.base, config, NULL));
    TEST_ASSERT_EQUAL(-4, PinManager_SetPin(&pinManagerInstance.base, PIN_FUNC_ROTARY_SW, GPIO_NUM_4));
    info->inFlight = 0;
    TEST_ASSERT_EQUAL(0, PinManager_DetachInterrupt(&pinManagerInstance.base, PIN_FUNC_ROTARY_SW));
    TEST_ASSERT_NULL(info->handler);
    TEST_ASSERT_NULL(info->handlerArg);
    TEST_ASSERT_EQUAL(0, PinManager_Stop(&pinManagerInstance.base.base));
}
void test_PinManagerOwnership_RejectsReplacementAndOwnedConfiguration(void) {
    attach();
    attach();
    TEST_ASSERT_EQUAL_UINT(1, mocked_gpio_isr_handler_add_fake.call_count);
    TEST_ASSERT_EQUAL(-4, PinManager_AttachInterrupt(&pinManagerInstance.base, PIN_FUNC_ROTARY_SW,
        PIN_INTR_ANYEDGE, fixture_callback, NULL));
    TEST_ASSERT_EQUAL(-4, PinManager_Configure(&pinManagerInstance.base, PIN_FUNC_ROTARY_SW, PIN_MODE_INPUT, PIN_PULL_UP));
    TEST_ASSERT_EQUAL(-4, PinManager_SetPin(&pinManagerInstance.base, PIN_FUNC_ROTARY_SW, GPIO_NUM_4));
    TEST_ASSERT_EQUAL(-4, PinManager_InitializeWithConfig(&pinManagerInstance.base, config, NULL));
}
void test_PinManagerOwnership_PartialInitCannotStart(void) {
    mocked_gpio_config_fake.return_val = ESP_FAIL;
    TEST_ASSERT_EQUAL(-3, PinManager_InitializeWithConfig(&pinManagerInstance.base, config, NULL));
    TEST_ASSERT_FALSE(atomic_load(&pinInitializing));
    TEST_ASSERT_FALSE(atomic_load(&pinConfigurationApplied));
    TEST_ASSERT_NOT_EQUAL(0, PinManager_Start(&pinManagerInstance.base.base));
    mocked_gpio_config_fake.return_val = ESP_OK;
    mocked_gpio_set_level_fake.return_val = ESP_FAIL;
    TEST_ASSERT_EQUAL(-3, PinManager_InitializeWithConfig(&pinManagerInstance.base, config, NULL));
    TEST_ASSERT_NOT_EQUAL(0, PinManager_Start(&pinManagerInstance.base.base));
    mocked_gpio_set_level_fake.return_val = ESP_OK;
    TEST_ASSERT_EQUAL(0, PinManager_InitializeWithConfig(&pinManagerInstance.base, config, NULL));
    TEST_ASSERT_EQUAL(0, PinManager_Start(&pinManagerInstance.base.base));
}
void test_PinManagerOwnership_InitializationAdmission(void) {
    atomic_store(&pinInitializing, true);
    TEST_ASSERT_EQUAL(-4, PinManager_InitializeWithConfig(&pinManagerInstance.base, config, NULL));
    TEST_ASSERT_TRUE(atomic_load(&pinInitializing));
    TEST_ASSERT_EQUAL(-4, PinManager_AttachInterrupt(&pinManagerInstance.base, PIN_FUNC_ROTARY_SW, PIN_INTR_ANYEDGE, NULL, NULL));
    TEST_ASSERT_EQUAL(-4, PinManager_Stop(&pinManagerInstance.base.base));
    TEST_ASSERT_FALSE(PinManager_Validate(&pinManagerInstance.base));
    atomic_store(&pinInitializing, false);
}
void test_PinManagerOwnership_GeometryRejectedBeforeSDKMutation(void) {
    config.entries[PIN_FUNC_BUILTIN_LED].pin = config.entries[PIN_FUNC_RELAY].pin;
    RESET_FAKE(mocked_gpio_config);
    TEST_ASSERT_EQUAL(-1, PinManager_InitializeWithConfig(&pinManagerInstance.base, config, NULL));
    TEST_ASSERT_EQUAL_UINT(0, mocked_gpio_config_fake.call_count);
    TEST_ASSERT_TRUE(atomic_load(&pinConfigurationApplied));
    TEST_ASSERT_EQUAL(GPIO_NUM_NC, PinManager_GetPin(&pinManagerInstance.base, PIN_FUNC_BUILTIN_LED));
}
void test_PinManagerOwnership_StartedIdenticalInitPreservesCallbacks(void) {
    TEST_ASSERT_EQUAL(0, PinManager_Start(&pinManagerInstance.base.base));
    attach();
    RESET_FAKE(mocked_gpio_config);
    TEST_ASSERT_EQUAL(0, PinManager_InitializeWithConfig(&pinManagerInstance.base, config, NULL));
    TEST_ASSERT_EQUAL_UINT(0, mocked_gpio_config_fake.call_count);
    TEST_ASSERT_TRUE(pinManagerInstance.pinMap[PIN_FUNC_ROTARY_SW].dispatchEnabled);
    gpio_isr_handler((void *)(uintptr_t)PIN_FUNC_ROTARY_SW);
    TEST_ASSERT_EQUAL_UINT(1, callback_count);
    config.defaultDebounceMs++;
    TEST_ASSERT_EQUAL(-4, PinManager_InitializeWithConfig(&pinManagerInstance.base, config, NULL));
}
