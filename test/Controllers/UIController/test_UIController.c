#include "ui_fixture.h"

void test_BootWaitsForLateReadinessWithinBoundedStartupBudget(void) {
  mockSystemState = SYSTEM_STATE_INITIALIZING;
  BootAnimation_Start();
  BootAnimation_Tick(100);
  BootAnimation_Tick(500);
  BootAnimation_Tick(2300);
  BootAnimation_Tick(3700);
  BootAnimation_Tick(13000);
  TEST_ASSERT_TRUE(BootAnimation_IsRunning());
  TEST_ASSERT_FALSE(atomic_load(&s_systemReady));
  BootAnimation_NotifyReady();
  BootAnimation_Tick(14000);
  BootAnimation_Tick(15000);
  BootAnimation_Tick(15300);
  BootAnimation_Tick(15600);
  TEST_ASSERT_FALSE(BootAnimation_IsRunning());
  BootAnimation_Start();
  BootAnimation_Tick(100);
  BootAnimation_Tick(100 + BOOT_MAX_DURATION_MS);
  TEST_ASSERT_FALSE(BootAnimation_IsRunning());
  mockSystemState = SYSTEM_STATE_READY;
}

void test_BootReadinessNotificationDoesNotInventServiceHealth(void) {
  BootAnimation_Start();
  TEST_ASSERT_FALSE(atomic_load(&s_systemReady));
  BootAnimation_NotifyReady();
  TEST_ASSERT_TRUE(atomic_load(&s_systemReady));
  TEST_ASSERT_EQUAL(BOOT_ANIM_STATE_FLASH, atomic_load(&s_animState));
  BootAnimation_Start();
  TEST_ASSERT_FALSE(atomic_load(&s_systemReady));
}

#include <stdarg.h>
static char console_output[4096];
static int capture_console(const char *format, ...) {
  size_t used = strlen(console_output);
  va_list args; va_start(args, format);
  int result = vsnprintf(console_output + used, sizeof(console_output) - used, format, args);
  va_end(args); return result;
}
#define TAG UIConsoleTestTag
#define printf capture_console
#include "Controllers/UIController/UIConsoleCommands.c"
#undef printf
#undef TAG
#include "Infrastructure/support/console_registration_fixture.h"
void test_UIRegistration_RejectsMissingBoundary(void) {
  registration_calls = registration_fail_at = 0;
  check_registration_inputs(UIConsoleCommands_Register);
}
void test_UIRegistration_PreservesEveryFailureAndAllowsRetry(void) {
  check_registration_failures(UIConsoleCommands_Register, 2);
}
static SystemSettings console_settings;
static esp_err_t console_snapshot_result;
static esp_err_t console_snapshot(IConfigurationManager *self, SystemSettings *out) {
  (void)self; *out = console_settings; return console_snapshot_result;
}
static IConfigurationManager console_config = {.GetSnapshot = console_snapshot};
void test_UIConsoleMachineConfigIsStableAndDoesNotWakeScreen(void);
void test_UIConsole_ConfigUsesSnapshotAndRejectsUnavailableSettings(void) {
  TEST_ASSERT_TRUE(Localization_Apply("en"));
  mocked_ConfigurationManager_GetInstance_fake.return_val = &console_config;
  console_snapshot_result = ESP_OK;
  console_settings.system.oled_timeout_sec = 20;
  console_settings.system.display_mode = 1;
  uiControllerInstance.config.screenTimeoutSec = 30;
  console_output[0] = 0;
  TEST_ASSERT_EQUAL(0, do_ui_config(0, NULL));
  TEST_ASSERT_NOT_NULL(strstr(console_output, "Screen Timeout:      20 sec"));
  console_snapshot_result = ESP_FAIL; console_output[0] = 0;
  TEST_ASSERT_EQUAL(1, do_ui_config(0, NULL));
  TEST_ASSERT_NULL(strstr(console_output, "Screen Timeout:"));
  mocked_ConfigurationManager_GetInstance_fake.return_val = NULL;
  TEST_ASSERT_TRUE(Localization_Apply("tr"));
}

void test_UIConsoleMachineConfigIsStableAndDoesNotWakeScreen(void) {
  mocked_ConfigurationManager_GetInstance_fake.return_val=&console_config;
  console_snapshot_result=ESP_OK;console_settings.system.oled_timeout_sec=20;console_settings.system.display_mode=1;
  uiControllerInstance.isScreenOn=false;
  char *args[]={"UI","CONFIG","--machine"};
  for (unsigned language=0;language<2;++language) {
    TEST_ASSERT_TRUE(Localization_Apply(language ? "en" : "tr"));console_output[0]=0;
    TEST_ASSERT_EQUAL(0,cmd_ui(3,args));
    TEST_ASSERT_NOT_NULL(strstr(console_output,"RACOOL_CLI v=1 type=ui_config screen_timeout_s=20 display_mode=1"));
    TEST_ASSERT_FALSE(uiControllerInstance.isScreenOn);TEST_ASSERT_EQUAL(0,mocked_xQueueSend_fake.call_count);
  }
  console_snapshot_result=ESP_FAIL;console_output[0]=0;
  TEST_ASSERT_EQUAL(1,cmd_ui(3,args));TEST_ASSERT_NULL(strstr(console_output,"type=ui_config"));
  mocked_ConfigurationManager_GetInstance_fake.return_val=NULL;TEST_ASSERT_TRUE(Localization_Apply("tr"));
}

void setUp(void) { UIFixture_SetUp(); }
void tearDown(void) { UIFixture_TearDown(); }

void test_UIController_MenuRowsRetryAllocationAndExceedSixteen(void) {
  TEST_ASSERT_TRUE(ScreenMenu_Create());
  uiControllerInstance.menu = &mockMenuMgr.base;
  SystemSettings settings = {0};
  /* Twenty visible injected rows; retired upper-bound IDs are filtered. */
  ui_menu_items = 23;
  unsigned base = ui_live_nodes;
  for (unsigned failure = 1; failure <= 40; ++failure) {
    ui_test_clean(uiControllerInstance.menuList);
    ui_fail_at = ui_alloc_calls + failure;
    ScreenMenu_Update(&settings, 25.0f,true,Localization_Snapshot());
    TEST_ASSERT_EQUAL(0, ui_test_child_count(uiControllerInstance.menuList));
    TEST_ASSERT_EQUAL(base, ui_live_nodes);
    ui_fail_at = 0;
    ScreenMenu_Update(&settings, 25.0f,true,Localization_Snapshot());
    TEST_ASSERT_EQUAL(20, ui_test_child_count(uiControllerInstance.menuList));
    unsigned calls = ui_alloc_calls;
    ScreenMenu_Update(&settings, 26.0f,true,Localization_Snapshot());
    TEST_ASSERT_EQUAL(calls, ui_alloc_calls);
  }
  ui_menu_items = 3;
  ScreenMenu_Update(&settings, 25.0f,true,Localization_Snapshot());
  TEST_ASSERT_EQUAL(3, ui_test_child_count(uiControllerInstance.menuList));
}

void test_UIController_RendererRetriesUnchangedMenuAfterAllocationFailure(void) {
  TEST_ASSERT_TRUE(ScreenMenu_Create());
  uiControllerInstance.menu = &mockMenuMgr.base;
  uiControllerInstance.oled = &ui_test_oled;
  mockMenuMgr.currentState = UI_STATE_CAT_TEMP;
  BootAnimation_Skip();
  renderValid = false;
  ui_menu_items = 2;
  SystemSettings settings = {0};
  ui_fail_at = ui_alloc_calls + 2;
  UIRenderer_Update(20000, &settings, 25, 24, 30);
  TEST_ASSERT_FALSE(renderValid);
  TEST_ASSERT_EQUAL(0, ui_test_child_count(uiControllerInstance.menuList));
  ui_fail_at = 0;
  UIRenderer_Update(20020, &settings, 25, 24, 30);
  TEST_ASSERT_TRUE(renderValid);
  TEST_ASSERT_EQUAL(2, ui_test_child_count(uiControllerInstance.menuList));
}

void test_UIController_RejectsInvalidStartupConfigurationBeforeAllocation(void) {
  IUIController *ui = UIController_GetModule();
  TEST_ASSERT_EQUAL(0, UI_TimeoutTicks(0));
  TEST_ASSERT_TRUE(UI_TimeoutTicks(1) >= 1);
  const UIControllerConfig defaults = UIController_GetDefaultConfig();
  UIControllerConfig invalid[15];
  for (unsigned i = 0; i < 15; ++i) invalid[i] = defaults;
  invalid[0].taskStackSize = 0;
  invalid[1].taskStackSize = UINT32_MAX;
  invalid[2].eventQueueSize = 0;
  invalid[3].eventQueueSize = UINT32_MAX;
  invalid[4].refreshIntervalMs = 0;
  invalid[5].refreshIntervalMs = UINT32_MAX;
  invalid[6].mutexTimeoutMs = UINT32_MAX;
  invalid[7].screenTimeoutSec = 0;
  invalid[8].screenTimeoutSec = UINT32_MAX;
  invalid[9].healthCheckIntervalMs = 0;
  invalid[10].healthCheckIntervalMs = UINT32_MAX;
  invalid[11].tempRefreshHysteresis = NAN;
  invalid[12].tempRefreshHysteresis = -1;
  invalid[13].tempWarmMin = INFINITY;
  invalid[14].tempFreeze = defaults.tempDanger + 1;
  unsigned queues = mocked_xQueueCreate_fake.call_count;
  for (unsigned i = 0; i < 15; ++i) {
    TEST_ASSERT_NOT_EQUAL(0, ui->InitializeWithConfig(ui, invalid[i], NULL));
    TEST_ASSERT_EQUAL(SERVICE_STATE_UNINITIALIZED, ui->base.GetState(&ui->base));
    TEST_ASSERT_EQUAL(queues, mocked_xQueueCreate_fake.call_count);
  }
  UIControllerConfig valid = defaults;
  valid.refreshIntervalMs = 1;
  valid.mutexTimeoutMs = 0; /* An intentional nonblocking mutex request remains valid. */
  TEST_ASSERT_EQUAL(0, ui->InitializeWithConfig(ui, valid, NULL));
}

void test_UIController_RejectsDrawingWithoutOLEDLock(void) {
  uiControllerInstance.oled = &ui_test_oled;
  ui_lock_failure = true;
  TEST_ASSERT_FALSE(UIRenderer_CreateUI());
  TEST_ASSERT_EQUAL(0, ui_alloc_calls);
  TEST_ASSERT_EQUAL(0, ui_lock_depth);
  ui_lock_failure = false;
  TEST_ASSERT_TRUE(ScreenMenu_Create());
  uiControllerInstance.menu = &mockMenuMgr.base;
  mockMenuMgr.currentState = UI_STATE_CAT_TEMP;
  BootAnimation_Skip();
  ui_menu_items = 2;
  SystemSettings settings = {0};
  unsigned before = ui_alloc_calls;
  renderValid = false;
  ui_lock_failure = true;
  UIRenderer_Update(30000, &settings, 25, 24, 30);
  TEST_ASSERT_EQUAL(before, ui_alloc_calls);
  TEST_ASSERT_EQUAL(0, ui_lock_depth);
  TEST_ASSERT_FALSE(renderValid);
  ui_lock_failure = false;
  UIRenderer_Update(30020, &settings, 25, 24, 30);
  TEST_ASSERT_EQUAL(2, ui_test_child_count(uiControllerInstance.menuList));
}

static esp_err_t ui_power_result;
static esp_err_t ui_test_power(IOLED *self, bool on) { (void)self; (void)on; return ui_power_result; }
void test_UIController_ScreenStateChangesOnlyAfterAppliedPower(void) {
  IOLED oled = {.SetPower = ui_test_power};
  uiControllerInstance.oled = &oled;
  uiControllerInstance.isScreenOn = false;
  ui_power_result = ESP_ERR_TIMEOUT;
  TEST_ASSERT_FALSE(UI_ApplyScreenPower(true));
  TEST_ASSERT_FALSE(uiControllerInstance.isScreenOn);
  ui_power_result = ESP_OK;
  TEST_ASSERT_TRUE(UI_ApplyScreenPower(true));
  TEST_ASSERT_TRUE(uiControllerInstance.isScreenOn);
  ui_power_result = ESP_FAIL;
  TEST_ASSERT_FALSE(UI_ApplyScreenPower(false));
  TEST_ASSERT_TRUE(uiControllerInstance.isScreenOn);
  uiControllerInstance.oled = NULL;
}

void test_UIController_GetModule(void) {
  IUIController *module = UIController_GetModule();
  TEST_ASSERT_NOT_NULL(module);
  TEST_ASSERT_EQUAL(SERVICE_STATE_UNINITIALIZED, uiControllerInstance.state);
}

void test_UIController_Initialize(void) {
  IUIController *module = UIController_GetModule();
  UIControllerConfig config = UIController_GetDefaultConfig();
  
  int32_t res = module->InitializeWithConfig(module, config, NULL);
  TEST_ASSERT_EQUAL(0, res);
  TEST_ASSERT_EQUAL(SERVICE_STATE_INITIALIZED, uiControllerInstance.state);
  TEST_ASSERT_NOT_NULL(uiControllerInstance.eventQueue);
  TEST_ASSERT_NOT_NULL(uiControllerInstance.mutex);

  module->base.Stop((IService *)module);
}

void test_UIController_PostEvent(void) {
  IUIController *module = UIController_GetModule();
  UIControllerConfig config = UIController_GetDefaultConfig();
  module->InitializeWithConfig(module, config, NULL);

  UIEvent event;
  event.type = UI_EVENT_TEMP_UPDATE;
  event.data.temp.objectTemp = 26.5f;
  event.data.temp.ambientTemp = 23.2f;
  event.data.temp.cpuTemp = 36.1f;

  int32_t res = module->PostEvent(module, &event);
  TEST_ASSERT_EQUAL(0, res);

  module->base.Stop((IService *)module);
}

