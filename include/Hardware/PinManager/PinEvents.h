#ifndef PIN_EVENTS_H
#define PIN_EVENTS_H

#include "Hardware/PinManager/PinDefinitions.h"
#include <stdbool.h>
#include <stdint.h>

/**
 * @file PinEvents.h
 * @brief PinManager olay (event) tanımlamaları.
 */

/**
 * @brief Pin durum değişikliği olayı ismi.
 */
#define PIN_EVENT_CHANGED "pin:changed"

/**
 * @brief Pin durum değişikliği veri yapısı.
 */
typedef struct {
    PinFunction_t func;      ///< Değişen pin fonksiyonu
    gpio_num_t pin;          ///< GPIO numarası
    bool level;              ///< Yeni mantıksal seviye (true=High, false=Low)
    uint64_t timestamp;      ///< Olay zaman damgası (us)
} PinChangedEvent_t;

/**
 * @brief Pin hata olayı ismi.
 */
#define PIN_EVENT_ERROR "pin:error"

/**
 * @brief Pin hata veri yapısı.
 */
typedef struct {
    PinFunction_t func;      ///< Hata veren pin fonksiyonu
    int32_t errorCode;       ///< Hata kodu
    const char *message;     ///< Hata mesajı
} PinErrorEvent_t;

#endif // PIN_EVENTS_H
