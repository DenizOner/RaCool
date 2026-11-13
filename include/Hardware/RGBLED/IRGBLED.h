#ifndef IRGBLED_H
#define IRGBLED_H
#include "Core/IService.h"
#include "Hardware/RGBLED/RGBLEDConfig.h"
#include "Hardware/RGBLED/RGBLEDTypes.h"
#include <esp_err.h>
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* Public controls copy commands into the existing worker queue without waiting.
 * ESP_OK is admission, not completed output. GetLEDState reports worker state. */
typedef struct IRGBLED IRGBLED;
struct IRGBLED {
  IService base;
  esp_err_t (*SetColorRGB)(IRGBLED *self, RGBColor color);
  /* Temporary output overlay; does not mutate settings or animation phase. */
  esp_err_t (*PreviewColor)(IRGBLED *self, RGBColor color, uint32_t duration_ms);
    esp_err_t (*PreviewAnimation)(IRGBLED *self, RGBLEDAnimType type, uint32_t duration_ms);
  esp_err_t (*SetColorHSV)(IRGBLED *self, HSVColor color);
  esp_err_t (*UpdateByTemperature)(IRGBLED *self, float temperature);
  esp_err_t (*FadeToColor)(IRGBLED *self, RGBColor target, uint32_t fade_ms);
  esp_err_t (*SetBrightness)(IRGBLED *self, uint8_t brightness);
  /* Transient worker overlay; saved brightness and animation phase are unchanged. */
  esp_err_t (*PreviewBrightness)(IRGBLED *self, uint8_t brightness, uint32_t duration_ms);
  esp_err_t (*StartAnimation)(IRGBLED *self, RGBLEDAnimType type,
                             RGBLEDPriority priority, uint32_t duration_ms);
  esp_err_t (*StopAnimation)(IRGBLED *self);
  /* Explicitly reselect persistent mode, clearing a manual color override even
   * when the selected mode is unchanged. Returns queue admission. */
  esp_err_t (*ApplySelectedMode)(IRGBLED *self);
  esp_err_t (*TurnOff)(IRGBLED *self);
  esp_err_t (*GetLEDState)(IRGBLED *self, RGBLEDState *out_state);
};

/* Initialize interface pointers before starting concurrent consumers. */
IRGBLED *GetRGBLEDService(void);
/* Persist the setting; configuration notifications apply it asynchronously. */
esp_err_t RGBLED_SaveBrightnessText(const char *value);

/* Deprecated source compatibility; new callers must check SetColorRGB errors. */
static inline void RGBLED_SetColor_Compat(IRGBLED *self, uint8_t r, uint8_t g, uint8_t b) {
  if (self && self->SetColorRGB) {
    RGBColor color = {.r=r,.g=g,.b=b};
    (void)self->SetColorRGB(self, color);
  }
}
/* Deprecated source compatibility; new callers must check operation results. */
static inline void RGBLED_UpdateTemp_Compat(IRGBLED *self, float temperature) {
  if (self && self->UpdateByTemperature) (void)self->UpdateByTemperature(self, temperature);
}
#ifdef __cplusplus
}
#endif
#endif
