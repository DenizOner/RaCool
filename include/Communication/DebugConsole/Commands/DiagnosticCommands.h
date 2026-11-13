#ifndef DIAGNOSTIC_COMMANDS_H
#define DIAGNOSTIC_COMMANDS_H

#include "Communication/DebugConsole/IDebugConsole.h"

esp_err_t DiagnosticCommands_Register(IDebugConsole* console);

#endif // DIAGNOSTIC_COMMANDS_H
