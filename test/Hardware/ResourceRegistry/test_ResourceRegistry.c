#ifdef RACOOL_TEST_NATIVE

#include <unity.h>
#include "Hardware/ResourceRegistry/IResourceRegistry.h"
#include "Hardware/ResourceRegistry/ResourceRegistryConfig.h"
#include "Core/DI/IDIRegistry.h"
#include <stdlib.h>
#include <string.h>

// Mock framework -- header then implementation
#include "../../Infrastructure/support/test_mocks.h"

static unsigned allocation_calls, fail_allocation_at;
static int outstanding_allocations;
static void *registry_test_calloc(size_t count, size_t size) {
    allocation_calls++;
    if (allocation_calls == fail_allocation_at) return NULL;
    void *result = calloc(count, size);
    if (result) outstanding_allocations++;
    return result;
}
static void registry_test_free(void *pointer) {
    if (pointer) outstanding_allocations--;
    free(pointer);
}
#define calloc registry_test_calloc
#define free registry_test_free

#define TAG TAG_resreg
#include "../../../src/Hardware/ResourceRegistry/ResourceRegistry.c"
#undef TAG
#undef calloc
#undef free

#define TAG TAG_resreg_cfg
#include "../../../src/Hardware/ResourceRegistry/ResourceRegistryConfig.c"
#undef TAG

static IResourceRegistry *test_registry;

static void ResetAllMocks(void) {
    RESET_FAKE(mocked_xSemaphoreCreateMutex);
    RESET_FAKE(mocked_xSemaphoreTake);
    RESET_FAKE(mocked_xSemaphoreGive);
    RESET_FAKE(mocked_ServiceLocator_GetService);
    RESET_FAKE(mocked_ServiceLocator_GetEventBus);
    FFF_RESET_HISTORY();
}

void setUp(void) {
    ResetAllMocks();
    mocked_xSemaphoreCreateMutex_fake.return_val = (SemaphoreHandle_t)1;
    mocked_xSemaphoreTake_fake.return_val = pdTRUE;
    mocked_xSemaphoreGive_fake.return_val = pdTRUE;

    test_registry = ResourceRegistry_GetInstance();
    allocation_calls = 0;
    fail_allocation_at = 0;
}

void tearDown(void) {
    if (test_registry) {
        mocked_xSemaphoreTake_fake.return_val = pdTRUE;
        fail_allocation_at = 0;
        for (int type = RESOURCE_TYPE_GPIO; type < RESOURCE_TYPE_MAX; ++type) {
            uint32_t maximum = 0;
            ResourceEntry_t *map = GetResourceEntry((ResourceId_t){(ResourceType_t)type, 0}, &maximum);
            for (uint32_t i = 0; map && i < maximum; ++i) {
                while (map[i].is_reserved) {
                    char owner[MAX_OWNER_LEN];
                    memcpy(owner, map[i].owner, sizeof(owner));
                    TEST_ASSERT_EQUAL(0, test_registry->Release(test_registry, (ResourceId_t){(ResourceType_t)type, i}, owner));
                }
            }
        }
        TEST_ASSERT_EQUAL(0, test_registry->Reset(test_registry));
        test_registry->base.Stop((struct IService *)test_registry);
    }
    ResetAllMocks();
}

void test_ResourceRegistry_Lifecycle(void) {
    ServiceState state = test_registry->base.GetState((struct IService *)test_registry);
    TEST_ASSERT_EQUAL(SERVICE_STATE_UNINITIALIZED, state);

    ResourceRegistryConfig config = ResourceRegistryConfig_GetDefault();
    int32_t res = test_registry->InitializeWithConfig(test_registry, config, NULL);
    TEST_ASSERT_EQUAL(0, res);

    state = test_registry->base.GetState((struct IService *)test_registry);
    TEST_ASSERT_EQUAL(SERVICE_STATE_INITIALIZED, state);

    test_registry->base.Start((struct IService *)test_registry);
    state = test_registry->base.GetState((struct IService *)test_registry);
    TEST_ASSERT_EQUAL(SERVICE_STATE_STARTED, state);
}

