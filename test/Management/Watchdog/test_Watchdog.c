/**
 * @file test_Watchdog.c
 * @brief Unit and native-integration tests for the modernised Watchdog module.
 *
 * Test coverage includes:
 *  - Full service lifecycle (init → start → stop state transitions).
 *  - Manual and automatic feed statistics.
 *  - Task registration with duplicate-rejection and overflow protection.
 *  - Null-handle rejection on RemoveTask.
 *  - GetTimeout, IsActive, GetRegisteredTaskCount queries.
 *  - GetStats copy semantics and NULL-guard.
 *  - UpdateConfig with valid and invalid parameters.
 *  - Config validation (warning_threshold vs timeout boundary).
 *  - SelfTest deadlock-safety and counter consistency.
 *  - CheckHealth staleness detection (manual-feed mode).
 *  - All seven CLI sub-commands (status, feed, selftest, config, health,
 *    tasks, reset).
 *  - Idempotent initialisation.
 *
 * @author RaCool DeepMind Team
 * @date 2026-08-01
 */

#include "Management/Watchdog/IWatchdog.h"
#include "Management/Watchdog/WatchdogConfig.h"
#include "Management/Watchdog/WatchdogConsoleCommands.h"
#include "test_mocks.h"
#include "unity.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Include implementation units directly for native white-box testing. */
#include "Management/Watchdog/WatchdogTaskRegistry.c"
#include "Management/Watchdog/WatchdogTimers.c"
#include "Management/Watchdog/Watchdog.c"
#include "Management/Watchdog/WatchdogConsoleCommands.c"

/* -----------------------------------------------------------------------
 * Unity boilerplate
 * --------------------------------------------------------------------- */

void setUp(void)    {}
void tearDown(void) {}

/* -----------------------------------------------------------------------
 * ServiceLocator custom fake
 * --------------------------------------------------------------------- */

/**
 * @brief Custom ServiceLocator fake that returns the Watchdog singleton when
 *        SVC_NAME_WATCHDOG is queried.
 */
static IService *custom_ServiceLocator_GetService(const char *name) {
    if (name != NULL && strcmp(name, SVC_NAME_WATCHDOG) == 0) {
        return (IService *)Watchdog_GetModule();
    }
    return NULL;
}

/* -----------------------------------------------------------------------
 * Per-test fixture helper
 * --------------------------------------------------------------------- */

/**
 * @brief Resets the watchdog singleton to a pristine state before each test.
 *
 * Clears all FFF call-count fakes and re-initialises every field of the
 * internal WatchdogServiceImpl so tests are fully isolated.
 */
static esp_err_t add_auto_user(const char *name, esp_task_wdt_user_handle_t *user) {
    TEST_ASSERT_EQUAL_STRING("racool_auto_feed", name);
    *user = (esp_task_wdt_user_handle_t)0x123;
    return ESP_OK;
}

static esp_err_t create_watchdog_timer(const esp_timer_create_args_t *args, esp_timer_handle_t *timer) {
    TEST_ASSERT_NOT_NULL(args->callback);
    *timer = (esp_timer_handle_t)(uintptr_t)(strcmp(args->name, "wdt_warning") == 0 ? 0x100 : 0x200);
    return ESP_OK;
}

static void setUp_Watchdog(void) {
    watchdogService.warningTimerFailed = false;
    watchdogService.autoFeedClosing = false;
    watchdogService.autoFeedActive = 0;
    watchdogService.autoFeedLastMs = 0;
    RESET_FAKE(mocked_esp_timer_create);
    RESET_FAKE(mocked_esp_timer_start_once);
    RESET_FAKE(mocked_esp_timer_start_periodic);
    RESET_FAKE(mocked_esp_timer_stop);
    RESET_FAKE(mocked_esp_timer_delete);
    mocked_esp_timer_create_fake.custom_fake = create_watchdog_timer;
    watchdogService.autoFeedUser = NULL;
    RESET_FAKE(mocked_xTaskGetCurrentTaskHandle);
    RESET_FAKE(mocked_esp_task_wdt_add_user);
    RESET_FAKE(mocked_esp_task_wdt_reset_user);
    RESET_FAKE(mocked_esp_task_wdt_delete_user);
    mocked_esp_task_wdt_add_user_fake.custom_fake = add_auto_user;
    /* Reset the singleton to UNINITIALIZED. */
    watchdogService.state               = SERVICE_STATE_UNINITIALIZED;
    watchdogService.config              = WatchdogConfig_GetDefault();
    memset(&watchdogService.stats, 0, sizeof(WatchdogStats_t));
    WatchdogTaskRegistry_Init(&watchdogService.registry, watchdogService.config.max_tasks);
    WatchdogTimers_Init(&watchdogService.timers);
    watchdogService.isActive            = false;
    watchdogService.mutex               = NULL;

    RESET_FAKE(mocked_esp_task_wdt_reconfigure);
    RESET_FAKE(mocked_esp_task_wdt_init);
    RESET_FAKE(mocked_esp_task_wdt_reset);
    RESET_FAKE(mocked_esp_task_wdt_add);
    RESET_FAKE(mocked_esp_task_wdt_delete);
    RESET_FAKE(mocked_xSemaphoreCreateMutex);
    RESET_FAKE(mocked_xSemaphoreTake);

    mocked_xSemaphoreCreateMutex_fake.return_val    = (SemaphoreHandle_t)1;
    mocked_xSemaphoreTake_fake.return_val           = 1;
    mocked_esp_task_wdt_reconfigure_fake.return_val = ESP_OK;
    mocked_esp_task_wdt_init_fake.return_val        = ESP_OK;
    mocked_esp_task_wdt_reset_fake.return_val       = ESP_OK;
    mocked_esp_task_wdt_add_fake.return_val         = ESP_OK;
    mocked_esp_task_wdt_delete_fake.return_val      = ESP_OK;

    /* Timer fakes — all succeed by default. */
    mocked_esp_timer_create_fake.return_val         = ESP_OK;
    mocked_esp_timer_start_once_fake.return_val     = ESP_OK;
    mocked_esp_timer_start_periodic_fake.return_val = ESP_OK;
    mocked_esp_timer_stop_fake.return_val           = ESP_OK;
    mocked_esp_timer_delete_fake.return_val         = ESP_OK;

    /* ServiceLocator fake. */
    mocked_ServiceLocator_GetService_fake.custom_fake =
        custom_ServiceLocator_GetService;
}

/* -----------------------------------------------------------------------
 * Test cases
 * --------------------------------------------------------------------- */

/**
 * @brief Verifies the full service lifecycle: UNINITIALIZED → INITIALIZED →
 *        STARTED → STOPPED, with the correct timeout loaded from config.
 */
