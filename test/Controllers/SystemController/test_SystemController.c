#ifndef RACOOL_TEST_NATIVE
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#endif
#include "Hardware/BuiltinLED/IBuiltinLED.h"
#include "Hardware/RGBLED/IRGBLED.h"
#include "Core/ServiceLocator/ServiceLocator.h"
#include "Controllers/SystemController/ISystemController.h"
#include "Controllers/SystemController/ISystemTelemetry.h"
#include "test_mocks.h"
#include "unity.h"

// Include the controller implementation for private event payload inspection.
#include "Controllers/SystemController/SystemController.c"
#include "Controllers/SystemController/SystemControllerConfig.c"
#define TAG TAG_SystemTelemetry
#include "Controllers/SystemController/SystemController_Reporting.c"
#undef TAG
#include "Controllers/SystemController/SystemControllerConsoleCommands.c"

// Mock RGBLED
static int32_t rgb_set_color_called = 0;
static uint8_t last_r, last_g, last_b;

static int32_t mock_rgb_set_color(IRGBLED *self, RGBColor color) {
  rgb_set_color_called++;
  last_r = color.r; last_g = color.g; last_b = color.b;
  return 0;
}
static IRGBLED mock_rgb = {.SetColorRGB = mock_rgb_set_color};

// Mock BuiltinLED
static int32_t builtin_set_animation_called = 0;
static BuiltinLEDAnimationType last_builtin_animation;
static esp_err_t mock_builtin_set_animation(IBuiltinLED *self, BuiltinLEDAnimationType type, BuiltinLEDPriority priority, uint32_t duration_ms) {
  builtin_set_animation_called++;
  last_builtin_animation = type;
  return ESP_OK;
}
static IBuiltinLED mock_builtin = {.SetAnimation = mock_builtin_set_animation};

// Mock Registry
static int32_t registry_resolve_called = 0;
static IService *mock_registry_resolve(IDIRegistry *self, const char *name) {
  registry_resolve_called++;
  if (strcmp(name, SVC_NAME_RGB_LED) == 0) return (IService *)&mock_rgb;
  if (strcmp(name, SVC_NAME_BUILTIN_LED) == 0) return (IService *)&mock_builtin;
  return NULL;
}
static IDIRegistry mock_registry = {.Resolve = mock_registry_resolve};

static SystemEvent queuedEvent;
static BaseType_t captureEvent(QueueHandle_t queue, const void *event, TickType_t timeout) {
    (void)queue; (void)timeout;
    queuedEvent = *(const SystemEvent *)event;
    return pdPASS;
}

static BaseType_t create_system_test_task(TaskFunction_t fn, const char *name, uint32_t stack,
    void *arg, uint32_t priority, TaskHandle_t *task) {
    (void)fn; (void)name; (void)stack; (void)arg; (void)priority;
    *task = (TaskHandle_t)(uintptr_t)(0x200 + mocked_xTaskCreate_fake.call_count);
    return pdPASS;
}

static QueueHandle_t create_system_queue(UBaseType_t length, UBaseType_t size) {
  (void)length; (void)size;
  return (QueueHandle_t)(uintptr_t)(0x300 + mocked_xQueueCreate_fake.call_count);
}
static BaseType_t acknowledge_system_stop(QueueHandle_t queue, void *item, TickType_t wait) {
  (void)wait;
  if (queue == controllerInstance.worker.stopped || queue == telemetryInstance.worker.stopped) {
    *(int32_t *)item = 0;
    return pdTRUE;
  }
  return pdFALSE;
}
void setUp(void) {
  ISystemController controller_interface = *GetSystemController();
  memset(&controllerInstance, 0, sizeof(controllerInstance));
  controllerInstance.base = controller_interface;
  RESET_FAKE(mocked_xQueueReceive);
  mocked_xQueueReceive_fake.custom_fake = acknowledge_system_stop;
  RESET_FAKE(mocked_xTaskGetCurrentTaskHandle);
  RESET_FAKE(mocked_vTaskDelete);

  ISystemTelemetry telemetry_interface = *GetSystemTelemetry();
  memset(&telemetryInstance, 0, sizeof(telemetryInstance));
  telemetryInstance.interface = telemetry_interface;
  RESET_FAKE(mocked_xSemaphoreCreateMutex);
  RESET_FAKE(mocked_xSemaphoreTake);
  RESET_FAKE(mocked_xSemaphoreGive);
  mocked_xSemaphoreCreateMutex_fake.return_val = (void *)0x101;
  mocked_xSemaphoreTake_fake.return_val = pdTRUE;
  RESET_FAKE(mocked_xQueueCreate);
  RESET_FAKE(mocked_xQueueSend);
  RESET_FAKE(mocked_xTaskCreate);
  RESET_FAKE(mocked_ServiceLocator_GetService);
  mocked_xQueueCreate_fake.custom_fake = create_system_queue;
  mocked_xQueueSend_fake.custom_fake = captureEvent;
  mocked_xTaskCreate_fake.return_val = pdPASS;
  mocked_xTaskCreate_fake.custom_fake = create_system_test_task;
  rgb_set_color_called = 0;
  builtin_set_animation_called = 0;
  registry_resolve_called = 0;

  // Reset controllerInstance
  controllerInstance.serviceState = SERVICE_STATE_UNINITIALIZED;
  controllerInstance.systemState = SYSTEM_STATE_INITIALIZING;
}

void tearDown(void) {}

