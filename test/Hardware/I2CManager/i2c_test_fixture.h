#ifndef I2C_TEST_FIXTURE_H
#define I2C_TEST_FIXTURE_H
#include "unity.h"
#include "Infrastructure/support/test_mocks.h"
#include <stdlib.h>
#include <string.h>
static bool allocationFails;
static void *fixture_malloc(size_t size) { return allocationFails ? NULL : malloc(size); }
#define malloc fixture_malloc
#include "Hardware/I2CManager/I2CManager.c"
#undef malloc

static IPinManager fixturePins;
static gpio_num_t fixtureSda, fixtureScl;
static i2c_master_bus_config_t createdBus;
static uint8_t transmitted[16];
static size_t transmittedSize;
static bool transferWasLocked;
static gpio_num_t fixture_pin(IPinManager *self, PinFunction_t function) {
    (void)self;
    return function == PIN_FUNC_I2C0_SDA ? fixtureSda : fixtureScl;
}
static esp_err_t fixture_bus(const i2c_master_bus_config_t *config, i2c_master_bus_handle_t *handle) {
    createdBus = *config;
    *handle = (void *)0x200;
    return ESP_OK;
}
static esp_err_t fixture_device(i2c_master_bus_handle_t bus, const i2c_device_config_t *config,
                                i2c_master_dev_handle_t *handle) {
    (void)bus; (void)config;
    *handle = (void *)0x300;
    return ESP_OK;
}
static esp_err_t fixture_write(i2c_master_dev_handle_t handle, const uint8_t *data, size_t size, int timeout) {
    (void)handle;
    TEST_ASSERT_EQUAL(I2C_MANAGER_TIMEOUT_MS, timeout);
    TEST_ASSERT_LESS_OR_EQUAL(sizeof(transmitted), size);
    transmittedSize = size;
    memcpy(transmitted, data, size);
    transferWasLocked = mocked_xSemaphoreTake_fake.call_count == mocked_xSemaphoreGive_fake.call_count + 1;
    return ESP_OK;
}
static void FixtureReset(void) {
    RESET_FAKE(mocked_i2c_master_bus_reset);
    memset(&i2cManagerInstance, 0, sizeof(i2cManagerInstance));
    allocationFails = false;
    transmittedSize = 0;
    transferWasLocked = false;
    fixtureSda = GPIO_NUM_6;
    fixtureScl = GPIO_NUM_7;
    fixturePins.GetPin = fixture_pin;
    RESET_FAKE(mocked_xSemaphoreCreateMutexStatic);
    RESET_FAKE(mocked_xSemaphoreTake);
    RESET_FAKE(mocked_xSemaphoreGive);
    RESET_FAKE(mocked_i2c_new_master_bus);
    RESET_FAKE(mocked_i2c_del_master_bus);
    RESET_FAKE(mocked_i2c_master_bus_add_device);
    RESET_FAKE(mocked_i2c_master_bus_rm_device);
    RESET_FAKE(mocked_i2c_master_transmit);
    RESET_FAKE(mocked_i2c_master_transmit_receive);
    RESET_FAKE(mocked_i2c_master_probe);
    RESET_FAKE(mocked_ServiceLocator_GetService);
    mocked_xSemaphoreCreateMutexStatic_fake.return_val = (void *)0x100;
    mocked_xSemaphoreTake_fake.return_val = pdTRUE;
    mocked_xSemaphoreGive_fake.return_val = pdTRUE;
    mocked_ServiceLocator_GetService_fake.return_val = (IService *)&fixturePins;
    mocked_i2c_new_master_bus_fake.custom_fake = fixture_bus;
    mocked_i2c_master_bus_add_device_fake.custom_fake = fixture_device;
    mocked_i2c_master_transmit_fake.custom_fake = fixture_write;
    mocked_i2c_master_probe_fake.return_val = ESP_ERR_NOT_FOUND;
}
static II2CManager *FixtureStart(void) {
    II2CManager *manager = I2CManager_GetInstance();
    TEST_ASSERT_EQUAL(ESP_OK, manager->base.Initialize(&manager->base, NULL));
    TEST_ASSERT_EQUAL(ESP_OK, manager->base.Start(&manager->base));
    return manager;
}
#endif
