/**
 * @file IConfigurationManager.h
 * @brief Public interface for the ConfigurationManager service.
 *
 * ConfigurationManager is the single authority for all persistent system
 * settings.  It owns the in-memory SystemSettings object and serialises it
 * to NVS (Non-Volatile Storage) on request.  All other modules must interact
 * with settings exclusively through this interface to guarantee thread safety
 * and event propagation.
 *
 * Lifecycle:
 *   GetInstance() → Initialize() → Start() → [use] → Stop()
 *
 * On Start(), the service subscribes to:
 *   - EVENT_CONFIG_SAVE_REQ  → triggers SaveAsync() for the named block.
 *   - EVENT_CONFIG_RESTART_REQ → sets the restart_required flag.
 *
 * Stop retains ownership until subscriptions can be removed. An outstanding
 * asynchronous operation must finish before Stop succeeds; a timeout preserves
 * worker/queue ownership for retry. Admitted requests are drained before stopping.
 *
 * Thread safety:
 *   Snapshot/field/status operations serialize access under the settings mutex.
 *   Legacy GetSettings returns borrowed live storage and is NOT a concurrent
 *   read snapshot; it is restricted to externally serialized compatibility use.
 *   Production readers use GetSnapshot/GetStatus and writers use UpdateFields.
 *
 * @author RaCool Team
 * @date   2026
 */

#ifndef ICONFIGURATION_MANAGER_H
#define ICONFIGURATION_MANAGER_H

#include "Management/ConfigurationManager/SystemConfigs.h"
#include "Core/IService.h"
#include <stdint.h>
#include <esp_err.h>

/** Forward declaration for the concrete interface struct. */
typedef struct IConfigurationManager IConfigurationManager;

/* Persisted configuration blocks; no credentials are stored in this status. */
#define CONFIG_STATUS_BLOCK_CAPACITY 7
#define CONFIG_STATUS_NAME_CAPACITY 32

typedef struct {
    char name[CONFIG_STATUS_NAME_CAPACITY];
    uint32_t version;
    bool dirty;
} ConfigurationBlockStatus;
typedef struct {
    bool restart_required;
    bool notification_pending;
    size_t block_count;
    ConfigurationBlockStatus blocks[CONFIG_STATUS_BLOCK_CAPACITY];
} ConfigurationStatus;

/** A synchronous field copy relative to one configuration block. */
typedef struct {
    size_t offset;
    const void *data;
    size_t size;
} ConfigurationFieldUpdate;


/**
 * @brief ConfigurationManager public interface (vtable).
 *
 * Embeds IService as its first member so that a pointer to
 * IConfigurationManager can be safely cast to IService* for lifecycle
 * management via the ServiceLocator.
 */
struct IConfigurationManager {
    IService base; /**< IService base – must remain the first member. */

    /**
     * @brief Synchronously saves one or all configuration blocks to NVS.
     *
     * The call blocks until all target blocks have been written and committed.
     * This is safe to call from any task context; internal mutex is acquired
     * for the duration of the write.
     *
     * @param self       Pointer to the IConfigurationManager instance.
     * @param block_name Name of the block to save ("system", "control",
     *                   "hardware", "calibration", "thermal",
     *                   "zigbee_reporting"), or NULL to save all blocks.
     * @return ESP_OK on success, ESP_FAIL if any block write failed.
     */
    esp_err_t (*Save)(IConfigurationManager *self, const char *block_name);

    /**
     * @brief Asynchronously saves one or all configuration blocks to NVS.
     *
     * Enqueues a copied block identifier into one bounded worker queue. The
     * caller may release its block-name storage when the function returns.
     * Queue acceptance is not persistence completion; worker errors affect health.
     * No task or heap allocation occurs per request. Closing rejects new requests.
     *
     * @param self Pointer to the ConfigurationManager instance.
     * @param block_name Registered block name, or NULL for all blocks.
     * @return ESP_OK if queued, ESP_ERR_TIMEOUT if admission/queue is busy,
     *         ESP_ERR_INVALID_STATE when stopped/closing, ESP_ERR_INVALID_ARG
     *         for an unknown block or foreign instance.
     */
    esp_err_t (*SaveAsync)(IConfigurationManager *self, const char *block_name);

