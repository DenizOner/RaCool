#include "i2c_test_fixture.h"
void setUp(void) { FixtureReset(); }
void tearDown(void) {}
void test_I2CLifecycleErrors_MissingPins(void) {
    II2CManager *manager = I2CManager_GetInstance();
    mocked_ServiceLocator_GetService_fake.return_val = NULL;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, manager->base.Initialize(&manager->base, NULL));
    TEST_ASSERT_EQUAL(0, mocked_i2c_new_master_bus_fake.call_count);
}
void test_I2CLifecycleErrors_InvalidPinAssignment(void) {
    II2CManager *manager = I2CManager_GetInstance();
    fixtureSda = GPIO_NUM_NC;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, manager->base.Initialize(&manager->base, NULL));
    fixtureSda = fixtureScl;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, manager->base.Initialize(&manager->base, NULL));
    TEST_ASSERT_EQUAL(0, mocked_i2c_new_master_bus_fake.call_count);
}
void test_I2CLifecycleErrors_BusCreationFailureIsNotSuccess(void) {
    II2CManager *manager = I2CManager_GetInstance();
    mocked_i2c_new_master_bus_fake.custom_fake = NULL;
    mocked_i2c_new_master_bus_fake.return_val = ESP_ERR_INVALID_STATE;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, manager->base.Initialize(&manager->base, NULL));
    TEST_ASSERT_EQUAL(SERVICE_STATE_UNINITIALIZED, manager->base.GetState(&manager->base));
    TEST_ASSERT_NULL(manager->GetBusHandle(manager, I2C_NUM_0));
}
void test_I2CLifecycleErrors_MutexInitializationFailure(void) {
    II2CManager *manager = I2CManager_GetInstance();
    mocked_xSemaphoreCreateMutexStatic_fake.return_val = NULL;
    TEST_ASSERT_EQUAL(ESP_ERR_NO_MEM, manager->base.Initialize(&manager->base, NULL));
    TEST_ASSERT_EQUAL(0, mocked_i2c_new_master_bus_fake.call_count);
}
void test_I2CLifecycleErrors_StopFailureKeepsBusAlive(void) {
    II2CManager *manager = FixtureStart();
    mocked_i2c_del_master_bus_fake.return_val = ESP_ERR_INVALID_STATE;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, manager->base.Stop(&manager->base));
    TEST_ASSERT_EQUAL(ESP_OK, manager->base.CheckHealth(&manager->base));
    TEST_ASSERT_EQUAL(SERVICE_STATE_STARTED, manager->base.GetState(&manager->base));
    TEST_ASSERT_EQUAL_PTR((void *)0x200, manager->GetBusHandle(manager, I2C_NUM_0));
    mocked_i2c_del_master_bus_fake.return_val = ESP_OK;
    TEST_ASSERT_EQUAL(ESP_OK, manager->base.Stop(&manager->base));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, manager->base.CheckHealth(&manager->base));
}

void test_I2CLifecycleErrors_DeviceOwnershipPreventsBusDeletion(void) {
    II2CManager *manager = FixtureStart();
    const i2c_device_config_t config = {.device_address = 0x5a, .scl_speed_hz = 100000};
    i2c_master_dev_handle_t device;
    TEST_ASSERT_EQUAL(ESP_OK, manager->AddDevice(manager, &config, &device));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, manager->base.Stop(&manager->base));
    TEST_ASSERT_EQUAL(0, mocked_i2c_del_master_bus_fake.call_count);
    TEST_ASSERT_EQUAL(SERVICE_STATE_STARTED, manager->base.GetState(&manager->base));
    mocked_i2c_master_bus_rm_device_fake.return_val = ESP_ERR_TIMEOUT;
    TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT, manager->RemoveDevice(manager, device));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, manager->base.Stop(&manager->base));
    TEST_ASSERT_EQUAL(0, mocked_i2c_del_master_bus_fake.call_count);
    mocked_i2c_master_bus_rm_device_fake.return_val = ESP_OK;
    TEST_ASSERT_EQUAL(ESP_OK, manager->RemoveDevice(manager, device));
    TEST_ASSERT_EQUAL(ESP_OK, manager->base.Stop(&manager->base));
    TEST_ASSERT_EQUAL(1, mocked_i2c_del_master_bus_fake.call_count);
}

void test_I2CLifecycleErrors_AddFailurePreservesCallerAndOwnership(void) {
    II2CManager *manager = FixtureStart();
    const i2c_device_config_t config = {.device_address = 0x5a, .scl_speed_hz = 100000};
    i2c_master_dev_handle_t device = (void *)0xABC;
    mocked_i2c_master_bus_add_device_fake.custom_fake = NULL;
    mocked_i2c_master_bus_add_device_fake.return_val = ESP_ERR_NO_MEM;
    TEST_ASSERT_EQUAL(ESP_ERR_NO_MEM, manager->AddDevice(manager, &config, &device));
    TEST_ASSERT_EQUAL_PTR((void *)0xABC, device);
    mocked_i2c_master_bus_add_device_fake.return_val = ESP_OK;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, manager->AddDevice(manager, &config, &device));
    TEST_ASSERT_EQUAL_PTR((void *)0xABC, device);
    TEST_ASSERT_EQUAL(ESP_OK, manager->base.Stop(&manager->base));
}

