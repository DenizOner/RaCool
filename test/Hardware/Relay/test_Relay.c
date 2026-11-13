#ifdef RACOOL_TEST_NATIVE

#include "Hardware/Relay/IRelay.h"
#include "Hardware/Relay/RelayConfig.h"
#include "Management/ConfigurationManager/SystemConfigs.h"
#include "test_mocks.h"
#include "unity.h"
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

esp_err_t mock_esp_timer_create(const esp_timer_create_args_t *args, esp_timer_handle_t *out_handle) {
  if (out_handle) {
    *out_handle = (esp_timer_handle_t)0x5678;
  }
  return ESP_OK;
}

static IDebugConsole *fixture_console;
IDebugConsole *DebugConsole_GetModule(void) { return fixture_console; }

// Include all modular source files directly for native unit testing
#include "Hardware/Relay/RelayDriver.c"
#include "Hardware/Relay/RelayStats.c"
#include "Hardware/Relay/RelayConsoleCommands.c"
#include "Hardware/Relay/Relay.c"

static int mock_log_called = 0;
static int32_t mock_log(LogLevel l, const char *t, const char *m, ...) {
  mock_log_called++;
  return 0;
}
static ILogManager mock_logManager_inst = {.Log = mock_log};

void setUp(void) {
  Relay_GetModule();
  fixture_console = NULL;
  RESET_FAKE(mocked_gpio_config);
  RESET_FAKE(mocked_gpio_set_level);
  RESET_FAKE(mocked_gpio_get_level);
  RESET_FAKE(mocked_xSemaphoreCreateMutex);
  RESET_FAKE(mocked_esp_timer_create);
  RESET_FAKE(mocked_xSemaphoreTake);
  RESET_FAKE(mocked_xSemaphoreGive);
  RESET_FAKE(mocked_esp_timer_start_periodic);
  RESET_FAKE(mocked_esp_timer_stop);
  RESET_FAKE(mocked_esp_timer_get_time);
  RESET_FAKE(mocked_vTaskDelay);
  RESET_FAKE(mocked_gpio_reset_pin);
  mock_log_called = 0;

  mocked_xSemaphoreCreateMutex_fake.return_val = (SemaphoreHandle_t)0x1234;
  mocked_esp_timer_create_fake.custom_fake = mock_esp_timer_create;
  mocked_xSemaphoreTake_fake.return_val = pdTRUE;

  // Reset internal relay state & counters
  relayService.state = SERVICE_STATE_INITIALIZED;
  relayService.current_status = RELAY_STATE_CLOSED;
  relayService.current_mode = RELAY_MODE_MANUAL;
  relayService.relay_pin = (gpio_num_t)RELAY_IN_PIN;
  relayService.mutex = (SemaphoreHandle_t)0x1234;
  relayService.async_timer = (esp_timer_handle_t)0x5678;
  relayService.last_switch_time_us = 0;
  relayService.energized_start_time_us = 0;
  relayService.auto_off_pending = false;
  relayService.auto_off_deadline_us = 0;
  memset(&relayService.stats, 0, sizeof(RelayStats));
}

void tearDown(void) {}

void test_Relay_DurationValidation(void) {
  const char *invalid[] = {"", "-1", "+1", "1.5", "1ms", " 1",
                          "4294967296", "9999999999999999999999999"};
  uint32_t value = 77;
  for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
    TEST_ASSERT_FALSE(Relay_ParseDuration(invalid[i], UINT32_MAX, &value));
    TEST_ASSERT_EQUAL_UINT32(77, value);
  }
  TEST_ASSERT_FALSE(Relay_ParseDuration(NULL, UINT32_MAX, &value));
  TEST_ASSERT_FALSE(Relay_ParseDuration("1", UINT32_MAX, NULL));
  TEST_ASSERT_FALSE(Relay_ParseDuration("3600001", RELAY_MAX_PULSE_DURATION_MS, &value));
  TEST_ASSERT_TRUE(Relay_ParseDuration("4294967295", UINT32_MAX, &value));
  TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, value);
  TEST_ASSERT_TRUE(Relay_ParseDuration("0", RELAY_MAX_PULSE_DURATION_MS, &value));
  TEST_ASSERT_EQUAL_UINT32(0, value);
}

