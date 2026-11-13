#include "Controllers/ClimateController/FanCommands.h"
/**
 * @file ApplicationManager.c
 * @brief Thread-safe, event-driven async implementation of the ApplicationManager facade.
 *
 * Serialized lifecycle control with cooperative worker shutdown and owned
 * event subscriptions. In-flight actions finish before worker release.
 *
 * @author RaCool Team
 * @date 2026
 */

#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "Communication/Zigbee/IZigbee.h"
#include "Controllers/ClimateController/IClimateController.h"
#include "Controllers/MenuManager/IMenuManager.h"
#include "Controllers/UIController/IUIController.h"
#include "Core/DI/IDIRegistry.h"
#include "Core/EventBus/IEventBus.h"
#include "Core/IService.h"
#include "Core/ServiceLocator/ServiceLocator.h"
#include "Core/ServiceLocator/ServiceNames.h"
#include "Hardware/Relay/IRelay.h"
#include "Hardware/RotaryEncoder/IRotaryEncoder.h"
#include "Hardware/RotaryEncoder/RotaryEncoderEvents.h"
#include "Management/ApplicationManager/ApplicationManagerConfig.h"
#include "Management/ApplicationManager/IApplicationManager.h"
#include "Management/ApplicationManager/FactoryReset.h"
#include "Management/ConfigurationManager/IConfigurationManager.h"
#include "Management/ConfigurationManager/SystemConfigs.h"
#include "Management/DiagnosticsManager/IDiagnosticsManager.h"
#include "Management/Watchdog/IWatchdog.h"
#include "Management/Watchdog/WatchdogWorker.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "AppManager";

/**
 * @brief Private implementation struct for ApplicationManager.
 */
typedef struct {
    IApplicationManager base;
    _Atomic(ServiceState) state;
    WatchdogWorker worker;
    TaskHandle_t taskHandle;
    QueueHandle_t eventQueue;
    SemaphoreHandle_t mutex;
    SemaphoreHandle_t controlMutex;
    IEventBus *ownedBus;
    uint8_t subscriptions;
    IRotaryEncoder *ownedEncoder;
    atomic_bool encoderBindingFailed;
    atomic_bool factoryResetBusy;
    uint32_t factoryResetId;
    uint32_t factoryResetGeneration; /* Local admission token, never persisted. */
    FactoryResetOperation factoryReset;

    // Diagnostics counters
    uint32_t processedEvents;
    uint32_t droppedEvents;
    uint32_t startTimeMs;

    // Service dependencies
    IClimateController *climateCtrl;
    IUIController *uiCtrl;
    IRotaryEncoder *encoder;
    IZigbee *zigbee;
    IRelay *relay;
    IMenuManager *menu;
    IDiagnosticsManager *diag;
    IWatchdog *watchdog;
} ApplicationManagerImpl;

static ApplicationManagerImpl appManager;
static portMUX_TYPE factoryStateMux = portMUX_INITIALIZER_UNLOCKED;

/**
 * @brief Helper to obtain MenuManager instance safely.
 */
static IMenuManager *App_GetMenuManager(void) {
    if (!appManager.menu) {
        appManager.menu = (IMenuManager *)ServiceLocator_GetService(SVC_NAME_MENU_MANAGER);
    }
    if (!appManager.menu || !appManager.menu->base.GetState || appManager.menu->base.GetState((IService *)appManager.menu) != SERVICE_STATE_STARTED) {
        return NULL;
    }
    return appManager.menu;
}

/**
 * @brief Thread-safe posting of internal events.
 */
