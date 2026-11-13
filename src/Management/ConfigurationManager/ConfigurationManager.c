/**
 * @file ConfigurationManager.c
 * @brief Concrete implementation of the IConfigurationManager interface.
 *
 * Responsibilities:
 *   - Owns the single in-memory SystemSettings instance.
 *   - Serialises/deserialises settings to/from NVS (Non-Volatile Storage).
 *   - Enforces thread safety via a FreeRTOS mutex on every public method.
 *   - Publishes EVENT_CONFIG_CHANGED on the EventBus after every mutation.
 *   - Subscribes to EVENT_CONFIG_SAVE_REQ (triggers async save) and
 *     EVENT_CONFIG_RESTART_REQ (sets restart_required flag).
 *   - Tracks per-block dirty flags to reflect unsaved in-memory changes.
 *   - Validates settings bounds and supports factory-reset per block.
 *   - Provides asynchronous saves through one bounded worker queue.
 *   - Handles NVS schema migration automatically on first boot after upgrade.
 *
 * Design notes:
 *   - Singleton factory initialization precedes concurrent consumers.
 *   - Block descriptor table (k_blocks[]) eliminates repeated block-name
 *     literals and macro tricks across Save/Load/Reset/GetVersion/IsDirty.
 *   - SaveAsync queues block identifiers; one worker drains admitted requests
 *     before cooperative Stop acknowledgment.
 *
 * @author RaCool Team
 * @date   2026
 */

#include "Management/ConfigurationManager/IConfigurationManager.h"
#include "Management/ConfigurationManager/SystemConfigs.h"
#include "Management/ConfigurationManager/ConfigurationManagerConfig.h"
#include "Communication/DebugConsole/Core/DeveloperMode.h"
#include "Management/Watchdog/WatchdogWorker.h"
#include "Hardware/BuiltinLED/BuiltinLEDConfig.h"
#include "Hardware/LEDAnimations/LEDAnimationCatalog.h"
#include "Core/EventBus/IEventBus.h"
#include "Core/Localization/Localization.h"
#include "Core/ServiceLocator/ServiceLocator.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

/* nvs_flash.h is only available on the target (ESP-IDF).
 * For native unit tests we only need the nvs.h mock. */
#ifndef RACOOL_TEST_NATIVE
#include <nvs_flash.h>
#endif
#include <nvs.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>



/* =========================================================================
 * Self-undef guard
 * When this translation unit is compiled together with test_mocks.h (native
 * tests), the mock header redefines ConfigurationManager_GetInstance to
 * mocked_ConfigurationManager_GetInstance.  That would rename our function
 * *definition* here and cause a duplicate-symbol clash with the FFF fake in
 * test_mocks.c.  We undef only this macro so the real singleton keeps its
 * canonical name.  GetEventBus stays redirected to its mock – the IEventBus.h
 * declaration is already resolved to mocked_GetEventBus at that point.
 * ========================================================================= */
#ifdef ConfigurationManager_GetInstance
#undef ConfigurationManager_GetInstance
#endif

/* =========================================================================
 * Module-private constants
 * ========================================================================= */


/** @brief ESP-IDF log tag. */
#define TAG "ConfigMgr"

/* =========================================================================
 * Private state
 * ========================================================================= */

/**
 * @brief Concrete ConfigurationManager instance layout.
 *
 * The @p base field is the public vtable and MUST remain the first member
 * so that (IConfigurationManager*) ↔ (ConfigurationManagerImpl*) casts are
 * well-defined.
 */
typedef struct {
    IConfigurationManager base;           /**< Public vtable – first member. */
    _Atomic(ServiceState) state;           /**< Current service lifecycle state. */
    SystemSettings        settings;        /**< Live in-memory settings. */
    SemaphoreHandle_t     mutex;           /**< Guards all settings accesses. */
    SemaphoreHandle_t lifecycleMutex;
    IEventBus *subscriptionBus;
    unsigned subscriptions;
    WatchdogWorker worker;
    QueueHandle_t requests;
    _Atomic(esp_err_t) asyncError;
    atomic_bool notificationPending;
    bool                  dirty[CONFIG_STATUS_BLOCK_CAPACITY]; /**< Per-block dirty flags. */
} ConfigurationManagerImpl;

/** @brief Module-level singleton. */
static ConfigurationManagerImpl s_mgr;
static _Atomic(TaskHandle_t) s_factoryResetOwner;

static bool config_mutation_allowed(IConfigurationManager *self) {
    if (self != &s_mgr.base) return false;
    TaskHandle_t owner = atomic_load(&s_factoryResetOwner);
    return !owner || owner == xTaskGetCurrentTaskHandle();
}

static esp_err_t config_set_factory_reset_guard(IConfigurationManager *self, bool active) {
    TaskHandle_t caller = xTaskGetCurrentTaskHandle();
    if (self != &s_mgr.base || !caller) return ESP_ERR_INVALID_ARG;
    if (!s_mgr.lifecycleMutex || xSemaphoreTake(s_mgr.lifecycleMutex,
        pdMS_TO_TICKS(CONFIG_CONTROL_TIMEOUT_MS)) != pdTRUE) return ESP_ERR_TIMEOUT;
    if (active && s_mgr.state != SERVICE_STATE_STARTED) {
        xSemaphoreGive(s_mgr.lifecycleMutex); return ESP_ERR_INVALID_STATE;
    }
    if (!s_mgr.mutex || xSemaphoreTake(s_mgr.mutex,
        pdMS_TO_TICKS(CONFIG_CONTROL_TIMEOUT_MS)) != pdTRUE) {
        xSemaphoreGive(s_mgr.lifecycleMutex); return ESP_ERR_TIMEOUT;
    }
    TaskHandle_t owner = atomic_load(&s_factoryResetOwner);
    esp_err_t result = ESP_OK;
    if (owner && owner != caller) result = ESP_ERR_NOT_FINISHED;
    else atomic_store(&s_factoryResetOwner, active ? caller : NULL);
    xSemaphoreGive(s_mgr.mutex);
    xSemaphoreGive(s_mgr.lifecycleMutex);
    return result;
}
/* Publication never holds the settings or lifecycle mutex. A competing update
 * retains pending work rather than waiting for another publisher's callback. */
static atomic_flag s_notificationPublishing = ATOMIC_FLAG_INIT;

/* Startup factory initialization must precede concurrent consumers. */
static volatile bool s_initialized = false;

/* =========================================================================
 * Block Descriptor Table (Single source of truth – eliminates all
 * repeated block-name string literals in Save/Load/Reset/GetVersion).
 * ========================================================================= */

/**
 * @brief Describes one NVS-backed configuration block.
 *
 * The offset and size fields allow generic NVS read/write helpers to operate
 * on any block without a hard-coded switch or macro expansion.
 */
typedef struct {
    const char *name;          /**< Human-readable block name used in API calls. */
    const char *ns;            /**< NVS namespace string (max 15 chars). */
    size_t      offset;        /**< Byte offset of block within SystemSettings. */
    size_t      size;          /**< Size of the block struct in bytes. */
    void      (*apply_defaults)(SystemSettings *s); /**< Restores compile-time defaults. */
} ConfigBlockDef;

/* Forward declarations for default-apply helpers (defined below). */
static void apply_defaults_system(SystemSettings *s);
static void apply_defaults_zigbee(SystemSettings *s);
static void apply_defaults_control(SystemSettings *s);
static void apply_defaults_hardware(SystemSettings *s);
static void apply_defaults_calibration(SystemSettings *s);
static void apply_defaults_thermal(SystemSettings *s);
static void apply_defaults_localization(SystemSettings *s);

/**
 * @brief Array of all persisted configuration blocks.
 *
 * Order is significant: dirty[] and GetVersion() use the same index.
 * Do NOT change the order without updating CONFIG_BLOCK_COUNT users.
 */
static const ConfigBlockDef k_blocks[] = {
    {
        "system",
        NS_CONFIG_SYSTEM,
        offsetof(SystemSettings, system),
        sizeof(SystemBaseConfigs),
        apply_defaults_system
    },
    {
        "control",
        NS_CONFIG_CONTROL,
        offsetof(SystemSettings, control),
        sizeof(ControlConfigs),
        apply_defaults_control
    },
    {
        "hardware",
        NS_CONFIG_HARDWARE,
        offsetof(SystemSettings, hardware),
        sizeof(HardwareConfigs),
        apply_defaults_hardware
    },
    {
        "calibration",
        NS_CONFIG_CALIB,
        offsetof(SystemSettings, calibration),
        sizeof(CalibrationConfigs),
        apply_defaults_calibration
    },
    {
        "thermal",
        NS_CONFIG_THERMAL,
        offsetof(SystemSettings, thermal),
        sizeof(ThermalConfigs),
        apply_defaults_thermal
    },
    {
        "zigbee_reporting",
        NS_CONFIG_ZIGBEE,
        offsetof(SystemSettings, zigbee_reporting),
        sizeof(ZigbeeReportingConfigs),
        apply_defaults_zigbee
    },
    {
        "localization",
        NS_CONFIG_LOCALIZATION,
        offsetof(SystemSettings, localization),
        sizeof(LocalizationConfigs),
        apply_defaults_localization
    },
};

/** @brief Total number of NVS-backed configuration blocks. */
#define CONFIG_BLOCK_COUNT  (sizeof(k_blocks) / sizeof(k_blocks[0]))

/* =========================================================================
 * Forward declarations (functions used before their definition)
 * ========================================================================= */

/** Forward declaration — defined after async_save_task below. */
static esp_err_t config_save_async(IConfigurationManager *self,
                                    const char *block_name);

/** Forward declaration — defined in the IsDirty section below. */
static bool config_is_dirty(IConfigurationManager *self,
                             const char *block_name);

/* =========================================================================
 * NVS helpers (internal, unlocked – caller must hold mutex or ensure safety)
 * ========================================================================= */

/**
 * @brief Writes a binary blob to the given NVS namespace.
 *
 * @param ns   NVS namespace string (max 15 chars).
 * @param key  NVS key string.
 * @param data Pointer to the data to write.
 * @param size Number of bytes to write.
 * @return ESP_OK on success, or an NVS error code.
 */
static esp_err_t nvs_save_block(const char *ns, const char *key,
                                const void *data, size_t size) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(ns, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open failed for ns='%s': 0x%x", ns, (unsigned)err);
        return err;
    }
    err = nvs_set_blob(handle, key, data, size);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

/**
 * @brief Reads a binary blob from the given NVS namespace.
 *
 * @param ns   NVS namespace string.
 * @param key  NVS key string.
 * @param data Pointer to the destination buffer.
 * @param size Expected size in bytes (enforced via length check).
 * @return ESP_OK on success, ESP_ERR_NVS_NOT_FOUND if the key is absent,
 *         ESP_ERR_NVS_INVALID_LENGTH on size mismatch.
 */