void test_Relay_CommandsRejectBeforeActuation(void) {
  char *badPulse[] = {"RELAY", "PULSE", "-1"};
  char *badMode[] = {"RELAY", "MODE", "AUTOO"};
  char *extra[] = {"RELAY", "ON", "unexpected"};
  TEST_ASSERT_EQUAL(1, cmd_relay_ctrl(3, badPulse));
  TEST_ASSERT_EQUAL(1, cmd_relay_ctrl(3, badMode));
  TEST_ASSERT_EQUAL(1, cmd_relay_ctrl(3, extra));
  TEST_ASSERT_EQUAL_UINT(0, mocked_gpio_set_level_fake.call_count);
  TEST_ASSERT_EQUAL(RELAY_MODE_MANUAL, relayService.current_mode);
  TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, Relay_SetMode(Relay_GetModule(), (RelayMode)99));
  TEST_ASSERT_EQUAL(RELAY_MODE_MANUAL, relayService.current_mode);
}

void test_Relay_CommandsPropagateDriverFailure(void) {
  char *on[] = {"RELAY", "ON"};
  mocked_gpio_set_level_fake.return_val = ESP_FAIL;
  relayService.state = SERVICE_STATE_STARTED;
  TEST_ASSERT_EQUAL(1, cmd_relay_ctrl(2, on));
  TEST_ASSERT_EQUAL(RELAY_STATE_CLOSED, relayService.current_status);
}

void test_Relay_TimerContentionNeverBlocks(void) {
  relayService.state = SERVICE_STATE_STARTED;
  relayService.auto_off_pending = true;
  mocked_xSemaphoreTake_fake.return_val = pdFALSE;
  Relay_TimerCallback(NULL);
  TEST_ASSERT_EQUAL_UINT(0, mocked_xSemaphoreTake_fake.arg1_val);
  TEST_ASSERT_EQUAL_UINT(0, mocked_gpio_set_level_fake.call_count);
  TEST_ASSERT_EQUAL_UINT(0, mocked_vTaskDelay_fake.call_count);
  TEST_ASSERT_TRUE(relayService.auto_off_pending);
}

void test_Relay_TimerDeadlineAndManualCancellation(void) {
  relayService.state = SERVICE_STATE_STARTED;
  relayService.current_status = RELAY_STATE_PULSING;
  relayService.auto_off_pending = true;
  relayService.auto_off_deadline_us = 200000;
  relayService.last_switch_time_us = 100000;
  mocked_esp_timer_get_time_fake.return_val = 199999;
  Relay_TimerCallback(NULL);
  TEST_ASSERT_EQUAL_UINT(0, mocked_gpio_set_level_fake.call_count);
  mocked_esp_timer_get_time_fake.return_val = 200000;
  Relay_TimerCallback(NULL);
  TEST_ASSERT_EQUAL_UINT(1, mocked_gpio_set_level_fake.call_count);
  TEST_ASSERT_EQUAL(RELAY_STATE_CLOSED, relayService.current_status);
  TEST_ASSERT_FALSE(relayService.auto_off_pending);
  TEST_ASSERT_EQUAL_UINT(0, mocked_vTaskDelay_fake.call_count);
  Relay_TimerCallback(NULL);
  TEST_ASSERT_EQUAL_UINT(1, mocked_gpio_set_level_fake.call_count);
  TEST_ASSERT_EQUAL(ESP_OK, Relay_Open_Internal(true));
  uint32_t calls = mocked_gpio_set_level_fake.call_count;
  Relay_TimerCallback(NULL);
  TEST_ASSERT_EQUAL_UINT(calls, mocked_gpio_set_level_fake.call_count);
  TEST_ASSERT_EQUAL(RELAY_STATE_OPEN, relayService.current_status);
}