static esp_err_t App_PostEventOwned(IApplicationManager *self, const AppManagerEvent *event) {
    (void)self;
    if (!event || event->id == APP_EVENT_NONE || !appManager.eventQueue) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(appManager.mutex, pdMS_TO_TICKS(APP_MUTEX_TIMEOUT_MS)) != pdTRUE) return ESP_ERR_TIMEOUT;
    if (appManager.state != SERVICE_STATE_STARTED || !atomic_load(&appManager.worker.task)) {
        xSemaphoreGive(appManager.mutex);
        return ESP_ERR_INVALID_STATE;
    }

    bool factory = event->id == APP_EVENT_FACTORY_RESET;
    if (factory && atomic_load(&appManager.factoryResetBusy)) {
        xSemaphoreGive(appManager.mutex);
        return ESP_ERR_NOT_FINISHED;
    }
    FactoryResetOperation previous_operation = {0};
    uint32_t previous_id = 0;
    if (factory) {
        portENTER_CRITICAL(&factoryStateMux);
        previous_operation = appManager.factoryReset;
        previous_id = appManager.factoryResetId;
        ++appManager.factoryResetGeneration;
        if (++appManager.factoryResetId == 0) ++appManager.factoryResetId;
        appManager.factoryReset = (FactoryResetOperation){
            .started_ms = (uint32_t)(esp_timer_get_time() / 1000)};
        atomic_store(&appManager.factoryResetBusy, true);
        portEXIT_CRITICAL(&factoryStateMux);
    }

    if (xQueueSend(appManager.eventQueue, event, pdMS_TO_TICKS(APP_EVENT_QUEUE_SEND_TIMEOUT_MS)) == pdPASS) {
        xSemaphoreGive(appManager.mutex);
        return ESP_OK;
    }

    appManager.droppedEvents++;
    if (factory) {
        portENTER_CRITICAL(&factoryStateMux);
        ++appManager.factoryResetGeneration;
        appManager.factoryReset = previous_operation;
        appManager.factoryResetId = previous_id;
        atomic_store(&appManager.factoryResetBusy, false);
        portEXIT_CRITICAL(&factoryStateMux);
    }
    xSemaphoreGive(appManager.mutex);
    ESP_LOGW(TAG, "Event queue full! Dropped event ID: %d", event->id);
    return ESP_ERR_TIMEOUT;
}

static esp_err_t App_PostEventImpl(IApplicationManager *self, const AppManagerEvent *event) {
    if (self != &appManager.base || !event || event->id <= APP_EVENT_NONE || event->id > APP_EVENT_FACTORY_RESET || !appManager.eventQueue)
        return ESP_ERR_INVALID_ARG;
    /* These legacy actions have no implementation. Queue acceptance must not
     * advertise a working temperature controller or telemetry request. */
    if (event->id == APP_EVENT_SET_TARGET_TEMP || event->id == APP_EVENT_TRIGGER_TELEMETRY)
        return ESP_ERR_NOT_SUPPORTED;
    if (!appManager.controlMutex || xSemaphoreTake(appManager.controlMutex, 0) != pdTRUE) return ESP_ERR_TIMEOUT;
    esp_err_t result = App_PostEventOwned(self, event);
    xSemaphoreGive(appManager.controlMutex);
    return result;
}

/**
 * @brief Rotation direct callback pushing event into async queue.
 */
static void App_OnRotationDirect(RotaryEncoderRotationEvent event, void *ctx) {
    (void)ctx;
    AppManagerEvent appEvt = {
        .id = APP_EVENT_ROTATION,
        .payload = { .rawValue = event.steps },
        .timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000)
    };
    App_PostEventImpl(&appManager.base, &appEvt);
}

static esp_err_t App_BindEncoderRotation(void) {
    if (appManager.state != SERVICE_STATE_STARTED) return ESP_ERR_INVALID_STATE;
    IRotaryEncoder *encoder = appManager.encoder;
    if (!encoder) return ESP_OK;
    if (!encoder->OnRotation || !encoder->UnsubscribeRotation) {
        atomic_store(&appManager.encoderBindingFailed, true);
        return ESP_ERR_NOT_SUPPORTED;
    }
    esp_err_t result = encoder->OnRotation(encoder, App_OnRotationDirect, NULL);
    atomic_store(&appManager.encoderBindingFailed, result != ESP_OK);
    if (result == ESP_OK) appManager.ownedEncoder = encoder;
    return result;
}

