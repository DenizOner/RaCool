#ifndef SYSTEM_INITIALIZER_STARTUP_H
#define SYSTEM_INITIALIZER_STARTUP_H

#include "Core/DI/IDIRegistry.h"
#include "Core/IService.h"
#include <stdbool.h>
#include <stdatomic.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

typedef enum {
    STARTUP_STAGE_CORE, STARTUP_STAGE_CONFIGURATION, STARTUP_STAGE_DISPLAY,
    STARTUP_STAGE_ENCODER, STARTUP_STAGE_MENU, STARTUP_STAGE_ZIGBEE,
    STARTUP_STAGE_LEDS, STARTUP_STAGE_FAN, STARTUP_STAGE_INFRARED,
    STARTUP_STAGE_CPU_SENSOR, STARTUP_STAGE_MONITORING,
    STARTUP_STAGE_CONTROLLERS, STARTUP_STAGE_APPLICATION, STARTUP_STAGE_COUNT
} SystemStartupStage;

typedef struct {
    uint32_t activeStages;
    uint32_t startedStages;
    uint32_t completedStages;
    bool failed;
} SystemStartupProgress;

/* Allocation-free display telemetry. Groups sharing a bit start serially;
 * concurrent radio/hardware workers use distinct bits. No service pointers or
 * caller-owned strings escape the startup call. Reset only after workers drain. */
void SystemInitializer_ResetStartupProgress(void);
void SystemInitializer_FailStartupProgress(void);
SystemStartupProgress SystemInitializer_GetStartupProgress(void);

typedef struct {
    IDIRegistry *registry;
    atomic_bool done;
    atomic_bool succeeded;
} SystemInitializerWorker;

/** Initialize/register/start a service; failure never means successful startup.
 * A failed Start retains its registry entry for cleanup/retry. Initialization
 * failures retain the module's own rollback contract. Registration failure
 * stops the newly initialized service; failed cleanup retains module ownership.
 */
bool SystemInitializer_StartService(const char *name, IService *service, IDIRegistry *registry);

/** Context and injected registry remain owned/valid until done, including after timeout.
 * Completion is published only after the worker releases its temporary resources/locks.
 */
bool SystemInitializer_StartWorker(SystemInitializerWorker *worker, TaskFunction_t entry,
                                   const char *name, IDIRegistry *registry);
void SystemInitializer_CompleteWorker(SystemInitializerWorker *worker, bool succeeded);
bool SystemInitializer_WaitWorker(SystemInitializerWorker *worker, uint32_t timeout_ms);

#endif
