#ifndef SYSTEM_INITIALIZER_CONFIG_H
#define SYSTEM_INITIALIZER_CONFIG_H

#include <stdbool.h>
#include <stdint.h>

#define SYSTEM_INITIALIZER_WORKER_STACK_BYTES 8192u
#define SYSTEM_INITIALIZER_WORKER_PRIORITY 5u
/* Includes bounded Zigbee descriptor persistence and the remaining service startup. */
#define SYSTEM_INITIALIZER_WORKER_TIMEOUT_MS 30000u
#define SYSTEM_INITIALIZER_SENSOR_SETTLE_MS 200u
#define SYSTEM_INITIALIZER_MAX_STARTUP_DELAY_MS 60000u

/**
 * @brief Configuration structure for the SystemInitializer
 *
 * This structure determines which components and modules are initialized
 * during the system boot process. This is useful for testing, safe mode,
 * or partial boots.
 */
typedef struct {
  bool enableCoreServices;    ///< Initialize core services like LogManager,
                              ///< ErrorHandler, MemoryManager, etc.
  bool enableHardwareModules; ///< Initialize hardware layers like LEDs, Relays,
                              ///< Sensors
  bool enableCommunicationModules; ///< Initialize networking, Zigbee, etc.
  bool enableUserInterface;        ///< Initialize screen, encoder, menus
  bool enableAdvancedManagers;     ///< Initialize Watchdog, Power, Security,
                                   ///< Diagnostics
  bool enableControllers;  ///< Initialize System, Climate, UI, Application
                           ///< controllers
  bool bypassNvsInit;      ///< Skip NVS initialization (useful for some test
                           ///< environments)
  uint32_t startupDelayMs; ///< Optional startup delay before initialization
                           ///< begins in milliseconds
} SystemInitializerConfig;

/**
 * @brief Get the default configuration for the SystemInitializer
 *
 * Returns a configuration with all modules enabled (true) and
 * zero startup delay.
 *
 * @return SystemInitializerConfig struct with default values
 */
SystemInitializerConfig SystemInitializerConfig_GetDefault(void);

#endif // SYSTEM_INITIALIZER_CONFIG_H