void test_Watchdog_Lifecycle(void) {
    setUp_Watchdog();
    IWatchdog *wd       = Watchdog_GetModule();
    IService  *service  = &(wd->base);

    TEST_ASSERT_EQUAL(0, service->Initialize(service, NULL));
    TEST_ASSERT_EQUAL(SERVICE_STATE_INITIALIZED, watchdogService.state);
    TEST_ASSERT_EQUAL(WATCHDOG_DEFAULT_TIMEOUT_MS, watchdogService.config.timeout_ms);

    TEST_ASSERT_EQUAL(0, service->Start(service));
    TEST_ASSERT_EQUAL(SERVICE_STATE_STARTED, watchdogService.state);

    TEST_ASSERT_EQUAL(0, service->CheckHealth(service));

    TEST_ASSERT_EQUAL(0, service->Stop(service));
    TEST_ASSERT_EQUAL(SERVICE_STATE_STOPPED, watchdogService.state);
}

/**
 * @brief Verifies idempotent initialisation — a second Initialize() call
 *        must be a no-op (return 0, no additional TWDT API calls).
 */
void test_Watchdog_Initialize_Idempotent(void) {
    setUp_Watchdog();
    IWatchdog *wd      = Watchdog_GetModule();
    IService  *service = &(wd->base);

    TEST_ASSERT_EQUAL(0, service->Initialize(service, NULL));
    uint32_t first_call_count = mocked_esp_task_wdt_reconfigure_fake.call_count +
                                mocked_esp_task_wdt_init_fake.call_count;

    /* Second call — should short-circuit without touching the TWDT. */
    TEST_ASSERT_EQUAL(0, service->Initialize(service, NULL));
    uint32_t second_call_count = mocked_esp_task_wdt_reconfigure_fake.call_count +
                                 mocked_esp_task_wdt_init_fake.call_count;

    TEST_ASSERT_EQUAL(first_call_count, second_call_count);
}

/**
 * @brief Verifies manual feed mechanics: not-started ignores the call;
 *        started increments the counter and calls esp_task_wdt_reset.
 */
void test_Watchdog_Feed(void) {
    setUp_Watchdog();
    IWatchdog *wd = Watchdog_GetModule();

    /* Feed is a no-op before the service starts. */
    wd->Feed(wd);
    TEST_ASSERT_EQUAL(0, mocked_esp_task_wdt_reset_fake.call_count);
    TEST_ASSERT_EQUAL(0, watchdogService.stats.totalFeeds);

    /* Feed works after start. */
    watchdogService.state = SERVICE_STATE_STARTED;
    wd->Feed(wd);
    TEST_ASSERT_EQUAL(1, mocked_esp_task_wdt_reset_fake.call_count);
    TEST_ASSERT_EQUAL(1, watchdogService.stats.totalFeeds);
}

/**
 * @brief Verifies that a failed esp_task_wdt_reset does not increment the
 *        feed counter (error path).
 */
void test_Watchdog_Feed_ErrorPath(void) {
    setUp_Watchdog();
    watchdogService.state               = SERVICE_STATE_STARTED;
    mocked_esp_task_wdt_reset_fake.return_val = ESP_FAIL;

    IWatchdog *wd = Watchdog_GetModule();
    wd->Feed(wd);

    TEST_ASSERT_EQUAL(1, mocked_esp_task_wdt_reset_fake.call_count);
    TEST_ASSERT_EQUAL(0, watchdogService.stats.totalFeeds); /* Must NOT increment. */
}

/**
 * @brief Verifies task addition and removal, duplicate rejection, and
 *        NULL-handle rejection.
 */
void test_Watchdog_TaskManagement(void) {
    setUp_Watchdog();
    IWatchdog    *wd       = Watchdog_GetModule();
    TaskHandle_t  fakeTask = (TaskHandle_t)0x1234;

    /* AddTask must fail before the service starts. */
    TEST_ASSERT_EQUAL(-1, wd->AddTask(wd, fakeTask, 1000));

    /* AddTask succeeds after start. */
    watchdogService.state = SERVICE_STATE_STARTED;
    TEST_ASSERT_EQUAL(0, wd->AddTask(wd, fakeTask, 1000));
    TEST_ASSERT_EQUAL(1, mocked_esp_task_wdt_add_fake.call_count);
    TEST_ASSERT_EQUAL(1, watchdogService.stats.totalTasksAdded);
    TEST_ASSERT_EQUAL(1, watchdogService.stats.activeTaskCount);
    TEST_ASSERT_EQUAL(1, (int)watchdogService.isActive);

    /* Duplicate handle must be rejected. */
    TEST_ASSERT_EQUAL(-1, wd->AddTask(wd, fakeTask, 0));
    TEST_ASSERT_EQUAL(1, mocked_esp_task_wdt_add_fake.call_count); /* No extra call. */

    /* RemoveTask succeeds for a registered handle. */
    TEST_ASSERT_EQUAL(0, wd->RemoveTask(wd, fakeTask));
    TEST_ASSERT_EQUAL(1, mocked_esp_task_wdt_delete_fake.call_count);
    TEST_ASSERT_EQUAL(1, watchdogService.stats.totalTasksRemoved);
    TEST_ASSERT_EQUAL(0, watchdogService.stats.activeTaskCount);
    TEST_ASSERT_EQUAL(0, (int)watchdogService.isActive);

    /* RemoveTask must fail for a NULL handle. */
    TEST_ASSERT_EQUAL(-1, wd->RemoveTask(wd, NULL));

    /* RemoveTask must fail for a handle that is not registered. */
    TEST_ASSERT_EQUAL(-1, wd->RemoveTask(wd, fakeTask));
}

/**
 * @brief Verifies that AddTask rejects registration when max_tasks is reached.
 */
void test_Watchdog_MaxTasksLimit(void) {
    setUp_Watchdog();
    watchdogService.state          = SERVICE_STATE_STARTED;
    watchdogService.config.max_tasks = 2;
    watchdogService.registry.maxTasks = 2;

    IWatchdog *wd = Watchdog_GetModule();

    TEST_ASSERT_EQUAL(0, wd->AddTask(wd, (void *)0x1001, 0));
    TEST_ASSERT_EQUAL(0, wd->AddTask(wd, (void *)0x1002, 0));
    /* Third handle exceeds the limit. */
    TEST_ASSERT_EQUAL(-1, wd->AddTask(wd, (void *)0x1003, 0));
    TEST_ASSERT_EQUAL(2, (int)watchdogService.stats.activeTaskCount);
}

/**
 * @brief Verifies IsActive, GetTimeout, and GetRegisteredTaskCount accessors.
 */
void test_Watchdog_Queries(void) {
    setUp_Watchdog();
    IWatchdog *wd = Watchdog_GetModule();

    watchdogService.isActive            = true;
    watchdogService.config.timeout_ms   = 3000;
    watchdogService.registry.registeredTaskCount = 4;
    watchdogService.stats.activeTaskCount = 4;

    TEST_ASSERT_TRUE(wd->IsActive(wd));
    TEST_ASSERT_EQUAL(3000, wd->GetTimeout(wd));
    TEST_ASSERT_EQUAL(4, wd->GetRegisteredTaskCount(wd));
}

/**
 * @brief Verifies GetStats copy semantics and NULL-guard.
 */