void test_UIController_IsScreenOn(void) {
  IUIController *module = UIController_GetModule();
  UIControllerConfig config = UIController_GetDefaultConfig();
  module->InitializeWithConfig(module, config, NULL);

  TEST_ASSERT_TRUE(module->IsScreenOn(module));

  module->base.Stop((IService *)module);
}

void test_UIController_OnRotaryClicked(void) {
  IUIController *module = UIController_GetModule();
  UIControllerConfig config = UIController_GetDefaultConfig();
  module->InitializeWithConfig(module, config, NULL);

  RESET_FAKE(mocked_xQueueSend);
  mocked_xQueueSend_fake.custom_fake = custom_xQueueSend;

  BusEvent event = { .eventName = RE_EVENT_BUTTON_CLICK };
  UIController_OnRotaryClicked(event);

  TEST_ASSERT_EQUAL(1, mocked_xQueueSend_fake.call_count);
  TEST_ASSERT_EQUAL(UI_EVENT_ENCODER_CLICK, lastSentUIEvent.type);

  module->base.Stop((IService *)module);
}

void test_UIController_OnRotaryRotated(void) {
  IUIController *module = UIController_GetModule();
  UIControllerConfig config = UIController_GetDefaultConfig();
  module->InitializeWithConfig(module, config, NULL);

  RESET_FAKE(mocked_xQueueSend);
  mocked_xQueueSend_fake.custom_fake = custom_xQueueSend;

  RotaryEncoderRotationEvent rotEvent = { .steps = 2 };
  BusEvent event = {
    .eventName = RE_EVENT_ROTATED,
    .data = &rotEvent,
    .dataSize = sizeof(rotEvent)
  };
  UIController_OnRotaryRotated(event);

  TEST_ASSERT_EQUAL(1, mocked_xQueueSend_fake.call_count);
  TEST_ASSERT_EQUAL(UI_EVENT_ENCODER_MOVE, lastSentUIEvent.type);
  TEST_ASSERT_EQUAL(2, lastSentUIEvent.data.encoder.difference);

  module->base.Stop((IService *)module);
}

void test_UIController_OnIRTempUpdated(void) {
  IUIController *module = UIController_GetModule();
  UIControllerConfig config = UIController_GetDefaultConfig();
  module->InitializeWithConfig(module, config, NULL);

  RESET_FAKE(mocked_xQueueSend);
  mocked_xQueueSend_fake.custom_fake = custom_xQueueSend;

  float temps[2] = { 28.5f, 22.1f };
  BusEvent event = {
    .eventName = EVENT_IR_TEMP_UPDATED,
    .data = temps,
    .dataSize = sizeof(temps)
  };
  UIController_OnIRTempUpdated(event);

  TEST_ASSERT_EQUAL(1, mocked_xQueueSend_fake.call_count);
  TEST_ASSERT_EQUAL(UI_EVENT_TEMP_UPDATE, lastSentUIEvent.type);
  TEST_ASSERT_EQUAL_FLOAT(28.5f, lastSentUIEvent.data.temp.objectTemp);
  TEST_ASSERT_EQUAL_FLOAT(22.1f, lastSentUIEvent.data.temp.ambientTemp);

  module->base.Stop((IService *)module);
}

void test_UIController_OnInternalTempUpdated(void) {
  IUIController *module = UIController_GetModule();
  UIControllerConfig config = UIController_GetDefaultConfig();
  module->InitializeWithConfig(module, config, NULL);

  RESET_FAKE(mocked_xQueueSend);
  mocked_xQueueSend_fake.custom_fake = custom_xQueueSend;

  float cpuTemp = 36.5f;
  BusEvent event = {
    .eventName = EVENT_INTERNAL_TEMP_UPDATED,
    .data = &cpuTemp,
    .dataSize = sizeof(cpuTemp)
  };
  UIController_OnInternalTempUpdated(event);

  TEST_ASSERT_EQUAL(1, mocked_xQueueSend_fake.call_count);
  TEST_ASSERT_EQUAL(UI_EVENT_TEMP_UPDATE, lastSentUIEvent.type);
  TEST_ASSERT_EQUAL_FLOAT(36.5f, lastSentUIEvent.data.temp.cpuTemp);

  module->base.Stop((IService *)module);
}

void test_UIController_OnRelayStateChanged(void) {
  IUIController *module = UIController_GetModule();
  UIControllerConfig config = UIController_GetDefaultConfig();
  module->InitializeWithConfig(module, config, NULL);

  RESET_FAKE(mocked_xQueueSend);
  mocked_xQueueSend_fake.custom_fake = custom_xQueueSend;

  bool isOpen = true;
  BusEvent event = {
    .eventName = EVENT_RELAY_STATE_CHANGED,
    .data = &isOpen,
    .dataSize = sizeof(isOpen)
  };
  UIController_OnRelayStateChanged(event);

  TEST_ASSERT_EQUAL(1, mocked_xQueueSend_fake.call_count);
  TEST_ASSERT_EQUAL(UI_EVENT_RELAY_STATE, lastSentUIEvent.type);
  TEST_ASSERT_TRUE(lastSentUIEvent.data.relay.isOpen);

  module->base.Stop((IService *)module);
}

void test_UIController_OnConfigChanged(void) {
  IUIController *module = UIController_GetModule();
  UIControllerConfig config = UIController_GetDefaultConfig();
  module->InitializeWithConfig(module, config, NULL);

  RESET_FAKE(mocked_xQueueSend);
  mocked_xQueueSend_fake.custom_fake = custom_xQueueSend;

  BusEvent event = { .eventName = EVENT_CONFIG_CHANGED };
  UIController_OnConfigChanged(event);

  TEST_ASSERT_EQUAL(1, mocked_xQueueSend_fake.call_count);
  TEST_ASSERT_EQUAL(UI_EVENT_CONFIG_CHANGED, lastSentUIEvent.type);

  module->base.Stop((IService *)module);
}

void test_UIController_CheckHealth(void) {
  IUIController *module = UIController_GetModule();
  
  // Unstarted state should fail health check
  TEST_ASSERT_NOT_EQUAL(0, module->base.CheckHealth((IService *)module));

  UIControllerConfig config = UIController_GetDefaultConfig();
  module->InitializeWithConfig(module, config, NULL);
  module->base.Start((IService *)module);

  // Started with valid queues/mutex should pass health check
  TEST_ASSERT_EQUAL(0, module->base.CheckHealth((IService *)module));

  module->base.Stop((IService *)module);
}

void test_BootAnimation_Lifecycle(void) {
  BootAnimation_Start();
  TEST_ASSERT_TRUE(BootAnimation_IsRunning());

  // Simulate tick progression
  BootAnimation_Tick(100);
  TEST_ASSERT_TRUE(BootAnimation_IsRunning());

  // Simulate system ready notification
  BootAnimation_NotifyReady();
  BootAnimation_Tick(500);
  BootAnimation_Tick(2300);
  BootAnimation_Tick(3700);
  BootAnimation_Tick(6200);

  // After dissolve duration expires, animation should complete
  BootAnimation_Tick(6500);
  TEST_ASSERT_FALSE(BootAnimation_IsRunning());
}

void test_BootAnimation_Skip(void) {
  BootAnimation_Start();
  TEST_ASSERT_TRUE(BootAnimation_IsRunning());

  // Calling skip should immediately stop running state
  BootAnimation_Skip();
  TEST_ASSERT_FALSE(BootAnimation_IsRunning());
}

static unsigned boot_release_order, boot_builtin_release_order, boot_external_release_order;
static esp_err_t boot_release_result;
static esp_err_t boot_release_builtin(IBuiltinLED *self,bool active) {
  (void)self; TEST_ASSERT_FALSE(active);
  boot_builtin_release_order=++boot_release_order;
  return boot_release_result;
}
static esp_err_t boot_release_external(IRGBLED *self) {
  (void)self; boot_external_release_order=++boot_release_order; return ESP_OK;
}
void test_BootCompletionReleasesBothLEDWorkersWithoutWaitingForUIPoll(void) {
  IBuiltinLED builtin={.SetStartupActive=boot_release_builtin};
  IRGBLED external={.StopAnimation=boot_release_external};
  uiControllerInstance.builtinLed=&builtin;uiControllerInstance.rgbLed=&external;
  mocked_ServiceLocator_GetService_fake.custom_fake=NULL;
  mocked_ServiceLocator_GetService_fake.return_val=NULL;
  boot_release_order=boot_builtin_release_order=boot_external_release_order=0;
  boot_release_result=ESP_OK;
  RainbowFlowAnim_BeginSynchronizedPhase(0);
  BootAnimation_StopLEDs();
  TEST_ASSERT_EQUAL(1,boot_builtin_release_order);
  TEST_ASSERT_EQUAL(2,boot_external_release_order);
  TEST_ASSERT_FALSE(RainbowFlowAnim_IsSynchronizedPhase());
  /* A rejected queue admission must not prevent external cleanup; the UI's
   * existing startup ownership path remains responsible for retrying. */
  boot_release_order=0;boot_release_result=ESP_ERR_TIMEOUT;
  BootAnimation_StopLEDs();
  TEST_ASSERT_EQUAL(2,boot_release_order);
  uiControllerInstance.builtinLed=NULL;uiControllerInstance.rgbLed=NULL;
}

static SystemSettings boot_settings;
static unsigned boot_calls;
static RGBLEDAnimType boot_animation;
static esp_err_t boot_snapshot_result;
static esp_err_t boot_state_result;
static RGBLEDState boot_rgb_state;
static esp_err_t get_boot_settings(IConfigurationManager *self, SystemSettings *out) {
  (void)self;
  if (boot_snapshot_result == ESP_OK) *out = boot_settings;
  return boot_snapshot_result;
}
static esp_err_t start_boot_rgb(IRGBLED *self, RGBLEDAnimType animation, RGBLEDPriority priority, uint32_t duration) {
  (void)self; (void)duration;
  ++boot_calls; boot_animation = animation;
  boot_rgb_state = (RGBLEDState){.colorMode=RGBLED_COLOR_MODE_ANIMATION, .activeAnim=animation,
      .animPriority=priority, .isOn=true};
  return ESP_OK;
}
static esp_err_t get_boot_rgb_state(IRGBLED *self, RGBLEDState *out) {
  (void)self;
  if (boot_state_result == ESP_OK) *out = boot_rgb_state;
  return boot_state_result;
}
void test_BootAnimation_RespectsSavedLEDPreference(void) {
  IConfigurationManager config = {.GetSnapshot = get_boot_settings};
  IRGBLED rgb = {.StartAnimation = start_boot_rgb, .GetLEDState = get_boot_rgb_state};
  mocked_ConfigurationManager_GetInstance_fake.return_val = &config;
  mocked_ServiceLocator_GetService_fake.return_val = NULL;
  uiControllerInstance.rgbLed = &rgb;
  boot_calls = 0;
  boot_snapshot_result = ESP_OK;
  boot_state_result = ESP_OK;
  memset(&boot_rgb_state, 0, sizeof(boot_rgb_state));
  memset(&boot_settings, 0, sizeof(boot_settings));
  boot_settings.hardware.ext_led_state = true;
  boot_settings.hardware.ext_led_startup_anim = RGBLED_ANIM_NONE;
  BootAnimation_Start();
  BootAnimation_TryStartRGB();
  TEST_ASSERT_EQUAL(0, boot_calls);
  boot_settings.hardware.ext_led_startup_anim = RGBLED_ANIM_RAINBOW;
  BootAnimation_Start();
  TEST_ASSERT_EQUAL(1, boot_calls);
  TEST_ASSERT_EQUAL(RGBLED_ANIM_RAINBOW, boot_animation);
  BootAnimation_TryStartRGB();
  TEST_ASSERT_EQUAL(1, boot_calls);
  /* If the RGB worker started the boot effect first, the UI readiness retry
   * must reuse it instead of resetting its animation phase. */
  boot_calls = 0;
  BootAnimation_Start();
  TEST_ASSERT_EQUAL(0, boot_calls);
  TEST_ASSERT_TRUE(s_rgbBootResolved);
  /* A different active effect still yields to the persisted boot selection. */
  boot_rgb_state.activeAnim = RGBLED_ANIM_BREATHING;
  BootAnimation_Start();
  TEST_ASSERT_EQUAL(1, boot_calls);
  TEST_ASSERT_EQUAL(RGBLED_ANIM_RAINBOW, boot_animation);
  boot_settings.hardware.ext_led_mode = EXT_LED_MODE_OFF;
  BootAnimation_Start();
  TEST_ASSERT_EQUAL(1, boot_calls);
  boot_settings.hardware.ext_led_mode = EXT_LED_MODE_ANIMATION;
  boot_snapshot_result = ESP_FAIL;
  BootAnimation_Start();
  TEST_ASSERT_EQUAL(1, boot_calls);
  TEST_ASSERT_FALSE(s_rgbBootResolved);
  boot_snapshot_result = ESP_OK;
  boot_rgb_state.activeAnim = RGBLED_ANIM_BREATHING;
  BootAnimation_TryStartRGB();
  TEST_ASSERT_EQUAL(2, boot_calls);
  uiControllerInstance.rgbLed = NULL;
  mocked_ConfigurationManager_GetInstance_fake.return_val = NULL;
}


