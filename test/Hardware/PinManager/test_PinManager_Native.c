#ifdef RACOOL_TEST_NATIVE

#include <unity.h>
#include "Hardware/PinManager/IPinManager.h"
#include "Hardware/PinManager/PinDefinitions.h"
#include "Hardware/PinManager/PinManagerConfig.h"
#include <string.h>
#include <stdio.h>
#include "Infrastructure/support/test_mocks.h"
#include "Hardware/ResourceRegistry/IResourceRegistry.h"
#include "Core/ServiceLocator/ServiceNames.h"

void setUp(void) {
    RESET_FAKE(mocked_xSemaphoreCreateMutex);
    RESET_FAKE(mocked_xSemaphoreTake);
    RESET_FAKE(mocked_gpio_config);
    RESET_FAKE(mocked_gpio_set_level);
    mocked_xSemaphoreCreateMutex_fake.return_val = (SemaphoreHandle_t)0x100;
    mocked_xSemaphoreTake_fake.return_val = pdTRUE;
}

void tearDown(void) {
    /* All injected interfaces have static lifetime. */
}

void test_PinManager_DefaultConfig(void) {
    PinManagerConfig config = PinManagerConfig_GetDefault();
    
    // The relay pin comes from SystemConfigs.h.
    TEST_ASSERT_EQUAL(0, config.entries[PIN_FUNC_RELAY].pin);
    TEST_ASSERT_EQUAL(PIN_MODE_OUTPUT, config.entries[PIN_FUNC_RELAY].mode);
    
    // The OLED clock pin comes from SystemConfigs.h.
    TEST_ASSERT_EQUAL(7, config.entries[PIN_FUNC_OLED_SCL].pin);
    TEST_ASSERT_EQUAL(PIN_MODE_OPEN_DRAIN, config.entries[PIN_FUNC_OLED_SCL].mode);
}

void test_PinManager_Validation_Success(void) {
    IPinManager *pm = PinManager_GetInstance();
    PinManagerConfig config = PinManagerConfig_GetDefault();
    
    int32_t res = pm->InitializeWithConfig(pm, config, NULL);
    TEST_ASSERT_EQUAL(0, res);
    TEST_ASSERT_TRUE(pm->Validate(pm));
}

void test_PinManager_Validation_Conflict(void) {
    IPinManager *pm = PinManager_GetInstance();
    PinManagerConfig config = PinManagerConfig_GetDefault();
    
    // Assign one GPIO to two unrelated functions.
    config.entries[PIN_FUNC_RELAY].pin = 10;
    config.entries[PIN_FUNC_BUILTIN_LED].pin = 10;
    
    RESET_FAKE(mocked_gpio_config);
    TEST_ASSERT_EQUAL(-1, pm->InitializeWithConfig(pm, config, NULL));
    TEST_ASSERT_EQUAL_UINT(0, mocked_gpio_config_fake.call_count);
    TEST_ASSERT_TRUE(pm->Validate(pm));
}

void test_PinManager_I2C_Sharing(void) {
    IPinManager *pm = PinManager_GetInstance();
    PinManagerConfig config = PinManagerConfig_GetDefault();
    
    // Corresponding I2C signals may share a GPIO.
    config.entries[PIN_FUNC_MLX90614_SCL].pin = 21;
    config.entries[PIN_FUNC_OLED_SCL].pin = 21;
    config.entries[PIN_FUNC_MLX90614_SDA].pin = 22;
    config.entries[PIN_FUNC_OLED_SDA].pin = 22;
    
    pm->InitializeWithConfig(pm, config, NULL);
    TEST_ASSERT_TRUE(pm->Validate(pm));
}