static unsigned applied_tx_calls;
static esp_err_t applied_tx_result;
static ZigbeeTxStatus applied_tx_status;
static uint8_t refreshed_tx;
static int32_t system_set_tx(IZigbee *self, ZigbeeTxProfile profile) {
  (void)self; ++applied_tx_calls;
  if (applied_tx_result == ESP_OK) applied_tx_status.profile = profile;
  return applied_tx_result;
}
static int32_t system_get_tx(IZigbee *self, ZigbeeTxStatus *status) {
  (void)self; *status = applied_tx_status; return ESP_OK;
}
static int32_t system_refresh_tx(IZigbee *self, uint8_t ep, uint16_t cluster,
    uint16_t attr, const void *value, uint8_t size) {
  (void)self;
  TEST_ASSERT_EQUAL(ZIGBEE_EP_SYSTEM, ep);
  TEST_ASSERT_EQUAL(ZIGBEE_SYSTEM_CONFIG_CLUSTER, cluster);
  TEST_ASSERT_EQUAL(ZIGBEE_ATTR_TX_PROFILE, attr);
  TEST_ASSERT_EQUAL(1, size);
  refreshed_tx = *(const uint8_t *)value;
  return 0;
}
void test_SystemController_TxQueueCopiesRequestAndRefreshesActualAfterFailure(void) {
  ISystemController *system = GetSystemController();
  TEST_ASSERT_EQUAL(0, system->base.Initialize(&system->base, &mock_registry));
  TEST_ASSERT_EQUAL(0, system->base.Start(&system->base));
  IZigbee radio = {.SetTxProfile=system_set_tx,.GetTxStatus=system_get_tx,.QueueAttributeUpdate=system_refresh_tx};
  mocked_ServiceLocator_GetService_fake.return_val = &radio.base;
  applied_tx_calls = 0; applied_tx_result = ESP_OK;
  applied_tx_status = (ZigbeeTxStatus){.profile=ZIGBEE_TX_NORMAL,.dbm=10};
  TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, system->RequestZigbeeTxProfile(system, 3));
  TEST_ASSERT_EQUAL(ESP_OK, system->RequestZigbeeTxProfile(system, ZIGBEE_TX_BOOST));
  TEST_ASSERT_EQUAL(0, applied_tx_calls);
  TEST_ASSERT_TRUE(queuedEvent.zigbeeTxProfile);
  TEST_ASSERT_EQUAL(ZIGBEE_TX_BOOST, queuedEvent.payload);
  SC_ProcessEvent(&queuedEvent);
  TEST_ASSERT_EQUAL(1, applied_tx_calls);
  TEST_ASSERT_EQUAL(ZIGBEE_TX_BOOST, refreshed_tx);
  applied_tx_result = ESP_FAIL;
  TEST_ASSERT_EQUAL(ESP_OK, system->RequestZigbeeTxProfile(system, ZIGBEE_TX_ECO));
  SC_ProcessEvent(&queuedEvent);
  TEST_ASSERT_EQUAL(ZIGBEE_TX_BOOST, refreshed_tx);
  mocked_xQueueSend_fake.custom_fake = NULL;
  mocked_xQueueSend_fake.return_val = pdFALSE;
  TEST_ASSERT_EQUAL(ESP_ERR_NO_MEM, system->RequestZigbeeTxProfile(system, ZIGBEE_TX_ECO));
  atomic_store(&controllerInstance.shutdownPending,true);
  TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, system->RequestZigbeeTxProfile(system, ZIGBEE_TX_ECO));
}

void test_SystemController_Lifecycle(void) {
  ISystemController *sc = GetSystemController();
  IService *service = &(sc->base);

  TEST_ASSERT_EQUAL(0, service->Initialize(service, &mock_registry));
  TEST_ASSERT_EQUAL(SERVICE_STATE_INITIALIZED, service->GetState(service));
  TEST_ASSERT_TRUE(controllerInstance.eventQueue != NULL);

  TEST_ASSERT_EQUAL(0, service->Start(service));
  TEST_ASSERT_EQUAL(SERVICE_STATE_STARTED, service->GetState(service));
  
  // Ready set because telemetry interval is default (Initial state is INITIALIZING, check Start logic)
  TEST_ASSERT_EQUAL(SYSTEM_STATE_INITIALIZING, controllerInstance.systemState);

  TEST_ASSERT_EQUAL(0, service->Stop(service));
  TEST_ASSERT_EQUAL(SERVICE_STATE_STOPPED, service->GetState(service));
}

void test_SystemController_States(void) {
  ISystemController *sc = GetSystemController();
  sc->base.Initialize(&(sc->base), &mock_registry);
  sc->base.Start(&(sc->base));

  rgb_set_color_called = 0;
  
  // System readiness is shown on the built-in indicator.
  sc->SetSystemState(sc, SYSTEM_STATE_READY);
  TEST_ASSERT_EQUAL(DOUBLE_BLINK_WHITE, last_builtin_animation);
  TEST_ASSERT_EQUAL(0, rgb_set_color_called);

  // Set ERROR -> Transitions to RECOVERING automatically
  sc->SetSystemState(sc, SYSTEM_STATE_ERROR);
  TEST_ASSERT_EQUAL(SYSTEM_STATE_RECOVERING, controllerInstance.systemState);
  TEST_ASSERT_EQUAL(POLICE_STROBE, last_builtin_animation);
  TEST_ASSERT_EQUAL(0, rgb_set_color_called);
}

void test_SystemController_Events(void) {
  ISystemController *sc = GetSystemController();
  sc->base.Initialize(&(sc->base), &mock_registry);
  sc->base.Start(&(sc->base));

  // Asynchronous event Handling
  sc->HandleSystemEvent(sc, 100); // Mapped to ERROR in config
  
  TEST_ASSERT_EQUAL(1, mocked_xQueueSend_fake.call_count);
  TEST_ASSERT_EQUAL(100, queuedEvent.eventId);
  TEST_ASSERT_EQUAL(0, queuedEvent.payload);
}

void test_SystemTelemetry_Basics(void) {
  ISystemTelemetry *tel = GetSystemTelemetry();
  IService *svc = (IService *)tel;

  TEST_ASSERT_EQUAL(0, svc->Initialize(svc, &mock_registry));
  tel->SetReportingInterval(tel, 5000);
  TEST_ASSERT_EQUAL(5000, tel->GetReportingInterval(tel));
}

void test_SystemController_All(void) {
  RUN_TEST(test_SystemController_Lifecycle);
  RUN_TEST(test_SystemController_States);
  RUN_TEST(test_SystemController_Events);
  RUN_TEST(test_SystemTelemetry_Basics);
}
static SystemSettings reporting_settings;
static unsigned reporting_save_calls;
static esp_err_t reporting_save_result;
static esp_err_t reporting_snapshot(IConfigurationManager *self, SystemSettings *out) {
    (void)self; *out = reporting_settings; return ESP_OK;
}
static esp_err_t reporting_save(IConfigurationManager *self, const char *block,
    const ConfigurationFieldUpdate *fields, size_t count, bool persist) {
    (void)self; ++reporting_save_calls;
    TEST_ASSERT_EQUAL_STRING("zigbee_reporting", block); TEST_ASSERT_EQUAL(1, count); TEST_ASSERT_TRUE(persist);
    if (reporting_save_result == ESP_OK)
        memcpy((uint8_t *)&reporting_settings.zigbee_reporting + fields[0].offset, fields[0].data, fields[0].size);
    return reporting_save_result;
}
static IConfigurationManager reporting_config = {.GetSnapshot = reporting_snapshot, .UpdateFields = reporting_save};
static IService *reporting_service(const char *name) {
    return strcmp(name, SVC_NAME_CONFIGURATION_MANAGER) == 0 ? (IService *)&reporting_config : NULL;
}
void test_SystemController_ReportMenuCanDisableAndReenableEveryInterval(void) {
    controllerInstance.serviceState = SERVICE_STATE_STARTED;
    mocked_ServiceLocator_GetService_fake.custom_fake = reporting_service;
    memset(&reporting_settings, 0, sizeof(reporting_settings));
    reporting_save_calls = 0; reporting_save_result = ESP_OK;
    const UIState states[] = {UI_STATE_ZB_REP_FAN, UI_STATE_ZB_REP_AMBIENT, UI_STATE_ZB_REP_CPU,
        UI_STATE_ZB_REP_HEALTH, UI_STATE_ZB_REP_MEMORY, UI_STATE_ZB_REP_UPTIME};
    for (unsigned i = 0; i < 6; ++i) {
        ZigbeeRep_ConfigHandler(states[i], 1, false);
        uint16_t value;
        TEST_ASSERT_EQUAL(ESP_OK, ZigbeeReporting_GetInterval(&reporting_config, (ZigbeeReportKind)i, &value));
        TEST_ASSERT_EQUAL(1, value);
        ZigbeeRep_ConfigHandler(states[i], -1, false);
        TEST_ASSERT_EQUAL(ESP_OK, ZigbeeReporting_GetInterval(&reporting_config, (ZigbeeReportKind)i, &value));
        TEST_ASSERT_EQUAL(0, value);
    }
    TEST_ASSERT_EQUAL(12, reporting_save_calls);
}
void test_SystemController_ReportMenuSaturatesAndPreservesOnFailure(void) {
    controllerInstance.serviceState = SERVICE_STATE_STARTED;
    mocked_ServiceLocator_GetService_fake.custom_fake = reporting_service;
    memset(&reporting_settings, 0, sizeof(reporting_settings));
    reporting_save_calls = 0; reporting_save_result = ESP_OK;
    ZigbeeRep_ConfigHandler(UI_STATE_ZB_REP_FAN, -128, false);
    TEST_ASSERT_EQUAL(0, reporting_settings.zigbee_reporting.fan_s);
    reporting_settings.zigbee_reporting.fan_s = ZB_REPORT_MAX_S;
    ZigbeeRep_ConfigHandler(UI_STATE_ZB_REP_FAN, 127, false);
    TEST_ASSERT_EQUAL(ZB_REPORT_MAX_S, reporting_settings.zigbee_reporting.fan_s);
    reporting_save_result = ESP_FAIL;
    ZigbeeRep_ConfigHandler(UI_STATE_ZB_REP_FAN, -1, false);
    TEST_ASSERT_EQUAL(ZB_REPORT_MAX_S, reporting_settings.zigbee_reporting.fan_s);
}

