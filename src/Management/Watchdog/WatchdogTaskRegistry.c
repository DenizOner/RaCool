/**
 * @file WatchdogTaskRegistry.c
 * @brief Task registration and handle management implementation for Watchdog.
 *
 * @author RaCool DeepMind Team
 * @date 2026-08-01
 */

#include "Management/Watchdog/WatchdogTaskRegistry.h"
#include <string.h>

void WatchdogTaskRegistry_Init(WatchdogTaskRegistry_t *registry, uint8_t maxTasks) {
    if (!registry) return;
    memset(registry->registeredTasks, 0, sizeof(registry->registeredTasks));
    registry->registeredTaskCount = 0;
    registry->maxTasks = (maxTasks > 0 && maxTasks <= WATCHDOG_MAX_TASKS_LIMIT) ? maxTasks : WATCHDOG_MAX_TASKS;
}

bool WatchdogTaskRegistry_Contains(const WatchdogTaskRegistry_t *registry, void *handle) {
    if (!registry) return false;
    for (uint8_t i = 0; i < registry->registeredTaskCount; i++) {
        if (registry->registeredTasks[i] == handle) {
            return true;
        }
    }
    return false;
}

int32_t WatchdogTaskRegistry_Add(WatchdogTaskRegistry_t *registry, void *handle) {
    if (!registry) return -1;
    if (registry->registeredTaskCount >= registry->maxTasks) {
        return -1;
    }
    if (WatchdogTaskRegistry_Contains(registry, handle)) {
        return -1;
    }
    registry->registeredTasks[registry->registeredTaskCount++] = handle;
    return 0;
}

int32_t WatchdogTaskRegistry_Remove(WatchdogTaskRegistry_t *registry, void *handle) {
    if (!registry || !handle) return -1;
    for (uint8_t i = 0; i < registry->registeredTaskCount; i++) {
        if (registry->registeredTasks[i] == handle) {
            registry->registeredTasks[i] = registry->registeredTasks[registry->registeredTaskCount - 1];
            registry->registeredTasks[registry->registeredTaskCount - 1] = NULL;
            registry->registeredTaskCount--;
            return 0;
        }
    }
    return -1;
}

uint32_t WatchdogTaskRegistry_GetCount(const WatchdogTaskRegistry_t *registry) {
    if (!registry) return 0;
    return registry->registeredTaskCount;
}
