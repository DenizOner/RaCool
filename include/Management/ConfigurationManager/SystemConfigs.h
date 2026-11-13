/**
 * @file SystemConfigs.h
 * @brief System-wide configuration constants, NVS namespace definitions, and
 *        all configuration data structures for the RaCool IoT device.
 *
 * This file serves as the single source of truth for:
 *   - Hardware pin assignments (GPIO, I2C, etc.)
 *   - Default runtime values for all subsystems
 *   - NVS namespace strings for persistent storage
 *   - Event bus event name constants
 *   - Typed configuration structs (SystemSettings and sub-blocks)
 *   - Typed enumerations for configuration fields
 *   - Validation range bounds for all numeric fields
 *
 * All macros are prefixed by subsystem to avoid naming collisions.
 * Config structs carry a `version` field to support forward migration.
 *
 * @author RaCool Team
 * @date   2026
 */

#ifndef SYSTEM_CONFIGS_H
#define SYSTEM_CONFIGS_H

/** Maximum number of fields in one synchronous block update. */
#define CONFIG_UPDATE_MAX_FIELDS 16u
#define HARDWARE_CONFIG_SCHEMA_VERSION 8u

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "Hardware/RGBLED/RGBLEDConfig.h"
#include "Hardware/BuiltinLED/BuiltinLEDConfig.h"

/* =========================================================================
 * Hardware Pin Definitions (Static – not stored in NVS)
 * ========================================================================= */

/** @name Relay */
/** @{ */
#define RELAY_IN_PIN          0   /**< GPIO pin driving the relay input. */
#define RELAY_DEBOUNCE_MS     50  /**< Debounce delay for relay state changes (ms). */
#define RELAY_MIN_SWITCH_MS   1000 /**< Minimum interval between relay toggles (ms). */
/** @} */

/** @name ClimateController Task & Watchdog */
/** @{ */
#define CLIMATE_CTRL_WATCHDOG_TIMEOUT_MS  5000  /**< Task watchdog timeout (ms). */
#define CLIMATE_SENSOR_STALE_TIMEOUT_MS   10000 /**< IR reading is 'stale' after this duration (ms). */
#define CLIMATE_HISTORY_BUFFER_SIZE       10    /**< Temperature samples kept in the ring buffer. */
/** @} */

/** @name MLX90614 Infrared Temperature Sensor */
/** @{ */
#define MLX90614_I2C_PORT   0       /**< I2C peripheral number (I2C_NUM_0). */
#define MLX90614_SCL_PIN    7       /**< I2C clock GPIO. */
#define MLX90614_SDA_PIN    6       /**< I2C data GPIO. */
#define MLX90614_I2C_FREQ   100000  /**< I2C bus frequency (Hz). */
/** @} */

/** @name Rotary Encoder (KY-040) */
/** @{ */
#define ROTARY_CLK_PIN  4  /**< Encoder CLK (A) GPIO. */
#define ROTARY_DT_PIN   5  /**< Encoder DT  (B) GPIO. */
#define ROTARY_SW_PIN   3  /**< Encoder push-button GPIO. */
/** @} */

/** @name SSD1306 OLED Display (128x64) */
/** @{ */
#define OLED_I2C_PORT   0       /**< I2C peripheral number (shared with MLX90614). */
#define OLED_SCL_PIN    7       /**< I2C clock GPIO. */
#define OLED_SDA_PIN    6       /**< I2C data GPIO. */
#define OLED_I2C_FREQ   100000  /**< I2C bus frequency (Hz). */
/** @} */

/** @name External RGB LED (4-pin, common-cathode) */
/** @{ */
#define RGB_RED_PIN    2   /**< Red channel GPIO (LEDC). */
#define RGB_GREEN_PIN  10  /**< Green channel GPIO (LEDC). */
#define RGB_BLUE_PIN   11  /**< Blue channel GPIO (LEDC). */
/** @} */

/** @name Built-in LED */
/** @{ */
#define BUILTIN_LED_PIN  8  /**< ESP32-C6 on-board LED GPIO. */
/** @} */

/* =========================================================================
 * System Configuration Defaults (Runtime – stored in NVS)
 * ========================================================================= */