static PowerMode_t menu_power_mode;
static int32_t menu_power_result;
static unsigned menu_power_calls;
static PowerMode_t menu_get_power(IPowerManager *self) { (void)self; return menu_power_mode; }
static int32_t menu_set_power(IPowerManager *self, PowerMode_t mode) {
    (void)self; ++menu_power_calls;
    if (menu_power_result == POWER_OK) menu_power_mode = mode;
    return menu_power_result;
}
static IPowerManager menu_power = {.GetPowerMode = menu_get_power, .SetPowerMode = menu_set_power};
static IService *menu_power_locator(const char *name) {
    return strcmp(name, SVC_NAME_POWER_MANAGER) == 0 ? (IService *)&menu_power : NULL;
}
void test_SystemController_PowerMenuUsesCommonProfileTransaction(void) {
    controllerInstance.serviceState = SERVICE_STATE_STARTED;
    mocked_ServiceLocator_GetService_fake.custom_fake = menu_power_locator;
    menu_power_mode = POWER_MODE_BALANCED;
    menu_power_result = POWER_OK; menu_power_calls = 0;
    PowerProfile_ConfigHandler(UI_STATE_SET_POWER_PROFILE, 1, false);
    TEST_ASSERT_EQUAL(POWER_MODE_LOW_POWER, menu_power_mode);
    PowerProfile_ConfigHandler(UI_STATE_SET_POWER_PROFILE, 1, false);
    TEST_ASSERT_EQUAL(POWER_MODE_PERFORMANCE, menu_power_mode);
    PowerProfile_ConfigHandler(UI_STATE_SET_POWER_PROFILE, -1, false);
    TEST_ASSERT_EQUAL(POWER_MODE_LOW_POWER, menu_power_mode);
    TEST_ASSERT_EQUAL(3, menu_power_calls);
    PowerProfile_ConfigHandler(UI_STATE_SET_POWER_PROFILE, 1, true);
    TEST_ASSERT_EQUAL(3, menu_power_calls);
    menu_power_result = POWER_ERR_INVALID_STATE;
    PowerProfile_ConfigHandler(UI_STATE_SET_POWER_PROFILE, 1, false);
    TEST_ASSERT_EQUAL(POWER_MODE_LOW_POWER, menu_power_mode);
    menu_power_mode = POWER_MODE_UNKNOWN;
    PowerProfile_ConfigHandler(UI_STATE_SET_POWER_PROFILE, 1, false);
    TEST_ASSERT_EQUAL(4, menu_power_calls);
}

static esp_err_t display_menu_save(IConfigurationManager *self, const char *block,
    const ConfigurationFieldUpdate *fields, size_t count, bool persist) {
    (void)self; ++reporting_save_calls;
    TEST_ASSERT_EQUAL_STRING("system", block); TEST_ASSERT_EQUAL(1, count); TEST_ASSERT_TRUE(persist);
    TEST_ASSERT_EQUAL(offsetof(SystemBaseConfigs, oled_timeout_sec), fields[0].offset);
    TEST_ASSERT_EQUAL(sizeof(uint32_t), fields[0].size);
    if (reporting_save_result == ESP_OK)
        memcpy(&reporting_settings.system.oled_timeout_sec, fields[0].data, fields[0].size);
    return reporting_save_result;
}
static esp_err_t auto_exit_menu_save(IConfigurationManager *self,const char *block,
    const ConfigurationFieldUpdate *fields,size_t count,bool persist) {
    (void)self; ++reporting_save_calls;
    TEST_ASSERT_EQUAL_STRING("system",block); TEST_ASSERT_EQUAL(1,count); TEST_ASSERT_TRUE(persist);
    TEST_ASSERT_EQUAL(offsetof(SystemBaseConfigs,menu_auto_exit_enabled),fields[0].offset);
    TEST_ASSERT_EQUAL(sizeof(uint8_t),fields[0].size);
    if(reporting_save_result==ESP_OK) memcpy(&reporting_settings.system.menu_auto_exit_enabled,fields[0].data,fields[0].size);
    return reporting_save_result;
}
void test_SystemController_MenuAutoExitUsesSharedOwnerAndPreservesFailures(void) {
    controllerInstance.serviceState=SERVICE_STATE_STARTED;
    mocked_ServiceLocator_GetService_fake.custom_fake=reporting_service;
    reporting_config.UpdateFields=auto_exit_menu_save;
    reporting_settings.system.menu_auto_exit_enabled=1;
    reporting_save_calls=0; reporting_save_result=ESP_OK;
    MenuAutoExit_ConfigHandler(UI_STATE_SET_MENU_AUTO_EXIT,1,false);
    TEST_ASSERT_EQUAL(0,reporting_settings.system.menu_auto_exit_enabled);
    MenuAutoExit_ConfigHandler(UI_STATE_SET_MENU_AUTO_EXIT,-1,false);
    TEST_ASSERT_EQUAL(1,reporting_settings.system.menu_auto_exit_enabled);
    reporting_save_result=ESP_FAIL;
    MenuAutoExit_ConfigHandler(UI_STATE_SET_MENU_AUTO_EXIT,1,false);
    TEST_ASSERT_EQUAL(1,reporting_settings.system.menu_auto_exit_enabled);
    MenuAutoExit_ConfigHandler(UI_STATE_SET_MENU_AUTO_EXIT,1,true);
    TEST_ASSERT_EQUAL(3,reporting_save_calls);
    reporting_config.UpdateFields=reporting_save;
}
void test_SystemController_DisplayTimeoutMenuPersistsAndSaturates(void) {
    controllerInstance.serviceState = SERVICE_STATE_STARTED;
    mocked_ServiceLocator_GetService_fake.custom_fake = reporting_service;
    reporting_config.UpdateFields = display_menu_save;
    reporting_save_calls = 0; reporting_save_result = ESP_OK;
    reporting_settings.system.oled_timeout_sec = 20;
    DisplayTimeout_ConfigHandler(UI_STATE_SET_TIMEOUT, 1, false);
    TEST_ASSERT_EQUAL(21, reporting_settings.system.oled_timeout_sec);
    DisplayTimeout_ConfigHandler(UI_STATE_SET_TIMEOUT, -128, false);
    TEST_ASSERT_EQUAL(OLED_SLEEP_ZB_MIN_SEC, reporting_settings.system.oled_timeout_sec);
    DisplayTimeout_ConfigHandler(UI_STATE_SET_TIMEOUT, 127, false);
    TEST_ASSERT_EQUAL(OLED_SLEEP_ZB_MAX_SEC, reporting_settings.system.oled_timeout_sec);
    reporting_save_result = ESP_FAIL;
    DisplayTimeout_ConfigHandler(UI_STATE_SET_TIMEOUT, -1, false);
    TEST_ASSERT_EQUAL(OLED_SLEEP_ZB_MAX_SEC, reporting_settings.system.oled_timeout_sec);
    DisplayTimeout_ConfigHandler(UI_STATE_SET_TIMEOUT, -1, true);
    TEST_ASSERT_EQUAL(4, reporting_save_calls);
    reporting_config.UpdateFields = reporting_save;
}

