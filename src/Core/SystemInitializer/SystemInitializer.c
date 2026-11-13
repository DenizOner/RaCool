#include <esp_err.h>
#include <esp_event.h>
#include <esp_netif.h>
#include <esp_rom_sys.h>
#include <esp_system.h>
#include <driver/gpio.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "Communication/DebugConsole/IDebugConsole.h"
#include "Communication/SerialMonitor/ISerialMonitor.h"
#include "Communication/Zigbee/IZigbee.h"
#include "Controllers/ClimateController/IClimateController.h"
#include "Controllers/MenuManager/IMenuManager.h"
#include "Controllers/SystemController/ISystemController.h"
#include "Controllers/UIController/IUIController.h"
#include "Controllers/UIController/UIControllerConfig.h"
#include "Controllers/UIController/UIConsoleCommands.h"
#include "Core/MemoryManager/MemoryConsoleCommands.h"
#include "Communication/DebugConsole/Commands/SystemCommands.h"
#include "Communication/DebugConsole/Commands/DiagnosticCommands.h"
#include "Core/ErrorHandler/IErrorHandler.h"
#include "Core/ErrorHandler/ErrorHandlerConsoleCommands.h"
#include "Core/EventBus/IEventBus.h"
#include "Core/EventBus/EventBusConsoleCommands.h"
#include "Core/LogManager/ILogManager.h"
#include "Core/LogManager/LogManagerConsoleCommands.h"
#include "Core/MemoryManager/IMemoryManager.h"
#include "Core/PerformanceMonitor/IPerformanceMonitor.h"
#include "Core/PerformanceMonitor/PerformanceMonitorConsoleCommands.h"
#include "Core/ServiceLocator/ServiceLocator.h"
#include "Core/ServiceLocator/ServiceLocatorConsoleCommands.h"
#include "Core/ServiceLocator/ServiceNames.h"
#include "Core/SystemInitializer/ISystemInitializer.h"
#include "Core/SystemInitializer/SystemInitializerConsoleCommands.h"
#include "Hardware/BuiltinLED/IBuiltinLED.h"
#include "Hardware/InfraredTemperatureSensor/IInfraredTemperatureSensor.h"
#include "Hardware/InternalTemperatureSensor/IInternalTemperatureSensor.h"
#include "Hardware/OLED/IOLED.h"
#include "Hardware/I2CManager/II2CManager.h"
#include "Hardware/PinManager/IPinManager.h"
#include "Hardware/PinManager/PinManagerConfig.h"
#include "Hardware/RGBLED/IRGBLED.h"
#include "Hardware/RGBLED/RGBLEDMenuPlugin.h"
#include "Hardware/Relay/IRelay.h"
#include "Hardware/ResourceRegistry/IResourceRegistry.h"
#include "Hardware/ResourceRegistry/ResourceRegistryCommands.h"
#include "Hardware/RotaryEncoder/IRotaryEncoder.h"
#include "Management/ApplicationManager/IApplicationManager.h"
#include "Management/ApplicationManager/ApplicationManagerConsoleCommands.h"
#include "Management/ConfigurationManager/IConfigurationManager.h"
#include "Management/ConfigurationManager/ConfigurationManagerCommands.h"
#include "Management/ConfigurationManager/SystemConfigs.h"
#include "Management/DiagnosticsManager/IDiagnosticsManager.h"
#include "Management/DiagnosticsManager/DiagnosticsManagerConsoleCommands.h"
#include "Management/PowerManager/IPowerManager.h"
#include "Management/SecurityManager/ISecurityManager.h"
#include "Management/SecurityManager/SecurityManagerConsoleCommands.h"
#include "Management/Watchdog/IWatchdog.h"
#include "Management/Watchdog/WatchdogConsoleCommands.h"
#include "Controllers/SystemController/SystemControllerConsoleCommands.h"
#include "Controllers/MenuManager/MenuManagerConsoleCommands.h"
#include "Communication/SerialMonitor/SerialMonitorConsoleCommands.h"
// Hardware ConsoleCommands
#include "Hardware/RGBLED/RGBLEDConsoleCommands.h"
#include "Hardware/PinManager/PinManagerConsoleCommands.h"
#include "Hardware/OLED/OLEDConsoleCommands.h"
#include "Hardware/I2CManager/I2CManagerConsoleCommands.h"
#include "Hardware/BuiltinLED/BuiltinLEDConsoleCommands.h"
#include "Hardware/InternalTemperatureSensor/InternalTemperatureSensorConsoleCommands.h"
#include "Hardware/InfraredTemperatureSensor/InfraredTemperatureSensorConsoleCommands.h"
#include "esp_log.h"
#include "esp_timer.h"