/** @name System Base Defaults */
/** @{ */
#define DEFAULT_SYSTEM_ENABLED         true   /**< Global system on/off switch. */
#define DEFAULT_TEMP_UNIT              0      /**< 0 = Celsius, 1 = Fahrenheit. */
#define DEFAULT_TELEMETRY_INTERVAL_MS  20000  /**< Zigbee telemetry interval (ms). */
#define DEFAULT_OLED_TIMEOUT_SEC       20     /**< OLED auto-off timeout (s). */
#define SYSTEM_CONFIG_SCHEMA_VERSION   5u
#define DEFAULT_DEVELOPER_MODE_ENABLED 0u
#define DEFAULT_MENU_AUTO_EXIT_ENABLED 1u
#define DEFAULT_DISPLAY_MODE           1      /**< 1=auto (sleep on timeout), 2=always on. */
#define SYSTEM_DISPLAY_MODE_COUNT      3U
static inline bool SystemConfigs_IsValidDisplayMode(uint8_t mode) {
    return mode == 1U || mode == 2U;
}
#define BUILTIN_LED_MODE_COUNT         2U     /**< 0=off, 1=automatic. */
#define DEFAULT_POWER_PROFILE          1      /**< Balanced DFS; router remains awake. */
#define POWER_PROFILE_COUNT            3
#define DEFAULT_ZIGBEE_TX_PROFILE      1      /**< Normal: retain the existing 10 dBm setting. */
#define ZIGBEE_TX_PROFILE_COUNT        3
/** @} */

/** @name Control Defaults */
/** @{ */
#define DEFAULT_THRESHOLD_HIGH     35.0f  /**< High temperature alert threshold (°C). */
#define DEFAULT_THRESHOLD_LOW      25.0f  /**< Low temperature alert threshold (°C). */
#define DEFAULT_RELAY_OPEN_TEMP    30.0f  /**< Fan relay opens above this temp (°C). */
#define DEFAULT_RELAY_CLOSE_TEMP   28.0f  /**< Fan relay closes below this temp (°C). */
#define DEFAULT_FAN_MANUAL_MODE    false  /**< false = automatic control. */
#define DEFAULT_FAN_MANUAL_STATE   false  /**< Manual override state (on/off). */
/** @} */

/** @name Hardware / LED Defaults */
/** @{ */
#define DEFAULT_BUILTIN_LED_STATE      false  /**< Built-in LED off on boot. */
#define DEFAULT_RGB_RED_STATE          false  /**< RGB red channel off on boot. */
#define DEFAULT_RGB_GREEN_STATE        false  /**< RGB green channel off on boot. */
#define DEFAULT_RGB_BLUE_STATE         false  /**< RGB blue channel off on boot. */
#define DEFAULT_BUILTIN_LED_BRIGHTNESS 255    /**< Built-in LED brightness (maximum). */
#define DEFAULT_EXT_LED_BRIGHTNESS     255    /**< RGB LED brightness (maximum). */

/** @name RGB Temperature Zone Defaults */
/** @{ */
#define DEFAULT_EXT_LED_TEMP_COLD_MIN      17.0f
#define DEFAULT_EXT_LED_TEMP_COLD_MAX      19.0f
#define DEFAULT_EXT_LED_TEMP_WARM_MIN      35.0f
#define DEFAULT_EXT_LED_TEMP_WARM_MAX      37.0f
#define DEFAULT_EXT_LED_TEMP_HOT_MIN       50.0f
#define DEFAULT_EXT_LED_TEMP_HOT_MAX       52.0f
#define DEFAULT_EXT_LED_TEMP_DANGER        60.0f
#define DEFAULT_EXT_LED_TEMP_FREEZE         4.0f
/** @} */

/** @name Default Thermal Zone Colors (CIE 1931 XY, ZCL format 0-65279)
 *  Used by ConfigurationManager for NVS defaults and migration.
 */
/** @{ */
#define DEFAULT_EXT_LED_COLOR_COLD_X     RGBLED_PRESET_BLUE_X  /**< Blue CIE X. */
#define DEFAULT_EXT_LED_COLOR_COLD_Y     RGBLED_PRESET_BLUE_Y  /**< Blue CIE Y. */
#define DEFAULT_EXT_LED_COLOR_WARM_X     RGBLED_PRESET_GREEN_X  /**< Green CIE X. */
#define DEFAULT_EXT_LED_COLOR_WARM_Y     RGBLED_PRESET_GREEN_Y  /**< Green CIE Y. */
#define DEFAULT_EXT_LED_COLOR_HOT_X      RGBLED_PRESET_RED_X  /**< Red CIE X. */
#define DEFAULT_EXT_LED_COLOR_HOT_Y      RGBLED_PRESET_RED_Y  /**< Red CIE Y. */
#define DEFAULT_EXT_LED_COLOR_DANGER_X   RGBLED_PRESET_PURPLE_X  /**< Purple CIE X. */
#define DEFAULT_EXT_LED_COLOR_DANGER_Y   RGBLED_PRESET_PURPLE_Y  /**< Purple CIE Y. */
#define DEFAULT_EXT_LED_COLOR_FREEZE_X   RGBLED_PRESET_WHITE_X  /**< White CIE X. */
#define DEFAULT_EXT_LED_COLOR_FREEZE_Y   RGBLED_PRESET_WHITE_Y  /**< White CIE Y. */
/** @} */