void test_Relay_TimerDwellAndGPIOFailureRetry(void) {
  relayService.state = SERVICE_STATE_STARTED;
  relayService.current_status = RELAY_STATE_PULSING;
  relayService.auto_off_pending = true;
  relayService.auto_off_deadline_us = 100001;
  relayService.last_switch_time_us = 100000;
  mocked_esp_timer_get_time_fake.return_val = 149999;
  Relay_TimerCallback(NULL);
  TEST_ASSERT_EQUAL_UINT(0, mocked_gpio_set_level_fake.call_count);
  mocked_esp_timer_get_time_fake.return_val = 150000;
  mocked_gpio_set_level_fake.return_val = ESP_FAIL;
  Relay_TimerCallback(NULL);
  TEST_ASSERT_TRUE(relayService.auto_off_pending);
  TEST_ASSERT_EQUAL(RELAY_STATE_PULSING, relayService.current_status);
  mocked_gpio_set_level_fake.return_val = ESP_OK;
  Relay_TimerCallback(NULL);
  TEST_ASSERT_FALSE(relayService.auto_off_pending);
  TEST_ASSERT_EQUAL(RELAY_STATE_CLOSED, relayService.current_status);
  TEST_ASSERT_EQUAL_UINT(0, mocked_vTaskDelay_fake.call_count);
}

void test_Relay_TimerStopFailureRetainsOwnership(void) {
  relayService.state = SERVICE_STATE_STARTED;
  relayService.current_status = RELAY_STATE_PULSING;
  relayService.auto_off_pending = true;
  mocked_esp_timer_get_time_fake.return_val = 100000;
  mocked_esp_timer_stop_fake.return_val = ESP_FAIL;
  Relay_TimerCallback(NULL);
  TEST_ASSERT_TRUE(relayService.auto_off_pending);
  TEST_ASSERT_EQUAL(RELAY_STATE_CLOSED, relayService.current_status);
  uint32_t calls = mocked_gpio_set_level_fake.call_count;
  mocked_esp_timer_get_time_fake.return_val = 200000;
  Relay_TimerCallback(NULL);
  TEST_ASSERT_EQUAL_UINT(calls, mocked_gpio_set_level_fake.call_count);
  TEST_ASSERT_EQUAL(ESP_FAIL, Relay_Open_Internal(true));
  TEST_ASSERT_EQUAL_UINT(calls, mocked_gpio_set_level_fake.call_count);
  mocked_esp_timer_stop_fake.return_val = ESP_OK;
  Relay_TimerCallback(NULL);
  TEST_ASSERT_FALSE(relayService.auto_off_pending);
}

void test_Relay_SchedulingFailureAndBounds(void) {
  relayService.state = SERVICE_STATE_STARTED;
  mocked_esp_timer_start_periodic_fake.return_val = ESP_FAIL;
  TEST_ASSERT_EQUAL(ESP_FAIL, Relay_PulseAsync(Relay_GetModule(), 100));
  TEST_ASSERT_FALSE(relayService.auto_off_pending);
  TEST_ASSERT_EQUAL(RELAY_STATE_CLOSED, relayService.current_status);
  TEST_ASSERT_EQUAL_UINT(2, mocked_gpio_set_level_fake.call_count);
  TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, Relay_PulseAsync(Relay_GetModule(), RELAY_MAX_PULSE_DURATION_MS + 1));
  TEST_ASSERT_EQUAL_UINT(2, mocked_gpio_set_level_fake.call_count);
  mocked_esp_timer_start_periodic_fake.return_val = ESP_OK;
  mocked_esp_timer_get_time_fake.return_val = 100000;
  TEST_ASSERT_EQUAL(ESP_OK, Relay_SetAutoOff(Relay_GetModule(), UINT32_MAX));
  TEST_ASSERT_TRUE(relayService.auto_off_deadline_us == 100000ULL + (uint64_t)UINT32_MAX * 1000ULL);
}

