#ifndef I_RESOURCE_REGISTRY_H
#define I_RESOURCE_REGISTRY_H

#include "Core/IService.h"
#include "Hardware/ResourceRegistry/ResourceRegistryConfig.h"
#include <driver/gpio.h>
#include <stdbool.h>
#include <stdint.h>

/**
 * @file IResourceRegistry.h
 * @brief Interface definition for the hardware Resource Registry service.
 *
 * Provides central tracking, conflict detection, thread-safe reservation,
 * release, and health validation of hardware resources (GPIO, I2C, PWM, ADC).
 */

/**
 * @brief Event names published by ResourceRegistry on EventBus.
 */
#define RESOURCE_EVENT_RESERVED   "resource.reserved"
#define RESOURCE_EVENT_RELEASED   "resource.released"
#define RESOURCE_EVENT_CONFLICT   "resource.conflict"

/**
 * @brief Types of hardware resources managed by the registry.
 */
typedef enum {
    RESOURCE_TYPE_NONE = 0,
    RESOURCE_TYPE_GPIO,
    RESOURCE_TYPE_I2C_BUS,
    RESOURCE_TYPE_PWM_CHANNEL,
    RESOURCE_TYPE_ADC_CHANNEL,
    RESOURCE_TYPE_MAX
} ResourceType_t;

/**
 * @brief Unique Resource Identifier structure.
 */
typedef struct {
    ResourceType_t type;  /**< Category of hardware resource. */
    uint32_t id;          /**< Hardware identifier (GPIO num, I2C port, PWM ch, ADC unit). */
} ResourceId_t;

/**
 * @brief Resource Registry Statistics structure.
 */
typedef struct {
    uint32_t total_reservations;  /**< Cumulative successful reservations. */
    uint32_t total_releases;      /**< Cumulative successful releases. */
    uint32_t total_conflicts;     /**< Cumulative conflict attempts. */
    uint32_t active_gpio_count;   /**< Currently reserved GPIO count. */
    uint32_t active_i2c_count;    /**< Currently reserved I2C count. */
    uint32_t active_pwm_count;    /**< Currently reserved PWM count. */
    uint32_t active_adc_count;    /**< Currently reserved ADC count. */
} ResourceRegistryStats_t;

/**
 * @brief Event data payload structure for EventBus notifications.
 */
typedef struct {
    ResourceId_t resource;    /**< The target resource. */
    char owner[16];           /**< Name of the requesting owner module. */
    bool shared;              /**< Whether shared access was granted/requested. */
    int32_t result_code;      /**< Result code of the operation (0 = success). */
} ResourceRegistryEvent_t;

typedef struct IResourceRegistry IResourceRegistry;

/**
 * @brief Service interface structure for ResourceRegistry.
 */
struct IResourceRegistry {
    IService base;  /**< Base service lifecycle interface. */

    /**
     * @brief Initializes the Resource Registry with a custom configuration.
     *
     * @param self Pointer to the IResourceRegistry instance.
     * @param config Configuration options.
     * @param registry Dependency Injection registry.
     * @return 0 on success, negative error code on failure.
     */
    int32_t (*InitializeWithConfig)(IResourceRegistry *self,
                                    ResourceRegistryConfig config,
                                    struct IDIRegistry *registry);

    /**
     * @brief Reserves a hardware resource for a specified owner module.
     *
     * Thread-safe reservation method. Shared access allows multiple clients
     * to reserve the same resource if all request shared mode (e.g. I2C bus).
     *
     * @param self Pointer to the IResourceRegistry instance.
     * @param resource Resource ID (type and numeric ID).
     * @param owner Name of the module reserving the resource.
     * @param shared True if resource can be shared, false for exclusive reservation.
     * Owner names must fit RESOURCE_REGISTRY_OWNER_CAPACITY, including termination.
     * Shared references are tracked for each owner; release must match that owner.
     * @return 0 success, -1 invalid input, -3 conflict, -4 lock/state/capacity failure.
     */
    int32_t (*Reserve)(IResourceRegistry *self, ResourceId_t resource,
                       const char *owner, bool shared);

    /**
     * @brief Checks if a resource is available for reservation.
     *
     * @param self Pointer to the IResourceRegistry instance.
     * @param resource Resource ID to query.
     * @return true if resource is free or available in shared mode, false if locked.
     */
    bool (*IsAvailable)(IResourceRegistry *self, ResourceId_t resource);

    /**
     * @brief Releases a previously reserved hardware resource.
     *
     * Thread-safe release method. Checks owner matching before releasing.
     *
     * @param self Pointer to the IResourceRegistry instance.
     * @param resource Resource ID to release.
     * @param owner Name of the module releasing the resource.
     * @return 0 success, -1 invalid input, -2 not reserved, -3 owner mismatch, -4 lock failure.
     */
    int32_t (*Release)(IResourceRegistry *self, ResourceId_t resource,
                       const char *owner);

    /**
     * @brief Validates current resource registrations for system sanity.
     *
     * @param self Pointer to the IResourceRegistry instance.
     * @return true if all registrations are valid and conflict-free, false otherwise.
     */
    bool (*Validate)(IResourceRegistry *self);

    /**
     * @brief Retrieves the owner name of a currently reserved resource.
     *
     * @param self Pointer to the IResourceRegistry instance.
     * @param resource Resource ID to query.
     * @param out_owner Buffer to receive owner name string (at least 16 bytes).
     * @param max_len Size of out_owner buffer.
     * For shared resources, returns one current owner. Failure preserves output.
     * @return 0 success, -2 not reserved, -1 invalid/short buffer, -4 lock failure.
     */
    int32_t (*GetOwner)(IResourceRegistry *self, ResourceId_t resource,
                        char *out_owner, size_t max_len);

    /**
     * @brief Dumps active resource allocations to standard console output.
     *
     * Format features colored ASCII layout for CLI / Debug Console.
     *
     * @param self Pointer to the IResourceRegistry instance.
     * @return 0 on success, negative error code on failure.
     */
    int32_t (*DumpStatus)(IResourceRegistry *self);

    /**
     * @brief Retrieves current operating statistics of the registry.
     *
     * @param self Pointer to the IResourceRegistry instance.
     * @param out_stats Pointer to structure receiving statistical counters.
     * @return 0 on success, negative error code on invalid args.
     */
    int32_t (*GetStats)(IResourceRegistry *self, ResourceRegistryStats_t *out_stats);

    /**
     * @brief Clears unused maps and statistics; rejects any active reservation.
     *
     * @param self Pointer to the IResourceRegistry instance.
     * @return 0 on success, negative error code on failure; -4 while ownership is active.
     */
    int32_t (*Reset)(IResourceRegistry *self);
};

/**
 * @brief Singleton factory getter for ResourceRegistry service.
 *
 * @return Pointer to IResourceRegistry singleton instance.
 */
IResourceRegistry *ResourceRegistry_GetInstance(void);

#endif // I_RESOURCE_REGISTRY_H