static int32_t App_ReleaseEncoderRotation(void) {
    IRotaryEncoder *encoder = appManager.ownedEncoder;
    if (!encoder) return 0;
    if (!encoder->UnsubscribeRotation ||
        encoder->UnsubscribeRotation(encoder, App_OnRotationDirect) != ESP_OK) return -1;
    appManager.ownedEncoder = NULL;
    return 0;
}

/**
 * @brief EventBus Button Click handler pushing event into async queue.
 */
static void App_OnButtonClick(BusEvent event) {
    (void)event;
    AppManagerEvent appEvt = {
        .id = APP_EVENT_BUTTON_CLICK,
        .payload = { .rawValue = 1 },
        .timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000)
    };
    App_PostEventImpl(&appManager.base, &appEvt);
}

/**
 * @brief EventBus Button Long Press handler pushing event into async queue.
 */
static void App_OnLongPress(BusEvent event) {
    uint32_t duration = 0;
    if (event.data && event.dataSize == sizeof(RotaryEncoderButtonEvent)) {
        RotaryEncoderButtonEvent *btn = (RotaryEncoderButtonEvent *)event.data;
        duration = (uint32_t)btn->duration_ms;
    }
    AppManagerEvent appEvt = {
        .id = APP_EVENT_BUTTON_LONGPRESS,
        .payload = { .durationMs = duration },
        .timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000)
    };
    App_PostEventImpl(&appManager.base, &appEvt);
}

/* The configuration and Zigbee SDK owners retain their independent commit
 * semantics. A failed settings reset must not request a network leave. */
