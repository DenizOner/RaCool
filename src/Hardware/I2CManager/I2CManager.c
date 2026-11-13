#include "Hardware/I2CManager/II2CManager.h"
#include "Hardware/I2CManager/I2CManagerConfig.h"
#include "Core/ServiceLocator/ServiceLocator.h"
#include "Core/ServiceLocator/ServiceNames.h"
#include "Hardware/PinManager/IPinManager.h"
#include "Hardware/PinManager/PinDefinitions.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>

static const char *TAG = "I2CManager";
typedef struct {
    _Atomic(ServiceState) state;
    atomic_bool bus_ready;
    i2c_master_bus_handle_t bus0_handle;
    SemaphoreHandle_t mutex;
    StaticSemaphore_t mutex_storage;
    i2c_master_dev_handle_t devices[I2C_MANAGER_MAX_DEVICES];
    const void *bus_owners[I2C_MANAGER_MAX_BUS_OWNERS];
    I2CDiagnostics diagnostics;
    atomic_uint lock_timeouts;
} I2CManagerImpl;
static I2CManagerImpl i2cManagerInstance;
#ifdef RACOOL_I2C_WAIT_TRACE
#include "esp_timer.h"
void RaCool_I2CTraceManagerWait(int64_t start, int64_t elapsed);
#endif

