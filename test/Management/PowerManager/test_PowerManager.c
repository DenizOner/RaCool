#include "Management/PowerManager/IPowerManager.h"
#include "Management/PowerManager/PowerManagerConsoleCommands.h"
#include "test_mocks.h"
#include "unity.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "Management/PowerManager/PowerManager.c"
#include "Management/PowerManager/PowerManagerConsoleCommands.c"

FAKE_VALUE_FUNC1(esp_err_t, esp_pm_configure, const void *);
FAKE_VALUE_FUNC1(esp_err_t, esp_pm_get_configuration, void *);
FAKE_VALUE_FUNC4(esp_err_t, esp_pm_lock_create, esp_pm_lock_type_t, int, const char *, esp_pm_lock_handle_t *);
FAKE_VALUE_FUNC1(esp_err_t, esp_pm_lock_acquire, esp_pm_lock_handle_t);
FAKE_VALUE_FUNC1(esp_err_t, esp_pm_lock_release, esp_pm_lock_handle_t);
FAKE_VALUE_FUNC1(esp_err_t, esp_pm_lock_delete, esp_pm_lock_handle_t);
static esp_err_t createLock(esp_pm_lock_type_t type, int arg, const char *name, esp_pm_lock_handle_t *handle) {
    (void)type; (void)arg; (void)name;
    *handle = (void *)(uintptr_t)(esp_pm_lock_create_fake.call_count + 1);
    return ESP_OK;
}
static esp_pm_config_t appliedProfile;
static SystemSettings power_settings;
static esp_err_t persist_result;
static unsigned persist_calls;
static esp_err_t power_snapshot(IConfigurationManager *self, SystemSettings *out) {
    (void)self; *out = power_settings; return ESP_OK;
}
static esp_err_t power_update(IConfigurationManager *self, const char *block,
    const ConfigurationFieldUpdate *updates, size_t count, bool persist) {
    (void)self;
    TEST_ASSERT_EQUAL_STRING("system", block);
    TEST_ASSERT_EQUAL(1, count);
    TEST_ASSERT_TRUE(persist);
    TEST_ASSERT_EQUAL(offsetof(SystemBaseConfigs, power_profile), updates[0].offset);
    TEST_ASSERT_EQUAL(1, updates[0].size);
    persist_calls++;
    if (persist_result == ESP_OK) power_settings.system.power_profile = *(const uint8_t *)updates[0].data;
    return persist_result;
}
static IConfigurationManager power_config = {.GetSnapshot = power_snapshot, .UpdateFields = power_update};
static esp_err_t power_registration_result;
static esp_err_t power_register(IDebugConsole *self, const esp_console_cmd_t *command, CommandCategory category) {
    (void)self; (void)category;
    TEST_ASSERT_EQUAL_STRING("power", command->command);
    return power_registration_result;
}
static IDebugConsole power_console = {.RegisterCommandWithCategory = power_register};
static IService *power_locator(const char *name) {
    if (strcmp(name, SVC_NAME_DEBUG_CONSOLE) == 0) return (IService *)&power_console;
    return strcmp(name, SVC_NAME_CONFIGURATION_MANAGER) == 0 ? (IService *)&power_config : NULL;
}
static esp_err_t current_profile(void *out) {
    *(esp_pm_config_t *)out = appliedProfile;
    return ESP_OK;
}
static esp_err_t captureProfile(const void *config) {
    appliedProfile = *(const esp_pm_config_t *)config;
    return ESP_OK;
}
static esp_err_t create_restart_timer(const esp_timer_create_args_t *args, esp_timer_handle_t *out) {
    (void)args;
    if (mocked_esp_timer_create_fake.return_val == ESP_OK) *out = (esp_timer_handle_t)7;
    return mocked_esp_timer_create_fake.return_val;
}
void setUp(void) {
    power_registration_result = ESP_OK;
    RESET_FAKE(mocked_esp_deep_sleep);
    RESET_FAKE(mocked_esp_deep_sleep_start);
    RESET_FAKE(mocked_esp_light_sleep_start);
    RESET_FAKE(mocked_esp_sleep_enable_timer_wakeup);
    RESET_FAKE(mocked_esp_sleep_disable_wakeup_source);
    RESET_FAKE(mocked_esp_deep_sleep_enable_gpio_wakeup);
    RESET_FAKE(mocked_esp_sleep_is_valid_wakeup_gpio);
    mocked_esp_sleep_is_valid_wakeup_gpio_fake.return_val = true;
    powerManager.state = SERVICE_STATE_UNINITIALIZED;
    powerManager.sleepTimerOwned = false;
    powerManager.sleepGpioOwned = false;
    atomic_store(&powerManager.sleepPending, false);
    RESET_FAKE(mocked_xSemaphoreCreateMutex);
    RESET_FAKE(mocked_xSemaphoreTake);
    RESET_FAKE(mocked_xSemaphoreGive);
    mocked_xSemaphoreCreateMutex_fake.return_val = (SemaphoreHandle_t)1;
    mocked_xSemaphoreTake_fake.return_val = pdTRUE;
    mocked_xSemaphoreGive_fake.return_val = pdTRUE;
    powerManager.mutex = (SemaphoreHandle_t)1;
    RESET_FAKE(mocked_ServiceLocator_GetService);
    mocked_ServiceLocator_GetService_fake.custom_fake = power_locator;
    memset(&power_settings, 0, sizeof(power_settings));
    power_settings.system.power_profile = DEFAULT_POWER_PROFILE;
    persist_result = ESP_OK;
    persist_calls = 0;
    appliedProfile = (esp_pm_config_t){.min_freq_mhz = 160, .max_freq_mhz = 160};
    RESET_FAKE(esp_pm_get_configuration);
    esp_pm_get_configuration_fake.custom_fake = current_profile;
    RESET_FAKE(mocked_esp_timer_create); RESET_FAKE(mocked_esp_timer_start_once);
    RESET_FAKE(mocked_esp_timer_stop); RESET_FAKE(mocked_esp_restart);
    mocked_esp_timer_create_fake.custom_fake = create_restart_timer;
    powerManager.restartTimer = NULL;
    atomic_store(&powerManager.restartPending, false);
    RESET_FAKE(esp_pm_configure);
    RESET_FAKE(esp_pm_lock_create);
    RESET_FAKE(esp_pm_lock_acquire);
    RESET_FAKE(esp_pm_lock_release);
    RESET_FAKE(esp_pm_lock_delete);
    esp_pm_lock_create_fake.custom_fake = createLock;
    memset(powerManager.locks, 0, sizeof(powerManager.locks));
    powerManager.activeLockCount = 0;
    esp_pm_configure_fake.custom_fake = captureProfile;
}
void tearDown(void) {}

void setUp_PowerManager(void) {
  powerManager.state = SERVICE_STATE_UNINITIALIZED;
  powerManager.activeLockCount = 0;
  memset(&powerManager.stats, 0, sizeof(powerManager.stats));
}

void tearDown_PowerManager(void) {}


/**
 * @brief Test Lifecycle
 */
void test_PowerManager_Lifecycle(void) {
  setUp_PowerManager();
  IPowerManager *pm = PowerManager_GetInstance();
  IService *service = &(pm->base);

  TEST_ASSERT_EQUAL(POWER_OK, service->Initialize(service, NULL));
  TEST_ASSERT_EQUAL(SERVICE_STATE_INITIALIZED, powerManager.state);

  TEST_ASSERT_EQUAL(POWER_OK, service->Start(service));
  TEST_ASSERT_EQUAL(SERVICE_STATE_STARTED, powerManager.state);

  TEST_ASSERT_EQUAL(0, service->CheckHealth(service));

  TEST_ASSERT_EQUAL(POWER_OK, service->Stop(service));
  TEST_ASSERT_EQUAL(SERVICE_STATE_STOPPED, powerManager.state);
}

/**
 * @brief Test Restart & RestartAsync
 */
