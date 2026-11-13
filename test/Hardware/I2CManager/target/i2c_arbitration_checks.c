#include "unity.h"
#include <stdatomic.h>
#include "Hardware/InfraredTemperatureSensor/InfraredTemperatureSensorConfig.h"
#include "Hardware/OLED/OLEDConfig.h"
#include "driver/i2c_master.h"
#include "Management/ConfigurationManager/SystemConfigs.h"

static atomic_uint sdk_reads;
static esp_err_t arbitration_read(i2c_master_dev_handle_t device, const uint8_t *tx,
    size_t tx_size, uint8_t *rx, size_t rx_size, int timeout) {
    (void)device; (void)tx; (void)tx_size; (void)rx; (void)rx_size; (void)timeout;
    atomic_fetch_add(&sdk_reads, 1);
    return ESP_OK;
}
#define i2c_master_transmit_receive arbitration_read
#define ServiceLocator_GetService Arbitration_GetService
#define I2CManager_GetInstance Arbitration_GetManager
#include "../../../../src/Hardware/I2CManager/I2CManager.c"
#undef I2CManager_GetInstance
#undef ServiceLocator_GetService
#undef i2c_master_transmit_receive
IService *Arbitration_GetService(const char *name) { (void)name; return NULL; }

static SemaphoreHandle_t writer_entered, writer_done, reader_done;
static int external_owner;
static atomic_int read_result, write_result;
static atomic_bool lifecycle_observation_failed;
static esp_err_t occupied_bus(void *context) {
    (void)context;
    II2CManager *manager = Arbitration_GetManager();
    /* This callback owns the real transfer mutex. A lifecycle observation or
     * already-started admission must neither wait on it nor report ERROR. */
    if (manager->base.GetState(&manager->base) != SERVICE_STATE_STARTED ||
        IService_SafeStart(&manager->base) != ESP_OK)
        atomic_store(&lifecycle_observation_failed, true);
    xSemaphoreGive(writer_entered);
    /* A bounded transfer; several consecutive transfers exceed the read budget. */
    vTaskDelay(pdMS_TO_TICKS(80));
    return ESP_OK;
}
static void competing_writer(void *context) {
    II2CManager *manager = context;
    esp_err_t result = ESP_OK;
    for (unsigned i = 0; i < 4 && result == ESP_OK; ++i)
        result = manager->ExecuteExternal(manager, &external_owner, occupied_bus, NULL);
    atomic_store(&write_result, result);
    xSemaphoreGive(writer_done);
    vTaskDelete(NULL);
}
static void waiting_reader(void *context) {
    II2CManager *manager = context;
    uint8_t data[3];
    atomic_store(&read_result, manager->ReadRegister(manager, (void *)0x300, 7, data, sizeof(data)));
    xSemaphoreGive(reader_done);
    vTaskDelete(NULL);
}

void target_i2c_verify_arbitration(void) {
    /* Real production manager and FreeRTOS mutex; only the SDK read is injected.
     * No physical bus or GPIO is installed by this test. */
    i2cManagerInstance.mutex = xSemaphoreCreateMutexStatic(&i2cManagerInstance.mutex_storage);
    i2cManagerInstance.bus0_handle = (void *)0x200;
    i2cManagerInstance.devices[0] = (void *)0x300;
    atomic_store(&i2cManagerInstance.state, SERVICE_STATE_STARTED);
    atomic_store(&lifecycle_observation_failed, false);
    II2CManager *manager = Arbitration_GetManager();
    i2c_master_bus_handle_t bus;
    TEST_ASSERT_EQUAL(ESP_OK, manager->AcquireBus(manager, &external_owner, &bus));
    writer_entered = xSemaphoreCreateBinary();
    writer_done = xSemaphoreCreateBinary();
    reader_done = xSemaphoreCreateBinary();
    TEST_ASSERT_NOT_NULL(writer_entered);
    TEST_ASSERT_NOT_NULL(writer_done);
    TEST_ASSERT_NOT_NULL(reader_done);
    esp_err_t observed[2];
    unsigned reads[2];
    for (unsigned phase = 0; phase < 2; ++phase) {
        atomic_store(&sdk_reads, 0);
        (void)xSemaphoreTake(writer_entered, 0);
        TEST_ASSERT_EQUAL(pdPASS, xTaskCreate(competing_writer, "bus_writer", 4096, manager,
                                             OLED_TASK_PRIORITY, NULL));
        TEST_ASSERT_EQUAL(pdTRUE, xSemaphoreTake(writer_entered, pdMS_TO_TICKS(1000)));
        UBaseType_t priority = phase == 0 ? OLED_TASK_PRIORITY - 1 : IRTEMP_WORKER_PRIORITY;
        TEST_ASSERT_EQUAL(pdPASS, xTaskCreate(waiting_reader, "bus_reader", 4096, manager, priority, NULL));
        /* Always join both workers before asserting their results. */
        TEST_ASSERT_EQUAL(pdTRUE, xSemaphoreTake(reader_done, pdMS_TO_TICKS(2000)));
        TEST_ASSERT_EQUAL(pdTRUE, xSemaphoreTake(writer_done, pdMS_TO_TICKS(2000)));
        observed[phase] = atomic_load(&read_result);
        reads[phase] = atomic_load(&sdk_reads);
        TEST_ASSERT_EQUAL(ESP_OK, atomic_load(&write_result));
    }
    TEST_ASSERT_EQUAL(ESP_OK, manager->ReleaseBus(manager, &external_owner));
    vSemaphoreDelete(writer_entered);
    vSemaphoreDelete(writer_done);
    vSemaphoreDelete(reader_done);
    vSemaphoreDelete(i2cManagerInstance.mutex);
    memset(&i2cManagerInstance, 0, sizeof(i2cManagerInstance));
    TEST_ASSERT_FALSE(atomic_load(&lifecycle_observation_failed));
    TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT, observed[0]);
    TEST_ASSERT_EQUAL(0, reads[0]);
    TEST_ASSERT_EQUAL(ESP_OK, observed[1]);
    TEST_ASSERT_EQUAL(1, reads[1]);
}

