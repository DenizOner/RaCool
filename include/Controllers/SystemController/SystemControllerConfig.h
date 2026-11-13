#ifndef SYSTEM_CONTROLLER_CONFIG_H
#define SYSTEM_CONTROLLER_CONFIG_H

#include "Controllers/SystemController/ISystemController.h"
#include "Hardware/BuiltinLED/BuiltinLEDConfig.h"
#include <stdint.h>

#define SYSTEM_CONTROLLER_EVENT_QUEUE_SIZE 10U
#define SYSTEM_CONTROLLER_TASK_STACK_SIZE 4096U
#define SYSTEM_CONTROLLER_TASK_PRIORITY 5U
#define SYSTEM_CONTROLLER_CONTROL_TIMEOUT_MS 2000U
#define SYSTEM_CONTROLLER_WORKER_POLL_MS 50U
#define SYSTEM_CONTROLLER_RECOVERY_DELAY_MS 10000U
#define SYSTEM_CONTROLLER_RECOVERY_RETRY_MS 1000U
#define SYSTEM_CONTROLLER_RECOVERY_MAX_ATTEMPTS 3U
#define SYSTEM_CONTROLLER_SHUTDOWN_MAX_DELAY_MS 60000U

#define SYSTEM_TELEMETRY_TASK_STACK_SIZE 4096U
#define SYSTEM_TELEMETRY_TASK_PRIORITY 5U
#define SYSTEM_TELEMETRY_CONTROL_TIMEOUT_MS 2000U
#define SYSTEM_TELEMETRY_WORKER_POLL_MS 50U
#define SYSTEM_TELEMETRY_MENU_STEP_MS 5000U

/**
 * @brief RGB Color representation for system indications
 */
typedef struct {
  uint8_t red;
  uint8_t green;
  uint8_t blue;
} SystemRGBColor;

/**
 * @brief Builtin LED Animation configuration
 */
typedef struct {
  BuiltinLEDAnimationType type;
  BuiltinLEDPriority priority;
  uint32_t duration_ms;
} SystemLEDAnimation;

/**
 * @brief Collection of indications for a specific system state
 */
typedef struct {
  SystemRGBColor rgbColor;
  SystemLEDAnimation builtinAnim;
  bool useRGB;
  bool useBuiltin;
} SystemStateIndication;

/**
 * @brief Mapping from event ID to target system state
 */
typedef struct {
  uint32_t eventId;
  SystemState targetState;
} SystemEventMapping;

/**
 * @brief SystemController Configuration structure
 */
typedef struct {
  SystemStateIndication indications[8]; // Indexed by SystemState enum
  SystemEventMapping eventMappings[8];  // Up to 8 custom event mappings
  uint8_t eventMappingCount;

  uint32_t telemetryIntervalMs;         // Dynamic reporting interval

  SystemLEDAnimation zigbeePairingStartAnim;
  SystemLEDAnimation zigbeePairingStopAnim;
} SystemControllerConfig;

/**
 * @brief Get the default configuration for SystemController
 */
SystemControllerConfig SystemController_GetDefaultConfig(void);

#endif // SYSTEM_CONTROLLER_CONFIG_H
