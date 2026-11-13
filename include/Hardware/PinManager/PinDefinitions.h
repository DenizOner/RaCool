#ifndef PIN_DEFINITIONS_H
#define PIN_DEFINITIONS_H

#include <driver/gpio.h>
#include <stdint.h>

/**
 * @file PinDefinitions.h
 * @brief Donanım pin fonksiyonları ve özelliklerine ait tip tanımlamaları.
 *
 * Bu dosya, sistem genelinde GPIO pinlerinin nasıl davranacağını belirleyen
 * enum ve yapıları içerir. Google Docstrings standartlarına uygundur.
 */

/**
 * @brief Sistemdeki tüm fiziksel pin fonksiyonları için benzersiz kimlikler.
 * Donanım modülleri PinManager servisinden pin talep ederken bu enum'u kullanır.
 */
typedef enum {
  PIN_FUNC_NONE = 0,             ///< Atanmamış fonksiyon

  // Çekirdek Donanım (Core Hardware)
  PIN_FUNC_RELAY,                ///< Röle kontrol pini
  PIN_FUNC_BUILTIN_LED,          ///< Dahili durum LED'i

  // I2C Arayüzleri
  PIN_FUNC_I2C0_SCL,             ///< Genel I2C0 Saat pini
  PIN_FUNC_I2C0_SDA,             ///< Genel I2C0 Veri pini
  PIN_FUNC_MLX90614_SCL,         ///< MLX90614 Sensör SCL
  PIN_FUNC_MLX90614_SDA,         ///< MLX90614 Sensör SDA
  PIN_FUNC_OLED_SCL,             ///< OLED Ekran SCL
  PIN_FUNC_OLED_SDA,             ///< OLED Ekran SDA

  // Rotary Encoder (Döner Kodlayıcı)
  PIN_FUNC_ROTARY_CLK,           ///< Encoder Saat pini
  PIN_FUNC_ROTARY_DT,            ///< Encoder Veri pini
  PIN_FUNC_ROTARY_SW,            ///< Encoder Buton pini

  // RGB LED
  PIN_FUNC_RGB_RED,              ///< RGB Kırmızı kanalı
  PIN_FUNC_RGB_GREEN,            ///< RGB Yeşil kanalı
  PIN_FUNC_RGB_BLUE,             ///< RGB Mavi kanalı

  PIN_FUNC_MAX_COUNT             ///< Toplam fonksiyon sayısını belirlemek için son eleman
} PinFunction_t;

/**
 * @brief Pin çalışma modları.
 */
typedef enum {
  PIN_MODE_INPUT = 0,            ///< Giriş modu
  PIN_MODE_OUTPUT,               ///< Çıkış modu
  PIN_MODE_INPUT_OUTPUT,        ///< Hem giriş hem çıkış modu
  PIN_MODE_OPEN_DRAIN            ///< Open-drain modu
} PinMode_t;

/**
 * @brief Pin direnç yapılandırması.
 */
typedef enum {
  PIN_PULL_NONE = 0,             ///< Direnç yok (Floating)
  PIN_PULL_UP,                   ///< Dahili Pull-up
  PIN_PULL_DOWN,                 ///< Dahili Pull-down
  PIN_PULL_UP_DOWN               ///< Hem Pull-up hem Pull-down
} PinPull_t;

/**
 * @brief Pin kesme (interrupt) tipleri.
 */
typedef enum {
  PIN_INTR_DISABLE = 0,          ///< Kesme devre dışı
  PIN_INTR_POSEDGE,              ///< Yükselen kenar tetiklemesi
  PIN_INTR_NEGEDGE,              ///< Düşen kenar tetiklemesi
  PIN_INTR_ANYEDGE,              ///< Her iki kenar tetiklemesi
  PIN_INTR_LOW_LEVEL,            ///< Düşük seviye tetiklemesi
  PIN_INTR_HIGH_LEVEL            ///< Yüksek seviye tetiklemesi
} PinInterruptType_t;

/**
 * @brief Pin sürüş gücü (Drive Strength).
 */
typedef enum {
  PIN_DRIVE_STRENGTH_0 = 0,      ///< En düşük sürüş gücü
  PIN_DRIVE_STRENGTH_1,
  PIN_DRIVE_STRENGTH_2,
  PIN_DRIVE_STRENGTH_3           ///< En yüksek sürüş gücü
} PinDriveStrength_t;

#endif // PIN_DEFINITIONS_H
