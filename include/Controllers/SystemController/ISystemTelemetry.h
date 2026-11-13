#ifndef ISYSTEM_TELEMETRY_H
#define ISYSTEM_TELEMETRY_H

#include <stdint.h>
#include "Core/IService.h"

/**
 * @brief System Telemetry Interface (ISP)
 * Handles reporting of system metrics to external entities (e.g., Zigbee)
 */
typedef struct ISystemTelemetry ISystemTelemetry;

struct ISystemTelemetry {
  IService base;

  /**
   * @brief Set the interval for periodic reporting
   * @param interval_ms Interval in milliseconds; zero disables periodic publication.
   * Explicit immediate reports and independent sensor/safety events remain active.
   */
  void (*SetReportingInterval)(ISystemTelemetry *self, uint32_t interval_ms);

  /**
   * @brief Get the current reporting interval
   */
  uint32_t (*GetReportingInterval)(ISystemTelemetry *self);

  /**
   * @brief Trigger an immediate report of all metrics
   */
  void (*TriggerImmediateReport)(ISystemTelemetry *self);
};

/**
 * @brief Factory method for creating/retrieving the telemetry implementation
 */
ISystemTelemetry *GetSystemTelemetry(void);

#endif // ISYSTEM_TELEMETRY_H