static IPowerManager *start_restart_test(void) {
    setUp_PowerManager();
    IPowerManager *pm = PowerManager_GetInstance();
    TEST_ASSERT_EQUAL(POWER_OK, pm->base.Initialize(&pm->base, NULL));
    TEST_ASSERT_EQUAL(POWER_OK, pm->base.Start(&pm->base));
    return pm;
}
void test_PowerManager_DeferredRestartFailureDoesNotRestart(void) {
    IPowerManager *pm = start_restart_test();
    mocked_esp_timer_create_fake.return_val = ESP_ERR_NO_MEM;
    TEST_ASSERT_EQUAL(POWER_ERR_NO_MEM, pm->RestartAsync(pm, 500));
    TEST_ASSERT_EQUAL(0, mocked_esp_restart_fake.call_count);
    TEST_ASSERT_EQUAL(0, powerManager.stats.totalRestarts);
    mocked_esp_timer_create_fake.return_val = ESP_OK;
    mocked_esp_timer_start_once_fake.return_val = ESP_FAIL;
    TEST_ASSERT_EQUAL(POWER_ERR_TIMEOUT, pm->RestartAsync(pm, 500));
    TEST_ASSERT_EQUAL(0, mocked_esp_restart_fake.call_count);
    TEST_ASSERT_EQUAL(0, powerManager.stats.totalRestarts);
    mocked_esp_timer_start_once_fake.return_val = ESP_OK;
    TEST_ASSERT_EQUAL(POWER_OK, pm->RestartAsync(pm, 500));
    TEST_ASSERT_EQUAL(2, mocked_esp_timer_create_fake.call_count);
    TEST_ASSERT_EQUAL(1, powerManager.stats.totalRestarts);
}
void test_PowerManager_DeferredRestartReusesTimerAndRejectsDuplicate(void) {
    IPowerManager *pm = start_restart_test();
    TEST_ASSERT_EQUAL(POWER_OK, pm->RestartAsync(pm, 0));
    TEST_ASSERT_EQUAL(1000, mocked_esp_timer_start_once_fake.arg1_val);
    TEST_ASSERT_EQUAL(POWER_ERR_BUSY, pm->RestartAsync(pm, 100));
    TEST_ASSERT_EQUAL(0, mocked_esp_restart_fake.call_count);
    RestartTimerCallback(NULL);
    TEST_ASSERT_EQUAL(1, mocked_esp_restart_fake.call_count);
    TEST_ASSERT_EQUAL(POWER_OK, pm->RestartAsync(pm, 100));
    TEST_ASSERT_EQUAL(1, mocked_esp_timer_create_fake.call_count);
}
void test_PowerManager_StopCancelsPendingRestartAndPropagatesFailure(void) {
    IPowerManager *pm = start_restart_test();
    TEST_ASSERT_EQUAL(POWER_OK, pm->RestartAsync(pm, 500));
    mocked_esp_timer_stop_fake.return_val = ESP_FAIL;
    TEST_ASSERT_EQUAL(POWER_ERR_TIMEOUT, pm->base.Stop(&pm->base));
    TEST_ASSERT_TRUE(atomic_load(&powerManager.restartPending));
    mocked_esp_timer_stop_fake.return_val = ESP_OK;
    TEST_ASSERT_EQUAL(POWER_OK, pm->base.Stop(&pm->base));
    RestartTimerCallback(NULL);
    TEST_ASSERT_EQUAL(0, mocked_esp_restart_fake.call_count);
}
void test_PowerManager_DeferredRestartValidatesStateAndDelay(void) {
    IPowerManager *pm = PowerManager_GetInstance();
    powerManager.state = SERVICE_STATE_STOPPED;
    TEST_ASSERT_EQUAL(POWER_ERR_INVALID_STATE, pm->RestartAsync(pm, 500));
    TEST_ASSERT_EQUAL(POWER_ERR_INVALID_ARG, pm->RestartAsync(pm, POWER_MAX_RESTART_DELAY_MS + 1));
    TEST_ASSERT_EQUAL(POWER_ERR_INVALID_ARG, pm->RestartAsync(NULL, 0));
    TEST_ASSERT_EQUAL(0, mocked_esp_timer_create_fake.call_count);
    TEST_ASSERT_EQUAL(0, mocked_esp_restart_fake.call_count);
}

void test_PowerManager_Restart(void) {
  setUp_PowerManager();
  IPowerManager *pm = PowerManager_GetInstance();
  pm->Restart(pm);
  TEST_ASSERT_EQUAL(1, mocked_esp_restart_fake.call_count);

  TEST_ASSERT_EQUAL(POWER_OK, pm->base.Initialize(&pm->base, NULL));
  TEST_ASSERT_EQUAL(POWER_OK, pm->base.Start(&pm->base));
  TEST_ASSERT_EQUAL(POWER_OK, pm->RestartAsync(pm, 100));
  TEST_ASSERT_EQUAL(1, mocked_esp_restart_fake.call_count);
  RestartTimerCallback(NULL);
  TEST_ASSERT_EQUAL(2, mocked_esp_restart_fake.call_count);
}

/**
 * @brief Test Deep Sleep & Light Sleep
 */
void test_PowerManager_SleepModes(void) {
  setUp_PowerManager();
  IPowerManager *pm = PowerManager_GetInstance();

  IService *svc = &(pm->base);
  svc->Initialize(svc, NULL);

  powerManager.config.enableRotaryWakeup = false;

  // Case 1: Deep Sleep With duration
  pm->EnterDeepSleep(pm, 1000000);
  TEST_ASSERT_EQUAL(1, mocked_esp_deep_sleep_fake.call_count);

  // Case 2: Deep Sleep Indefinite (0 duration)
  pm->EnterDeepSleep(pm, 0);
  TEST_ASSERT_EQUAL(1, mocked_esp_deep_sleep_start_fake.call_count);

  // Case 3: Light Sleep
  TEST_ASSERT_EQUAL(POWER_OK, pm->EnterLightSleep(pm, 500000));
  TEST_ASSERT_EQUAL(1, mocked_esp_light_sleep_start_fake.call_count);
}

/**
 * @brief Test Power Modes & Lock Acquisition
 */
void test_PowerManager_ModesAndLocks(void) {
  setUp_PowerManager();
  IPowerManager *pm = PowerManager_GetInstance();
  IService *svc = &(pm->base);
  svc->Initialize(svc, NULL);

  TEST_ASSERT_EQUAL(POWER_OK, pm->SetPowerMode(pm, POWER_MODE_PERFORMANCE));
  TEST_ASSERT_EQUAL(POWER_MODE_PERFORMANCE, pm->GetPowerMode(pm));

  TEST_ASSERT_EQUAL(POWER_OK, pm->SetPowerMode(pm, POWER_MODE_LOW_POWER));
  TEST_ASSERT_EQUAL(POWER_MODE_LOW_POWER, pm->GetPowerMode(pm));

  TEST_ASSERT_EQUAL(POWER_OK, pm->AcquirePowerLock(pm, "TestLock"));
  TEST_ASSERT_EQUAL(1, powerManager.activeLockCount);

  TEST_ASSERT_EQUAL(POWER_OK, pm->ReleasePowerLock(pm, "TestLock"));
  TEST_ASSERT_EQUAL(0, powerManager.activeLockCount);
}

/**
 * @brief Test Reset Reason and Telemetry Stats
 */
void test_PowerManager_Telemetry(void) {
  setUp_PowerManager();
  IPowerManager *pm = PowerManager_GetInstance();
  IService *svc = &(pm->base);
  svc->Initialize(svc, NULL);
  TEST_ASSERT_EQUAL(POWER_OK, svc->Start(svc));

  PowerStats_t stats;
  TEST_ASSERT_EQUAL(POWER_OK, pm->GetStats(pm, &stats));
  TEST_ASSERT_NOT_NULL(pm->GetResetReasonString(pm));
  TEST_ASSERT_EQUAL(POWER_OK, pm->SelfTest(pm));
}

/**
 * @brief Test Error Paths
 */
void test_PowerManager_Errors(void) {
  setUp_PowerManager();
  IPowerManager *pm = PowerManager_GetInstance();
  IService *service = &(pm->base);

  // Start fail (not initialized)
  powerManager.state = SERVICE_STATE_STOPPED;
  TEST_ASSERT_EQUAL(-1, service->Start(service));

  // Stop fail (not started)
  powerManager.state = SERVICE_STATE_INITIALIZED;
  TEST_ASSERT_EQUAL(-1, service->Stop(service));

  // Invalid args
  TEST_ASSERT_EQUAL(POWER_ERR_INVALID_ARG, pm->SetPowerMode(NULL, POWER_MODE_BALANCED));
  TEST_ASSERT_EQUAL(POWER_ERR_INVALID_ARG, pm->AcquirePowerLock(NULL, "Test"));
  TEST_ASSERT_EQUAL(POWER_ERR_INVALID_ARG, pm->ReleasePowerLock(pm, NULL));
  TEST_ASSERT_EQUAL(POWER_ERR_INVALID_ARG, pm->GetStats(NULL, NULL));
  TEST_ASSERT_EQUAL(POWER_ERR_INVALID_ARG, pm->SelfTest(NULL));
}

