#ifndef ISYSTEM_CONTROLLER_H
#define ISYSTEM_CONTROLLER_H

#include "Core/IService.h"
#include <stdint.h>

typedef enum {
  SYSTEM_STATE_INITIALIZING,
  SYSTEM_STATE_READY,
  SYSTEM_STATE_SETUP,
  SYSTEM_STATE_ALARM,
  SYSTEM_STATE_MAINTENANCE,
  SYSTEM_STATE_LOW_POWER,
  SYSTEM_STATE_RECOVERING,
  SYSTEM_STATE_ERROR
} SystemState;

typedef struct ISystemController ISystemController;

struct ISystemController {
  IService base;

  void (*SetSystemState)(ISystemController *self, SystemState state);
  SystemState (*GetSystemState)(ISystemController *self);
  void (*HandleSystemEvent)(ISystemController *self, uint32_t eventId);
  /** Queue one delayed shutdown in the existing worker. Success means queue admission. */
  int32_t (*RequestShutdown)(ISystemController *self, uint32_t delay_ms);
  /** Nonblocking copied request for SDK callbacks; success means queue admission. */
  int32_t (*RequestZigbeeTxProfile)(ISystemController *self, uint8_t profile);
};

/**
 * @brief Get the SystemController instance
 */
ISystemController *GetSystemController(void);

#endif // ISYSTEM_CONTROLLER_H
