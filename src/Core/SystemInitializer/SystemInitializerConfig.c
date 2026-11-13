#include "Core/SystemInitializer/SystemInitializerConfig.h"

SystemInitializerConfig SystemInitializerConfig_GetDefault(void) {
  SystemInitializerConfig config = {
      .enableCoreServices = true,
      .enableHardwareModules = true,
      .enableCommunicationModules = true,
      .enableUserInterface = true,
      .enableAdvancedManagers = true,
      .enableControllers = true,
      .bypassNvsInit = false,
      .startupDelayMs = 0,
  };

  return config;
}