    /**
     * @brief Loads one or all configuration blocks from NVS.
     *
     * If a block is not found in NVS (first boot), the in-memory defaults
     * set by GetInstance() are preserved.  A sanity-check and version
     * migration are performed after loading.
     *
     * @param self       Pointer to the IConfigurationManager instance.
     * @param block_name Block name to load, or NULL to load all blocks.
     * @return ESP_OK on success, ESP_FAIL if any read failed.
     */
    esp_err_t (*Load)(IConfigurationManager *self, const char *block_name);

    /**
     * @brief Returns a pointer to the live SystemSettings object.
     *
     * @warning The returned pointer is valid for the lifetime of the service.
     *          It is not protected during caller access. Do not use it in concurrent
     *          production consumers. Use GetSnapshot and UpdateFields instead.
     *
     * @param self Pointer to the IConfigurationManager instance.
     * @return Non-NULL pointer to the current SystemSettings.
     */
    /* Legacy borrowed view: no lock remains held after return. */
    SystemSettings *(*GetSettings)(IConfigurationManager *self);

    /**
     * @brief Atomically replaces the in-memory settings and publishes an event.
     *
     * Validates a candidate under the internal mutex, then copies it into the managed
     * struct, releases the mutex, then publishes EVENT_CONFIG_CHANGED on the
     * EventBus so that all subscribers (UIController, ClimateController, etc.)
     * can react to the change.
     *
     * @param self         Pointer to the IConfigurationManager instance.
     * @param new_settings Pointer to the replacement SystemSettings data.
     * @param size         Must equal sizeof(SystemSettings); enforced at runtime.
     * @return ESP_OK on success, ESP_ERR_INVALID_SIZE on size mismatch,
     *         ESP_ERR_INVALID_ARG for invalid values, ESP_ERR_INVALID_STATE on lock failure.
     */
    esp_err_t (*UpdateSettings)(IConfigurationManager *self,
                                void *new_settings, size_t size);

    /**
     * @brief Reports whether a system restart is required for pending changes.
     *
     * @param self Pointer to the IConfigurationManager instance.
     * @return true if a restart is pending, false otherwise.
     */
    bool (*IsRestartRequired)(IConfigurationManager *self);

    /**
     * @brief Validates the current in-memory settings against defined bounds.
     *
     * Checks each field for physically plausible values (e.g. relay_open_temp
     * must be greater than relay_close_temp, calibration offsets within
     * [CALIBRATION_MIN, CALIBRATION_MAX], etc.).
     *
     * @param self Pointer to the IConfigurationManager instance.
     * @return ESP_OK if all values are valid,
     *         ESP_ERR_INVALID_ARG if any value is out of range.
     */
    esp_err_t (*ValidateSettings)(IConfigurationManager *self);

    /**
     * @brief Resets one or all configuration blocks to compile-time defaults.
     *
     * Each block is staged and committed before replacing its live value. Failed
     * blocks retain their values/dirty flags. Multi-block reset is not an atomic
     * NVS transaction: successful blocks remain applied if another block fails.
     * Any applied changes publish EVENT_CONFIG_CHANGED, including partial success.
     *
     * @param self       Pointer to the IConfigurationManager instance.
     * @param block_name Block to reset, or NULL to reset all blocks.
     * @return ESP_OK on success, ESP_FAIL if NVS write failed.
     */
    esp_err_t (*ResetToDefaults)(IConfigurationManager *self,
                                 const char *block_name);

    /**
     * @brief Returns the NVS schema version of a specific configuration block.
     *
     * Useful for diagnostics and migration verification.
     *
     * @param self       Pointer to the IConfigurationManager instance.
     * @param block_name Name of the block ("system", "control", etc.).
     * @return Schema version number, or 0 if block_name is unrecognised.
     */
    uint32_t (*GetVersion)(IConfigurationManager *self, const char *block_name);

    /**
     * @brief Reports whether any configuration block has unsaved in-memory changes.
     *
     * A block becomes dirty after UpdateSettings() or ResetToDefaults(), and
     * becomes clean after a successful Save() for that block.
     *
     * @param self       Pointer to the IConfigurationManager instance.
     * @param block_name Named block to query ("system", "control", etc.),
     *                   or NULL to query any block.
     * @return true if the specified block (or any block when NULL) has
     *         unsaved changes, false otherwise.
     */
    bool (*IsDirty)(IConfigurationManager *self, const char *block_name);