static esp_err_t LockBus(void) {
    if (!i2cManagerInstance.mutex) return ESP_ERR_INVALID_STATE;
#ifdef RACOOL_I2C_WAIT_TRACE
    int64_t start = esp_timer_get_time();
#endif
    if (xSemaphoreTake(i2cManagerInstance.mutex, pdMS_TO_TICKS(I2C_MANAGER_TIMEOUT_MS)) != pdTRUE) {
        atomic_fetch_add(&i2cManagerInstance.lock_timeouts, 1);
#ifdef RACOOL_I2C_WAIT_TRACE
        RaCool_I2CTraceManagerWait(start, esp_timer_get_time() - start);
#endif
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}
static void UnlockBus(void) { xSemaphoreGive(i2cManagerInstance.mutex); }

/* Called only under the bus lock after an admitted SDK/boundary operation. */
static void RecordTransfer(esp_err_t result) {
    i2cManagerInstance.diagnostics.last_transfer_result = result;
    if (result != ESP_OK) {
        ++i2cManagerInstance.diagnostics.transfer_failures;
        i2cManagerInstance.diagnostics.last_transfer_failure = result;
    }
}
static esp_err_t I2CManager_GetDiagnostics(II2CManager *self, I2CDiagnostics *out) {
    if (!self || !out) return ESP_ERR_INVALID_ARG;
    esp_err_t result = LockBus();
    if (result != ESP_OK) return result;
    I2CDiagnostics snapshot = i2cManagerInstance.diagnostics;
    snapshot.bus_ready = atomic_load(&i2cManagerInstance.bus_ready);
    snapshot.lock_timeouts = atomic_load(&i2cManagerInstance.lock_timeouts);
    for (unsigned i = 0; i < I2C_MANAGER_MAX_DEVICES; ++i)
        if (i2cManagerInstance.devices[i]) ++snapshot.owned_devices;
    for (unsigned i = 0; i < I2C_MANAGER_MAX_BUS_OWNERS; ++i)
        if (i2cManagerInstance.bus_owners[i]) ++snapshot.external_owners;
    UnlockBus();
    *out = snapshot;
    return ESP_OK;
}

/* Called with the bus mutex held. NULL locates a free ownership slot. */
static int FindDevice(i2c_master_dev_handle_t handle) {
    for (int i = 0; i < I2C_MANAGER_MAX_DEVICES; ++i)
        if (i2cManagerInstance.devices[i] == handle) return i;
    return -1;
}

static esp_err_t LockActiveBus(void) {
    esp_err_t result = LockBus();
    if (result != ESP_OK) return result;
    if (!i2cManagerInstance.bus0_handle) {
        UnlockBus();
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}

static i2c_master_bus_handle_t I2CManager_GetBusHandle(II2CManager *self, i2c_port_t port) {
    if (!self || port != I2C_NUM_0 || LockBus() != ESP_OK) return NULL;
    i2c_master_bus_handle_t handle = i2cManagerInstance.bus0_handle;
    UnlockBus();
    return handle;
}

static esp_err_t I2CManager_AcquireBus(II2CManager *self, const void *owner,
                                      i2c_master_bus_handle_t *out) {
    if (!self || !owner || !out) return ESP_ERR_INVALID_ARG;
    esp_err_t result = LockActiveBus();
    if (result != ESP_OK) return result;
    int free_slot = -1;
    for (int i = 0; i < I2C_MANAGER_MAX_BUS_OWNERS; ++i) {
        if (i2cManagerInstance.bus_owners[i] == owner) {
            UnlockBus();
            return ESP_ERR_INVALID_STATE;
        }
        if (!i2cManagerInstance.bus_owners[i]) free_slot = i;
    }
    if (free_slot < 0) { UnlockBus(); return ESP_ERR_NO_MEM; }
    i2cManagerInstance.bus_owners[free_slot] = owner;
    *out = i2cManagerInstance.bus0_handle;
    UnlockBus();
    return ESP_OK;
}

static esp_err_t I2CManager_ReleaseBus(II2CManager *self, const void *owner) {
    if (!self || !owner) return ESP_ERR_INVALID_ARG;
    esp_err_t result = LockActiveBus();
    if (result != ESP_OK) return result;
    for (int i = 0; i < I2C_MANAGER_MAX_BUS_OWNERS; ++i) {
        if (i2cManagerInstance.bus_owners[i] == owner) {
            i2cManagerInstance.bus_owners[i] = NULL;
            UnlockBus();
            return ESP_OK;
        }
    }
    UnlockBus();
    return ESP_ERR_INVALID_STATE;
}

static esp_err_t I2CManager_AddDevice(II2CManager *self, const i2c_device_config_t *config,
                                     i2c_master_dev_handle_t *handle) {
    if (!self || !config || !handle) return ESP_ERR_INVALID_ARG;
    esp_err_t result = LockActiveBus();
    if (result != ESP_OK) return result;
    int slot = FindDevice(NULL);
    if (slot < 0) { UnlockBus(); return ESP_ERR_NO_MEM; }
    i2c_master_dev_handle_t created = NULL;
    result = i2c_master_bus_add_device(i2cManagerInstance.bus0_handle, config, &created);
    if (result == ESP_OK && !created) result = ESP_ERR_INVALID_STATE;
    if (result == ESP_OK) {
        i2cManagerInstance.devices[slot] = created;
        *handle = created;
    }
    UnlockBus();
    return result;
}

static esp_err_t I2CManager_ExecuteExternal(II2CManager *self, const void *owner,
                                          esp_err_t (*operation)(void *), void *context) {
    if (!self || !owner || !operation) return ESP_ERR_INVALID_ARG;
    esp_err_t result = LockActiveBus();
    if (result != ESP_OK) return result;
    bool registered = false;
    for (unsigned i = 0; i < I2C_MANAGER_MAX_BUS_OWNERS; ++i)
        if (i2cManagerInstance.bus_owners[i] == owner) { registered = true; break; }
    if (registered) {
        result = operation(context);
        ++i2cManagerInstance.diagnostics.external_operations;
        RecordTransfer(result);
    } else result = ESP_ERR_INVALID_STATE;
    UnlockBus();
    return result;
}

static esp_err_t I2CManager_RemoveDevice(II2CManager *self, i2c_master_dev_handle_t handle) {
    if (!self || !handle) return ESP_ERR_INVALID_ARG;
    esp_err_t result = LockActiveBus();
    if (result != ESP_OK) return result;
    int slot = FindDevice(handle);
    if (slot < 0) { UnlockBus(); return ESP_ERR_INVALID_ARG; }
    result = i2c_master_bus_rm_device(handle);
    if (result == ESP_OK) i2cManagerInstance.devices[slot] = NULL;
    UnlockBus();
    return result;
}

static esp_err_t I2CManager_ReadRegister(II2CManager *self, i2c_master_dev_handle_t handle,
                                        uint8_t reg, uint8_t *data, size_t size) {
    if (!self || !handle || !data || !size) return ESP_ERR_INVALID_ARG;
    esp_err_t result = LockActiveBus();
    if (result != ESP_OK) return result;
    if (FindDevice(handle) < 0) result = ESP_ERR_INVALID_ARG;
    else {
        result = i2c_master_transmit_receive(handle, &reg, 1, data, size, I2C_MANAGER_TIMEOUT_MS);
        ++i2cManagerInstance.diagnostics.reads;
        RecordTransfer(result);
    }
    UnlockBus();
    return result;
}

static esp_err_t I2CManager_WriteRegister(II2CManager *self, i2c_master_dev_handle_t handle,
                                         uint8_t reg, const uint8_t *data, size_t size) {
    if (!self || !handle || (!data && size) || size == SIZE_MAX) return ESP_ERR_INVALID_ARG;
    uint8_t *buffer = malloc(size + 1);
    if (!buffer) return ESP_ERR_NO_MEM;
    buffer[0] = reg;
    if (size) memcpy(buffer + 1, data, size);
    esp_err_t result = LockActiveBus();
    if (result == ESP_OK) {
        if (FindDevice(handle) < 0) result = ESP_ERR_INVALID_ARG;
        else {
            result = i2c_master_transmit(handle, buffer, size + 1, I2C_MANAGER_TIMEOUT_MS);
            ++i2cManagerInstance.diagnostics.writes;
            RecordTransfer(result);
        }
        UnlockBus();
    }
    free(buffer);
    return result;
}

static esp_err_t I2CManager_Scan(II2CManager *self, i2c_port_t port) {
    if (!self || port != I2C_NUM_0) return ESP_ERR_INVALID_ARG;
    esp_err_t result = LockActiveBus();
    if (result != ESP_OK) return result;
    printf("\n--- I2C bus %d: responding addresses ---\n", port);
    for (uint16_t address = 0x03; address <= 0x77; ++address) {
        result = i2c_master_probe(i2cManagerInstance.bus0_handle, address, I2C_MANAGER_TIMEOUT_MS);
        if (result == ESP_OK) printf("0x%02x ", address);
        else if (result != ESP_ERR_NOT_FOUND) break;
    }
    UnlockBus();
    printf("\n");
    return result == ESP_ERR_NOT_FOUND ? ESP_OK : result;
}

/* Service lifecycle calls are serialized by the application service manager.
 * The static mutex survives Stop so waiting API callers never use a freed lock.
 * Device owners must detach their devices before stopping the bus. */
static int32_t I2CManager_Initialize(IService *self, IDIRegistry *registry) {
    (void)registry;
    if (!self) return ESP_ERR_INVALID_ARG;
    if (!i2cManagerInstance.mutex) {
        i2cManagerInstance.mutex = xSemaphoreCreateMutexStatic(&i2cManagerInstance.mutex_storage);
        if (!i2cManagerInstance.mutex) return ESP_ERR_NO_MEM;
    }
    esp_err_t result = LockBus();
    if (result != ESP_OK) return result;
    if (i2cManagerInstance.bus0_handle) { UnlockBus(); return ESP_OK; }
    IPinManager *pins = (IPinManager *)ServiceLocator_GetService(SVC_NAME_PIN_MANAGER);
    if (!pins || !pins->GetPin) { UnlockBus(); return ESP_ERR_INVALID_STATE; }
    gpio_num_t sda = pins->GetPin(pins, PIN_FUNC_I2C0_SDA);
    gpio_num_t scl = pins->GetPin(pins, PIN_FUNC_I2C0_SCL);
    if (!GPIO_IS_VALID_OUTPUT_GPIO(sda) || !GPIO_IS_VALID_OUTPUT_GPIO(scl) || sda == scl) {
        UnlockBus();
        return ESP_ERR_INVALID_ARG;
    }
    const i2c_master_bus_config_t config = {
        .i2c_port = I2C_NUM_0, .sda_io_num = sda, .scl_io_num = scl,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = I2C_MANAGER_GLITCH_IGNORE_COUNT,
        .flags.enable_internal_pullup = true,
    };
    i2c_master_bus_handle_t handle = NULL;
    result = i2c_new_master_bus(&config, &handle);
    if (result == ESP_OK && !handle) result = ESP_ERR_INVALID_STATE;
    if (result == ESP_OK) {
        i2cManagerInstance.bus0_handle = handle;
        i2cManagerInstance.state = SERVICE_STATE_INITIALIZED;
        atomic_store(&i2cManagerInstance.bus_ready, true);
        ESP_LOGI(TAG, "Bus initialized on SDA %d, SCL %d", sda, scl);
    }
    UnlockBus();
    return result;
}

static int32_t I2CManager_Start(IService *self) {
    if (!self) return ESP_ERR_INVALID_ARG;
    esp_err_t result = LockActiveBus();
    if (result != ESP_OK) return result;
    if (i2cManagerInstance.state != SERVICE_STATE_INITIALIZED &&
        i2cManagerInstance.state != SERVICE_STATE_STARTED) result = ESP_ERR_INVALID_STATE;
    else i2cManagerInstance.state = SERVICE_STATE_STARTED;
    UnlockBus();
    return result;
}

static esp_err_t I2CManager_RecoverBus(II2CManager *self) {
    if (self != I2CManager_GetInstance()) return ESP_ERR_INVALID_ARG;
    esp_err_t result = LockBus();
    if (result != ESP_OK) return result;
    if (i2cManagerInstance.state != SERVICE_STATE_STARTED || !i2cManagerInstance.bus0_handle) {
        UnlockBus(); return ESP_ERR_INVALID_STATE;
    }
    for (unsigned i = 0; i < I2C_MANAGER_MAX_BUS_OWNERS; ++i) {
        if (i2cManagerInstance.bus_owners[i]) { UnlockBus(); return ESP_ERR_INVALID_STATE; }
    }
    for (unsigned i = 0; i < I2C_MANAGER_MAX_DEVICES; ++i) {
        if (i2cManagerInstance.devices[i]) { UnlockBus(); return ESP_ERR_INVALID_STATE; }
    }
    result = i2c_master_bus_reset(i2cManagerInstance.bus0_handle);
    RecordTransfer(result);
    UnlockBus();
    return result;
}

static int32_t I2CManager_Stop(IService *self) {
    if (!self) return ESP_ERR_INVALID_ARG;
    esp_err_t result = LockBus();
    if (result != ESP_OK) return result;
    if (i2cManagerInstance.bus0_handle) {
        for (int i = 0; i < I2C_MANAGER_MAX_BUS_OWNERS; ++i) {
            if (i2cManagerInstance.bus_owners[i]) {
                UnlockBus();
                return ESP_ERR_INVALID_STATE;
            }
        }
        for (int i = 0; i < I2C_MANAGER_MAX_DEVICES; ++i) {
            if (i2cManagerInstance.devices[i]) {
                UnlockBus();
                return ESP_ERR_INVALID_STATE;
            }
        }
        result = i2c_del_master_bus(i2cManagerInstance.bus0_handle);
        if (result != ESP_OK) { UnlockBus(); return result; }
        i2cManagerInstance.bus0_handle = NULL;
    }
    atomic_store(&i2cManagerInstance.bus_ready, false);
    i2cManagerInstance.state = SERVICE_STATE_STOPPED;
    UnlockBus();
    return ESP_OK;
}

static ServiceState I2CManager_GetState(IService *self) {
    if (!self) return SERVICE_STATE_ERROR;
    /* A transfer holding the bus does not change the lifecycle state. State
     * publication must not turn ordinary occupancy into a service failure.
     * Lifecycle mutations still acquire the bus mutex before changing state. */
    return atomic_load(&i2cManagerInstance.state);
}
static int32_t I2CManager_CheckHealth(IService *self) {
    if (!self) return ESP_ERR_INVALID_ARG;
    /* Resource availability is independent of ordinary transfer occupancy.
     * This is not a wire/device probe; SDK transfer and sensor checks report
     * actual peripheral failures separately. Never wait on the transfer lock. */
    return atomic_load(&i2cManagerInstance.bus_ready) ? ESP_OK : ESP_ERR_INVALID_STATE;
}

II2CManager *I2CManager_GetInstance(void) {
    static II2CManager service = {
        .base = {.Initialize = I2CManager_Initialize, .Start = I2CManager_Start,
                 .Stop = I2CManager_Stop, .GetState = I2CManager_GetState,
                 .CheckHealth = I2CManager_CheckHealth},
        .GetBusHandle = I2CManager_GetBusHandle, .AddDevice = I2CManager_AddDevice,
        .RemoveDevice = I2CManager_RemoveDevice, .ReadRegister = I2CManager_ReadRegister,
        .WriteRegister = I2CManager_WriteRegister, .Scan = I2CManager_Scan,
        .AcquireBus = I2CManager_AcquireBus, .ReleaseBus = I2CManager_ReleaseBus,
        .ExecuteExternal = I2CManager_ExecuteExternal,
        .GetDiagnostics = I2CManager_GetDiagnostics, .RecoverBus = I2CManager_RecoverBus,
    };
    return &service;
}