// External plugin getter from SerialLogPlugin.c
extern ISerialMonitorPlugin* LogManager_GetSerialPlugin(void);

// Static instance
static SystemInitializer systemInitializerInstance;

#include "Core/SystemInitializer/SystemInitializerStartup.h"

/**
 * @brief Initialize all core services linearly
 */
static bool InitializeCoreServices(IDIRegistry *registry) {
  ESP_LOGI("SystemInitializer", "Initializing Core Services...");

  // 1. LogManager (Must be first for logging)
  ILogManager *logMgr = GetLogManager();
  if (!logMgr || IService_SafeInitialize((IService *)logMgr, registry) != 0 ||
      ServiceLocator_RegisterLogManager(logMgr) != 0 ||
      IService_SafeStart((IService *)logMgr) != 0) return false;

  // 2. DebugConsole
  IDebugConsole *debugConsole = DebugConsole_GetModule();
  if (!SystemInitializer_StartService(SVC_NAME_DEBUG_CONSOLE, (IService *)debugConsole, registry) ||
      !debugConsole->SetMuteLog) return false;
  if (debugConsole->SetMuteLog(debugConsole, false) != ESP_OK) return false;
  esp_rom_printf("[SystemInitializer] DebugConsole logs enabled\n");

  // 3. Essential Infrastructure
  if (!SystemInitializer_StartService(SVC_NAME_ERROR_HANDLER, (IService *)GetErrorHandler(), registry)) return false;
  if (!SystemInitializer_StartService(SVC_NAME_MEMORY_MANAGER, (IService *)GetMemoryManager(), registry)) return false;
  if (!SystemInitializer_StartService(SVC_NAME_RESOURCE_REGISTRY, (IService *)ResourceRegistry_GetInstance(), registry)) return false;
  if (!SystemInitializer_StartService(SVC_NAME_EVENT_BUS, (IService *)GetEventBus(), registry)) return false;


  // 4. Performance Monitor (Linear, not nested)
  esp_rom_printf("[SystemInitializer] Starting PerformanceMonitor...\n");
  PerformanceMonitorConfig perfCfg = PerformanceMonitorConfig_GetDefault();
  if (!PerformanceMonitor.InitializeWithConfig || !PerformanceMonitor.base.Start ||
      PerformanceMonitor.InitializeWithConfig(perfCfg) != ESP_OK ||
      PerformanceMonitor.base.Start((IService *)&PerformanceMonitor) != 0) return false;
  esp_rom_printf("[SystemInitializer] PerformanceMonitor started\n");

  // 5. PinManager (Linear)
  // PinManager owns GPIO ISR setup and initializes before the encoder/UI.
  esp_rom_printf("[SystemInitializer] Starting PinManager...\n");
  IPinManager *pinMgr = PinManager_GetInstance();
  if (!SystemInitializer_StartService(SVC_NAME_PIN_MANAGER, (IService *)pinMgr, registry)) return false;
  esp_rom_printf("[SystemInitializer] PinManager started\n");

  // 6. Initialize the shared I2C infrastructure.
  esp_rom_printf("[SystemInitializer] Starting I2CManager...\n");
  II2CManager *i2cMgr = I2CManager_GetInstance();
  if (!SystemInitializer_StartService(SVC_NAME_I2C_MANAGER, (IService *)i2cMgr, registry)) return false;
  esp_rom_printf("[SystemInitializer] I2C Infrastructure ready\n");

  // 7. SerialMonitor (Core Communication Bridge)
  esp_rom_printf("[SystemInitializer] Starting SerialMonitor...\n");
  ISerialMonitor *serMon = SerialMonitor_GetModule();
  if (!SystemInitializer_StartService(SVC_NAME_SERIAL_MONITOR, (IService *)serMon, registry)) return false;
  esp_rom_printf("[SystemInitializer] SerialMonitor started\n");

  esp_rom_printf("[SystemInitializer] Core Services setup complete\n");
  return true;
}