static int32_t ui_stop_ack;
static BaseType_t ui_send_stop_ack(QueueHandle_t queue, const void *value, TickType_t wait) {
  (void)queue; (void)wait; ui_stop_ack = *(const int32_t *)value; return pdTRUE;
}
static BaseType_t ui_receive_stop_ack(QueueHandle_t queue, void *value, TickType_t wait) {
  (void)queue;
  if (!wait) return pdFALSE;
  mocked_xTaskGetCurrentTaskHandle_fake.return_val = (TaskHandle_t)0x9999;
  WatchdogWorker_Checkpoint(&uiControllerInstance.worker, NULL, 0);
  mocked_xTaskGetCurrentTaskHandle_fake.return_val = (TaskHandle_t)4;
  *(int32_t *)value = ui_stop_ack; return pdTRUE;
}
void test_UIController_StopTimeoutAndRestart(void) {
  IUIController *ui = UIController_GetModule();
  TEST_ASSERT_EQUAL(0, ui->InitializeWithConfig(ui, UIController_GetDefaultConfig(), NULL));
  TEST_ASSERT_EQUAL(0, ui->base.Start((IService *)ui));
  RESET_FAKE(mocked_vTaskDelete);
  RESET_FAKE(mocked_xQueueReceive);
  mocked_xTaskGetCurrentTaskHandle_fake.return_val = (TaskHandle_t)4;
  TEST_ASSERT_EQUAL(-1, ui->base.Stop((IService *)ui));
  TEST_ASSERT_EQUAL(0, mocked_vTaskDelete_fake.call_count);
  TEST_ASSERT_NOT_EQUAL(0, ui->base.CheckHealth((IService *)ui));
  QueueHandle_t queue = uiControllerInstance.eventQueue;
  SemaphoreHandle_t mutex = uiControllerInstance.mutex;
  mocked_xQueueSend_fake.custom_fake = ui_send_stop_ack;
  mocked_xQueueReceive_fake.custom_fake = ui_receive_stop_ack;
  TEST_ASSERT_EQUAL(0, ui->base.Stop((IService *)ui));
  TEST_ASSERT_EQUAL_PTR(queue, uiControllerInstance.eventQueue);
  TEST_ASSERT_EQUAL_PTR(mutex, uiControllerInstance.mutex);
  UIEvent event = {.type = UI_EVENT_SCREEN_WAKEUP};
  TEST_ASSERT_EQUAL(-1, ui->PostEvent(ui, &event));
  TEST_ASSERT_EQUAL(0, ui->base.Start((IService *)ui));
  TEST_ASSERT_EQUAL(0, ui->base.CheckHealth((IService *)ui));
}
void test_UIController_AllocationAndLaunchFailures(void) {
  IUIController *ui = UIController_GetModule();
  mocked_xQueueCreate_fake.return_val = NULL;
  TEST_ASSERT_EQUAL(-1, ui->InitializeWithConfig(ui, UIController_GetDefaultConfig(), NULL));
  TEST_ASSERT_EQUAL(SERVICE_STATE_UNINITIALIZED, ui->base.GetState((IService *)ui));
  TEST_ASSERT_NULL(uiControllerInstance.mutex);
  TEST_ASSERT_NULL(uiControllerInstance.eventQueue);
  mocked_xQueueCreate_fake.return_val = (QueueHandle_t)1;
  TEST_ASSERT_EQUAL(0, ui->InitializeWithConfig(ui, UIController_GetDefaultConfig(), NULL));
  mocked_xTaskCreate_fake.custom_fake = NULL;
  mocked_xTaskCreate_fake.return_val = pdFAIL;
  TEST_ASSERT_EQUAL(-1, ui->base.Start((IService *)ui));
  TEST_ASSERT_EQUAL(SERVICE_STATE_INITIALIZED, ui->base.GetState((IService *)ui));
  TEST_ASSERT_NULL(atomic_load(&uiControllerInstance.worker.task));
}

void test_UIController_MenuDependencyErrorsPreventStart(void) {
  IUIController *ui = UIController_GetModule();
  TEST_ASSERT_EQUAL(0, ui->InitializeWithConfig(ui, UIController_GetDefaultConfig(), NULL));
  menu_service_state = SERVICE_STATE_UNINITIALIZED;
  menu_init_result = -1;
  TEST_ASSERT_NOT_EQUAL(0, ui->base.Start(&ui->base));
  TEST_ASSERT_EQUAL(SERVICE_STATE_INITIALIZED, ui->base.GetState(&ui->base));
  TEST_ASSERT_EQUAL(0, menu_start_calls);
  TEST_ASSERT_NULL(atomic_load(&uiControllerInstance.worker.task));
  menu_init_result = 0;
  menu_start_result = -1;
  TEST_ASSERT_NOT_EQUAL(0, ui->base.Start(&ui->base));
  TEST_ASSERT_EQUAL(SERVICE_STATE_INITIALIZED, ui->base.GetState(&ui->base));
  TEST_ASSERT_NULL(atomic_load(&uiControllerInstance.worker.task));
  menu_start_result = 0;
  TEST_ASSERT_EQUAL(0, ui->base.Start(&ui->base));
}

void test_UIController_PreservesAlreadyStartedMenu(void) {
  IUIController *ui = UIController_GetModule();
  TEST_ASSERT_EQUAL(0, ui->InitializeWithConfig(ui, UIController_GetDefaultConfig(), NULL));
  TEST_ASSERT_EQUAL(0, ui->base.Start(&ui->base));
  TEST_ASSERT_EQUAL(0, menu_init_calls);
  TEST_ASSERT_EQUAL(0, menu_start_calls);
}

void test_UIController_ConfigCannotChangeAfterInitialization(void) {
  IUIController *ui = UIController_GetModule();
  UIControllerConfig original = UIController_GetDefaultConfig();
  TEST_ASSERT_EQUAL(0, ui->InitializeWithConfig(ui, original, NULL));
  UIControllerConfig changed = original;
  changed.eventQueueSize++;
  changed.refreshIntervalMs++;
  const ServiceState states[] = {SERVICE_STATE_INITIALIZED, SERVICE_STATE_STARTED,
                                SERVICE_STATE_STOPPED, SERVICE_STATE_ERROR};
  QueueHandle_t queue = uiControllerInstance.eventQueue;
  for (unsigned i = 0; i < sizeof(states) / sizeof(states[0]); ++i) {
    uiControllerInstance.state = states[i];
    TEST_ASSERT_NOT_EQUAL(0, ui->InitializeWithConfig(ui, changed, NULL));
    TEST_ASSERT_EQUAL(original.eventQueueSize, uiControllerInstance.config.eventQueueSize);
    TEST_ASSERT_EQUAL(original.refreshIntervalMs, uiControllerInstance.config.refreshIntervalMs);
    TEST_ASSERT_EQUAL_PTR(queue, uiControllerInstance.eventQueue);
    TEST_ASSERT_EQUAL(states[i], uiControllerInstance.state);
  }
}

void test_UIController_ConfigFailurePreservesOriginalAndRejectsForeignSelf(void) {
  IUIController *ui = UIController_GetModule();
  UIControllerConfig original = uiControllerInstance.config;
  UIControllerConfig desired = UIController_GetDefaultConfig();
  desired.eventQueueSize++;
  TEST_ASSERT_NOT_EQUAL(0, ui->InitializeWithConfig(NULL, desired, NULL));
  TEST_ASSERT_EQUAL(original.eventQueueSize, uiControllerInstance.config.eventQueueSize);
  mocked_xQueueCreate_fake.return_val = NULL;
  TEST_ASSERT_NOT_EQUAL(0, ui->InitializeWithConfig(ui, desired, NULL));
  TEST_ASSERT_EQUAL(original.eventQueueSize, uiControllerInstance.config.eventQueueSize);
  TEST_ASSERT_EQUAL(SERVICE_STATE_UNINITIALIZED, uiControllerInstance.state);
  mocked_xQueueCreate_fake.return_val = (QueueHandle_t)1;
  TEST_ASSERT_EQUAL(0, ui->InitializeWithConfig(ui, desired, NULL));
  TEST_ASSERT_EQUAL(desired.eventQueueSize, uiControllerInstance.config.eventQueueSize);
}

void test_UIController_ScreenAllocationFailuresRollbackAndRetry(void) {
  IUIController *ui = UIController_GetModule();
  TEST_ASSERT_EQUAL(0, ui->InitializeWithConfig(ui, UIController_GetDefaultConfig(), NULL));
  TEST_ASSERT_EQUAL(0, ui->base.Start(&ui->base));
  unsigned allocation_count = ui_alloc_calls;
  TEST_ASSERT_GREATER_THAN(40, allocation_count);
  for (unsigned fail = 1; fail <= allocation_count; ++fail) {
    UIFixture_SetUp();
    TEST_ASSERT_EQUAL(0, ui->InitializeWithConfig(ui, UIController_GetDefaultConfig(), NULL));
    ui_fail_at = fail;
    TEST_ASSERT_NOT_EQUAL(0, ui->base.Start(&ui->base));
    TEST_ASSERT_EQUAL(0, ui_live_nodes);
    TEST_ASSERT_EQUAL_PTR((void *)0x100, ui_active_screen);
    TEST_ASSERT_EQUAL_PTR((void *)0x200, ui_default_group);
    TEST_ASSERT_EQUAL(0, ui_load_calls);
    TEST_ASSERT_EQUAL(0, ui_lock_depth);
    TEST_ASSERT_FALSE(BootAnimation_IsRunning());
    TEST_ASSERT_NULL(atomic_load(&uiControllerInstance.worker.task));
    TEST_ASSERT_EQUAL(0, uiControllerInstance.subscriptions);
    TEST_ASSERT_EQUAL(SERVICE_STATE_INITIALIZED, uiControllerInstance.state);
    ui_fail_at = 0;
    TEST_ASSERT_EQUAL(0, ui->base.Start(&ui->base));
    TEST_ASSERT_EQUAL(1, ui_load_calls);
    TEST_ASSERT_TRUE(BootAnimation_IsRunning());
  }
}
void test_UIController_RequiresReadyDisplayBeforeCreatingScreens(void) {
  IUIController *ui = UIController_GetModule();
  TEST_ASSERT_EQUAL(0, ui->InitializeWithConfig(ui, UIController_GetDefaultConfig(), NULL));
  ui_oled_state = SERVICE_STATE_ERROR;
  TEST_ASSERT_NOT_EQUAL(0, ui->base.Start(&ui->base));
  TEST_ASSERT_EQUAL(0, ui_alloc_calls);
  TEST_ASSERT_NULL(atomic_load(&uiControllerInstance.worker.task));
}

#include "Core/TaskPriorities.h"
void test_UIController_RejectsPriorityThatCanStarveThermalSampling(void) {
  IUIController *ui = UIController_GetModule();
  UIControllerConfig config = UIController_GetDefaultConfig();
  const UBaseType_t rejected[] = {0, RACOOL_TASK_PRIORITY_THERMAL_SENSOR, (UBaseType_t)-1};
  for (unsigned i = 0; i < sizeof(rejected) / sizeof(rejected[0]); ++i) {
    config.taskPriority = rejected[i];
    TEST_ASSERT_NOT_EQUAL(0, ui->InitializeWithConfig(ui, config, NULL));
    TEST_ASSERT_EQUAL(SERVICE_STATE_UNINITIALIZED, uiControllerInstance.state);
    TEST_ASSERT_EQUAL(0, mocked_xQueueCreate_fake.call_count);
  }
  config.taskPriority = RACOOL_TASK_PRIORITY_UI;
  TEST_ASSERT_EQUAL(0, ui->InitializeWithConfig(ui, config, NULL));
}

void test_BootQueueSurvivesClockWrapAndKeepsAllGroupsBeforeExit(void);
void test_BootProgressQueuesCompletedGroupsAndHonorsMinimumDwell(void);
void test_BootProgressNeverInfersReadyFromControllerAndShowsFailure(void);
void test_UIAlwaysOnIdleReturnsHomeWithoutPowerChangeOrRepeatedQueueing(void);
void test_UIAlwaysOnIdleQueueRejectionRetriesAndRecentInputStaysInMenu(void);
void test_UIWorkerAlwaysOnStillAppliesConfiguredMenuTimeout(void);
void test_UIDisabledMenuAutoExitKeepsMenuButPreservesScreenSleep(void);
void test_UIController_UnsubscribeBusyMustNotDeclareStopped(void);
void test_UIConsole_RejectsMalformedTokensAndEventPayloadsBeforeQueue(void);
void test_UIConsole_MissingInterfacesCannotClaimSuccess(void);
void test_UIConsole_ValidDiagnosticValuesReachOwnedPayload(void);
void test_UIInactivityWrapAndZeroStartApplyPowerAtDeadline(void);
void test_UIInactivityFreshClockPreservesRecentInput(void);
void test_UIInactivityPowerFailurePreservesAppliedState(void);
void test_UIInactivityRecentActivityNeverWakesSuspendedScreen(void);
void test_UIInactivityFullUptimeCannotWakeFromARepeatedLowTimestamp(void);
void test_UIWorker_LateZigbeeRegistrationUpdatesMonitoringIcon(void);
void test_UIRotaryLongPressValidatesAndCopiesDuration(void);
void test_UIWakeWaitsForMenuResetAndConsumesFirstClick(void);
void test_UIWakeRejectedAdmissionAndPanelFailureStayOff(void);
void test_UILongPressOnlyOpensPairingOnAnAwakeDisplay(void);
void test_UIMainMenuNamesRootAndHighlightsSelectedCategory(void);
void test_UIDarkHoldResetIsOnceAndBoundToPressStart(void);
void test_UIDarkResetBlocksWakeUntilTerminalFailure(void);
void test_UISignalStrengthUsesMeasuredCoordinatorAndPreservesDot(void);
void test_UISignalStrengthRefreshesAfterOneSecond(void);
void test_UISignalStrengthHysteresisPreventsThresholdFlicker(void);
void test_UIFanModeIconsRefreshAcrossEveryModeWithoutTemperatureChange(void);
void test_UIAllStatusInkIsCenteredInsideEqualCells(void);
void test_UIMonitorLanguageRefreshUsesOneCatalogWithoutWaking(void);
void test_UITranslationsCoverEveryStateAndAnimationInBothLocales(void);
void test_UIConfigAndIndependentTemperaturesRefreshWithoutWake(void);
void test_UIMissingMeasurementIsNotAFabricatedTemperature(void);
void test_UIPairRendererCannotCancelRadio(void);
void test_UIConsoleMachineStatusReportsAppliedStateInBothLocales(void);
void test_UISettingsSnapshotRefreshDoesNotRequireConfigNotification(void);
void test_TelemetryListDistinguishesDisabledFromAnActiveInterval(void);

