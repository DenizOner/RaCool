#ifndef IOLED_H
#define IOLED_H

#include "Core/IService.h"
#include "Hardware/OLED/OLEDConfig.h"
#include <stdbool.h>
#include "esp_err.h"
#include <stdint.h>

/**
 * @brief OLED Service Interface
 *
 * Owns one LVGL worker and an explicit I2C bus lease. Commands are synchronous
 * and serialized with panel lifetime. Stop waits for the worker outside the
 * LVGL lock before detaching panel/IO and releasing the bus. Failed Stop keeps
 * resources for retry. The LVGL display, buffer and mutex remain allocated across
 * Stop/Start so UIController screen pointers stay valid. Call Lock/Unlock only
 * after successful base initialization; do not nest the non-recursive lock.
 */
typedef struct IOLED IOLED;

/* Last complete SSD1306 page payload accepted by every SDK transfer.
 * This is panel-RAM transmission evidence, not an optical measurement. */
typedef struct {
  uint32_t sequence;
  uint32_t timestamp_ms;
  bool powered;
  bool inverted;
  bool flipped;
  uint8_t pixels[OLED_WIDTH * OLED_HEIGHT / 8];
} OLEDFrameSnapshot;

struct IOLED {
  IService base;

  /**
   * @brief Initializes the OLED display and LVGL environment
   * @param self OLED instance
   * @param config Configuration struct
   * @return true on success, false otherwise
   */
  bool (*Initialize)(IOLED *self, OLEDConfig config);

  /**
   * @brief Sets the power state of the OLED display
   * @param self OLED instance
   * @return ESP_OK only after SDK success; otherwise the validation, lock or SDK error.
   * @param on True to turn on, false to turn off
   */
  esp_err_t (*SetPower)(IOLED *self, bool on);

  /* Caller holds Lock. Refresh the selected LVGL screen into panel RAM before
   * enabling output. Failed/no transfer must leave the panel disabled. */
  esp_err_t (*PresentAndWake)(IOLED *self);

  /**
   * @brief Acquire the LVGL mutex within the bounded control timeout.
   * @return true only when acquired; call Unlock only after success.
   * @param self OLED instance
   */
  bool (*Lock)(IOLED *self);

  /**
   * @brief Unlocks the LVGL Mutex
   * @param self OLED instance
   */
  void (*Unlock)(IOLED *self);
  /**
   * @brief Sets the contrast of the OLED display
   * @param self OLED instance
   * @return ESP_OK only after SDK success; otherwise the validation, lock or SDK error.
   * @param contrast Contrast value (0-255)
   */
  esp_err_t (*SetContrast)(IOLED *self, uint8_t contrast);

  /**
   * @brief Inverts the display colors
   * @param self OLED instance
   * @return ESP_OK only after SDK success; otherwise the validation, lock or SDK error.
   * @param invert True to invert, false for normal
   */
  esp_err_t (*InvertDisplay)(IOLED *self, bool invert);
  /* Checked bounded copy; no redraw/wake, output unchanged on failure. */
  esp_err_t (*CopyFrame)(IOLED *self, OLEDFrameSnapshot *output);
};

/**
 * @brief Returns the Singleton instance of the OLED module
 * @return IOLED* Module instance
 */
IOLED *OLED_GetModule(void);

#endif // IOLED_H