void test_Watchdog_Stats(void) {
    setUp_Watchdog();
    IWatchdog *wd = Watchdog_GetModule();

    /* NULL outStats must return -1. */
    TEST_ASSERT_EQUAL(-1, wd->GetStats(wd, NULL));

    watchdogService.stats.totalFeeds      = 42;
    watchdogService.stats.activeTaskCount = 3;
    watchdogService.stats.totalTimeouts   = 5;
    watchdogService.stats.totalAutoFeeds  = 10;

    WatchdogStats_t stats;
    TEST_ASSERT_EQUAL(0, wd->GetStats(wd, &stats));
    TEST_ASSERT_EQUAL(42, stats.totalFeeds);
    TEST_ASSERT_EQUAL(3,  stats.activeTaskCount);
    TEST_ASSERT_EQUAL(5,  stats.totalTimeouts);
    TEST_ASSERT_EQUAL(10, stats.totalAutoFeeds);
}

/**
 * @brief Verifies UpdateConfig success path and invalid-config rejection.
 */
void test_Watchdog_UpdateConfig(void) {
    setUp_Watchdog();
    IWatchdog *wd = Watchdog_GetModule();

    WatchdogConfig cfg = WatchdogConfig_GetDefault();
    cfg.timeout_ms = 8000;
    /* Ensure warning threshold is still valid. */
    cfg.warning_threshold_ms = 4000;

    TEST_ASSERT_EQUAL(0, wd->UpdateConfig(wd, &cfg));
    TEST_ASSERT_EQUAL(8000, wd->GetTimeout(wd));

    /* timeout_ms below minimum must be rejected. */
    cfg.timeout_ms = 50;
    TEST_ASSERT_EQUAL(-1, wd->UpdateConfig(wd, &cfg));

    /* NULL config must be rejected. */
    TEST_ASSERT_EQUAL(-1, wd->UpdateConfig(wd, NULL));
}

/**
 * @brief Verifies that WatchdogConfig_IsValid rejects configurations where
 *        warning_threshold_ms >= timeout_ms.
 */
void test_Watchdog_ConfigValidation_Warning(void) {
    WatchdogConfig cfg = WatchdogConfig_GetDefault();

    /* Equal values are invalid (threshold must be strictly less). */
    cfg.warning_threshold_ms = cfg.timeout_ms;
    TEST_ASSERT_FALSE(WatchdogConfig_IsValid(&cfg));

    /* Threshold exceeds timeout — also invalid. */
    cfg.warning_threshold_ms = cfg.timeout_ms + 1;
    TEST_ASSERT_FALSE(WatchdogConfig_IsValid(&cfg));

    /* Zero threshold — invalid. */
    cfg.warning_threshold_ms = 0;
    TEST_ASSERT_FALSE(WatchdogConfig_IsValid(&cfg));

    /* Valid combination. */
    cfg.warning_threshold_ms = cfg.timeout_ms - 1;
    TEST_ASSERT_TRUE(WatchdogConfig_IsValid(&cfg));
}

/**
 * @brief Verifies SelfTest passes in INITIALIZED and STARTED states and
 *        returns an error in UNINITIALIZED state.
 *
 *        Also verifies that SelfTest is deadlock-safe (i.e., the internal
 *        mutex is released before calling Feed()/GetStats() — if a deadlock
 *        occurred the test would hang and be caught by the timeout).
 */
void test_Watchdog_SelfTest(void) {
    setUp_Watchdog();
    IWatchdog *wd = Watchdog_GetModule();

    /* Uninitialized — must fail. */
    TEST_ASSERT_EQUAL(-1, wd->SelfTest(wd));

    /* Initialized (but not started) — passes without calling Feed(). */
    TEST_ASSERT_EQUAL(0, wd->base.Initialize(&wd->base, NULL));
    TEST_ASSERT_EQUAL(0, wd->SelfTest(wd));

    /* Started — Feed() is called; counter must increment. */
    TEST_ASSERT_EQUAL(0, wd->base.Start(&wd->base));
    uint32_t feeds_before = watchdogService.stats.totalFeeds;
    TEST_ASSERT_EQUAL(0, wd->SelfTest(wd));
    TEST_ASSERT_GREATER_THAN(feeds_before, watchdogService.stats.totalFeeds);
}

/**
 * @brief Verifies CheckHealth returns -2 when the last manual feed is older
 *        than warning_threshold_ms (stale-feed detection).
 */
void test_Watchdog_CheckHealth_LastFeed(void) {
    setUp_Watchdog();
    IWatchdog *wd      = Watchdog_GetModule();
    IService  *service = &(wd->base);

    watchdogService.state                    = SERVICE_STATE_STARTED;
    watchdogService.config.auto_feed_enabled = false;
    watchdogService.config.warning_threshold_ms = 3000;

    /* Simulate a very old feed: esp_timer_get_time returns 0 by default in
     * the mock, so set lastFeedTimeMs to a value that looks "ancient". */
    watchdogService.stats.lastFeedTimeMs = 1; /* 1 ms (much older than threshold). */
    /* mock returns 0 for esp_timer_get_time, so now_ms = 0, age = -1 wraps. */
    /* Override: set lastFeedTimeMs such that age > threshold when now_ms = 0. */
    /* With uint64 arithmetic: age = 0 - 1 = UINT64_MAX — definitely > threshold. */

    int32_t health = service->CheckHealth(service);
    TEST_ASSERT_EQUAL(-2, health);
}

/**
 * @brief Verifies all seven CLI sub-commands execute without crashing.
 */
void test_Watchdog_ConsoleCommands(void) {
    setUp_Watchdog();
    IWatchdog *wd = Watchdog_GetModule();

    /* Need a started service so that feed/selftest work. */
    wd->base.Initialize(&(wd->base), NULL);
    wd->base.Start(&(wd->base));

    /* status */
    char *argv_status[] = {"watchdog", "status"};
    TEST_ASSERT_EQUAL(0, cmd_watchdog(2, argv_status));

    /* feed */
    char *argv_feed[] = {"watchdog", "feed"};
    TEST_ASSERT_EQUAL(0, cmd_watchdog(2, argv_feed));

    /* selftest */
    char *argv_selftest[] = {"watchdog", "selftest"};
    TEST_ASSERT_EQUAL(0, cmd_watchdog(2, argv_selftest));

    /* config — get */
    char *argv_config_get[] = {"watchdog", "config"};
    TEST_ASSERT_EQUAL(0, cmd_watchdog(2, argv_config_get));

    /* config — set valid */
    char *argv_config_set[] = {"watchdog", "config", "6000"};
    TEST_ASSERT_EQUAL(0, cmd_watchdog(3, argv_config_set));
    TEST_ASSERT_EQUAL(6000, wd->GetTimeout(wd));

    /* health */
    char *argv_health[] = {"watchdog", "health"};
    TEST_ASSERT_EQUAL(0, cmd_watchdog(2, argv_health));

    /* tasks */
    char *argv_tasks[] = {"watchdog", "tasks"};
    TEST_ASSERT_EQUAL(0, cmd_watchdog(2, argv_tasks));

    /* reset (Stop + Start cycle) */
    char *argv_reset[] = {"watchdog", "reset"};
    TEST_ASSERT_EQUAL(0, cmd_watchdog(2, argv_reset));
    TEST_ASSERT_EQUAL(SERVICE_STATE_STARTED, watchdogService.state);
}