void test_PinManager_GetSetPin(void) {
    IPinManager *pm = PinManager_GetInstance();
    TEST_ASSERT_EQUAL(0, pm->SetPin(pm, PIN_FUNC_RELAY, GPIO_NUM_20));
    TEST_ASSERT_EQUAL(GPIO_NUM_20, pm->GetPin(pm, PIN_FUNC_RELAY));
    TEST_ASSERT_EQUAL(-1, pm->SetPin(pm, PIN_FUNC_RELAY, GPIO_NUM_5));
    TEST_ASSERT_EQUAL(GPIO_NUM_20, pm->GetPin(pm, PIN_FUNC_RELAY));
}

void test_PinManager_RejectsCrossedI2CAndInvalidEnums(void) {
    IPinManager *pm = PinManager_GetInstance();
    PinManagerConfig config = PinManagerConfig_GetDefault();
    config.entries[PIN_FUNC_MLX90614_SDA].pin = config.entries[PIN_FUNC_MLX90614_SCL].pin;
    TEST_ASSERT_NOT_EQUAL(0, pm->InitializeWithConfig(pm, config, NULL));
    TEST_ASSERT_TRUE(pm->Validate(pm));
    TEST_ASSERT_EQUAL(-1, pm->SetPin(pm, PIN_FUNC_RELAY, (gpio_num_t)64));
    TEST_ASSERT_EQUAL(-1, pm->Configure(pm, PIN_FUNC_RELAY, (PinMode_t)99, PIN_PULL_NONE));
    TEST_ASSERT_EQUAL(-1, pm->Configure(pm, PIN_FUNC_RELAY, PIN_MODE_OUTPUT, (PinPull_t)99));
}

static unsigned reserve_calls, fail_reserve_at, release_calls;
void test_PinManager_LockFailurePreservesMapping(void) {
    IPinManager *pm = PinManager_GetInstance();
    gpio_num_t original = pm->GetPin(pm, PIN_FUNC_RELAY);
    mocked_xSemaphoreTake_fake.return_val = pdFALSE;
    TEST_ASSERT_EQUAL(GPIO_NUM_NC, pm->GetPin(pm, PIN_FUNC_RELAY));
    TEST_ASSERT_EQUAL(-4, pm->SetPin(pm, PIN_FUNC_RELAY, GPIO_NUM_9));
    TEST_ASSERT_EQUAL(-4, pm->Configure(pm, PIN_FUNC_RELAY, PIN_MODE_OUTPUT, PIN_PULL_NONE));
    TEST_ASSERT_FALSE(pm->Validate(pm));
    mocked_xSemaphoreTake_fake.return_val = pdTRUE;
    TEST_ASSERT_EQUAL(original, pm->GetPin(pm, PIN_FUNC_RELAY));
}
static bool fail_release_once;
static unsigned references[PIN_FUNC_MAX_COUNT];
static int32_t fixture_reserve(IResourceRegistry *self, ResourceId_t resource, const char *owner, bool shared) {
    (void)self; (void)shared;
    unsigned function = 0;
    TEST_ASSERT_EQUAL(1, sscanf(owner, "PF_%u", &function));
    TEST_ASSERT_TRUE(function > PIN_FUNC_NONE && function < PIN_FUNC_MAX_COUNT);
    TEST_ASSERT_EQUAL(RESOURCE_TYPE_GPIO, resource.type);
    if (++reserve_calls == fail_reserve_at) return -3;
    references[function]++;
    return 0;
}
static int32_t fixture_release(IResourceRegistry *self, ResourceId_t resource, const char *owner) {
    (void)self; (void)resource;
    unsigned function = 0;
    TEST_ASSERT_EQUAL(1, sscanf(owner, "PF_%u", &function));
    release_calls++;
    if (fail_release_once) { fail_release_once = false; return -3; }
    TEST_ASSERT_GREATER_THAN(0, references[function]);
    references[function]--;
    return 0;
}
static IResourceRegistry fixture_resources = {.Reserve = fixture_reserve, .Release = fixture_release};
static IService *fixture_resolve(IDIRegistry *self, const char *name) {
    (void)self;
    return strcmp(name, SVC_NAME_RESOURCE_REGISTRY) == 0 ? &fixture_resources.base : NULL;
}
static IDIRegistry fixture_registry = {.Resolve = fixture_resolve};
void test_PinManager_ReservationRollbackRetryAndIdempotence(void) {
    IPinManager *pm = PinManager_GetInstance();
    PinManagerConfig config = PinManagerConfig_GetDefault();
    for (int i = 1; i < PIN_FUNC_MAX_COUNT; ++i) config.entries[i].pin = GPIO_NUM_NC;
    config.entries[PIN_FUNC_RELAY].pin = GPIO_NUM_0;
    config.entries[PIN_FUNC_BUILTIN_LED].pin = GPIO_NUM_1;
    config.entries[PIN_FUNC_I2C0_SCL].pin = GPIO_NUM_7;
    config.entries[PIN_FUNC_OLED_SCL].pin = GPIO_NUM_7;
    reserve_calls = release_calls = 0;
    fail_reserve_at = 3;
    fail_release_once = true;
    memset(references, 0, sizeof(references));
    TEST_ASSERT_EQUAL(-2, pm->InitializeWithConfig(pm, config, &fixture_registry));
    TEST_ASSERT_EQUAL_UINT(3, reserve_calls);
    TEST_ASSERT_EQUAL_UINT(2, release_calls);
    TEST_ASSERT_EQUAL_UINT(0, references[PIN_FUNC_RELAY]);
    TEST_ASSERT_EQUAL_UINT(1, references[PIN_FUNC_BUILTIN_LED]);
    reserve_calls = 0; fail_reserve_at = 0;
    TEST_ASSERT_EQUAL(0, pm->InitializeWithConfig(pm, config, &fixture_registry));
    TEST_ASSERT_EQUAL_UINT(3, reserve_calls);
    TEST_ASSERT_TRUE(pm->Validate(pm));
    TEST_ASSERT_TRUE(pm->Validate(pm));
    TEST_ASSERT_EQUAL_UINT(3, reserve_calls);
    TEST_ASSERT_EQUAL_UINT(1, references[PIN_FUNC_BUILTIN_LED]);
    TEST_ASSERT_EQUAL(-4, pm->SetPin(pm, PIN_FUNC_RELAY, GPIO_NUM_9));
    config.entries[PIN_FUNC_RELAY].pin = GPIO_NUM_9;
    TEST_ASSERT_EQUAL(-4, pm->InitializeWithConfig(pm, config, &fixture_registry));
    TEST_ASSERT_EQUAL(GPIO_NUM_0, pm->GetPin(pm, PIN_FUNC_RELAY));
}

