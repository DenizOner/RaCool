#include "test_mocks.h"
#include "unity.h"
#include "Communication/DebugConsole/IDebugConsole.h"
#include "Core/LogManager/ILogManager.h"
#include "Core/ErrorHandler/IErrorHandler.h"
#include "Core/ErrorHandler/ErrorHandlerConfig.h"
#include "Core/PerformanceMonitor/IPerformanceMonitor.h"
#include "Core/SystemInitializer/ISystemInitializer.h"
#include "Controllers/SystemController/ISystemController.h"
#include "Controllers/MenuManager/IMenuManager.h"
#include "Hardware/RGBLED/IRGBLED.h"
#include "Hardware/PinManager/IPinManager.h"
#include "Hardware/OLED/IOLED.h"
#include "Hardware/I2CManager/II2CManager.h"
#include "Hardware/BuiltinLED/IBuiltinLED.h"
#include "Hardware/InternalTemperatureSensor/IInternalTemperatureSensor.h"
#include "Management/Watchdog/WatchdogConfig.h"
#include "Core/ServiceLocator/ServiceNames.h"
#include "Core/ServiceLocator/ServiceLocator.h"
#include "Core/ErrorHandler/ErrorHandlerConsoleCommands.h"
#include "Core/EventBus/EventBusConsoleCommands.h"
#include "Core/LogManager/LogManagerConsoleCommands.h"
#include "Core/PerformanceMonitor/PerformanceMonitorConsoleCommands.h"
#include "Core/ServiceLocator/ServiceLocatorConsoleCommands.h"
#include "Core/SystemInitializer/SystemInitializerConsoleCommands.h"
#include "Management/ApplicationManager/ApplicationManagerConsoleCommands.h"
#include "Management/Watchdog/WatchdogConsoleCommands.h"
#include "Management/SecurityManager/SecurityManagerConsoleCommands.h"
#include "Management/PowerManager/PowerManagerConsoleCommands.h"
#include "Controllers/SystemController/SystemControllerConsoleCommands.h"
#include "Controllers/MenuManager/MenuManagerConsoleCommands.h"
#include "Hardware/RGBLED/RGBLEDConsoleCommands.h"
#include "Hardware/PinManager/PinManagerConsoleCommands.h"
#include "Hardware/OLED/OLEDConsoleCommands.h"
#include "Hardware/I2CManager/I2CManagerConsoleCommands.h"
#include "Hardware/BuiltinLED/BuiltinLEDConsoleCommands.h"
#include "Hardware/InternalTemperatureSensor/InternalTemperatureSensorConsoleCommands.h"

/* This package verifies command admission, not diagnostic handler execution.
 * Unused service boundaries fail loudly if a registrar invokes a handler. */