void test_ResourceRegistry_ReserveAndReleaseGPIO(void) {
    ResourceRegistryConfig config = ResourceRegistryConfig_GetDefault();
    test_registry->InitializeWithConfig(test_registry, config, NULL);
    test_registry->base.Start((struct IService *)test_registry);

    ResourceId_t res1 = {RESOURCE_TYPE_GPIO, 10};

    // Exclusive reservation
    int32_t result = test_registry->Reserve(test_registry, res1, "Owner1", false);
    TEST_ASSERT_EQUAL(0, result);
    TEST_ASSERT_FALSE(test_registry->IsAvailable(test_registry, res1));

    char owner[16];
    test_registry->GetOwner(test_registry, res1, owner, sizeof(owner));
    TEST_ASSERT_EQUAL_STRING("Owner1", owner);

    // Conflict reservation
    result = test_registry->Reserve(test_registry, res1, "Owner2", false);
    TEST_ASSERT_EQUAL(-3, result);

    // Mismatched owner release
    result = test_registry->Release(test_registry, res1, "Owner2");
    TEST_ASSERT_EQUAL(-3, result);

    // Valid release
    result = test_registry->Release(test_registry, res1, "Owner1");
    TEST_ASSERT_EQUAL(0, result);
    TEST_ASSERT_TRUE(test_registry->IsAvailable(test_registry, res1));
}

void test_ResourceRegistry_SharedI2C(void) {
    ResourceRegistryConfig config = ResourceRegistryConfig_GetDefault();
    test_registry->InitializeWithConfig(test_registry, config, NULL);
    test_registry->base.Start((struct IService *)test_registry);

    ResourceId_t i2c_res = {RESOURCE_TYPE_I2C_BUS, 0};

    // Shared reservation 1
    int32_t result = test_registry->Reserve(test_registry, i2c_res, "Sensor1", true);
    TEST_ASSERT_EQUAL(0, result);
    TEST_ASSERT_TRUE(test_registry->IsAvailable(test_registry, i2c_res));

    // Shared reservation 2
    result = test_registry->Reserve(test_registry, i2c_res, "Sensor2", true);
    TEST_ASSERT_EQUAL(0, result);

    // Exclusive reservation fails
    result = test_registry->Reserve(test_registry, i2c_res, "Sensor3", false);
    TEST_ASSERT_EQUAL(-3, result);

    // First shared release (ref count drops to 1)
    result = test_registry->Release(test_registry, i2c_res, "Sensor1");
    TEST_ASSERT_EQUAL(0, result);
    TEST_ASSERT_TRUE(test_registry->IsAvailable(test_registry, i2c_res));

    // Second shared release
    result = test_registry->Release(test_registry, i2c_res, "Sensor2");
    TEST_ASSERT_EQUAL(0, result);
}

void test_ResourceRegistry_PWM_ADC_And_Bounds(void) {
    ResourceRegistryConfig config = ResourceRegistryConfig_GetDefault();
    test_registry->InitializeWithConfig(test_registry, config, NULL);
    test_registry->base.Start((struct IService *)test_registry);

    // PWM Channel
    ResourceId_t pwm_res = {RESOURCE_TYPE_PWM_CHANNEL, 1};
    TEST_ASSERT_EQUAL(0, test_registry->Reserve(test_registry, pwm_res, "RGB", false));

    // ADC Channel
    ResourceId_t adc_res = {RESOURCE_TYPE_ADC_CHANNEL, 0};
    TEST_ASSERT_EQUAL(0, test_registry->Reserve(test_registry, adc_res, "Battery", false));

    // Out of bounds
    ResourceId_t invalid_res = {RESOURCE_TYPE_GPIO, 999};
    TEST_ASSERT_EQUAL(-1, test_registry->Reserve(test_registry, invalid_res, "Owner", false));
}

void test_ResourceRegistry_StatsAndReset(void) {
    ResourceRegistryConfig config = ResourceRegistryConfig_GetDefault();
    test_registry->InitializeWithConfig(test_registry, config, NULL);
    test_registry->base.Start((struct IService *)test_registry);

    ResourceId_t res = {RESOURCE_TYPE_GPIO, 5};
    test_registry->Reserve(test_registry, res, "Owner1", false);
    test_registry->Reserve(test_registry, res, "Owner2", false); // Conflict

    ResourceRegistryStats_t stats;
    test_registry->GetStats(test_registry, &stats);
    TEST_ASSERT_EQUAL(1, stats.total_reservations);
    TEST_ASSERT_EQUAL(1, stats.total_conflicts);
    TEST_ASSERT_EQUAL(1, stats.active_gpio_count);

    TEST_ASSERT_EQUAL(-4, test_registry->Reset(test_registry));
    TEST_ASSERT_FALSE(test_registry->IsAvailable(test_registry, res));
    TEST_ASSERT_EQUAL(0, test_registry->Release(test_registry, res, "Owner1"));
    TEST_ASSERT_EQUAL(0, test_registry->Reset(test_registry));
    test_registry->GetStats(test_registry, &stats);
    TEST_ASSERT_EQUAL(0, stats.total_reservations);
    TEST_ASSERT_EQUAL(0, stats.active_gpio_count);
}

