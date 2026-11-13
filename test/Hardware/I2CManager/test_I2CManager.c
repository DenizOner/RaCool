#include "i2c_test_fixture.h"
void setUp(void) { FixtureReset(); }
void tearDown(void) {}

void test_I2CStateAndSafeStartDoNotFailDuringTransferOccupancy(void) {
    II2CManager *manager = FixtureStart();
    mocked_xSemaphoreTake_fake.return_val = pdFALSE;
    unsigned calls = mocked_xSemaphoreTake_fake.call_count;
    TEST_ASSERT_EQUAL(SERVICE_STATE_STARTED, manager->base.GetState(&manager->base));
    TEST_ASSERT_EQUAL(ESP_OK, IService_SafeStart(&manager->base));
    TEST_ASSERT_EQUAL(ESP_OK, IService_SafeInitialize(&manager->base, NULL));
    TEST_ASSERT_EQUAL(calls, mocked_xSemaphoreTake_fake.call_count);
    TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT, manager->base.Stop(&manager->base));
    TEST_ASSERT_EQUAL(SERVICE_STATE_STARTED, manager->base.GetState(&manager->base));
    mocked_xSemaphoreTake_fake.return_val = pdTRUE;
    TEST_ASSERT_EQUAL(ESP_OK, manager->base.Stop(&manager->base));
    mocked_xSemaphoreTake_fake.return_val = pdFALSE;
    TEST_ASSERT_EQUAL(SERVICE_STATE_STOPPED, manager->base.GetState(&manager->base));
    TEST_ASSERT_EQUAL(SERVICE_STATE_ERROR, manager->base.GetState(NULL));
}

void test_I2CInitializedStateDoesNotBypassStartLockFailure(void) {
    II2CManager *manager = I2CManager_GetInstance();
    TEST_ASSERT_EQUAL(ESP_OK, manager->base.Initialize(&manager->base, NULL));
    mocked_xSemaphoreTake_fake.return_val = pdFALSE;
    TEST_ASSERT_EQUAL(SERVICE_STATE_INITIALIZED, manager->base.GetState(&manager->base));
    TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT, IService_SafeStart(&manager->base));
    TEST_ASSERT_EQUAL(SERVICE_STATE_INITIALIZED, manager->base.GetState(&manager->base));
    mocked_xSemaphoreTake_fake.return_val = pdTRUE;
    TEST_ASSERT_EQUAL(ESP_OK, IService_SafeStart(&manager->base));
}

void test_I2CHealthDoesNotWaitForTransferOccupancyOrBypassTransferFailures(void) {
    II2CManager *manager = FixtureStart();
    const i2c_device_config_t config = {.device_address=0x5a,.scl_speed_hz=100000};
    i2c_master_dev_handle_t device = NULL;
    TEST_ASSERT_EQUAL(ESP_OK, manager->AddDevice(manager, &config, &device));
    mocked_xSemaphoreTake_fake.return_val = pdFALSE;
    unsigned calls = mocked_xSemaphoreTake_fake.call_count;
    TEST_ASSERT_EQUAL(ESP_OK, manager->base.CheckHealth(&manager->base));
    TEST_ASSERT_EQUAL(calls, mocked_xSemaphoreTake_fake.call_count);
    uint8_t value;
    TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT, manager->ReadRegister(manager,device,0,&value,1));
    TEST_ASSERT_EQUAL(0, mocked_i2c_master_transmit_receive_fake.call_count);
    mocked_xSemaphoreTake_fake.return_val = pdTRUE;
    TEST_ASSERT_EQUAL(ESP_OK, manager->RemoveDevice(manager,device));
    TEST_ASSERT_EQUAL(ESP_OK, manager->base.Stop(&manager->base));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, manager->base.CheckHealth(&manager->base));
}