/**
 * @name External LED Operating Modes
 * These constants are independent of RGBLEDColorMode enum values and must
 * be used consistently by ApplySettings, UIController, and MenuPlugin.
 */
/** @{ */
#define EXT_LED_MODE_TEMPERATURE  0  /**< Automatic temperature-based color. */
#define EXT_LED_MODE_ANIMATION    1  /**< Animation playback mode. */
#define EXT_LED_MODE_OFF          2  /**< LED disabled. */
#define EXT_LED_MODE_COUNT        3  /**< Total number of modes. */

#define DEFAULT_EXT_LED_MODE  EXT_LED_MODE_TEMPERATURE  /**< Default: temperature mode. */
#define DEFAULT_EXT_LED_ANIM  0                         /**< RGBLED_ANIM_NONE. */
/** @} */

/** @} */ /* end Hardware/LED Defaults */

/** @name Built-in LED Condition/Action Constants */
/** @{ */
#define BUILTIN_LED_COND_FAULT       0  /**< System fault condition. */
#define BUILTIN_LED_COND_CONNECTING  1  /**< Zigbee connecting condition. */
#define BUILTIN_LED_COND_HEATING     2  /**< Fan/relay active condition. */
#define BUILTIN_LED_COND_IDLE        3  /**< System idle condition. */
#define BUILTIN_LED_COND_COUNT       4  /**< Total number of conditions. */

#define BUILTIN_LED_ACT_OFF      0  /**< LED off. */
#define BUILTIN_LED_ACT_SOLID    1  /**< LED solid on. */
#define BUILTIN_LED_ACT_BLINK    2  /**< LED blinking. */
#define BUILTIN_LED_ACT_BREATHE  3  /**< LED breathing (PWM fade). */
#define BUILTIN_LED_ACT_COUNT    4  /**< Total number of actions. */

#define DEFAULT_BUILTIN_LED_IDLE_ANIM        BUILTIN_LED_ACT_BREATHE  /**< Idle: breathing. */
#define DEFAULT_BUILTIN_LED_HEATING_ANIM     BUILTIN_LED_ACT_SOLID    /**< Heating: solid. */
#define DEFAULT_BUILTIN_LED_CONNECTING_ANIM  BUILTIN_LED_ACT_BLINK    /**< Connecting: blink. */
#define DEFAULT_BUILTIN_LED_FAULT_ANIM       BUILTIN_LED_ACT_BLINK    /**< Fault: blink. */
/** @} */

/** @name Calibration Defaults */
/** @{ */
#define DEFAULT_IR_OBJECT_CAL   0.0f  /**< Object temperature calibration offset (°C). */
#define DEFAULT_IR_AMBIENT_CAL  0.0f  /**< Ambient temperature calibration offset (°C). */
#define CALIBRATION_MIN        -9.99f  /**< Minimum allowed calibration offset (°C). */
#define CALIBRATION_MAX         9.99f  /**< Maximum allowed calibration offset (°C). */
/** @} */

/** @name Internal Thermal Defaults */
/** @{ */
#define DEFAULT_CPU_TEMP_WARNING    80.0f   /**< CPU warning threshold (°C). */
#define DEFAULT_CPU_TEMP_CRITICAL   100.0f  /**< CPU critical threshold (°C). */
#define DEFAULT_CPU_TEMP_EMERGENCY  115.0f  /**< CPU emergency shutdown threshold (°C). */
#define DEFAULT_CPU_TEMP_POLLING_MS 5000    /**< CPU temperature polling interval (ms). */
/** @} */

/** @name Zigbee Reporting Interval Defaults (seconds) */
/** @{ */
#define DEFAULT_ZB_REPORT_FAN_S     3   /**< Radiator surface temperature report interval. */
#define DEFAULT_ZB_REPORT_AMBIENT_S 20  /**< Ambient temperature report interval. */
#define DEFAULT_ZB_REPORT_CPU_S     30  /**< CPU temperature report interval. */
#define DEFAULT_ZB_REPORT_HEALTH_S  180 /**< Health report interval. */
#define DEFAULT_ZB_REPORT_MEMORY_S  360 /**< Memory report interval. */
#define DEFAULT_ZB_REPORT_UPTIME_S  600 /**< Uptime report interval. */
/** @} */

/* =========================================================================
 * Validation Bounds (enforced by IConfigurationManager::ValidateSettings)
 * ========================================================================= */