static MenuManagerError system_registration_result;
static unsigned system_registration_calls;
static MenuManagerError system_removal_result;
static MenuManagerError register_system_menu(IMenuManager *self, const MenuPluginRegistration *entries, size_t count) {
    (void)self;
    system_registration_calls++;
    TEST_ASSERT_EQUAL(16, count);
    for (size_t i = 0; i < count; ++i) TEST_ASSERT_NOT_NULL(entries[i].legacy);
    return system_registration_result;
}
static MenuManagerError unregister_system_menu(IMenuManager *self, const MenuPluginRegistration *entries, size_t count) {
    (void)self; (void)entries; (void)count;
    return system_removal_result;
}
void test_SystemController_MenuFailurePreventsStartupAndAllowsRetry(void) {
    ISystemController *sc = GetSystemController();
    TEST_ASSERT_EQUAL(0, sc->base.Initialize(&sc->base, NULL));
    IMenuManager menu = {.RegisterPluginBatch = register_system_menu, .UnregisterPluginBatch = unregister_system_menu};
    controllerInstance.menuManager = &menu;
    controllerInstance.telemetry = NULL;
    system_registration_calls = 0;
    system_registration_result = MENU_ERR_BUSY;
    TEST_ASSERT_NOT_EQUAL(0, sc->base.Start(&sc->base));
    TEST_ASSERT_EQUAL(SERVICE_STATE_INITIALIZED, sc->base.GetState(&sc->base));
    TEST_ASSERT_EQUAL(0, mocked_xTaskCreate_fake.call_count);
    TEST_ASSERT_EQUAL(1, system_registration_calls);
    system_registration_result = MENU_OK;
    TEST_ASSERT_EQUAL(0, sc->base.Start(&sc->base));
    TEST_ASSERT_EQUAL(2, system_registration_calls);
    TEST_ASSERT_EQUAL(1, mocked_xTaskCreate_fake.call_count);
    controllerInstance.menuManager = NULL;
}

void test_SystemController_StopTimeoutPreservesOwnershipAndRejectsLateEvents(void) {
    ISystemController *sc = GetSystemController();
    TEST_ASSERT_EQUAL(0, sc->base.Initialize(&sc->base, &mock_registry));
    TEST_ASSERT_EQUAL(0, sc->base.Start(&sc->base));
    TaskHandle_t original = controllerInstance.worker.task;
    QueueHandle_t queue = controllerInstance.eventQueue;
    mocked_xQueueReceive_fake.custom_fake = NULL;
    mocked_xQueueReceive_fake.return_val = pdFALSE;
    TEST_ASSERT_NOT_EQUAL(0, sc->base.Stop(&sc->base));
    TEST_ASSERT_EQUAL_PTR(original, controllerInstance.worker.task);
    TEST_ASSERT_EQUAL_PTR(queue, controllerInstance.eventQueue);
    TEST_ASSERT_EQUAL(0, mocked_vTaskDelete_fake.call_count);
    unsigned sent = mocked_xQueueSend_fake.call_count;
    sc->HandleSystemEvent(sc, 100);
    TEST_ASSERT_EQUAL(sent, mocked_xQueueSend_fake.call_count);
    TEST_ASSERT_NOT_EQUAL(0, sc->base.Start(&sc->base));
    mocked_xQueueReceive_fake.custom_fake = acknowledge_system_stop;
    TEST_ASSERT_EQUAL(0, sc->base.Stop(&sc->base));
    TEST_ASSERT_NULL(controllerInstance.worker.task);
    TEST_ASSERT_EQUAL(0, sc->base.Start(&sc->base));
}

void test_SystemController_AllocationFailureAllowsCleanRetry(void) {
    ISystemController *sc = GetSystemController();
    mocked_xSemaphoreCreateMutex_fake.return_val = NULL;
    TEST_ASSERT_NOT_EQUAL(0, sc->base.Initialize(&sc->base, &mock_registry));
    TEST_ASSERT_EQUAL(SERVICE_STATE_UNINITIALIZED, sc->base.GetState(&sc->base));
    TEST_ASSERT_NULL(controllerInstance.eventQueue);
    mocked_xSemaphoreCreateMutex_fake.return_val = (void *)0x101;
    TEST_ASSERT_EQUAL(0, sc->base.Initialize(&sc->base, &mock_registry));
}

void test_SystemController_TaskCreationFailureRollsBackAndAllowsRetry(void) {
    ISystemController *sc = GetSystemController();
    TEST_ASSERT_EQUAL(0, sc->base.Initialize(&sc->base, &mock_registry));
    mocked_xTaskCreate_fake.custom_fake = NULL;
    mocked_xTaskCreate_fake.return_val = pdFAIL;
    TEST_ASSERT_NOT_EQUAL(0, sc->base.Start(&sc->base));
    TEST_ASSERT_EQUAL(SERVICE_STATE_INITIALIZED, sc->base.GetState(&sc->base));
    TEST_ASSERT_FALSE(controllerInstance.telemetryOwned);
    TEST_ASSERT_NULL(controllerInstance.worker.task);
    mocked_xTaskCreate_fake.custom_fake = create_system_test_task;
    TEST_ASSERT_EQUAL(0, sc->base.Start(&sc->base));
}

