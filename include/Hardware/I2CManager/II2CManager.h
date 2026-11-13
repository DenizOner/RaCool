#ifndef II2C_MANAGER_H
#define II2C_MANAGER_H

#include "Core/IService.h"
#include <driver/i2c_master.h>
#include <esp_err.h>

/* Task-only resource/transfer summary. Counters persist across Stop/Start and
 * wrap modulo 2^32. Scan NACKs and rejected arguments are not transfer failures.
 * External operations include callbacks issued under a valid bus lease. */
typedef struct {
    bool bus_ready;
    uint32_t owned_devices, external_owners;
    uint32_t reads, writes, external_operations, transfer_failures, lock_timeouts;
    esp_err_t last_transfer_result, last_transfer_failure;
} I2CDiagnostics;

/**
 * @brief I2C Manager Interface
 * 
 * Provides a centralized way to manage the I2C master bus and its devices
 * using the modern ESP-IDF driver/i2c_master.h API.
 */
typedef struct II2CManager {
    IService base;

    /**
     * @brief Gets the bus handle for a specific I2C port
     */
    i2c_master_bus_handle_t (*GetBusHandle)(struct II2CManager *self, i2c_port_t port);

    /**
     * @brief Registers an owned device on the bus (bounded by I2C_MANAGER_MAX_DEVICES).
     * The output is unchanged on failure. Stop rejects outstanding owned devices.
     */
    esp_err_t (*AddDevice)(struct II2CManager *self, const i2c_device_config_t *dev_cfg, i2c_master_dev_handle_t *ret_handle);

    /**
     * @brief Removes a device from the bus
     */
    esp_err_t (*RemoveDevice)(struct II2CManager *self, i2c_master_dev_handle_t dev_handle);

    /**
     * @brief Performs a thread-safe read from a registered, still-owned device.
     */
    esp_err_t (*ReadRegister)(struct II2CManager *self, i2c_master_dev_handle_t dev_handle, uint8_t reg_addr, uint8_t *data, size_t len);

    /**
     * @brief Performs a thread-safe write to a registered, still-owned device.
     */
    esp_err_t (*WriteRegister)(struct II2CManager *self, i2c_master_dev_handle_t dev_handle, uint8_t reg_addr, const uint8_t *data, size_t len);

    /**
     * @brief Scans the I2C bus for connected devices
     * @return ESP_OK if scan completed, or error code
     */
    esp_err_t (*Scan)(struct II2CManager *self, i2c_port_t port);

    /** Pins the bus lifetime for an external SDK driver such as esp_lcd.
     * owner must be a stable non-NULL identity; duplicate acquisition is rejected.
     * Release only after all external devices/transfers have been removed.
     * Output is unchanged on failure. GetBusHandle alone does not pin lifetime. */
    esp_err_t (*AcquireBus)(struct II2CManager *self, const void *owner,
                            i2c_master_bus_handle_t *out);
    esp_err_t (*ReleaseBus)(struct II2CManager *self, const void *owner);

    /** Synchronously run an external SDK operation under the same mutex as
     * registered-device transfers. owner must hold an AcquireBus lease.
     * The callback must not call this manager or acquire a caller's outer locks.
     * Its context is borrowed only until return; the mutex is released on every
     * callback result. This does not extend the SDK operation's timeout. */
    esp_err_t (*ExecuteExternal)(struct II2CManager *self, const void *owner,
                                 esp_err_t (*operation)(void *), void *context);
    /* Bounded bus lock; failure preserves output. Never call from a health/ISR
     * callback or from an ExecuteExternal operation holding this bus lock. */
    esp_err_t (*GetDiagnostics)(struct II2CManager *self, I2CDiagnostics *out);
    /** Explicit task-only recovery. Rejects all outstanding owned devices and
     * external leases; callers must stop/detach their owners first. No retry or
     * automatic lifecycle change. SDK errors propagate and ownership remains. */
    esp_err_t (*RecoverBus)(struct II2CManager *self);

} II2CManager;

/**
 * @brief Get the global I2CManager instance
 */
II2CManager *I2CManager_GetInstance(void);

#endif // II2C_MANAGER_H