/** @name System Base Validation Bounds */
/** @{ */
#define OLED_TIMEOUT_MIN_SEC    5u      /**< Minimum OLED auto-off timeout (s). */
#define OLED_TIMEOUT_MAX_SEC    3600u   /**< Maximum OLED auto-off timeout (s — 1 hour). */
#define OLED_SLEEP_ZB_MIN_SEC   5u      /**< Minimum Zigbee-configurable sleep timeout (s). */
#define OLED_SLEEP_ZB_MAX_SEC   60u     /**< Maximum Zigbee-configurable sleep timeout (s). */
#define DEFAULT_ENCODER_STEPS_PER_DETENT 4u
#define DEFAULT_ENCODER_ACCEL_ENABLED 1u
#define DEFAULT_ENCODER_ACCEL_THRESHOLD_MS 100u
#define DEFAULT_ENCODER_INVERT_DIRECTION 0u

#define TELEMETRY_MIN_MS        0u      /**< Zero disables periodic local telemetry. */
#define TELEMETRY_ACTIVE_MIN_MS 1000u   /**< Minimum enabled telemetry interval (ms). */
#define TELEMETRY_MAX_MS        600000u /**< Maximum telemetry interval (ms — 10 min). */
/** @} */

/** @name Zigbee Reporting Validation Bounds */
/** @{ */
#define ZB_REPORT_MIN_S   0u     /**< Zero disables unsolicited Zigbee reports. */
#define ZB_REPORT_MAX_S   3600u  /**< Maximum Zigbee reporting interval (s — 1 hour). */
/** @} */

/** @name Thermal Polling Validation Bounds */
/** @{ */
#define CPU_POLL_MIN_MS   500u    /**< Minimum CPU temperature polling interval (ms). */
#define CPU_POLL_MAX_MS   60000u  /**< Maximum CPU temperature polling interval (ms). */
/** @} */

/** @name Temperature Threshold Validation Bounds */
/** @{ */
#define FAN_SETPOINT_MIN_C 20.0f /**< Minimum configurable fan setpoint. */
#define FAN_SETPOINT_MAX_C 99.0f /**< Maximum configurable fan setpoint. */
#define FAN_DELTA_MIN_DECIC -90
#define FAN_DELTA_MAX_DECIC 90
#define DEFAULT_FAN_DELTA_ON_DECIC 20
#define DEFAULT_FAN_DELTA_OFF_DECIC 10
#define DEFAULT_FAN_STRATEGY 0u
#define DEFAULT_OBJECT_TEMPERATURE_SOURCE 0u
#define DEFAULT_AMBIENT_TEMPERATURE_SOURCE 0u
#define TEMPERATURE_SOURCE_COUNT 4u
#define FAN_STRATEGY_COUNT 2u
#define EXTERNAL_TEMPERATURE_HEARTBEAT_MS 300000u
#define EXTERNAL_TEMPERATURE_STALE_MS 300000u
#define EXTERNAL_TEMPERATURE_MIN_C -50.0f
#define EXTERNAL_TEMPERATURE_MAX_C 200.0f

/** Validate both fan setpoints, including non-finite input and hysteresis. */
static inline bool SystemConfigs_IsFanSetpointPairValid(float open, float close) {
    return open >= FAN_SETPOINT_MIN_C && open <= FAN_SETPOINT_MAX_C &&
           close >= FAN_SETPOINT_MIN_C && close <= FAN_SETPOINT_MAX_C && open > close;
}

#define TEMP_THRESHOLD_MIN   -50.0f /**< Minimum valid temperature threshold (°C). */
#define TEMP_THRESHOLD_MAX   200.0f /**< Maximum valid temperature threshold (°C). */
#define RELAY_HYSTERESIS_MIN   0.5f /**< Minimum required hysteresis (open − close) (°C). */
#define RGB_COLD_MIN_LIMIT      7.0f /**< Cold zone lower bound (°C). */
#define RGB_COLD_MAX_LIMIT     25.0f /**< Cold zone upper bound (°C). */
#define RGB_WARM_MIN_LIMIT     26.0f /**< Warm zone lower bound (°C). */
#define RGB_WARM_MAX_LIMIT     44.0f /**< Warm zone upper bound (°C). */
#define RGB_HOT_MIN_LIMIT      45.0f /**< Hot zone lower bound (°C). */
#define RGB_HOT_MAX_LIMIT      59.0f /**< Hot zone upper bound (°C). */
#define RGB_DANGER_MIN_LIMIT   60.0f /**< Danger threshold lower bound (°C). */
#define RGB_DANGER_MAX_LIMIT   99.0f /**< Danger threshold upper bound (°C). */
#define RGB_FREEZE_MIN_LIMIT  -18.0f /**< Freeze threshold lower bound (°C). */
#define RGB_FREEZE_MAX_LIMIT    6.0f /**< Freeze threshold upper bound (°C). */
/** @} */

