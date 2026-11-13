#ifndef I_PIN_MANAGER_H
#define I_PIN_MANAGER_H

#include "Core/DI/IDIRegistry.h"
#include "Core/IService.h"
#include "Hardware/PinManager/PinDefinitions.h"
#include "Hardware/PinManager/PinManagerConfig.h"
#include <driver/gpio.h>
#include <stdbool.h>

/** Typed GPIO mapping, configuration and ISR ownership interface. */

#define PIN_MANAGER_ERR_SDK (-3)
#define PIN_MANAGER_ERR_BUSY (-4)
typedef struct IPinManager IPinManager;

/** ISR callback captures role, level and borrowed context. Retain caller resources until DetachInterrupt succeeds; use only ISR-safe operations. */
typedef void (*PinInterruptHandler_t)(PinFunction_t func, bool level, void *arg);

struct IPinManager {
  IService base;

  /** Initialize validated mappings; active interrupt ownership rejects reconfiguration. */
  int32_t (*InitializeWithConfig)(IPinManager *self, PinManagerConfig config,
                                  IDIRegistry *registry);

  /** Return the mapped GPIO or GPIO_NUM_NC on failure. */
  gpio_num_t (*GetPin)(IPinManager *self, PinFunction_t func);

  /** Change an unowned mapping; return zero or a negative error. */
  int32_t (*SetPin)(IPinManager *self, PinFunction_t func, gpio_num_t pin);

  /** Configure mode and pull; reject active interrupt ownership. */
  int32_t (*Configure)(IPinManager *self, PinFunction_t func, PinMode_t mode,
                       PinPull_t pull);

  /** Capture handler/context only after SDK attachment succeeds. PIN_MANAGER_ERR_SDK may retain failed rollback cleanup: retry DetachInterrupt. PIN_MANAGER_ERR_BUSY preserves another binding: do not detach that owner. */
  int32_t (*AttachInterrupt)(IPinManager *self, PinFunction_t func,
                             PinInterruptType_t type,
                             PinInterruptHandler_t handler, void *arg);

  /** Disable admission and remove the SDK binding. Success requires all captured references to return. Retain caller resources on every failure and retry cleanup. */
  int32_t (*DetachInterrupt)(IPinManager *self, PinFunction_t func);

  /** Set software debounce milliseconds; zero disables it. */
  int32_t (*SetDebounce)(IPinManager *self, PinFunction_t func, uint32_t ms);

  /** Print an owned mapping snapshot outside the state lock. */
  int32_t (*DumpPinMap)(IPinManager *self);

  /** Validate geometry and reservations without reacquiring owned references. */
  bool (*Validate)(IPinManager *self);
};

/** Return the module or NULL if its synchronization resource cannot be created. */
IPinManager *PinManager_GetInstance(void);

#endif // I_PIN_MANAGER_H