void test_PinManager_DumpFailurePreservesMappingAndCanRetry(void) {
    IPinManager *manager = PinManager_GetInstance();
    TEST_ASSERT_EQUAL(0, manager->InitializeWithConfig(manager, PinManagerConfig_GetDefault(), NULL));
    gpio_num_t original = manager->GetPin(manager, PIN_FUNC_RELAY);
    mocked_xSemaphoreTake_fake.return_val = pdFALSE;
    TEST_ASSERT_NOT_EQUAL(0, manager->DumpPinMap(manager));
    mocked_xSemaphoreTake_fake.return_val = pdTRUE;
    TEST_ASSERT_EQUAL(0, manager->DumpPinMap(manager));
    TEST_ASSERT_EQUAL(original, manager->GetPin(manager, PIN_FUNC_RELAY));
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_PinManager_DefaultConfig);
    RUN_TEST(test_PinManager_DumpFailurePreservesMappingAndCanRetry);
    RUN_TEST(test_PinManager_Validation_Success);
    RUN_TEST(test_PinManager_Validation_Conflict);
    RUN_TEST(test_PinManager_I2C_Sharing);
    RUN_TEST(test_PinManager_GetSetPin);
    RUN_TEST(test_PinManager_RejectsCrossedI2CAndInvalidEnums);
    RUN_TEST(test_PinManager_LockFailurePreservesMapping);
    RUN_TEST(test_PinManager_ReservationRollbackRetryAndIdempotence);
    return UNITY_END();
}

#endif
