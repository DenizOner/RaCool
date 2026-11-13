#include "Core/ServiceLocator/ServiceLocatorConfig.h"

static const ServiceLocatorConfig default_config = {
    .maxServices = 40, .locatorTag = "ServiceLocator", .enableLogging = true};

const ServiceLocatorConfig *ServiceLocatorConfig_GetDefault(void) {
  return &default_config;
}