void test_UIWorker_IndependentBuiltinStartupAndRuntimeHandoff(void);
static DiagnosticsTestResult visual_health;
static DiagnosticsTestResult visual_health_snapshot(IDiagnosticsManager *self) { (void)self;return visual_health; }
void test_UIStatusWidgetDoesNotInventHealthOrUnitDecorations(void) {
    SystemSettings settings={0};char value[64],unit[16];int percent=-1;
    IDiagnosticsManager diagnostics={.GetLastTestResult=visual_health_snapshot};
    mocked_ServiceLocator_GetService_fake.custom_fake=NULL;
    mocked_ServiceLocator_GetService_fake.return_val=&diagnostics.base;
    const LocalizationCatalog *catalog=Localization_Find("en");
    visual_health=(DiagnosticsTestResult){0};
    TEST_ASSERT_TRUE(WidgetSystem_GetValue(UI_STATE_SYSTEM_STATUS,&settings,value,sizeof(value),unit,sizeof(unit),&percent,catalog));
    TEST_ASSERT_EQUAL_STRING("Unknown",value);TEST_ASSERT_EQUAL_STRING("",unit);
    visual_health=(DiagnosticsTestResult){.completed=true,.services_checked=9,.health_score=100};
    TEST_ASSERT_TRUE(WidgetSystem_GetValue(UI_STATE_SYSTEM_STATUS,&settings,value,sizeof(value),unit,sizeof(unit),&percent,catalog));
    TEST_ASSERT_EQUAL_STRING("100%",value);TEST_ASSERT_EQUAL_STRING("",unit);
    visual_health.health_score=101;
    TEST_ASSERT_TRUE(WidgetSystem_GetValue(UI_STATE_SYSTEM_STATUS,&settings,value,sizeof(value),unit,sizeof(unit),&percent,catalog));
    TEST_ASSERT_EQUAL_STRING("Unknown",value);
    mocked_ServiceLocator_GetService_fake.return_val=NULL;
}
int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_UIStatusWidgetDoesNotInventHealthOrUnitDecorations);
  RUN_TEST(test_UIWorker_IndependentBuiltinStartupAndRuntimeHandoff);
  RUN_TEST(test_UIFanModeIconsRefreshAcrossEveryModeWithoutTemperatureChange);
  RUN_TEST(test_UIAllStatusInkIsCenteredInsideEqualCells);
  RUN_TEST(test_UIMonitorLanguageRefreshUsesOneCatalogWithoutWaking);
  RUN_TEST(test_UITranslationsCoverEveryStateAndAnimationInBothLocales);
  RUN_TEST(test_UIConfigAndIndependentTemperaturesRefreshWithoutWake);
  RUN_TEST(test_UIMissingMeasurementIsNotAFabricatedTemperature);
  RUN_TEST(test_UIPairRendererCannotCancelRadio);
  RUN_TEST(test_UIConsoleMachineStatusReportsAppliedStateInBothLocales);
  RUN_TEST(test_UISettingsSnapshotRefreshDoesNotRequireConfigNotification);

  RUN_TEST(test_UIDarkHoldResetIsOnceAndBoundToPressStart);
  RUN_TEST(test_UIDarkResetBlocksWakeUntilTerminalFailure);
  RUN_TEST(test_UISignalStrengthUsesMeasuredCoordinatorAndPreservesDot);
  RUN_TEST(test_UISignalStrengthRefreshesAfterOneSecond);
  RUN_TEST(test_UISignalStrengthHysteresisPreventsThresholdFlicker);
  RUN_TEST(test_UIRotaryLongPressValidatesAndCopiesDuration);
  RUN_TEST(test_UIWakeWaitsForMenuResetAndConsumesFirstClick);
  RUN_TEST(test_UIWakeRejectedAdmissionAndPanelFailureStayOff);
  RUN_TEST(test_UILongPressOnlyOpensPairingOnAnAwakeDisplay);
  RUN_TEST(test_UIMainMenuNamesRootAndHighlightsSelectedCategory);
  RUN_TEST(test_UIWorker_LateZigbeeRegistrationUpdatesMonitoringIcon);
  RUN_TEST(test_UIInactivityFullUptimeCannotWakeFromARepeatedLowTimestamp);
  RUN_TEST(test_UIInactivityRecentActivityNeverWakesSuspendedScreen);
  RUN_TEST(test_UIInactivityWrapAndZeroStartApplyPowerAtDeadline);
  RUN_TEST(test_UIInactivityFreshClockPreservesRecentInput);
  RUN_TEST(test_UIInactivityPowerFailurePreservesAppliedState);

  RUN_TEST(test_UIConsole_RejectsMalformedTokensAndEventPayloadsBeforeQueue);
  RUN_TEST(test_UIConsole_MissingInterfacesCannotClaimSuccess);
  RUN_TEST(test_UIConsole_ValidDiagnosticValuesReachOwnedPayload);

  RUN_TEST(test_BootReadinessNotificationDoesNotInventServiceHealth);
  RUN_TEST(test_UIController_RejectsPriorityThatCanStarveThermalSampling);
  RUN_TEST(test_UIController_ScreenAllocationFailuresRollbackAndRetry);
  RUN_TEST(test_UIController_RequiresReadyDisplayBeforeCreatingScreens);
  RUN_TEST(test_UIController_ConfigCannotChangeAfterInitialization);
  RUN_TEST(test_UIController_ConfigFailurePreservesOriginalAndRejectsForeignSelf);
  RUN_TEST(test_UIController_MenuDependencyErrorsPreventStart);
  RUN_TEST(test_UIController_PreservesAlreadyStartedMenu);
  RUN_TEST(test_UIController_UnsubscribeBusyMustNotDeclareStopped);
  RUN_TEST(test_UIConsole_ConfigUsesSnapshotAndRejectsUnavailableSettings);
  RUN_TEST(test_UIConsoleMachineConfigIsStableAndDoesNotWakeScreen);
  RUN_TEST(test_UIController_StopTimeoutAndRestart);
  RUN_TEST(test_UIController_AllocationAndLaunchFailures);
  RUN_TEST(test_UIController_MenuRowsRetryAllocationAndExceedSixteen);
    RUN_TEST(test_UIController_RendererRetriesUnchangedMenuAfterAllocationFailure);
    RUN_TEST(test_UIController_RejectsInvalidStartupConfigurationBeforeAllocation);
  RUN_TEST(test_UIController_RejectsDrawingWithoutOLEDLock);
  RUN_TEST(test_UIController_ScreenStateChangesOnlyAfterAppliedPower);
  RUN_TEST(test_UIController_GetModule);
  RUN_TEST(test_UIController_Initialize);
  RUN_TEST(test_UIController_PostEvent);
  RUN_TEST(test_UIController_IsScreenOn);
  RUN_TEST(test_UIController_OnRotaryClicked);
  RUN_TEST(test_UIController_OnRotaryRotated);
  RUN_TEST(test_UIController_OnIRTempUpdated);
  RUN_TEST(test_UIController_OnInternalTempUpdated);
  RUN_TEST(test_UIController_OnRelayStateChanged);
  RUN_TEST(test_UIController_OnConfigChanged);
  RUN_TEST(test_UIController_CheckHealth);
  RUN_TEST(test_BootProgressQueuesCompletedGroupsAndHonorsMinimumDwell);
  RUN_TEST(test_BootQueueSurvivesClockWrapAndKeepsAllGroupsBeforeExit);
  RUN_TEST(test_BootProgressNeverInfersReadyFromControllerAndShowsFailure);
  RUN_TEST(test_UIAlwaysOnIdleReturnsHomeWithoutPowerChangeOrRepeatedQueueing);
  RUN_TEST(test_UIAlwaysOnIdleQueueRejectionRetriesAndRecentInputStaysInMenu);
  RUN_TEST(test_UIWorkerAlwaysOnStillAppliesConfiguredMenuTimeout);
  RUN_TEST(test_UIDisabledMenuAutoExitKeepsMenuButPreservesScreenSleep);
  RUN_TEST(test_BootAnimation_Lifecycle);
  RUN_TEST(test_BootWaitsForLateReadinessWithinBoundedStartupBudget);
  RUN_TEST(test_BootAnimation_Skip);
  RUN_TEST(test_BootCompletionReleasesBothLEDWorkersWithoutWaitingForUIPoll);
  RUN_TEST(test_BootAnimation_RespectsSavedLEDPreference);
  RUN_TEST(test_UIRegistration_RejectsMissingBoundary);
  RUN_TEST(test_UIRegistration_PreservesEveryFailureAndAllowsRetry);
  RUN_TEST(test_TelemetryListDistinguishesDisabledFromAnActiveInterval);
  return UNITY_END();
}

void test_UIController_UnsubscribeBusyMustNotDeclareStopped(void) {
  IUIController *ui = UIController_GetModule();
  TEST_ASSERT_EQUAL(0, ui->InitializeWithConfig(ui, UIController_GetDefaultConfig(), NULL));
  TEST_ASSERT_EQUAL(0, ui->base.Start(&ui->base));
  mocked_xQueueSend_fake.custom_fake = ui_send_stop_ack;
  mocked_xQueueReceive_fake.custom_fake = ui_receive_stop_ack;
  ui_unsubscribe_result = EVENT_BUS_ERR_BUSY;
  TEST_ASSERT_NOT_EQUAL(0, ui->base.Stop(&ui->base));
  TEST_ASSERT_NOT_EQUAL(SERVICE_STATE_STOPPED, ui->base.GetState(&ui->base));
  TEST_ASSERT_NOT_EQUAL(0, ui->base.Start(&ui->base));
  ui_unsubscribe_result = 0;
  TEST_ASSERT_EQUAL(0, ui->base.Stop(&ui->base));
  TEST_ASSERT_EQUAL(0, ui->base.Start(&ui->base));
}


void test_UIConsole_RejectsMalformedTokensAndEventPayloadsBeforeQueue(void) {
  RESET_FAKE(mocked_xQueueSend);
  TEST_ASSERT_EQUAL(1, cmd_ui(0, NULL));
  char *missing[] = {"UI", NULL}; TEST_ASSERT_EQUAL(1, cmd_ui(2, missing));
  char *extra[] = {"UI", "STATUS", "extra"}; TEST_ASSERT_EQUAL(1, cmd_ui(3, extra));
  char *values[] = {"nan", "inf", "25junk", "1e10000", ""};
  for (unsigned i = 0; i < sizeof(values)/sizeof(values[0]); ++i) {
    char *args[] = {"EVENT", "TEMP", values[i]}; TEST_ASSERT_EQUAL(1, do_ui_event(3, args));
  }
  char *enc[] = {"EVENT", "ENCODER", "128"}; TEST_ASSERT_EQUAL(1, do_ui_event(3, enc));
  enc[2] = "-129"; TEST_ASSERT_EQUAL(1, do_ui_event(3, enc));
  enc[2] = "1junk"; TEST_ASSERT_EQUAL(1, do_ui_event(3, enc));
  char *relay[] = {"EVENT", "RELAY", "unknown"}; TEST_ASSERT_EQUAL(1, do_ui_event(3, relay));
  char *zb[] = {"EVENT", "ZIGBEE", "2"}; TEST_ASSERT_EQUAL(1, do_ui_event(3, zb));
  char *click[] = {"EVENT", "CLICK", "extra"}; TEST_ASSERT_EQUAL(1, do_ui_event(3, click));
  TEST_ASSERT_EQUAL(0, mocked_xQueueSend_fake.call_count);
}
void test_UIConsole_MissingInterfacesCannotClaimSuccess(void) {
  IUIController *ui = UIController_GetModule(); IUIController saved = *ui;
  ui->PostEvent = NULL; int wake = do_ui_wake(0, NULL);
  char *event[] = {"EVENT", "WAKE"}; int post = do_ui_event(2, event);
  ui->IsScreenOn = NULL; int status = do_ui_status(0, NULL);
  ui->base.CheckHealth = NULL; int health = do_ui_test(0, NULL);
  *ui = saved;
  TEST_ASSERT_EQUAL(1, wake); TEST_ASSERT_EQUAL(1, post); TEST_ASSERT_EQUAL(1, status); TEST_ASSERT_EQUAL(1, health);
}
static UIEvent ui_console_event;
static unsigned ui_console_posts;
static int32_t capture_ui_console_post(IUIController *self, const UIEvent *event) {
  (void)self; ui_console_event = *event; ++ui_console_posts; return 0;
}
void test_UIConsole_ValidDiagnosticValuesReachOwnedPayload(void) {
  IUIController *ui = UIController_GetModule(); IUIController saved = *ui;
  ui->PostEvent = capture_ui_console_post; ui_console_posts = 0;
  char *temps[] = {"EVENT", "TEMP", "25.5", "22.3", "35.1"}; int temp = do_ui_event(5, temps);
  UIEvent captured = ui_console_event;
  char *enc[] = {"EVENT", "ENCODER", "-128"}; int encoder = do_ui_event(3, enc);
  int32_t diff = ui_console_event.data.encoder.difference;
  char *relay[] = {"EVENT", "RELAY", "OFF"}; int boolean = do_ui_event(3, relay);
  bool relay_value = ui_console_event.data.relay.isOpen;
  *ui = saved;
  TEST_ASSERT_EQUAL(0, temp); TEST_ASSERT_FLOAT_WITHIN(0.01f, 25.5f, captured.data.temp.objectTemp);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 22.3f, captured.data.temp.ambientTemp);
  TEST_ASSERT_EQUAL(0, encoder); TEST_ASSERT_EQUAL(-128, diff);
  TEST_ASSERT_EQUAL(0, boolean); TEST_ASSERT_FALSE(relay_value); TEST_ASSERT_EQUAL(3, ui_console_posts);
}