void test_ResourceRegistry_SharedOwnerIsolation(void) {
    TEST_ASSERT_EQUAL(0, test_registry->InitializeWithConfig(test_registry, ResourceRegistryConfig_GetDefault(), NULL));
    ResourceId_t resource = {RESOURCE_TYPE_I2C_BUS, 0};
    TEST_ASSERT_EQUAL(0, test_registry->Reserve(test_registry, resource, "First", true));
    TEST_ASSERT_EQUAL(0, test_registry->Reserve(test_registry, resource, "First", true));
    TEST_ASSERT_EQUAL(0, test_registry->Reserve(test_registry, resource, "Second", true));
    TEST_ASSERT_EQUAL(-3, test_registry->Release(test_registry, resource, "Foreign"));
    TEST_ASSERT_EQUAL_UINT32(3, res_reg_inst.i2c_map[0].ref_count);
    TEST_ASSERT_EQUAL(0, test_registry->Release(test_registry, resource, "First"));
    TEST_ASSERT_EQUAL(0, test_registry->Release(test_registry, resource, "First"));
    TEST_ASSERT_EQUAL(-3, test_registry->Release(test_registry, resource, "First"));
    char owner[MAX_OWNER_LEN];
    TEST_ASSERT_EQUAL(0, test_registry->GetOwner(test_registry, resource, owner, sizeof(owner)));
    TEST_ASSERT_EQUAL_STRING("Second", owner);
    TEST_ASSERT_EQUAL(0, test_registry->Release(test_registry, resource, "Second"));
    strcpy(owner, "unchanged");
    TEST_ASSERT_EQUAL(-2, test_registry->GetOwner(test_registry, resource, owner, sizeof(owner)));
    TEST_ASSERT_EQUAL_STRING("unchanged", owner);
}

void test_ResourceRegistry_SharedCapacityAndOverflow(void) {
    TEST_ASSERT_EQUAL(0, test_registry->InitializeWithConfig(test_registry, ResourceRegistryConfig_GetDefault(), NULL));
    ResourceId_t resource = {RESOURCE_TYPE_GPIO, 4};
    char owner[MAX_OWNER_LEN];
    for (unsigned i = 0; i < RESOURCE_REGISTRY_MAX_SHARED_OWNERS; ++i) {
        snprintf(owner, sizeof(owner), "owner%u", i);
        TEST_ASSERT_EQUAL(0, test_registry->Reserve(test_registry, resource, owner, true));
    }
    TEST_ASSERT_EQUAL(-4, test_registry->Reserve(test_registry, resource, "extra", true));
    TEST_ASSERT_EQUAL_UINT32(RESOURCE_REGISTRY_MAX_SHARED_OWNERS, res_reg_inst.gpio_map[4].ref_count);
    TEST_ASSERT_EQUAL(0, test_registry->Release(test_registry, resource, "owner0"));
    TEST_ASSERT_EQUAL(0, test_registry->Reserve(test_registry, resource, "extra", true));
    ResourceEntry_t *entry = &res_reg_inst.gpio_map[4];
    uint32_t refs = entry->ref_count;
    entry->ref_count = UINT32_MAX;
    TEST_ASSERT_EQUAL(-4, test_registry->Reserve(test_registry, resource, "extra", true));
    entry->ref_count = refs;
    uint32_t individual = entry->owner_refs[0];
    entry->owner_refs[0] = UINT32_MAX;
    TEST_ASSERT_EQUAL(-4, test_registry->Reserve(test_registry, resource, "extra", true));
    entry->owner_refs[0] = individual;
}