static IService sleep_services[5];
static IOLED sleep_oled;
static unsigned oled_power_calls;
static bool oled_fail;
static bool oled_restore_fail;
static esp_err_t sleep_oled_power(IOLED *self, bool on) {
    (void)self;
    ++oled_power_calls;
    TEST_ASSERT_EQUAL(oled_power_calls > 1, on);
    return (!on && oled_fail) || (on && oled_restore_fail) ? ESP_FAIL : ESP_OK;
}
static unsigned sleep_order;
static unsigned restore_order[5], restore_count;
static int restore_failed_index=-1;
static int32_t sleep_start(IService *service) {
    unsigned index=(unsigned)(service-sleep_services);restore_order[restore_count++]=index;
    return (int)index==restore_failed_index ? -123 : 0;
}
static bool stop_failed;
static ServiceState sleep_started(IService *service) { (void)service; return SERVICE_STATE_STARTED; }
static int32_t sleep_stop(IService *service) {
    TEST_ASSERT_EQUAL_PTR(&sleep_services[sleep_order], service);
    ++sleep_order;
    return stop_failed && sleep_order == 1 ? -1 : 0;
}
static gpio_num_t sleep_wake_pin(IPinManager *self, PinFunction_t function) {
    (void)self; TEST_ASSERT_EQUAL(PIN_FUNC_ROTARY_SW, function); return 3;
}
static IPinManager sleep_pins = {.GetPin = sleep_wake_pin};
static bool provide_sleep_pins;
static IService *sleep_locator(const char *name) {
    if (!strcmp(name, SVC_NAME_PIN_MANAGER) && provide_sleep_pins) return (IService *)&sleep_pins;
    if (!strcmp(name, SVC_NAME_OLED)) return (IService *)&sleep_oled;
    const char *names[] = {SVC_NAME_UI_CONTROLLER, SVC_NAME_CLIMATE_CONTROLLER, SVC_NAME_RELAY,
                          SVC_NAME_RGB_LED, SVC_NAME_BUILTIN_LED};
    for (unsigned i = 0; i < 5; ++i) if (strcmp(name, names[i]) == 0) return &sleep_services[i];
    return NULL;
}
static void prepare_sleep_test(void) {
    powerManager.state = SERVICE_STATE_INITIALIZED;
    provide_sleep_pins = false;
    sleep_order = 0; restore_count = 0; stop_failed = false;restore_failed_index=-1;
    oled_power_calls = 0; oled_fail = false; oled_restore_fail = false;
    sleep_oled.SetPower = sleep_oled_power;
    for (unsigned i=0; i<5; ++i) sleep_services[i] = (IService){.Stop=sleep_stop, .Start=sleep_start, .GetState=sleep_started};
    mocked_ServiceLocator_GetService_fake.custom_fake = sleep_locator;
    RESET_FAKE(mocked_esp_deep_sleep_start);
    powerManager.config.enableRotaryWakeup = false;
}
void test_PowerManager_SleepStopsProducersBeforeOutputs(void) {
    prepare_sleep_test();
    IPowerManager *pm = PowerManager_GetInstance();
    TEST_ASSERT_EQUAL(POWER_OK, pm->EnterDeepSleep(pm, 0));
    TEST_ASSERT_EQUAL(5, sleep_order);
    TEST_ASSERT_EQUAL(1, mocked_esp_deep_sleep_start_fake.call_count);
    mocked_ServiceLocator_GetService_fake.custom_fake = NULL;
}
void test_PowerManager_SleepFailureStillStopsOutputs(void) {
    prepare_sleep_test(); stop_failed = true;
    uint32_t sleeps = powerManager.stats.totalSleeps;
    IPowerManager *pm = PowerManager_GetInstance();
    TEST_ASSERT_EQUAL(POWER_ERR_TIMEOUT, pm->EnterDeepSleep(pm, 0));
    TEST_ASSERT_EQUAL(5, sleep_order);
    TEST_ASSERT_EQUAL(0, mocked_esp_deep_sleep_start_fake.call_count);
    TEST_ASSERT_EQUAL(sleeps, powerManager.stats.totalSleeps);
    TEST_ASSERT_EQUAL_UINT(4, restore_count);
    for (unsigned i = 0; i < 4; ++i) TEST_ASSERT_EQUAL_UINT(4 - i, restore_order[i]);
    mocked_ServiceLocator_GetService_fake.custom_fake = NULL;
}
void test_PowerManager_MissingWakeupAbortsBeforeCleanup(void) {
    prepare_sleep_test(); powerManager.config.enableRotaryWakeup = true;
    IPowerManager *pm = PowerManager_GetInstance();
    TEST_ASSERT_EQUAL(POWER_ERR_INVALID_STATE, pm->EnterDeepSleep(pm, 0));
    TEST_ASSERT_EQUAL(0, sleep_order);
    TEST_ASSERT_EQUAL(0, mocked_esp_deep_sleep_start_fake.call_count);
    mocked_ServiceLocator_GetService_fake.custom_fake = NULL;
}

void test_PowerManager_ProfileRollbackAndBounds(void) {
    IPowerManager *pm = PowerManager_GetInstance();
    TEST_ASSERT_EQUAL(POWER_OK, pm->base.Initialize(&pm->base, NULL));
    TEST_ASSERT_EQUAL(POWER_OK, pm->SetPowerMode(pm, POWER_MODE_BALANCED));
    TEST_ASSERT_EQUAL(80, appliedProfile.min_freq_mhz);
    TEST_ASSERT_EQUAL(160, appliedProfile.max_freq_mhz);
    TEST_ASSERT_FALSE(appliedProfile.light_sleep_enable);
    TEST_ASSERT_EQUAL(POWER_OK, pm->SetPowerMode(pm, POWER_MODE_LOW_POWER));
    TEST_ASSERT_EQUAL(40, appliedProfile.min_freq_mhz);
    TEST_ASSERT_EQUAL(80, appliedProfile.max_freq_mhz);
    TEST_ASSERT_FALSE(appliedProfile.light_sleep_enable);
    esp_pm_configure_fake.custom_fake = NULL;
    esp_pm_configure_fake.return_val = ESP_FAIL;
    TEST_ASSERT_EQUAL(POWER_ERR_INVALID_STATE, pm->SetPowerMode(pm, POWER_MODE_PERFORMANCE));
    TEST_ASSERT_EQUAL(POWER_MODE_LOW_POWER, pm->GetPowerMode(pm));
    esp_pm_configure_fake.return_val = ESP_ERR_NOT_SUPPORTED;
    TEST_ASSERT_EQUAL(POWER_ERR_NOT_SUPPORTED, pm->SetPowerMode(pm, POWER_MODE_PERFORMANCE));
    unsigned calls = esp_pm_configure_fake.call_count;
    TEST_ASSERT_EQUAL(POWER_ERR_INVALID_ARG, pm->SetPowerMode(pm, (PowerMode_t)-1));
    TEST_ASSERT_EQUAL(POWER_ERR_INVALID_ARG, pm->SetPowerMode(pm, POWER_MODE_LIGHT_SLEEP));
    TEST_ASSERT_EQUAL(calls, esp_pm_configure_fake.call_count);
    TEST_ASSERT_EQUAL(POWER_MODE_LOW_POWER, pm->GetPowerMode(pm));
}

void test_PowerManager_LockLifecycleFailures(void) {
    IPowerManager *pm = PowerManager_GetInstance();
    powerManager.state = SERVICE_STATE_STARTED;
    TEST_ASSERT_EQUAL(POWER_OK, pm->AcquireTypedPowerLock(pm, "radio", POWER_LOCK_NO_LIGHT_SLEEP));
    TEST_ASSERT_EQUAL(ESP_PM_NO_LIGHT_SLEEP, esp_pm_lock_create_fake.arg0_val);
    TEST_ASSERT_EQUAL(POWER_ERR_BUSY, pm->base.Stop(&pm->base));
    TEST_ASSERT_EQUAL(SERVICE_STATE_STARTED, powerManager.state);
    TEST_ASSERT_EQUAL(POWER_ERR_BUSY, pm->base.Initialize(&pm->base, NULL));
    TEST_ASSERT_EQUAL(1, powerManager.activeLockCount);
    esp_pm_lock_delete_fake.return_val = ESP_FAIL;
    TEST_ASSERT_EQUAL(POWER_ERR_LOCK_FAILED, pm->ReleasePowerLock(pm, "radio"));
    TEST_ASSERT_EQUAL(0, powerManager.activeLockCount);
    TEST_ASSERT_EQUAL(POWER_ERR_LOCK_FAILED, pm->base.Stop(&pm->base));
    TEST_ASSERT_EQUAL(SERVICE_STATE_STARTED, powerManager.state);
    esp_pm_lock_delete_fake.return_val = ESP_OK;
    TEST_ASSERT_EQUAL(POWER_OK, pm->base.Stop(&pm->base));
    TEST_ASSERT_EQUAL(SERVICE_STATE_STOPPED, powerManager.state);
    TEST_ASSERT_EQUAL(POWER_ERR_INVALID_STATE, pm->AcquirePowerLock(pm, "stopped"));
    TEST_ASSERT_EQUAL(POWER_OK, pm->base.Initialize(&pm->base, NULL));
    esp_pm_lock_create_fake.custom_fake = NULL;
    esp_pm_lock_create_fake.return_val = ESP_ERR_NO_MEM;
    TEST_ASSERT_EQUAL(POWER_ERR_LOCK_FAILED, pm->AcquirePowerLock(pm, "allocation"));
    TEST_ASSERT_EQUAL(0, powerManager.activeLockCount);
    TEST_ASSERT_EQUAL_STRING("", powerManager.locks[0].name);
}