static unsigned inactivity_power_calls;
static esp_err_t inactivity_power_result;
static bool inactivity_power_on;
static esp_err_t inactivity_power(IOLED *self, bool on) {
  (void)self; inactivity_power_on = on; ++inactivity_power_calls; return inactivity_power_result;
}
static IOLED inactivity_oled = {.SetPower = inactivity_power};
static void prepare_inactivity(uint32_t activity, uint32_t now) {
  uiControllerInstance.menu = &mockMenuMgr.base;
  mockMenuMgr.lastActivityTime = activity;
  uiControllerInstance.oled = &inactivity_oled;
  uiControllerInstance.isScreenOn = true;
  inactivity_power_calls = 0; inactivity_power_result = ESP_OK;
  RESET_FAKE(mocked_esp_timer_get_time);
  mocked_esp_timer_get_time_fake.return_val = (int64_t)now * 1000;
}
void test_UIInactivityWrapAndZeroStartApplyPowerAtDeadline(void) {
  prepare_inactivity(UINT32_MAX - 1000u, 30000u);
  UI_ApplyInactivityPower(20000u, true, true);
  TEST_ASSERT_EQUAL(1, inactivity_power_calls); TEST_ASSERT_FALSE(uiControllerInstance.isScreenOn);
  prepare_inactivity(0u, 20000u); UI_ApplyInactivityPower(20000u, true, true);
  TEST_ASSERT_EQUAL(0, inactivity_power_calls);
  mocked_esp_timer_get_time_fake.return_val = 20001000;
  UI_ApplyInactivityPower(20000u, true, true); TEST_ASSERT_EQUAL(1, inactivity_power_calls);
}
void test_UIInactivityFreshClockPreservesRecentInput(void) {
  prepare_inactivity(1002u, 1010u);
  UI_ApplyInactivityPower(20000u, true, true);
  TEST_ASSERT_EQUAL(1, mocked_esp_timer_get_time_fake.call_count);
  TEST_ASSERT_EQUAL(0, inactivity_power_calls); TEST_ASSERT_TRUE(uiControllerInstance.isScreenOn);
}
void test_UIInactivityPowerFailurePreservesAppliedState(void) {
  prepare_inactivity(1u, 30000u); inactivity_power_result = ESP_FAIL;
  UI_ApplyInactivityPower(20000u, true, true);
  TEST_ASSERT_EQUAL(1, inactivity_power_calls); TEST_ASSERT_TRUE(uiControllerInstance.isScreenOn);
  uiControllerInstance.menu = NULL;
  UI_ApplyInactivityPower(20000u, true, true); TEST_ASSERT_EQUAL(1, inactivity_power_calls);
}

void test_UIInactivityRecentActivityNeverWakesSuspendedScreen(void) {
  prepare_inactivity(1002u, 1010u); uiControllerInstance.isScreenOn = false;
  inactivity_power_result = ESP_FAIL; UI_ApplyInactivityPower(20000u, false, true);
  TEST_ASSERT_EQUAL(0, inactivity_power_calls);
  TEST_ASSERT_FALSE(uiControllerInstance.isScreenOn);
  inactivity_power_result = ESP_OK; UI_ApplyInactivityPower(20000u, false, true);
  TEST_ASSERT_FALSE(uiControllerInstance.isScreenOn);
  TEST_ASSERT_FALSE(uiControllerInstance.wakeNavigationPending);
  mocked_esp_timer_get_time_fake.return_val = 100000000;
  UI_ApplyInactivityPower(20000u, false, true);
  TEST_ASSERT_FALSE(uiControllerInstance.isScreenOn); TEST_ASSERT_EQUAL(0, inactivity_power_calls);
}

static uint64_t full_activity_ms;
static uint64_t full_activity(const IMenuManager *self) { (void)self; return full_activity_ms; }
void test_UIInactivityFullUptimeCannotWakeFromARepeatedLowTimestamp(void) {
  prepare_inactivity(0, 10); uiControllerInstance.isScreenOn = false;
  mockMenuMgr.base.GetLastActivityTimeMs = full_activity; full_activity_ms = 0;
  uint64_t now = (uint64_t)UINT32_MAX + 11u;
  mocked_esp_timer_get_time_fake.return_val = (int64_t)(now * 1000u);
  UI_ApplyInactivityPower(20000u, true, true);
  bool stayed_off = !uiControllerInstance.isScreenOn; unsigned old_calls = inactivity_power_calls;
  full_activity_ms = now - 1u;
  UI_ApplyInactivityPower(20000u, true, true);
  bool woke = uiControllerInstance.isScreenOn;
  mockMenuMgr.base.GetLastActivityTimeMs = NULL;
  TEST_ASSERT_TRUE(stayed_off); TEST_ASSERT_EQUAL(0, old_calls);
  TEST_ASSERT_FALSE(woke); TEST_ASSERT_FALSE(uiControllerInstance.wakeNavigationPending); TEST_ASSERT_EQUAL(0, inactivity_power_calls);
}

#include <setjmp.h>
static jmp_buf icon_worker_exit;
static unsigned icon_receive_calls;
static bool icon_registered;
static int32_t icon_connected(IZigbee *self) { (void)self; return true; }
static IZigbee icon_zigbee = {.IsConnected = icon_connected};
static IService *icon_services(const char *name) {
  if (!strcmp(name, SVC_NAME_ZIGBEE)) return icon_registered ? &icon_zigbee.base : NULL;
  return ui_test_services(name);
}
static BaseType_t icon_receive(QueueHandle_t queue, void *item, TickType_t ticks) {
  (void)queue; (void)item; (void)ticks;
  if (++icon_receive_calls == 3) longjmp(icon_worker_exit, 1);
  if (icon_receive_calls == 2) {
    TEST_ASSERT_NULL(uiControllerInstance.zigbee);
    icon_registered = true;
  }
  return pdFALSE;
}
void test_UIWorker_LateZigbeeRegistrationUpdatesMonitoringIcon(void) {
  IUIController *ui = UIController_GetModule();
  TEST_ASSERT_EQUAL(0, ui->InitializeWithConfig(ui, UIController_GetDefaultConfig(), NULL));
  TEST_ASSERT_NULL(uiControllerInstance.zigbee);
  uiControllerInstance.oled = &ui_test_oled;
  TEST_ASSERT_TRUE(UIRenderer_CreateUI());
  BootAnimation_Skip();
  console_snapshot_result = ESP_OK;
  memset(&console_settings, 0, sizeof(console_settings));
  console_settings.system.display_mode = 2;
  uiControllerInstance.configMgr = &console_config;
  uiControllerInstance.builtinLed = NULL;
  mockMenuMgr.currentState = UI_STATE_MONITOR;
  icon_receive_calls = 0; icon_registered = false;
  mocked_ServiceLocator_GetService_fake.custom_fake = icon_services;
  mocked_xQueueReceive_fake.custom_fake = icon_receive;
  if (!setjmp(icon_worker_exit)) UIController_Task(NULL);
  mocked_xQueueReceive_fake.custom_fake = NULL;
  mocked_ServiceLocator_GetService_fake.custom_fake = ui_test_services;
  TEST_ASSERT_EQUAL_PTR(&icon_zigbee, uiControllerInstance.zigbee);
  TEST_ASSERT_EQUAL_HEX32(0xFFFFFF, ui_text_color(uiControllerInstance.zigbeeIcon));
  TEST_ASSERT_EQUAL(0, ui_lock_depth);
}

static MenuEvent wake_menu_event;
static unsigned wake_menu_posts;
static MenuManagerError wake_admission;
static MenuManagerError capture_wake_menu(IMenuManager *self, const MenuEvent *event) {
  (void)self; wake_menu_event = *event; ++wake_menu_posts; return wake_admission;
}
static void wake_activity(IMenuManager *self) { (void)self; }
static void prepare_wake(void) {
  prepare_inactivity(0, 1000);
  mockMenuMgr.base.PostEvent = capture_wake_menu;
  mockMenuMgr.base.UpdateActivity = wake_activity;
  wake_menu_posts = 0; wake_admission = MENU_OK;
  BootAnimation_Skip();
  uiControllerInstance.isScreenOn = false;
  mockMenuMgr.currentState = UI_STATE_SET_HIGH_TEMP;
  mockMenuMgr.isEditing = true;
}
void test_UIWakeWaitsForMenuResetAndConsumesFirstClick(void) {
  prepare_wake();
  UIEvent click = {.type = UI_EVENT_ENCODER_CLICK}; UIController_HandleEvent(&click);
  TEST_ASSERT_EQUAL(MENU_EVENT_WAKE_UP, wake_menu_event.type);
  TEST_ASSERT_EQUAL(1, wake_menu_posts); TEST_ASSERT_FALSE(uiControllerInstance.isScreenOn);
  UIController_HandleEvent(&click);
  TEST_ASSERT_EQUAL(1, wake_menu_posts); TEST_ASSERT_EQUAL(0, inactivity_power_calls);
  mockMenuMgr.currentState = UI_STATE_MONITOR;
  TEST_ASSERT_FALSE(UI_WakeToMonitor());
  mockMenuMgr.isEditing = false;
  TEST_ASSERT_FALSE(UI_WakeToMonitor());
  TEST_ASSERT_EQUAL(0, inactivity_power_calls); TEST_ASSERT_TRUE(uiControllerInstance.wakeNavigationPending);
  TEST_ASSERT_TRUE(ScreenMonitor_Create()); uiControllerInstance.oled = &ui_test_oled;
  SystemSettings settings = {0}; UIRenderer_Update(1000, &settings, 25,24,30);
  TEST_ASSERT_TRUE(uiControllerInstance.isScreenOn); TEST_ASSERT_EQUAL(1,ui_present_calls);
}
void test_UIWakeRejectedAdmissionAndPanelFailureStayOff(void) {
  prepare_wake(); wake_admission = MENU_ERR_QUEUE_FULL;
  TEST_ASSERT_FALSE(UI_WakeToMonitor()); TEST_ASSERT_FALSE(uiControllerInstance.wakeNavigationPending);
  TEST_ASSERT_EQUAL(0, inactivity_power_calls);
  wake_admission = MENU_OK; TEST_ASSERT_FALSE(UI_WakeToMonitor());
  mockMenuMgr.currentState = UI_STATE_MONITOR; mockMenuMgr.isEditing = false;
  inactivity_power_result = ESP_FAIL; TEST_ASSERT_FALSE(UI_WakeToMonitor());
  TEST_ASSERT_TRUE(uiControllerInstance.wakeNavigationPending); TEST_ASSERT_FALSE(uiControllerInstance.isScreenOn);
  TEST_ASSERT_TRUE(ScreenMonitor_Create()); uiControllerInstance.oled=&ui_test_oled;
  SystemSettings settings={0}; ui_present_result=ESP_FAIL;
  UIRenderer_Update(1000,&settings,25,24,30); TEST_ASSERT_FALSE(uiControllerInstance.isScreenOn);
  ui_present_result=ESP_OK; UIRenderer_Update(1020,&settings,25,24,30);
  TEST_ASSERT_TRUE(uiControllerInstance.isScreenOn);
  TEST_ASSERT_EQUAL(2, wake_menu_posts);
}
void test_UILongPressOnlyOpensPairingOnAnAwakeDisplay(void) {
  prepare_wake();
  UIEvent event = {.type = UI_EVENT_ENCODER_LONG_PRESS, .data.encoder.difference = RE_LONG_PRESS_MS};
  UIController_HandleEvent(&event); TEST_ASSERT_EQUAL(0, wake_menu_posts);
  uiControllerInstance.isScreenOn = true; event.data.encoder.difference--;
  UIController_HandleEvent(&event); TEST_ASSERT_EQUAL(0, wake_menu_posts);
  event.data.encoder.difference++; UIController_HandleEvent(&event);
  TEST_ASSERT_EQUAL(1, wake_menu_posts); TEST_ASSERT_EQUAL(MENU_EVENT_BUTTON_LONG_PRESSED, wake_menu_event.type);
  TEST_ASSERT_FALSE(uiControllerInstance.zbActionSelected);
}
void test_UIMainMenuNamesRootAndHighlightsSelectedCategory(void) {
  TEST_ASSERT_TRUE(ScreenMenu_Create()); uiControllerInstance.menu = &mockMenuMgr.base;
  mockMenuMgr.activeCategory = UI_STATE_MONITOR; mockMenuMgr.currentState = UI_STATE_CAT_TEMP;
  SystemSettings settings = {0}; TEST_ASSERT_TRUE(ScreenMenu_Update(&settings,25,true,Localization_Snapshot()));
  for(unsigned i=0;i<128;++i) if((void *)&ui_nodes[i]==(void *)uiControllerInstance.menuHeader) TEST_ASSERT_TRUE(ui_nodes[i].flags & LV_OBJ_FLAG_HIDDEN);
  mockMenuMgr.activeCategory=UI_STATE_CAT_TEMP;ScreenMenu_Update(&settings,25,true,Localization_Snapshot());
  for(unsigned i=0;i<128;++i) if((void *)&ui_nodes[i]==(void *)uiControllerInstance.menuHeader) TEST_ASSERT_FALSE(ui_nodes[i].flags & LV_OBJ_FLAG_HIDDEN);
  mockMenuMgr.activeCategory=UI_STATE_MONITOR;ScreenMenu_Update(&settings,25,true,Localization_Snapshot());
  TEST_ASSERT_EQUAL(lv_color_hex(0), ui_text_color(uiControllerInstance.menuIcons[0]));
  for (int i = 1; i < 4; ++i) TEST_ASSERT_EQUAL(lv_color_hex(0xffffff), ui_text_color(uiControllerInstance.menuIcons[i]));
  mockMenuMgr.currentState = UI_STATE_CAT_POWER; ScreenMenu_Update(&settings,25,true,Localization_Snapshot());
  TEST_ASSERT_EQUAL(lv_color_hex(0), ui_text_color(uiControllerInstance.menuIcons[3]));
  TEST_ASSERT_EQUAL(lv_color_hex(0xffffff), ui_text_color(uiControllerInstance.menuIcons[0]));
}