/**
 * @brief Verifies that an unknown sub-command returns -1.
 */
void test_Watchdog_ConsoleCommands_Unknown(void) {
    setUp_Watchdog();
    char *argv_bad[] = {"watchdog", "bogus"};
    TEST_ASSERT_EQUAL(-1, cmd_watchdog(2, argv_bad));
}

/**
 * @brief Verifies config sub-command rejects non-numeric and zero inputs.
 */
void test_Watchdog_ConsoleCommands_ConfigInvalid(void) {
    setUp_Watchdog();
    IWatchdog *wd = Watchdog_GetModule();
    wd->base.Initialize(&(wd->base), NULL);
    wd->base.Start(&(wd->base));

    /* Non-numeric argument. */
    char *argv_bad[] = {"watchdog", "config", "abc"};
    TEST_ASSERT_EQUAL(-1, cmd_watchdog(3, argv_bad));

    /* Zero timeout. */
    char *argv_zero[] = {"watchdog", "config", "0"};
    TEST_ASSERT_EQUAL(-1, cmd_watchdog(3, argv_zero));
}

/* -----------------------------------------------------------------------
 * Entry points
 * --------------------------------------------------------------------- */

void test_Watchdog_AutomaticFeedOwnsUserUntilStop(void) {
    setUp_Watchdog();
    IWatchdog *wd = Watchdog_GetModule();
    TEST_ASSERT_EQUAL(0, wd->base.Initialize(&wd->base, NULL));
    TEST_ASSERT_EQUAL(0, wd->base.Start(&wd->base));
    TEST_ASSERT_NOT_NULL(watchdogService.autoFeedUser);
    Watchdog_AutoFeedCallback(NULL);
    TEST_ASSERT_EQUAL(1, mocked_esp_task_wdt_reset_user_fake.call_count);
    TEST_ASSERT_EQUAL(1, watchdogService.stats.totalAutoFeeds);
    TEST_ASSERT_EQUAL(0, mocked_esp_task_wdt_reset_fake.call_count);
    TEST_ASSERT_EQUAL(0, mocked_esp_task_wdt_add_fake.call_count);
    TEST_ASSERT_EQUAL(0, wd->base.Stop(&wd->base));
    TEST_ASSERT_EQUAL(1, mocked_esp_task_wdt_delete_user_fake.call_count);
    TEST_ASSERT_NULL(watchdogService.autoFeedUser);
    Watchdog_AutoFeedCallback(NULL);
    TEST_ASSERT_EQUAL(1, mocked_esp_task_wdt_reset_user_fake.call_count);
}
void test_Watchdog_FailedAutomaticFeedPreservesEvidence(void) {
    setUp_Watchdog();
    watchdogService.mutex = (SemaphoreHandle_t)1;
    watchdogService.state = SERVICE_STATE_STARTED;
    watchdogService.autoFeedUser = (esp_task_wdt_user_handle_t)0x123;
    watchdogService.stats.lastFeedTimeMs = 42;
    watchdogService.timers.warningTimer = (esp_timer_handle_t)1;
    unsigned arms = mocked_esp_timer_start_once_fake.call_count;
    mocked_esp_task_wdt_reset_user_fake.return_val = ESP_FAIL;
    Watchdog_AutoFeedCallback(NULL);
    TEST_ASSERT_EQUAL(0, watchdogService.stats.totalAutoFeeds);
    TEST_ASSERT_EQUAL(42, watchdogService.stats.lastFeedTimeMs);
    TEST_ASSERT_EQUAL(arms, mocked_esp_timer_start_once_fake.call_count);
    mocked_xSemaphoreTake_fake.return_val = 0;
    Watchdog_AutoFeedCallback(NULL);
    TEST_ASSERT_EQUAL(2, mocked_esp_task_wdt_reset_user_fake.call_count);
    TEST_ASSERT_EQUAL(0, watchdogService.stats.totalAutoFeeds);
    TEST_ASSERT_EQUAL(42, watchdogService.stats.lastFeedTimeMs);
    TEST_ASSERT_EQUAL(arms, mocked_esp_timer_start_once_fake.call_count);
}
void test_Watchdog_AutomaticUserFailuresAreNotSuccess(void) {
    setUp_Watchdog();
    IWatchdog *wd = Watchdog_GetModule();
    TEST_ASSERT_EQUAL(0, wd->base.Initialize(&wd->base, NULL));
    mocked_esp_task_wdt_add_user_fake.custom_fake = NULL;
    mocked_esp_task_wdt_add_user_fake.return_val = ESP_FAIL;
    TEST_ASSERT_EQUAL(-1, wd->base.Start(&wd->base));
    TEST_ASSERT_EQUAL(SERVICE_STATE_INITIALIZED, watchdogService.state);
    mocked_esp_task_wdt_add_user_fake.custom_fake = add_auto_user;
    TEST_ASSERT_EQUAL(0, wd->base.Start(&wd->base));
    mocked_esp_task_wdt_delete_user_fake.return_val = ESP_FAIL;
    TEST_ASSERT_EQUAL(-1, wd->base.Stop(&wd->base));
    TEST_ASSERT_EQUAL(SERVICE_STATE_STARTED, watchdogService.state);
    TEST_ASSERT_NOT_NULL(watchdogService.autoFeedUser);
}
void test_Watchdog_AutoFeedDisableReleasesOwnedUser(void) {
    setUp_Watchdog();
    IWatchdog *wd = Watchdog_GetModule();
    TEST_ASSERT_EQUAL(0, wd->base.Initialize(&wd->base, NULL));
    TEST_ASSERT_EQUAL(0, wd->base.Start(&wd->base));
    WatchdogConfig cfg = watchdogService.config;
    cfg.auto_feed_enabled = false;
    TEST_ASSERT_EQUAL(0, wd->UpdateConfig(wd, &cfg));
    TEST_ASSERT_NULL(watchdogService.autoFeedUser);
    Watchdog_AutoFeedCallback(NULL);
    TEST_ASSERT_EQUAL(0, mocked_esp_task_wdt_reset_user_fake.call_count);
}

