#ifndef CONSOLE_PRIVACY_H
#define CONSOLE_PRIVACY_H

#include <stdbool.h>
#include <stddef.h>

/* Shared policy for terminal rendering, history and event/log redaction. */
static inline bool ConsolePrivacy_IsPrivate(const char *command) {
    if (!command) return false;
    while (*command == ' ' || *command == '\t' || *command == '\r' || *command == '\n') ++command;
    const char *end = command;
    while (*end && *end != ' ' && *end != '\t' && *end != '\r' && *end != '\n') {
        if (*end == '\"' || *end == '\'' || *end == '\\') return true;
        ++end;
    }
    return end - command == 3 &&
           (command[0] == 's' || command[0] == 'S') &&
           (command[1] == 'e' || command[1] == 'E') &&
           (command[2] == 'c' || command[2] == 'C');
}

/* Volatile stores preserve erasure of caller-owned temporary secret storage. */
static inline void ConsolePrivacy_Clear(void *storage, size_t size) {
    volatile unsigned char *bytes = (volatile unsigned char *)storage;
    while (size--) *bytes++ = 0;
}
#endif
