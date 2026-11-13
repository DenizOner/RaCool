#ifndef SCREEN_MONITOR_H
#define SCREEN_MONITOR_H

#include "Management/ConfigurationManager/SystemConfigs.h"
#include <stdint.h>
#include "Controllers/ClimateController/ClimateControllerConfig.h"
#include <stdbool.h>
#include "Core/Localization/Localization.h"

/* Resolve from the same settings snapshot used for this frame. */
static inline ClimateFanMode ScreenMonitor_GetFanMode(const ControlConfigs *config) {
  if (config->fan_manual_mode)
    return config->fan_manual_state ? CLIMATE_FAN_MANUAL_ON : CLIMATE_FAN_MANUAL_OFF;
  return config->fan_strategy == 1 ? CLIMATE_FAN_AMBIENT_DELTA : CLIMATE_FAN_OBJECT_SENSITIVE;
}

bool ScreenMonitor_Create(void);

void ScreenMonitor_Update(uint32_t nowMs, SystemSettings *settings,
                          float objectTemp, float ambientTemp, float cpuTemp,
                          bool isRelayOn, ClimateFanMode fanMode, bool isZigbeeConnected, uint8_t signalBars,
                          const LocalizationCatalog *catalog);

#endif /* SCREEN_MONITOR_H */
