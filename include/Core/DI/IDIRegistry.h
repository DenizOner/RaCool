#ifndef IDI_REGISTRY_H
#define IDI_REGISTRY_H

#include <stdbool.h>
#include <stdint.h>

struct IService; // Forward declaration

/**
 * @brief Dependency Injection Registry Interface
 *
 * This interface provides a way to register (bind) and retrieve (resolve)
 * service implementations based on their interface names.
 */
typedef struct IDIRegistry {
  /**
   * @brief Bind an implementation to a name
   * @param name Name of the interface/service
   * @param implementation Pointer to the IService implementation
   * @return 0 on success, error code otherwise
   */
  int32_t (*Bind)(struct IDIRegistry *self, const char *name,
                  struct IService *implementation);

  /**
   * @brief Resolve an implementation by name
   * @param name Name of the interface/service
   * @return Pointer to the IService implementation or NULL if not found
   */
  struct IService *(*Resolve)(struct IDIRegistry *self, const char *name);

  /**
   * @brief Check if a service is registered
   */
  bool (*Has)(struct IDIRegistry *self, const char *name);
} IDIRegistry;

#endif // IDI_REGISTRY_H
