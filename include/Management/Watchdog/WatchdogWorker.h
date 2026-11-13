#ifndef WATCHDOG_WORKER_H
#define WATCHDOG_WORKER_H

#include "Management/Watchdog/IWatchdog.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <stdatomic.h>

/* One existing service task, no additional worker. Init is called by the
 * service initializer before publishing the service. Control resources remain
 * allocated across Stop/Start. The worker calls Checkpoint with no locks held.
 * Start/Stop serialize on gate; the worker never takes gate. */
typedef struct {
    SemaphoreHandle_t gate;
    QueueHandle_t stopped;
    _Atomic(TaskHandle_t) task;
    atomic_bool stopRequested;
    atomic_bool registered;
    atomic_bool registrationFailed;
    bool stopPending; /* Parent-owned under gate; survives a Stop timeout. */
    IWatchdog *watchdog; /* Worker-owned until successful stop acknowledgment. */
    uint64_t nextRegistrationMs;
} WatchdogWorker;

int32_t WatchdogWorker_Init(WatchdogWorker *worker);
int32_t WatchdogWorker_Start(WatchdogWorker *worker, TaskFunction_t task,
    const char *name, uint32_t stackSize, void *arg, uint32_t priority);
int32_t WatchdogWorker_Stop(WatchdogWorker *worker, TickType_t timeout);
/* Call before first work and each loop iteration, outside all resource locks.
 * Stop completion parks the worker until its owner deletes it. Registration
 * failure leaves normal service work running, but Healthy returns false. */
void WatchdogWorker_Checkpoint(WatchdogWorker *worker, IWatchdog *available, uint64_t nowMs);
bool WatchdogWorker_Healthy(const WatchdogWorker *worker);

#endif
