#include "Management/Watchdog/WatchdogWorker.h"
#include "Management/Watchdog/WatchdogConfig.h"
#include "esp_log.h"

int32_t WatchdogWorker_Init(WatchdogWorker *w) {
    if (!w) return -1;
    if (w->gate && w->stopped) return 0;
    w->gate = xSemaphoreCreateMutex();
    if (!w->gate) return -1;
    w->stopped = xQueueCreate(1, sizeof(int32_t));
    if (!w->stopped) {
        vSemaphoreDelete(w->gate);
        w->gate = NULL;
        return -1;
    }
    atomic_init(&w->task, NULL);
    atomic_init(&w->stopRequested, false);
    atomic_init(&w->registered, false);
    atomic_init(&w->registrationFailed, false);
    w->stopPending = false;
    w->watchdog = NULL;
    w->nextRegistrationMs = 0;
    return 0;
}

int32_t WatchdogWorker_Start(WatchdogWorker *w, TaskFunction_t fn, const char *name,
                            uint32_t stack, void *arg, uint32_t priority) {
    if (!w || !w->gate || !fn || !name || !stack) return -1;
    if (xSemaphoreTake(w->gate, pdMS_TO_TICKS(WATCHDOG_WORKER_CONTROL_TIMEOUT_MS)) != pdTRUE) return -1;
    if (atomic_load(&w->task) || w->stopPending) {
        xSemaphoreGive(w->gate);
        return -1;
    }
    atomic_store(&w->stopRequested, false);
    atomic_store(&w->registered, false);
    atomic_store(&w->registrationFailed, false);
    w->watchdog = NULL;
    w->nextRegistrationMs = 0;
    TaskHandle_t task = NULL;
    BaseType_t result = xTaskCreate(fn, name, stack, arg, priority, &task);
    if (result == pdPASS) atomic_store(&w->task, task);
    xSemaphoreGive(w->gate);
    return result == pdPASS && task ? 0 : -1;
}

int32_t WatchdogWorker_Stop(WatchdogWorker *w, TickType_t timeout) {
    if (!w || !w->gate) return -1;
    TaskHandle_t current = xTaskGetCurrentTaskHandle();
    if (atomic_load(&w->task) && atomic_load(&w->task) == current) return -1;
    TickType_t start = xTaskGetTickCount();
    if (xSemaphoreTake(w->gate, timeout) != pdTRUE) return -1;
    TaskHandle_t task = atomic_load(&w->task);
    if (!task) { xSemaphoreGive(w->gate); return 0; }
    if (!w->stopPending) {
        w->stopPending = true;
        atomic_store(&w->stopRequested, true);
    }
    TickType_t elapsed = xTaskGetTickCount() - start;
    TickType_t remaining = elapsed < timeout ? timeout - elapsed : 0;
    int32_t result = -1;
    if (xQueueReceive(w->stopped, &result, remaining) != pdTRUE) {
        /* Keep the generation pending: a late acknowledgment belongs to this
         * request, and a retry must consume it before issuing another one. */
        xSemaphoreGive(w->gate);
        return -1;
    }
    w->stopPending = false;
    if (result == 0) {
        /* The worker acknowledged from a lock-free boundary after deregistration. */
        vTaskDelete(task);
        atomic_store(&w->task, NULL);
        atomic_store(&w->stopRequested, false);
    }
    xSemaphoreGive(w->gate);
    return result;
}

void WatchdogWorker_Checkpoint(WatchdogWorker *w, IWatchdog *available, uint64_t nowMs) {
    if (atomic_load(&w->stopRequested)) {
        int32_t result = 0;
        if (atomic_load(&w->registered)) {
            result = w->watchdog->RemoveTask(w->watchdog, xTaskGetCurrentTaskHandle());
            if (result == 0) atomic_store(&w->registered, false);
        }
        /* Publish failure before acknowledgment, so a subsequent request cannot
         * be overwritten by this generation. Failed deregistration keeps running. */
        if (result != 0) atomic_store(&w->stopRequested, false);
        if (xQueueSend(w->stopped, &result, 0) != pdTRUE) {
            atomic_store(&w->stopRequested, true);
            return;
        }
        if (result == 0) { vTaskSuspend(NULL); return; }
    }
    if (!atomic_load(&w->registered) && available && nowMs >= w->nextRegistrationMs) {
        w->nextRegistrationMs = nowMs + WATCHDOG_WORKER_RETRY_MS;
        bool ready = available->base.GetState((IService *)available) == SERVICE_STATE_STARTED;
        int32_t result = ready ? available->AddTask(available, NULL, 0) : -1;
        if (result == 0) {
            w->watchdog = available;
            atomic_store(&w->registered, true);
            atomic_store(&w->registrationFailed, false);
        } else if (!atomic_exchange(&w->registrationFailed, true)) {
            ESP_LOGW("WatchdogWorker", "Task registration pending or failed; will retry");
        }
    }
    if (atomic_load(&w->registered)) w->watchdog->Feed(w->watchdog);
}

bool WatchdogWorker_Healthy(const WatchdogWorker *w) {
    return atomic_load(&w->task) != NULL && !atomic_load(&w->registrationFailed) &&
           !atomic_load(&w->stopRequested);
}