    /* Bounded metadata snapshot. Failure leaves output unchanged. Prefer this
     * to legacy bool getters, which cannot distinguish false from read failure. */
    esp_err_t (*GetStatus)(IConfigurationManager *self, ConfigurationStatus *output);

    /** Copies a consistent owned representation under the manager mutex, including
     * stable padding bytes. Byte comparison is suitable for change invalidation,
     * not semantic equality. Failure leaves output unchanged. */
    esp_err_t (*GetSnapshot)(IConfigurationManager *self, SystemSettings *output);

    /** Build the owner's actual defaults without mutating live or stored settings. */
    esp_err_t (*GetDefaults)(IConfigurationManager *self, SystemSettings *output);

    /** Read-only verification of all persisted and live default fields and schema
     * versions. Reads independent NVS scratch storage, without migration or Load.
     * Padding is excluded; failure leaves output unchanged. */
    esp_err_t (*VerifyDefaults)(IConfigurationManager *self, SystemSettings *output);
    /** Narrow full-reset guard, owned by the calling action worker. Other tasks
     * cannot change settings until it releases the guard. No mutex is retained
     * across owner calls. Release by a foreign task is rejected. */
    esp_err_t (*SetFactoryResetGuard)(IConfigurationManager *self, bool active);
    bool (*IsMutationAllowed)(IConfigurationManager *self);

    /**
     * Applies non-overlapping fields to a copy of the latest named block.
     * Offsets are relative to that block; schema versions cannot be overwritten.
     * The caller owns all input buffers until this synchronous call returns.
     * Validation and optional NVS commit precede the live-state update. Errors
     * preserve live settings and dirty flags. Unrelated fields are retained.
     * This API serializes cooperative callers; legacy direct writes must migrate.
     */
    esp_err_t (*UpdateFields)(IConfigurationManager *self, const char *block_name,
                             const ConfigurationFieldUpdate *fields, size_t count,
                             bool persist);
    /** Compare expected field bytes and update under the same owner lock.
     * expected_values contains count non-null values matching fields[i].size.
     * A conflict returns ESP_ERR_INVALID_STATE without mutation or notification. */
    esp_err_t (*UpdateFieldsIfUnchanged)(IConfigurationManager *self, const char *block_name,
                              const ConfigurationFieldUpdate *fields, const void *const *expected_values,
                              size_t count, bool persist);

};

/* =========================================================================
 * Type-safe helper (non-vtable convenience wrapper)
 * ========================================================================= */

/**
 * @brief Type-safe wrapper around UpdateSettings().
 *
 * Equivalent to calling UpdateSettings() with @p sizeof(SystemSettings),
 * but enforces the correct pointer type at compile time.
 *
 * @param self         Pointer to the IConfigurationManager instance.
 * @param new_settings Pointer to the SystemSettings to copy in.
 * @return ESP_OK on success, or the error code from UpdateSettings().
 */
static inline esp_err_t
UpdateSettingsTyped(IConfigurationManager *self,
                    const SystemSettings  *new_settings) {
    if (!self || !self->UpdateSettings || !new_settings) return ESP_ERR_INVALID_ARG;
    /* Cast away const for the void* UpdateSettings signature; the
     * implementation copies the data so the source is not modified. */
    return self->UpdateSettings(self, (void *)new_settings,
                                sizeof(SystemSettings));
}

/**
 * @brief Returns the singleton ConfigurationManager instance.
 *
 * On first call the vtable is wired, default values are applied to all
 * settings, and Load() is called to overlay any previously persisted values
 * from NVS.  Subsequent calls return the same pointer.
 *
 * This function is thread-safe with respect to subsequent calls; the FreeRTOS
 * scheduler must be running before this is called (mutex creation requires it).
 *
 * @return Non-NULL pointer to the IConfigurationManager singleton.
 */
IConfigurationManager *ConfigurationManager_GetInstance(void);

#endif /* ICONFIGURATION_MANAGER_H */