static uint32_t App_FactoryResetClockMs(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

static void App_BeginFactoryReset(void) {
    IConfigurationManager *config = (IConfigurationManager *)ServiceLocator_GetService(SVC_NAME_CONFIGURATION_MANAGER);
    IZigbee *radio = appManager.zigbee;
    if (!radio) radio = (IZigbee *)ServiceLocator_GetService(SVC_NAME_ZIGBEE);
    IPowerManager *power = (IPowerManager *)ServiceLocator_GetService(SVC_NAME_POWER_MANAGER);
    if (xSemaphoreTake(appManager.mutex, pdMS_TO_TICKS(APP_MUTEX_TIMEOUT_MS)) != pdTRUE) return;
    if (atomic_load(&appManager.factoryResetBusy) && appManager.factoryReset.phase == FACTORY_RESET_IDLE) {
        portENTER_CRITICAL(&factoryStateMux);
        appManager.factoryReset.config = config;
        appManager.factoryReset.power = power;
        appManager.factoryReset.radio = radio;
        appManager.factoryReset.ClockMs = App_FactoryResetClockMs;
        appManager.factoryReset.phase = FACTORY_RESET_PREFLIGHT;
        portEXIT_CRITICAL(&factoryStateMux);
    }
    xSemaphoreGive(appManager.mutex);
}

static void App_AdvanceFactoryReset(void) {
    if (!atomic_load(&appManager.factoryResetBusy)) return;
    portENTER_CRITICAL(&factoryStateMux);
    FactoryResetOperation operation = appManager.factoryReset;
    uint32_t id = appManager.factoryResetId;
    uint32_t generation = appManager.factoryResetGeneration;
    portEXIT_CRITICAL(&factoryStateMux);
    FactoryResetPhase previous = operation.phase;
    if (operation.phase == FACTORY_RESET_IDLE &&
        (uint32_t)((uint32_t)(esp_timer_get_time() / 1000) - operation.started_ms) >= APP_FACTORY_RESET_TIMEOUT_MS) {
        operation.phase = FACTORY_RESET_FAILED;
        operation.error = ESP_ERR_TIMEOUT;
    }
    FactoryReset_Advance(&operation, (uint32_t)(esp_timer_get_time() / 1000));
    bool release = operation.phase == FACTORY_RESET_FAILED && operation.network_receipt == 0;
    if (operation.phase == FACTORY_RESET_FAILED && operation.network_receipt != 0) {
        ZigbeeFactoryResetStatus status;
        /* A timeout must not abandon SDK ownership. A late terminal callback
         * can release admission, but can never turn this failure into restart. */
        if (operation.radio->GetFactoryResetStatus(operation.radio, &status) == ESP_OK &&
            status.operation_id == operation.network_receipt && !status.pending) release = true;
    }
    if (release && operation.guard_held) {
        if (operation.config->SetFactoryResetGuard(operation.config, false) != ESP_OK) release = false;
        else operation.guard_held = false;
    }
    portENTER_CRITICAL(&factoryStateMux);
    if (generation != appManager.factoryResetGeneration || !atomic_load(&appManager.factoryResetBusy)) {
        portEXIT_CRITICAL(&factoryStateMux);
        return;
    }
    appManager.factoryReset = operation;
    if (release) atomic_store(&appManager.factoryResetBusy, false);
    portEXIT_CRITICAL(&factoryStateMux);
    if (operation.phase != previous) {
        ESP_LOGI(TAG, "[FACTORY_RESET] id=%lu phase=%u error=%ld",
                 (unsigned long)id, (unsigned)operation.phase, (long)operation.error);
    }
}

/**
 * @brief Internal event handler executing queue events asynchronously.
 */
static void App_ProcessEvent(const AppManagerEvent *evt) {
    IMenuManager *menu = App_GetMenuManager();

    switch (evt->id) {
        case APP_EVENT_ROTATION: {
            // UIController alone owns rotation activity and its sleeping-screen policy.
            // The direct observer must not refresh activity and indirectly wake OLED.
            ESP_LOGD(TAG, "Processing ROTATION: steps=%ld", (long)evt->payload.rawValue);
            break;
        }

        case APP_EVENT_BUTTON_CLICK: {
            ESP_LOGI(TAG, "Processing BUTTON_CLICK");
            if (menu && menu->UpdateActivity) {
                menu->UpdateActivity(menu);
            }
            break;
        }



        case APP_EVENT_BUTTON_LONGPRESS: {
            ESP_LOGI(TAG, "Processing LONG_PRESS (%ld ms)", (long)evt->payload.durationMs);
            /* Physical long press belongs to UI pairing navigation. Factory
             * reset is separately admitted by the asleep-start UI hold or explicit menu/CLI paths. */
            break;
        }

        case APP_EVENT_SET_TARGET_TEMP: {
            ESP_LOGI(TAG, "Processing SET_TARGET_TEMP: %.1f C", evt->payload.targetTemperature);
            break;
        }

        case APP_EVENT_TOGGLE_RELAY: {
            ESP_LOGI(TAG, "Processing TOGGLE_RELAY: desiredState=%d", evt->payload.relayState);
            IConfigurationManager *owner=ConfigurationManager_GetInstance();
            esp_err_t result=FanCommands_Dispatch(owner,(FanCommand){FAN_COMMAND_MODE,
                evt->payload.relayState ? CLIMATE_FAN_MANUAL_ON : CLIMATE_FAN_MANUAL_OFF,0},false);
            if (result != ESP_OK) ESP_LOGE(TAG, "Relay request failed: %ld", (long)result);
            break;
        }

        case APP_EVENT_SCREEN_WAKE: {
            ESP_LOGI(TAG, "Processing SCREEN_WAKE request");
            if (menu && menu->UpdateActivity) {
                menu->UpdateActivity(menu);
            }
            break;
        }

        case APP_EVENT_TRIGGER_TELEMETRY: {
            ESP_LOGI(TAG, "Processing TRIGGER_TELEMETRY");
            break;
        }

        case APP_EVENT_FACTORY_RESET: {
            ESP_LOGW(TAG, "Processing FACTORY_RESET request");
            App_BeginFactoryReset();
            break;
        }

        default:
            ESP_LOGW(TAG, "Unknown event ID: %d", evt->id);
            break;
    }
}

static bool App_DispatchQueuedEvent(const AppManagerEvent *event) {
    /* Stop closes admission before requesting the worker checkpoint. A failed
     * watchdog deregistration may keep the worker alive, but must not admit
     * queued relay or factory-reset actions while closing. */
    if (appManager.state != SERVICE_STATE_STARTED) return false;
    App_ProcessEvent(event);
    if (xSemaphoreTake(appManager.mutex, pdMS_TO_TICKS(APP_MUTEX_TIMEOUT_MS)) == pdTRUE) {
        appManager.processedEvents++;
        xSemaphoreGive(appManager.mutex);
    }
    return true;
}

/**
 * @brief Main event-driven application task.
 */
static void App_Task(void *pvParameters) {
    (void)pvParameters;
    ESP_LOGI(TAG, "Application Facade Task Started (Event Driven)");

    int retries = 0;
    while (retries < APP_SERVICE_INIT_MAX_RETRIES) {
        WatchdogWorker_Checkpoint(&appManager.worker,
            (IWatchdog *)ServiceLocator_GetService(SVC_NAME_WATCHDOG),
            (uint64_t)esp_timer_get_time() / 1000ULL);
        appManager.encoder = (IRotaryEncoder *)ServiceLocator_GetService(SVC_NAME_ROTARY_ENCODER);
        appManager.zigbee = (IZigbee *)ServiceLocator_GetService(SVC_NAME_ZIGBEE);
        appManager.relay = (IRelay *)ServiceLocator_GetService(SVC_NAME_RELAY);
        appManager.climateCtrl = (IClimateController *)ServiceLocator_GetService(SVC_NAME_CLIMATE_CONTROLLER);
        appManager.uiCtrl = (IUIController *)ServiceLocator_GetService(SVC_NAME_UI_CONTROLLER);
        appManager.diag = (IDiagnosticsManager *)ServiceLocator_GetService(SVC_NAME_DIAGNOSTICS_MANAGER);
        appManager.watchdog = (IWatchdog *)ServiceLocator_GetService(SVC_NAME_WATCHDOG);
        appManager.menu = (IMenuManager *)ServiceLocator_GetService(SVC_NAME_MENU_MANAGER);

        if (appManager.encoder && appManager.zigbee && appManager.relay &&
            appManager.climateCtrl && appManager.uiCtrl && appManager.diag &&
            appManager.watchdog && appManager.menu &&
            appManager.menu->base.GetState((IService *)appManager.menu) == SERVICE_STATE_STARTED) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(APP_SERVICE_INIT_RETRY_DELAY_MS));
        retries++;
    }

    appManager.menu = App_GetMenuManager();

    WatchdogWorker_Checkpoint(&appManager.worker, appManager.watchdog,
                             (uint64_t)esp_timer_get_time() / 1000ULL);

    esp_err_t bindingResult = App_BindEncoderRotation();
    if (bindingResult != ESP_OK && bindingResult != ESP_ERR_INVALID_STATE)
        ESP_LOGE(TAG, "Rotation callback registration failed: %s", esp_err_to_name(bindingResult));

    AppManagerEvent event;
    while (1) {
        WatchdogWorker_Checkpoint(&appManager.worker,
            (IWatchdog *)ServiceLocator_GetService(SVC_NAME_WATCHDOG),
            (uint64_t)esp_timer_get_time() / 1000ULL);
        // Wait for incoming async events (Event-Driven block, no high CPU polling!)
        uint32_t wait_ms = atomic_load(&appManager.factoryResetBusy) ? APP_FACTORY_RESET_POLL_MS : 1000U;
        if (xQueueReceive(appManager.eventQueue, &event, pdMS_TO_TICKS(wait_ms)) == pdPASS) {
            App_DispatchQueuedEvent(&event);
        }
        App_AdvanceFactoryReset();
    }
}