/* =========================================================================
 * Typed Enumerations for Configuration Fields
 * ========================================================================= */

/**
 * @brief Temperature display unit.
 *
 * Stored as uint8_t in SystemBaseConfigs.temp_unit for NVS binary-blob
 * compatibility.  Values match ZCL hvacThermostatUiCfg tempDisplayMode.
 */
typedef enum {
    TEMP_UNIT_CELSIUS    = 0, /**< Display in degrees Celsius. */
    TEMP_UNIT_FAHRENHEIT = 1, /**< Display in degrees Fahrenheit. */
    TEMP_UNIT_COUNT      = 2  /**< Number of valid values (sentinel). */
} TempUnit;

/**
 * @brief Built-in LED animation action.
 *
 * Stored as uint8_t in HardwareConfigs animation fields for NVS
 * compatibility.  Values equal the BUILTIN_LED_ACT_* macros.
 * Use BUILTIN_LED_ACT_COUNT (macro) for range checks; do NOT add a
 * sentinel enum value here to avoid clashing with BuiltinLEDAnimationType
 * in BuiltinLEDConfig.h which also defines BUILTIN_LED_ANIM_COUNT.
 */
typedef enum {
    BUILTIN_LED_ANIM_OFF     = BUILTIN_LED_ACT_OFF,     /**< LED off (0). */
    BUILTIN_LED_ANIM_SOLID   = BUILTIN_LED_ACT_SOLID,   /**< LED solid on (1). */
    BUILTIN_LED_ANIM_BLINK   = BUILTIN_LED_ACT_BLINK,   /**< LED blinking (2). */
    BUILTIN_LED_ANIM_BREATHE = BUILTIN_LED_ACT_BREATHE  /**< LED breathing / PWM fade (3). */
} BuiltinLEDAnimAction;

/**
 * @brief External RGB LED operating mode.
 *
 * Stored as uint8_t in HardwareConfigs.ext_led_mode for NVS compatibility.
 * Values equal the EXT_LED_MODE_* macros.
 */
typedef enum {
    EXT_LED_OP_TEMPERATURE = EXT_LED_MODE_TEMPERATURE, /**< Temperature-based colour (0). */
    EXT_LED_OP_ANIMATION   = EXT_LED_MODE_ANIMATION,   /**< Animation playback (1). */
    EXT_LED_OP_OFF         = EXT_LED_MODE_OFF,          /**< LED disabled (2). */
    EXT_LED_OP_COUNT       = EXT_LED_MODE_COUNT         /**< Sentinel value (3). */
} ExtLEDMode;


/* =========================================================================
 * UI & Application Loop Constants
 * ========================================================================= */
#define APP_MAIN_LOOP_DELAY_MS       1000  /**< Main application loop delay (ms). */
#define UI_TEMP_REFRESH_HYSTERESIS   0.05f /**< Min temp delta to trigger UI refresh (°C). */
#define UI_HEALTH_CHECK_INTERVAL_MS  5000  /**< UI health polling interval (ms). */
#define UI_REFRESH_INTERVAL_MS       100   /**< OLED refresh interval (ms). */
#define UI_DEFAULT_SCREEN_TIMEOUT_SEC 60   /**< Default OLED screen timeout (s). */
#define BUILTIN_LED_ANIM_INTERVAL_MS 50    /**< Built-in LED animation tick interval (ms). */

/* =========================================================================
 * FreeRTOS Task Configuration
 * ========================================================================= */
#define UI_TASK_STACK_SIZE            8192  /**< UI controller task stack size (bytes). */
#define UI_TASK_PRIORITY              5     /**< UI controller task priority. */
#define CLIMATE_CTRL_TASK_STACK_SIZE  4096  /**< Climate controller task stack size (bytes). */
#define CLIMATE_CTRL_TASK_PRIORITY    5     /**< Climate controller task priority. */
#define SYSTEM_CTRL_TASK_STACK_SIZE   4096  /**< System controller task stack size (bytes). */
#define SYSTEM_CTRL_TASK_PRIORITY     5     /**< System controller task priority. */

/* =========================================================================
 * Event Bus Event Name Constants (Single Source of Truth)
 * ========================================================================= */