static unsigned recovery_restart_calls;
static int32_t recovery_restart_result;
static int32_t recovery_checked_restart(IPowerManager *self, uint32_t delay) {
    (void)self; TEST_ASSERT_EQUAL(0, delay); ++recovery_restart_calls; return recovery_restart_result;
}
static IPowerManager recovery_power = {.RestartAsync = recovery_checked_restart};
static IService *recovery_power_resolve(const char *name) {
    return !strcmp(name, SVC_NAME_POWER_MANAGER) ? &recovery_power.base : NULL;
}
void test_SystemController_RecoveryUsesOneElapsedDeadlineAndCanBeCancelled(void) {
    recovery_restart_calls = 0; recovery_restart_result = POWER_ERR_BUSY;
    mocked_ServiceLocator_GetService_fake.custom_fake = recovery_power_resolve;
    ISystemController *sc = GetSystemController();
    TEST_ASSERT_EQUAL(0, sc->base.Initialize(&sc->base, &mock_registry));
    TEST_ASSERT_EQUAL(0, sc->base.Start(&sc->base));
    RESET_FAKE(mocked_esp_timer_get_time);
    RESET_FAKE(mocked_esp_restart);
    mocked_esp_timer_get_time_fake.return_val = 1000000;
    unsigned tasks = mocked_xTaskCreate_fake.call_count;
    sc->SetSystemState(sc, SYSTEM_STATE_ERROR);
    mocked_esp_timer_get_time_fake.return_val = 5000000;
    sc->SetSystemState(sc, SYSTEM_STATE_ERROR);
    TEST_ASSERT_EQUAL(tasks, mocked_xTaskCreate_fake.call_count);
    TEST_ASSERT_TRUE(controllerInstance.recoveryStartedUs == 1000000);
    SC_CheckRecovery();
    TEST_ASSERT_EQUAL(0, mocked_esp_restart_fake.call_count);
    mocked_esp_timer_get_time_fake.return_val = 11000000;
    SC_CheckRecovery();
    TEST_ASSERT_TRUE(controllerInstance.recoveryPending);
    TEST_ASSERT_EQUAL(1, recovery_restart_calls);
    recovery_restart_result = POWER_OK;
    mocked_esp_timer_get_time_fake.return_val = 12000000;
    SC_CheckRecovery();
    SC_CheckRecovery();
    TEST_ASSERT_EQUAL(2, recovery_restart_calls);
    TEST_ASSERT_EQUAL(0, mocked_esp_restart_fake.call_count);
    sc->SetSystemState(sc, SYSTEM_STATE_READY);
    sc->SetSystemState(sc, SYSTEM_STATE_ERROR);
    sc->SetSystemState(sc, SYSTEM_STATE_READY);
    mocked_esp_timer_get_time_fake.return_val = 50000000;
    SC_CheckRecovery();
    TEST_ASSERT_EQUAL(2, recovery_restart_calls);
    TEST_ASSERT_EQUAL(0, mocked_esp_restart_fake.call_count);
    sc->SetSystemState(sc, (SystemState)-1);
    TEST_ASSERT_EQUAL(SYSTEM_STATE_READY, sc->GetSystemState(sc));
}

void test_SystemController_RecoveryBudgetIsSpacedAndCannotBeRearmedByRepeatedErrors(void) {
    recovery_restart_calls = 0; recovery_restart_result = POWER_ERR_BUSY;
    mocked_ServiceLocator_GetService_fake.custom_fake = recovery_power_resolve;
    ISystemController *sc = GetSystemController();
    TEST_ASSERT_EQUAL(0, sc->base.Initialize(&sc->base, &mock_registry));
    TEST_ASSERT_EQUAL(0, sc->base.Start(&sc->base));
    mocked_esp_timer_get_time_fake.return_val = 1000000;
    sc->SetSystemState(sc, SYSTEM_STATE_ERROR);
    mocked_esp_timer_get_time_fake.return_val = 11000000;
    SC_CheckRecovery(); SC_CheckRecovery();
    TEST_ASSERT_EQUAL(1, recovery_restart_calls);
    mocked_esp_timer_get_time_fake.return_val = 11999999;
    SC_CheckRecovery(); TEST_ASSERT_EQUAL(1, recovery_restart_calls);
    mocked_esp_timer_get_time_fake.return_val = 12000000;
    SC_CheckRecovery(); TEST_ASSERT_EQUAL(2, recovery_restart_calls);
    mocked_esp_timer_get_time_fake.return_val = 13000000;
    SC_CheckRecovery(); TEST_ASSERT_EQUAL(3, recovery_restart_calls);
    TEST_ASSERT_FALSE(controllerInstance.recoveryPending);
    sc->SetSystemState(sc, SYSTEM_STATE_ERROR);
    mocked_esp_timer_get_time_fake.return_val = 90000000;
    SC_CheckRecovery(); TEST_ASSERT_EQUAL(3, recovery_restart_calls);
    sc->SetSystemState(sc, SYSTEM_STATE_READY);
    sc->SetSystemState(sc, SYSTEM_STATE_ERROR);
    mocked_esp_timer_get_time_fake.return_val = 100000000;
    SC_CheckRecovery(); TEST_ASSERT_EQUAL(4, recovery_restart_calls);
}

static esp_err_t telemetry_menu_save(IConfigurationManager *self, const char *block,
    const ConfigurationFieldUpdate *fields, size_t count, bool persist) {
    (void)self; ++reporting_save_calls;
    TEST_ASSERT_EQUAL_STRING("system", block);
    TEST_ASSERT_EQUAL(1, count); TEST_ASSERT_TRUE(persist);
    TEST_ASSERT_EQUAL(offsetof(SystemBaseConfigs, telemetry_interval_ms), fields[0].offset);
    if (reporting_save_result == ESP_OK)
        memcpy(&reporting_settings.system.telemetry_interval_ms, fields[0].data, fields[0].size);
    return reporting_save_result;
}
void test_SystemController_TelemetryMenuSaturatesAndPreservesWorkerOnSaveFailure(void) {
    controllerInstance.serviceState = SERVICE_STATE_STARTED;
    controllerInstance.telemetry = GetSystemTelemetry();
    mocked_ServiceLocator_GetService_fake.custom_fake = reporting_service;
    reporting_config.UpdateFields = telemetry_menu_save;
    reporting_save_calls = 0; reporting_save_result = ESP_OK;
    reporting_settings.system.telemetry_interval_ms = 1000;
    Telemetry_ConfigHandler(UI_STATE_SET_TELEMETRY, -1, false);
    TEST_ASSERT_EQUAL(0, reporting_settings.system.telemetry_interval_ms);
    TEST_ASSERT_EQUAL(0, Telemetry_GetReportingInterval(GetSystemTelemetry()));
    Telemetry_ConfigHandler(UI_STATE_SET_TELEMETRY, 127, false);
    TEST_ASSERT_EQUAL(TELEMETRY_MAX_MS, reporting_settings.system.telemetry_interval_ms);
    TEST_ASSERT_EQUAL(TELEMETRY_MAX_MS, Telemetry_GetReportingInterval(GetSystemTelemetry()));
    reporting_save_result = ESP_FAIL;
    Telemetry_ConfigHandler(UI_STATE_SET_TELEMETRY, -1, false);
    TEST_ASSERT_EQUAL(TELEMETRY_MAX_MS, reporting_settings.system.telemetry_interval_ms);
    TEST_ASSERT_EQUAL(TELEMETRY_MAX_MS, Telemetry_GetReportingInterval(GetSystemTelemetry()));
    Telemetry_ConfigHandler(UI_STATE_SET_TIMEOUT, -1, false);
    TEST_ASSERT_EQUAL(3, reporting_save_calls);
    reporting_config.UpdateFields = reporting_save;
}

