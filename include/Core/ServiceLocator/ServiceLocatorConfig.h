#ifndef SERVICE_LOCATOR_CONFIG_H
#define SERVICE_LOCATOR_CONFIG_H

#include <stdbool.h>
#include <stdint.h>

#define SERVICE_LOCATOR_MAX_SERVICES 64u
#define SERVICE_LOCATOR_NAME_CAPACITY 64u
#define SERVICE_LOCATOR_TAG_CAPACITY 32u
#define SERVICE_LOCATOR_LOCK_TIMEOUT_MS 200u


/**
 * @brief ServiceLocator Configuration Structure
 */
typedef struct {
  uint32_t maxServices;   /**< Maximum number of services */
  const char *locatorTag; /**< Tag for logging */
  bool enableLogging;     /**< Enable/Disable internal logging */
} ServiceLocatorConfig;

/**
 * @brief Get the default configuration for the ServiceLocator
 * @return Constant pointer to default configuration
 */
const ServiceLocatorConfig *ServiceLocatorConfig_GetDefault(void);

#endif // SERVICE_LOCATOR_CONFIG_H
