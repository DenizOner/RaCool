#ifndef SERIAL_MONITOR_INTERNAL_H
#define SERIAL_MONITOR_INTERNAL_H
#include <stddef.h>
#include <stdint.h>
/* Private integration boundary: Locked functions require the facade control
 * mutex. Timer dispatch only tries the mutex without waiting. */
typedef int32_t (*SerialMonitorPerfEnqueue)(const void *data, size_t length);
int32_t SerialMonitor_PerfStartLocked(uint32_t frequency_hz);
int32_t SerialMonitor_PerfStopLocked(void);
void SerialMonitor_PerfTickLocked(SerialMonitorPerfEnqueue enqueue);
void SerialMonitor_PerfTick(void);
#endif
