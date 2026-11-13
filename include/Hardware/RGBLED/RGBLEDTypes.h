#ifndef RGBLED_TYPES_H
#define RGBLED_TYPES_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ──────────────────────────────────────────
 * Color models
 * ────────────────────────────────────────── */

/** @brief RGB channels (0-255) */
typedef struct {
  uint8_t r;
  uint8_t g;
  uint8_t b;
} RGBColor;

/** @brief HSV color */
typedef struct {
  uint16_t h; /**< Hue: 0-360 degrees */
  uint8_t  s; /**< Saturation: 0-255 */
  uint8_t  v; /**< Value/Brightness: 0-255 */
} HSVColor;

/** @brief Color mode */
typedef enum {
  RGBLED_COLOR_MODE_RGB = 0,
  RGBLED_COLOR_MODE_HSV,
  RGBLED_COLOR_MODE_TEMPERATURE,
  RGBLED_COLOR_MODE_ANIMATION,
  RGBLED_COLOR_MODE_COUNT
} RGBLEDColorMode;

/* ──────────────────────────────────────────
 * Animation types
 * ────────────────────────────────────────── */

typedef enum {
  RGBLED_ANIM_NONE = 0,
  RGBLED_ANIM_BREATHING,     /**< Breathing */
  RGBLED_ANIM_RAINBOW,       /**< Rainbow cycle */
  RGBLED_ANIM_PULSE,         /**< Quick pulse */
  RGBLED_ANIM_CANDLE,        /**< Candle flicker */
  RGBLED_ANIM_ALERT,         /**< Alert blink */
  RGBLED_ANIM_FADE_IN_OUT,   /**< Fade in/out */
  RGBLED_ANIM_COLOR_CYCLE,   /**< Color cycle */
  RGBLED_ANIM_TEMPERATURE,   /**< Temperature-based color */
  /* Existing IDs 0..8 remain unchanged for persisted/protocol settings. */
  RGBLED_ANIM_HEARTBEAT_RED,
  RGBLED_ANIM_BLINK_WHITE,
  RGBLED_ANIM_SOS_RED,
  RGBLED_ANIM_BREATHING_GREEN,
  RGBLED_ANIM_FADE_BLUE,
  RGBLED_ANIM_STROBE_WHITE,
  RGBLED_ANIM_POLICE_STROBE,
  RGBLED_ANIM_SOLID_RED,
  RGBLED_ANIM_SOLID_GREEN,
  RGBLED_ANIM_SOLID_BLUE,
  RGBLED_ANIM_SOLID_WHITE,
  RGBLED_ANIM_BLINK_RED,
  RGBLED_ANIM_BREATHING_RED,
  RGBLED_ANIM_DOUBLE_BLINK_WHITE,
  RGBLED_ANIM_COUNT
} RGBLEDAnimType;

/** @brief Animation priority */
typedef enum {
  RGBLED_PRIO_LOW = 0,       /**< Normal operation */
  RGBLED_PRIO_MEDIUM,        /**< User override */
  RGBLED_PRIO_HIGH,          /**< System alert */
  RGBLED_PRIO_CRITICAL       /**< Error state */
} RGBLEDPriority;

/* ──────────────────────────────────────────
 * Commands (async queue)
 * ────────────────────────────────────────── */

typedef enum {
  RGBLED_CMD_SET_COLOR_RGB,
  RGBLED_CMD_SET_COLOR_HSV,
  RGBLED_CMD_SET_BRIGHTNESS,
  RGBLED_CMD_SET_ANIMATION,
  RGBLED_CMD_STOP_ANIMATION,
  RGBLED_CMD_UPDATE_TEMPERATURE,
  RGBLED_CMD_TURN_OFF,
  RGBLED_CMD_APPLY_SETTINGS,
  RGBLED_CMD_FADE_TO_COLOR,
  RGBLED_CMD_PREVIEW_COLOR,
  RGBLED_CMD_PREVIEW_ANIMATION,
  RGBLED_CMD_PREVIEW_BRIGHTNESS,
} RGBLEDCommandType;

/** @brief Copied asynchronous queue command */
typedef struct {
  RGBLEDCommandType type;
  union {
    RGBColor rgb;
    HSVColor hsv;
    uint8_t brightness;
    float temperature;
      bool force_mode;
    struct {
      RGBLEDAnimType animType;
      RGBLEDPriority priority;
      uint32_t duration_ms;
    } animation;
    struct { RGBColor color; uint32_t duration_ms; } preview;
    struct { uint8_t brightness; uint32_t duration_ms; } brightness_preview;
    struct {
      RGBColor target;
      uint32_t fade_ms;
    } fade;
  } payload;
} RGBLEDCommand;

/* ──────────────────────────────────────────
 * Runtime state
 * ────────────────────────────────────────── */

/** @brief Worker state snapshot */
typedef struct {
  RGBColor currentColor;       /**< Logical worker color; not a measured physical edge */
  RGBColor targetColor;        /**< Target transition color */
  HSVColor currentHSV;         /**< Current HSV value */
  uint8_t brightness;          /**< Applied brightness (0-255) */
  RGBLEDColorMode colorMode;   /**< Active color mode */
  RGBLEDAnimType activeAnim;   /**< Active animation */
  RGBLEDPriority animPriority; /**< Animation priority */
  bool isOn;                   /**< LED on state? */
  bool isFading;               /**< Fade transition active? */
  bool previewActive;
  RGBColor previewColor;
  uint32_t previewRemainingMs;
  float lastTemperature;       /**< Last known temperature */
} RGBLEDState;

/* ──────────────────────────────────────────
 * Event names
 * ────────────────────────────────────────── */

#define RGBLED_EVENT_STATE_CHANGED  "rgbled_state_changed"
#define RGBLED_EVENT_COLOR_CHANGED  "rgbled_color_changed"
#define RGBLED_EVENT_ANIM_FINISHED  "rgbled_anim_finished"
#define RGBLED_EVENT_ERROR          "rgbled_error"

#ifdef __cplusplus
}
#endif

#endif // RGBLED_TYPES_H