void test_Relay_LifecycleRejectsSDKFailures(void) {
  IService *service = &Relay_GetModule()->base;
  mocked_gpio_reset_pin_fake.return_val = ESP_FAIL;
  TEST_ASSERT_EQUAL(-1, service->Start(service));
  TEST_ASSERT_EQUAL(SERVICE_STATE_INITIALIZED, relayService.state);
  mocked_gpio_reset_pin_fake.return_val = ESP_OK;
  mocked_gpio_set_level_fake.return_val = ESP_FAIL;
  TEST_ASSERT_EQUAL(-1, service->Start(service));
  mocked_gpio_set_level_fake.return_val = ESP_OK;
  TEST_ASSERT_EQUAL(0, service->Start(service));
  relayService.current_status = RELAY_STATE_OPEN;
  mocked_gpio_set_level_fake.return_val = ESP_FAIL;
  TEST_ASSERT_EQUAL(-1, service->Stop(service));
  TEST_ASSERT_EQUAL(SERVICE_STATE_STARTED, relayService.state);
  TEST_ASSERT_EQUAL(RELAY_STATE_OPEN, relayService.current_status);
  mocked_gpio_set_level_fake.return_val = ESP_OK;
  TEST_ASSERT_EQUAL(0, service->Stop(service));
  TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, Relay_Open_Internal(true));
}

void test_Relay_InitFailureAndStatsLockFailure(void) {
  IService *service = &Relay_GetModule()->base;
  relayService.state = SERVICE_STATE_UNINITIALIZED;
  relayService.async_timer = NULL;
  mocked_esp_timer_create_fake.custom_fake = NULL;
  mocked_esp_timer_create_fake.return_val = ESP_FAIL;
  TEST_ASSERT_EQUAL(-1, service->Initialize(service, NULL));
  TEST_ASSERT_EQUAL(SERVICE_STATE_UNINITIALIZED, relayService.state);
  mocked_xSemaphoreTake_fake.return_val = pdFALSE;
  RelayStats stats = {.total_switches = 777};
  TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT, Relay_GetStats(Relay_GetModule(), &stats));
  TEST_ASSERT_EQUAL_UINT32(777, stats.total_switches);
  TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT, Relay_ResetStats(Relay_GetModule()));
  TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT, Relay_Open_Internal(true));
}

void test_Relay_RepeatedCommandsPreserveActualEdgesAndTime(void) {
  relayService.state = SERVICE_STATE_STARTED;
  relayService.current_status = RELAY_STATE_PULSING;
  relayService.auto_off_pending = true;
  relayService.last_switch_time_us = 1000000;
  relayService.energized_start_time_us = 1000000;
  relayService.stats.total_switches = 1;
  mocked_esp_timer_get_time_fake.return_val = 2000000;
  TEST_ASSERT_EQUAL(ESP_OK, Relay_Open_Internal(true));
  TEST_ASSERT_FALSE(relayService.auto_off_pending);
  TEST_ASSERT_EQUAL(RELAY_STATE_OPEN, relayService.current_status);
  TEST_ASSERT_EQUAL_UINT(0, mocked_gpio_set_level_fake.call_count);
  TEST_ASSERT_EQUAL_UINT(0, mocked_vTaskDelay_fake.call_count);
  TEST_ASSERT_TRUE(relayService.energized_start_time_us == 1000000);
  RelayStats stats;
  TEST_ASSERT_EQUAL(ESP_OK, Relay_GetStats(Relay_GetModule(), &stats));
  TEST_ASSERT_TRUE(stats.total_active_time_ms == 1000);
  TEST_ASSERT_EQUAL_UINT32(1, stats.total_switches);
  TEST_ASSERT_EQUAL(ESP_OK, Relay_Close_Internal(true));
  TEST_ASSERT_EQUAL(ESP_OK, Relay_Close_Internal(true));
  TEST_ASSERT_EQUAL_UINT(1, mocked_gpio_set_level_fake.call_count);
  TEST_ASSERT_TRUE(relayService.stats.total_active_time_ms == 1000);
}

void test_Relay_InfoRoutesWithoutActuation(void) {
  char *info[] = {"RELAY", "INFO"};
  TEST_ASSERT_EQUAL(0, cmd_relay_ctrl(2, info));
  TEST_ASSERT_EQUAL_UINT(0, mocked_gpio_set_level_fake.call_count);
  TEST_ASSERT_EQUAL_UINT(1, mocked_gpio_get_level_fake.call_count);
  mocked_xSemaphoreTake_fake.return_val = pdFALSE;
  TEST_ASSERT_EQUAL(1, cmd_relay_ctrl(2, info));
  TEST_ASSERT_EQUAL_UINT(1, mocked_gpio_get_level_fake.call_count);
}