void test_PowerManager_NamedSDKLocks(void) {
    IPowerManager *pm = PowerManager_GetInstance();
    TEST_ASSERT_EQUAL(POWER_OK, pm->base.Initialize(&pm->base, NULL));
    char owner[] = "transfer";
    TEST_ASSERT_EQUAL(POWER_OK, pm->AcquireTypedPowerLock(pm, owner, POWER_LOCK_APB_MAX));
    owner[0] = 'X';
    TEST_ASSERT_EQUAL_STRING("transfer", powerManager.locks[0].name);
    TEST_ASSERT_EQUAL(ESP_PM_APB_FREQ_MAX, esp_pm_lock_create_fake.arg0_val);
    TEST_ASSERT_EQUAL(POWER_OK, pm->AcquireTypedPowerLock(pm, "transfer", POWER_LOCK_APB_MAX));
    TEST_ASSERT_EQUAL(1, esp_pm_lock_acquire_fake.call_count);
    TEST_ASSERT_EQUAL(2, powerManager.activeLockCount);
    TEST_ASSERT_EQUAL(POWER_ERR_INVALID_ARG, pm->AcquirePowerLock(pm, "transfer"));
    TEST_ASSERT_EQUAL(POWER_ERR_INVALID_ARG, pm->ReleasePowerLock(pm, "unknown"));
    TEST_ASSERT_EQUAL(POWER_OK, pm->ReleasePowerLock(pm, "transfer"));
    TEST_ASSERT_EQUAL(0, esp_pm_lock_release_fake.call_count);
    esp_pm_lock_release_fake.return_val = ESP_FAIL;
    TEST_ASSERT_EQUAL(POWER_ERR_LOCK_FAILED, pm->ReleasePowerLock(pm, "transfer"));
    TEST_ASSERT_EQUAL(1, powerManager.activeLockCount);
    esp_pm_lock_release_fake.return_val = ESP_OK;
    TEST_ASSERT_EQUAL(POWER_OK, pm->ReleasePowerLock(pm, "transfer"));
    TEST_ASSERT_EQUAL(0, powerManager.activeLockCount);
    TEST_ASSERT_EQUAL(1, esp_pm_lock_delete_fake.call_count);
    esp_pm_lock_acquire_fake.return_val = ESP_FAIL;
    TEST_ASSERT_EQUAL(POWER_ERR_LOCK_FAILED, pm->AcquirePowerLock(pm, "failed"));
    TEST_ASSERT_EQUAL(0, powerManager.activeLockCount);
    TEST_ASSERT_EQUAL(2, esp_pm_lock_delete_fake.call_count);
}

void test_PowerManager_ConsoleDurationValidation(void) {
    const char *invalid[] = {"", "-1", "+1", " 1", "10x", "4294967296", "99999999999999999999999"};
    for (unsigned i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        uint32_t unchanged = 123;
        TEST_ASSERT_FALSE(ParseMilliseconds(invalid[i], &unchanged));
        TEST_ASSERT_EQUAL(123, unchanged);
    }
    uint32_t duration;
    TEST_ASSERT_TRUE(ParseMilliseconds("0", &duration));
    TEST_ASSERT_EQUAL(0, duration);
    TEST_ASSERT_TRUE(ParseMilliseconds("4294967295", &duration));
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, duration);
}

void test_PowerManager_RegistrationFailureDoesNotApplySDKProfile(void) {
    IPowerManager *pm = PowerManager_GetInstance();
    setUp_PowerManager();
    TEST_ASSERT_EQUAL(POWER_OK, pm->base.Initialize(&pm->base, NULL));
    const esp_err_t errors[] = {ESP_ERR_NO_MEM, ESP_ERR_TIMEOUT};
    for (unsigned i = 0; i < sizeof(errors) / sizeof(errors[0]); ++i) {
        power_registration_result = errors[i];
        unsigned applied = esp_pm_configure_fake.call_count;
        TEST_ASSERT_EQUAL(errors[i], pm->base.Start(&pm->base));
        TEST_ASSERT_EQUAL(applied, esp_pm_configure_fake.call_count);
        TEST_ASSERT_EQUAL(SERVICE_STATE_INITIALIZED, powerManager.state);
        TEST_ASSERT_EQUAL(POWER_MODE_UNKNOWN, pm->GetPowerMode(pm));
    }
    power_registration_result = ESP_OK;
    TEST_ASSERT_EQUAL(POWER_OK, pm->base.Start(&pm->base));
}

void test_PowerManager_StartAppliesPersistedProfile(void) {
    IPowerManager *pm = PowerManager_GetInstance();
    setUp_PowerManager();
    TEST_ASSERT_EQUAL(POWER_OK, pm->base.Initialize(&pm->base, NULL));
    TEST_ASSERT_EQUAL(POWER_MODE_UNKNOWN, pm->GetPowerMode(pm));
    power_settings.system.power_profile = POWER_MODE_LOW_POWER;
    TEST_ASSERT_EQUAL(POWER_OK, pm->base.Start(&pm->base));
    TEST_ASSERT_EQUAL(40, appliedProfile.min_freq_mhz);
    TEST_ASSERT_EQUAL(80, appliedProfile.max_freq_mhz);
    TEST_ASSERT_FALSE(appliedProfile.light_sleep_enable);
    TEST_ASSERT_EQUAL(POWER_MODE_LOW_POWER, pm->GetPowerMode(pm));
    TEST_ASSERT_EQUAL(0, persist_calls);
    TEST_ASSERT_EQUAL(POWER_OK, pm->SelfTest(pm));
    TEST_ASSERT_EQUAL(0, persist_calls);
}

void test_PowerManager_StartFailureDoesNotClaimAppliedMode(void) {
    IPowerManager *pm = PowerManager_GetInstance();
    setUp_PowerManager();
    TEST_ASSERT_EQUAL(POWER_OK, pm->base.Initialize(&pm->base, NULL));
    esp_pm_configure_fake.custom_fake = NULL;
    esp_pm_configure_fake.return_val = ESP_FAIL;
    TEST_ASSERT_EQUAL(POWER_ERR_INVALID_STATE, pm->base.Start(&pm->base));
    TEST_ASSERT_EQUAL(SERVICE_STATE_INITIALIZED, powerManager.state);
    TEST_ASSERT_EQUAL(POWER_MODE_UNKNOWN, pm->GetPowerMode(pm));
    TEST_ASSERT_EQUAL(0, persist_calls);
    esp_pm_configure_fake.custom_fake = captureProfile;
    power_settings.system.power_profile = 255;
    TEST_ASSERT_EQUAL(POWER_ERR_INVALID_ARG, pm->base.Start(&pm->base));
    power_settings.system.power_profile = POWER_MODE_BALANCED;
    TEST_ASSERT_EQUAL(POWER_OK, pm->base.Start(&pm->base));
}

void test_PowerManager_PersistenceFailureRestoresSDKProfile(void) {
    IPowerManager *pm = start_restart_test();
    persist_result = ESP_FAIL;
    TEST_ASSERT_EQUAL(POWER_ERR_INVALID_STATE, pm->SetPowerMode(pm, POWER_MODE_LOW_POWER));
    TEST_ASSERT_EQUAL(POWER_MODE_BALANCED, pm->GetPowerMode(pm));
    TEST_ASSERT_EQUAL(POWER_MODE_BALANCED, power_settings.system.power_profile);
    TEST_ASSERT_EQUAL(80, appliedProfile.min_freq_mhz);
    TEST_ASSERT_EQUAL(160, appliedProfile.max_freq_mhz);
    TEST_ASSERT_EQUAL(SERVICE_STATE_STARTED, powerManager.state);
    persist_result = ESP_OK;
    TEST_ASSERT_EQUAL(POWER_OK, pm->SetPowerMode(pm, POWER_MODE_LOW_POWER));
    TEST_ASSERT_EQUAL(POWER_MODE_LOW_POWER, power_settings.system.power_profile);
}

static unsigned rollback_calls;
static esp_err_t reject_rollback(const void *config) {
    if (++rollback_calls == 2) return ESP_FAIL;
    return captureProfile(config);
}
void test_PowerManager_RollbackFailureMarksUnknownState(void) {
    IPowerManager *pm = start_restart_test();
    rollback_calls = 0;
    esp_pm_configure_fake.custom_fake = reject_rollback;
    persist_result = ESP_FAIL;
    TEST_ASSERT_EQUAL(POWER_ERR_INVALID_STATE, pm->SetPowerMode(pm, POWER_MODE_LOW_POWER));
    TEST_ASSERT_EQUAL(SERVICE_STATE_ERROR, powerManager.state);
    TEST_ASSERT_EQUAL(POWER_MODE_UNKNOWN, pm->GetPowerMode(pm));
    TEST_ASSERT_EQUAL(POWER_MODE_BALANCED, power_settings.system.power_profile);
    TEST_ASSERT_EQUAL(POWER_ERR_INVALID_STATE, pm->SetPowerMode(pm, POWER_MODE_BALANCED));
}