static esp_err_t App_GetStatusImpl(IApplicationManager *self, AppManagerStatus *status) {
    if (self != &appManager.base || !status) return ESP_ERR_INVALID_ARG;
    if (!appManager.mutex) return ESP_ERR_INVALID_STATE;

    if (xSemaphoreTake(appManager.mutex, pdMS_TO_TICKS(APP_MUTEX_TIMEOUT_MS)) == pdTRUE) {
        status->state = appManager.state;
        status->processedEventsCount = appManager.processedEvents;
        status->droppedEventsCount = appManager.droppedEvents;
        status->queueCapacity = APP_EVENT_QUEUE_SIZE;
        status->queueCurrentUsage = appManager.eventQueue ? (uint32_t)uxQueueMessagesWaiting(appManager.eventQueue) : 0;
        status->uptimeMs = (uint32_t)(esp_timer_get_time() / 1000) - appManager.startTimeMs;
        status->isWatchdogRegistered = atomic_load(&appManager.worker.registered);
        portENTER_CRITICAL(&factoryStateMux);
        status->factoryResetId = appManager.factoryResetId;
        status->factoryResetPhase = (uint32_t)appManager.factoryReset.phase;
        status->factoryResetError = appManager.factoryReset.error;
        status->factoryResetStartedMs = appManager.factoryReset.started_ms;
        status->factoryResetBusy = atomic_load(&appManager.factoryResetBusy);
        portEXIT_CRITICAL(&factoryStateMux);
        xSemaphoreGive(appManager.mutex);
        return ESP_OK;
    }
    return ESP_ERR_TIMEOUT;
}