static unsigned registration_calls;
static esp_err_t fixture_register(IDebugConsole *self, const esp_console_cmd_t *cmd,
                                  CommandCategory category) {
  (void)self; (void)category;
  registration_calls++;
  TEST_ASSERT_NULL(strchr(cmd->command, ' '));
  if (registration_calls == 1) TEST_ASSERT_EQUAL_STRING("relay_info", cmd->command);
  else TEST_ASSERT_EQUAL_STRING("RELAY", cmd->command);
  return registration_calls == 2 ? ESP_FAIL : ESP_OK;
}
void test_Relay_CommandRegistrationRetry(void) {
  IDebugConsole console = {.RegisterCommandWithCategory = fixture_register};
  fixture_console = &console;
  registration_calls = 0;
  IService *service = &Relay_GetModule()->base;
  TEST_ASSERT_EQUAL(-1, service->Initialize(service, NULL));
  TEST_ASSERT_EQUAL_UINT(2, registration_calls);
  TEST_ASSERT_EQUAL(0, service->Initialize(service, NULL));
  TEST_ASSERT_EQUAL_UINT(3, registration_calls);
  TEST_ASSERT_EQUAL(0, service->Initialize(service, NULL));
  TEST_ASSERT_EQUAL_UINT(3, registration_calls);
}

void test_Relay_InitMutexAndPinBounds(void) {
  IService *service = &Relay_GetModule()->base;
  relayService.state = SERVICE_STATE_UNINITIALIZED;
  relayService.mutex = NULL;
  mocked_xSemaphoreCreateMutex_fake.return_val = NULL;
  TEST_ASSERT_EQUAL(-1, service->Initialize(service, NULL));
  TEST_ASSERT_EQUAL_UINT(0, mocked_esp_timer_create_fake.call_count);
  relayService.mutex = (SemaphoreHandle_t)0x1234;
  relayService.state = SERVICE_STATE_INITIALIZED;
  relayService.relay_pin = (gpio_num_t)GPIO_NUM_MAX;
  TEST_ASSERT_EQUAL(-1, service->Start(service));
  TEST_ASSERT_EQUAL_UINT(0, mocked_gpio_config_fake.call_count);
}

/**
 * @brief Test Lifecycle (Init, Start, Health, Stop)
 */
void test_Relay_Lifecycle(void) {
  IRelay *relay = Relay_GetModule();
  IService *service = &(relay->base);
  relayService.state = SERVICE_STATE_UNINITIALIZED;

  TEST_ASSERT_EQUAL(0, service->Initialize(service, NULL));
  relayService.relay_pin = (gpio_num_t)RELAY_IN_PIN;
  TEST_ASSERT_EQUAL(SERVICE_STATE_INITIALIZED, relayService.state);

  TEST_ASSERT_EQUAL(0, service->Start(service));
  TEST_ASSERT_EQUAL(SERVICE_STATE_STARTED, relayService.state);

  TEST_ASSERT_EQUAL(0, service->CheckHealth(service));

  TEST_ASSERT_EQUAL(0, service->Stop(service));
  TEST_ASSERT_EQUAL(SERVICE_STATE_STOPPED, relayService.state);
}

/**
 * @brief Test Basic Relay Operations (Open, Close, Toggle, Status)
 */
void test_Relay_BasicOps(void) {
  IRelay *relay = Relay_GetModule();
  IService *service = &(relay->base);
  service->Initialize(service, NULL);
  service->Start(service);

  // Open Relay
  TEST_ASSERT_EQUAL(ESP_OK, relay->Open(relay));
  TEST_ASSERT_TRUE(relay->IsOpen(relay));
  TEST_ASSERT_EQUAL(RELAY_STATE_OPEN, relay->GetStatus(relay));

  // Close Relay
  TEST_ASSERT_EQUAL(ESP_OK, relay->Close(relay));
  TEST_ASSERT_FALSE(relay->IsOpen(relay));
  TEST_ASSERT_EQUAL(RELAY_STATE_CLOSED, relay->GetStatus(relay));

  // Toggle Relay (CLOSED -> OPEN)
  TEST_ASSERT_EQUAL(ESP_OK, relay->Toggle(relay));
  TEST_ASSERT_TRUE(relay->IsOpen(relay));

  // Toggle Relay (OPEN -> CLOSED)
  TEST_ASSERT_EQUAL(ESP_OK, relay->Toggle(relay));
  TEST_ASSERT_FALSE(relay->IsOpen(relay));
}