void test_PowerManager_LockSnapshotIsBoundedAndCopied(void) {
    IPowerManager *pm = PowerManager_GetInstance();
    TEST_ASSERT_EQUAL(POWER_OK, pm->base.Initialize(&pm->base, NULL));
    PowerLockSnapshot entries[2]; size_t count = 99;
    TEST_ASSERT_EQUAL(POWER_OK, pm->GetLockSnapshot(pm, NULL, 0, &count));
    TEST_ASSERT_EQUAL(0, count);
    TEST_ASSERT_EQUAL(POWER_OK, pm->AcquireTypedPowerLock(pm, "uart", POWER_LOCK_APB_MAX));
    TEST_ASSERT_EQUAL(POWER_OK, pm->AcquireTypedPowerLock(pm, "uart", POWER_LOCK_APB_MAX));
    TEST_ASSERT_EQUAL(POWER_OK, pm->AcquireTypedPowerLock(pm, "transfer", POWER_LOCK_CPU_MAX));
    memset(entries, 0x55, sizeof(entries));
    TEST_ASSERT_EQUAL(POWER_ERR_NO_MEM, pm->GetLockSnapshot(pm, entries, 1, &count));
    TEST_ASSERT_EQUAL(2, count); TEST_ASSERT_EQUAL(0x55, entries[0].name[0]);
    TEST_ASSERT_EQUAL(POWER_OK, pm->GetLockSnapshot(pm, entries, 2, &count));
    TEST_ASSERT_EQUAL_STRING("uart", entries[0].name);
    TEST_ASSERT_EQUAL(2, entries[0].references); TEST_ASSERT_EQUAL(POWER_LOCK_APB_MAX, entries[0].kind);
    TEST_ASSERT_EQUAL(POWER_OK, pm->ReleasePowerLock(pm, "uart"));
    TEST_ASSERT_EQUAL(2, entries[0].references);
    TEST_ASSERT_EQUAL(POWER_ERR_INVALID_ARG, pm->GetLockSnapshot(pm, NULL, 1, &count));
    TEST_ASSERT_EQUAL(POWER_ERR_INVALID_ARG, pm->GetLockSnapshot(pm, entries, 2, NULL));
    TEST_ASSERT_EQUAL(POWER_OK, pm->ReleasePowerLock(pm, "uart"));
    TEST_ASSERT_EQUAL(POWER_OK, pm->ReleasePowerLock(pm, "transfer"));
    TEST_ASSERT_EQUAL(POWER_OK, pm->GetLockSnapshot(pm, entries, 2, &count));
    TEST_ASSERT_EQUAL(0, count);
}

void test_PowerManager_OledFailureRestoresStoppedDependencies(void) {
    prepare_sleep_test(); oled_fail = true;
    uint32_t sleeps = powerManager.stats.totalSleeps;
    IPowerManager *pm = PowerManager_GetInstance();
    TEST_ASSERT_EQUAL(POWER_ERR_INVALID_STATE, pm->EnterDeepSleep(pm, 0));
    TEST_ASSERT_EQUAL_UINT(2, oled_power_calls);
    TEST_ASSERT_EQUAL_UINT(5, restore_count);
    for (unsigned i = 0; i < 5; ++i) TEST_ASSERT_EQUAL_UINT(4 - i, restore_order[i]);
    TEST_ASSERT_EQUAL_UINT(sleeps, powerManager.stats.totalSleeps);
    TEST_ASSERT_EQUAL_UINT(0, mocked_esp_deep_sleep_start_fake.call_count);
    mocked_ServiceLocator_GetService_fake.custom_fake = NULL;
}
void test_PowerManager_LightSleepErrorsDoNotClaimWakeOrSleep(void) {
    powerManager.state = SERVICE_STATE_INITIALIZED;
    IPowerManager *pm = PowerManager_GetInstance();
    uint32_t sleeps = powerManager.stats.totalSleeps, wakes = powerManager.stats.totalWakeups;
    RESET_FAKE(mocked_esp_light_sleep_start);
    mocked_esp_sleep_enable_timer_wakeup_fake.return_val = ESP_FAIL;
    TEST_ASSERT_EQUAL(POWER_ERR_INVALID_STATE, pm->EnterLightSleep(pm, 1000));
    TEST_ASSERT_EQUAL_UINT(0, mocked_esp_light_sleep_start_fake.call_count);
    mocked_esp_sleep_enable_timer_wakeup_fake.return_val = ESP_OK;
    mocked_esp_light_sleep_start_fake.return_val = ESP_FAIL;
    TEST_ASSERT_EQUAL(POWER_ERR_INVALID_STATE, pm->EnterLightSleep(pm, 1000));
    TEST_ASSERT_EQUAL_UINT(sleeps, powerManager.stats.totalSleeps);
    TEST_ASSERT_EQUAL_UINT(wakes, powerManager.stats.totalWakeups);
    mocked_esp_light_sleep_start_fake.return_val = ESP_OK;
    TEST_ASSERT_EQUAL(POWER_OK, pm->EnterLightSleep(pm, 1000));
    TEST_ASSERT_EQUAL_UINT(sleeps + 1, powerManager.stats.totalSleeps);
    TEST_ASSERT_EQUAL_UINT(wakes + 1, powerManager.stats.totalWakeups);
    TEST_ASSERT_EQUAL_INT32(INT32_MAX, SleepDurationMilliseconds(UINT64_MAX));
}

void test_PowerManager_ConcurrentSleepAndLifecycleAreRejected(void) {
    IPowerManager *pm = PowerManager_GetInstance();
    atomic_store(&powerManager.sleepPending, true);
    TEST_ASSERT_EQUAL(POWER_ERR_BUSY, pm->EnterLightSleep(pm, 1000));
    RESET_FAKE(mocked_esp_deep_sleep_start);
    TEST_ASSERT_EQUAL(POWER_ERR_BUSY, pm->EnterDeepSleep(pm, 0));
    TEST_ASSERT_EQUAL_UINT(0, mocked_esp_deep_sleep_start_fake.call_count);
    TEST_ASSERT_TRUE(atomic_load(&powerManager.sleepPending));
    TEST_ASSERT_EQUAL(POWER_ERR_BUSY, pm->base.Stop(&pm->base));
    TEST_ASSERT_EQUAL(POWER_ERR_BUSY, pm->base.Initialize(&pm->base, NULL));
    atomic_store(&powerManager.sleepPending, false);
}

void test_PowerManager_TimerCleanupFailureBlocksStaleIndefiniteSleepAndCanRetry(void) {
    IPowerManager *pm = PowerManager_GetInstance();
    powerManager.state = SERVICE_STATE_STARTED;
    RESET_FAKE(mocked_esp_sleep_disable_wakeup_source);
    RESET_FAKE(mocked_esp_sleep_enable_timer_wakeup);
    RESET_FAKE(mocked_esp_light_sleep_start);
    RESET_FAKE(mocked_esp_deep_sleep_start);
    TEST_ASSERT_EQUAL(POWER_ERR_INVALID_ARG, pm->EnterLightSleep(pm, 0));
    TEST_ASSERT_EQUAL_UINT(0, mocked_esp_sleep_enable_timer_wakeup_fake.call_count);
    mocked_esp_sleep_disable_wakeup_source_fake.return_val = ESP_FAIL;
    TEST_ASSERT_EQUAL(POWER_ERR_INVALID_STATE, pm->EnterLightSleep(pm, 1000));
    TEST_ASSERT_TRUE(powerManager.sleepTimerOwned);
    TEST_ASSERT_FALSE(atomic_load(&powerManager.sleepPending));
    TEST_ASSERT_EQUAL(POWER_ERR_INVALID_STATE, pm->EnterDeepSleep(pm, 0));
    TEST_ASSERT_EQUAL_UINT(0, mocked_esp_deep_sleep_start_fake.call_count);
    TEST_ASSERT_TRUE(powerManager.sleepTimerOwned);
    mocked_esp_sleep_disable_wakeup_source_fake.return_val = ESP_OK;
    TEST_ASSERT_EQUAL(POWER_OK, pm->EnterLightSleep(pm, 1000));
    TEST_ASSERT_FALSE(powerManager.sleepTimerOwned);
    TEST_ASSERT_EQUAL(ESP_SLEEP_WAKEUP_TIMER, mocked_esp_sleep_disable_wakeup_source_fake.arg0_val);
    TEST_ASSERT_EQUAL_UINT(4, mocked_esp_sleep_disable_wakeup_source_fake.call_count);
}

static int32_t reject_console_sleep(IPowerManager *self, uint64_t duration) {
    (void)self;
    TEST_ASSERT_EQUAL_UINT(3000, duration);
    return POWER_ERR_TIMEOUT;
}
static IService *console_sleep_service(const char *name) {
    static IPowerManager service = {.EnterDeepSleep = reject_console_sleep};
    return !strcmp(name, SVC_NAME_POWER_MANAGER) ? (IService *)&service : NULL;
}
void test_PowerManager_ConsolePropagatesDeepSleepPreparationFailure(void) {
    mocked_ServiceLocator_GetService_fake.custom_fake = console_sleep_service;
    char *args[] = {"power", "sleep", "3", "deep"};
    TEST_ASSERT_EQUAL_INT(-1, cmd_power(4, args));
}

static void test_MutexFailurePreservesStateAndReleasesSleepAdmission(void) {
    IPowerManager *pm = PowerManager_GetInstance();
    TEST_ASSERT_EQUAL(POWER_OK, pm->base.Initialize((IService *)pm, NULL));
    TEST_ASSERT_EQUAL(POWER_OK, pm->base.Start((IService *)pm));
    unsigned profiles = esp_pm_configure_fake.call_count;
    mocked_xSemaphoreTake_fake.return_val = pdFALSE;
    TEST_ASSERT_EQUAL(POWER_ERR_LOCK_FAILED, pm->SetPowerMode(pm, POWER_MODE_LOW_POWER));
    TEST_ASSERT_EQUAL(profiles, esp_pm_configure_fake.call_count);
    TEST_ASSERT_EQUAL(POWER_ERR_LOCK_FAILED, pm->EnterDeepSleep(pm, 1000));
    TEST_ASSERT_FALSE(atomic_load(&powerManager.sleepPending));
    TEST_ASSERT_EQUAL(SERVICE_STATE_STARTED, powerManager.state);
    TEST_ASSERT_EQUAL(POWER_MODE_UNKNOWN, pm->GetPowerMode(pm));
    mocked_xSemaphoreTake_fake.return_val = pdTRUE;
    TEST_ASSERT_EQUAL(POWER_OK, pm->SetPowerMode(pm, POWER_MODE_LOW_POWER));
}