void test_SystemController_CLIRejectsMalformedStatesAndUnappliedRequests(void) {
    ISystemController *sc = GetSystemController();
    TEST_ASSERT_EQUAL(0, sc->base.Initialize(&sc->base, &mock_registry));
    TEST_ASSERT_EQUAL(0, sc->base.Start(&sc->base));
    const char *invalid[] = {"abc", "1x", "-1", "99999999999999999999999", "8", ""};
    char *args[] = {"sys", "setstate", NULL};
    for (unsigned i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        args[2] = (char *)invalid[i];
        TEST_ASSERT_NOT_EQUAL(0, sys_console_cmd_handler(3, args));
        TEST_ASSERT_EQUAL(SYSTEM_STATE_INITIALIZING, sc->GetSystemState(sc));
    }
    args[2] = "1";
    TEST_ASSERT_EQUAL(0, sys_console_cmd_handler(3, args));
    TEST_ASSERT_EQUAL(SYSTEM_STATE_READY, sc->GetSystemState(sc));
    mocked_xSemaphoreTake_fake.return_val = pdFALSE;
    args[2] = "0";
    TEST_ASSERT_NOT_EQUAL(0, sys_console_cmd_handler(3, args));
    mocked_xSemaphoreTake_fake.return_val = pdTRUE;
    TEST_ASSERT_EQUAL(0, sc->base.Stop(&sc->base));
    TEST_ASSERT_NOT_EQUAL(0, sys_console_cmd_handler(3, args));
}

void test_SystemController_MenuRemovalBusyKeepsOwnershipUntilRetry(void) {
    ISystemController *sc = GetSystemController();
    TEST_ASSERT_EQUAL(0, sc->base.Initialize(&sc->base, NULL));
    IMenuManager menu = {.RegisterPluginBatch = register_system_menu, .UnregisterPluginBatch = unregister_system_menu};
    controllerInstance.menuManager = &menu;
    system_registration_result = MENU_OK;
    system_removal_result = MENU_ERR_BUSY;
    TEST_ASSERT_EQUAL(0, sc->base.Start(&sc->base));
    TEST_ASSERT_NOT_EQUAL(0, sc->base.Stop(&sc->base));
    TEST_ASSERT_TRUE(controllerInstance.menuRegistered);
    TEST_ASSERT_TRUE(controllerInstance.telemetryOwned);
    TEST_ASSERT_NULL(controllerInstance.worker.task);
    TEST_ASSERT_NOT_NULL(telemetryInstance.worker.task);
    TEST_ASSERT_NOT_EQUAL(0, sc->base.Start(&sc->base));
    system_removal_result = MENU_OK;
    TEST_ASSERT_EQUAL(0, sc->base.Stop(&sc->base));
    TEST_ASSERT_FALSE(controllerInstance.menuRegistered);
    TEST_ASSERT_FALSE(controllerInstance.telemetryOwned);
    TEST_ASSERT_EQUAL(0, sc->base.Start(&sc->base));
    TEST_ASSERT_EQUAL(0, sc->base.Stop(&sc->base));
}

static BaseType_t acknowledge_controller_only(QueueHandle_t queue, void *item, TickType_t wait) {
    if (queue == telemetryInstance.worker.stopped) return pdFALSE;
    return acknowledge_system_stop(queue, item, wait);
}
void test_SystemController_TelemetryStopTimeoutRetainsDependencyForRetry(void) {
    ISystemController *sc = GetSystemController();
    TEST_ASSERT_EQUAL(0, sc->base.Initialize(&sc->base, NULL));
    TEST_ASSERT_EQUAL(0, sc->base.Start(&sc->base));
    TaskHandle_t telemetry_task = telemetryInstance.worker.task;
    mocked_xQueueReceive_fake.custom_fake = acknowledge_controller_only;
    TEST_ASSERT_NOT_EQUAL(0, sc->base.Stop(&sc->base));
    TEST_ASSERT_NULL(controllerInstance.worker.task);
    TEST_ASSERT_TRUE(controllerInstance.telemetryOwned);
    TEST_ASSERT_EQUAL_PTR(telemetry_task, telemetryInstance.worker.task);
    TEST_ASSERT_NOT_EQUAL(0, sc->base.Start(&sc->base));
    mocked_xQueueReceive_fake.custom_fake = acknowledge_system_stop;
    TEST_ASSERT_EQUAL(0, sc->base.Stop(&sc->base));
    TEST_ASSERT_FALSE(controllerInstance.telemetryOwned);
    TEST_ASSERT_EQUAL(0, sc->base.Start(&sc->base));
}

static unsigned shutdown_calls;
static int32_t reject_shutdown(IPowerManager *self, uint64_t duration) {
    (void)self;
    TEST_ASSERT_EQUAL_UINT(0, duration);
    ++shutdown_calls;
    return POWER_ERR_TIMEOUT;
}
static IService *shutdown_service(const char *name) {
    static IPowerManager power = {.EnterDeepSleep = reject_shutdown};
    return !strcmp(name, SVC_NAME_POWER_MANAGER) ? (IService *)&power : NULL;
}
void test_SystemController_ShutdownHasNoUnconfiguredSdkFallback(void) {
    ISystemController *system = GetSystemController();
    TEST_ASSERT_EQUAL_INT(0, system->base.Initialize(&system->base, NULL));
    TEST_ASSERT_EQUAL_INT(0, system->base.Start(&system->base));
    RESET_FAKE(mocked_esp_deep_sleep_start);
    RESET_FAKE(mocked_ServiceLocator_GetService);
    System_MenuHandler(UI_STATE_SHUTDOWN, 0, true);
    TEST_ASSERT_EQUAL_UINT(0, mocked_esp_deep_sleep_start_fake.call_count);
    shutdown_calls = 0;
    mocked_ServiceLocator_GetService_fake.custom_fake = shutdown_service;
    System_MenuHandler(UI_STATE_SHUTDOWN, 0, true);
    TEST_ASSERT_EQUAL_UINT(0, shutdown_calls);
    TEST_ASSERT_TRUE(queuedEvent.shutdown);
    TEST_ASSERT_TRUE(atomic_load(&controllerInstance.shutdownPending));
    SC_ProcessEvent(&queuedEvent);
    SC_CheckShutdown();
    TEST_ASSERT_EQUAL_UINT(1, shutdown_calls);
    TEST_ASSERT_FALSE(atomic_load(&controllerInstance.shutdownPending));
    TEST_ASSERT_EQUAL_UINT(0, mocked_esp_deep_sleep_start_fake.call_count);
}