static SystemInitializerWorker communication_worker = {.done = true};
static SystemInitializerWorker hardware_worker = {.done = true};
static atomic_bool startup_busy;
static _Atomic(ServiceState) startup_state;

static void zigbee_init_task(void *parameter) {
    SystemInitializerWorker *worker = parameter;
    bool succeeded = SystemInitializer_StartService(SVC_NAME_ZIGBEE, (IService *)Zigbee_Create(), worker->registry);
    SystemInitializer_CompleteWorker(worker, succeeded);
    vTaskDelete(NULL);
}

static bool InitializeHardwareServices(IDIRegistry *registry) {
    if (!SystemInitializer_StartService(SVC_NAME_BUILTIN_LED, (IService *)BuiltinLED_GetModule(), registry) ||
        !SystemInitializer_StartService(SVC_NAME_RGB_LED, (IService *)GetRGBLEDService(), registry) ||
        !SystemInitializer_StartService(SVC_NAME_RELAY, (IService *)Relay_GetModule(), registry)) return false;
    /* Preserve the MLX90614 power-up requirement with a positive RTOS delay. */
    TickType_t settle = pdMS_TO_TICKS(SYSTEM_INITIALIZER_SENSOR_SETTLE_MS);
    vTaskDelay(settle ? settle : 1);
    if (!SystemInitializer_StartService(SVC_NAME_IR_TEMP_SENSOR, (IService *)InfraredTemperatureSensor_GetModule(), registry)) return false;
    IInternalTemperatureSensor *internal = InternalTemperatureSensor_GetModule();
    return SystemInitializer_StartService(SVC_NAME_INTERNAL_TEMP_SENSOR, (IService *)internal, registry);
}

static void hw_init_task(void *parameter) {
    SystemInitializerWorker *worker = parameter;
    bool succeeded = InitializeHardwareServices(worker->registry);
    SystemInitializer_CompleteWorker(worker, succeeded);
    vTaskDelete(NULL);
}

#include <nvs_flash.h>