static void test_ReinitializePreservesActiveProfileAndRetainedResources(void) {
    IPowerManager *pm = PowerManager_GetInstance();
    TEST_ASSERT_EQUAL(POWER_OK, pm->base.Initialize((IService *)pm, NULL));
    TEST_ASSERT_EQUAL(POWER_OK, pm->base.Start((IService *)pm));
    TEST_ASSERT_EQUAL(POWER_ERR_BUSY, pm->base.Initialize((IService *)pm, NULL));
    TEST_ASSERT_EQUAL(SERVICE_STATE_STARTED, powerManager.state);
    TEST_ASSERT_EQUAL(POWER_MODE_BALANCED, pm->GetPowerMode(pm));
    TEST_ASSERT_EQUAL(POWER_OK, pm->base.Stop((IService *)pm));
    powerManager.sleepTimerOwned = true;
    TEST_ASSERT_EQUAL(POWER_ERR_BUSY, pm->base.Initialize((IService *)pm, NULL));
    powerManager.sleepTimerOwned = false;
    powerManager.locks[0].handle = (esp_pm_lock_handle_t)1;
    TEST_ASSERT_EQUAL(POWER_ERR_BUSY, pm->base.Initialize((IService *)pm, NULL));
    powerManager.locks[0].handle = NULL;
    TEST_ASSERT_EQUAL(POWER_OK, pm->base.Initialize((IService *)pm, NULL));
}

static void test_StopRetainsFailedTimerCleanupForRetry(void) {
    IPowerManager *pm = PowerManager_GetInstance();
    TEST_ASSERT_EQUAL(POWER_OK, pm->base.Initialize((IService *)pm, NULL));
    TEST_ASSERT_EQUAL(POWER_OK, pm->base.Start((IService *)pm));
    powerManager.sleepTimerOwned = true;
    mocked_esp_sleep_disable_wakeup_source_fake.return_val = ESP_FAIL;
    TEST_ASSERT_EQUAL(POWER_ERR_INVALID_STATE, pm->base.Stop((IService *)pm));
    TEST_ASSERT_EQUAL(SERVICE_STATE_STARTED, powerManager.state);
    TEST_ASSERT_TRUE(powerManager.sleepTimerOwned);
    mocked_esp_sleep_disable_wakeup_source_fake.return_val = ESP_OK;
    TEST_ASSERT_EQUAL(POWER_OK, pm->base.Stop((IService *)pm));
    TEST_ASSERT_FALSE(powerManager.sleepTimerOwned);
    TEST_ASSERT_EQUAL(POWER_OK, pm->base.Initialize((IService *)pm, NULL));
}

static void test_InvalidWakeLevelPreservesConfiguration(void) {
    IPowerManager *pm = PowerManager_GetInstance();
    TEST_ASSERT_EQUAL(POWER_OK, pm->base.Initialize((IService *)pm, NULL));
    PowerManagerConfig original = powerManager.config;
    PowerManagerConfig invalid = original;
    invalid.rotaryWakeupLevel = 2;
    TEST_ASSERT_EQUAL(POWER_ERR_INVALID_ARG, pm->InitializeWithConfig(pm, invalid, NULL));
    TEST_ASSERT_EQUAL_MEMORY(&original, &powerManager.config, sizeof(original));
}

static void test_GpioWakeCleanupFailureRetainsOwnershipAndBlocksSleep(void) {
    IPowerManager *pm = PowerManager_GetInstance();
    TEST_ASSERT_EQUAL(POWER_OK, pm->base.Initialize((IService *)pm, NULL));
    powerManager.sleepGpioOwned = true;
    mocked_esp_sleep_disable_wakeup_source_fake.return_val = ESP_FAIL;
    TEST_ASSERT_EQUAL(POWER_ERR_INVALID_STATE, pm->EnterLightSleep(pm, 1000));
    TEST_ASSERT_TRUE(powerManager.sleepGpioOwned);
    TEST_ASSERT_FALSE(atomic_load(&powerManager.sleepPending));
    mocked_esp_sleep_disable_wakeup_source_fake.return_val = ESP_OK;
    TEST_ASSERT_EQUAL(ESP_OK, CleanupOwnedGpioWake());
    TEST_ASSERT_FALSE(powerManager.sleepGpioOwned);
    TEST_ASSERT_EQUAL(ESP_SLEEP_WAKEUP_GPIO, mocked_esp_sleep_disable_wakeup_source_fake.arg0_val);
}

static void test_AbortedDeepSleepRemovesOwnedGpioWakeTrigger(void) {
    IPowerManager *pm = PowerManager_GetInstance();
    prepare_sleep_test();
    provide_sleep_pins = true; stop_failed = true;
    powerManager.config.enableRotaryWakeup = true;
    RESET_FAKE(mocked_esp_deep_sleep_enable_gpio_wakeup);
    RESET_FAKE(mocked_esp_sleep_disable_wakeup_source);
    TEST_ASSERT_EQUAL(POWER_ERR_TIMEOUT, pm->EnterDeepSleep(pm, 0));
    TEST_ASSERT_EQUAL_UINT64(1ULL << 3, mocked_esp_deep_sleep_enable_gpio_wakeup_fake.arg0_val);
    TEST_ASSERT_EQUAL(1, mocked_esp_sleep_disable_wakeup_source_fake.call_count);
    TEST_ASSERT_EQUAL(ESP_SLEEP_WAKEUP_GPIO, mocked_esp_sleep_disable_wakeup_source_fake.arg0_val);
    TEST_ASSERT_FALSE(powerManager.sleepGpioOwned);
    TEST_ASSERT_EQUAL(4, restore_count);
    provide_sleep_pins = false;
}

static void test_GpioSdkFailureCleansPartialTriggerAndInvalidPinDoesNotOwnIt(void) {
    IPowerManager *pm = PowerManager_GetInstance();
    prepare_sleep_test(); provide_sleep_pins = true;
    powerManager.config.enableRotaryWakeup = true;
    RESET_FAKE(mocked_esp_deep_sleep_enable_gpio_wakeup);
    RESET_FAKE(mocked_esp_sleep_disable_wakeup_source);
    mocked_esp_deep_sleep_enable_gpio_wakeup_fake.return_val = ESP_FAIL;
    TEST_ASSERT_EQUAL(POWER_ERR_INVALID_STATE, pm->EnterDeepSleep(pm, 0));
    TEST_ASSERT_EQUAL(1, mocked_esp_sleep_disable_wakeup_source_fake.call_count);
    TEST_ASSERT_FALSE(powerManager.sleepGpioOwned);
    TEST_ASSERT_EQUAL(0, sleep_order);
    mocked_esp_sleep_is_valid_wakeup_gpio_fake.return_val = false;
    TEST_ASSERT_EQUAL(POWER_ERR_INVALID_STATE, pm->EnterDeepSleep(pm, 0));
    TEST_ASSERT_EQUAL(1, mocked_esp_deep_sleep_enable_gpio_wakeup_fake.call_count);
    TEST_ASSERT_EQUAL(1, mocked_esp_sleep_disable_wakeup_source_fake.call_count);
    TEST_ASSERT_FALSE(powerManager.sleepGpioOwned);
    provide_sleep_pins = false;
}

void test_PowerManager_IncompleteRollbackChangesAbortResultAndContinues(void) {
    prepare_sleep_test();stop_failed=true;restore_failed_index=4;
    IPowerManager *pm=PowerManager_GetInstance();uint32_t sleeps=powerManager.stats.totalSleeps;
    TEST_ASSERT_EQUAL(POWER_ERR_INVALID_STATE,pm->EnterDeepSleep(pm,0));
    TEST_ASSERT_EQUAL(4,restore_count);
    for (unsigned i=0;i<4;i++) TEST_ASSERT_EQUAL(4-i,restore_order[i]);
    TEST_ASSERT_EQUAL(0,mocked_esp_deep_sleep_start_fake.call_count);
    TEST_ASSERT_EQUAL(sleeps,powerManager.stats.totalSleeps);
    TEST_ASSERT_FALSE(atomic_load(&powerManager.sleepPending));
}
void test_PowerManager_OledAbortStillRestoresOtherDependenciesAfterFailure(void) {
    prepare_sleep_test();oled_fail=true;restore_failed_index=4;
    IPowerManager *pm=PowerManager_GetInstance();
    TEST_ASSERT_EQUAL(POWER_ERR_INVALID_STATE,pm->EnterDeepSleep(pm,0));
    TEST_ASSERT_EQUAL(5,restore_count);
    for (unsigned i=0;i<5;i++) TEST_ASSERT_EQUAL(4-i,restore_order[i]);
    TEST_ASSERT_EQUAL(2,oled_power_calls);
    TEST_ASSERT_EQUAL(0,mocked_esp_deep_sleep_start_fake.call_count);
    TEST_ASSERT_FALSE(atomic_load(&powerManager.sleepPending));
}

