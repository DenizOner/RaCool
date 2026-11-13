#ifndef ILOGGER_H
#define ILOGGER_H

#include "esp_log.h"
#include <stdarg.h>

/**
 * @file ILogger.h
 * @brief Logger soyutlama arayuzu.
 *
 * Farkli log hedeflerine (flash, konsol, ag) yazma destegi saglar.
 */
typedef struct ILogger {
    void (*Log)(struct ILogger *self, esp_log_level_t level,
                const char *tag, const char *fmt, ...);
    void (*VLog)(struct ILogger *self, esp_log_level_t level,
                 const char *tag, const char *fmt, va_list args);
} ILogger;

#endif /* ILOGGER_H */