void test_SystemController_ShutdownQueueBoundsDeadlineFailureAndStopCancellation(void) {
    ISystemController *system = GetSystemController();
    TEST_ASSERT_EQUAL_INT(0, system->base.Initialize(&system->base, NULL));
    TEST_ASSERT_EQUAL_INT(0, system->base.Start(&system->base));
    mocked_ServiceLocator_GetService_fake.custom_fake = shutdown_service;
    shutdown_calls = 0;
    unsigned tasks = mocked_xTaskCreate_fake.call_count;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, system->RequestShutdown(system, SYSTEM_CONTROLLER_SHUTDOWN_MAX_DELAY_MS + 1));
    mocked_xQueueSend_fake.custom_fake = NULL;
    mocked_xQueueSend_fake.return_val = pdFALSE;
    TEST_ASSERT_EQUAL(ESP_ERR_NO_MEM, system->RequestShutdown(system, 1000));
    TEST_ASSERT_FALSE(atomic_load(&controllerInstance.shutdownPending));
    mocked_xQueueSend_fake.custom_fake = captureEvent;
    TEST_ASSERT_EQUAL(ESP_OK, system->RequestShutdown(system, 1000));
    TEST_ASSERT_NOT_EQUAL(ESP_OK, system->RequestShutdown(system, 0));
    TEST_ASSERT_EQUAL_UINT(tasks, mocked_xTaskCreate_fake.call_count);
    mocked_esp_timer_get_time_fake.return_val = 1000000;
    controllerInstance.recoveryPending = true;
    SC_ProcessEvent(&queuedEvent);
    TEST_ASSERT_FALSE(controllerInstance.recoveryPending);
    mocked_esp_timer_get_time_fake.return_val = 1999999;
    SC_CheckShutdown();
    TEST_ASSERT_EQUAL_UINT(0, shutdown_calls);
    mocked_esp_timer_get_time_fake.return_val = 2000000;
    SC_CheckShutdown();
    TEST_ASSERT_EQUAL_UINT(1, shutdown_calls);
    SC_CheckShutdown();
    TEST_ASSERT_EQUAL_UINT(1, shutdown_calls);
    TEST_ASSERT_EQUAL(ESP_OK, system->RequestShutdown(system, 1000));
    TEST_ASSERT_EQUAL_INT(0, system->base.Stop(&system->base));
    TEST_ASSERT_FALSE(atomic_load(&controllerInstance.shutdownPending));
    TEST_ASSERT_FALSE(controllerInstance.shutdownScheduled);
    TEST_ASSERT_NOT_EQUAL(ESP_OK, system->RequestShutdown(system, 0));
}

static unsigned menu_reset_calls, menu_network_calls;
static esp_err_t menu_reset_result;
static esp_err_t menu_forward_factory(IApplicationManager *self) {
  (void)self; ++menu_reset_calls; return menu_reset_result;
}
static IApplicationManager menu_app = {.TriggerFactoryReset = menu_forward_factory};
static unsigned menu_restart_calls;
static int32_t menu_forward_restart(IPowerManager *self, uint32_t delay) {
  (void)self; TEST_ASSERT_EQUAL(0, delay); ++menu_restart_calls; return POWER_OK;
}
static IPowerManager menu_factory_restart_power = {.RestartAsync = menu_forward_restart};
static IService *menu_resolve(const char *name) {
  if (!strcmp(name, SVC_NAME_APPLICATION_MANAGER)) return (IService *)&menu_app;
  if (!strcmp(name, SVC_NAME_POWER_MANAGER)) return (IService *)&menu_factory_restart_power;
  return NULL;
}
void test_SystemMenu_ActionsAreQueuedWithoutImmediateReset(void) {
  mocked_ServiceLocator_GetService_fake.custom_fake = menu_resolve;
  menu_restart_calls = 0;
  controllerInstance.serviceState = SERVICE_STATE_STARTED;
  controllerInstance.eventQueue = (void *)0x1;
  RESET_FAKE(mocked_esp_restart);
  System_MenuHandler(UI_STATE_RESTART, 0, true);
  TEST_ASSERT_EQUAL(0, mocked_esp_restart_fake.call_count);
  TEST_ASSERT_TRUE(queuedEvent.menuAction);
  SC_ProcessEvent(&queuedEvent);
  TEST_ASSERT_EQUAL(0, mocked_esp_restart_fake.call_count);
  TEST_ASSERT_EQUAL(1, menu_restart_calls);
}
void test_SystemMenu_FactoryResetFailureDoesNotLeaveNetwork(void) {
  mocked_ServiceLocator_GetService_fake.custom_fake = menu_resolve;
  menu_reset_calls = menu_network_calls = 0; menu_reset_result = ESP_FAIL;
  RESET_FAKE(mocked_esp_restart);
  SystemEvent event = {.menuAction = true, .payload = UI_STATE_FACTORY_RESET};
  SC_ProcessEvent(&event);
  TEST_ASSERT_EQUAL(1, menu_reset_calls);
  TEST_ASSERT_EQUAL(0, menu_network_calls);
  TEST_ASSERT_EQUAL(0, mocked_esp_restart_fake.call_count);
}
void test_SystemMenu_FactoryResetForwardsToCoordinatorWithoutForcedRestart(void) {
  mocked_ServiceLocator_GetService_fake.custom_fake = menu_resolve;
  menu_reset_calls = menu_network_calls = 0; menu_reset_result = ESP_OK;
  RESET_FAKE(mocked_esp_restart);
  SystemEvent event = {.menuAction = true, .payload = UI_STATE_FACTORY_RESET};
  SC_ProcessEvent(&event);
  TEST_ASSERT_EQUAL(1, menu_reset_calls);
  TEST_ASSERT_EQUAL(0, menu_network_calls);
  TEST_ASSERT_EQUAL(0, mocked_esp_restart_fake.call_count);
}

void test_SystemMenu_FactoryResetMissingCoordinatorPreservesSettings(void) {
  mocked_ServiceLocator_GetService_fake.custom_fake = menu_resolve;
  menu_reset_calls = menu_network_calls = 0;
  menu_app.TriggerFactoryReset = NULL;
  SystemEvent event = {.menuAction = true, .payload = UI_STATE_FACTORY_RESET};
  SC_ProcessEvent(&event);
  menu_app.TriggerFactoryReset = menu_forward_factory;
  TEST_ASSERT_EQUAL(0, menu_reset_calls);
  TEST_ASSERT_EQUAL(0, menu_network_calls);
}
void test_SystemMenu_QueueRejectionDoesNotRunAction(void) {
  controllerInstance.serviceState = SERVICE_STATE_STARTED;
  controllerInstance.eventQueue = (void *)0x1;
  mocked_xQueueSend_fake.custom_fake = NULL;
  mocked_xQueueSend_fake.return_val = pdFAIL;
  RESET_FAKE(mocked_esp_restart);
  System_MenuHandler(UI_STATE_RESTART, 0, true);
  TEST_ASSERT_EQUAL(0, mocked_esp_restart_fake.call_count);
  TEST_ASSERT_EQUAL(1, mocked_xQueueSend_fake.call_count);
}

