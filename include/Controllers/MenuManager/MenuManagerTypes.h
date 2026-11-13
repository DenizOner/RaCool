#ifndef MENU_MANAGER_TYPES_H
#define MENU_MANAGER_TYPES_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Menu states and advanced settings
 */
typedef enum {
  UI_STATE_MONITOR = 0,
  UI_STATE_MENU_ROOT,           // Menu entry sentinel
  // Main Categories (Parents)
  UI_STATE_CAT_TEMP,           // Stable legacy ID for the Fan root.
  UI_STATE_CAT_LIGHT,          // Lighting root.
  UI_STATE_CAT_DEVICE,         // Stable legacy ID for the Preferences root.
  UI_STATE_CAT_POWER,          // Stable legacy ID for the System root.
  UI_STATE_EXIT,               // exit
  
  // Temp Submenu Group
  UI_STATE_SET_LOW_TEMP,       // set low temp
  UI_STATE_SET_HIGH_TEMP,      // set high temp
  UI_STATE_SET_TEMP_UNIT,      // set temp unit
  UI_STATE_FAN_MODE,           // fan mode
  UI_STATE_CALIBRATE_OBJECT,   // calibrate object
  UI_STATE_CALIBRATE_AMBIENT,  // calibrate ambient
  
  // Light Submenu Group
  UI_STATE_SET_LED_MODE,       // set led mode
  UI_STATE_SET_LED_ANIM,       // set led anim
  UI_STATE_SET_RGB_COLD,       // set rgb cold
    UI_STATE_SET_RGB_WARM,    // set rgb warm
  UI_STATE_SET_RGB_HOT,        // set rgb hot
  UI_STATE_SET_RGB_BRIGHTNESS, // set rgb brightness
  UI_STATE_SET_RGB_TEMP_COLD,     // set rgb temp cold
  UI_STATE_SET_RGB_TEMP_COLD_MAX, // set rgb temp cold max
    UI_STATE_SET_RGB_TEMP_WARM,  // set rgb temp warm
  UI_STATE_SET_RGB_TEMP_WARM_MAX, // set rgb temp warm max
  UI_STATE_SET_RGB_TEMP_HOT,      // set rgb temp hot
  UI_STATE_SET_RGB_TEMP_HOT_MAX, // set rgb temp hot max
    UI_STATE_SET_RGB_TEMP_DANGER,   // set rgb temp danger
    UI_STATE_SET_RGB_TEMP_FREEZE,   // set rgb temp freeze
  UI_STATE_SET_BUILTIN_IDLE,      // set builtin idle
  UI_STATE_SET_BUILTIN_HEATING,   // Reserved legacy ID; no independently configured heating animation.
  UI_STATE_SET_BUILTIN_CONNECTING,// set builtin connecting
  UI_STATE_SET_BUILTIN_FAULT,     // set builtin fault
  
  // Device Submenu Group
  UI_STATE_SET_TIMEOUT,        // set timeout
  UI_STATE_SYSTEM_STATUS,      // system status
  UI_STATE_ZIGBEE_PAIR,        // zigbee pair
  UI_STATE_SET_TELEMETRY,      // set telemetry
  // Zigbee Reporting Category (Parent)
  UI_STATE_CAT_ZIGBEE_REP,     // cat zigbee rep
  UI_STATE_ZB_REP_FAN,         // zb rep fan
  UI_STATE_ZB_REP_AMBIENT,     // zb rep ambient
  UI_STATE_ZB_REP_CPU,         // zb rep cpu
  UI_STATE_ZB_REP_HEALTH,      // zb rep health
  UI_STATE_ZB_REP_MEMORY,      // zb rep memory
  UI_STATE_ZB_REP_UPTIME,      // zb rep uptime
  
  // Power Submenu Group
  UI_STATE_FACTORY_RESET,      // factory reset
  UI_STATE_RESTART,            // restart
  UI_STATE_SHUTDOWN,           // shutdown
  
  UI_STATE_BACK,               // back
  UI_STATE_SET_BUILTIN_BRIGHTNESS, // set builtin brightness
  UI_STATE_SET_POWER_PROFILE,     // persisted CPU frequency profile
  UI_STATE_SET_ZIGBEE_TX_PROFILE, // persisted radio transmit power
  UI_STATE_CAT_RGB_XY,
  UI_STATE_RGB_COLD_X, UI_STATE_RGB_COLD_Y,
  UI_STATE_RGB_WARM_X, UI_STATE_RGB_WARM_Y,
  UI_STATE_RGB_HOT_X, UI_STATE_RGB_HOT_Y,
  UI_STATE_RGB_DANGER_X, UI_STATE_RGB_DANGER_Y,
  UI_STATE_RGB_FREEZE_X, UI_STATE_RGB_FREEZE_Y,
  UI_STATE_FAN_DELTA_ON, UI_STATE_FAN_DELTA_OFF,
  UI_STATE_OBJECT_SOURCE, UI_STATE_AMBIENT_SOURCE,
  UI_STATE_SET_LANGUAGE,       // Persisted locale selection; appended to preserve existing IDs.
  UI_STATE_SET_BUILTIN_STARTUP, // Independent built-in boot selection.
  UI_STATE_SET_RGB_DANGER,     // Preset selection; preserve existing IDs.
  UI_STATE_SET_RGB_FREEZE,
  /* Appended nested navigation IDs preserve all existing command identities. */
  UI_STATE_CAT_BUILTIN_LED,
  UI_STATE_CAT_EXTERNAL_LED,
  UI_STATE_CAT_BUILTIN_ANIMATIONS,
  UI_STATE_CAT_RGB_REGIONS,
  UI_STATE_CAT_ZONE_FREEZE,
  UI_STATE_CAT_ZONE_COLD,
  UI_STATE_CAT_ZONE_WARM,
  UI_STATE_CAT_ZONE_HOT,
  UI_STATE_CAT_ZONE_DANGER,
  UI_STATE_CAT_SCREEN,
  UI_STATE_CAT_CALIBRATION,
  UI_STATE_CAT_ZIGBEE,
  UI_STATE_CAT_ENERGY,
  UI_STATE_CAT_ADVANCED,
  UI_STATE_CAT_MAINTENANCE,
  UI_STATE_SET_BUILTIN_MODE,
  UI_STATE_SET_DISPLAY_MODE,
  UI_STATE_SET_MENU_AUTO_EXIT, /* Appended to preserve existing state IDs. */
  UI_STATE_COUNT               // count
} UIState;