static esp_err_t nvs_load_block(const char *ns, const char *key,
                                void *data, size_t size, bool *migrated) {
    if (migrated) *migrated = false;
    _Alignas(SystemSettings) uint8_t candidate[sizeof(SystemSettings)];
    if (!data || size > sizeof(candidate)) return ESP_ERR_INVALID_SIZE;
    /* SDK errors and short reads must never partially overwrite live storage. */
    memcpy(candidate, data, size);
    nvs_handle_t handle;
    esp_err_t err = nvs_open(ns, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        /* NVS_NOT_FOUND is expected on first boot – not an error. */
        if (err != ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGW(TAG, "nvs_open failed for ns='%s': 0x%x", ns, (unsigned)err);
        }
        return err;
    }
    size_t loaded = size;
    err = nvs_get_blob(handle, key, candidate, &loaded);
    nvs_close(handle);

    if (err == ESP_OK && strcmp(ns, NS_CONFIG_HARDWARE) == 0 &&
        size == sizeof(HardwareConfigs) && loaded == 64u) {
        const size_t old_tail = offsetof(HardwareConfigs, ext_led_color_danger_x);
        memmove(candidate + offsetof(HardwareConfigs, ext_led_mode),
                candidate + old_tail, loaded - old_tail);
        HardwareConfigs normalized;
        memcpy(&normalized, candidate, sizeof(normalized));
        normalized.ext_led_color_danger_x = DEFAULT_EXT_LED_COLOR_DANGER_X;
        normalized.ext_led_color_danger_y = DEFAULT_EXT_LED_COLOR_DANGER_Y;
        normalized.ext_led_color_freeze_x = DEFAULT_EXT_LED_COLOR_FREEZE_X;
        normalized.ext_led_color_freeze_y = DEFAULT_EXT_LED_COLOR_FREEZE_Y;
        memcpy(candidate, &normalized, sizeof(normalized));
        loaded = 72u;
    }
    if (err == ESP_OK && strcmp(ns, NS_CONFIG_HARDWARE) == 0 &&
        size == sizeof(HardwareConfigs) && (loaded == 72u || loaded == size)) {
        HardwareConfigs normalized;
        memcpy(&normalized, candidate, sizeof(normalized));
        if (loaded == 72u) memcpy(&normalized.version, candidate + 68u, sizeof(normalized.version));
        HardwareConfigs *hardware = &normalized;
        if (hardware->version < 7) {
            hardware->encoder_steps_per_detent = DEFAULT_ENCODER_STEPS_PER_DETENT;
            hardware->encoder_accel_enabled = DEFAULT_ENCODER_ACCEL_ENABLED;
            hardware->encoder_accel_threshold_ms = DEFAULT_ENCODER_ACCEL_THRESHOLD_MS;
            hardware->encoder_invert_direction = DEFAULT_ENCODER_INVERT_DIRECTION;
            memcpy(candidate, &normalized, sizeof(normalized));
            loaded = size;
        }
    }
    if (err == ESP_OK && strcmp(ns, NS_CONFIG_CONTROL) == 0 && size == sizeof(ControlConfigs)) {
        if (loaded == 24u) {
            uint32_t legacy_version;
            memcpy(&legacy_version, candidate + 20u, sizeof(legacy_version));
            if (legacy_version != 1u) return ESP_ERR_INVALID_ARG;
            ControlConfigs normalized = {0};
            memcpy(&normalized, candidate, 18u);
            normalized.fan_strategy = DEFAULT_FAN_STRATEGY;
            normalized.object_temperature_source = DEFAULT_OBJECT_TEMPERATURE_SOURCE;
            normalized.ambient_temperature_source = DEFAULT_AMBIENT_TEMPERATURE_SOURCE;
            normalized.fan_delta_on_decic = DEFAULT_FAN_DELTA_ON_DECIC;
            normalized.fan_delta_off_decic = DEFAULT_FAN_DELTA_OFF_DECIC;
            normalized.version = 2;
            memcpy(candidate, &normalized, sizeof(normalized));
            loaded = size;
            if (migrated) *migrated = true;
        } else if (loaded == size) {
            ControlConfigs normalized;
            memcpy(&normalized, candidate, sizeof(normalized));
            if (normalized.version != 2u) return ESP_ERR_INVALID_ARG;
        }
    }
    if (err == ESP_OK && loaded != size) {
        ESP_LOGW(TAG, "Size mismatch for ns='%s': expected=%u got=%u",
                 ns, (unsigned)size, (unsigned)loaded);
        return ESP_ERR_NVS_INVALID_LENGTH;
    }
    if (err == ESP_OK) memcpy(data, candidate, size);
    return err;
}

/* =========================================================================
 * Default value application (internal)
 * ========================================================================= */

/**
 * @brief Applies compile-time defaults to the system sub-block.
 *
 * @param s Pointer to the SystemSettings to populate.
 */
static void apply_defaults_system(SystemSettings *s) {
    s->system.system_enabled        = DEFAULT_SYSTEM_ENABLED;
    s->system.temp_unit             = DEFAULT_TEMP_UNIT;
    s->system.developer_mode_enabled = DEFAULT_DEVELOPER_MODE_ENABLED;
    s->system.telemetry_interval_ms = DEFAULT_TELEMETRY_INTERVAL_MS;
    s->system.oled_timeout_sec      = DEFAULT_OLED_TIMEOUT_SEC;
    s->system.display_mode          = DEFAULT_DISPLAY_MODE;
    s->system.power_profile         = DEFAULT_POWER_PROFILE;
    s->system.zigbee_tx_profile      = DEFAULT_ZIGBEE_TX_PROFILE;
    s->system.menu_auto_exit_enabled = DEFAULT_MENU_AUTO_EXIT_ENABLED;
    s->system.version               = SYSTEM_CONFIG_SCHEMA_VERSION;
}

/**
 * @brief Applies compile-time defaults to the Zigbee reporting sub-block.
 *
 * @param s Pointer to the SystemSettings to populate.
 */
static void apply_defaults_zigbee(SystemSettings *s) {
    s->zigbee_reporting.fan_s     = DEFAULT_ZB_REPORT_FAN_S;
    s->zigbee_reporting.ambient_s = DEFAULT_ZB_REPORT_AMBIENT_S;
    s->zigbee_reporting.cpu_s     = DEFAULT_ZB_REPORT_CPU_S;
    s->zigbee_reporting.health_s  = DEFAULT_ZB_REPORT_HEALTH_S;
    s->zigbee_reporting.memory_s  = DEFAULT_ZB_REPORT_MEMORY_S;
    s->zigbee_reporting.uptime_s  = DEFAULT_ZB_REPORT_UPTIME_S;
    s->zigbee_reporting.version   = 1;
}

/**
 * @brief Applies compile-time defaults to the control sub-block.
 *
 * @param s Pointer to the SystemSettings to populate.
 */
static void apply_defaults_control(SystemSettings *s) {
    s->control.threshold_high   = DEFAULT_THRESHOLD_HIGH;
    s->control.threshold_low    = DEFAULT_THRESHOLD_LOW;
    s->control.relay_open_temp  = DEFAULT_RELAY_OPEN_TEMP;
    s->control.relay_close_temp = DEFAULT_RELAY_CLOSE_TEMP;
    s->control.fan_manual_mode  = DEFAULT_FAN_MANUAL_MODE;
    s->control.fan_manual_state = DEFAULT_FAN_MANUAL_STATE;
    s->control.fan_strategy = DEFAULT_FAN_STRATEGY;
    s->control.object_temperature_source = DEFAULT_OBJECT_TEMPERATURE_SOURCE;
    s->control.ambient_temperature_source = DEFAULT_AMBIENT_TEMPERATURE_SOURCE;
    s->control.reserved = 0;
    s->control.reserved_tail = 0;
    s->control.fan_delta_on_decic = DEFAULT_FAN_DELTA_ON_DECIC;
    s->control.fan_delta_off_decic = DEFAULT_FAN_DELTA_OFF_DECIC;
    s->control.version = 2;
}

/**
 * @brief Applies compile-time defaults to the hardware sub-block.
 *
 * @param s Pointer to the SystemSettings to populate.
 */
static void apply_defaults_hardware(SystemSettings *s) {
    s->hardware.builtin_led_brightness = DEFAULT_BUILTIN_LED_BRIGHTNESS;
    s->hardware.builtin_led_state           = DEFAULT_BUILTIN_LED_STATE;
    s->hardware.builtin_led_anim_mode       = 1; /* Automatic mode */
    s->hardware.builtin_led_startup_anim     = DEFAULT_BUILTIN_LED_STARTUP_ANIM;
    s->hardware.builtin_led_idle_anim       = DOUBLE_BLINK_WHITE;
    s->hardware.builtin_led_connecting_anim = HEARTBEAT_RED;
    s->hardware.builtin_led_fault_anim      = POLICE_STROBE;
    s->hardware.rgb_red_state               = DEFAULT_RGB_RED_STATE;
    s->hardware.rgb_green_state             = DEFAULT_RGB_GREEN_STATE;
    s->hardware.rgb_blue_state              = DEFAULT_RGB_BLUE_STATE;
    s->hardware.ext_led_brightness          = DEFAULT_EXT_LED_BRIGHTNESS;
    s->hardware.ext_led_mode                = DEFAULT_EXT_LED_MODE;
    s->hardware.ext_led_anim                = DEFAULT_EXT_LED_ANIM;
    s->hardware.ext_led_temp_cold_min       = DEFAULT_EXT_LED_TEMP_COLD_MIN;
    s->hardware.ext_led_temp_cold_max       = DEFAULT_EXT_LED_TEMP_COLD_MAX;
    s->hardware.ext_led_temp_warm_min       = DEFAULT_EXT_LED_TEMP_WARM_MIN;
    s->hardware.ext_led_temp_warm_max       = DEFAULT_EXT_LED_TEMP_WARM_MAX;
    s->hardware.ext_led_temp_hot_min        = DEFAULT_EXT_LED_TEMP_HOT_MIN;
    s->hardware.ext_led_temp_hot_max        = DEFAULT_EXT_LED_TEMP_HOT_MAX;
    s->hardware.ext_led_temp_danger         = DEFAULT_EXT_LED_TEMP_DANGER;
    s->hardware.ext_led_temp_freeze         = DEFAULT_EXT_LED_TEMP_FREEZE;
    s->hardware.ext_led_color_cold_x        = DEFAULT_EXT_LED_COLOR_COLD_X;
    s->hardware.ext_led_color_cold_y        = DEFAULT_EXT_LED_COLOR_COLD_Y;
    s->hardware.ext_led_color_warm_x        = DEFAULT_EXT_LED_COLOR_WARM_X;
    s->hardware.ext_led_color_warm_y        = DEFAULT_EXT_LED_COLOR_WARM_Y;
    s->hardware.ext_led_color_hot_x         = DEFAULT_EXT_LED_COLOR_HOT_X;
    s->hardware.ext_led_color_hot_y         = DEFAULT_EXT_LED_COLOR_HOT_Y;
    s->hardware.ext_led_color_danger_x      = DEFAULT_EXT_LED_COLOR_DANGER_X;
    s->hardware.ext_led_color_danger_y      = DEFAULT_EXT_LED_COLOR_DANGER_Y;
    s->hardware.ext_led_color_freeze_x      = DEFAULT_EXT_LED_COLOR_FREEZE_X;
    s->hardware.ext_led_color_freeze_y      = DEFAULT_EXT_LED_COLOR_FREEZE_Y;
    s->hardware.encoder_steps_per_detent = DEFAULT_ENCODER_STEPS_PER_DETENT;
    s->hardware.encoder_accel_enabled = DEFAULT_ENCODER_ACCEL_ENABLED;
    s->hardware.encoder_accel_threshold_ms = DEFAULT_ENCODER_ACCEL_THRESHOLD_MS;
    s->hardware.encoder_invert_direction = DEFAULT_ENCODER_INVERT_DIRECTION;
    s->hardware.version = HARDWARE_CONFIG_SCHEMA_VERSION;
}

/**
 * @brief Applies compile-time defaults to the calibration sub-block.
 *
 * @param s Pointer to the SystemSettings to populate.
 */
static void apply_defaults_calibration(SystemSettings *s) {
    s->calibration.ir_object_cal  = DEFAULT_IR_OBJECT_CAL;
    s->calibration.ir_ambient_cal = DEFAULT_IR_AMBIENT_CAL;
    s->calibration.version        = 1;
}