/**
 * @brief Test Asynchronous Control (Pulse, Auto-Off)
 */
void test_Relay_AsyncOps(void) {
  IRelay *relay = Relay_GetModule();
  IService *service = &(relay->base);
  service->Initialize(service, NULL);
  service->Start(service);

  // Pulse Async
  TEST_ASSERT_EQUAL(ESP_OK, relay->PulseAsync(relay, 500));
  TEST_ASSERT_TRUE(relay->IsOpen(relay));
  TEST_ASSERT_EQUAL(RELAY_STATE_PULSING, relay->GetStatus(relay));

  // Auto Off
  TEST_ASSERT_EQUAL(ESP_OK, relay->SetAutoOff(relay, 1000));
  TEST_ASSERT_TRUE(relay->IsOpen(relay));
}

/**
 * @brief Test Relay Operational Statistics and Anti-Short Cycle Protection
 */
void test_Relay_StatsAndProtections(void) {
  IRelay *relay = Relay_GetModule();
  IService *service = &(relay->base);
  service->Initialize(service, NULL);
  service->Start(service);

  RelayStats stats;
  TEST_ASSERT_EQUAL(ESP_OK, relay->GetStats(relay, &stats));
  TEST_ASSERT_EQUAL(0, stats.total_switches);

  // Perform Open & Close cycles
  relay->Open(relay);
  relay->Close(relay);

  TEST_ASSERT_EQUAL(ESP_OK, relay->GetStats(relay, &stats));
  TEST_ASSERT_EQUAL(1, stats.total_switches);

  // Test Stats Reset
  TEST_ASSERT_EQUAL(ESP_OK, relay->ResetStats(relay));
  TEST_ASSERT_EQUAL(ESP_OK, relay->GetStats(relay, &stats));
  TEST_ASSERT_EQUAL(0, stats.total_switches);

  // Test Null arg check
  TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, relay->GetStats(relay, NULL));
}

/**
 * @brief Test Relay Operating Modes and Self-Test
 */
void test_Relay_ModesAndDiagnostics(void) {
  IRelay *relay = Relay_GetModule();
  IService *service = &(relay->base);
  service->Initialize(service, NULL);
  service->Start(service);

  // Mode changes
  TEST_ASSERT_EQUAL(ESP_OK, relay->SetMode(relay, RELAY_MODE_AUTOMATIC));
  TEST_ASSERT_EQUAL(RELAY_MODE_AUTOMATIC, relay->GetMode(relay));

  TEST_ASSERT_EQUAL(ESP_OK, relay->SetMode(relay, RELAY_MODE_MANUAL));
  TEST_ASSERT_EQUAL(RELAY_MODE_MANUAL, relay->GetMode(relay));

  // Run Self-Test
  TEST_ASSERT_EQUAL(ESP_OK, relay->RunSelfTest(relay));
}

void test_Relay_CheckedStateRejectsUnavailableAndPreservesOutput(void) {
  IRelay *relay = Relay_GetModule();
  bool state = true;
  relayService.state = SERVICE_STATE_STARTED;
  relayService.current_status = RELAY_STATE_CLOSED;
  mocked_xSemaphoreTake_fake.return_val = pdFALSE;
  TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT, relay->ReadState(relay, &state));
  TEST_ASSERT_TRUE(state);
  TEST_ASSERT_EQUAL(0, mocked_xSemaphoreTake_fake.arg1_val);
  mocked_xSemaphoreTake_fake.return_val = pdTRUE;
  TEST_ASSERT_EQUAL(ESP_OK, relay->ReadState(relay, &state));
  TEST_ASSERT_FALSE(state);
  relayService.current_status = RELAY_STATE_PULSING;
  TEST_ASSERT_EQUAL(ESP_OK, relay->ReadState(relay, &state));
  TEST_ASSERT_TRUE(state);
  relayService.state = SERVICE_STATE_STOPPED;
  TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, relay->ReadState(relay, &state));
  TEST_ASSERT_TRUE(state);
  TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, relay->ReadState(relay, NULL));
}

