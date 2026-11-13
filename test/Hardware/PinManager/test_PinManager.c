#include "Core/EventBus/IEventBus.h"
#include "unity.h"
#include "Infrastructure/support/test_mocks.h"
#include "Hardware/PinManager/IPinManager.h"
#include "Hardware/PinManager/PinManagerConfig.h"
#include "Hardware/PinManager/PinEvents.h"
#include "Core/DI/IDIRegistry.h"
#include "Core/ServiceLocator/ServiceNames.h"

static unsigned event_count;
static PinChangedEvent_t last_event;
static int32_t publish_isr(BusEvent event, bool *wake) {
    TEST_ASSERT_EQUAL_STRING(PIN_EVENT_CHANGED, event.eventName);
    TEST_ASSERT_EQUAL(sizeof(last_event), event.dataSize);
    memcpy(&last_event, event.data, sizeof(last_event));
    ++event_count;
    *wake = false;
    return 0;
}
static IEventBus bus = {.PublishFromISR = publish_isr};
static IService *resolve(IDIRegistry *self, const char *name) {
    return strcmp(name, SVC_NAME_EVENT_BUS) == 0 ? &bus.base : NULL;
}
static IDIRegistry registry = {.Resolve = resolve};
void setUp(void) {
    RESET_FAKE(mocked_gpio_config);
    RESET_FAKE(mocked_gpio_isr_handler_add);
    RESET_FAKE(mocked_gpio_get_level);
    RESET_FAKE(mocked_esp_timer_get_time);
    mocked_xSemaphoreCreateMutex_fake.return_val = (SemaphoreHandle_t)0x100;
    mocked_xSemaphoreTake_fake.return_val = pdTRUE;
    event_count = 0;
    IPinManager *pm = PinManager_GetInstance();
    TEST_ASSERT_EQUAL(0, pm->InitializeWithConfig(pm, PinManagerConfig_GetDefault(), &registry));
}
void tearDown(void) {}
void test_PinManager_Lifecycle(void) {
    IPinManager *pm = PinManager_GetInstance();
    TEST_ASSERT_EQUAL(SERVICE_STATE_INITIALIZED, pm->base.GetState(&pm->base));
    TEST_ASSERT_EQUAL(0, pm->base.Start(&pm->base));
    TEST_ASSERT_EQUAL(SERVICE_STATE_STARTED, pm->base.GetState(&pm->base));
    TEST_ASSERT_EQUAL(0, pm->base.Stop(&pm->base));
    TEST_ASSERT_EQUAL(SERVICE_STATE_STOPPED, pm->base.GetState(&pm->base));
}
void test_PinManager_Configure_Output(void) {
    IPinManager *pm = PinManager_GetInstance();
    RESET_FAKE(mocked_gpio_config);
    TEST_ASSERT_EQUAL(0, pm->Configure(pm, PIN_FUNC_BUILTIN_LED, PIN_MODE_OUTPUT, PIN_PULL_NONE));
    TEST_ASSERT_EQUAL(1, mocked_gpio_config_fake.call_count);
    mocked_gpio_config_fake.return_val = ESP_FAIL;
    TEST_ASSERT_NOT_EQUAL(0, pm->Configure(pm, PIN_FUNC_BUILTIN_LED, PIN_MODE_OUTPUT, PIN_PULL_NONE));
}
void test_PinManager_EventBus_Integration(void) {
    IPinManager *pm = PinManager_GetInstance();
    TEST_ASSERT_EQUAL(0, pm->Configure(pm, PIN_FUNC_ROTARY_SW, PIN_MODE_INPUT, PIN_PULL_UP));
    TEST_ASSERT_EQUAL(0, pm->AttachInterrupt(pm, PIN_FUNC_ROTARY_SW, (PinInterruptType_t)GPIO_INTR_ANYEDGE, NULL, NULL));
    TEST_ASSERT_NOT_NULL(mocked_gpio_isr_handler_add_fake.arg1_val);
    mocked_esp_timer_get_time_fake.return_val = 1000000;
    mocked_gpio_get_level_fake.return_val = 1;
    mocked_gpio_isr_handler_add_fake.arg1_val(mocked_gpio_isr_handler_add_fake.arg2_val);
    TEST_ASSERT_EQUAL(1, event_count);
    TEST_ASSERT_EQUAL(PIN_FUNC_ROTARY_SW, last_event.func);
    TEST_ASSERT_EQUAL(pm->GetPin(pm, PIN_FUNC_ROTARY_SW), last_event.pin);
    TEST_ASSERT_TRUE(last_event.level);
    mocked_gpio_isr_handler_add_fake.arg1_val(mocked_gpio_isr_handler_add_fake.arg2_val);
    TEST_ASSERT_EQUAL_MESSAGE(1, event_count, "Debounce must suppress a duplicate edge");
#if UINTPTR_MAX > UINT32_MAX
    uintptr_t alias = (uintptr_t)UINT32_MAX + 1 + PIN_FUNC_ROTARY_SW;
    mocked_esp_timer_get_time_fake.return_val = 2000000;
    mocked_gpio_isr_handler_add_fake.arg1_val((void *)alias);
    TEST_ASSERT_EQUAL_MESSAGE(1, event_count, "A wide invalid ISR tag must not alias a valid function");
#endif

}