static int32_t SystemInitializer_InitializeOwned(SystemInitializerConfig config, IDIRegistry *registry) {
  if (config.startupDelayMs > 0) {
    TickType_t delay = pdMS_TO_TICKS(config.startupDelayMs);
    vTaskDelay(delay ? delay : 1);
  }
  if (registry == NULL) registry = ServiceLocator_GetRegistry();
  
  if (!config.bypassNvsInit) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_LOGE("SystemInitializer", "NVS recovery required; stored user settings were preserved: %s", esp_err_to_name(ret));
      return -1;
    }
    if (ret != ESP_OK) return -1;
  }

  if (esp_netif_init() != ESP_OK) return -1;
  esp_err_t event_result = esp_event_loop_create_default();
  if (event_result != ESP_OK && event_result != ESP_ERR_INVALID_STATE) return -1;

  if (config.enableCoreServices && !InitializeCoreServices(registry)) return -1;
  
  if (!SystemInitializer_StartService(SVC_NAME_CONFIGURATION_MANAGER, (IService *)ConfigurationManager_GetInstance(), registry)) return -1;
  if (config.enableCoreServices)
    if (!SystemInitializer_StartService(SVC_NAME_POWER_MANAGER, (IService *)PowerManager_GetInstance(), registry)) return -1;

  // Early UI Initialization: Start OLED, Menu & UI Controller immediately
  // so that the boot splash animation renders instantly on power-up!
  if (config.enableUserInterface) {
    if (!SystemInitializer_StartService(SVC_NAME_OLED, (IService *)OLED_GetModule(), registry)) return -1;
    if (!SystemInitializer_StartService(SVC_NAME_ROTARY_ENCODER, (IService *)RotaryEncoder_GetModule(), registry)) return -1;
    if (!SystemInitializer_StartService(SVC_NAME_MENU_MANAGER, (IService *)MenuManager_GetModule(), registry)) return -1;
    if (RGBLEDMenuPlugin_Init() != ESP_OK) {
      ESP_LOGE("SystemInitializer", "LED menu initialization failed");
      return -1;
    }

    IUIController *uiCtrl = UIController_GetModule();
    if (!uiCtrl || !uiCtrl->InitializeWithConfig ||
        uiCtrl->InitializeWithConfig(uiCtrl, UIController_GetDefaultConfig(), registry) != 0 ||
        !SystemInitializer_StartService(SVC_NAME_UI_CONTROLLER, (IService *)uiCtrl, registry)) return -1;
    esp_rom_printf("[SystemInitializer] Early UI Controller Started (Splash Animation Active)\n");
    /* The OLED owner already exists. Register its diagnostics before long
     * radio/hardware startup so a real loading frame can be inspected. */
    if (config.enableAdvancedManagers && OLED_RegisterConsoleCommands() != ESP_OK) return -1;
  }

  // Concurrent Communication (Zigbee) & Hardware Modules init
  if (config.enableCommunicationModules &&
      !SystemInitializer_StartWorker(&communication_worker, zigbee_init_task, "zigbee_init", registry)) return -1;
  if (config.enableHardwareModules &&
      !SystemInitializer_StartWorker(&hardware_worker, hw_init_task, "hw_init", registry)) return -1;
  /* Completion means service startup, not successful remote Zigbee joining. */
  if (config.enableCommunicationModules &&
      !SystemInitializer_WaitWorker(&communication_worker, SYSTEM_INITIALIZER_WORKER_TIMEOUT_MS)) return -1;
  if (config.enableHardwareModules &&
      !SystemInitializer_WaitWorker(&hardware_worker, SYSTEM_INITIALIZER_WORKER_TIMEOUT_MS)) return -1;

  if (config.enableAdvancedManagers) {
    if (!SystemInitializer_StartService(SVC_NAME_WATCHDOG, (IService *)Watchdog_GetModule(), registry)) return -1;
    if (!SystemInitializer_StartService(SVC_NAME_SECURITY_MANAGER, (IService *)SecurityManager_GetModule(), registry)) return -1;
    if (!SystemInitializer_StartService(SVC_NAME_DIAGNOSTICS_MANAGER, (IService *)DiagnosticsManager_GetModule(), registry)) return -1;
    
    // Register Diagnostic Commands to Console
    IDebugConsole *debugConsole = DebugConsole_GetModule();
    if (!debugConsole) return -1;
    {
        if (DiagnosticCommands_Register(debugConsole) != ESP_OK) return -1;
        if (UIConsoleCommands_Register(debugConsole) != ESP_OK ||
            MemoryConsoleCommands_Register(debugConsole) != ESP_OK ||
            ResourceRegistryCommands_Register(debugConsole) != ESP_OK) return -1;
        // Management
        if (ApplicationManager_RegisterConsoleCommands() != ESP_OK) return -1;
        if (ConfigurationManagerCommands_Register(debugConsole) != ESP_OK) return -1;
        if (Watchdog_RegisterConsoleCommands() != ESP_OK) return -1;
        if (SecurityManager_RegisterConsoleCommands() != ESP_OK) return -1;
        if (DiagnosticsManager_RegisterConsoleCommands() != ESP_OK) return -1;
        // Controllers
        if (SystemController_RegisterConsoleCommands() != ESP_OK) return -1;
        if (MenuManager_RegisterConsoleCommands() != ESP_OK) return -1;
        // Communication
        esp_err_t serial_console_result = SerialMonitor_RegisterConsoleCommands();
        if (serial_console_result != ESP_OK) {
            ESP_LOGE("SystemInitializer", "SER_PERF command registration failed: %s",
                     esp_err_to_name(serial_console_result));
            return -1;
        }
        // Core
        if (ErrorHandler_RegisterConsoleCommands() != ESP_OK) return -1;
        if (EventBus_RegisterConsoleCommands() != ESP_OK) return -1;
        if (LogManager_RegisterConsoleCommands() != ESP_OK) return -1;
        if (PerformanceMonitor_RegisterConsoleCommands() != ESP_OK) return -1;
        if (ServiceLocator_RegisterConsoleCommands() != ESP_OK) return -1;
        if (SystemInitializer_RegisterConsoleCommands() != ESP_OK) return -1;
        // Hardware
        if (RGBLED_RegisterConsoleCommands() != ESP_OK) return -1;
        if (PinManager_RegisterConsoleCommands() != ESP_OK) return -1;
        if (!config.enableUserInterface && OLED_RegisterConsoleCommands() != ESP_OK) return -1;
        if (I2CManager_RegisterConsoleCommands() != ESP_OK) return -1;
        if (BuiltinLED_RegisterConsoleCommands() != ESP_OK) return -1;
        if (InternalTemperatureSensor_RegisterConsoleCommands() != ESP_OK) return -1;
        if (InfraredTemperatureSensor_RegisterConsoleCommands() != ESP_OK) return -1;
    }
  }

  if (config.enableControllers) {
    if (!SystemInitializer_StartService(SVC_NAME_SYSTEM_CONTROLLER, (IService *)GetSystemController(), registry)) return -1;
    if (!SystemInitializer_StartService(SVC_NAME_CLIMATE_CONTROLLER, (IService *)ClimateController_GetModule(), registry)) return -1;
    if (!SystemInitializer_StartService(SVC_NAME_APPLICATION_MANAGER, (IService *)ApplicationManager_GetModule(), registry)) return -1;
  }

  ESP_LOGI("SystemInitializer", "System Initialization COMPLETE");
  esp_rom_printf("\r\n--- SYSTEM INITIALIZATION COMPLETE ---\r\n");
  return 0;
}

