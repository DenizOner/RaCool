#pragma once
#include "Hardware/BuiltinLED/IBuiltinLED.h"
#include <stdio.h>

/* This snapshot describes the worker, not the physical emitted color. */
static inline void BuiltinLED_PrintMachineStatus(const BuiltinLEDStatus *status,
                                               esp_err_t driver_error) {
    printf("RACOOL_CLI v=1 type=builtin_status on=%u enabled=%u brightness=%u animation=%d priority=%d remaining_ms=%lu plugins=%u driver_error=%d\n",
           (unsigned)status->is_on, (unsigned)status->enabled,
           (unsigned)status->brightness, (int)status->current_animation,
           (int)status->current_priority, (unsigned long)status->remaining_ms,
           (unsigned)status->registered_plugins_count, (int)driver_error);
}
