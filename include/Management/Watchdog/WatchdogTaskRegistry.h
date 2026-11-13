/**
 * @file WatchdogTaskRegistry.h
 * @brief Task registration and handle management module for Watchdog Service.
 *
 * Handles adding, removing, searching, and tracking monitored FreeRTOS task handles.
 *
 * @author RaCool DeepMind Team
 * @date 2026-08-01
 */

#ifndef WATCHDOG_TASK_REGISTRY_H
#define WATCHDOG_TASK_REGISTRY_H

#include "Management/Watchdog/WatchdogConfig.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Internal registry structure for monitored task handles.
 */
typedef struct {
    void *registeredTasks[WATCHDOG_MAX_TASKS_LIMIT];
    uint8_t registeredTaskCount;
    uint8_t maxTasks;
} WatchdogTaskRegistry_t;

/**
 * @brief Resets/initializes task registry to empty state.
 * @param registry Pointer to task registry.
 * @param maxTasks Maximum allowed tasks threshold.
 */
void WatchdogTaskRegistry_Init(WatchdogTaskRegistry_t *registry, uint8_t maxTasks);

/**
 * @brief Checks if a task handle is registered.
 * @param registry Pointer to task registry.
 * @param handle Task handle pointer to check.
 * @return true if registered, false otherwise.
 */
bool WatchdogTaskRegistry_Contains(const WatchdogTaskRegistry_t *registry, void *handle);

/**
 * @brief Adds a task handle to the registry.
 * @param registry Pointer to task registry.
 * @param handle Task handle pointer.
 * @return 0 on success, -1 if full or duplicate.
 */
int32_t WatchdogTaskRegistry_Add(WatchdogTaskRegistry_t *registry, void *handle);

/**
 * @brief Removes a task handle from the registry.
 * @param registry Pointer to task registry.
 * @param handle Task handle pointer.
 * @return 0 on success, -1 if not found or NULL.
 */
int32_t WatchdogTaskRegistry_Remove(WatchdogTaskRegistry_t *registry, void *handle);

/**
 * @brief Gets active registered task count.
 * @param registry Pointer to task registry.
 * @return Current task count.
 */
uint32_t WatchdogTaskRegistry_GetCount(const WatchdogTaskRegistry_t *registry);

#ifdef __cplusplus
}
#endif

#endif // WATCHDOG_TASK_REGISTRY_H