#define EVENT_CONFIG_CHANGED         "config.changed"       /**< Config block was updated. */
#define EVENT_CONFIG_SAVE_REQ        "config.save_req"      /**< Async save requested. */
#define EVENT_CONFIG_RESTART_REQ     "config.restart_req"   /**< Restart required after config. */
#define EVENT_INTERNAL_TEMP_UPDATED  "hw.int_temp.updated"  /**< CPU temp measurement ready. */
#define EVENT_IR_TEMP_UPDATED        "hw.ir_temp.updated"   /**< IR sensor measurement ready. */
#define EVENT_RELAY_STATE_CHANGED    "hw.relay.changed"     /**< Relay state toggled. */
#define EVENT_SYSTEM_THERMAL_ALERT   "sys.thermal.alert"    /**< Thermal threshold exceeded. */
#define EVENT_CLIMATE_SENSOR_STALE   "climate.sensor.stale" /**< IR reading is stale. */

/* =========================================================================
 * NVS Namespace Strings (max 15 chars per ESP-IDF restriction)
 * ========================================================================= */
#define NS_CONFIG_SYSTEM   "conf_sys"    /**< SystemBaseConfigs namespace. */
#define NS_CONFIG_CONTROL  "conf_ctrl"   /**< ControlConfigs namespace. */
#define NS_CONFIG_HARDWARE "conf_hw"     /**< HardwareConfigs namespace. */
#define NS_CONFIG_CALIB    "conf_cal"    /**< CalibrationConfigs namespace. */
#define NS_CONFIG_THERMAL  "conf_therm"  /**< ThermalConfigs namespace. */
#define NS_CONFIG_ZIGBEE   "conf_zb"     /**< ZigbeeReportingConfigs namespace. */
#define NS_CONFIG_LOCALIZATION "conf_locale" /**< LocalizationConfigs namespace. */
#define LOCALIZATION_LOCALE_CAPACITY 8
#define LOCALIZATION_SCHEMA_VERSION 1U
#define DEFAULT_LOCALIZATION_LOCALE "tr"

/** @brief NVS blob key used for all config blocks (single key per namespace). */
#define NS_CONFIG_KEY      "data"

/* =========================================================================
 * Configuration Data Structures
 * ========================================================================= */

/**
 * @brief Core system operating parameters.
 *
 * Stored in NVS namespace @ref NS_CONFIG_SYSTEM.
 */
typedef struct {
    bool     system_enabled;          /**< Global system on/off switch. */
    uint8_t  temp_unit;               /**< 0 = Celsius, 1 = Fahrenheit. */
    uint8_t  reserved_system_padding; /**< Reserved; preserves legacy layout. */
    uint8_t  developer_mode_enabled;  /**< Persistent Developer Tools preference. */
    uint32_t telemetry_interval_ms;   /**< Zigbee attribute reporting interval (ms). */
    uint32_t oled_timeout_sec;        /**< OLED screen auto-off timeout (s). */
    uint8_t  display_mode;            /**< 1=auto (sleep on inactivity), 2=always on. */
    uint8_t  power_profile;           /**< 0=performance, 1=balanced, 2=low power. */
    uint8_t  zigbee_tx_profile;       /**< 0=Eco, 1=Normal, 2=Boost; independent of CPU/sleep. */
    uint8_t  menu_auto_exit_enabled;  /**< 0=keep menu open, 1=return home after inactivity. */
    uint32_t version;                 /**< Schema version for NVS migration. */
} SystemBaseConfigs;

/**
 * @brief Zigbee attribute reporting intervals.
 *
 * Stored in NVS namespace @ref NS_CONFIG_ZIGBEE.
 * All fields are in seconds.
 */
typedef struct {
    uint16_t fan_s;      /**< Fan state reporting interval (s). */
    uint16_t ambient_s;  /**< Ambient temperature reporting interval (s). */
    uint16_t cpu_s;      /**< CPU temperature reporting interval (s). */
    uint16_t health_s;   /**< Health status reporting interval (s). */
    uint16_t memory_s;   /**< Memory usage reporting interval (s). */
    uint16_t uptime_s;   /**< Device uptime reporting interval (s). */
    uint32_t version;    /**< Schema version for NVS migration. */
} ZigbeeReportingConfigs;

/**
 * @brief Climate control thresholds and fan operating mode.
 *
 * Stored in NVS namespace @ref NS_CONFIG_CONTROL.
 */