/**
 * @brief Applies compile-time defaults to the thermal sub-block.
 *
 * @param s Pointer to the SystemSettings to populate.
 */
static void apply_defaults_thermal(SystemSettings *s) {
    s->thermal.cpu_temp_warning        = DEFAULT_CPU_TEMP_WARNING;
    s->thermal.cpu_temp_critical       = DEFAULT_CPU_TEMP_CRITICAL;
    s->thermal.cpu_temp_emergency      = DEFAULT_CPU_TEMP_EMERGENCY;
    s->thermal.cpu_polling_interval_ms = DEFAULT_CPU_TEMP_POLLING_MS;
    s->thermal.version                 = 1;
}

/**
 * @brief Applies compile-time defaults to all sub-blocks.
 *
 * @param s Pointer to the SystemSettings to fully reset.
 */
static void apply_defaults_all(SystemSettings *s) {
    apply_defaults_system(s);
    apply_defaults_zigbee(s);
    apply_defaults_control(s);
    apply_defaults_hardware(s);
    apply_defaults_calibration(s);
    apply_defaults_thermal(s);
    apply_defaults_localization(s);
    s->restart_required = false;
}

static void apply_defaults_localization(SystemSettings *s) {
    memset(&s->localization, 0, sizeof(s->localization));
    memcpy(s->localization.locale, DEFAULT_LOCALIZATION_LOCALE,
           sizeof(DEFAULT_LOCALIZATION_LOCALE));
    s->localization.version = LOCALIZATION_SCHEMA_VERSION;
}

/* Called under the configuration mutex after checked live-state application.
 * Keeping selection in this owner prevents late consumers from restoring an old language. */
static void apply_locale_selection(void) {
    if (!Localization_Apply(s_mgr.settings.localization.locale)) {
        (void)Localization_Apply(DEFAULT_LOCALIZATION_LOCALE);
        ESP_LOGW(TAG, "Configured language '%s' is unavailable; using Turkish without changing storage",
                 s_mgr.settings.localization.locale);
    }
}

/* =========================================================================
 * EventBus notification helper
 * ========================================================================= */

/**
 * @brief Publishes EVENT_CONFIG_CHANGED to the EventBus if available.
 *
 * Safe to call with mutex released; does not require the lock.
 *
 * @note A consistent snapshot is taken under the configuration mutex, then
 *       published after unlock. EventBus copies it before this function returns.
 */
static esp_err_t config_get_defaults(IConfigurationManager *self,SystemSettings *output) {
    if (self!=&s_mgr.base || !output) return ESP_ERR_INVALID_ARG;
    SystemSettings defaults={0};
    apply_defaults_all(&defaults);
    *output=defaults;
    return ESP_OK;
}

static esp_err_t config_get_snapshot(IConfigurationManager *self, SystemSettings *output);

static void retry_config_notification(void) {
    if (!atomic_load(&s_mgr.notificationPending) ||
        atomic_flag_test_and_set(&s_notificationPublishing)) return;
    /* Clear before snapshotting: an update concurrent with publication must
     * remain pending, including updates made by an injected callback. */
    atomic_store(&s_mgr.notificationPending, false);
    IEventBus *eb = GetEventBus();
    if (!eb || !eb->Publish) {
        atomic_store(&s_mgr.notificationPending, true);
        atomic_flag_clear(&s_notificationPublishing);
        return;
    }
    SystemSettings snapshot;
    if (config_get_snapshot(&s_mgr.base, &snapshot) != ESP_OK) {
        ESP_LOGE(TAG, "Cannot snapshot configuration event");
        atomic_store(&s_mgr.notificationPending, true);
        atomic_flag_clear(&s_notificationPublishing);
        return;
    }
    _Static_assert(sizeof(SystemSettings) <= EVENT_BUS_PAYLOAD_CAPACITY, "Configuration event exceeds queue payload capacity");

    BusEvent event = {
        .eventName = EVENT_CONFIG_CHANGED,
        .type      = EVENT_TYPE_INFO,
        .data      = &snapshot,
        .dataSize  = sizeof(SystemSettings)
    };
    if (eb->Publish(event) != 0) atomic_store(&s_mgr.notificationPending, true);
    atomic_flag_clear(&s_notificationPublishing);
}

static void publish_config_changed(void) {
    atomic_store(&s_mgr.notificationPending, true);
    retry_config_notification();
}

/* =========================================================================
 * Event-driven handlers (subscribed on Start, unsubscribed on Stop)
 * ========================================================================= */

/**
 * @brief Handles EVENT_CONFIG_SAVE_REQ: triggers an async NVS save.
 *
 * If the event carries a non-empty data payload it is interpreted as the
 * block name string to save; otherwise all blocks are saved.
 *
 * @param event Incoming bus event.
 */
static void on_save_req_event(BusEvent event) {
    const char *block_name = NULL;
    if (event.dataSize > 0) {
        if (!event.data || !memchr(event.data, '\0', event.dataSize)) {
            ESP_LOGE(TAG, "Rejected unterminated configuration save payload");
            return;
        }
        block_name = (const char *)event.data;
        bool known = false;
        for (size_t i = 0; i < CONFIG_BLOCK_COUNT; ++i) {
            if (strcmp(block_name, k_blocks[i].name) == 0) { known = true; break; }
        }
        if (!known) {
            ESP_LOGE(TAG, "Rejected unknown configuration save block");
            return;
        }
    }
    if (config_save_async(&s_mgr.base, block_name) != ESP_OK)
        ESP_LOGE(TAG, "Configuration save request rejected");
}

/**
 * @brief Handles EVENT_CONFIG_RESTART_REQ: sets the restart_required flag.
 *
 * @param event Incoming bus event (payload ignored).
 */
static esp_err_t enqueue_config_request(uint8_t request);
static void on_restart_req_event(BusEvent event) {
    (void)event;
    if (enqueue_config_request((uint8_t)(CONFIG_BLOCK_COUNT + 1)) != ESP_OK)
        ESP_LOGE(TAG, "Restart flag request rejected");
}

/* =========================================================================
 * IService interface implementations
 * ========================================================================= */

/**
 * @brief Starts the ConfigurationManager service.
 *
 * Transitions from INITIALIZED → STARTED.
 *
 * @param self IService pointer (unused; global singleton is used internally).
 * @return 0 on success, -1 if not in INITIALIZED state.
 */
static void async_save_task(void *param);

static const char *config_events[] = {
    EVENT_CONFIG_SAVE_REQ, "system_config_save_requested", EVENT_CONFIG_RESTART_REQ
};
static BusEventHandler config_handlers[] = {
    on_save_req_event, on_save_req_event, on_restart_req_event
};

/* Lifecycle mutex is never acquired by event handlers. BUSY retains ownership. */
static int32_t remove_config_subscriptions(void) {
    int32_t result = 0;
    for (unsigned i = 0; i < 3; ++i) {
        if (!(s_mgr.subscriptions & (1U << i))) continue;
        if (!s_mgr.subscriptionBus || !s_mgr.subscriptionBus->Unsubscribe ||
            s_mgr.subscriptionBus->Unsubscribe(s_mgr.subscriptionBus, config_events[i], config_handlers[i]) != 0) {
            result = -1;
        } else s_mgr.subscriptions &= ~(1U << i);
    }
    if (!s_mgr.subscriptions) s_mgr.subscriptionBus = NULL;
    return result;
}

static int32_t config_start(IService *self) {
    if (self != &s_mgr.base.base || !s_mgr.lifecycleMutex ||
        xSemaphoreTake(s_mgr.lifecycleMutex, pdMS_TO_TICKS(CONFIG_CONTROL_TIMEOUT_MS)) != pdTRUE) return -1;
    if (s_mgr.state != SERVICE_STATE_INITIALIZED && s_mgr.state != SERVICE_STATE_STOPPED) {
        xSemaphoreGive(s_mgr.lifecycleMutex); return -1;
    }
    IEventBus *eb = GetEventBus();
    if (eb && (!eb->Subscribe || !eb->Unsubscribe)) {
        xSemaphoreGive(s_mgr.lifecycleMutex); return -1;
    }
    s_mgr.subscriptionBus = eb;
    if (eb) {
        for (unsigned i = 0; i < 3; ++i) {
            if (eb->Subscribe(eb, config_events[i], config_handlers[i]) != 0) {
                s_mgr.state = SERVICE_STATE_ERROR;
                if (remove_config_subscriptions() == 0) s_mgr.state = SERVICE_STATE_STOPPED;
                xSemaphoreGive(s_mgr.lifecycleMutex); return -1;
            }
            s_mgr.subscriptions |= (1U << i);
        }
    }
    if (!s_mgr.requests) s_mgr.requests = xQueueCreate(CONFIG_ASYNC_QUEUE_LENGTH, sizeof(uint8_t));
    if (!s_mgr.requests || WatchdogWorker_Init(&s_mgr.worker) != 0 ||
        WatchdogWorker_Start(&s_mgr.worker, async_save_task, "ConfigAsync", CONFIG_ASYNC_STACK_SIZE,
                             NULL, CONFIG_ASYNC_TASK_PRIORITY) != 0) {
        s_mgr.state = SERVICE_STATE_ERROR;
        if (remove_config_subscriptions() == 0) s_mgr.state = SERVICE_STATE_STOPPED;
        xSemaphoreGive(s_mgr.lifecycleMutex); return -1;
    }
    atomic_store(&s_mgr.asyncError, ESP_OK);
    s_mgr.state = SERVICE_STATE_STARTED;
    xSemaphoreGive(s_mgr.lifecycleMutex);
    return 0;
}

static int32_t config_stop(IService *self) {
    if (self != &s_mgr.base.base || !s_mgr.lifecycleMutex ||
          xSemaphoreTake(s_mgr.lifecycleMutex, pdMS_TO_TICKS(CONFIG_CONTROL_TIMEOUT_MS)) != pdTRUE) return -1;
    if (atomic_load(&s_factoryResetOwner)) { xSemaphoreGive(s_mgr.lifecycleMutex); return -1; }
    if (s_mgr.state == SERVICE_STATE_STOPPED) {
        xSemaphoreGive(s_mgr.lifecycleMutex); return 0;
    }
    if (s_mgr.state != SERVICE_STATE_STARTED && s_mgr.state != SERVICE_STATE_ERROR) {
        xSemaphoreGive(s_mgr.lifecycleMutex); return -1;
    }
    s_mgr.state = SERVICE_STATE_ERROR;
    /* Worker acknowledges only outside settings/NVS ownership and after draining
     * requests admitted before closing. Timeout keeps all control resources. */
    if (atomic_load(&s_mgr.worker.task) &&
        WatchdogWorker_Stop(&s_mgr.worker, pdMS_TO_TICKS(CONFIG_STOP_TIMEOUT_MS)) != 0) {
        xSemaphoreGive(s_mgr.lifecycleMutex); return -1;
    }
    int32_t result = remove_config_subscriptions();
    if (result == 0) s_mgr.state = SERVICE_STATE_STOPPED;
    xSemaphoreGive(s_mgr.lifecycleMutex);
    return result;
}

/**
 * @brief Returns the current service lifecycle state.
 *
 * @param self IService pointer (unused).
 * @return Current ServiceState value.
 */
static ServiceState config_get_state(IService *self) {
    (void)self;
    return s_mgr.state;
}

/**
 * @brief Performs a health check on the service.
 *
 * @param self IService pointer (unused).
 * @return 0 if healthy (mutex is valid), -1 otherwise.
 */
