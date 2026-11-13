#ifndef RGBLED_ANIMATIONS_H
#define RGBLED_ANIMATIONS_H

#include "Hardware/RGBLED/RGBLEDTypes.h"
#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ──────────────────────────────────────────
 * Animasyon Motor Arayüzü
 * ────────────────────────────────────────── */

/**
 * @brief Animasyon motorunu başlatır
 * @return ESP_OK başarılı
 */
esp_err_t RGBLEDAnimEngine_Init(void);

/**
 * @brief Animasyon motorunu durdurur ve kaynakları serbest bırakır
 */
void RGBLEDAnimEngine_Deinit(void);

/**
 * @brief Yeni animasyon başlatır
 * @param type Animasyon tipi
 * @param baseColor Animasyonun temel rengi (NONE hariç)
 * @param priority Animasyon önceliği
 * @param duration_ms Süre (0 = sonsuz)
 * @return ESP_OK başarılı
 */
esp_err_t RGBLEDAnimEngine_Start(RGBLEDAnimType type, RGBColor baseColor,
                                  RGBLEDPriority priority, uint32_t duration_ms);

/**
 * @brief Mevcut animasyonu durdurur
 * @return ESP_OK başarılı
 */
esp_err_t RGBLEDAnimEngine_Stop(void);

/**
 * @brief Animasyon tick'i — periyodik olarak çağrılır
 * @param elapsed_ms Son tick'ten bu yana geçen süre
 * @param out_color Hesaplanan renk çıktısı
 * @return true: renk güncellendi, false: güncelleme yok
 */
bool RGBLEDAnimEngine_Tick(uint32_t elapsed_ms, RGBColor *out_color);

/**
 * @brief Animasyonun aktif olup olmadığını sorgular
 * @return true aktif animasyon var
 */
bool RGBLEDAnimEngine_IsActive(void);

/**
 * @brief Aktif animasyon tipini döndürür
 */
RGBLEDAnimType RGBLEDAnimEngine_GetCurrentType(void);

/**
 * @brief Aktif animasyon önceliğini döndürür
 */
RGBLEDPriority RGBLEDAnimEngine_GetCurrentPriority(void);

#ifdef __cplusplus
}
#endif

#endif // RGBLED_ANIMATIONS_H