void test_I2CManager_InitialState(void) {
    II2CManager *manager = I2CManager_GetInstance();
    TEST_ASSERT_EQUAL_PTR(manager, I2CManager_GetInstance());
    TEST_ASSERT_EQUAL(SERVICE_STATE_UNINITIALIZED, manager->base.GetState(&manager->base));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, manager->base.Start(&manager->base));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, manager->base.CheckHealth(&manager->base));
}
void test_I2CManager_LifecycleAndRestart(void) {
    II2CManager *manager = FixtureStart();
    TEST_ASSERT_EQUAL(SERVICE_STATE_STARTED, manager->base.GetState(&manager->base));
    TEST_ASSERT_EQUAL(ESP_OK, manager->base.CheckHealth(&manager->base));
    TEST_ASSERT_EQUAL_PTR((void *)0x200, manager->GetBusHandle(manager, I2C_NUM_0));
    TEST_ASSERT_EQUAL(ESP_OK, manager->base.Stop(&manager->base));
    TEST_ASSERT_NULL(manager->GetBusHandle(manager, I2C_NUM_0));
    TEST_ASSERT_EQUAL(SERVICE_STATE_STOPPED, manager->base.GetState(&manager->base));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, manager->base.Start(&manager->base));
    TEST_ASSERT_EQUAL(ESP_OK, IService_SafeInitialize(&manager->base, NULL));
    TEST_ASSERT_EQUAL(ESP_OK, manager->base.Start(&manager->base));
    TEST_ASSERT_EQUAL(2, mocked_i2c_new_master_bus_fake.call_count);
    TEST_ASSERT_EQUAL(1, mocked_xSemaphoreCreateMutexStatic_fake.call_count);
}
void test_I2CManager_DoubleInitializeDoesNotReplaceBus(void) {
    II2CManager *manager = FixtureStart();
    TEST_ASSERT_EQUAL(ESP_OK, manager->base.Initialize(&manager->base, NULL));
    TEST_ASSERT_EQUAL(1, mocked_i2c_new_master_bus_fake.call_count);
    TEST_ASSERT_EQUAL(SERVICE_STATE_STARTED, manager->base.GetState(&manager->base));
}
void test_I2CManager_PinAndClockConfiguration(void) {
    FixtureStart();
    TEST_ASSERT_EQUAL(GPIO_NUM_6, createdBus.sda_io_num);
    TEST_ASSERT_EQUAL(GPIO_NUM_7, createdBus.scl_io_num);
    TEST_ASSERT_EQUAL(I2C_CLK_SRC_DEFAULT, createdBus.clk_source);
    TEST_ASSERT_TRUE(createdBus.flags.enable_internal_pullup);
}
void test_I2CManager_DeviceClockIsForwarded(void) {
    II2CManager *manager = FixtureStart();
    const i2c_device_config_t config = {.device_address=0x5a, .scl_speed_hz=100000};
    i2c_master_dev_handle_t device = NULL;
    TEST_ASSERT_EQUAL(ESP_OK, manager->AddDevice(manager, &config, &device));
    TEST_ASSERT_EQUAL_PTR(&config, mocked_i2c_master_bus_add_device_fake.arg1_val);
    TEST_ASSERT_EQUAL_PTR((void *)0x300, device);
    TEST_ASSERT_EQUAL(ESP_OK, manager->RemoveDevice(manager, device));
    TEST_ASSERT_EQUAL_PTR(device, mocked_i2c_master_bus_rm_device_fake.arg0_val);
}

void test_I2CRecoveryRejectsOwnersAndPropagatesSdkFailureWithoutDeletion(void) {
    II2CManager *manager = FixtureStart();
    i2c_master_bus_handle_t bus;
    int owner;
    TEST_ASSERT_EQUAL(ESP_OK, manager->AcquireBus(manager, &owner, &bus));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, manager->RecoverBus(manager));
    TEST_ASSERT_EQUAL(0, mocked_i2c_master_bus_reset_fake.call_count);
    TEST_ASSERT_EQUAL(ESP_OK, manager->ReleaseBus(manager, &owner));
    i2c_device_config_t config = {.device_address=0x5a, .scl_speed_hz=100000};
    i2c_master_dev_handle_t device;
    TEST_ASSERT_EQUAL(ESP_OK, manager->AddDevice(manager, &config, &device));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, manager->RecoverBus(manager));
    TEST_ASSERT_EQUAL(0, mocked_i2c_master_bus_reset_fake.call_count);
    TEST_ASSERT_EQUAL(ESP_OK, manager->RemoveDevice(manager, device));
    mocked_i2c_master_bus_reset_fake.return_val = ESP_ERR_TIMEOUT;
    TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT, manager->RecoverBus(manager));
    TEST_ASSERT_EQUAL_PTR(bus, manager->GetBusHandle(manager, 0));
    mocked_i2c_master_bus_reset_fake.return_val = ESP_OK;
    TEST_ASSERT_EQUAL(ESP_OK, manager->RecoverBus(manager));
    TEST_ASSERT_EQUAL(2, mocked_i2c_master_bus_reset_fake.call_count);
    TEST_ASSERT_EQUAL(0, mocked_i2c_del_master_bus_fake.call_count);
    mocked_xSemaphoreTake_fake.return_val = pdFALSE;
    TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT, manager->RecoverBus(manager));
    TEST_ASSERT_EQUAL(2, mocked_i2c_master_bus_reset_fake.call_count);
}
