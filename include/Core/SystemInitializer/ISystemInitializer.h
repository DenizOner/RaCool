#ifndef ISYSTEM_INITIALIZER_H
#define ISYSTEM_INITIALIZER_H

#include "Core/IService.h"
#include "Core/DI/IDIRegistry.h"
#include "Core/SystemInitializer/SystemInitializerConfig.h"

/**
 * @brief System startup coordinator interface.
 */
typedef struct SystemInitializer {
  IService base;
  void (*ManageDependencies)(void);
  int32_t (*InitializeWithConfig)(struct SystemInitializer *self,
                                  SystemInitializerConfig config,
                                  IDIRegistry *registry);
} SystemInitializer;

/**
 * @brief Return the startup coordinator; initialization must precede concurrent consumers.
 * Startup failure can retain running bootstrap workers. An injected registry must
 * outlive those workers and any initialized service retaining its own reference.
 * The production registry is ServiceLocator-owned and remains valid until reset.
 * @return SystemInitializer instance.
 */
SystemInitializer *GetSystemInitializer(void);

#endif // ISYSTEM_INITIALIZER_H
