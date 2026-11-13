#ifndef ISERVICE_H
#define ISERVICE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct IDIRegistry; /* Forward declaration. */

/**
 * @brief Service lifecycle states.
 */
typedef enum {
  SERVICE_STATE_UNINITIALIZED = 0,
  SERVICE_STATE_INITIALIZED,
  SERVICE_STATE_STARTED,
  SERVICE_STATE_STOPPED,
  SERVICE_STATE_ERROR
} ServiceState;

/**
 * @brief Base service interface.
 *
 * All services in the RaCool system embed this struct as their first member
 * (inheritance via composition), enabling lifecycle management through a
 * common interface regardless of the concrete service type.
 * The Safe helpers below check state; they are not synchronization barriers.
 * The lifecycle owner must serialize requests for each service.
 */
typedef struct IService {
  /**
   * @brief Prepares the service and resolves its dependencies.
   *
   * Implementations should obtain their required dependencies from the
   * registry here and transition to SERVICE_STATE_INITIALIZED on success.
   * Must be called before Start().
   *
   * @param self     Pointer to the service instance.
   * @param registry Dependency injection registry for resolving services.
   * @return         0 on success, negative error code on failure.
   */
  int32_t (*Initialize)(struct IService *self, struct IDIRegistry *registry);

  /**
   * @brief Activates the service (e.g., starts background tasks, opens resources).
   *
   * The service must be initialized; implementations may also resume from
   * SERVICE_STATE_STOPPED when their retained resources support restart.
   * On success, the state transitions to SERVICE_STATE_STARTED.
   *
   * @param self Pointer to the service instance.
   * @return     0 on success, negative error code on failure.
   */
  int32_t (*Start)(struct IService *self);

  /**
   * @brief Deactivates the service and releases any held resources.
   *
   * Stop new work and cooperatively join owned workers before releasing their
   * resources. Never delete a worker while it owns a lock or borrowed payload.
   * Implementations may retain process-owned resources/subscriptions for restart
   * if callback storage remains valid. A timeout/failure preserves ownership for
   * retry and must not report SERVICE_STATE_STOPPED before stop succeeds.
   *
   * @param self Pointer to the service instance.
   * @return     0 on success, negative error code on failure.
   */
  int32_t (*Stop)(struct IService *self);

  /**
   * @brief Returns the current lifecycle state of the service.
   *
   * @param self Pointer to the service instance.
   * @return     Current ServiceState value.
   */
  ServiceState (*GetState)(struct IService *self);

  /**
   * @brief Performs a health check on the service.
   *
   * Returns 0 if the service is operating correctly, negative otherwise.
   * Used by DiagnosticsManager to detect degraded services.
   *
   * @param self Pointer to the service instance.
   * @return     0 if healthy, negative on fault.
   */
  int32_t (*CheckHealth)(struct IService *self);
} IService;

/**
 * @brief Safely initialises a service if it has not already been initialised.
 *
 * Checks the service state before calling Initialize() so that calling code
 * does not need to track initialisation manually.
 *
 * @param service  Pointer to the IService instance.
 * @param registry Dependency injection registry.
 * @return         0 if already initialised or on successful initialisation,
 *                 the implementation error code on failure; -1 for an invalid interface/state.
 */
static inline int32_t IService_SafeInitialize(IService *service,
                                               struct IDIRegistry *registry) {
  if (!service || !service->Initialize || !service->GetState) return -1;
  ServiceState state = service->GetState(service);
  if (state == SERVICE_STATE_INITIALIZED || state == SERVICE_STATE_STARTED) {
    return 0; /* Already initialised or started. */
  }
  if (state != SERVICE_STATE_UNINITIALIZED && state != SERVICE_STATE_STOPPED &&
      state != SERVICE_STATE_ERROR) return -1;
  return service->Initialize(service, registry);
}

/**
 * @brief Safely starts a service if it has not already been started.
 *
 * Validates state before calling Start() to prevent double-start bugs.
 *
 * @param service Pointer to the IService instance.
 * @return        0 if already started or on successful start,
 *                -1 if the service is uninitialised, NULL, or missing vtable.
 */
static inline int32_t IService_SafeStart(IService *service) {
  if (!service || !service->Start || !service->GetState) return -1;
  ServiceState state = service->GetState(service);
  if (state == SERVICE_STATE_STARTED) {
    return 0; /* Already started. */
  }
  if (state != SERVICE_STATE_INITIALIZED && state != SERVICE_STATE_STOPPED) {
    return -1; /* Initialization/recovery must succeed before starting. */
  }
  return service->Start(service);
}

#endif // ISERVICE_H