void target_i2c_verify_sdk_recovery(void) {
    /* Install a real, otherwise unowned SDK bus on the configured OLED/IR pins.
     * This verifies SDK recovery, not recovery from a deliberately stuck wire. */
    i2c_master_bus_config_t config = {
        .i2c_port=I2C_NUM_0, .sda_io_num=OLED_SDA_PIN, .scl_io_num=OLED_SCL_PIN,
        .clk_source=I2C_CLK_SRC_DEFAULT, .glitch_ignore_cnt=I2C_MANAGER_GLITCH_IGNORE_COUNT,
        .flags.enable_internal_pullup=true};
    i2c_master_bus_handle_t bus = NULL;
    TEST_ASSERT_EQUAL(ESP_OK, i2c_new_master_bus(&config, &bus));
    i2cManagerInstance.mutex = xSemaphoreCreateMutexStatic(&i2cManagerInstance.mutex_storage);
    if (!i2cManagerInstance.mutex) {
        esp_err_t cleanup = i2c_del_master_bus(bus);
        TEST_ASSERT_EQUAL(ESP_OK, cleanup);
        TEST_FAIL_MESSAGE("Recovery test mutex allocation failed");
    }
    i2cManagerInstance.bus0_handle = bus;
    atomic_store(&i2cManagerInstance.bus_ready, true);
    atomic_store(&i2cManagerInstance.state, SERVICE_STATE_STARTED);
    II2CManager *manager = Arbitration_GetManager();
    esp_err_t recovery = manager->RecoverBus(manager);
    /* Exercise the public detach/recover/reattach contract with a real SDK
     * device and the external-driver lease used by OLED. No sensor/panel
     * transfer or deliberately stuck physical wire is claimed here. */
    i2c_device_config_t device_config = {
        .dev_addr_length=I2C_ADDR_BIT_LEN_7, .device_address=0x5a,
        .scl_speed_hz=100000};
    i2c_master_dev_handle_t device = NULL;
    i2c_master_bus_handle_t leased_bus = NULL;
    int owner;
    bool leased = false;
    esp_err_t lifecycle[12];
    for (unsigned i=0;i<12;++i) lifecycle[i]=ESP_ERR_NOT_FINISHED;
    lifecycle[0]=manager->AddDevice(manager,&device_config,&device);
    if (lifecycle[0]==ESP_OK) {
        lifecycle[1]=manager->AcquireBus(manager,&owner,&leased_bus);
        leased=lifecycle[1]==ESP_OK;
        if (leased) {
            lifecycle[2]=manager->RecoverBus(manager);
            lifecycle[3]=manager->RemoveDevice(manager,device);
            if (lifecycle[3]==ESP_OK) {
                device=NULL;
                lifecycle[4]=manager->RecoverBus(manager);
                lifecycle[5]=manager->ReleaseBus(manager,&owner);
                if (lifecycle[5]==ESP_OK) {
                    leased=false;
                    lifecycle[6]=manager->RecoverBus(manager);
                    if (lifecycle[6]==ESP_OK) lifecycle[7]=manager->AddDevice(manager,&device_config,&device);
                    if (lifecycle[7]==ESP_OK) {
                        lifecycle[8]=manager->AcquireBus(manager,&owner,&leased_bus);
                        leased=lifecycle[8]==ESP_OK;
                        if (leased) lifecycle[9]=manager->RecoverBus(manager);
                    }
                }
            }
        }
    }
    if (device) {
        lifecycle[10]=manager->RemoveDevice(manager,device);
        if (lifecycle[10]==ESP_OK) device=NULL;
    }
    if (leased) {
        lifecycle[11]=manager->ReleaseBus(manager,&owner);
        if (lifecycle[11]==ESP_OK) leased=false;
    }
    bool ready = manager->base.CheckHealth(&manager->base) == ESP_OK;
    esp_err_t cleanup = i2c_del_master_bus(bus);
    if (cleanup == ESP_OK) {
        vSemaphoreDelete(i2cManagerInstance.mutex);
        memset(&i2cManagerInstance, 0, sizeof(i2cManagerInstance));
    }
    TEST_ASSERT_EQUAL(ESP_OK, cleanup);
    TEST_ASSERT_EQUAL(ESP_OK, recovery);
    const esp_err_t expected[12]={ESP_OK,ESP_OK,ESP_ERR_INVALID_STATE,ESP_OK,
        ESP_ERR_INVALID_STATE,ESP_OK,ESP_OK,ESP_OK,ESP_OK,ESP_ERR_INVALID_STATE,
        ESP_OK,ESP_OK};
    for (unsigned i=0;i<12;++i) TEST_ASSERT_EQUAL(expected[i],lifecycle[i]);
    TEST_ASSERT_EQUAL_PTR(bus,leased_bus);
    TEST_ASSERT_TRUE(ready);
}