static int32_t config_check_health(IService *self) {
    (void)self;
    return s_mgr.mutex && s_mgr.state != SERVICE_STATE_ERROR &&
        atomic_load(&s_mgr.asyncError) == ESP_OK ? 0 : -1;
}

/* =========================================================================
 * Save / Load implementations
 * ========================================================================= */

/**
 * @brief Synchronously saves one or all configuration blocks to NVS.
 *
 * Each block is saved independently; the function records per-block failures
 * so that a single failing block does not prevent others from being saved.
 *
 * @param self       IConfigurationManager pointer (unused; singleton used).
 * @param block_name Named block, or NULL for all blocks.
 * @return ESP_OK if all targeted blocks succeeded, ESP_FAIL otherwise.
 */
static esp_err_t config_save(IConfigurationManager *self, const char *block_name) {
    (void)self;
    if (!s_mgr.mutex || xSemaphoreTake(s_mgr.mutex, portMAX_DELAY) != pdTRUE)
        return ESP_ERR_INVALID_STATE;
    if (!config_mutation_allowed(self)) { xSemaphoreGive(s_mgr.mutex); return ESP_ERR_NOT_FINISHED; }

    bool any_failed = false;
    bool matched    = false;

    for (size_t i = 0; i < CONFIG_BLOCK_COUNT; i++) {
        if (block_name != NULL && strcmp(block_name, k_blocks[i].name) != 0) {
            continue;
        }
        matched = true;
        const uint8_t *src = (const uint8_t *)&s_mgr.settings + k_blocks[i].offset;
        esp_err_t e = nvs_save_block(k_blocks[i].ns, NS_CONFIG_KEY,
                                     src, k_blocks[i].size);
        if (e != ESP_OK) {
            ESP_LOGE(TAG, "Save failed for '%s': 0x%x",
                     k_blocks[i].name, (unsigned)e);
            any_failed = true;
        } else {
            s_mgr.dirty[i] = false; /* Block successfully persisted. */
        }
    }

    xSemaphoreGive(s_mgr.mutex);

    if (block_name && !matched) {
        ESP_LOGW(TAG, "Unknown block name '%s' – nothing saved", block_name);
        return ESP_ERR_INVALID_ARG;
    }
    if (!any_failed) {
        ESP_LOGI(TAG, "Settings saved: %s", block_name ? block_name : "ALL");
    }
    return any_failed ? ESP_FAIL : ESP_OK;
}

/* One bounded request queue; identifiers never borrow caller storage. */
static esp_err_t enqueue_config_request(uint8_t request) {
    if (!s_mgr.lifecycleMutex || xSemaphoreTake(s_mgr.lifecycleMutex, 0) != pdTRUE)
        return ESP_ERR_TIMEOUT;
    esp_err_t result = ESP_ERR_INVALID_STATE;
    if (s_mgr.state == SERVICE_STATE_STARTED && s_mgr.requests && atomic_load(&s_mgr.worker.task))
        result = xQueueSend(s_mgr.requests, &request, 0) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
    xSemaphoreGive(s_mgr.lifecycleMutex);
    return result;
}

static void process_config_request(uint8_t request) {
    esp_err_t result;
    if (request == CONFIG_BLOCK_COUNT + 1) {
        result = xSemaphoreTake(s_mgr.mutex, portMAX_DELAY) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
        if (result == ESP_OK) {
            s_mgr.settings.restart_required = true;
            xSemaphoreGive(s_mgr.mutex);
        }
    } else if (request <= CONFIG_BLOCK_COUNT) {
        result = config_save(&s_mgr.base, request == CONFIG_BLOCK_COUNT ? NULL : k_blocks[request].name);
    } else result = ESP_ERR_INVALID_ARG;
    atomic_store(&s_mgr.asyncError, result);
    if (result != ESP_OK) ESP_LOGE(TAG, "Queued configuration operation failed: %d", (int)result);
}

static void async_save_task(void *param) {
    (void)param;
    TickType_t wait = pdMS_TO_TICKS(CONFIG_ASYNC_POLL_MS);
    if (!wait) wait = 1;
    for (;;) {
        uint8_t request;
        if (atomic_load(&s_mgr.worker.stopRequested)) {
            while (xQueueReceive(s_mgr.requests, &request, 0) == pdTRUE) process_config_request(request);
            /* Do not checkpoint on the open path: Stop may arrive between the
             * closing check and checkpoint and otherwise bypass queue draining. */
            WatchdogWorker_Checkpoint(&s_mgr.worker, NULL, 0);
            continue;
        }
        retry_config_notification();
        if (xQueueReceive(s_mgr.requests, &request, wait) == pdTRUE) process_config_request(request);
    }
}

static esp_err_t config_save_async(IConfigurationManager *self, const char *block_name) {
    if (self != &s_mgr.base) return ESP_ERR_INVALID_ARG;
    if (!config_mutation_allowed(self)) return ESP_ERR_NOT_FINISHED;
    uint8_t request = CONFIG_BLOCK_COUNT;
    if (block_name) {
        size_t i;
        for (i = 0; i < CONFIG_BLOCK_COUNT; ++i) if (strcmp(block_name, k_blocks[i].name) == 0) break;
        if (i == CONFIG_BLOCK_COUNT) return ESP_ERR_INVALID_ARG;
        request = (uint8_t)i;
    }
    return enqueue_config_request(request);
}

/* -------------------------------------------------------------------------
 * Load
 * ------------------------------------------------------------------------- */

/**
 * @brief Internal migration handler called after NVS load.
 *
 * Called with the configuration mutex held. The NVS helper does not acquire
 * this mutex; loading, migration and persistence form one serialized operation.
 */
_Static_assert(sizeof(HardwareConfigs) == 76, "Hardware NVS blob size must remain compatible");
_Static_assert(sizeof(SystemBaseConfigs) == 20, "System NVS blob size must remain compatible");
_Static_assert(offsetof(SystemBaseConfigs, developer_mode_enabled) == 3, "Developer preference must occupy former padding");
_Static_assert(sizeof(LocalizationConfigs) == 12, "Locale NVS blob size must remain compatible");
_Static_assert(offsetof(LocalizationConfigs, version) == 8, "Locale version must trail the code");
_Static_assert(sizeof(ControlConfigs) == 32 && offsetof(ControlConfigs, version) == 28, "Control V2 layout");
_Static_assert(offsetof(SystemBaseConfigs, version) == 16, "System version offset must remain compatible");
_Static_assert(offsetof(SystemBaseConfigs, power_profile) == 13, "Power profile must occupy former padding");
_Static_assert(offsetof(SystemBaseConfigs, zigbee_tx_profile) == 14, "TX profile must occupy former padding");
_Static_assert(offsetof(SystemBaseConfigs, menu_auto_exit_enabled) == 15, "Menu idle switch must occupy former padding");
_Static_assert(offsetof(HardwareConfigs, version) == 72, "Hardware v7 version follows the appended encoder fields");
_Static_assert(offsetof(HardwareConfigs, builtin_led_brightness) == 66, "Brightness must occupy former padding");

static void run_migrations(void) {
    HardwareConfigs *hw = &s_mgr.settings.hardware;
    if (hw->version >= HARDWARE_CONFIG_SCHEMA_VERSION) return;
    if (hw->version < 3) {
        apply_defaults_hardware(&s_mgr.settings);
    } else if (hw->version < 5) {
        /* Match legacy preset coordinates exactly; preserve arbitrary user colors. */
        static const uint16_t old_xy[][2] = {
            {0x1300,0x1200}, {0x1500,0x6000}, {0x1900,0x6c00}, {0x7000,0x7000},
            {0x9000,0x5000}, {0xb000,0x3000}, {0x3300,0x1800}, {0x3127,0x3290}
        };
        static const uint16_t new_xy[][2] = {
            {RGBLED_PRESET_BLUE_X,RGBLED_PRESET_BLUE_Y}, {RGBLED_PRESET_CYAN_X,RGBLED_PRESET_CYAN_Y},
            {RGBLED_PRESET_GREEN_X,RGBLED_PRESET_GREEN_Y}, {RGBLED_PRESET_YELLOW_X,RGBLED_PRESET_YELLOW_Y},
            {RGBLED_PRESET_ORANGE_X,RGBLED_PRESET_ORANGE_Y}, {RGBLED_PRESET_RED_X,RGBLED_PRESET_RED_Y},
            {RGBLED_PRESET_PURPLE_X,RGBLED_PRESET_PURPLE_Y}, {RGBLED_PRESET_WHITE_X,RGBLED_PRESET_WHITE_Y}
        };
        uint16_t *xs[] = {&hw->ext_led_color_cold_x,&hw->ext_led_color_warm_x,&hw->ext_led_color_hot_x,
                          &hw->ext_led_color_danger_x,&hw->ext_led_color_freeze_x};
        uint16_t *ys[] = {&hw->ext_led_color_cold_y,&hw->ext_led_color_warm_y,&hw->ext_led_color_hot_y,
                          &hw->ext_led_color_danger_y,&hw->ext_led_color_freeze_y};
        for (size_t zone = 0; zone < 5; ++zone) {
            for (size_t color = 0; color < 8; ++color) {
                if (*xs[zone] == old_xy[color][0] && *ys[zone] == old_xy[color][1]) {
                    *xs[zone] = new_xy[color][0];
                    *ys[zone] = new_xy[color][1];
                    break;
                }
            }
        }
        hw->version = 5;
    }
    if (hw->version < 6) hw->builtin_led_brightness = DEFAULT_BUILTIN_LED_BRIGHTNESS;
    if (hw->version < 7) {
        hw->encoder_steps_per_detent = DEFAULT_ENCODER_STEPS_PER_DETENT;
        hw->encoder_accel_enabled = DEFAULT_ENCODER_ACCEL_ENABLED;
        hw->encoder_accel_threshold_ms = DEFAULT_ENCODER_ACCEL_THRESHOLD_MS;
        hw->encoder_invert_direction = DEFAULT_ENCODER_INVERT_DIRECTION;
    }
    hw->builtin_led_startup_anim = DEFAULT_BUILTIN_LED_STARTUP_ANIM;
    hw->version = HARDWARE_CONFIG_SCHEMA_VERSION;
}

/**
 * @brief Sanity-checks critical control temperatures after loading.
 *
 * If relay_open_temp is unreasonably low (e.g. NVS was erased), restore
 * safe defaults to prevent erratic relay behaviour.
 */
/**
 * @brief Sanity-checks critical control temperatures after loading.
 *
 * Restores safe defaults for any control value that is physically impossible
 * or indicates a corrupted/erased NVS (e.g. floats stuck at 0).
 */