void test_UIRotaryLongPressValidatesAndCopiesDuration(void) {
  IUIController *ui = UIController_GetModule();
  TEST_ASSERT_EQUAL(0, ui->InitializeWithConfig(ui, UIController_GetDefaultConfig(), NULL));
  RESET_FAKE(mocked_xQueueSend); mocked_xQueueSend_fake.custom_fake = custom_xQueueSend;
  RotaryEncoderButtonEvent button = {.duration_ms = RE_LONG_PRESS_MS};
  BusEvent event = {.data = &button, .dataSize = sizeof(button) - 1};
  UIController_OnRotaryLongPressed(event); TEST_ASSERT_EQUAL(0, mocked_xQueueSend_fake.call_count);
  event.dataSize = sizeof(button); button.duration_ms = UINT32_MAX;
  UIController_OnRotaryLongPressed(event); TEST_ASSERT_EQUAL(0, mocked_xQueueSend_fake.call_count);
  button.duration_ms = RE_LONG_PRESS_MS; UIController_OnRotaryLongPressed(event);
  TEST_ASSERT_EQUAL(1, mocked_xQueueSend_fake.call_count);
  TEST_ASSERT_EQUAL(UI_EVENT_ENCODER_LONG_PRESS, lastSentUIEvent.type);
  TEST_ASSERT_EQUAL(RE_LONG_PRESS_MS, lastSentUIEvent.data.encoder.difference);
}

static unsigned ui_reset_calls;
static esp_err_t ui_reset_request(IApplicationManager *self) { (void)self; ++ui_reset_calls; return ESP_FAIL; }
static IApplicationManager ui_reset_app={.TriggerFactoryReset=ui_reset_request};
static IService *ui_reset_service(const char *name) { return strcmp(name,SVC_NAME_APPLICATION_MANAGER)==0 ? &ui_reset_app.base : NULL; }
void test_UIDarkHoldResetIsOnceAndBoundToPressStart(void) {
  prepare_wake(); ui_reset_calls=0; mocked_ServiceLocator_GetService_fake.custom_fake=ui_reset_service;
  UIEvent press={.type=UI_EVENT_ENCODER_PRESS},held={.type=UI_EVENT_ENCODER_HELD,.data.encoder.difference=9999},release={.type=UI_EVENT_ENCODER_RELEASE};
  UIController_HandleEvent(&held); TEST_ASSERT_EQUAL(0,ui_reset_calls);
  UIController_HandleEvent(&press); UIController_HandleEvent(&held); TEST_ASSERT_EQUAL(0,ui_reset_calls);
  held.data.encoder.difference=10000; UIController_HandleEvent(&held); UIController_HandleEvent(&held);
  TEST_ASSERT_EQUAL(1,ui_reset_calls); UIController_HandleEvent(&release); UIController_HandleEvent(&held);
  TEST_ASSERT_EQUAL(1,ui_reset_calls);
  uiControllerInstance.isScreenOn=true; UIController_HandleEvent(&press);
  uiControllerInstance.isScreenOn=false; UIController_HandleEvent(&held); TEST_ASSERT_EQUAL(1,ui_reset_calls);
  UIController_HandleEvent(&release); UIController_HandleEvent(&press); UIController_HandleEvent(&held);
  TEST_ASSERT_EQUAL(2,ui_reset_calls);
}
static int8_t ui_signal_rssi;
static bool ui_signal_valid;
static bool ui_signal_connected;
static unsigned ui_signal_reads;
static int32_t ui_signal_connected_read(IZigbee *self) { (void)self; return ui_signal_connected ? 1 : 0; }
static int32_t ui_signal_info(IZigbee *self,ZigbeeNetworkInfo *out) { (void)self; ++ui_signal_reads; *out=(ZigbeeNetworkInfo){.rssi=ui_signal_rssi,.coordinator_link_valid=ui_signal_valid}; return 0; }
void test_UISignalStrengthUsesMeasuredCoordinatorAndPreservesDot(void) {
  TEST_ASSERT_EQUAL_UINT32(1000U, UI_SIGNAL_REFRESH_MS);
  TEST_ASSERT_EQUAL_UINT32(1000U, UI_SIGNAL_REFRESH_MS);
  TEST_ASSERT_TRUE(ScreenMonitor_Create()); uiControllerInstance.menu=&mockMenuMgr.base; uiControllerInstance.oled=&ui_test_oled;
  IZigbee radio={.IsConnected=icon_connected,.GetNetworkInfo=ui_signal_info};uiControllerInstance.zigbee=&radio;
  BootAnimation_Skip(); SystemSettings settings={0};
  int8_t rssi[]={-50,-70,-90}; int height[]={11,7,4}; ui_signal_valid=true;
  for(unsigned j=0;j<3;++j) {
    ui_signal_rssi=rssi[j];renderValid=false;UIRenderer_Update(100000+j*UI_SIGNAL_REFRESH_MS,&settings,25,24,30);
    for(unsigned i=0;i<128;++i) if((void *)&ui_nodes[i]==(void *)uiControllerInstance.zigbeeIconClip) TEST_ASSERT_EQUAL(height[j],ui_nodes[i].height);
  }
  ui_signal_valid=false;UIRenderer_Update(108000,&settings,25,24,30);
  for(unsigned i=0;i<128;++i) if((void *)&ui_nodes[i]==(void *)uiControllerInstance.zigbeeIconClip) TEST_ASSERT_EQUAL(4,ui_nodes[i].height);
}

void test_UISignalStrengthRefreshesAfterOneSecond(void) {
  TEST_ASSERT_TRUE(ScreenMonitor_Create()); uiControllerInstance.menu=&mockMenuMgr.base; uiControllerInstance.oled=&ui_test_oled;
  IZigbee radio={.IsConnected=ui_signal_connected_read,.GetNetworkInfo=ui_signal_info};uiControllerInstance.zigbee=&radio;
  BootAnimation_Skip(); SystemSettings settings={0}; ui_signal_valid=true; ui_signal_rssi=-65;
  ui_signal_reads=0; ui_signal_connected=false; renderValid=false;
  UIRenderer_Update(300000,&settings,25,24,30);
  TEST_ASSERT_EQUAL_UINT(0,ui_signal_reads);
  ui_signal_connected=true; renderValid=false;
  UIRenderer_Update(300100,&settings,25,24,30);
  TEST_ASSERT_EQUAL_UINT(1,ui_signal_reads);
  renderValid=false; UIRenderer_Update(301099,&settings,25,24,30);
  TEST_ASSERT_EQUAL_UINT(1,ui_signal_reads);
  renderValid=false; UIRenderer_Update(301100,&settings,25,24,30);
  TEST_ASSERT_EQUAL_UINT(2,ui_signal_reads);
  ui_signal_connected=false;
}

void test_UISignalStrengthHysteresisPreventsThresholdFlicker(void) {
  TEST_ASSERT_TRUE(ScreenMonitor_Create()); uiControllerInstance.menu=&mockMenuMgr.base; uiControllerInstance.oled=&ui_test_oled;
  IZigbee radio={.IsConnected=icon_connected,.GetNetworkInfo=ui_signal_info};uiControllerInstance.zigbee=&radio;
  BootAnimation_Skip(); SystemSettings settings={0}; ui_signal_valid=true;
  const int8_t rssi[]={-65,-66,-68,-69,-70,-66,-65};
  const int expectedHeight[]={11,11,11,7,7,7,11};
  for(unsigned j=0;j<sizeof(rssi)/sizeof(rssi[0]);++j) {
    ui_signal_rssi=rssi[j];renderValid=false;
    UIRenderer_Update(200000+j*UI_SIGNAL_REFRESH_MS,&settings,25,24,30);
    bool found=false;
    for(unsigned i=0;i<128;++i) if((void *)&ui_nodes[i]==(void *)uiControllerInstance.zigbeeIconClip) {
      TEST_ASSERT_EQUAL(expectedHeight[j],ui_nodes[i].height);found=true;break;
    }
    TEST_ASSERT_TRUE(found);
  }
}

static AppManagerStatus dark_reset_status;
static esp_err_t dark_reset_status_result;
static esp_err_t dark_reset_admit(IApplicationManager *self) { (void)self; return ESP_OK; }
static esp_err_t dark_reset_read(IApplicationManager *self, AppManagerStatus *out) {
  (void)self;
  if (dark_reset_status_result == ESP_OK) *out = dark_reset_status;
  return dark_reset_status_result;
}
void test_UIDarkResetBlocksWakeUntilTerminalFailure(void) {
  prepare_wake();
  ui_reset_app.TriggerFactoryReset = dark_reset_admit;
  ui_reset_app.GetStatus = dark_reset_read;
  mocked_ServiceLocator_GetService_fake.custom_fake = ui_reset_service;
  UIEvent press = {.type=UI_EVENT_ENCODER_PRESS};
  UIEvent held = {.type=UI_EVENT_ENCODER_HELD, .data.encoder.difference=10000};
  UIController_HandleEvent(&press); UIController_HandleEvent(&held);
  TEST_ASSERT_TRUE(uiControllerInstance.resetKeepsScreenOff);
  TEST_ASSERT_FALSE(UI_WakeToMonitor());
  TEST_ASSERT_FALSE(uiControllerInstance.wakeNavigationPending);
  dark_reset_status = (AppManagerStatus){.factoryResetPhase=FACTORY_RESET_RESTART_REQUESTED};
  dark_reset_status_result = ESP_OK; UI_UpdateResetScreenLatch();
  TEST_ASSERT_TRUE(uiControllerInstance.resetKeepsScreenOff);
  dark_reset_status.factoryResetPhase = FACTORY_RESET_FAILED;
  dark_reset_status.factoryResetBusy = true; UI_UpdateResetScreenLatch();
  TEST_ASSERT_TRUE(uiControllerInstance.resetKeepsScreenOff);
  dark_reset_status.factoryResetBusy = false;
  dark_reset_status_result = ESP_ERR_TIMEOUT; UI_UpdateResetScreenLatch();
  TEST_ASSERT_TRUE(uiControllerInstance.resetKeepsScreenOff);
  dark_reset_status_result = ESP_OK; UI_UpdateResetScreenLatch();
  TEST_ASSERT_FALSE(uiControllerInstance.resetKeepsScreenOff);
  UI_WakeToMonitor(); TEST_ASSERT_TRUE(uiControllerInstance.wakeNavigationPending);
  ui_reset_app.TriggerFactoryReset = ui_reset_request; ui_reset_app.GetStatus = NULL;
}

void test_UIFanModeIconsRefreshAcrossEveryModeWithoutTemperatureChange(void) {
  TEST_ASSERT_TRUE(ScreenMonitor_Create());
  uiControllerInstance.menu = &mockMenuMgr.base; uiControllerInstance.oled = &ui_test_oled;
  BootAnimation_Skip(); renderValid = false;
  SystemSettings settings = {0};
  ClimateFanMode sequence[] = {CLIMATE_FAN_AMBIENT_DELTA, CLIMATE_FAN_OBJECT_SENSITIVE,
    CLIMATE_FAN_MANUAL_OFF, CLIMATE_FAN_MANUAL_ON, CLIMATE_FAN_AMBIENT_DELTA};
  for (unsigned i = 0; i < sizeof(sequence)/sizeof(sequence[0]); ++i) {
    ClimateFanMode mode = sequence[i];
    settings.control.fan_manual_mode = mode == CLIMATE_FAN_MANUAL_ON || mode == CLIMATE_FAN_MANUAL_OFF;
    settings.control.fan_manual_state = mode == CLIMATE_FAN_MANUAL_ON;
    settings.control.fan_strategy = mode == CLIMATE_FAN_AMBIENT_DELTA ? 1 : 0;
    UIRenderer_Update(200000 + i * 10, &settings, 25, 24, 30);
    TEST_ASSERT_EQUAL_PTR(&fan_mode_images[mode], ui_image_source(uiControllerInstance.fanIcon));
    /* Mode alone must not fabricate relay activity. */
    TEST_ASSERT_EQUAL_HEX32(0x111111, ui_text_color(uiControllerInstance.relayIcon));
  }
  for (unsigned i = 0; i < 4; ++i) {
    const lv_image_dsc_t *image = &fan_mode_images[i];
    TEST_ASSERT_EQUAL(121, image->data_size);
    TEST_ASSERT_EQUAL(11, image->header.w); TEST_ASSERT_EQUAL(11, image->header.h);
    for (unsigned j = i + 1; j < 4; ++j)
      TEST_ASSERT_NOT_EQUAL(0, memcmp(image->data, fan_mode_images[j].data, image->data_size));
  }
  const void *original = ui_image_source(uiControllerInstance.fanIcon);
  ScreenMonitor_Update(0, &settings, 25, 24, 30, true, (ClimateFanMode)99, true, 3, Localization_Snapshot());
  TEST_ASSERT_EQUAL_PTR(original, ui_image_source(uiControllerInstance.fanIcon));
  TEST_ASSERT_EQUAL_HEX32(0xFFFFFF, ui_text_color(uiControllerInstance.relayIcon));
}

