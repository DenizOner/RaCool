#include "i2c_test_fixture.h"
static esp_err_t diagnostic_external_failure(void *context) { (void)context; return ESP_FAIL; }
void test_I2CTransfers_DiagnosticsDistinguishOwnershipSDKFailuresAndLockContention(void) {
    II2CManager *manager=FixtureStart();
    i2c_device_config_t config={.device_address=0x5a}; i2c_master_dev_handle_t handle;
    TEST_ASSERT_EQUAL(ESP_OK,manager->AddDevice(manager,&config,&handle));
    int owner; i2c_master_bus_handle_t bus;
    TEST_ASSERT_EQUAL(ESP_OK,manager->AcquireBus(manager,&owner,&bus));
    uint8_t data=0;
    mocked_i2c_master_transmit_receive_fake.return_val=ESP_ERR_TIMEOUT;
    TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT,manager->ReadRegister(manager,handle,0,&data,1));
    TEST_ASSERT_EQUAL(ESP_OK,manager->WriteRegister(manager,handle,0,&data,1));
    TEST_ASSERT_EQUAL(ESP_FAIL,manager->ExecuteExternal(manager,&owner,diagnostic_external_failure,NULL));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,manager->ReadRegister(manager,(void*)0x999,0,&data,1));
    I2CDiagnostics stats;
    TEST_ASSERT_EQUAL(ESP_OK,manager->GetDiagnostics(manager,&stats));
    TEST_ASSERT_TRUE(stats.bus_ready); TEST_ASSERT_EQUAL(1,stats.owned_devices);
    TEST_ASSERT_EQUAL(1,stats.external_owners); TEST_ASSERT_EQUAL(1,stats.reads);
    TEST_ASSERT_EQUAL(1,stats.writes); TEST_ASSERT_EQUAL(1,stats.external_operations);
    TEST_ASSERT_EQUAL(2,stats.transfer_failures); TEST_ASSERT_EQUAL(ESP_FAIL,stats.last_transfer_failure);
    I2CDiagnostics before=stats; mocked_xSemaphoreTake_fake.return_val=pdFALSE;
    TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT,manager->GetDiagnostics(manager,&stats));
    TEST_ASSERT_EQUAL_MEMORY(&before,&stats,sizeof(stats));
    mocked_xSemaphoreTake_fake.return_val=pdTRUE;
    TEST_ASSERT_EQUAL(ESP_OK,manager->GetDiagnostics(manager,&stats)); TEST_ASSERT_EQUAL(1,stats.lock_timeouts);
    TEST_ASSERT_EQUAL(ESP_OK,manager->RemoveDevice(manager,handle));
    TEST_ASSERT_EQUAL(ESP_OK,manager->ReleaseBus(manager,&owner));
    TEST_ASSERT_EQUAL(ESP_OK,manager->base.Stop(&manager->base));
    TEST_ASSERT_EQUAL(ESP_OK,manager->GetDiagnostics(manager,&stats));
    TEST_ASSERT_FALSE(stats.bus_ready); TEST_ASSERT_EQUAL(0,stats.owned_devices);
    TEST_ASSERT_EQUAL(0,stats.external_owners); TEST_ASSERT_EQUAL(2,stats.transfer_failures);
}
void setUp(void) { FixtureReset(); }
void tearDown(void) {}
static unsigned external_calls;
static esp_err_t external_transfer(void *context) {
    external_calls++;
    TEST_ASSERT_EQUAL(mocked_xSemaphoreGive_fake.call_count + 1, mocked_xSemaphoreTake_fake.call_count);
    return *(esp_err_t *)context;
}
void test_I2CTransfers_ExternalLeaseSharesMutexAndPreservesErrors(void) {
    II2CManager *manager = FixtureStart();
    int owner;
    i2c_master_bus_handle_t bus;
    esp_err_t error = ESP_FAIL;
    external_calls = 0;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, manager->ExecuteExternal(manager, &owner, external_transfer, &error));
    TEST_ASSERT_EQUAL(0, external_calls);
    TEST_ASSERT_EQUAL(ESP_OK, manager->AcquireBus(manager, &owner, &bus));
    TEST_ASSERT_EQUAL(ESP_FAIL, manager->ExecuteExternal(manager, &owner, external_transfer, &error));
    TEST_ASSERT_EQUAL(1, external_calls);
    TEST_ASSERT_EQUAL(mocked_xSemaphoreTake_fake.call_count, mocked_xSemaphoreGive_fake.call_count);
    TEST_ASSERT_EQUAL(ESP_OK, manager->ReleaseBus(manager, &owner));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, manager->ExecuteExternal(manager, &owner, external_transfer, &error));
    TEST_ASSERT_EQUAL(1, external_calls);
}
void test_I2CTransfers_ExternalLockFailureDoesNotInvokeSdk(void) {
    II2CManager *manager = FixtureStart();
    int owner;
    i2c_master_bus_handle_t bus;
    esp_err_t error = ESP_OK;
    external_calls = 0;
    TEST_ASSERT_EQUAL(ESP_OK, manager->AcquireBus(manager, &owner, &bus));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, manager->ExecuteExternal(manager, &owner, NULL, NULL));
    mocked_xSemaphoreTake_fake.return_val = pdFALSE;
    unsigned gives = mocked_xSemaphoreGive_fake.call_count;
    TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT, manager->ExecuteExternal(manager, &owner, external_transfer, &error));
    TEST_ASSERT_EQUAL(0, external_calls);
    TEST_ASSERT_EQUAL(gives, mocked_xSemaphoreGive_fake.call_count);
}
static II2CManager *StartWithDevice(void) {
    II2CManager *manager = FixtureStart();
    i2c_master_dev_handle_t device;
    const i2c_device_config_t config = {.device_address = 0x5a, .scl_speed_hz = 100000};
    TEST_ASSERT_EQUAL(ESP_OK, manager->AddDevice(manager, &config, &device));
    return manager;
}
void test_I2CTransfers_WriteCopiesRegisterAndPayload(void) {
    II2CManager *manager = StartWithDevice();
    const uint8_t payload[] = {0x12, 0x34};
    const uint8_t expected[] = {0x07, 0x12, 0x34};
    TEST_ASSERT_EQUAL(ESP_OK, manager->WriteRegister(manager, (void *)0x300, 0x07, payload, sizeof(payload)));
    TEST_ASSERT_EQUAL(sizeof(expected), transmittedSize);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, transmitted, sizeof(expected));
    TEST_ASSERT_TRUE(transferWasLocked);
    TEST_ASSERT_EQUAL(mocked_xSemaphoreTake_fake.call_count, mocked_xSemaphoreGive_fake.call_count);
}
void test_I2CTransfers_RegisterOnlyWrite(void) {
    II2CManager *manager = StartWithDevice();
    TEST_ASSERT_EQUAL(ESP_OK, manager->WriteRegister(manager, (void *)0x300, 0x10, NULL, 0));
    TEST_ASSERT_EQUAL(1, transmittedSize);
    TEST_ASSERT_EQUAL(0x10, transmitted[0]);
}
void test_I2CTransfers_ReadPreservesSDKErrorAndUnlocks(void) {
    II2CManager *manager = StartWithDevice();
    uint8_t data[2];
    mocked_i2c_master_transmit_receive_fake.return_val = ESP_ERR_TIMEOUT;
    TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT, manager->ReadRegister(manager, (void *)0x300, 7, data, sizeof(data)));
    TEST_ASSERT_EQUAL_PTR(data, mocked_i2c_master_transmit_receive_fake.arg3_val);
    TEST_ASSERT_EQUAL(2, mocked_i2c_master_transmit_receive_fake.arg4_val);
    TEST_ASSERT_EQUAL(mocked_xSemaphoreTake_fake.call_count, mocked_xSemaphoreGive_fake.call_count);
}
void test_I2CTransfers_InvalidBuffersAndOverflow(void) {
    II2CManager *manager = StartWithDevice();
    uint8_t value = 0;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, manager->WriteRegister(manager, (void *)0x300, 0, &value, SIZE_MAX));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, manager->WriteRegister(manager, (void *)0x300, 0, NULL, 1));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, manager->ReadRegister(manager, (void *)0x300, 0, &value, 0));
    TEST_ASSERT_EQUAL(0, mocked_i2c_master_transmit_fake.call_count);
    TEST_ASSERT_EQUAL(0, mocked_i2c_master_transmit_receive_fake.call_count);
}
void test_I2CTransfers_AllocationAndLockFailure(void) {
    II2CManager *manager = StartWithDevice();
    uint8_t value = 1;
    allocationFails = true;
    TEST_ASSERT_EQUAL(ESP_ERR_NO_MEM, manager->WriteRegister(manager, (void *)0x300, 0, &value, 1));
    allocationFails = false;
    mocked_xSemaphoreTake_fake.return_val = pdFALSE;
    unsigned gives = mocked_xSemaphoreGive_fake.call_count;
    TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT, manager->WriteRegister(manager, (void *)0x300, 0, &value, 1));
    TEST_ASSERT_EQUAL(gives, mocked_xSemaphoreGive_fake.call_count);
    TEST_ASSERT_EQUAL(0, mocked_i2c_master_transmit_fake.call_count);
}

void test_I2CTransfers_ForeignAndDetachedDevicesAreRejected(void) {
    II2CManager *manager = StartWithDevice();
    uint8_t value = 0;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, manager->ReadRegister(manager, (void *)0x400, 7, &value, 1));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, manager->WriteRegister(manager, (void *)0x400, 7, &value, 1));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, manager->RemoveDevice(manager, (void *)0x400));
    TEST_ASSERT_EQUAL(0, mocked_i2c_master_bus_rm_device_fake.call_count);
    TEST_ASSERT_EQUAL(ESP_OK, manager->RemoveDevice(manager, (void *)0x300));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, manager->ReadRegister(manager, (void *)0x300, 7, &value, 1));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, manager->WriteRegister(manager, (void *)0x300, 7, &value, 1));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, manager->RemoveDevice(manager, (void *)0x300));
    TEST_ASSERT_EQUAL(0, mocked_i2c_master_transmit_fake.call_count);
    TEST_ASSERT_EQUAL(0, mocked_i2c_master_transmit_receive_fake.call_count);
    TEST_ASSERT_EQUAL(1, mocked_i2c_master_bus_rm_device_fake.call_count);
}