void test_Watchdog_TimerFailureReleasesUser(void) {
    setUp_Watchdog();
    IWatchdog *wd = Watchdog_GetModule();
    TEST_ASSERT_EQUAL(0, wd->base.Initialize(&wd->base, NULL));
    mocked_esp_timer_start_periodic_fake.return_val = ESP_FAIL;
    TEST_ASSERT_EQUAL(-1, wd->base.Start(&wd->base));
    TEST_ASSERT_NULL(watchdogService.autoFeedUser);
    TEST_ASSERT_EQUAL(1, mocked_esp_task_wdt_delete_user_fake.call_count);
    TEST_ASSERT_EQUAL(SERVICE_STATE_INITIALIZED, watchdogService.state);
    mocked_esp_task_wdt_delete_user_fake.return_val = ESP_FAIL;
    TEST_ASSERT_EQUAL(-1, wd->base.Start(&wd->base));
    TEST_ASSERT_NOT_NULL(watchdogService.autoFeedUser);
    mocked_esp_task_wdt_delete_user_fake.return_val = ESP_OK;
    TEST_ASSERT_EQUAL(0, wd->base.Stop(&wd->base));
    TEST_ASSERT_NULL(watchdogService.autoFeedUser);
}
void test_Watchdog_WarningCallbackDoesNotBlockOrRunAfterStop(void) {
    setUp_Watchdog();
    watchdogService.mutex = (SemaphoreHandle_t)1;
    watchdogService.state = SERVICE_STATE_STOPPED;
    Watchdog_WarningTimerCallback(NULL);
    TEST_ASSERT_EQUAL(0, watchdogService.stats.totalWarnings);
    watchdogService.state = SERVICE_STATE_STARTED;
    mocked_xSemaphoreTake_fake.return_val = 0;
    Watchdog_WarningTimerCallback(NULL);
    TEST_ASSERT_EQUAL(0, watchdogService.stats.totalWarnings);
    TEST_ASSERT_EQUAL(0, mocked_xSemaphoreTake_fake.arg1_val);
    mocked_xSemaphoreTake_fake.return_val = 1;
    Watchdog_WarningTimerCallback(NULL);
    TEST_ASSERT_EQUAL(1, watchdogService.stats.totalWarnings);
}

void test_Watchdog_RestartPreservesRegisteredTaskActivity(void) {
    setUp_Watchdog();
    IWatchdog *wd = Watchdog_GetModule();
    TEST_ASSERT_EQUAL(0, wd->base.Initialize(&wd->base, NULL));
    TEST_ASSERT_EQUAL(0, wd->base.Start(&wd->base));
    TEST_ASSERT_EQUAL(0, wd->AddTask(wd, (void *)0x456, 1000));
    TEST_ASSERT_TRUE(wd->IsActive(wd));
    TEST_ASSERT_EQUAL(0, wd->base.Stop(&wd->base));
    TEST_ASSERT_FALSE(wd->IsActive(wd));
    TEST_ASSERT_EQUAL(0, wd->base.Start(&wd->base));
    TEST_ASSERT_TRUE(wd->IsActive(wd));
    TEST_ASSERT_EQUAL(1, wd->GetRegisteredTaskCount(wd));
}


void test_Watchdog_RemoveFailurePreservesRegistration(void) {
    setUp_Watchdog();
    IWatchdog *wd = Watchdog_GetModule();
    watchdogService.state = SERVICE_STATE_STARTED;
    void *task = (void *)0x1234;
    TEST_ASSERT_EQUAL(0, wd->AddTask(wd, task, 0));
    mocked_esp_task_wdt_delete_fake.return_val = ESP_FAIL;
    TEST_ASSERT_EQUAL(-1, wd->RemoveTask(wd, task));
    TEST_ASSERT_EQUAL(1, wd->GetRegisteredTaskCount(wd));
    TEST_ASSERT_EQUAL(1, watchdogService.stats.activeTaskCount);
    TEST_ASSERT_EQUAL(0, watchdogService.stats.totalTasksRemoved);
    TEST_ASSERT_TRUE(wd->IsActive(wd));
    mocked_esp_task_wdt_delete_fake.return_val = ESP_OK;
    TEST_ASSERT_EQUAL(0, wd->RemoveTask(wd, task));
    TEST_ASSERT_EQUAL(0, wd->GetRegisteredTaskCount(wd));
    TEST_ASSERT_EQUAL(1, watchdogService.stats.totalTasksRemoved);
}
void test_Watchdog_CurrentTaskUsesConcreteIdentity(void) {
    setUp_Watchdog();
    IWatchdog *wd = Watchdog_GetModule();
    watchdogService.state = SERVICE_STATE_STARTED;
    mocked_xTaskGetCurrentTaskHandle_fake.return_val = (TaskHandle_t)0x1234;
    TEST_ASSERT_EQUAL(0, wd->AddTask(wd, NULL, 0));
    TEST_ASSERT_EQUAL_PTR((void *)0x1234, mocked_esp_task_wdt_add_fake.arg0_val);
    TEST_ASSERT_EQUAL(-1, wd->AddTask(wd, (void *)0x1234, 0));
    mocked_xTaskGetCurrentTaskHandle_fake.return_val = (TaskHandle_t)0x5678;
    mocked_esp_task_wdt_add_fake.return_val = ESP_FAIL;
    TEST_ASSERT_EQUAL(-1, wd->AddTask(wd, NULL, 0));
    TEST_ASSERT_EQUAL(1, wd->GetRegisteredTaskCount(wd));
    mocked_esp_task_wdt_add_fake.return_val = ESP_OK;
    TEST_ASSERT_EQUAL(0, wd->AddTask(wd, NULL, 0));
    TEST_ASSERT_EQUAL(2, wd->GetRegisteredTaskCount(wd));
    TEST_ASSERT_EQUAL(0, wd->RemoveTask(wd, (void *)0x1234));
    TEST_ASSERT_EQUAL(0, wd->RemoveTask(wd, (void *)0x5678));
}
void test_Watchdog_CLIResetStopsOnFailure(void) {
    setUp_Watchdog();
    IWatchdog *wd = Watchdog_GetModule();
    TEST_ASSERT_EQUAL(0, wd->base.Initialize(&wd->base, NULL));
    TEST_ASSERT_EQUAL(0, wd->base.Start(&wd->base));
    mocked_esp_task_wdt_delete_user_fake.return_val = ESP_FAIL;
    unsigned additions = mocked_esp_task_wdt_add_user_fake.call_count;
    char *argv[] = {"watchdog", "reset"};
    TEST_ASSERT_EQUAL(-1, cmd_watchdog(2, argv));
    TEST_ASSERT_EQUAL(SERVICE_STATE_STARTED, wd->base.GetState(&wd->base));
    TEST_ASSERT_EQUAL(additions, mocked_esp_task_wdt_add_user_fake.call_count);
    mocked_esp_task_wdt_delete_user_fake.return_val = ESP_OK;
    TEST_ASSERT_EQUAL(0, cmd_watchdog(2, argv));
    TEST_ASSERT_EQUAL(additions + 1, mocked_esp_task_wdt_add_user_fake.call_count);
}
void test_Watchdog_RejectedConfigPreservesSettingsAndCapacity(void) {
    setUp_Watchdog();
    IWatchdog *wd = Watchdog_GetModule();
    WatchdogConfig before = watchdogService.config, next = before;
    next.timeout_ms += 1000;
    next.max_tasks = 1;
    mocked_esp_task_wdt_reconfigure_fake.return_val = ESP_FAIL;
    TEST_ASSERT_EQUAL(-1, wd->UpdateConfig(wd, &next));
    TEST_ASSERT_EQUAL(before.timeout_ms, wd->GetTimeout(wd));
    TEST_ASSERT_EQUAL(before.max_tasks, watchdogService.registry.maxTasks);
    mocked_esp_task_wdt_reconfigure_fake.return_val = ESP_OK;
    watchdogService.state = SERVICE_STATE_STARTED;
    TEST_ASSERT_EQUAL(0, wd->AddTask(wd, (void *)0x1234, 0));
    TEST_ASSERT_EQUAL(0, wd->AddTask(wd, (void *)0x5678, 0));
    unsigned calls = mocked_esp_task_wdt_reconfigure_fake.call_count;
    TEST_ASSERT_EQUAL(-1, wd->UpdateConfig(wd, &next));
    TEST_ASSERT_EQUAL(calls, mocked_esp_task_wdt_reconfigure_fake.call_count);
    TEST_ASSERT_EQUAL(before.max_tasks, watchdogService.registry.maxTasks);
}


