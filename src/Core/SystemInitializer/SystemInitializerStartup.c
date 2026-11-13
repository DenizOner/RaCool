#include "Core/SystemInitializer/SystemInitializerStartup.h"
#include "Core/ServiceLocator/ServiceLocator.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "Core/SystemInitializer/SystemInitializerConfig.h"
#include "Core/ServiceLocator/ServiceNames.h"
#include <string.h>

_Static_assert(STARTUP_STAGE_COUNT < 32, "Startup stages must fit in an atomic mask");
static atomic_uint active_startup_stages;
static atomic_bool startup_progress_failed;
static atomic_uint started_startup_stages;
static atomic_uint completed_startup_stages;

void SystemInitializer_ResetStartupProgress(void) {
    atomic_store(&active_startup_stages, 0);
    atomic_store(&started_startup_stages, 0);
    atomic_store(&completed_startup_stages, 0);
    atomic_store(&startup_progress_failed, false);
}
void SystemInitializer_FailStartupProgress(void) {
    atomic_store(&startup_progress_failed, true);
}
SystemStartupProgress SystemInitializer_GetStartupProgress(void) {
    return (SystemStartupProgress){
        .activeStages=atomic_load(&active_startup_stages),
        .startedStages=atomic_load(&started_startup_stages),
        .completedStages=atomic_load(&completed_startup_stages),
        .failed=atomic_load(&startup_progress_failed)};
}
static uint32_t StartupStageMask(const char *name) {
    static const struct { const char *name; SystemStartupStage stage; } stages[] = {
        {SVC_NAME_CONFIGURATION_MANAGER, STARTUP_STAGE_CONFIGURATION},
        {SVC_NAME_OLED, STARTUP_STAGE_DISPLAY}, {SVC_NAME_ROTARY_ENCODER, STARTUP_STAGE_ENCODER},
        {SVC_NAME_MENU_MANAGER, STARTUP_STAGE_MENU}, {SVC_NAME_ZIGBEE, STARTUP_STAGE_ZIGBEE},
        {SVC_NAME_BUILTIN_LED, STARTUP_STAGE_LEDS}, {SVC_NAME_RGB_LED, STARTUP_STAGE_LEDS},
        {SVC_NAME_RELAY, STARTUP_STAGE_FAN}, {SVC_NAME_IR_TEMP_SENSOR, STARTUP_STAGE_INFRARED},
        {SVC_NAME_INTERNAL_TEMP_SENSOR, STARTUP_STAGE_CPU_SENSOR},
        {SVC_NAME_WATCHDOG, STARTUP_STAGE_MONITORING}, {SVC_NAME_SECURITY_MANAGER, STARTUP_STAGE_MONITORING},
        {SVC_NAME_DIAGNOSTICS_MANAGER, STARTUP_STAGE_MONITORING},
        {SVC_NAME_SYSTEM_CONTROLLER, STARTUP_STAGE_CONTROLLERS},
        {SVC_NAME_CLIMATE_CONTROLLER, STARTUP_STAGE_CONTROLLERS},
        {SVC_NAME_APPLICATION_MANAGER, STARTUP_STAGE_APPLICATION},
    };
    for (unsigned i=0; i<sizeof(stages)/sizeof(stages[0]); ++i)
        if (strcmp(name, stages[i].name)==0) return 1U << stages[i].stage;
    return 1U << STARTUP_STAGE_CORE;
}
static bool EndStartupStage(uint32_t stage, bool succeeded) {
    if (!succeeded) SystemInitializer_FailStartupProgress();
    else atomic_fetch_or(&completed_startup_stages, stage);
    atomic_fetch_and(&active_startup_stages, ~stage);
    return succeeded;
}

bool SystemInitializer_StartService(const char *name, IService *service, IDIRegistry *registry) {
    if (!name || !*name || !service) return false;
    uint32_t stage = StartupStageMask(name);
    atomic_fetch_or(&started_startup_stages, stage);
    atomic_fetch_or(&active_startup_stages, stage);
    ServiceState previous = service->GetState ? service->GetState(service) : SERVICE_STATE_UNINITIALIZED;
    bool initialized_here = previous == SERVICE_STATE_UNINITIALIZED ||
        previous == SERVICE_STATE_STOPPED || previous == SERVICE_STATE_ERROR;
    esp_rom_printf("[InitService] Attempting to start: %s\n", name);
    int32_t result = IService_SafeInitialize(service, registry);
    if (result != 0) {
        esp_rom_printf("!!! [InitService] FAILED Initialize for %s (Res: %ld) !!!\n", name, (long)result);
        return EndStartupStage(stage, false);
    }
    result = ServiceLocator_RegisterService(name, service);
    if (result != 0) {
        esp_rom_printf("!!! [InitService] FAILED Register for %s (Res: %ld) !!!\n", name, (long)result);
        if (initialized_here && service->Stop) {
            int32_t cleanup = service->Stop(service);
            if (cleanup != 0) ESP_LOGE("SystemInitializer", "Registration cleanup retained for %s: %ld", name, (long)cleanup);
        }
        return EndStartupStage(stage, false);
    }
    esp_rom_printf("[InitService] Invoking Start for %s...\n", name);
    result = IService_SafeStart(service);
    if (result != 0) {
        esp_rom_printf("!!! [InitService] FAILED Start for %s (Res: %ld) !!!\n", name, (long)result);
        /* Keep the registered owner discoverable until its own cleanup succeeds. */
        return EndStartupStage(stage, false);
    }
    esp_rom_printf("[InitService] SUCCESS: %s started\n", name);
    return EndStartupStage(stage, true);
}

bool SystemInitializer_StartWorker(SystemInitializerWorker *worker, TaskFunction_t entry,
                                   const char *name, IDIRegistry *registry) {
    if (!worker || !entry || !name || !*name) return false;
    bool available = true;
    if (!atomic_compare_exchange_strong(&worker->done, &available, false)) return false;
    worker->registry = registry;
    atomic_store(&worker->succeeded, false);
    if (xTaskCreate(entry, name, SYSTEM_INITIALIZER_WORKER_STACK_BYTES, worker,
                    SYSTEM_INITIALIZER_WORKER_PRIORITY, NULL) != pdPASS) {
        worker->registry = NULL;
        atomic_store(&worker->done, true);
        return false;
    }
    return true;
}

void SystemInitializer_CompleteWorker(SystemInitializerWorker *worker, bool succeeded) {
    if (!worker) return;
    worker->registry = NULL;
    atomic_store(&worker->succeeded, succeeded);
    atomic_store(&worker->done, true);
}

bool SystemInitializer_WaitWorker(SystemInitializerWorker *worker, uint32_t timeout_ms) {
    if (!worker || !timeout_ms) return false;
    TickType_t timeout = pdMS_TO_TICKS(timeout_ms);
    if (!timeout) timeout = 1;
    TickType_t started = xTaskGetTickCount();
    while (!atomic_load(&worker->done)) {
        if ((TickType_t)(xTaskGetTickCount() - started) >= timeout) return false;
        vTaskDelay(1);
    }
    return atomic_load(&worker->succeeded);
}