/**
 * @brief Menu node kinds
 */
typedef enum {
    MENU_NODE_ROOT = 0,     // Root node (monitor)
    MENU_NODE_CATEGORY,     // Main category
    MENU_NODE_LEAF,         // Leaf node with editable value
    MENU_NODE_ACTION,       // Action node (reset, restart, etc.)
    MENU_NODE_BACK,         // Back navigation node
    MENU_NODE_EXIT          // Exit node
} MenuNodeType;

/**
 * @brief Menu manager error codes
 */
typedef enum {
    MENU_OK = 0,
    MENU_ERR_NULL_PTR = -1,
    MENU_ERR_INVALID_STATE = -2,
    MENU_ERR_QUEUE_FULL = -3,
    MENU_ERR_OUT_OF_MEMORY = -4,
    MENU_ERR_NOT_INITIALIZED = -5,
    MENU_ERR_PLUGIN_LIMIT = -6,
    MENU_ERR_NOT_FOUND = -7,
    MENU_ERR_BUSY = -8
} MenuManagerError;

/**
 * @brief Menu event kinds
 */
typedef enum {
    MENU_EVENT_ENCODER_ROTATED = 0,
    MENU_EVENT_BUTTON_PRESSED,
    MENU_EVENT_BUTTON_LONG_PRESSED,
    MENU_EVENT_NAVIGATE_TO,
    MENU_EVENT_WAKE_UP,
    MENU_EVENT_TIMEOUT
} MenuEventType;

/**
 * @brief Menu event arguments
 */
typedef struct {
    MenuEventType type;
    union {
        int8_t encoderDirection;
        UIState targetState;
    } data;
} MenuEvent;

/**
 * @brief Plugin callback actions
 */
typedef enum {
    MENU_PLUGIN_ACTION_ENCODER,
    MENU_PLUGIN_ACTION_BUTTON,
    MENU_PLUGIN_ACTION_ENTER,
    MENU_PLUGIN_ACTION_EXIT
} MenuPluginAction;

/**
 * @brief Plugin callback arguments
 */
typedef struct {
    UIState state;
    MenuPluginAction action;
    int8_t direction;
    bool handled;
} MenuPluginEvent;

/**
 * @brief Plugin event callback signature
 */
typedef void (*MenuPluginHandlerEx)(const MenuPluginEvent *event);

#ifdef __cplusplus
}
#endif

#endif // MENU_MANAGER_TYPES_H