void test_Watchdog_TimerCleanupRetainsOwnershipOnFailure(void) {
    setUp_Watchdog();
    WatchdogTimers_t timers = {.autoFeedTimer = (esp_timer_handle_t)0x200};
    mocked_esp_timer_stop_fake.return_val = ESP_FAIL;
    TEST_ASSERT_EQUAL(ESP_FAIL, WatchdogTimers_StopAutoFeed(&timers));
    TEST_ASSERT_NOT_NULL(timers.autoFeedTimer);
    TEST_ASSERT_EQUAL(0, mocked_esp_timer_delete_fake.call_count);
    mocked_esp_timer_stop_fake.return_val = ESP_ERR_INVALID_STATE;
    mocked_esp_timer_delete_fake.return_val = ESP_FAIL;
    TEST_ASSERT_EQUAL(ESP_FAIL, WatchdogTimers_StopAutoFeed(&timers));
    TEST_ASSERT_NOT_NULL(timers.autoFeedTimer);
    mocked_esp_timer_delete_fake.return_val = ESP_OK;
    TEST_ASSERT_EQUAL(ESP_OK, WatchdogTimers_StopAutoFeed(&timers));
    TEST_ASSERT_NULL(timers.autoFeedTimer);
}
static esp_task_wdt_config_t captured_wdt_config;
static esp_err_t capture_wdt_config(const esp_task_wdt_config_t *cfg) {
    captured_wdt_config = *cfg;
    return ESP_OK;
}

void test_Watchdog_ConfigTimerFailureRollsBack(void) {
    setUp_Watchdog();
    IWatchdog *wd = Watchdog_GetModule();
    TEST_ASSERT_EQUAL(0, wd->base.Initialize(&wd->base, NULL));
    TEST_ASSERT_EQUAL(0, wd->base.Start(&wd->base));
    WatchdogConfig old = watchdogService.config, next = old;
    next.timeout_ms += 1000;
    next.warning_threshold_ms -= 1000;
    mocked_esp_task_wdt_reconfigure_fake.custom_fake = capture_wdt_config;
    esp_err_t seq[] = {ESP_FAIL, ESP_OK};
    SET_RETURN_SEQ(mocked_esp_timer_start_periodic, seq, 2);
    TEST_ASSERT_EQUAL(-1, wd->UpdateConfig(wd, &next));
    TEST_ASSERT_EQUAL(old.timeout_ms, wd->GetTimeout(wd));
    TEST_ASSERT_EQUAL(old.warning_threshold_ms, watchdogService.config.warning_threshold_ms);
    TEST_ASSERT_EQUAL(old.timeout_ms, captured_wdt_config.timeout_ms);
    TEST_ASSERT_EQUAL((uint64_t)old.warning_threshold_ms * 1000, mocked_esp_timer_start_periodic_fake.arg1_val);
    TEST_ASSERT_NOT_NULL(watchdogService.autoFeedUser);
    TEST_ASSERT_EQUAL(0, wd->base.CheckHealth(&wd->base));
}
void test_Watchdog_RollbackFailureIsUnhealthyAndRecoverable(void) {
    setUp_Watchdog();
    IWatchdog *wd = Watchdog_GetModule();
    TEST_ASSERT_EQUAL(0, wd->base.Initialize(&wd->base, NULL));
    TEST_ASSERT_EQUAL(0, wd->base.Start(&wd->base));
    WatchdogConfig next = watchdogService.config;
    next.timeout_ms += 1000;
    esp_err_t timerSeq[] = {ESP_FAIL, ESP_OK};
    esp_err_t sdkSeq[] = {ESP_OK, ESP_FAIL};
    SET_RETURN_SEQ(mocked_esp_timer_start_periodic, timerSeq, 2);
    SET_RETURN_SEQ(mocked_esp_task_wdt_reconfigure, sdkSeq, 2);
    TEST_ASSERT_EQUAL(-1, wd->UpdateConfig(wd, &next));
    TEST_ASSERT_EQUAL(SERVICE_STATE_ERROR, wd->base.GetState(&wd->base));
    TEST_ASSERT_EQUAL(-1, wd->base.CheckHealth(&wd->base));
    RESET_FAKE(mocked_esp_task_wdt_reconfigure);
    TEST_ASSERT_EQUAL(0, wd->base.Stop(&wd->base));
    TEST_ASSERT_EQUAL(0, wd->base.Start(&wd->base));
    TEST_ASSERT_EQUAL(0, wd->base.CheckHealth(&wd->base));
}
void test_Watchdog_WarningFailureIsObservableAndRecoverable(void) {
    setUp_Watchdog();
    IWatchdog *wd = Watchdog_GetModule();
    TEST_ASSERT_EQUAL(0, wd->base.Initialize(&wd->base, NULL));
    TEST_ASSERT_EQUAL(0, wd->base.Start(&wd->base));
    mocked_esp_timer_start_once_fake.return_val = ESP_FAIL;
    wd->Feed(wd);
    TEST_ASSERT_EQUAL(1, watchdogService.stats.totalFeeds);
    TEST_ASSERT_EQUAL(-3, wd->base.CheckHealth(&wd->base));
    mocked_esp_timer_start_once_fake.return_val = ESP_OK;
    wd->Feed(wd);
    TEST_ASSERT_EQUAL(0, wd->base.CheckHealth(&wd->base));
    mocked_esp_timer_delete_fake.return_val = ESP_FAIL;
    TEST_ASSERT_EQUAL(-1, wd->base.Stop(&wd->base));
    TEST_ASSERT_EQUAL(SERVICE_STATE_ERROR, wd->base.GetState(&wd->base));
    TEST_ASSERT_NOT_NULL(watchdogService.timers.autoFeedTimer);
    mocked_esp_timer_delete_fake.return_val = ESP_OK;
    TEST_ASSERT_EQUAL(0, wd->base.Stop(&wd->base));
    TEST_ASSERT_NULL(watchdogService.timers.autoFeedTimer);
    TEST_ASSERT_NULL(watchdogService.timers.warningTimer);
}

void test_Watchdog_AutoFeedSurvivesBusyServiceMutex(void) {
    setUp_Watchdog();
    IWatchdog *wd = Watchdog_GetModule();
    TEST_ASSERT_EQUAL(0, wd->base.Initialize(&wd->base, NULL));
    TEST_ASSERT_EQUAL(0, wd->base.Start(&wd->base));
    RESET_FAKE(mocked_esp_task_wdt_reset_user);
    mocked_xSemaphoreTake_fake.return_val = 0;
    Watchdog_AutoFeedCallback(NULL);
    TEST_ASSERT_EQUAL(1, mocked_esp_task_wdt_reset_user_fake.call_count);
    TEST_ASSERT_EQUAL(0, mocked_xSemaphoreTake_fake.arg1_val);
    mocked_xSemaphoreTake_fake.return_val = 1;
    WatchdogStats_t stats;
    TEST_ASSERT_EQUAL(0, wd->GetStats(wd, &stats));
    TEST_ASSERT_EQUAL(1, stats.totalAutoFeeds);
}