static void run_sanity_checks(void) {
    bool restored = false;

    /* Relay temperature must be meaningfully above zero. */
    if (!SystemConfigs_IsFanSetpointPairValid(s_mgr.settings.control.relay_open_temp, s_mgr.settings.control.relay_close_temp)) {
        ESP_LOGW(TAG, "Sanity: relay_open_temp=%.2f invalid – restoring defaults",
                 s_mgr.settings.control.relay_open_temp);
        s_mgr.settings.control.relay_open_temp  = DEFAULT_RELAY_OPEN_TEMP;
        s_mgr.settings.control.relay_close_temp = DEFAULT_RELAY_CLOSE_TEMP;
        restored = true;
    }

    /* Relay open must be strictly higher than close (positive hysteresis). */
    if (s_mgr.settings.control.relay_open_temp <=
        s_mgr.settings.control.relay_close_temp + RELAY_HYSTERESIS_MIN) {
        ESP_LOGW(TAG, "Sanity: relay hysteresis too small (open=%.2f close=%.2f) – restoring defaults",
                 s_mgr.settings.control.relay_open_temp,
                 s_mgr.settings.control.relay_close_temp);
        s_mgr.settings.control.relay_open_temp  = DEFAULT_RELAY_OPEN_TEMP;
        s_mgr.settings.control.relay_close_temp = DEFAULT_RELAY_CLOSE_TEMP;
        restored = true;
    }

    /* Alert thresholds: high must be above low. */
    if (s_mgr.settings.control.threshold_high <=
        s_mgr.settings.control.threshold_low) {
        ESP_LOGW(TAG, "Sanity: threshold ordering invalid (high=%.2f low=%.2f) – restoring defaults",
                 s_mgr.settings.control.threshold_high,
                 s_mgr.settings.control.threshold_low);
        s_mgr.settings.control.threshold_high = DEFAULT_THRESHOLD_HIGH;
        s_mgr.settings.control.threshold_low  = DEFAULT_THRESHOLD_LOW;
        restored = true;
    }

    if (!restored) {
        ESP_LOGD(TAG, "Sanity checks passed");
    }
}

/**
 * @brief Loads one or all configuration blocks from NVS.
 *
 * Blocks not found in NVS (first boot) retain their in-memory defaults.
 * After loading, sanity checks and schema migrations are applied.
 *
 * @param self       IConfigurationManager pointer (unused).
 * @param block_name Named block to load, or NULL for all.
 * @return ESP_OK on success, ESP_FAIL if any read failed.
 */
static esp_err_t config_validate_candidate(SystemSettings s);

static esp_err_t config_load(IConfigurationManager *self, const char *block_name) {
    if (self != &s_mgr.base) return ESP_ERR_INVALID_ARG;
    if (block_name) {
        bool found = false;
        for (size_t i = 0; i < CONFIG_BLOCK_COUNT; ++i) if (strcmp(block_name, k_blocks[i].name) == 0) found = true;
        if (!found) return ESP_ERR_INVALID_ARG;
    }
    if (!s_mgr.mutex || xSemaphoreTake(s_mgr.mutex, portMAX_DELAY) != pdTRUE)
        return ESP_ERR_INVALID_STATE;
    if (!config_mutation_allowed(self)) { xSemaphoreGive(s_mgr.mutex); return ESP_ERR_NOT_FINISHED; }
    SystemSettings original = s_mgr.settings;

    bool any_failed = false;
    bool persist_hardware = false, persist_system = false, persist_control = false;

    for (size_t i = 0; i < CONFIG_BLOCK_COUNT; i++) {
        if (block_name != NULL && strcmp(block_name, k_blocks[i].name) != 0) {
            continue;
        }
        uint8_t *dst = (uint8_t *)&s_mgr.settings + k_blocks[i].offset;
        bool migrated = false;
        esp_err_t e = nvs_load_block(k_blocks[i].ns, NS_CONFIG_KEY,
                                     dst, k_blocks[i].size, &migrated);
        if (migrated && !strcmp(k_blocks[i].name, "control")) persist_control = true;
        if (e != ESP_OK && e != ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGE(TAG, "Load failed for '%s': 0x%x",
                     k_blocks[i].name, (unsigned)e);
            any_failed = true;
        }
    }

    /* Apply sanity checks while still holding the lock. */
    run_sanity_checks();

    if (!any_failed && (block_name == NULL || strcmp(block_name, "hardware") == 0)) {
        persist_hardware = s_mgr.settings.hardware.version < HARDWARE_CONFIG_SCHEMA_VERSION;
        run_migrations();
        if (!any_failed) {
            HardwareConfigs *hardware = &s_mgr.settings.hardware;
            bool changed = false;
            uint8_t *selections[] = {&hardware->builtin_led_startup_anim,
                &hardware->builtin_led_idle_anim, &hardware->builtin_led_connecting_anim,
                &hardware->builtin_led_fault_anim};
            for (size_t index = 0; index < sizeof(selections) / sizeof(selections[0]); ++index) {
                if (!LEDAnimationCatalog_IsValid(*selections[index], true)) {
                    *selections[index] = 255;
                    changed = true;
                }
            }
            if (!LEDAnimationCatalog_IsValid(hardware->ext_led_anim, false)) {
                /* Legacy temperature ID is a color mode, not an animated frame source. */
                if (hardware->ext_led_anim == RGBLED_ANIM_TEMPERATURE &&
                    hardware->ext_led_mode == EXT_LED_MODE_ANIMATION)
                    hardware->ext_led_mode = EXT_LED_MODE_TEMPERATURE;
                hardware->ext_led_anim = RGBLED_ANIM_NONE;
                changed = true;
            }
            persist_hardware |= changed;
        }
    }
    if (!any_failed && (block_name == NULL || strcmp(block_name, "system") == 0)) {
        SystemBaseConfigs *system = &s_mgr.settings.system;
        /* Retired off mode becomes automatic only when loading old storage. */
        if (system->display_mode == 0) {
            system->display_mode = DEFAULT_DISPLAY_MODE;
            persist_system = true;
        }
        if (system->version < 3 || system->power_profile >= POWER_PROFILE_COUNT ||
            system->zigbee_tx_profile >= ZIGBEE_TX_PROFILE_COUNT) {
            if (system->version < 2 || system->power_profile >= POWER_PROFILE_COUNT)
                system->power_profile = DEFAULT_POWER_PROFILE;
            if (system->version < 3 || system->zigbee_tx_profile >= ZIGBEE_TX_PROFILE_COUNT)
                system->zigbee_tx_profile = DEFAULT_ZIGBEE_TX_PROFILE;
            if (system->version < 3) system->version = 3;
            persist_system = true;
        }
    }
    if (!any_failed && (block_name == NULL || strcmp(block_name, "system") == 0) &&
        s_mgr.settings.system.version < 4) {
        /* V1-V3 byte 15 was unspecified padding; never interpret it as a setting. */
        s_mgr.settings.system.menu_auto_exit_enabled = DEFAULT_MENU_AUTO_EXIT_ENABLED;
        s_mgr.settings.system.version = 4;
        persist_system = true;
    }
    if (!any_failed && (block_name == NULL || strcmp(block_name, "system") == 0) &&
        s_mgr.settings.system.version < 5) {
        /* V1-V4 byte 3 was padding, not a stored user preference. */
        s_mgr.settings.system.developer_mode_enabled = DEFAULT_DEVELOPER_MODE_ENABLED;
        s_mgr.settings.system.version = SYSTEM_CONFIG_SCHEMA_VERSION;
        persist_system = true;
    }
    if (!any_failed) any_failed = config_validate_candidate(s_mgr.settings) != ESP_OK;
    /* Validate all blocks before committing any normalized migration. NVS
     * namespaces commit independently; this is not a cross-namespace transaction. */
    if (!any_failed && persist_hardware)
        any_failed = nvs_save_block(NS_CONFIG_HARDWARE, NS_CONFIG_KEY,
            &s_mgr.settings.hardware, sizeof(s_mgr.settings.hardware)) != ESP_OK;
    if (!any_failed && persist_system)
        any_failed = nvs_save_block(NS_CONFIG_SYSTEM, NS_CONFIG_KEY,
            &s_mgr.settings.system, sizeof(s_mgr.settings.system)) != ESP_OK;
    if (!any_failed && persist_control)
        any_failed = nvs_save_block(NS_CONFIG_CONTROL, NS_CONFIG_KEY, &s_mgr.settings.control, sizeof(s_mgr.settings.control)) != ESP_OK;
    bool changed = !any_failed && memcmp(&original, &s_mgr.settings, sizeof(original)) != 0;
    if (any_failed) s_mgr.settings = original;
    else {
        apply_locale_selection();
        DeveloperMode_SetEnabled(s_mgr.settings.system.developer_mode_enabled != 0);
    }
    xSemaphoreGive(s_mgr.mutex);
    if (changed && s_initialized) publish_config_changed();
    return any_failed ? ESP_FAIL : ESP_OK;
}

/* =========================================================================
 * GetSettings / IsRestartRequired
 * ========================================================================= */

/**
 * @brief Returns a pointer to the live SystemSettings object.
 *
 * @param self IConfigurationManager pointer (unused).
 * @return Pointer to the managed SystemSettings instance.
 */
/* Bounded CLI metadata observation; not a health callback. */
#define CONFIG_STATUS_TIMEOUT_MS 100

static esp_err_t config_get_status(IConfigurationManager *self, ConfigurationStatus *output) {
    if (self != &s_mgr.base || !output) return ESP_ERR_INVALID_ARG;
    _Static_assert(CONFIG_BLOCK_COUNT == CONFIG_STATUS_BLOCK_CAPACITY, "Status capacity must match schema");
    TickType_t wait = pdMS_TO_TICKS(CONFIG_STATUS_TIMEOUT_MS);
    if (!wait) wait = 1;
    if (!s_mgr.mutex) return ESP_ERR_INVALID_STATE;
    if (xSemaphoreTake(s_mgr.mutex, wait) != pdTRUE) return ESP_ERR_TIMEOUT;
    ConfigurationStatus snapshot = {0};
    snapshot.restart_required = s_mgr.settings.restart_required;
    snapshot.notification_pending = atomic_load(&s_mgr.notificationPending);
    snapshot.block_count = CONFIG_BLOCK_COUNT;
    for (size_t i = 0; i < CONFIG_BLOCK_COUNT; ++i) {
        size_t length = strlen(k_blocks[i].name);
        if (length >= CONFIG_STATUS_NAME_CAPACITY) { xSemaphoreGive(s_mgr.mutex); return ESP_ERR_INVALID_SIZE; }
        memcpy(snapshot.blocks[i].name, k_blocks[i].name, length + 1);
        snapshot.blocks[i].dirty = s_mgr.dirty[i];
        const uint8_t *version = (const uint8_t *)&s_mgr.settings + k_blocks[i].offset + k_blocks[i].size - sizeof(uint32_t);
        memcpy(&snapshot.blocks[i].version, version, sizeof(uint32_t));
    }
    xSemaphoreGive(s_mgr.mutex);
    *output = snapshot;
    return ESP_OK;
}

static SystemSettings *config_get_settings(IConfigurationManager *self) {
    return self == &s_mgr.base ? &s_mgr.settings : NULL;
}

/* Compatibility only: false also represents an unavailable snapshot. */
static bool config_is_restart_required(IConfigurationManager *self) {
    ConfigurationStatus status;
    return config_get_status(self, &status) == ESP_OK && status.restart_required;
}

/* =========================================================================
 * UpdateSettings
 * ========================================================================= */

/**
 * @brief Atomically replaces in-memory settings and publishes an event.
 *
 * @param self         IConfigurationManager pointer (unused).
 * @param new_settings Source data pointer.
 * @param size         Must equal sizeof(SystemSettings).
 * @return ESP_OK on success, ESP_ERR_INVALID_SIZE on mismatch.
 */
static esp_err_t config_validate_candidate(SystemSettings s);