static void assert_status_ink(lv_obj_t *object, unsigned slot, int left, int top, int width, int height) {
  for (unsigned i = 0; i < 128; ++i) if ((void *)&ui_nodes[i] == (void *)object) {
    TEST_ASSERT_EQUAL(LV_ALIGN_TOP_LEFT, ui_nodes[i].align);
    int x = ui_nodes[i].x + left, y = ui_nodes[i].y + top;
    TEST_ASSERT_TRUE(x >= 83 + (int)slot * 15 && x + width <= 98 + (int)slot * 15);
    TEST_ASSERT_TRUE(y >= 43 && y + height <= 64);
    TEST_ASSERT_INT_WITHIN(1, 2 * (90 + (int)slot * 15), 2 * x + width - 1);
    TEST_ASSERT_INT_WITHIN(1, 106, 2 * y + height - 1);
    return;
  }
  TEST_FAIL_MESSAGE("Status object is missing");
}
void test_UIAllStatusInkIsCenteredInsideEqualCells(void) {
  TEST_ASSERT_TRUE(ScreenMonitor_Create());
  SystemSettings settings = {0};
  for (unsigned mode = 0; mode < 4; ++mode) for (unsigned bars = 0; bars <= 4; ++bars) {
    ScreenMonitor_Update(0, &settings, 25, 24, 30, bars % 2, (ClimateFanMode)mode, bars % 2, bars, Localization_Snapshot());
    const lv_image_dsc_t *image = ui_image_source(uiControllerInstance.fanIcon);
    int left=11, top=11, right=-1, bottom=-1;
    for (int y=0;y<11;++y) for(int x=0;x<11;++x) if(image->data[y*11+x]) {
      if(x<left)left=x; if(x>right)right=x;
      if(y<top)top=y; if(y>bottom)bottom=y;
    }
    assert_status_ink(uiControllerInstance.fanIcon,1,left,top,right-left+1,bottom-top+1);
    assert_status_ink(uiControllerInstance.relayIcon,2,0,0,10,10);
    assert_status_ink(uiControllerInstance.zigbeeIconClip,0,bars>=3?0:bars==2?2:5,
                      bars>=3?1:0,bars>=3?12:bars==2?8:3,bars>=3?9:bars==2?6:3);
  }
}

void test_UIMonitorLanguageRefreshUsesOneCatalogWithoutWaking(void) {
  TEST_ASSERT_TRUE(ScreenMonitor_Create());
  uiControllerInstance.menu=&mockMenuMgr.base;
  uiControllerInstance.oled=&ui_test_oled;
  BootAnimation_Skip(); renderValid=false;
  uiControllerInstance.isScreenOn=false;
  uiControllerInstance.wakeNavigationPending=false;
  SystemSettings settings={0};
  TEST_ASSERT_TRUE(Localization_Apply("tr"));
  UIRenderer_Update(100000,&settings,25,24,30);
  TEST_ASSERT_EQUAL_STRING("Sistem kararlı",ui_label_text(uiControllerInstance.statusLabel));
  TEST_ASSERT_EQUAL_STRING("O:24.0°",ui_label_text(uiControllerInstance.ambientLabel));
  unsigned previousPresent=ui_present_calls;
  TEST_ASSERT_TRUE(Localization_Apply("en"));
  /* No temperature, menu, relay or activity change: catalog alone redraws. */
  UIRenderer_Update(100001,&settings,25,24,30);
  TEST_ASSERT_EQUAL_STRING("System stable",ui_label_text(uiControllerInstance.statusLabel));
  TEST_ASSERT_EQUAL_STRING("A:24.0°",ui_label_text(uiControllerInstance.ambientLabel));
  TEST_ASSERT_EQUAL(previousPresent,ui_present_calls);
  TEST_ASSERT_FALSE(uiControllerInstance.isScreenOn);
  /* A captured catalog remains coherent even after the active locale changes. */
  ScreenMonitor_Update(0,&settings,25,24,30,true,CLIMATE_FAN_MANUAL_ON,true,3,Localization_Find("tr"));
  TEST_ASSERT_EQUAL_STRING("Fan aktif",ui_label_text(uiControllerInstance.statusLabel));
  TEST_ASSERT_EQUAL_STRING("O:24.0°",ui_label_text(uiControllerInstance.ambientLabel));
  for(unsigned i=0;i<128;++i) {
    if((void *)&ui_nodes[i]==(void *)uiControllerInstance.statusLabel) TEST_ASSERT_EQUAL_PTR(&racool_font_10,ui_nodes[i].font);
    if((void *)&ui_nodes[i]==(void *)uiControllerInstance.tempLabel) TEST_ASSERT_EQUAL_PTR(&racool_font_20,ui_nodes[i].font);
    if((void *)&ui_nodes[i]==(void *)uiControllerInstance.zigbeeIcon) TEST_ASSERT_EQUAL_PTR(&lv_font_montserrat_10,ui_nodes[i].font);
  }
  TEST_ASSERT_TRUE(Localization_Apply("tr"));
}

void test_UISettingsSnapshotRefreshDoesNotRequireConfigNotification(void) {
  TEST_ASSERT_TRUE(ScreenMonitor_Create());
  uiControllerInstance.menu=&mockMenuMgr.base;uiControllerInstance.oled=&ui_test_oled;
  BootAnimation_Skip();renderValid=false;
  uiControllerInstance.isScreenOn=false;uiControllerInstance.wakeNavigationPending=false;
  SystemSettings settings={0};
  UIRenderer_Update(100000,&settings,25,24,30);
  TEST_ASSERT_EQUAL_STRING("25.0°C",ui_label_text(uiControllerInstance.tempLabel));
  /* The owner snapshot changed, but its optional queue notification was lost. */
  settings.system.temp_unit=1;
  UIRenderer_Update(100001,&settings,25,24,30);
  TEST_ASSERT_EQUAL_STRING("77.0°F",ui_label_text(uiControllerInstance.tempLabel));
  TEST_ASSERT_EQUAL(0,ui_present_calls);
  TEST_ASSERT_FALSE(uiControllerInstance.isScreenOn);
}

void test_UITranslationsCoverEveryStateAndAnimationInBothLocales(void) {
  for(unsigned language=0;language<2;++language) {
    const LocalizationCatalog *catalog=Localization_Find(language ? "en" : "tr");
    for(unsigned state=0;state<UI_STATE_COUNT;++state) {
      const char *text=UITranslation_MenuLabel((UIState)state,"UNTRANSLATED",catalog);
      TEST_ASSERT_TRUE(text && text[0]);
      TEST_ASSERT_NOT_EQUAL(0,strcmp(text,"UNTRANSLATED"));
    }
    size_t count;const LEDAnimationCatalogEntry *entries=LEDAnimationCatalog_Entries(&count);
    for(size_t index=0;index<count;++index) {
      const char *internal=UITranslation_Animation(entries[index].builtin_id,true,catalog);
      const char *external=UITranslation_Animation(entries[index].external_id,false,catalog);
      TEST_ASSERT_EQUAL_STRING(internal,external);
      TEST_ASSERT_TRUE(internal[0]);
    }
    TEST_ASSERT_EQUAL_STRING(language ? "Candle" : "Mum ışığı",UITranslation_Animation(RGBLED_ANIM_CANDLE,false,catalog));
  }
}
void test_UIConfigAndIndependentTemperaturesRefreshWithoutWake(void) {
  TEST_ASSERT_TRUE(ScreenMonitor_Create());
  uiControllerInstance.menu=&mockMenuMgr.base;uiControllerInstance.oled=&ui_test_oled;
  BootAnimation_Skip();renderValid=false;
  uiControllerInstance.isScreenOn=false;uiControllerInstance.wakeNavigationPending=false;
  SystemSettings settings={0};
  UIRenderer_Update(100000,&settings,25,24,30);
  settings.system.temp_unit=1;
  UIEvent event={.type=UI_EVENT_CONFIG_CHANGED};UIController_HandleEvent(&event);
  UIRenderer_Update(100001,&settings,25,24,30);
  TEST_ASSERT_EQUAL_STRING("77.0°F",ui_label_text(uiControllerInstance.tempLabel));
  settings.system.temp_unit=0;UIController_HandleEvent(&event);
  UIRenderer_Update(100002,&settings,25,24,30);
  UIRenderer_Update(100003,&settings,25,26,30);
  TEST_ASSERT_EQUAL_STRING("O:26.0°",ui_label_text(uiControllerInstance.ambientLabel));
  UIRenderer_Update(100004,&settings,25,26,31);
  TEST_ASSERT_EQUAL_STRING("C:31.0°",ui_label_text(uiControllerInstance.cpuLabel));
  TEST_ASSERT_EQUAL(0,ui_present_calls);TEST_ASSERT_FALSE(uiControllerInstance.isScreenOn);
}
void test_UIMissingMeasurementIsNotAFabricatedTemperature(void) {
  TEST_ASSERT_TRUE(ScreenMonitor_Create());
  uiControllerInstance.menu=&mockMenuMgr.base;uiControllerInstance.oled=&ui_test_oled;
  BootAnimation_Skip();renderValid=false;
  SystemSettings settings={0};
  UIRenderer_Update(100000,&settings,NAN,NAN,NAN);
  TEST_ASSERT_EQUAL_STRING("--.-°C",ui_label_text(uiControllerInstance.tempLabel));
  TEST_ASSERT_EQUAL_STRING("O:--.-°",ui_label_text(uiControllerInstance.ambientLabel));
  TEST_ASSERT_EQUAL_STRING("C:--.-°",ui_label_text(uiControllerInstance.cpuLabel));
  TEST_ASSERT_EQUAL_STRING("Ölçüm bekle",ui_label_text(uiControllerInstance.statusLabel));
  UIRenderer_Update(100001,&settings,25,24,30);
  TEST_ASSERT_EQUAL_STRING("25.0°C",ui_label_text(uiControllerInstance.tempLabel));
}
static unsigned ui_render_cancel_calls;
static int32_t ui_render_cancel(IZigbee *self) { (void)self;++ui_render_cancel_calls;return ESP_FAIL; }
void test_UIPairRendererCannotCancelRadio(void) {
  TEST_ASSERT_TRUE(ScreenZigbeePair_Create());
  IZigbee radio={.DisconnectFromNetwork=ui_render_cancel};uiControllerInstance.zigbee=&radio;
  ui_render_cancel_calls=0;s_pairStartTimeMs=0;s_pairTimedOut=false;
  ScreenZigbeePair_Update(1000,true,false,false,Localization_Snapshot());
  ScreenZigbeePair_Update(181001,true,false,false,Localization_Snapshot());
  TEST_ASSERT_EQUAL(0,ui_render_cancel_calls);
  TEST_ASSERT_TRUE(s_pairTimedOut);
}

void test_UIConsoleMachineStatusReportsAppliedStateInBothLocales(void) {
  char *args[]={"UI","STATUS","--machine"};
  BootAnimation_Skip();
  for(unsigned language=0;language<2;++language) {
    TEST_ASSERT_TRUE(Localization_Apply(language ? "en" : "tr"));
    for(unsigned on=0;on<2;++on) {
      uiControllerInstance.isScreenOn=on;console_output[0]=0;
      TEST_ASSERT_EQUAL(0,cmd_ui(3,args));
      TEST_ASSERT_NOT_NULL(strstr(console_output,on ? "RACOOL_CLI v=1 type=ui screen=active " : "RACOOL_CLI v=1 type=ui screen=suspended "));
      TEST_ASSERT_NULL(strstr(console_output,"Screen State:"));
      TEST_ASSERT_EQUAL(on,uiControllerInstance.isScreenOn);
      TEST_ASSERT_EQUAL(0,mocked_xQueueSend_fake.call_count);
    }
  }
  args[1]="WAKE";TEST_ASSERT_EQUAL(1,cmd_ui(3,args));
  TEST_ASSERT_TRUE(Localization_Apply("tr"));
}