void test_ResourceRegistry_InvalidInputAndCallerOutputs(void) {
    TEST_ASSERT_EQUAL(0, test_registry->InitializeWithConfig(test_registry, ResourceRegistryConfig_GetDefault(), NULL));
    ResourceId_t resource = {RESOURCE_TYPE_GPIO, 3};
    TEST_ASSERT_EQUAL(-1, test_registry->Reserve(test_registry, resource, "abcdefghijklmnop", true));
    TEST_ASSERT_EQUAL(-1, test_registry->Reserve(test_registry, resource, "", true));
    TEST_ASSERT_EQUAL(-1, test_registry->Release(test_registry, resource, NULL));
    TEST_ASSERT_EQUAL(0, test_registry->Reserve(test_registry, resource, "Owner", false));
    char owner[3] = "ok";
    TEST_ASSERT_EQUAL(-1, test_registry->GetOwner(test_registry, resource, owner, sizeof(owner)));
    TEST_ASSERT_EQUAL_STRING("ok", owner);
    mocked_xSemaphoreTake_fake.return_val = pdFALSE;
    ResourceRegistryStats_t stats = {.total_reservations = 123};
    TEST_ASSERT_EQUAL(-4, test_registry->GetStats(test_registry, &stats));
    TEST_ASSERT_EQUAL_UINT32(123, stats.total_reservations);
    TEST_ASSERT_EQUAL(-4, test_registry->Release(test_registry, resource, "Owner"));
    TEST_ASSERT_EQUAL(-4, test_registry->Reset(test_registry));
    TEST_ASSERT_EQUAL(-4, test_registry->GetOwner(test_registry, resource, owner, sizeof(owner)));
    TEST_ASSERT_FALSE(test_registry->IsAvailable(test_registry, resource));
    TEST_ASSERT_FALSE(test_registry->Validate(test_registry));
    TEST_ASSERT_EQUAL(-4, test_registry->DumpStatus(test_registry));
}

void test_ResourceRegistry_ReinitializeCannotEraseOwnership(void) {
    ResourceRegistryConfig original = ResourceRegistryConfig_GetDefault();
    TEST_ASSERT_EQUAL(0, test_registry->InitializeWithConfig(test_registry, original, NULL));
    ResourceId_t resource = {RESOURCE_TYPE_GPIO, 3};
    TEST_ASSERT_EQUAL(0, test_registry->Reserve(test_registry, resource, "Owner", false));
    ResourceEntry_t *map = res_reg_inst.gpio_map;
    original.max_gpio_entries++;
    TEST_ASSERT_EQUAL(-4, test_registry->InitializeWithConfig(test_registry, original, NULL));
    TEST_ASSERT_EQUAL_PTR(map, res_reg_inst.gpio_map);
    TEST_ASSERT_EQUAL(-4, test_registry->Reset(test_registry));
    TEST_ASSERT_EQUAL(0, test_registry->base.Stop(&test_registry->base));
    TEST_ASSERT_EQUAL(-4, test_registry->Reserve(test_registry, resource, "Late", true));
    TEST_ASSERT_EQUAL(0, test_registry->Release(test_registry, resource, "Owner"));
}

void test_ResourceRegistry_AllocationRollbackAndConfigurationLimits(void) {
    ResourceRegistryConfig config = ResourceRegistryConfig_GetDefault();
    TEST_ASSERT_EQUAL(0, test_registry->InitializeWithConfig(test_registry, config, NULL));
    ResourceEntry_t *maps[] = {res_reg_inst.gpio_map, res_reg_inst.i2c_map, res_reg_inst.pwm_map, res_reg_inst.adc_map};
    int outstanding = outstanding_allocations;
    for (unsigned i = 1; i <= 4; ++i) {
        allocation_calls = 0; fail_allocation_at = i;
        TEST_ASSERT_EQUAL(-1, test_registry->InitializeWithConfig(test_registry, config, NULL));
        TEST_ASSERT_EQUAL(outstanding, outstanding_allocations);
        TEST_ASSERT_EQUAL_PTR(maps[0], res_reg_inst.gpio_map);
        TEST_ASSERT_EQUAL_PTR(maps[1], res_reg_inst.i2c_map);
        TEST_ASSERT_EQUAL_PTR(maps[2], res_reg_inst.pwm_map);
        TEST_ASSERT_EQUAL_PTR(maps[3], res_reg_inst.adc_map);
        TEST_ASSERT_EQUAL(SERVICE_STATE_INITIALIZED, test_registry->base.GetState(&test_registry->base));
    }
    fail_allocation_at = 0;
    config.max_gpio_entries = UINT32_MAX;
    TEST_ASSERT_EQUAL(-1, test_registry->InitializeWithConfig(test_registry, config, NULL));
    TEST_ASSERT_EQUAL_PTR(maps[0], res_reg_inst.gpio_map);
    allocation_calls = 0; fail_allocation_at = 1;
    TEST_ASSERT_EQUAL(-1, test_registry->DumpStatus(test_registry));
    TEST_ASSERT_EQUAL(outstanding, outstanding_allocations);
}