static esp_err_t reset_user_during_stop(esp_task_wdt_user_handle_t user) {
    TEST_ASSERT_EQUAL_PTR(watchdogService.autoFeedUser, user);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, Watchdog_StopAutomaticFeed());
    TEST_ASSERT_EQUAL(0, mocked_esp_task_wdt_delete_user_fake.call_count);
    TEST_ASSERT_FALSE(watchdogService.autoFeedClosing);
    return ESP_OK;
}

void test_Watchdog_StopPreservesBorrowedAutomaticUser(void) {
    setUp_Watchdog();
    IWatchdog *wd = Watchdog_GetModule();
    TEST_ASSERT_EQUAL(0, wd->base.Initialize(&wd->base, NULL));
    TEST_ASSERT_EQUAL(0, wd->base.Start(&wd->base));
    mocked_esp_task_wdt_reset_user_fake.custom_fake = reset_user_during_stop;
    Watchdog_AutoFeedCallback(NULL);
    TEST_ASSERT_EQUAL(0, watchdogService.autoFeedActive);
    TEST_ASSERT_EQUAL(1, watchdogService.stats.totalAutoFeeds);
    TEST_ASSERT_EQUAL(0, wd->base.Stop(&wd->base));
    TEST_ASSERT_EQUAL(1, mocked_esp_task_wdt_delete_user_fake.call_count);
    Watchdog_AutoFeedCallback(NULL);
    TEST_ASSERT_EQUAL(1, mocked_esp_task_wdt_reset_user_fake.call_count);
}

void test_Watchdog_CLIStrictArgumentsPreserveConfiguration(void) {
    setUp_Watchdog(); IWatchdog *wd = Watchdog_GetModule(); wd->base.Initialize(&wd->base, NULL);
    unsigned calls = mocked_esp_task_wdt_reconfigure_fake.call_count;
    char *values[] = {"6000junk", "-1", "+6000", " 6000", "999", "60001", "4294967296000", "", NULL};
    for (unsigned i = 0; i < sizeof(values)/sizeof(values[0]); ++i) {
        char *args[] = {"watchdog", "config", values[i]}; TEST_ASSERT_EQUAL(-1, cmd_watchdog(3, args));
    }
    char *extra[] = {"watchdog", "feed", "extra"}; TEST_ASSERT_EQUAL(-1, cmd_watchdog(3, extra));
    char *missing[] = {"watchdog", NULL}; TEST_ASSERT_EQUAL(-1, cmd_watchdog(2, missing));
    TEST_ASSERT_EQUAL(-1, cmd_watchdog(0, NULL));
    TEST_ASSERT_EQUAL(calls, mocked_esp_task_wdt_reconfigure_fake.call_count);
}
void test_Watchdog_TimeoutUpdatePreservesOtherLiveSettings(void) {
    setUp_Watchdog(); IWatchdog *wd = Watchdog_GetModule(); wd->base.Initialize(&wd->base, NULL);
    WatchdogConfig cfg = WatchdogConfig_GetDefault(); cfg.auto_feed_enabled = false; cfg.panic_on_timeout = false;
    cfg.max_tasks = 3; cfg.warning_threshold_ms = 1200;
    TEST_ASSERT_EQUAL(0, wd->UpdateConfig(wd, &cfg));
    char *args[] = {"watchdog", "config", "6000"}; TEST_ASSERT_EQUAL(0, cmd_watchdog(3, args));
    TEST_ASSERT_EQUAL(6000, watchdogService.config.timeout_ms);
    TEST_ASSERT_EQUAL(1200, watchdogService.config.warning_threshold_ms);
    TEST_ASSERT_FALSE(watchdogService.config.auto_feed_enabled); TEST_ASSERT_FALSE(watchdogService.config.panic_on_timeout);
    TEST_ASSERT_EQUAL(3, watchdogService.config.max_tasks);
    TEST_ASSERT_EQUAL(0, wd->UpdateTimeout(wd, 1000)); TEST_ASSERT_EQUAL(500, watchdogService.config.warning_threshold_ms);
    mocked_xSemaphoreTake_fake.return_val = 0;
    TEST_ASSERT_EQUAL(-1, wd->UpdateTimeout(wd, 2000)); TEST_ASSERT_EQUAL(1000, watchdogService.config.timeout_ms);
}
void test_Watchdog_CheckedFeedRejectsSdkAndTimerFailure(void) {
    setUp_Watchdog(); IWatchdog *wd = Watchdog_GetModule(); wd->base.Initialize(&wd->base, NULL); wd->base.Start(&wd->base);
    mocked_esp_task_wdt_reset_fake.return_val = ESP_FAIL;
    TEST_ASSERT_EQUAL(-1, WD_CmdFeed(wd)); TEST_ASSERT_EQUAL(0, watchdogService.stats.totalFeeds);
    mocked_esp_task_wdt_reset_fake.return_val = ESP_OK;
    mocked_esp_timer_start_once_fake.return_val = ESP_FAIL;
    TEST_ASSERT_EQUAL(-1, wd->FeedChecked(wd)); TEST_ASSERT_EQUAL(1, watchdogService.stats.totalFeeds);
    mocked_esp_timer_start_once_fake.return_val = ESP_OK;
    TEST_ASSERT_EQUAL(0, wd->FeedChecked(wd)); TEST_ASSERT_EQUAL(2, watchdogService.stats.totalFeeds);
}
void test_Watchdog_CLIUnavailableCallbacksAndFailedStatsAreRejected(void) {
    setUp_Watchdog(); IWatchdog *wd = Watchdog_GetModule(); wd->base.Initialize(&wd->base, NULL);
    mocked_xSemaphoreTake_fake.return_val = 0; TEST_ASSERT_EQUAL(-1, WD_CmdStatus(wd));
    IWatchdog empty = {0};
    TEST_ASSERT_EQUAL(-1, WD_CmdStatus(&empty)); TEST_ASSERT_EQUAL(-1, WD_CmdFeed(&empty));
    TEST_ASSERT_EQUAL(-1, WD_CmdSelfTest(&empty)); TEST_ASSERT_EQUAL(-1, WD_CmdHealth(&empty));
    TEST_ASSERT_EQUAL(-1, WD_CmdTasks(&empty)); TEST_ASSERT_EQUAL(-1, WD_CmdReset(&empty));
    TEST_ASSERT_EQUAL(-1, wd->UpdateTimeout(&empty, 5000)); TEST_ASSERT_EQUAL(-1, wd->FeedChecked(&empty));
}
static int32_t feed_without_counter_change(IWatchdog *self) { (void)self; return 0; }
void test_Watchdog_SelfTestAcceptsCounterWrapAndRejectsUnchangedCounter(void) {
    setUp_Watchdog();
    IWatchdog *wd = Watchdog_GetModule();
    TEST_ASSERT_EQUAL(0, wd->base.Initialize(&wd->base, NULL));
    TEST_ASSERT_EQUAL(0, wd->base.Start(&wd->base));
    watchdogService.stats.totalFeeds = UINT32_MAX;
    TEST_ASSERT_EQUAL(0, wd->SelfTest(wd));
    TEST_ASSERT_EQUAL_UINT32(0, watchdogService.stats.totalFeeds);
    int32_t (*saved)(IWatchdog *) = wd->FeedChecked;
    wd->FeedChecked = feed_without_counter_change;
    int32_t result = wd->SelfTest(wd);
    wd->FeedChecked = saved;
    TEST_ASSERT_EQUAL(-2, result);
}
static int32_t unavailable_watchdog_stats(IWatchdog *self, WatchdogStats_t *out) { (void)self; (void)out; return -1; }
void test_Watchdog_SelfTestCannotPassWithUnavailableFinalStats(void) {
    setUp_Watchdog(); IWatchdog *wd = Watchdog_GetModule(); wd->base.Initialize(&wd->base, NULL);
    int32_t (*saved)(IWatchdog *, WatchdogStats_t *) = wd->GetStats;
    wd->GetStats = unavailable_watchdog_stats;
    int32_t result = wd->SelfTest(wd); wd->GetStats = saved;
    TEST_ASSERT_EQUAL(-3, result);
}