int32_t SystemInitializer_InitializeWithConfig(SystemInitializer *self, SystemInitializerConfig config, IDIRegistry *registry) {
    if (!self || config.startupDelayMs > SYSTEM_INITIALIZER_MAX_STARTUP_DELAY_MS ||
        atomic_exchange(&startup_busy, true)) return -1;
    if (!atomic_load(&communication_worker.done) || !atomic_load(&hardware_worker.done)) {
        atomic_store(&startup_busy, false);
        return -1;
    }
    if (atomic_load(&startup_state) == SERVICE_STATE_INITIALIZED) {
        atomic_store(&startup_busy, false);
        return 0;
    }
    SystemInitializer_ResetStartupProgress();
    int32_t result = SystemInitializer_InitializeOwned(config, registry);
    if (result != 0) SystemInitializer_FailStartupProgress();
    atomic_store(&startup_state, result == 0 ? SERVICE_STATE_INITIALIZED : SERVICE_STATE_ERROR);
    atomic_store(&startup_busy, false);
    return result;
}

static ServiceState SystemInitializer_GetState(IService *self) {
    (void)self;
    return atomic_load(&startup_state);
}

int32_t SystemInitializer_Initialize(IService *self, IDIRegistry *registry) {
  SystemInitializer *initializer = (SystemInitializer *)self;
  return initializer->InitializeWithConfig(initializer, SystemInitializerConfig_GetDefault(), registry);
}

void SystemInitializer_ManageDependencies(void) {}

SystemInitializer *GetSystemInitializer(void) {
  systemInitializerInstance.base.Initialize = SystemInitializer_Initialize;
  systemInitializerInstance.base.GetState = SystemInitializer_GetState;
  systemInitializerInstance.InitializeWithConfig = SystemInitializer_InitializeWithConfig;
  systemInitializerInstance.ManageDependencies = SystemInitializer_ManageDependencies;
  return &systemInitializerInstance;
}