static esp_err_t config_update_settings(IConfigurationManager *self,
                                         void *new_settings, size_t size) {
    (void)self;
    if (!self || !new_settings)          return ESP_ERR_INVALID_ARG;
    if (size != sizeof(SystemSettings)) return ESP_ERR_INVALID_SIZE;

    if (!s_mgr.mutex || xSemaphoreTake(s_mgr.mutex, portMAX_DELAY) != pdTRUE)
        return ESP_ERR_INVALID_STATE;
    if (!config_mutation_allowed(self)) { xSemaphoreGive(s_mgr.mutex); return ESP_ERR_NOT_FINISHED; }
    SystemSettings candidate;
    memcpy(&candidate, new_settings, sizeof(candidate));
    esp_err_t valid = config_validate_candidate(candidate);
    if (valid != ESP_OK) {
        xSemaphoreGive(s_mgr.mutex);
        return valid;
    }
    memcpy(&s_mgr.settings, &candidate, size);
    apply_locale_selection();
    DeveloperMode_SetEnabled(s_mgr.settings.system.developer_mode_enabled != 0);
    /* Mark every block dirty — caller must Save() to persist. */
    for (size_t i = 0; i < CONFIG_BLOCK_COUNT; i++) {
        s_mgr.dirty[i] = true;
    }
    xSemaphoreGive(s_mgr.mutex);

    publish_config_changed();
    return ESP_OK;
}

/* =========================================================================
 * ValidateSettings
 * ========================================================================= */

/**
 * @brief Validates in-memory settings against defined physical bounds.
 *
 * Checks performed:
 *   - relay_open_temp > relay_close_temp (hysteresis must be positive)
 *   - threshold_high > threshold_low
 *   - Calibration offsets within [CALIBRATION_MIN, CALIBRATION_MAX]
 *   - CPU thermal thresholds are ordered: warning < critical < emergency
 *   - ext_led_brightness in [0, 255]
 *
 * @param self IConfigurationManager pointer (unused).
 * @return ESP_OK if all values are valid, ESP_ERR_INVALID_ARG otherwise.
 */
static esp_err_t config_validate_candidate(SystemSettings s) {
    _Static_assert(sizeof(bool) == sizeof(uint8_t), "Persisted boolean schema requires one byte");
    const bool *flags[] = {&s.system.system_enabled, &s.control.fan_manual_mode,
        &s.control.fan_manual_state, &s.hardware.builtin_led_state,
        &s.hardware.rgb_red_state, &s.hardware.rgb_green_state,
        &s.hardware.rgb_blue_state, &s.restart_required};
    for (size_t i = 0; i < sizeof(flags)/sizeof(flags[0]); ++i) {
        uint8_t representation;
        memcpy(&representation, flags[i], sizeof(representation));
        if (representation > 1) return ESP_ERR_INVALID_ARG;
    }
    bool ok = true;
    if (!isfinite(s.control.threshold_high) || !isfinite(s.control.threshold_low) ||
        !isfinite(s.calibration.ir_object_cal) || !isfinite(s.calibration.ir_ambient_cal) ||
        !isfinite(s.thermal.cpu_temp_warning) || !isfinite(s.thermal.cpu_temp_critical) ||
        !isfinite(s.thermal.cpu_temp_emergency)) {
        ESP_LOGE(TAG, "Validate: non-finite control, calibration or thermal value");
        return ESP_ERR_INVALID_ARG;
    }
    if (s.system.developer_mode_enabled > 1 || s.system.menu_auto_exit_enabled > 1 ||
        !SystemConfigs_IsValidDisplayMode(s.system.display_mode) ||
        s.hardware.builtin_led_anim_mode >= BUILTIN_LED_MODE_COUNT)
        return ESP_ERR_INVALID_ARG;
    if (s.control.threshold_high > TEMP_THRESHOLD_MAX ||
        s.control.threshold_low < TEMP_THRESHOLD_MIN ||
        s.thermal.cpu_temp_warning < TEMP_THRESHOLD_MIN ||
        s.thermal.cpu_temp_emergency > TEMP_THRESHOLD_MAX)
        return ESP_ERR_INVALID_ARG;

    /* ── Control: relay hysteresis ──────────────────────────────────────── */
    if (!SystemConfigs_IsFanSourceConfigValid(&s.control)) return ESP_ERR_INVALID_ARG;
    if (!SystemConfigs_IsFanSetpointPairValid(s.control.relay_open_temp, s.control.relay_close_temp)) {
        ESP_LOGE(TAG, "Validate: relay_open_temp (%.2f) must be > relay_close_temp (%.2f)",
                 s.control.relay_open_temp, s.control.relay_close_temp);
        ok = false;
    }

    /* ── Control: alert threshold ordering ──────────────────────────────── */
    if (s.control.threshold_high <= s.control.threshold_low) {
        ESP_LOGE(TAG, "Validate: threshold_high (%.2f) must be > threshold_low (%.2f)",
                 s.control.threshold_high, s.control.threshold_low);
        ok = false;
    }

    /* ── Calibration: offset bounds ─────────────────────────────────────── */
    if (s.calibration.ir_object_cal < CALIBRATION_MIN ||
        s.calibration.ir_object_cal > CALIBRATION_MAX) {
        ESP_LOGE(TAG, "Validate: ir_object_cal (%.2f) out of [%.2f, %.2f]",
                 s.calibration.ir_object_cal, CALIBRATION_MIN, CALIBRATION_MAX);
        ok = false;
    }
    if (s.calibration.ir_ambient_cal < CALIBRATION_MIN ||
        s.calibration.ir_ambient_cal > CALIBRATION_MAX) {
        ESP_LOGE(TAG, "Validate: ir_ambient_cal (%.2f) out of [%.2f, %.2f]",
                 s.calibration.ir_ambient_cal, CALIBRATION_MIN, CALIBRATION_MAX);
        ok = false;
    }

    /* ── Thermal: threshold ordering ─────────────────────────────────────── */
    if (s.thermal.cpu_temp_warning >= s.thermal.cpu_temp_critical) {
        ESP_LOGE(TAG, "Validate: cpu_temp_warning (%.1f) must be < critical (%.1f)",
                 s.thermal.cpu_temp_warning, s.thermal.cpu_temp_critical);
        ok = false;
    }
    if (s.thermal.cpu_temp_critical >= s.thermal.cpu_temp_emergency) {
        ESP_LOGE(TAG, "Validate: cpu_temp_critical (%.1f) must be < emergency (%.1f)",
                 s.thermal.cpu_temp_critical, s.thermal.cpu_temp_emergency);
        ok = false;
    }

    if (s.system.power_profile >= POWER_PROFILE_COUNT) {
        ESP_LOGE(TAG, "Validate: invalid power profile %u", (unsigned)s.system.power_profile);
        ok = false;
    }
    if (!SystemConfigs_IsLocalizationValid(&s.localization)) {
        ESP_LOGE(TAG, "Validate: invalid localization code or schema");
        ok = false;
    }
    if (s.system.zigbee_tx_profile >= ZIGBEE_TX_PROFILE_COUNT) {
        ESP_LOGE(TAG, "Validate: invalid Zigbee TX profile %u", (unsigned)s.system.zigbee_tx_profile);
        ok = false;
    }

    /* ── System: temperature unit ───────────────────────────────────────── */
    if (s.system.temp_unit >= TEMP_UNIT_COUNT) {
        ESP_LOGE(TAG, "Validate: temp_unit (%u) invalid (must be < %u)",
                 (unsigned)s.system.temp_unit, (unsigned)TEMP_UNIT_COUNT);
        ok = false;
    }

    /* ── System: OLED timeout ───────────────────────────────────────────── */
    if (s.system.oled_timeout_sec < OLED_TIMEOUT_MIN_SEC ||
        s.system.oled_timeout_sec > OLED_TIMEOUT_MAX_SEC) {
        ESP_LOGE(TAG, "Validate: oled_timeout_sec (%u) out of [%u, %u]",
                 (unsigned)s.system.oled_timeout_sec,
                 (unsigned)OLED_TIMEOUT_MIN_SEC,
                 (unsigned)OLED_TIMEOUT_MAX_SEC);
        ok = false;
    }

    if (s.hardware.encoder_steps_per_detent == 0 ||
        s.hardware.encoder_accel_enabled > 1 || s.hardware.encoder_invert_direction > 1) ok = false;

    /* ── System: telemetry interval ─────────────────────────────────────── */
    if ((s.system.telemetry_interval_ms != 0 &&
         s.system.telemetry_interval_ms < TELEMETRY_ACTIVE_MIN_MS) ||
        s.system.telemetry_interval_ms > TELEMETRY_MAX_MS) {
        ESP_LOGE(TAG, "Validate: telemetry_interval_ms (%u) out of [%u, %u]",
                 (unsigned)s.system.telemetry_interval_ms,
                 (unsigned)TELEMETRY_MIN_MS, (unsigned)TELEMETRY_MAX_MS);
        ok = false;
    }

    /* ── Zigbee: reporting interval bounds ───────────────────────────────── */
#define CHECK_ZB(field) \
    if (s.zigbee_reporting.field > ZB_REPORT_MAX_S) { \
        ESP_LOGE(TAG, "Validate: zigbee_reporting." #field " (%u) out of [%u, %u]", \
                 (unsigned)s.zigbee_reporting.field, \
                 (unsigned)ZB_REPORT_MIN_S, (unsigned)ZB_REPORT_MAX_S); \
        ok = false; \
    }
    CHECK_ZB(fan_s)
    CHECK_ZB(ambient_s)
    CHECK_ZB(cpu_s)
    CHECK_ZB(health_s)
    CHECK_ZB(memory_s)
    CHECK_ZB(uptime_s)
#undef CHECK_ZB

    /* ── Thermal: polling interval ───────────────────────────────────────── */
    if (s.thermal.cpu_polling_interval_ms < CPU_POLL_MIN_MS ||
        s.thermal.cpu_polling_interval_ms > CPU_POLL_MAX_MS) {
        ESP_LOGE(TAG, "Validate: cpu_polling_interval_ms (%u) out of [%u, %u]",
                 (unsigned)s.thermal.cpu_polling_interval_ms,
                 (unsigned)CPU_POLL_MIN_MS, (unsigned)CPU_POLL_MAX_MS);
        ok = false;
    }

    /* ── Hardware: ext_led_mode ──────────────────────────────────────────── */
    if (!LEDAnimationCatalog_IsValid(s.hardware.builtin_led_startup_anim, true) ||
        !LEDAnimationCatalog_IsValid(s.hardware.builtin_led_idle_anim, true) ||
        !LEDAnimationCatalog_IsValid(s.hardware.builtin_led_connecting_anim, true) ||
        !LEDAnimationCatalog_IsValid(s.hardware.builtin_led_fault_anim, true) ||
        !LEDAnimationCatalog_IsValid(s.hardware.ext_led_anim, false)) {
        ESP_LOGE(TAG, "Validate: unsupported LED animation selection");
        ok = false;
    }
    if (s.hardware.ext_led_mode >= EXT_LED_MODE_COUNT) {
        ESP_LOGE(TAG, "Validate: ext_led_mode (%u) invalid (must be < %u)",
                 (unsigned)s.hardware.ext_led_mode, (unsigned)EXT_LED_MODE_COUNT);
        ok = false;
    }

    /* ── RGB LED thermal zones ──────────────────────────────────────────── */
    if (!SystemConfigs_ValidateRGBXY(&s.hardware)) {
        ESP_LOGE(TAG, "Validate: invalid RGB XY coordinates");
        ok = false;
    }
    if (!SystemConfigs_ValidateRGBThermal(&s.hardware)) {
        ESP_LOGE(TAG, "Validate: invalid RGB thermal zones");
        ok = false;
    }

    if (ok) {
        ESP_LOGI(TAG, "Settings validation passed");
    }
    return ok ? ESP_OK : ESP_ERR_INVALID_ARG;
}

static esp_err_t config_validate_settings(IConfigurationManager *self) {
    if (!self || !s_mgr.mutex || xSemaphoreTake(s_mgr.mutex, portMAX_DELAY) != pdTRUE)
        return ESP_ERR_INVALID_STATE;
    SystemSettings snapshot = s_mgr.settings;
    xSemaphoreGive(s_mgr.mutex);
    return config_validate_candidate(snapshot);
}


