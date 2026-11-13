#ifndef SYSTEM_COMMANDS_H
#define SYSTEM_COMMANDS_H

#include "Communication/DebugConsole/IDebugConsole.h"
#include <stdbool.h>

void SystemCommands_Register(IDebugConsole* console);

// Returns true if the input was intercepted and handled (e.g. y/n for reset)
bool SystemCommands_InterceptInput(const char* line);

#endif // SYSTEM_COMMANDS_H
