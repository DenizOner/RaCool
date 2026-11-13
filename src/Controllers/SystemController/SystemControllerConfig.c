#include "Controllers/SystemController/SystemControllerConfig.h"
#include <stddef.h>

SystemControllerConfig SystemController_GetDefaultConfig(void) {
  SystemControllerConfig config = {
      .indications =
          {
              [SYSTEM_STATE_INITIALIZING] =
                  {.rgbColor = {0, 0, 0},
                   .builtinAnim = {BREATHING_GREEN, BUILTIN_LED_PRIO_MEDIUM, 10000},
                   .useRGB = false,
                   .useBuiltin = true},
              [SYSTEM_STATE_READY] =
                  {.rgbColor = {0, 0, 0},
                   .builtinAnim = {DOUBLE_BLINK_WHITE, BUILTIN_LED_PRIO_LOW, 0},
                   .useRGB = false,
                   .useBuiltin = true},
              [SYSTEM_STATE_SETUP] =
                  {.rgbColor = {0, 0, 0},
                   .builtinAnim = {RAINBOW_FLOW, BUILTIN_LED_PRIO_MEDIUM, 0},
                   .useRGB = false,
                   .useBuiltin = true},
              [SYSTEM_STATE_ALARM] =
                  {.rgbColor = {0, 0, 0},
                   .builtinAnim = {POLICE_STROBE, BUILTIN_LED_PRIO_HIGH, 0},
                   .useRGB = false,
                   .useBuiltin = true},
              [SYSTEM_STATE_MAINTENANCE] =
                  {.rgbColor = {0, 0, 0},
                   .builtinAnim = {DOUBLE_BLINK_WHITE, BUILTIN_LED_PRIO_LOW, 0},
                   .useRGB = false,
                   .useBuiltin = true},
              [SYSTEM_STATE_LOW_POWER] =
                  {.rgbColor = {0, 0, 0},
                   .builtinAnim = {0, 0, 0},
                   .useRGB = false,
                   .useBuiltin = false},
              [SYSTEM_STATE_RECOVERING] =
                  {.rgbColor = {0, 0, 0},
                   .builtinAnim = {POLICE_STROBE, BUILTIN_LED_PRIO_HIGH, 500},
                   .useRGB = false,
                   .useBuiltin = true},
              [SYSTEM_STATE_ERROR] =
                  {.rgbColor = {0, 0, 0},
                   .builtinAnim = {POLICE_STROBE, BUILTIN_LED_PRIO_HIGH, 0},
                   .useRGB = false,
                   .useBuiltin = true},
          },
      .eventMappings = {{.eventId = 100, .targetState = SYSTEM_STATE_ERROR},
                        {.eventId = 200, .targetState = SYSTEM_STATE_ALARM}},
      .eventMappingCount = 2,
      .telemetryIntervalMs = 20000,
      .zigbeePairingStartAnim = {HEARTBEAT_RED, BUILTIN_LED_PRIO_HIGH, 0},
      .zigbeePairingStopAnim = {SOLID_GREEN, BUILTIN_LED_PRIO_HIGH, 3000}};

  return config;
}