typedef struct {
    float    threshold_high;     /**< High temperature alert threshold (°C). */
    float    threshold_low;      /**< Low temperature alert threshold (°C). */
    float    relay_open_temp;    /**< Relay opens (fan on) above this (°C). */
    float    relay_close_temp;   /**< Relay closes (fan off) below this (°C). */
    bool     fan_manual_mode;    /**< true = manual override, false = auto. */
    bool     fan_manual_state;   /**< Manual state: true = on, false = off. */
    uint8_t  fan_strategy;       /**< 0=object_sensitive, 1=ambient_delta. */
    uint8_t  object_temperature_source; /**< 0=internal, 1..3=external slot. */
    uint8_t  ambient_temperature_source;
    uint8_t  reserved;
    int16_t  fan_delta_on_decic;  /**< Signed tenths of a Celsius degree. */
    int16_t  fan_delta_off_decic;
    uint16_t reserved_tail;
    uint32_t version;            /**< Schema version for NVS migration. */
} ControlConfigs;

static inline bool SystemConfigs_IsFanSourceConfigValid(const ControlConfigs *c) {
    return c && c->fan_strategy < FAN_STRATEGY_COUNT &&
        c->object_temperature_source < TEMPERATURE_SOURCE_COUNT &&
        c->ambient_temperature_source < TEMPERATURE_SOURCE_COUNT &&
        c->fan_delta_on_decic >= FAN_DELTA_MIN_DECIC && c->fan_delta_on_decic <= FAN_DELTA_MAX_DECIC &&
        c->fan_delta_off_decic >= FAN_DELTA_MIN_DECIC && c->fan_delta_off_decic <= FAN_DELTA_MAX_DECIC &&
        c->fan_delta_on_decic > c->fan_delta_off_decic;
}

/**
 * @brief LED and indicator hardware configuration.
 *
 * Covers both the built-in single LED and the external 4-pin RGB LED.
 * Stored in NVS namespace @ref NS_CONFIG_HARDWARE.
 *
 * Animation fields use shared LEDAnimationCatalog IDs stored as uint8_t
 * for NVS binary-blob compatibility. The ext_led_mode field uses
 * @ref ExtLEDMode / @ref EXT_LED_MODE_* values, also stored as uint8_t.
 */
typedef struct {
    bool    builtin_led_state;           /**< Built-in LED on/off. */
    uint8_t builtin_led_anim_mode;       /**< 0 = off, 1 = automatic condition-driven mode. */
    uint8_t builtin_led_startup_anim;    /**< Independent boot animation; v8 reuses the reserved byte. */
    uint8_t builtin_led_idle_anim;       /**< Animation ID when system is idle. */
    uint8_t builtin_led_connecting_anim; /**< Animation ID while Zigbee connects. */
    uint8_t builtin_led_fault_anim;      /**< Animation ID on system fault. */
    bool    rgb_red_state;               /**< RGB red channel manual state. */
    bool    rgb_green_state;             /**< RGB green channel manual state. */
    bool    rgb_blue_state;              /**< RGB blue channel manual state. */
    uint8_t ext_led_brightness;          /**< RGB LED global brightness (0–255). */
    float   ext_led_temp_cold_min;       /**< Cold zone lower boundary (°C). */
    float   ext_led_temp_cold_max;       /**< Cold zone upper boundary (°C). */
    float   ext_led_temp_warm_min;       /**< Warm zone lower boundary (°C). */
    float   ext_led_temp_warm_max;       /**< Warm zone upper boundary (°C). */
    float   ext_led_temp_hot_min;        /**< Hot zone lower boundary (°C). */
    float   ext_led_temp_hot_max;        /**< Hot zone upper boundary (°C). */
    float   ext_led_temp_danger;         /**< Danger threshold (°C). */
    float   ext_led_temp_freeze;         /**< Freeze threshold (°C). */
    /**
     * Thermal zone colors as CIE 1931 XY (ZCL currentX/currentY, range 0-65279).
     * Schema v2: replaces uint8 preset index. NVS migration resets to defaults.
    * Defaults: cold=blue(0x1300,0x1200) warm=green(0x1900,0x6C00) hot=red(0xB000,0x3000).
     */
    uint16_t ext_led_color_cold_x;      /**< Cold zone color CIE X (0-65279). */
    uint16_t ext_led_color_cold_y;      /**< Cold zone color CIE Y (0-65279). */
    uint16_t ext_led_color_warm_x;      /**< Warm zone color CIE X. */
    uint16_t ext_led_color_warm_y;      /**< Warm zone color CIE Y. */
    uint16_t ext_led_color_hot_x;       /**< Hot zone color CIE X. */
    uint16_t ext_led_color_hot_y;       /**< Hot zone color CIE Y. */
    uint16_t ext_led_color_danger_x;    /**< Danger zone color CIE X. */
    uint16_t ext_led_color_danger_y;    /**< Danger zone color CIE Y. */
    uint16_t ext_led_color_freeze_x;    /**< Freeze zone color CIE X. */
    uint16_t ext_led_color_freeze_y;    /**< Freeze zone color CIE Y. */
    uint8_t ext_led_mode;                /**< LED operating mode; see ExtLEDMode / EXT_LED_MODE_*. */
    uint8_t ext_led_anim;                /**< Animation index (for animation mode). */
    uint8_t builtin_led_brightness;      /**< Global brightness, 0–255; schema v6 uses former padding. */
    uint8_t encoder_steps_per_detent;
    uint8_t encoder_accel_enabled;
    uint8_t encoder_accel_threshold_ms;
    uint8_t encoder_invert_direction;
    uint32_t version;                    /**< Schema version for NVS migration. */
} HardwareConfigs;

