#ifndef SERVICELOCATOR_H
#define SERVICELOCATOR_H

#include "Core/DI/IDIRegistry.h"
#include "Core/EventBus/IEventBus.h"
#include "Core/LogManager/ILogManager.h"
#include "Core/ServiceLocator/ServiceLocatorConfig.h"
#include <stdint.h>


/**
 * @brief Initialize the registry from a validated configuration.
 */
int32_t ServiceLocator_Init(const ServiceLocatorConfig *config);

/**
 * @brief Release registry storage; return busy during lifecycle callbacks.
 */
int32_t ServiceLocator_Deinit(void);

/**
 * @brief Registry and service lifecycle operations.
 * Lookup-only bindings may omit lifecycle methods; an explicit batch fails for
 * a missing requested method. Start stops at the first failure, while Stop
 * attempts every distinct owner in reverse first-binding order and returns the
 * first error. Aliases never run one owner's lifecycle callback twice.
 */
typedef struct ServiceLocator {
  int32_t (*RegisterService)(const char *serviceName, IService *service);
  IService *(*GetService)(const char *serviceName);
  int32_t (*UnregisterService)(const char *serviceName);
  int32_t (*StartAllServices)(void);
  int32_t (*StopAllServices)(void);
} ServiceLocator;

/**
 * @brief Register the LogManager service.
 */
int32_t ServiceLocator_RegisterLogManager(ILogManager *logger);

/**
 * @brief Resolve the LogManager service.
 */
ILogManager *ServiceLocator_GetLogManager(void);

/**
 * @brief Resolve the EventBus service.
 */
IEventBus *ServiceLocator_GetEventBus(void);

/**
 * @brief Register a named service; the module retains service ownership.
 */
int32_t ServiceLocator_RegisterService(const char *name, IService *service);

/**
 * @brief Borrow a registered service pointer from its module owner.
 */
IService *ServiceLocator_GetService(const char *serviceName);

/**
 * @brief Return a caller-freed array of distinct borrowed service pointers.
 */
IService **ServiceLocator_GetAllServices(uint32_t *count);

typedef struct {
  IService *service; /* Borrowed from its module owner. */
  char name[SERVICE_LOCATOR_NAME_CAPACITY];
} ServiceLocatorBinding;

/* Copy name/service pairs in one registry snapshot; names remain owned by the
 * caller after removal. Failure preserves output/count. */
int32_t ServiceLocator_CopyBindings(ServiceLocatorBinding *bindings,uint32_t capacity,uint32_t *count);

/**
 * @brief Copy a registered service name into caller-owned storage.
 */
/* Copy into caller-owned storage; failure leaves outputs unchanged.
 * Service pointers remain borrowed from their module owners. */
int32_t ServiceLocator_CopyServiceName(IService *service, char *name, size_t capacity);
/* Copy distinct owners in first-binding order; aliases appear in CopyBindings. */
int32_t ServiceLocator_CopyServices(IService **services, uint32_t capacity, uint32_t *count);

/**
 * @brief Get the IDIRegistry interface from the ServiceLocator
 */
IDIRegistry *ServiceLocator_GetRegistry(void);

/**
 * @brief Return the global registry interface.
 */
ServiceLocator *GetServiceLocator(void);

#endif // SERVICELOCATOR_H