static esp_err_t App_TriggerFactoryResetImpl(IApplicationManager *self) {
    AppManagerEvent evt = {
        .id = APP_EVENT_FACTORY_RESET,
        .payload = { .rawValue = 0 },
        .timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000)
    };
    return App_PostEventImpl(self, &evt);
}

static int32_t App_Initialize(IService *self, IDIRegistry *registry) {
    (void)registry;
    if (appManager.state == SERVICE_STATE_ERROR) return -1;
    if (appManager.state >= SERVICE_STATE_INITIALIZED) return 0;

    if (!appManager.mutex) {
        appManager.mutex = xSemaphoreCreateMutex();
    }
    if (!appManager.eventQueue) {
        appManager.eventQueue = xQueueCreate(APP_EVENT_QUEUE_SIZE, sizeof(AppManagerEvent));
    }

    if (!appManager.controlMutex) appManager.controlMutex = xSemaphoreCreateMutex();
    if (!appManager.mutex || !appManager.eventQueue || !appManager.controlMutex || WatchdogWorker_Init(&appManager.worker) != 0) {
        if (appManager.controlMutex) { vSemaphoreDelete(appManager.controlMutex); appManager.controlMutex = NULL; }
        if (appManager.eventQueue) { vQueueDelete(appManager.eventQueue); appManager.eventQueue = NULL; }
        if (appManager.mutex) { vSemaphoreDelete(appManager.mutex); appManager.mutex = NULL; }
        return -1;
    }

    appManager.startTimeMs = (uint32_t)(esp_timer_get_time() / 1000);
    appManager.processedEvents = 0;
    appManager.droppedEvents = 0;

    appManager.state = SERVICE_STATE_INITIALIZED;
    return 0;
}

static const char *appEventNames[] = {RE_EVENT_BUTTON_CLICK, RE_EVENT_BUTTON_LONGPRESS};
static BusEventHandler appEventHandlers[] = {App_OnButtonClick, App_OnLongPress};

static int32_t App_ReleaseSubscriptions(void) {
    for (unsigned i = 0; i < sizeof(appEventNames) / sizeof(appEventNames[0]); ++i) {
        if (!(appManager.subscriptions & (1U << i))) continue;
        IEventBus *bus = appManager.ownedBus;
        if (!bus || !bus->Unsubscribe || bus->Unsubscribe(bus, appEventNames[i], appEventHandlers[i]) != 0) return -1;
        appManager.subscriptions &= ~(1U << i);
    }
    appManager.ownedBus = NULL;
    return 0;
}