static jmp_buf startup_worker_exit;
static unsigned startup_receive_count, startup_set_count;
static BuiltinLEDAnimationType startup_selected;
static esp_err_t startup_led_status(IBuiltinLED *self, BuiltinLEDStatus *out) {
  (void)self; memset(out,0,sizeof(*out));out->current_animation=BUILTIN_LED_ANIM_COUNT;return ESP_OK;
}
static esp_err_t startup_led_set(IBuiltinLED *self,BuiltinLEDAnimationType animation,BuiltinLEDPriority priority,uint32_t duration) {
  (void)self;(void)priority;(void)duration;startup_selected=animation;++startup_set_count;return ESP_OK;
}
static esp_err_t startup_led_phase(IBuiltinLED *self,bool active) {
  (void)self;
  if(active) {startup_selected=(BuiltinLEDAnimationType)console_settings.hardware.builtin_led_startup_anim;++startup_set_count;}
  return ESP_OK;
}
static BaseType_t startup_receive(QueueHandle_t queue,void *item,TickType_t ticks) {
  (void)queue;(void)item;(void)ticks;
  if(++startup_receive_count==2)longjmp(startup_worker_exit,1);
  return pdFALSE;
}
void test_UIWorker_IndependentBuiltinStartupAndRuntimeHandoff(void) {
  IUIController *ui=UIController_GetModule();
  TEST_ASSERT_EQUAL(0,ui->InitializeWithConfig(ui,UIController_GetDefaultConfig(),NULL));
  uiControllerInstance.oled=&ui_test_oled;
  TEST_ASSERT_TRUE(UIRenderer_CreateUI());
  console_snapshot_result=ESP_OK;
  memset(&console_settings,0,sizeof(console_settings));
  console_settings.system.display_mode=2;
  console_settings.hardware.builtin_led_anim_mode=1;
  console_settings.hardware.builtin_led_startup_anim=SOS_RED;
  console_settings.hardware.builtin_led_idle_anim=SOLID_GREEN;
  console_settings.hardware.ext_led_anim=RGBLED_ANIM_CANDLE;
  uiControllerInstance.configMgr=&console_config;
  IBuiltinLED led={.GetStatus=startup_led_status,.SetAnimation=startup_led_set,.SetStartupActive=startup_led_phase};
  uiControllerInstance.builtinLed=&led;uiControllerInstance.zigbee=&icon_zigbee;
  mockMenuMgr.currentState=UI_STATE_MONITOR;
  const uint8_t modes[]={1,1,0};
  const BuiltinLEDAnimationType expected[]={SOS_RED,SOLID_GREEN,BUILTIN_LED_ANIM_NONE};
  for(unsigned phase=0;phase<3;++phase) {
    if(phase==0)BootAnimation_Start();else BootAnimation_Skip();
    console_settings.hardware.builtin_led_anim_mode=modes[phase];
    startup_receive_count=startup_set_count=0;
    mocked_xQueueReceive_fake.custom_fake=startup_receive;
    if(!setjmp(startup_worker_exit))UIController_Task(NULL);
    TEST_ASSERT_EQUAL(1,startup_set_count);
    TEST_ASSERT_EQUAL(expected[phase],startup_selected);
    TEST_ASSERT_EQUAL(RGBLED_ANIM_CANDLE,console_settings.hardware.ext_led_anim);
  }
  mocked_xQueueReceive_fake.custom_fake=NULL;
  uiControllerInstance.builtinLed=NULL;
}

void test_BootProgressQueuesCompletedGroupsAndHonorsMinimumDwell(void) {
  TEST_ASSERT_TRUE(ScreenBoot_Create());BootAnimation_Start();
  uint32_t core=s_progressGroups[0].mask, sensors=s_progressGroups[2].mask;
  ui_startup_progress.startedStages=core|sensors|STAGE_BIT(STARTUP_STAGE_ZIGBEE);
  ui_startup_progress.completedStages=core|sensors;
  ui_startup_progress.activeStages=STAGE_BIT(STARTUP_STAGE_ZIGBEE);
  BootAnimation_UpdateProgressText(100);
  TEST_ASSERT_EQUAL_STRING("Temel sistem hazır",ui_label_text(uiControllerInstance.bootTaglineLabel));
  BootAnimation_UpdateProgressText(1099);TEST_ASSERT_EQUAL(0,s_progressCursor);
  BootAnimation_UpdateProgressText(1100);
  TEST_ASSERT_EQUAL_STRING("Sensörler hazır",ui_label_text(uiControllerInstance.bootTaglineLabel));
  BootAnimation_UpdateProgressText(2100);
  TEST_ASSERT_EQUAL_STRING("Zigbee hazırlanıyor",ui_label_text(uiControllerInstance.bootTaglineLabel));
  BootAnimation_NotifyReady();ui_startup_progress.activeStages=0;
  BootAnimation_UpdateProgressText(2200);
  TEST_ASSERT_EQUAL_STRING("Zigbee hazırlanıyor",ui_label_text(uiControllerInstance.bootTaglineLabel));
  BootAnimation_UpdateProgressText(3100);
  TEST_ASSERT_EQUAL_STRING("Zigbee hazır",ui_label_text(uiControllerInstance.bootTaglineLabel));
  BootAnimation_UpdateProgressText(4100);TEST_ASSERT_FALSE(s_progressDrained);
  TEST_ASSERT_EQUAL_STRING("",ui_label_text(uiControllerInstance.bootTaglineLabel));
  TEST_ASSERT_EQUAL_STRING("Hazır",ui_label_text(uiControllerInstance.bootStatusLabel));
  TEST_ASSERT_TRUE(Localization_Apply("en"));BootAnimation_UpdateProgressText(5100);
  TEST_ASSERT_EQUAL_STRING("",ui_label_text(uiControllerInstance.bootTaglineLabel));
  TEST_ASSERT_EQUAL_STRING("Ready",ui_label_text(uiControllerInstance.bootStatusLabel));
  TEST_ASSERT_TRUE(s_progressDrained);
}
void test_BootProgressNeverInfersReadyFromControllerAndShowsFailure(void) {
  TEST_ASSERT_TRUE(ScreenBoot_Create());BootAnimation_Start();
  mockSystemState=SYSTEM_STATE_READY;
  ui_startup_progress.activeStages=STAGE_BIT(STARTUP_STAGE_ZIGBEE);
  BootAnimation_UpdateProgressText(100);
  TEST_ASSERT_FALSE(atomic_load(&s_systemReady));
  TEST_ASSERT_EQUAL_STRING("Zigbee hazırlanıyor",ui_label_text(uiControllerInstance.bootTaglineLabel));
  BootAnimation_NotifyReady();ui_startup_progress.failed=true;
  BootAnimation_UpdateProgressText(200);
  TEST_ASSERT_FALSE(atomic_load(&s_systemReady));
  TEST_ASSERT_EQUAL_STRING("Başlatma başarısız",ui_label_text(uiControllerInstance.bootTaglineLabel));
}
void test_BootQueueSurvivesClockWrapAndKeepsAllGroupsBeforeExit(void) {
  TEST_ASSERT_TRUE(ScreenBoot_Create());BootAnimation_Start();
  ui_startup_progress.startedStages=(1U<<STARTUP_STAGE_COUNT)-1;
  ui_startup_progress.completedStages=ui_startup_progress.startedStages;
  BootAnimation_NotifyReady();
  BootAnimation_UpdateProgressText(UINT32_MAX-499);
  BootAnimation_UpdateProgressText(499);TEST_ASSERT_EQUAL(0,s_progressCursor);
  BootAnimation_UpdateProgressText(500);TEST_ASSERT_EQUAL(1,s_progressCursor);
  for(unsigned i=1;i<=5;++i)BootAnimation_UpdateProgressText(500+i*1000);
  TEST_ASSERT_EQUAL(5,s_progressCount);TEST_ASSERT_EQUAL(5,s_progressCursor);
  TEST_ASSERT_TRUE(s_progressDrained);
  BootAnimation_Start();TEST_ASSERT_EQUAL(0,s_progressCount);TEST_ASSERT_FALSE(s_progressDrained);
}
static unsigned idle_posts;
static MenuEvent idle_event;
static MenuManagerError idle_admission;
static MenuManagerError capture_idle_event(IMenuManager *self,const MenuEvent *event) {
  (void)self;++idle_posts;idle_event=*event;return idle_admission;
}
void test_UIAlwaysOnIdleReturnsHomeWithoutPowerChangeOrRepeatedQueueing(void) {
  prepare_inactivity(0,20000);
  mockMenuMgr.currentState=UI_STATE_SET_HIGH_TEMP;mockMenuMgr.isEditing=true;
  mockMenuMgr.base.PostEvent=capture_idle_event;
  idle_posts=0;idle_admission=MENU_OK;
  UI_ApplyInactivityPower(20000, false, true);TEST_ASSERT_EQUAL(0,idle_posts);
  mocked_esp_timer_get_time_fake.return_val=20001000;
  UI_ApplyInactivityPower(20000, false, true);
  TEST_ASSERT_EQUAL(1,idle_posts);TEST_ASSERT_EQUAL(MENU_EVENT_NAVIGATE_TO,idle_event.type);
  TEST_ASSERT_EQUAL(UI_STATE_MONITOR,idle_event.data.targetState);
  TEST_ASSERT_TRUE(uiControllerInstance.isScreenOn);TEST_ASSERT_EQUAL(0,inactivity_power_calls);
  TEST_ASSERT_TRUE(mockMenuMgr.isEditing); /* Worker owns cancellation. */
  UI_ApplyInactivityPower(20000, false, true);TEST_ASSERT_EQUAL(1,idle_posts);
  mockMenuMgr.currentState=UI_STATE_MONITOR;mockMenuMgr.isEditing=false;
  UI_ApplyInactivityPower(20000, false, true);
  TEST_ASSERT_FALSE(uiControllerInstance.idleNavigationPending);TEST_ASSERT_EQUAL(1,idle_posts);
}
void test_UIAlwaysOnIdleQueueRejectionRetriesAndRecentInputStaysInMenu(void) {
  prepare_inactivity(0,30000);mockMenuMgr.currentState=UI_STATE_CAT_TEMP;
  mockMenuMgr.base.PostEvent=capture_idle_event;idle_posts=0;idle_admission=MENU_ERR_QUEUE_FULL;
  UI_ApplyInactivityPower(20000, false, true);TEST_ASSERT_FALSE(uiControllerInstance.idleNavigationPending);
  idle_admission=MENU_OK;UI_ApplyInactivityPower(20000, false, true);
  TEST_ASSERT_EQUAL(2,idle_posts);TEST_ASSERT_TRUE(uiControllerInstance.idleNavigationPending);
  uiControllerInstance.idleNavigationPending=false;mockMenuMgr.lastActivityTime=29999;
  UI_ApplyInactivityPower(20000, false, true);TEST_ASSERT_EQUAL(2,idle_posts);
  uiControllerInstance.isScreenOn=false;mockMenuMgr.lastActivityTime=0;
  UI_ApplyInactivityPower(20000, false, true);TEST_ASSERT_EQUAL(2,idle_posts);TEST_ASSERT_EQUAL(0,inactivity_power_calls);
}
void test_UIWorkerAlwaysOnStillAppliesConfiguredMenuTimeout(void) {
  IUIController *ui=UIController_GetModule();
  TEST_ASSERT_EQUAL(0,ui->InitializeWithConfig(ui,UIController_GetDefaultConfig(),NULL));
  uiControllerInstance.oled=&ui_test_oled;TEST_ASSERT_TRUE(UIRenderer_CreateUI());BootAnimation_Skip();
  console_snapshot_result=ESP_OK;memset(&console_settings,0,sizeof(console_settings));
  console_settings.system.display_mode=2;console_settings.system.oled_timeout_sec=20;
  console_settings.system.menu_auto_exit_enabled=DEFAULT_MENU_AUTO_EXIT_ENABLED;
  uiControllerInstance.configMgr=&console_config;uiControllerInstance.menu=&mockMenuMgr.base;
  uiControllerInstance.isScreenOn=true;mockMenuMgr.currentState=UI_STATE_CAT_TEMP;
  mockMenuMgr.lastActivityTime=0;mockMenuMgr.base.PostEvent=capture_idle_event;
  idle_posts=0;idle_admission=MENU_OK;
  mocked_esp_timer_get_time_fake.return_val=30000000;
  startup_receive_count=0;mocked_xQueueReceive_fake.custom_fake=startup_receive;
  if(!setjmp(startup_worker_exit)) UIController_Task(NULL);
  TEST_ASSERT_EQUAL(1,idle_posts);TEST_ASSERT_TRUE(uiControllerInstance.isScreenOn);
  mocked_xQueueReceive_fake.custom_fake=NULL;
}

void test_TelemetryListDistinguishesDisabledFromAnActiveInterval(void) {
  SystemSettings settings={0};char label[128];
  for(unsigned language=0;language<2;++language) {
    const LocalizationCatalog *catalog=Localization_Find(language ? "en" : "tr");
    TEST_ASSERT_NOT_NULL(catalog);
    FormatMenuItemLabel(UI_STATE_SET_TELEMETRY,"Interval",&settings,label,sizeof(label),catalog);
    TEST_ASSERT_NOT_NULL(strstr(label,Localization_Text(catalog,LOC_COMMON_OFF)));
    TEST_ASSERT_NULL(strstr(label,"0 s"));
    settings.system.telemetry_interval_ms=5000;
    FormatMenuItemLabel(UI_STATE_SET_TELEMETRY,"Interval",&settings,label,sizeof(label),catalog);
    TEST_ASSERT_NOT_NULL(strstr(label,"5 s"));
    TEST_ASSERT_NULL(strstr(label,Localization_Text(catalog,LOC_COMMON_OFF)));
    settings.system.telemetry_interval_ms=0;
  }
}

void test_UIDisabledMenuAutoExitKeepsMenuButPreservesScreenSleep(void) {
  prepare_inactivity(0,30000);mockMenuMgr.currentState=UI_STATE_CAT_TEMP;
  uiControllerInstance.idleNavigationPending=false;
  mockMenuMgr.base.PostEvent=capture_idle_event;idle_posts=0;idle_admission=MENU_OK;
  UI_ApplyInactivityPower(20000,false,false);
  TEST_ASSERT_EQUAL(0,idle_posts);TEST_ASSERT_EQUAL(0,inactivity_power_calls);
  TEST_ASSERT_EQUAL(UI_STATE_CAT_TEMP,mockMenuMgr.currentState);
  UI_ApplyInactivityPower(20000,false,true);
  TEST_ASSERT_EQUAL(1,idle_posts);
  uiControllerInstance.idleNavigationPending=false;
  UI_ApplyInactivityPower(20000,true,false);
  TEST_ASSERT_EQUAL(1,inactivity_power_calls);TEST_ASSERT_FALSE(inactivity_power_on);
}
