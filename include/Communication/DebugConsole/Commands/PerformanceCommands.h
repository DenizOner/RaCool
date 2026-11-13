#ifndef PERFORMANCE_COMMANDS_H
#define PERFORMANCE_COMMANDS_H

#include "Communication/DebugConsole/IDebugConsole.h"

/**
 * @brief Performans ve izleme komutlarini kaydeder.
 * Komutlar: CPUTEMP, UPTIME, HEAPWATCH
 */
void PerformanceCommands_Register(IDebugConsole* console);

#endif // PERFORMANCE_COMMANDS_H
