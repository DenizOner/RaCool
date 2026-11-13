#include "i2c_test_fixture.h"
void setUp(void) { FixtureReset(); }
void tearDown(void) {}
void test_I2CManager_E2E_DeviceTransferAndDetach(void) {
    II2CManager *manager = FixtureStart();
    i2c_master_dev_handle_t device;
    const i2c_device_config_t config = {.device_address=0x5a, .scl_speed_hz=100000};
    TEST_ASSERT_EQUAL(ESP_OK, manager->AddDevice(manager, &config, &device));
    uint8_t data[2] = {0x10, 0x20};
    TEST_ASSERT_EQUAL(ESP_OK, manager->WriteRegister(manager, device, 7, data, 2));
    TEST_ASSERT_EQUAL(ESP_OK, manager->ReadRegister(manager, device, 7, data, 2));
    TEST_ASSERT_EQUAL(ESP_OK, manager->RemoveDevice(manager, device));
    TEST_ASSERT_EQUAL(ESP_OK, manager->base.Stop(&manager->base));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, manager->ReadRegister(manager, device, 7, data, 2));
    TEST_ASSERT_EQUAL(1, mocked_i2c_master_transmit_receive_fake.call_count);
}
void test_I2CManager_E2E_ScanSkipsReservedAddresses(void) {
    II2CManager *manager = FixtureStart();
    TEST_ASSERT_EQUAL(ESP_OK, manager->Scan(manager, I2C_NUM_0));
    TEST_ASSERT_EQUAL(0x77 - 0x03 + 1, mocked_i2c_master_probe_fake.call_count);
    TEST_ASSERT_EQUAL(0x03, mocked_i2c_master_probe_fake.arg1_history[0]);
    TEST_ASSERT_EQUAL(0x77, mocked_i2c_master_probe_fake.arg1_val);
    TEST_ASSERT_EQUAL(mocked_xSemaphoreTake_fake.call_count, mocked_xSemaphoreGive_fake.call_count);
}
void test_I2CManager_E2E_ScanPropagatesStuckBus(void) {
    II2CManager *manager = FixtureStart();
    mocked_i2c_master_probe_fake.return_val = ESP_ERR_TIMEOUT;
    TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT, manager->Scan(manager, I2C_NUM_0));
    TEST_ASSERT_EQUAL(1, mocked_i2c_master_probe_fake.call_count);
    TEST_ASSERT_EQUAL(mocked_xSemaphoreTake_fake.call_count, mocked_xSemaphoreGive_fake.call_count);
}