void test_PowerManager_LateLockFailureReportsIncompleteRollback(void) {
    prepare_sleep_test();restore_failed_index=4;
    BaseType_t admission[]={pdTRUE,pdFALSE};
    SET_RETURN_SEQ(mocked_xSemaphoreTake,admission,2);
    IPowerManager *pm=PowerManager_GetInstance();uint32_t sleeps=powerManager.stats.totalSleeps;
    TEST_ASSERT_EQUAL(POWER_ERR_INVALID_STATE,pm->EnterDeepSleep(pm,0));
    TEST_ASSERT_EQUAL(5,restore_count);TEST_ASSERT_EQUAL(2,oled_power_calls);
    TEST_ASSERT_EQUAL(0,mocked_esp_deep_sleep_start_fake.call_count);
    TEST_ASSERT_EQUAL(sleeps,powerManager.stats.totalSleeps);
    TEST_ASSERT_FALSE(atomic_load(&powerManager.sleepPending));
}

static ServiceState unsafe_sleep_state(IService *service) {
    (void)service;
    return SERVICE_STATE_ERROR;
}

void test_PowerManager_ErrorOrMissingDependencyStateCannotEnterSleep(void) {
    IPowerManager *pm = PowerManager_GetInstance();
    prepare_sleep_test();
    for (unsigned i = 0; i < 5; ++i) sleep_services[i].GetState = unsafe_sleep_state;
    TEST_ASSERT_EQUAL(POWER_ERR_TIMEOUT, pm->EnterDeepSleep(pm, 0));
    TEST_ASSERT_EQUAL(0, sleep_order);
    TEST_ASSERT_EQUAL(0, restore_count);
    TEST_ASSERT_EQUAL(0, mocked_esp_deep_sleep_start_fake.call_count);
    TEST_ASSERT_FALSE(atomic_load(&powerManager.sleepPending));
    for (unsigned i = 0; i < 5; ++i) sleep_services[i].GetState = NULL;
    TEST_ASSERT_EQUAL(POWER_ERR_TIMEOUT, pm->EnterDeepSleep(pm, 0));
    TEST_ASSERT_EQUAL(0, mocked_esp_deep_sleep_start_fake.call_count);
    TEST_ASSERT_EQUAL(0, sleep_order);
}

void test_PowerManager_AbortReportsRetainedGpioWakeCleanup(void) {
    prepare_sleep_test(); provide_sleep_pins = true; stop_failed = true;
    powerManager.config.enableRotaryWakeup = true;
    mocked_esp_sleep_disable_wakeup_source_fake.return_val = ESP_FAIL;
    IPowerManager *pm = PowerManager_GetInstance();
    TEST_ASSERT_EQUAL(POWER_ERR_INVALID_STATE, pm->EnterDeepSleep(pm, 0));
    TEST_ASSERT_TRUE(powerManager.sleepGpioOwned);
    TEST_ASSERT_EQUAL(4, restore_count);
    TEST_ASSERT_FALSE(atomic_load(&powerManager.sleepPending));
    TEST_ASSERT_EQUAL(0, mocked_esp_deep_sleep_start_fake.call_count);
    mocked_esp_sleep_disable_wakeup_source_fake.return_val = ESP_OK;
    TEST_ASSERT_EQUAL(ESP_OK, CleanupOwnedGpioWake());
    TEST_ASSERT_FALSE(powerManager.sleepGpioOwned);
}

void test_PowerManager_LateLockAbortReportsGpioCleanupFailure(void) {
    prepare_sleep_test(); provide_sleep_pins = true;
    powerManager.config.enableRotaryWakeup = true;
    mocked_esp_sleep_disable_wakeup_source_fake.return_val = ESP_FAIL;
    BaseType_t admission[] = {pdTRUE, pdFALSE};
    SET_RETURN_SEQ(mocked_xSemaphoreTake, admission, 2);
    IPowerManager *pm = PowerManager_GetInstance();
    TEST_ASSERT_EQUAL(POWER_ERR_INVALID_STATE, pm->EnterDeepSleep(pm, 0));
    TEST_ASSERT_EQUAL(5, restore_count);
    TEST_ASSERT_EQUAL(2, oled_power_calls);
    TEST_ASSERT_TRUE(powerManager.sleepGpioOwned);
    TEST_ASSERT_FALSE(atomic_load(&powerManager.sleepPending));
    TEST_ASSERT_EQUAL(0, mocked_esp_deep_sleep_start_fake.call_count);
}

void test_PowerManager_LateLockAbortReportsOledRestoreFailure(void) {
    prepare_sleep_test(); oled_restore_fail = true;
    BaseType_t admission[] = {pdTRUE, pdFALSE};
    SET_RETURN_SEQ(mocked_xSemaphoreTake, admission, 2);
    IPowerManager *pm = PowerManager_GetInstance();
    TEST_ASSERT_EQUAL(POWER_ERR_INVALID_STATE, pm->EnterDeepSleep(pm, 0));
    TEST_ASSERT_EQUAL(5, restore_count);
    TEST_ASSERT_EQUAL(2, oled_power_calls);
    TEST_ASSERT_FALSE(atomic_load(&powerManager.sleepPending));
    TEST_ASSERT_EQUAL(0, mocked_esp_deep_sleep_start_fake.call_count);
}

void test_PowerManager_UnsafeRtcDurationRejectedBeforeAdmission(void) {
    prepare_sleep_test();
    IPowerManager *pm = PowerManager_GetInstance();
    unsigned locks = mocked_xSemaphoreTake_fake.call_count;
    TEST_ASSERT_EQUAL(POWER_ERR_INVALID_ARG, pm->EnterDeepSleep(pm, POWER_SLEEP_MAX_DURATION_US + 1));
    TEST_ASSERT_EQUAL(POWER_ERR_INVALID_ARG, pm->EnterLightSleep(pm, UINT64_MAX));
    TEST_ASSERT_EQUAL(locks, mocked_xSemaphoreTake_fake.call_count);
    TEST_ASSERT_EQUAL(0, sleep_order);
    TEST_ASSERT_EQUAL(0, mocked_esp_deep_sleep_start_fake.call_count);
    TEST_ASSERT_EQUAL(0, mocked_esp_sleep_enable_timer_wakeup_fake.call_count);
    TEST_ASSERT_FALSE(atomic_load(&powerManager.sleepPending));
    TEST_ASSERT_EQUAL(POWER_OK, pm->EnterDeepSleep(pm, POWER_SLEEP_MAX_DURATION_US));
    TEST_ASSERT_TRUE(mocked_esp_deep_sleep_fake.arg0_val == POWER_SLEEP_MAX_DURATION_US);
}

void test_PowerManager_InjectedSdkReturnCannotHideGpioCleanupFailure(void) {
    prepare_sleep_test(); provide_sleep_pins = true;
    powerManager.config.enableRotaryWakeup = true;
    mocked_esp_sleep_disable_wakeup_source_fake.return_val = ESP_FAIL;
    IPowerManager *pm = PowerManager_GetInstance();
    TEST_ASSERT_EQUAL(POWER_ERR_INVALID_STATE, pm->EnterDeepSleep(pm, 0));
    TEST_ASSERT_EQUAL(1, mocked_esp_deep_sleep_start_fake.call_count);
    TEST_ASSERT_TRUE(powerManager.sleepGpioOwned);
    TEST_ASSERT_FALSE(atomic_load(&powerManager.sleepPending));
}

static bool reject_reset_mutation(IConfigurationManager *self) { (void)self; return false; }
void test_PowerFactoryGuardRejectsProfileSleepAndRestartWithoutSdkActions(void) {
    IPowerManager *pm = start_restart_test();
    power_config.IsMutationAllowed = reject_reset_mutation;
    mocked_ConfigurationManager_GetInstance_fake.return_val = &power_config;
    unsigned sdk = esp_pm_configure_fake.call_count;
    TEST_ASSERT_EQUAL(POWER_ERR_BUSY, pm->SetPowerMode(pm, POWER_MODE_BALANCED));
    TEST_ASSERT_EQUAL(POWER_ERR_BUSY, pm->RestartAsync(pm, 0));
    TEST_ASSERT_EQUAL(POWER_ERR_BUSY, pm->EnterLightSleep(pm, 1000));
    TEST_ASSERT_EQUAL(POWER_ERR_BUSY, pm->EnterDeepSleep(pm, 0));
    pm->Restart(pm);
    TEST_ASSERT_EQUAL(sdk, esp_pm_configure_fake.call_count);
    TEST_ASSERT_EQUAL(0, mocked_esp_restart_fake.call_count);
    TEST_ASSERT_EQUAL(0, mocked_esp_timer_start_once_fake.call_count);
    TEST_ASSERT_EQUAL(0, mocked_esp_deep_sleep_start_fake.call_count);
    TEST_ASSERT_EQUAL(0, mocked_esp_light_sleep_start_fake.call_count);
    TEST_ASSERT_FALSE(pm->IsTransitionPending(pm));
    atomic_store(&powerManager.sleepPending, true);
    TEST_ASSERT_TRUE(pm->IsTransitionPending(pm));
    atomic_store(&powerManager.sleepPending, false);
    atomic_store(&powerManager.restartPending, true);
    TEST_ASSERT_TRUE(pm->IsTransitionPending(pm));
    atomic_store(&powerManager.restartPending, false);
    power_config.IsMutationAllowed = NULL;
    mocked_ConfigurationManager_GetInstance_fake.return_val = NULL;
}
void test_PowerUnavailableOwnerCannotAdmitFactoryPreflight(void) {
    IPowerManager *pm = PowerManager_GetInstance();
    TEST_ASSERT_TRUE(pm->IsTransitionPending(pm));
    TEST_ASSERT_EQUAL(POWER_OK, pm->base.Initialize(&pm->base, NULL));
    TEST_ASSERT_TRUE(pm->IsTransitionPending(pm));
    TEST_ASSERT_EQUAL(POWER_OK, pm->base.Start(&pm->base));
    TEST_ASSERT_FALSE(pm->IsTransitionPending(pm));
    powerManager.state = SERVICE_STATE_ERROR;
    TEST_ASSERT_TRUE(pm->IsTransitionPending(pm));
    powerManager.state = SERVICE_STATE_STOPPED;
    TEST_ASSERT_TRUE(pm->IsTransitionPending(pm));
}