static int32_t App_StartOwned(IService *self) {
    (void)self;
    if (appManager.state == SERVICE_STATE_STARTED)
        return WatchdogWorker_Healthy(&appManager.worker) ? 0 : -1;
    ServiceState previous = appManager.state;
    if (previous != SERVICE_STATE_INITIALIZED && previous != SERVICE_STATE_STOPPED) return -1;
    IEventBus *bus = ServiceLocator_GetEventBus();
    if (bus) {
        appManager.ownedBus = bus;
        for (unsigned i = 0; i < sizeof(appEventNames) / sizeof(appEventNames[0]); ++i) {
            if (!bus->Subscribe || !bus->Unsubscribe || bus->Subscribe(bus, appEventNames[i], appEventHandlers[i]) != 0) {
                if (App_ReleaseSubscriptions() != 0) appManager.state = SERVICE_STATE_ERROR;
                return -1;
            }
            appManager.subscriptions |= (1U << i);
        }
    }
    appManager.state = SERVICE_STATE_STARTED;
    atomic_store(&appManager.encoderBindingFailed, false);
    if (WatchdogWorker_Start(&appManager.worker, App_Task, APP_TASK_NAME, APP_TASK_STACK_SIZE, NULL,
                             APP_TASK_PRIORITY) != 0) {
        appManager.state = App_ReleaseSubscriptions() == 0 ? previous : SERVICE_STATE_ERROR;
        return -1;
    }
    appManager.taskHandle = atomic_load(&appManager.worker.task);
    return 0;
}

static int32_t App_StopOwned(IService *self) {
    (void)self;
    if (atomic_load(&appManager.factoryResetBusy)) return -1;
    appManager.state = SERVICE_STATE_ERROR;
    if (WatchdogWorker_Stop(&appManager.worker, pdMS_TO_TICKS(WATCHDOG_WORKER_CONTROL_TIMEOUT_MS)) != 0) return -1;
    appManager.taskHandle = NULL;
    if (App_ReleaseEncoderRotation() != 0) return -1;
    if (App_ReleaseSubscriptions() != 0) return -1;
    if (xSemaphoreTake(appManager.mutex, pdMS_TO_TICKS(APP_MUTEX_TIMEOUT_MS)) != pdTRUE) return -1;
    AppManagerEvent discarded;
    while (xQueueReceive(appManager.eventQueue, &discarded, 0) == pdTRUE) {}
    xSemaphoreGive(appManager.mutex);
    appManager.state = SERVICE_STATE_STOPPED;
    return 0;
}

static int32_t App_Start(IService *self) {
    if (!appManager.controlMutex || xSemaphoreTake(appManager.controlMutex,
        pdMS_TO_TICKS(WATCHDOG_WORKER_CONTROL_TIMEOUT_MS)) != pdTRUE) return -1;
    int32_t result = App_StartOwned(self);
    xSemaphoreGive(appManager.controlMutex);
    return result;
}

static int32_t App_Stop(IService *self) {
    TaskHandle_t task = atomic_load(&appManager.worker.task);
    if (task && task == xTaskGetCurrentTaskHandle()) return -1;
    if (!appManager.controlMutex || xSemaphoreTake(appManager.controlMutex,
        pdMS_TO_TICKS(WATCHDOG_WORKER_CONTROL_TIMEOUT_MS)) != pdTRUE) return -1;
    int32_t result = App_StopOwned(self);
    xSemaphoreGive(appManager.controlMutex);
    return result;
}

static ServiceState App_GetState(IService *self) {
    (void)self;
    return appManager.state;
}

static int32_t App_CheckHealth(IService *self) {
    (void)self;
    return (appManager.state == SERVICE_STATE_STARTED && !atomic_load(&appManager.encoderBindingFailed) &&
            WatchdogWorker_Healthy(&appManager.worker)) ? 0 : -1;
}

IApplicationManager *ApplicationManager_GetModule(void) {
    static bool initialized = false;
    if (!initialized) {
        appManager.base.base.Initialize = App_Initialize;
        appManager.base.base.Start = App_Start;
        appManager.base.base.Stop = App_Stop;
        appManager.base.base.GetState = App_GetState;
        appManager.base.base.CheckHealth = App_CheckHealth;

        appManager.base.GetStatus = App_GetStatusImpl;
        appManager.base.PostEvent = App_PostEventImpl;
        appManager.base.TriggerFactoryReset = App_TriggerFactoryResetImpl;

        appManager.state = SERVICE_STATE_UNINITIALIZED;
        initialized = true;
    }
    return &appManager.base;
}
