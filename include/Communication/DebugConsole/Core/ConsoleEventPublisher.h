#ifndef CONSOLE_EVENT_PUBLISHER_H
#define CONSOLE_EVENT_PUBLISHER_H

#include <stdbool.h>

/**
 * @file ConsoleEventPublisher.h
 * @brief Konsol olay yayinlama modulu (SRP — Event sorumluluğu).
 *
 * EventBus uzerinden konsol olaylarini yayinlar.
 * DebugConsole ana modulunden ayrilmistir.
 */

/* Olay isimleri — merkezi tanimlama */
#define CONSOLE_EVT_CMD_EXECUTED   "CMD_EXECUTED"
#define CONSOLE_EVT_CMD_FAILED     "CMD_FAILED"
#define CONSOLE_EVT_STARTED        "CONSOLE_STARTED"
#define CONSOLE_EVT_STOPPED        "CONSOLE_STOPPED"
#define CONSOLE_EVT_MODE_CHANGED   "CONSOLE_MODE_CHANGED"

/**
 * @brief Komut yurutme olayini yayinlar.
 */
void ConsoleEventPublisher_CommandExecuted(const char *cmd);

/**
 * @brief Komut hata olayini yayinlar.
 */
void ConsoleEventPublisher_CommandFailed(const char *cmd, int error_code);

/**
 * @brief Konsol baslatma olayini yayinlar.
 */
void ConsoleEventPublisher_Started(void);

/**
 * @brief Konsol durdurma olayini yayinlar.
 */
void ConsoleEventPublisher_Stopped(void);

#endif /* CONSOLE_EVENT_PUBLISHER_H */