/** Validate finite, ordered thermal zones against their permitted ranges. */
bool SystemConfigs_ValidateRGBThermal(const HardwareConfigs *hardware);

enum { RGB_ZONE_COLD, RGB_ZONE_WARM, RGB_ZONE_HOT, RGB_ZONE_DANGER, RGB_ZONE_FREEZE, RGB_ZONE_COUNT };
#define RGB_XY_MAX_COORDINATE 65279u
#define RGB_XY_OLED_STEP 32
bool SystemConfigs_RGBZoneOffsets(unsigned zone, size_t *x, size_t *y);
bool SystemConfigs_GetRGBZoneXY(const HardwareConfigs *hardware, unsigned zone, uint16_t *x, uint16_t *y);
bool SystemConfigs_ValidateRGBXY(const HardwareConfigs *hardware);
bool SystemConfigs_ValidateRGBCoordinates(uint16_t x, uint16_t y);

/**
 * @brief Temperature sensor calibration offsets.
 *
 * Stored in NVS namespace @ref NS_CONFIG_CALIB.
 * Values must be within [@ref CALIBRATION_MIN, @ref CALIBRATION_MAX].
 */
typedef struct {
    float    ir_object_cal;   /**< Object temperature calibration offset (°C). */
    float    ir_ambient_cal;  /**< Ambient temperature calibration offset (°C). */
    uint32_t version;         /**< Schema version for NVS migration. */
} CalibrationConfigs;

/**
 * @brief CPU/internal thermal protection thresholds and polling rate.
 *
 * Stored in NVS namespace @ref NS_CONFIG_THERMAL.
 */
typedef struct {
    float    cpu_temp_warning;         /**< Warning threshold (°C) – log only. */
    float    cpu_temp_critical;        /**< Critical threshold (°C) – throttle. */
    float    cpu_temp_emergency;       /**< Emergency threshold (°C) – shutdown. */
    uint32_t cpu_polling_interval_ms;  /**< Sensor polling interval (ms). */
    uint32_t version;                  /**< Schema version for NVS migration. */
} ThermalConfigs;

/** Persisted product-language selection in its own versioned NVS block. */
typedef struct {
    char locale[LOCALIZATION_LOCALE_CAPACITY]; /**< Stable locale code, not catalog index. */
    uint32_t version;                         /**< Independent schema version. */
} LocalizationConfigs;

/** Validate storage syntax; a removed catalog remains a valid persisted code. */
bool SystemConfigs_IsLocalizationValid(const LocalizationConfigs *configuration);

/** Aggregate settings owned exclusively by ConfigurationManager.
 * Other modules obtain owned copies through GetSnapshot. */
typedef struct {
    SystemBaseConfigs      system;            /**< Core system parameters. */
    ZigbeeReportingConfigs zigbee_reporting;  /**< Zigbee reporting intervals. */
    ControlConfigs         control;           /**< Climate control settings. */
    HardwareConfigs        hardware;          /**< LED and indicator settings. */
    CalibrationConfigs     calibration;       /**< Sensor calibration offsets. */
    ThermalConfigs         thermal;           /**< Thermal protection settings. */
    LocalizationConfigs    localization;      /**< Product OLED/CLI language. */
    bool                   restart_required;  /**< Set when a restart is needed. */
} SystemSettings;

/* =========================================================================
 * Utility Functions
 * ========================================================================= */

/** Forward declaration required by SystemConfigs_Print signature. */
struct IConfigurationManager;

/**
 * @brief Prints all current system settings to the log output.
 *
 * Retrieves an owned SystemSettings snapshot from the provided manager and formats
 * every field to the ESP-IDF logging facility. Also prints static pin
 * definitions for board verification.
 *
 * @param configMgr Pointer to the active IConfigurationManager instance.
 * @return ESP_OK after formatting, or the argument/snapshot error.
 */
esp_err_t SystemConfigs_Print(struct IConfigurationManager *configMgr);

#endif /* SYSTEM_CONFIGS_H */