#include "../../Infrastructure/support/console_capture.h"
void test_Relay_CLIStatusIsLanguageIndependentAndRejectsInvalidInput(void) {
  const LocalizationCatalog *saved=Localization_Snapshot(); char tr[2048],en[2048];
  char *human[]={"relay_info"}; char *machine[]={"relay_info","--machine"};
  TEST_ASSERT_NOT_EQUAL(0,cmd_relay_info(0,NULL));
  TEST_ASSERT_NOT_EQUAL(0,cmd_relay_ctrl(2,NULL));
  char *bad[]={"RELAY",NULL}; TEST_ASSERT_NOT_EQUAL(0,cmd_relay_ctrl(2,bad));
  TEST_ASSERT_TRUE(Localization_Apply("tr"));
  TEST_ASSERT_EQUAL(0,test_capture_console(cmd_relay_info,1,human,tr,sizeof(tr)));
  TEST_ASSERT_NOT_NULL(strstr(tr,Localization_Text(Localization_Find("tr"),LOC_CLI_RELAY_TITLE)));
  TEST_ASSERT_EQUAL(0,test_capture_console(cmd_relay_info,2,machine,tr,sizeof(tr)));
  TEST_ASSERT_TRUE(Localization_Apply("en"));
  TEST_ASSERT_EQUAL(0,test_capture_console(cmd_relay_info,1,human,en,sizeof(en)));
  TEST_ASSERT_NOT_NULL(strstr(en,"Relay Hardware Status"));
  TEST_ASSERT_EQUAL(0,test_capture_console(cmd_relay_info,2,machine,en,sizeof(en)));
  TEST_ASSERT_EQUAL_STRING(tr,en);
  TEST_ASSERT_NOT_NULL(strstr(en,"type=relay_status state=0 mode=0"));
  relayService.current_status=(RelayState)99;
  TEST_ASSERT_NOT_EQUAL(0,test_capture_console(cmd_relay_info,2,machine,en,sizeof(en)));
  TEST_ASSERT_NULL(strstr(en,"RACOOL_CLI"));
  relayService.current_status=RELAY_STATE_CLOSED;
  TEST_ASSERT_TRUE(Localization_Apply(saved->code));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_Relay_CLIStatusIsLanguageIndependentAndRejectsInvalidInput);
  RUN_TEST(test_Relay_CheckedStateRejectsUnavailableAndPreservesOutput);
  RUN_TEST(test_Relay_Lifecycle);
  RUN_TEST(test_Relay_DurationValidation);
  RUN_TEST(test_Relay_CommandsRejectBeforeActuation);
  RUN_TEST(test_Relay_CommandsPropagateDriverFailure);
  RUN_TEST(test_Relay_TimerContentionNeverBlocks);
  RUN_TEST(test_Relay_TimerDeadlineAndManualCancellation);
  RUN_TEST(test_Relay_TimerDwellAndGPIOFailureRetry);
  RUN_TEST(test_Relay_TimerStopFailureRetainsOwnership);
  RUN_TEST(test_Relay_SchedulingFailureAndBounds);
  RUN_TEST(test_Relay_LifecycleRejectsSDKFailures);
  RUN_TEST(test_Relay_InitFailureAndStatsLockFailure);
  RUN_TEST(test_Relay_RepeatedCommandsPreserveActualEdgesAndTime);
  RUN_TEST(test_Relay_InfoRoutesWithoutActuation);
  RUN_TEST(test_Relay_CommandRegistrationRetry);
  RUN_TEST(test_Relay_InitMutexAndPinBounds);
  RUN_TEST(test_Relay_BasicOps);
  RUN_TEST(test_Relay_AsyncOps);
  RUN_TEST(test_Relay_StatsAndProtections);
  RUN_TEST(test_Relay_ModesAndDiagnostics);
  return UNITY_END();
}

#endif // RACOOL_TEST_NATIVE