/** Exact writable field boundaries; sizes follow the actual schema types. */
typedef struct { size_t block_offset; size_t offset; size_t size; } ConfigFieldDef;
#define CONFIG_FIELD(block, type, field) {offsetof(SystemSettings, block), offsetof(type, field), sizeof(((type *)0)->field)}
static const ConfigFieldDef k_fields[] = {
    CONFIG_FIELD(system, SystemBaseConfigs, system_enabled),
    CONFIG_FIELD(system, SystemBaseConfigs, temp_unit),
    CONFIG_FIELD(system, SystemBaseConfigs, telemetry_interval_ms),
    CONFIG_FIELD(system, SystemBaseConfigs, oled_timeout_sec),
    CONFIG_FIELD(system, SystemBaseConfigs, display_mode),
    CONFIG_FIELD(system, SystemBaseConfigs, menu_auto_exit_enabled),
    CONFIG_FIELD(system, SystemBaseConfigs, developer_mode_enabled),
    CONFIG_FIELD(system, SystemBaseConfigs, power_profile),
    CONFIG_FIELD(system, SystemBaseConfigs, zigbee_tx_profile),
    CONFIG_FIELD(control, ControlConfigs, threshold_high),
    CONFIG_FIELD(control, ControlConfigs, threshold_low),
    CONFIG_FIELD(control, ControlConfigs, relay_open_temp),
    CONFIG_FIELD(control, ControlConfigs, relay_close_temp),
    CONFIG_FIELD(control, ControlConfigs, fan_manual_mode),
    CONFIG_FIELD(control, ControlConfigs, fan_manual_state),
    CONFIG_FIELD(control, ControlConfigs, fan_strategy),
    CONFIG_FIELD(control, ControlConfigs, object_temperature_source),
    CONFIG_FIELD(control, ControlConfigs, ambient_temperature_source),
    CONFIG_FIELD(control, ControlConfigs, fan_delta_on_decic),
    CONFIG_FIELD(control, ControlConfigs, fan_delta_off_decic),
    CONFIG_FIELD(hardware, HardwareConfigs, builtin_led_brightness),
    CONFIG_FIELD(hardware, HardwareConfigs, encoder_steps_per_detent),
    CONFIG_FIELD(hardware, HardwareConfigs, encoder_accel_enabled),
    CONFIG_FIELD(hardware, HardwareConfigs, encoder_accel_threshold_ms),
    CONFIG_FIELD(hardware, HardwareConfigs, encoder_invert_direction),
    CONFIG_FIELD(hardware, HardwareConfigs, builtin_led_state),
    CONFIG_FIELD(hardware, HardwareConfigs, builtin_led_anim_mode),
    CONFIG_FIELD(hardware, HardwareConfigs, builtin_led_startup_anim),
    CONFIG_FIELD(hardware, HardwareConfigs, builtin_led_idle_anim),
    CONFIG_FIELD(hardware, HardwareConfigs, builtin_led_connecting_anim),
    CONFIG_FIELD(hardware, HardwareConfigs, builtin_led_fault_anim),
    CONFIG_FIELD(hardware, HardwareConfigs, rgb_red_state),
    CONFIG_FIELD(hardware, HardwareConfigs, rgb_green_state),
    CONFIG_FIELD(hardware, HardwareConfigs, rgb_blue_state),
    CONFIG_FIELD(hardware, HardwareConfigs, ext_led_brightness),
    CONFIG_FIELD(hardware, HardwareConfigs, ext_led_temp_cold_min),
    CONFIG_FIELD(hardware, HardwareConfigs, ext_led_temp_cold_max),
    CONFIG_FIELD(hardware, HardwareConfigs, ext_led_temp_warm_min),
    CONFIG_FIELD(hardware, HardwareConfigs, ext_led_temp_warm_max),
    CONFIG_FIELD(hardware, HardwareConfigs, ext_led_temp_hot_min),
    CONFIG_FIELD(hardware, HardwareConfigs, ext_led_temp_hot_max),
    CONFIG_FIELD(hardware, HardwareConfigs, ext_led_temp_danger),
    CONFIG_FIELD(hardware, HardwareConfigs, ext_led_temp_freeze),
    CONFIG_FIELD(hardware, HardwareConfigs, ext_led_color_cold_x),
    CONFIG_FIELD(hardware, HardwareConfigs, ext_led_color_cold_y),
    CONFIG_FIELD(hardware, HardwareConfigs, ext_led_color_warm_x),
    CONFIG_FIELD(hardware, HardwareConfigs, ext_led_color_warm_y),
    CONFIG_FIELD(hardware, HardwareConfigs, ext_led_color_hot_x),
    CONFIG_FIELD(hardware, HardwareConfigs, ext_led_color_hot_y),
    CONFIG_FIELD(hardware, HardwareConfigs, ext_led_color_danger_x),
    CONFIG_FIELD(hardware, HardwareConfigs, ext_led_color_danger_y),
    CONFIG_FIELD(hardware, HardwareConfigs, ext_led_color_freeze_x),
    CONFIG_FIELD(hardware, HardwareConfigs, ext_led_color_freeze_y),
    CONFIG_FIELD(hardware, HardwareConfigs, ext_led_mode),
    CONFIG_FIELD(hardware, HardwareConfigs, ext_led_anim),
    CONFIG_FIELD(calibration, CalibrationConfigs, ir_object_cal),
    CONFIG_FIELD(calibration, CalibrationConfigs, ir_ambient_cal),
    CONFIG_FIELD(thermal, ThermalConfigs, cpu_temp_warning),
    CONFIG_FIELD(thermal, ThermalConfigs, cpu_temp_critical),
    CONFIG_FIELD(thermal, ThermalConfigs, cpu_temp_emergency),
    CONFIG_FIELD(thermal, ThermalConfigs, cpu_polling_interval_ms),
    CONFIG_FIELD(zigbee_reporting, ZigbeeReportingConfigs, fan_s),
    CONFIG_FIELD(zigbee_reporting, ZigbeeReportingConfigs, ambient_s),
    CONFIG_FIELD(zigbee_reporting, ZigbeeReportingConfigs, cpu_s),
    CONFIG_FIELD(zigbee_reporting, ZigbeeReportingConfigs, health_s),
    CONFIG_FIELD(zigbee_reporting, ZigbeeReportingConfigs, memory_s),
    CONFIG_FIELD(zigbee_reporting, ZigbeeReportingConfigs, uptime_s),
    CONFIG_FIELD(localization, LocalizationConfigs, locale),
};
#undef CONFIG_FIELD

static bool config_default_fields_equal(const SystemSettings *actual,
                                        const SystemSettings *expected,
                                        const ConfigBlockDef *block) {
    const uint8_t *a = (const uint8_t *)actual + block->offset;
    const uint8_t *e = (const uint8_t *)expected + block->offset;
    for (size_t i = 0; i < sizeof(k_fields) / sizeof(k_fields[0]); ++i) {
        if (k_fields[i].block_offset == block->offset &&
            memcmp(a + k_fields[i].offset, e + k_fields[i].offset, k_fields[i].size) != 0)
            return false;
    }
    return memcmp(a + block->size - sizeof(uint32_t),
                  e + block->size - sizeof(uint32_t), sizeof(uint32_t)) == 0;
}

static esp_err_t config_verify_defaults(IConfigurationManager *self, SystemSettings *output) {
    if (self != &s_mgr.base || !output) return ESP_ERR_INVALID_ARG;
    if (!s_mgr.mutex || xSemaphoreTake(s_mgr.mutex,
        pdMS_TO_TICKS(CONFIG_CONTROL_TIMEOUT_MS)) != pdTRUE) return ESP_ERR_TIMEOUT;
    SystemSettings defaults = {0};
    SystemSettings stored = {0};
    apply_defaults_all(&defaults);
    esp_err_t result = s_mgr.settings.restart_required ? ESP_ERR_INVALID_STATE : ESP_OK;
    for (size_t i = 0; result == ESP_OK && i < CONFIG_BLOCK_COUNT; ++i) {
        const ConfigBlockDef *block = &k_blocks[i];
        if (s_mgr.dirty[i] || !config_default_fields_equal(&s_mgr.settings, &defaults, block)) {
            result = ESP_ERR_INVALID_STATE;
            break;
        }
        nvs_handle_t handle;
        result = nvs_open(block->ns, NVS_READONLY, &handle);
        if (result != ESP_OK) break;
        size_t size = block->size;
        result = nvs_get_blob(handle, NS_CONFIG_KEY,
                              (uint8_t *)&stored + block->offset, &size);
        nvs_close(handle);
        if (result == ESP_OK && size != block->size) result = ESP_ERR_INVALID_SIZE;
        if (result == ESP_OK && !config_default_fields_equal(&stored, &defaults, block))
            result = ESP_ERR_INVALID_STATE;
    }
    if (result == ESP_OK) *output = s_mgr.settings;
    xSemaphoreGive(s_mgr.mutex);
    return result;
}

static esp_err_t config_get_snapshot(IConfigurationManager *self, SystemSettings *output) {
    if (!self || !output) return ESP_ERR_INVALID_ARG;
    if (!s_mgr.mutex || xSemaphoreTake(s_mgr.mutex, portMAX_DELAY) != pdTRUE)
        return ESP_ERR_INVALID_STATE;
    /* Preserve the owner's complete representation for snapshot invalidation;
     * struct assignment does not guarantee copying padding bytes. */
    memcpy(output, &s_mgr.settings, sizeof(*output));
    xSemaphoreGive(s_mgr.mutex);
    return ESP_OK;
}

static esp_err_t config_update_fields_impl(IConfigurationManager *self, const char *block_name,
                                      const ConfigurationFieldUpdate *fields, size_t count,
                                      bool persist, const void *const *expected_values) {
    if (!self || !block_name || !fields || count == 0 || count > CONFIG_UPDATE_MAX_FIELDS)
        return ESP_ERR_INVALID_ARG;
    size_t index;
    for (index = 0; index < CONFIG_BLOCK_COUNT; ++index)
        if (strcmp(block_name, k_blocks[index].name) == 0) break;
    if (index == CONFIG_BLOCK_COUNT) return ESP_ERR_INVALID_ARG;
    const ConfigBlockDef *block = &k_blocks[index];
    // The trailing schema version is owned by migration, never by field updates.
    size_t writable_size = block->size - sizeof(uint32_t);
    for (size_t i = 0; i < count; ++i) {
        if ((expected_values && !expected_values[i]) || !fields[i].data || fields[i].size == 0 || fields[i].offset >= writable_size ||
            fields[i].size > writable_size - fields[i].offset) return ESP_ERR_INVALID_ARG;
        bool recognized = false;
        for (size_t f = 0; f < sizeof(k_fields)/sizeof(k_fields[0]); ++f)
            if (k_fields[f].block_offset == block->offset && k_fields[f].offset == fields[i].offset &&
                k_fields[f].size == fields[i].size) { recognized = true; break; }
        if (!recognized) return ESP_ERR_INVALID_ARG;
        for (size_t j = 0; j < i; ++j)
            if (fields[i].offset < fields[j].offset + fields[j].size &&
                fields[j].offset < fields[i].offset + fields[i].size) return ESP_ERR_INVALID_ARG;
    }
    if (!s_mgr.mutex || xSemaphoreTake(s_mgr.mutex, portMAX_DELAY) != pdTRUE)
        return ESP_ERR_INVALID_STATE;
    if (!config_mutation_allowed(self)) { xSemaphoreGive(s_mgr.mutex); return ESP_ERR_NOT_FINISHED; }
    /* A preference toggle must not implicitly persist unrelated pending system edits. */
    if (persist && s_mgr.dirty[index] && block->offset == offsetof(SystemSettings, system)) {
        for (size_t i = 0; i < count; ++i) {
            if (fields[i].offset == offsetof(SystemBaseConfigs, developer_mode_enabled)) {
                xSemaphoreGive(s_mgr.mutex);
                return ESP_ERR_INVALID_STATE;
            }
        }
    }
    SystemSettings candidate = s_mgr.settings;
    uint8_t *updated = (uint8_t *)&candidate + block->offset;
    uint8_t *live = (uint8_t *)&s_mgr.settings + block->offset;
    if (expected_values) for (size_t i=0; i<count; ++i) {
        if (memcmp(live+fields[i].offset,expected_values[i],fields[i].size)!=0) {
            xSemaphoreGive(s_mgr.mutex);
            return ESP_ERR_INVALID_STATE;
        }
    }
    for (size_t i = 0; i < count; ++i)
        memcpy(updated + fields[i].offset, fields[i].data, fields[i].size);
    esp_err_t result = config_validate_candidate(candidate);
    bool changed = memcmp(updated, live, block->size) != 0;
    if (result == ESP_OK && persist && (changed || s_mgr.dirty[index]))
        result = nvs_save_block(block->ns, NS_CONFIG_KEY, updated, block->size);
    if (result == ESP_OK) {
        memcpy(live, updated, block->size);
        if (block->offset == offsetof(SystemSettings, system))
            DeveloperMode_SetEnabled(s_mgr.settings.system.developer_mode_enabled != 0);
        if (block->offset == offsetof(SystemSettings, localization)) apply_locale_selection();
        s_mgr.dirty[index] = persist ? false : (s_mgr.dirty[index] || changed);
    }
    xSemaphoreGive(s_mgr.mutex);
    if (result == ESP_OK && changed) publish_config_changed();
    return result;
}

