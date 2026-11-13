#ifndef MENU_MANAGER_CONFIG_H
#define MENU_MANAGER_CONFIG_H

#include "Controllers/MenuManager/MenuManagerTypes.h"
#include <stdint.h>
#include <stdbool.h>

#define MENU_LABEL_CAPACITY 32
#define MENU_CONTROL_TIMEOUT_MS 2000
#define MENU_WORKER_POLL_MS 50
#define MENU_BUTTON_COOLDOWN_MS 200
#define MENU_HISTORY_CAPACITY 8
#define MENU_QUEUE_CAPACITY_MIN 1
#define MENU_QUEUE_CAPACITY_MAX 64
#define MENU_TASK_STACK_MIN_BYTES 4096
#define MENU_TASK_STACK_DEFAULT_BYTES 6144
#define MENU_TASK_STACK_MAX_BYTES 16384

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Applied menu worker and navigation configuration.
 * Button debounce/long press belong to the encoder/pin owners; menu safety
 * cooldown and history capacity are fixed module limits below. Screen sleep
 * belongs to SystemController. Critical actions always require confirmation.
 */
typedef struct {
    uint16_t eventQueueSize;        // Internal event queue capacity
    uint16_t taskStackSize;         // Worker task stack size
    uint8_t taskPriority;           // Worker task priority
    bool circularNavigation;        // Circular navigation between siblings
} MenuManagerConfig;

/**
 * @brief Return the default configuration.
 */
MenuManagerConfig MenuManager_GetDefaultConfig(void);

/**
 * @brief Return the fallback label for a UI state without a plugin.
 */
const char* MenuManager_GetStateLabel(UIState state);

#ifdef __cplusplus
}
#endif

#endif // MENU_MANAGER_CONFIG_H