#include "../../Infrastructure/support/console_capture.h"
static int32_t cli_power_stats(IPowerManager *self,PowerStats_t *out) {
    (void)self; memset(out,0,sizeof(*out)); out->currentMode=POWER_MODE_BALANCED; return POWER_OK;
}
static const char *cli_power_reset(IPowerManager *self) { (void)self; return "SW_CPU"; }
static PowerWakeupSource_t cli_power_wake(IPowerManager *self) { (void)self; return (PowerWakeupSource_t)0; }
static int32_t cli_power_failed_selftest(IPowerManager *self) { (void)self; return POWER_ERR_TIMEOUT; }
static IService *cli_power_locator(const char *name) {
    static IPowerManager service={.GetStats=cli_power_stats,.GetResetReasonString=cli_power_reset,.GetWakeupCause=cli_power_wake,.SelfTest=cli_power_failed_selftest};
    return !strcmp(name,SVC_NAME_POWER_MANAGER) ? (IService *)&service : NULL;
}
void test_PowerManager_CLIValidatesAndSeparatesHumanMachineResponses(void) {
    mocked_ServiceLocator_GetService_fake.custom_fake=cli_power_locator;
    char tr[2048],en[2048]; char *human[]={"power","status"}; char *machine[]={"power","status","--machine"};
    const LocalizationCatalog *saved=Localization_Snapshot();
    TEST_ASSERT_NOT_EQUAL(0,cmd_power(0,NULL));
    char *bad[]={"power",NULL}; TEST_ASSERT_NOT_EQUAL(0,cmd_power(2,bad));
    char *extra[]={"power","status","extra"}; TEST_ASSERT_NOT_EQUAL(0,cmd_power(3,extra));
    TEST_ASSERT_TRUE(Localization_Apply("tr"));
    TEST_ASSERT_EQUAL(0,test_capture_console(cmd_power,2,human,tr,sizeof(tr)));
    TEST_ASSERT_NOT_NULL(strstr(tr,Localization_Text(Localization_Find("tr"),LOC_CLI_POWER_TITLE)));
    TEST_ASSERT_EQUAL(0,test_capture_console(cmd_power,3,machine,tr,sizeof(tr)));
    TEST_ASSERT_TRUE(Localization_Apply("en"));
    char *brief[]={"power"};
    TEST_ASSERT_EQUAL(0,test_capture_console(cmd_power,1,brief,en,sizeof(en)));
    TEST_ASSERT_NOT_NULL(strstr(en,"Current Mode"));
    TEST_ASSERT_NULL(strstr(en,"Power Locks"));
    TEST_ASSERT_NULL(strstr(en,"Reset Reason"));
    TEST_ASSERT_EQUAL(0,test_capture_console(cmd_power,2,human,en,sizeof(en)));
    TEST_ASSERT_NOT_NULL(strstr(en,"Power Manager Status"));
    TEST_ASSERT_EQUAL(0,test_capture_console(cmd_power,3,machine,en,sizeof(en)));
    TEST_ASSERT_EQUAL_STRING(tr,en);
    TEST_ASSERT_NOT_NULL(strstr(en,"type=power_status mode=1"));
    char *selftest[]={"power","selftest","--machine"};
    TEST_ASSERT_NOT_EQUAL(0,test_capture_console(cmd_power,3,selftest,en,sizeof(en)));
    TEST_ASSERT_NOT_NULL(strstr(en,"type=power_selftest result=failed"));
    TEST_ASSERT_TRUE(Localization_Apply(saved->code));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_PowerManager_CLIValidatesAndSeparatesHumanMachineResponses);
    RUN_TEST(test_PowerFactoryGuardRejectsProfileSleepAndRestartWithoutSdkActions);
    RUN_TEST(test_PowerUnavailableOwnerCannotAdmitFactoryPreflight);
    RUN_TEST(test_PowerManager_ErrorOrMissingDependencyStateCannotEnterSleep);
    RUN_TEST(test_PowerManager_AbortReportsRetainedGpioWakeCleanup);
    RUN_TEST(test_PowerManager_LateLockAbortReportsGpioCleanupFailure);
    RUN_TEST(test_PowerManager_LateLockAbortReportsOledRestoreFailure);
    RUN_TEST(test_PowerManager_UnsafeRtcDurationRejectedBeforeAdmission);
    RUN_TEST(test_PowerManager_InjectedSdkReturnCannotHideGpioCleanupFailure);

    RUN_TEST(test_GpioSdkFailureCleansPartialTriggerAndInvalidPinDoesNotOwnIt);
    RUN_TEST(test_AbortedDeepSleepRemovesOwnedGpioWakeTrigger);
    RUN_TEST(test_GpioWakeCleanupFailureRetainsOwnershipAndBlocksSleep);
    RUN_TEST(test_InvalidWakeLevelPreservesConfiguration);
    RUN_TEST(test_PowerManager_LateLockFailureReportsIncompleteRollback);
    RUN_TEST(test_PowerManager_IncompleteRollbackChangesAbortResultAndContinues);
    RUN_TEST(test_PowerManager_OledAbortStillRestoresOtherDependenciesAfterFailure);
    RUN_TEST(test_StopRetainsFailedTimerCleanupForRetry);
    RUN_TEST(test_ReinitializePreservesActiveProfileAndRetainedResources);
    RUN_TEST(test_MutexFailurePreservesStateAndReleasesSleepAdmission);
  RUN_TEST(test_PowerManager_LockSnapshotIsBoundedAndCopied);
  RUN_TEST(test_PowerManager_ProfileRollbackAndBounds);
  RUN_TEST(test_PowerManager_NamedSDKLocks);
  RUN_TEST(test_PowerManager_LockLifecycleFailures);
  RUN_TEST(test_PowerManager_ConsoleDurationValidation);
  RUN_TEST(test_PowerManager_Lifecycle);
  RUN_TEST(test_PowerManager_Restart);
  RUN_TEST(test_PowerManager_DeferredRestartFailureDoesNotRestart);
  RUN_TEST(test_PowerManager_DeferredRestartReusesTimerAndRejectsDuplicate);
  RUN_TEST(test_PowerManager_StopCancelsPendingRestartAndPropagatesFailure);
  RUN_TEST(test_PowerManager_DeferredRestartValidatesStateAndDelay);
  RUN_TEST(test_PowerManager_SleepModes);
  RUN_TEST(test_PowerManager_ModesAndLocks);
  RUN_TEST(test_PowerManager_Telemetry);
  RUN_TEST(test_PowerManager_Errors);
  RUN_TEST(test_PowerManager_SleepStopsProducersBeforeOutputs);
  RUN_TEST(test_PowerManager_ConsolePropagatesDeepSleepPreparationFailure);
  RUN_TEST(test_PowerManager_TimerCleanupFailureBlocksStaleIndefiniteSleepAndCanRetry);
  RUN_TEST(test_PowerManager_ConcurrentSleepAndLifecycleAreRejected);
  RUN_TEST(test_PowerManager_OledFailureRestoresStoppedDependencies);
  RUN_TEST(test_PowerManager_LightSleepErrorsDoNotClaimWakeOrSleep);
  RUN_TEST(test_PowerManager_SleepFailureStillStopsOutputs);
  RUN_TEST(test_PowerManager_MissingWakeupAbortsBeforeCleanup);
  RUN_TEST(test_PowerManager_StartAppliesPersistedProfile);
  RUN_TEST(test_PowerManager_StartFailureDoesNotClaimAppliedMode);
  RUN_TEST(test_PowerManager_PersistenceFailureRestoresSDKProfile);
  RUN_TEST(test_PowerManager_RollbackFailureMarksUnknownState);
  RUN_TEST(test_PowerManager_RegistrationFailureDoesNotApplySDKProfile);
  return UNITY_END();
}