static uintptr_t next_device;
static esp_err_t CreateUniqueDevice(i2c_master_bus_handle_t bus, const i2c_device_config_t *config,
                                    i2c_master_dev_handle_t *out) {
    (void)bus; (void)config;
    *out = (i2c_master_dev_handle_t)next_device++;
    return ESP_OK;
}
void test_I2CLifecycleErrors_DeviceCapacityAndReleasedSlot(void) {
    II2CManager *manager = FixtureStart();
    next_device = 0x300;
    mocked_i2c_master_bus_add_device_fake.custom_fake = CreateUniqueDevice;
    const i2c_device_config_t config = {.device_address = 0x5a, .scl_speed_hz = 100000};
    i2c_master_dev_handle_t device = NULL;
    for (unsigned i = 0; i < I2C_MANAGER_MAX_DEVICES; ++i)
        TEST_ASSERT_EQUAL(ESP_OK, manager->AddDevice(manager, &config, &device));
    i2c_master_dev_handle_t last = device;
    TEST_ASSERT_EQUAL(ESP_ERR_NO_MEM, manager->AddDevice(manager, &config, &device));
    TEST_ASSERT_EQUAL_PTR(last, device);
    TEST_ASSERT_EQUAL(I2C_MANAGER_MAX_DEVICES, mocked_i2c_master_bus_add_device_fake.call_count);
    TEST_ASSERT_EQUAL(ESP_OK, manager->RemoveDevice(manager, last));
    TEST_ASSERT_EQUAL(ESP_OK, manager->AddDevice(manager, &config, &device));
    TEST_ASSERT_NOT_EQUAL(last, device);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, manager->base.Stop(&manager->base));
    TEST_ASSERT_EQUAL(0, mocked_i2c_del_master_bus_fake.call_count);
}

void test_I2CLifecycleErrors_ExternalBusOwnersPreventDeletion(void) {
    II2CManager *manager = FixtureStart();
    int owner_a, owner_b;
    i2c_master_bus_handle_t bus = NULL;
    TEST_ASSERT_EQUAL(ESP_OK, manager->AcquireBus(manager, &owner_a, &bus));
    TEST_ASSERT_EQUAL_PTR((void *)0x200, bus);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, manager->AcquireBus(manager, &owner_a, &bus));
    TEST_ASSERT_EQUAL(ESP_OK, manager->AcquireBus(manager, &owner_b, &bus));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, manager->base.Stop(&manager->base));
    TEST_ASSERT_EQUAL(0, mocked_i2c_del_master_bus_fake.call_count);
    TEST_ASSERT_EQUAL(ESP_OK, manager->ReleaseBus(manager, &owner_a));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, manager->ReleaseBus(manager, &owner_a));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, manager->base.Stop(&manager->base));
    mocked_xSemaphoreTake_fake.return_val = pdFALSE;
    TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT, manager->ReleaseBus(manager, &owner_b));
    mocked_xSemaphoreTake_fake.return_val = pdTRUE;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, manager->base.Stop(&manager->base));
    TEST_ASSERT_EQUAL(ESP_OK, manager->ReleaseBus(manager, &owner_b));
    TEST_ASSERT_EQUAL(ESP_OK, manager->base.Stop(&manager->base));
    TEST_ASSERT_EQUAL(1, mocked_i2c_del_master_bus_fake.call_count);
}

void test_I2CLifecycleErrors_BusOwnerCapacityAndArguments(void) {
    II2CManager *manager = FixtureStart();
    int owners[I2C_MANAGER_MAX_BUS_OWNERS + 1];
    i2c_master_bus_handle_t bus = (void *)0xABC;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, manager->AcquireBus(manager, NULL, &bus));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, manager->AcquireBus(manager, owners, NULL));
    TEST_ASSERT_EQUAL_PTR((void *)0xABC, bus);
    for (unsigned i = 0; i < I2C_MANAGER_MAX_BUS_OWNERS; ++i)
        TEST_ASSERT_EQUAL(ESP_OK, manager->AcquireBus(manager, &owners[i], &bus));
    bus = (void *)0xABC;
    TEST_ASSERT_EQUAL(ESP_ERR_NO_MEM, manager->AcquireBus(manager, &owners[I2C_MANAGER_MAX_BUS_OWNERS], &bus));
    TEST_ASSERT_EQUAL_PTR((void *)0xABC, bus);
    for (unsigned i = 0; i < I2C_MANAGER_MAX_BUS_OWNERS; ++i)
        TEST_ASSERT_EQUAL(ESP_OK, manager->ReleaseBus(manager, &owners[i]));
    TEST_ASSERT_EQUAL(ESP_OK, manager->base.Stop(&manager->base));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, manager->AcquireBus(manager, owners, &bus));
    TEST_ASSERT_EQUAL_PTR((void *)0xABC, bus);
}