void test_ResourceRegistry_ActiveStatsWithoutHistory(void) {
    ResourceRegistryConfig config = ResourceRegistryConfig_GetDefault();
    config.enable_stats = false;
    TEST_ASSERT_EQUAL(0, test_registry->InitializeWithConfig(test_registry, config, NULL));
    ResourceId_t resource = {RESOURCE_TYPE_GPIO, 2};
    TEST_ASSERT_EQUAL(0, test_registry->Reserve(test_registry, resource, "Owner", true));
    ResourceRegistryStats_t stats;
    TEST_ASSERT_EQUAL(0, test_registry->GetStats(test_registry, &stats));
    TEST_ASSERT_EQUAL_UINT32(0, stats.total_reservations);
    TEST_ASSERT_EQUAL_UINT32(1, stats.active_gpio_count);
    TEST_ASSERT_EQUAL(0, test_registry->Release(test_registry, resource, "Owner"));
    TEST_ASSERT_EQUAL(0, test_registry->GetStats(test_registry, &stats));
    TEST_ASSERT_EQUAL_UINT32(0, stats.active_gpio_count);
}

void test_ResourceRegistry_ValidatesOwnershipConsistency(void) {
    TEST_ASSERT_EQUAL(0, test_registry->InitializeWithConfig(test_registry, ResourceRegistryConfig_GetDefault(), NULL));
    ResourceId_t resource = {RESOURCE_TYPE_GPIO, 2};
    TEST_ASSERT_EQUAL(0, test_registry->Reserve(test_registry, resource, "Owner", true));
    TEST_ASSERT_TRUE(test_registry->Validate(test_registry));
    ResourceEntry_t *entry = &res_reg_inst.gpio_map[2];
    entry->ref_count++;
    TEST_ASSERT_FALSE(test_registry->Validate(test_registry));
    entry->ref_count--;
    TEST_ASSERT_TRUE(test_registry->Validate(test_registry));
}

#include "../../Infrastructure/support/console_capture.h"
static int dump_registry_for_capture(int argc,char **argv) {
    (void)argc; (void)argv; return test_registry->DumpStatus(test_registry);
}
void test_ResourceRegistry_DumpTranslatesLabelsAndPreservesOwnership(void) {
    TEST_ASSERT_EQUAL(0,test_registry->InitializeWithConfig(test_registry,ResourceRegistryConfig_GetDefault(),NULL));
    ResourceId_t id={RESOURCE_TYPE_GPIO,1};
    TEST_ASSERT_EQUAL(0,test_registry->Reserve(test_registry,id,"test_owner",false));
    int allocations=outstanding_allocations;
    const LocalizationCatalog *saved=Localization_Snapshot(); char output[2048];
    TEST_ASSERT_TRUE(Localization_Apply("tr"));
    TEST_ASSERT_EQUAL(0,test_capture_console(dump_registry_for_capture,0,NULL,output,sizeof(output)));
    TEST_ASSERT_NOT_NULL(strstr(output,Localization_Text(Localization_Find("tr"),LOC_CLI_RESOURCE_DUMP_HEADER)));
    TEST_ASSERT_NOT_NULL(strstr(output,"test_owner"));
    TEST_ASSERT_TRUE(Localization_Apply("en"));
    TEST_ASSERT_EQUAL(0,test_capture_console(dump_registry_for_capture,0,NULL,output,sizeof(output)));
    TEST_ASSERT_NOT_NULL(strstr(output,"Resource Type"));
    TEST_ASSERT_EQUAL(allocations,outstanding_allocations);
    char owner[16]; TEST_ASSERT_EQUAL(0,test_registry->GetOwner(test_registry,id,owner,sizeof(owner)));
    TEST_ASSERT_EQUAL_STRING("test_owner",owner);
    TEST_ASSERT_TRUE(Localization_Apply(saved->code));
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_ResourceRegistry_Lifecycle);
    RUN_TEST(test_ResourceRegistry_ReserveAndReleaseGPIO);
    RUN_TEST(test_ResourceRegistry_SharedI2C);
    RUN_TEST(test_ResourceRegistry_PWM_ADC_And_Bounds);
    RUN_TEST(test_ResourceRegistry_StatsAndReset);
    RUN_TEST(test_ResourceRegistry_SharedOwnerIsolation);
    RUN_TEST(test_ResourceRegistry_SharedCapacityAndOverflow);
    RUN_TEST(test_ResourceRegistry_InvalidInputAndCallerOutputs);
    RUN_TEST(test_ResourceRegistry_ReinitializeCannotEraseOwnership);
    RUN_TEST(test_ResourceRegistry_AllocationRollbackAndConfigurationLimits);
    RUN_TEST(test_ResourceRegistry_ActiveStatsWithoutHistory);
    RUN_TEST(test_ResourceRegistry_ValidatesOwnershipConsistency);
    RUN_TEST(test_ResourceRegistry_DumpTranslatesLabelsAndPreservesOwnership);
    return UNITY_END();
}

#endif // RACOOL_TEST_NATIVE