static esp_err_t config_update_fields(IConfigurationManager *self, const char *block_name,
                                      const ConfigurationFieldUpdate *fields, size_t count, bool persist) {
    return config_update_fields_impl(self,block_name,fields,count,persist,NULL);
}
static esp_err_t config_update_fields_checked(IConfigurationManager *self, const char *block_name,
                                      const ConfigurationFieldUpdate *fields,const void *const *expected_values,
                                      size_t count,bool persist) {
    if (!expected_values) return ESP_ERR_INVALID_ARG;
    return config_update_fields_impl(self,block_name,fields,count,persist,expected_values);
}

/* =========================================================================
 * ResetToDefaults
 * ========================================================================= */

/**
 * @brief Resets one or all configuration blocks to compile-time defaults
 *        and immediately persists them to NVS.
 *
 * @param self       IConfigurationManager pointer (unused).
 * @param block_name Block to reset, or NULL for all blocks.
 * @return ESP_OK on success, ESP_FAIL on failure.
 */
static esp_err_t config_reset_to_defaults(IConfigurationManager *self, const char *block_name) {
    if (self != &s_mgr.base) return ESP_ERR_INVALID_ARG;
    bool matched = block_name == NULL;
    for (size_t i = 0; i < CONFIG_BLOCK_COUNT && !matched; ++i)
        matched = strcmp(block_name, k_blocks[i].name) == 0;
    if (!matched) return ESP_ERR_INVALID_ARG;
    if (!s_mgr.mutex || xSemaphoreTake(s_mgr.mutex, pdMS_TO_TICKS(CONFIG_CONTROL_TIMEOUT_MS)) != pdTRUE) return ESP_ERR_INVALID_STATE;
    if (!config_mutation_allowed(self)) { xSemaphoreGive(s_mgr.mutex); return ESP_ERR_NOT_FINISHED; }
    bool failed = false, applied = false;
    for (size_t i = 0; i < CONFIG_BLOCK_COUNT; ++i) {
        if (block_name && strcmp(block_name, k_blocks[i].name) != 0) continue;
        SystemSettings candidate = s_mgr.settings;
        k_blocks[i].apply_defaults(&candidate);
        const uint8_t *defaults = (const uint8_t *)&candidate + k_blocks[i].offset;
        uint8_t *live = (uint8_t *)&s_mgr.settings + k_blocks[i].offset;
        esp_err_t result = nvs_save_block(k_blocks[i].ns, NS_CONFIG_KEY, defaults, k_blocks[i].size);
        if (result != ESP_OK) {
            failed = true;
            ESP_LOGE(TAG, "Reset save failed for '%s': 0x%x", k_blocks[i].name, (unsigned)result);
            continue;
        }
        applied |= memcmp(live, defaults, k_blocks[i].size) != 0;
        memcpy(live, defaults, k_blocks[i].size);
        if (k_blocks[i].offset == offsetof(SystemSettings, system))
            DeveloperMode_SetEnabled(s_mgr.settings.system.developer_mode_enabled != 0);
        if (k_blocks[i].offset == offsetof(SystemSettings, localization)) apply_locale_selection();
        s_mgr.dirty[i] = false;
    }
    if (!block_name && !failed) {
        applied |= s_mgr.settings.restart_required;
        s_mgr.settings.restart_required = false;
    }
    xSemaphoreGive(s_mgr.mutex);
    if (applied) publish_config_changed();
    return failed ? ESP_FAIL : ESP_OK;
}

/* =========================================================================
 * GetVersion
 * ========================================================================= */

/**
 * @brief Returns the NVS schema version of a named configuration block.
 *
 * @param self       IConfigurationManager pointer (unused).
 * @param block_name Name of the block to query.
 * @return Version number, or 0 if block_name is unrecognised.
 */
static uint32_t config_get_version(IConfigurationManager *self, const char *block_name) {
    if (!block_name) return 0;
    ConfigurationStatus status;
    if (config_get_status(self, &status) != ESP_OK) return 0;
    for (size_t i = 0; i < status.block_count; ++i)
        if (strcmp(block_name, status.blocks[i].name) == 0) return status.blocks[i].version;
    return 0;
}

/* =========================================================================
 * IsDirty
 * ========================================================================= */

/**
 * @brief Returns true if the named block (or any block if NULL) has
 *        unsaved in-memory changes.
 *
 * @param self       IConfigurationManager pointer (unused).
 * @param block_name Named block to query, or NULL for any block.
 * @return true if dirty, false if all targeted blocks are clean.
 */
/* Compatibility only: false also represents an unavailable snapshot. */
static bool config_is_dirty(IConfigurationManager *self, const char *block_name) {
    ConfigurationStatus status;
    if (config_get_status(self, &status) != ESP_OK) return false;
    for (size_t i = 0; i < status.block_count; ++i)
        if ((!block_name || strcmp(block_name, status.blocks[i].name) == 0) && status.blocks[i].dirty) return true;
    return false;
}

/* =========================================================================
 * Initialize (IService)
 * ========================================================================= */

/**
 * @brief Initialises the ConfigurationManager service.
 *
 * Creates the internal mutex if not already created and transitions the
 * service to SERVICE_STATE_INITIALIZED.
 *
 * @param self     IService pointer (cast to ConfigurationManagerImpl).
 * @param registry IDIRegistry pointer (unused – settings use NVS directly).
 * @return 0 on success, -1 if mutex creation failed.
 */
static int32_t config_initialize(IService *self, IDIRegistry *registry) {
    (void)self; (void)registry;

    if (s_mgr.mutex == NULL) {
        s_mgr.mutex = xSemaphoreCreateMutex();
        if (s_mgr.mutex == NULL) {
            ESP_LOGE(TAG, "Failed to create mutex");
            return -1;
        }
    }
    if (!s_mgr.lifecycleMutex) s_mgr.lifecycleMutex = xSemaphoreCreateMutex();
    if (!s_mgr.lifecycleMutex || xSemaphoreTake(s_mgr.lifecycleMutex, pdMS_TO_TICKS(CONFIG_CONTROL_TIMEOUT_MS)) != pdTRUE) return -1;
    if (s_mgr.state == SERVICE_STATE_STARTED || s_mgr.state == SERVICE_STATE_ERROR) {
        xSemaphoreGive(s_mgr.lifecycleMutex); return -1;
    }
    s_mgr.state = SERVICE_STATE_INITIALIZED;
    xSemaphoreGive(s_mgr.lifecycleMutex);
    ESP_LOGI(TAG, "ConfigurationManager initialized");
    return 0;
}

/* =========================================================================
 * Singleton factory
 * ========================================================================= */

/**
 * @brief Returns the singleton IConfigurationManager instance.
 *
 * Call during startup before concurrent consumers. This factory allocates and
 * loads persisted settings, and must never run from ISR context.
 *
 * @return Singleton interface, or NULL when initial resource allocation fails.
 */
IConfigurationManager *ConfigurationManager_GetInstance(void) {
    if (s_initialized) {
        return &s_mgr.base;
    }

    if (!s_initialized) {
        /* Wire the IService vtable. */
        s_mgr.base.base.Initialize  = config_initialize;
        s_mgr.base.base.Start       = config_start;
        s_mgr.base.base.Stop        = config_stop;
        s_mgr.base.base.GetState    = config_get_state;
        s_mgr.base.base.CheckHealth = config_check_health;

        /* Wire the IConfigurationManager vtable. */
        s_mgr.base.Save              = config_save;
        s_mgr.base.SaveAsync         = config_save_async;
        s_mgr.base.Load              = config_load;
        s_mgr.base.GetSettings       = config_get_settings;
        s_mgr.base.UpdateSettings    = config_update_settings;
          s_mgr.base.GetSnapshot       = config_get_snapshot;
          s_mgr.base.GetDefaults       = config_get_defaults;
          s_mgr.base.VerifyDefaults    = config_verify_defaults;
          s_mgr.base.SetFactoryResetGuard = config_set_factory_reset_guard;
          s_mgr.base.IsMutationAllowed = config_mutation_allowed;
        s_mgr.base.GetStatus         = config_get_status;
        s_mgr.base.UpdateFields      = config_update_fields;
        s_mgr.base.UpdateFieldsIfUnchanged = config_update_fields_checked;
        s_mgr.base.IsRestartRequired = config_is_restart_required;
        s_mgr.base.ValidateSettings  = config_validate_settings;
        s_mgr.base.ResetToDefaults   = config_reset_to_defaults;
        s_mgr.base.GetVersion        = config_get_version;
        s_mgr.base.IsDirty           = config_is_dirty;

        /* Initialise dirty flags — all blocks are clean before first load. */
        for (size_t i = 0; i < CONFIG_BLOCK_COUNT; i++) {
            s_mgr.dirty[i] = false;
        }

        /* Create mutex once (Initialize() will also guard this). */
        if (s_mgr.mutex == NULL) {
            s_mgr.mutex = xSemaphoreCreateMutex();
        }

        if (config_initialize(&s_mgr.base.base, NULL) != 0) return NULL;

        /* Apply compile-time defaults before loading from NVS. */
        apply_defaults_all(&s_mgr.settings);

        /* Overlay with any previously persisted values. */
        esp_err_t loaded = config_load(&s_mgr.base, NULL);
        if (loaded != ESP_OK) {
            s_mgr.state = SERVICE_STATE_UNINITIALIZED;
            ESP_LOGE(TAG, "Initial configuration load failed: 0x%x", (unsigned)loaded);
            return NULL;
        }

        s_mgr.state = SERVICE_STATE_INITIALIZED;
        SystemConfigs_Print(&s_mgr.base);

        s_initialized = true; /* Publish — must be last. */
    }

    return &s_mgr.base;
}