#define UNUSED_SERVICE_BOUNDARY(type, name) \
    type *name(void) { TEST_FAIL_MESSAGE("Registration executed " #name); return NULL; }
UNUSED_SERVICE_BOUNDARY(ILogManager, GetLogManager)
UNUSED_SERVICE_BOUNDARY(IErrorHandler, GetErrorHandler)
UNUSED_SERVICE_BOUNDARY(SystemInitializer, GetSystemInitializer)
UNUSED_SERVICE_BOUNDARY(ISystemController, GetSystemController)
UNUSED_SERVICE_BOUNDARY(IMenuManager, MenuManager_GetModule)
UNUSED_SERVICE_BOUNDARY(IRGBLED, GetRGBLEDService)
esp_err_t RGBLED_SaveBrightnessText(const char *value) {
    (void)value;
    TEST_FAIL_MESSAGE("Registration executed brightness persistence");
    return ESP_ERR_INVALID_STATE;
}
UNUSED_SERVICE_BOUNDARY(IPinManager, PinManager_GetInstance)
UNUSED_SERVICE_BOUNDARY(IOLED, OLED_GetModule)
UNUSED_SERVICE_BOUNDARY(II2CManager, I2CManager_GetInstance)
UNUSED_SERVICE_BOUNDARY(IBuiltinLED, BuiltinLED_GetModule)
UNUSED_SERVICE_BOUNDARY(IInternalTemperatureSensor, InternalTemperatureSensor_GetModule)
PerformanceMonitorProxy PerformanceMonitor = {0};
const ErrorHandlerConfig *ErrorHandlerConfig_GetConfig(void) {
    TEST_FAIL_MESSAGE("Registration invoked error history configuration"); return NULL;
}
int32_t ServiceLocator_CopyBindings(ServiceLocatorBinding *bindings, uint32_t capacity, uint32_t *count) {
    (void)bindings; (void)capacity; (void)count;
    TEST_FAIL_MESSAGE("Registration invoked binding snapshot"); return -1;
}
int32_t ServiceLocator_CopyServices(IService **services, uint32_t capacity, uint32_t *count) {
    (void)services; (void)capacity; (void)count;
    TEST_FAIL_MESSAGE("Registration invoked service snapshot"); return -1;
}
int32_t ServiceLocator_CopyServiceName(IService *service, char *name, size_t capacity) {
    (void)service; (void)name; (void)capacity;
    TEST_FAIL_MESSAGE("Registration invoked service name lookup"); return -1;
}
WatchdogConfig WatchdogConfig_GetDefault(void) {
    TEST_FAIL_MESSAGE("Registration invoked watchdog configuration");
    return (WatchdogConfig){0};
}
esp_err_t BuiltinLED_SaveEnabled(bool enabled) {
    (void)enabled; TEST_FAIL_MESSAGE("Registration changed the LED gate"); return ESP_FAIL;
}
esp_err_t BuiltinLED_SaveBrightnessText(const char *value) {
    (void)value; TEST_FAIL_MESSAGE("Registration changed brightness"); return ESP_FAIL;
}

static unsigned calls, fail_at;
static bool console_available;
static esp_err_t failure;
static esp_err_t register_command(IDebugConsole *self, const esp_console_cmd_t *command,
                                  CommandCategory category) {
    (void)self;
    TEST_ASSERT_NOT_NULL(command);
    TEST_ASSERT_NOT_NULL(command->command);
    TEST_ASSERT_NOT_NULL(command->func);
    TEST_ASSERT_LESS_THAN(CMD_CAT_MAX, category);
    ++calls;
    return calls == fail_at ? failure : ESP_OK;
}
static IDebugConsole console = {.RegisterCommandWithCategory = register_command};
static IService *locate(const char *name) {
    TEST_ASSERT_EQUAL_STRING(SVC_NAME_DEBUG_CONSOLE, name);
    return console_available ? (IService *)&console : NULL;
}
static const struct { const char *name; esp_err_t (*register_all)(void); unsigned count; } modules[] = {
    {"ErrorHandler", ErrorHandler_RegisterConsoleCommands, 6},
    {"EventBus", EventBus_RegisterConsoleCommands, 2},
    {"LogManager", LogManager_RegisterConsoleCommands, 4},
    {"PerformanceMonitor", PerformanceMonitor_RegisterConsoleCommands, 3},
    {"ServiceLocator", ServiceLocator_RegisterConsoleCommands, 2},
    {"SystemInitializer", SystemInitializer_RegisterConsoleCommands, 1},
    {"ApplicationManager", ApplicationManager_RegisterConsoleCommands, 1},
    {"Watchdog", Watchdog_RegisterConsoleCommands, 1},
    {"SecurityManager", SecurityManager_RegisterConsoleCommands, 1},
    {"PowerManager", PowerManager_RegisterConsoleCommands, 1},
    {"SystemController", SystemController_RegisterConsoleCommands, 1},
    {"MenuManager", MenuManager_RegisterConsoleCommands, 1},
    {"RGBLED", RGBLED_RegisterConsoleCommands, 1},
    {"PinManager", PinManager_RegisterConsoleCommands, 1},
    {"OLED", OLED_RegisterConsoleCommands, 1},
    {"I2CManager", I2CManager_RegisterConsoleCommands, 1},
    {"BuiltinLED", BuiltinLED_RegisterConsoleCommands, 1},
    {"InternalTemperatureSensor", InternalTemperatureSensor_RegisterConsoleCommands, 1},
};
void setUp(void) {
    calls = fail_at = 0;
    console_available = true;
    console.RegisterCommandWithCategory = register_command;
    mocked_ServiceLocator_GetService_fake.custom_fake = locate;
}
void tearDown(void) {}
void test_ModuleRegistration_RejectsUnavailableConsoleAndMethod(void) {
    for (unsigned i = 0; i < sizeof(modules) / sizeof(modules[0]); ++i) {
        console_available = false;
        TEST_ASSERT_EQUAL_MESSAGE(ESP_ERR_INVALID_STATE, modules[i].register_all(), modules[i].name);
        console_available = true;
        console.RegisterCommandWithCategory = NULL;
        TEST_ASSERT_EQUAL_MESSAGE(ESP_ERR_INVALID_STATE, modules[i].register_all(), modules[i].name);
        console.RegisterCommandWithCategory = register_command;
    }
    TEST_ASSERT_EQUAL(0, calls);
}
void test_ModuleRegistration_PreservesFailuresAndReRegistersAfterConsoleRestart(void) {
    const esp_err_t errors[] = {ESP_ERR_NO_MEM, ESP_ERR_TIMEOUT};
    for (unsigned i = 0; i < sizeof(modules) / sizeof(modules[0]); ++i) {
        for (unsigned e = 0; e < sizeof(errors) / sizeof(errors[0]); ++e) {
            for (unsigned position = 1; position <= modules[i].count; ++position) {
                calls = 0; fail_at = position; failure = errors[e];
                TEST_ASSERT_EQUAL_MESSAGE(errors[e], modules[i].register_all(), modules[i].name);
                TEST_ASSERT_EQUAL_MESSAGE(position, calls, modules[i].name);
                calls = fail_at = 0;
                TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, modules[i].register_all(), modules[i].name);
                TEST_ASSERT_EQUAL_MESSAGE(modules[i].count, calls, modules[i].name);
                calls = 0;
                TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, modules[i].register_all(), modules[i].name);
                TEST_ASSERT_EQUAL_MESSAGE(modules[i].count, calls, modules[i].name);
            }
        }
    }
}