static esp_err_t language_menu_save(IConfigurationManager *self,const char *block,
    const ConfigurationFieldUpdate *fields,size_t count,bool persist) {
    (void)self;++reporting_save_calls;
    TEST_ASSERT_EQUAL_STRING("localization",block);
    TEST_ASSERT_EQUAL(1,count);TEST_ASSERT_TRUE(persist);
    TEST_ASSERT_EQUAL(offsetof(LocalizationConfigs,locale),fields[0].offset);
    TEST_ASSERT_EQUAL(sizeof(reporting_settings.localization.locale),fields[0].size);
    const char *code=fields[0].data;
    TEST_ASSERT_NOT_NULL(Localization_Find(code));
    if(reporting_save_result==ESP_OK) {
        memcpy(reporting_settings.localization.locale,code,fields[0].size);
        TEST_ASSERT_TRUE(Localization_Apply(code));
    }
    return reporting_save_result;
}
void test_SystemController_LanguageMenuUsesCheckedConfigurationOwner(void) {
    controllerInstance.serviceState=SERVICE_STATE_STARTED;
    mocked_ServiceLocator_GetService_fake.custom_fake=reporting_service;
    reporting_config.UpdateFields=language_menu_save;
    memset(&reporting_settings,0,sizeof(reporting_settings));
    strcpy(reporting_settings.localization.locale,"tr");
    TEST_ASSERT_TRUE(Localization_Apply("tr"));
    reporting_save_calls=0;reporting_save_result=ESP_OK;
    Language_ConfigHandler(UI_STATE_SET_LANGUAGE,1,false);
    TEST_ASSERT_EQUAL_STRING("en",reporting_settings.localization.locale);
    TEST_ASSERT_EQUAL_STRING("en",Localization_Snapshot()->code);
    Language_ConfigHandler(UI_STATE_SET_LANGUAGE,-1,false);
    TEST_ASSERT_EQUAL_STRING("tr",reporting_settings.localization.locale);
    reporting_save_result=ESP_FAIL;
    Language_ConfigHandler(UI_STATE_SET_LANGUAGE,1,false);
    TEST_ASSERT_EQUAL_STRING("tr",reporting_settings.localization.locale);
    TEST_ASSERT_EQUAL_STRING("tr",Localization_Snapshot()->code);
    unsigned calls=reporting_save_calls;
    Language_ConfigHandler(UI_STATE_SET_LANGUAGE,1,true);
    Language_ConfigHandler(UI_STATE_SET_LANGUAGE,0,false);
    Language_ConfigHandler(UI_STATE_SET_TIMEOUT,1,false);
    controllerInstance.serviceState=SERVICE_STATE_STOPPED;
    Language_ConfigHandler(UI_STATE_SET_LANGUAGE,1,false);
    TEST_ASSERT_EQUAL(calls,reporting_save_calls);
    reporting_config.UpdateFields=reporting_save;
}

#include "Infrastructure/support/console_capture.h"
void test_SystemController_MachineContractAndResponseLanguage(void) {
    ISystemController *sc = GetSystemController();
    TEST_ASSERT_EQUAL(0, sc->base.Initialize(&sc->base, &mock_registry));
    TEST_ASSERT_EQUAL(0, sc->base.Start(&sc->base));
    const LocalizationCatalog *baseline = Localization_Snapshot();
    char output[4096]; char *status[] = {"sys", "status", "--machine"};
    for (unsigned language = 0; language < 2; ++language) {
        TEST_ASSERT_TRUE(Localization_Apply(language ? "en" : "tr"));
        TEST_ASSERT_EQUAL(0, test_capture_console(sys_console_cmd_handler, 3, status, output, sizeof(output)));
        TEST_ASSERT_EQUAL_STRING("RACOOL_CLI v=1 type=system_status state=0\n", output);
        TEST_ASSERT_EQUAL(0, test_capture_console(sys_console_cmd_handler, 2, status, output, sizeof(output)));
        TEST_ASSERT_NOT_NULL(strstr(output, language ? "SystemController Status" : "Sistem Denetleyicisi Durumu"));
    }
    char *set[] = {"sys", "setstate", "invalid", "--machine"};
    TEST_ASSERT_NOT_EQUAL(0, test_capture_console(sys_console_cmd_handler, 4, set, output, sizeof(output)));
    TEST_ASSERT_EQUAL_STRING("RACOOL_CLI v=1 type=system_state result=rejected state=-1 reason=invalid\n", output);
    set[2] = "1";
    TEST_ASSERT_EQUAL(0, test_capture_console(sys_console_cmd_handler, 4, set, output, sizeof(output)));
    TEST_ASSERT_NOT_NULL(strstr(output, "RACOOL_CLI v=1 type=system_state result=applied state=1\n"));
    TEST_ASSERT_EQUAL_STRING("RACOOL_CLI v=1 type=system_state result=applied state=1\n", strstr(output, "RACOOL_CLI"));
    mocked_xSemaphoreTake_fake.return_val = pdFALSE; set[2] = "0";
    TEST_ASSERT_NOT_EQUAL(0, test_capture_console(sys_console_cmd_handler, 4, set, output, sizeof(output)));
    TEST_ASSERT_NOT_NULL(strstr(output, "result=rejected"));
    mocked_xSemaphoreTake_fake.return_val = pdTRUE;
    TEST_ASSERT_TRUE(Localization_Apply(baseline->code));
    TEST_ASSERT_EQUAL(0, sc->base.Stop(&sc->base));
}

static void ignored_telemetry_interval(ISystemTelemetry *self,uint32_t interval) {
    (void)self;(void)interval;
}
static uint32_t ignored_telemetry_readback(ISystemTelemetry *self) { (void)self;return 0; }
void test_SystemController_MenuSaveRequiresTelemetryRuntimeReadback(void) {
    controllerInstance.serviceState=SERVICE_STATE_STARTED;
    mocked_ServiceLocator_GetService_fake.custom_fake=reporting_service;
    reporting_settings.system.telemetry_interval_ms=20000;
    ISystemTelemetry ignored={.SetReportingInterval=ignored_telemetry_interval,.GetReportingInterval=ignored_telemetry_readback};
    controllerInstance.telemetry=&ignored;
    Telemetry_ConfigHandler(UI_STATE_SET_TELEMETRY,0,true);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE,MenuEditSession_Result(UI_STATE_SET_TELEMETRY));
    ignored.GetReportingInterval=NULL;
    Telemetry_ConfigHandler(UI_STATE_SET_TELEMETRY,0,true);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE,MenuEditSession_Result(UI_STATE_SET_TELEMETRY));
    controllerInstance.telemetry=GetSystemTelemetry();
    const uint32_t intervals[]={0,5000};
    for(unsigned i=0;i<2;++i) {
        reporting_settings.system.telemetry_interval_ms=intervals[i];
        Telemetry_ConfigHandler(UI_STATE_SET_TELEMETRY,0,true);
        TEST_ASSERT_EQUAL(ESP_OK,MenuEditSession_Result(UI_STATE_SET_TELEMETRY));
        TEST_ASSERT_EQUAL(intervals[i],controllerInstance.telemetry->GetReportingInterval(controllerInstance.telemetry));
    }
    controllerInstance.serviceState=SERVICE_STATE_STOPPED;
    Telemetry_ConfigHandler(UI_STATE_SET_TELEMETRY,0,true);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE,MenuEditSession_Result(UI_STATE_SET_TELEMETRY));
}