#include "../../Infrastructure/support/console_capture.h"
void test_Watchdog_CLILanguageAndMachineStatus(void) {
    setUp_Watchdog(); IWatchdog *wd = Watchdog_GetModule();
    TEST_ASSERT_EQUAL(0, wd->base.Initialize(&wd->base, NULL));
    TEST_ASSERT_EQUAL(0, wd->base.Start(&wd->base));
    char tr[2048], en[2048]; char *human[] = {"watchdog", "status"};
    char *machine[] = {"watchdog", "status", "--machine"};
    const LocalizationCatalog *saved = Localization_Snapshot();
    TEST_ASSERT_TRUE(Localization_Apply("tr"));
    TEST_ASSERT_EQUAL(0, test_capture_console(cmd_watchdog,2,human,tr,sizeof(tr)));
    TEST_ASSERT_NOT_NULL(strstr(tr,Localization_Text(Localization_Find("tr"),LOC_CLI_WATCHDOG_TITLE)));
    TEST_ASSERT_EQUAL(0, test_capture_console(cmd_watchdog,3,machine,tr,sizeof(tr)));
    TEST_ASSERT_TRUE(Localization_Apply("en"));
    TEST_ASSERT_EQUAL(0, test_capture_console(cmd_watchdog,2,human,en,sizeof(en)));
    TEST_ASSERT_NOT_NULL(strstr(en,"Watchdog Module Status"));
    TEST_ASSERT_EQUAL(0, test_capture_console(cmd_watchdog,3,machine,en,sizeof(en)));
    TEST_ASSERT_EQUAL_STRING(tr,en);
    TEST_ASSERT_NOT_NULL(strstr(en,"RACOOL_CLI v=1 type=watchdog_status active=0"));
    int32_t (*saved_stats)(IWatchdog *,WatchdogStats_t *) = wd->GetStats;
    wd->GetStats = unavailable_watchdog_stats;
    TEST_ASSERT_NOT_EQUAL(0,test_capture_console(cmd_watchdog,3,machine,en,sizeof(en)));
    wd->GetStats = saved_stats;
    TEST_ASSERT_NULL(strstr(en,"RACOOL_CLI"));
    TEST_ASSERT_TRUE(Localization_Apply(saved->code));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_Watchdog_CLILanguageAndMachineStatus);
    RUN_TEST(test_Watchdog_CLIStrictArgumentsPreserveConfiguration);
    RUN_TEST(test_Watchdog_TimeoutUpdatePreservesOtherLiveSettings);
    RUN_TEST(test_Watchdog_CheckedFeedRejectsSdkAndTimerFailure);
    RUN_TEST(test_Watchdog_CLIUnavailableCallbacksAndFailedStatsAreRejected);
    RUN_TEST(test_Watchdog_SelfTestCannotPassWithUnavailableFinalStats);
    RUN_TEST(test_Watchdog_SelfTestAcceptsCounterWrapAndRejectsUnchangedCounter);
    RUN_TEST(test_Watchdog_StopPreservesBorrowedAutomaticUser);
    RUN_TEST(test_Watchdog_AutoFeedSurvivesBusyServiceMutex);
    RUN_TEST(test_Watchdog_TimerCleanupRetainsOwnershipOnFailure);
    RUN_TEST(test_Watchdog_ConfigTimerFailureRollsBack);
    RUN_TEST(test_Watchdog_RollbackFailureIsUnhealthyAndRecoverable);
    RUN_TEST(test_Watchdog_WarningFailureIsObservableAndRecoverable);
    RUN_TEST(test_Watchdog_RemoveFailurePreservesRegistration);
    RUN_TEST(test_Watchdog_CurrentTaskUsesConcreteIdentity);
    RUN_TEST(test_Watchdog_CLIResetStopsOnFailure);
    RUN_TEST(test_Watchdog_RejectedConfigPreservesSettingsAndCapacity);
    RUN_TEST(test_Watchdog_RestartPreservesRegisteredTaskActivity);
    RUN_TEST(test_Watchdog_TimerFailureReleasesUser);
    RUN_TEST(test_Watchdog_WarningCallbackDoesNotBlockOrRunAfterStop);
    RUN_TEST(test_Watchdog_AutomaticFeedOwnsUserUntilStop);
    RUN_TEST(test_Watchdog_FailedAutomaticFeedPreservesEvidence);
    RUN_TEST(test_Watchdog_AutomaticUserFailuresAreNotSuccess);
    RUN_TEST(test_Watchdog_AutoFeedDisableReleasesOwnedUser);
    RUN_TEST(test_Watchdog_Lifecycle);
    RUN_TEST(test_Watchdog_Initialize_Idempotent);
    RUN_TEST(test_Watchdog_Feed);
    RUN_TEST(test_Watchdog_Feed_ErrorPath);
    RUN_TEST(test_Watchdog_TaskManagement);
    RUN_TEST(test_Watchdog_MaxTasksLimit);
    RUN_TEST(test_Watchdog_Queries);
    RUN_TEST(test_Watchdog_Stats);
    RUN_TEST(test_Watchdog_UpdateConfig);
    RUN_TEST(test_Watchdog_ConfigValidation_Warning);
    RUN_TEST(test_Watchdog_SelfTest);
    RUN_TEST(test_Watchdog_CheckHealth_LastFeed);
    RUN_TEST(test_Watchdog_ConsoleCommands);
    RUN_TEST(test_Watchdog_ConsoleCommands_Unknown);
    RUN_TEST(test_Watchdog_ConsoleCommands_ConfigInvalid);
    return UNITY_END();
}

/**
 * @brief Convenience aggregate runner used by the device test harness.
 */

