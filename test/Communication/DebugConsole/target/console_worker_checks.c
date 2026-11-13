#include "unity.h"
#define DiagnosticCommands_Register ConsoleTarget_RegisterDiagnostics
#define SystemCommands_Register ConsoleTarget_Register
#define HardwareCommands_Register ConsoleTarget_Register
#define PerformanceCommands_Register ConsoleTarget_Register
#define NetworkCommands_Register ConsoleTarget_Register
#define StdioConsoleIO_GetDriver ConsoleTarget_GetIO
#define FlashLogger_GetModule ConsoleTarget_GetLogger
#define DashboardManager_GetModule ConsoleTarget_GetDashboard
#define ConsoleEventPublisher_Started ConsoleTarget_Started
#define ConsoleEventPublisher_Stopped ConsoleTarget_Stopped
#define ConsoleEventPublisher_CommandExecuted ConsoleTarget_Executed
#define ConsoleEventPublisher_CommandFailed ConsoleTarget_Failed
#define DeveloperMode_Reset ConsoleTarget_DeveloperReset
#define DeveloperMode_SetEnabled ConsoleTarget_DeveloperSetEnabled
#define DeveloperMode_IsEnabled ConsoleTarget_DeveloperIsEnabled
#define DeveloperMode_IsUserRoot ConsoleTarget_DeveloperIsUserRoot
#define DeveloperMode_IsVisibleUserRoot ConsoleTarget_DeveloperIsVisibleUserRoot
#define DeveloperMode_IsUserLine ConsoleTarget_DeveloperIsUserLine
#define DeveloperMode_UserUsage ConsoleTarget_DeveloperUserUsage
#include "../../../../src/Communication/DebugConsole/DebugConsole.c"
#include "../../../../src/Communication/DebugConsole/Core/DeveloperMode.c"
#include "../../../../src/Communication/DebugConsole/Core/CommandRegistry.c"
#include "../../../../src/Communication/DebugConsole/Core/InputParser.c"
#include "../../../../src/Communication/DebugConsole/Core/HistoryManager.c"

/* The worker, SDK console and RTOS ownership are real. Only the transport and
 * unrelated command modules/events are injected; no USB input is consumed. */
static SemaphoreHandle_t io_release;
static atomic_bool io_entered;
static atomic_bool drain_io;
static atomic_uint io_deinit_count;
static bool initialized;
static bool watchdog_owned;
static esp_log_level_t original_log_level;
static bool log_level_saved;
static SemaphoreHandle_t command_release;
static atomic_bool command_entered, command_returned;
static bool public_task_started;
static SemaphoreHandle_t registration_release;
static atomic_bool registration_entered, registration_returned;
static atomic_int registration_result;
static atomic_uint registration_violations;
static bool registration_task_started;

static void blocked_registration(IDebugConsole *console) {
    if (console->base.Start(&console->base) != ESP_ERR_INVALID_STATE ||
        console->base.Stop(&console->base) != ESP_ERR_INVALID_STATE)
        atomic_fetch_add(&registration_violations, 1);
    atomic_store(&registration_entered, true);
    xSemaphoreTake(registration_release, portMAX_DELAY);
    /* Intentionally ignore the backend result: RegisterModule must preserve it. */
    (void)console->RegisterCommand(console, NULL);
}
static void registration_task(void *parameter) {
    IDebugConsole *console = parameter;
    atomic_store(&registration_result,
        console->RegisterModule(console, "target_registration", blocked_registration));
    atomic_store(&registration_returned, true);
    vTaskDelete(NULL);
}

static int blocked_command(int argc, char **argv) {
    (void)argc; (void)argv;
    atomic_store(&command_entered, true);
    xSemaphoreTake(command_release, portMAX_DELAY);
    return 0;
}

static void public_command_task(void *unused) {
    (void)unused;
    DeveloperMode_SetEnabled(true);
    DebugConsole_ProcessCommand(&consoleService.base, "target_block");
    atomic_store(&command_returned, true);
    vTaskDelete(NULL);
}

void ConsoleTarget_Register(IDebugConsole *console) { (void)console; }
esp_err_t ConsoleTarget_RegisterDiagnostics(IDebugConsole *console) { (void)console; return ESP_OK; }
ILogger *ConsoleTarget_GetLogger(void) { return NULL; }
IDashboard *ConsoleTarget_GetDashboard(void) { return NULL; }
void ConsoleTarget_Started(void) {}
void ConsoleTarget_Stopped(void) {}
void ConsoleTarget_Executed(const char *command) { (void)command; }
void ConsoleTarget_Failed(const char *command, int error) { (void)command; (void)error; }
static int32_t io_init(IConsoleIO *io) { (void)io; return 0; }
static int32_t io_deinit(IConsoleIO *io) {
    (void)io;
    atomic_fetch_add(&io_deinit_count, 1);
    return 0;
}
static bool io_available(IConsoleIO *io, uint32_t timeout) {
    (void)io; (void)timeout;
    atomic_store(&io_entered, true);
    if (!atomic_load(&drain_io)) xSemaphoreTake(io_release, portMAX_DELAY);
    return false;
}
static IConsoleIO target_io = {
    .Initialize = io_init, .Deinitialize = io_deinit, .IsDataAvailable = io_available,
};
IConsoleIO *ConsoleTarget_GetIO(void) { return &target_io; }

void target_console_worker_cleanup(void) {
    atomic_store(&drain_io, true);
    if (io_release) xSemaphoreGive(io_release);
    if (command_release) xSemaphoreGive(command_release);
    if (registration_release) xSemaphoreGive(registration_release);
    if (registration_task_started) {
        for (unsigned i = 0; i < 200 && !atomic_load(&registration_returned); ++i)
            vTaskDelay(pdMS_TO_TICKS(10));
        TEST_ASSERT_TRUE(atomic_load(&registration_returned));
        registration_task_started = false;
    }
    if (public_task_started) {
        for (unsigned i = 0; i < 200 && !atomic_load(&command_returned); ++i)
            vTaskDelay(pdMS_TO_TICKS(10));
        TEST_ASSERT_TRUE(atomic_load(&command_returned));
        public_task_started = false;
    }
    if (initialized) {
        TEST_ASSERT_EQUAL(ESP_OK, DebugConsole_Stop(&consoleService.base.base));
        initialized = false;
        /* Remove this test's callbacks only after captured registration and
         * console ownership have ended, before releasing callback storage. */
        TEST_ASSERT_TRUE(dc_take_mutex(pdMS_TO_TICKS(200)));
        consoleService.pluginCount = 0;
        dc_give_mutex();
    }
    if (io_release) { vSemaphoreDelete(io_release); io_release = NULL; }
    if (command_release) { vSemaphoreDelete(command_release); command_release = NULL; }
    if (registration_release) { vSemaphoreDelete(registration_release); registration_release = NULL; }
    if (watchdog_owned) {
        TEST_ASSERT_EQUAL(ESP_OK, esp_task_wdt_deinit());
        watchdog_owned = false;
    }
    if (log_level_saved) {
        esp_log_level_set("*", original_log_level);
        log_level_saved = false;
    }
}

void target_console_worker_verify_shutdown(void) {
    atomic_store(&drain_io, false);
    original_log_level = esp_log_level_get("*");
    log_level_saved = true;
    if (esp_task_wdt_status(NULL) == ESP_ERR_INVALID_STATE) {
        const esp_task_wdt_config_t config = {.timeout_ms = 5000, .idle_core_mask = 0, .trigger_panic = true};
        TEST_ASSERT_EQUAL(ESP_OK, esp_task_wdt_init(&config));
        watchdog_owned = true;
    }
    IDebugConsole *console = DebugConsole_GetModule();
    IService *service = &console->base;
    TEST_ASSERT_EQUAL(ESP_OK, service->Initialize(service, NULL));
    initialized = true;
    TEST_ASSERT_FALSE(DeveloperMode_IsEnabled());
    TEST_ASSERT_TRUE(DeveloperMode_IsUserLine("\"CLIMATE\" \"MODE\" ON"));
    TEST_ASSERT_FALSE(DeveloperMode_IsUserLine("\"SYSTEM\" TASKS"));
    TEST_ASSERT_FALSE(DeveloperMode_IsUserLine("DEVELOPER TOOLS diagnostics"));
    TEST_ASSERT_EQUAL(pdTRUE,xSemaphoreTake(consoleService.mutex,pdMS_TO_TICKS(100)));
    TEST_ASSERT_NOT_EQUAL(ESP_OK,console->SetLiveMode(console,true));
    TEST_ASSERT_FALSE(consoleService.isLiveMode);
    TEST_ASSERT_NOT_EQUAL(ESP_OK,console->SetMuteLog(console,true));
    xSemaphoreGive(consoleService.mutex);
    TEST_ASSERT_EQUAL(ESP_OK,console->SetLiveMode(console,true));
    TEST_ASSERT_TRUE(consoleService.isLiveMode);
    TEST_ASSERT_EQUAL(ESP_OK,console->SetLiveMode(console,false));
    TEST_ASSERT_EQUAL(ESP_OK,console->SetMuteLog(console,true));
    TEST_ASSERT_EQUAL(ESP_LOG_WARN,esp_log_level_get("*"));
    TEST_ASSERT_EQUAL(ESP_OK,console->SetMuteLog(console,false));
    TEST_ASSERT_EQUAL(ESP_LOG_INFO,esp_log_level_get("*"));
    char history[DEBUG_CONSOLE_HISTORY_SIZE][DEBUG_CONSOLE_LINE_BUFFER_SIZE];int historyCount=-1;
    TEST_ASSERT_TRUE(HistoryManager_Add("owned-target-history"));
    TEST_ASSERT_EQUAL(ESP_OK,console->CopyCommandHistory(console,history,DEBUG_CONSOLE_HISTORY_SIZE,&historyCount));
    TEST_ASSERT_EQUAL(1,historyCount);HistoryManager_Clear();
    TEST_ASSERT_EQUAL_STRING("owned-target-history",history[0]);
    io_release = xSemaphoreCreateBinary();
    TEST_ASSERT_NOT_NULL(io_release);
    atomic_store(&io_entered, false);
    atomic_store(&io_deinit_count, 0);
    TEST_ASSERT_EQUAL(ESP_OK, service->Start(service));
    for (unsigned i = 0; i < 100 && !atomic_load(&io_entered); ++i)
        vTaskDelay(pdMS_TO_TICKS(10));
    TEST_ASSERT_TRUE(atomic_load(&io_entered));
    TaskHandle_t owned = consoleService.consoleTaskHandle;
    TEST_ASSERT_NOT_NULL(owned);
    TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT, service->Stop(service));
    TEST_ASSERT_EQUAL_PTR(owned, consoleService.consoleTaskHandle);
    TEST_ASSERT_TRUE(consoleService.ioOwned);
    TEST_ASSERT_TRUE(consoleService.sdkOwned);
    TEST_ASSERT_EQUAL_UINT(0, atomic_load(&io_deinit_count));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, service->Start(service));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, console->ProcessCommand(console, "help"));
    xSemaphoreGive(io_release);
    TEST_ASSERT_EQUAL(ESP_OK, service->Stop(service));
    TEST_ASSERT_NULL(consoleService.consoleTaskHandle);
    TEST_ASSERT_EQUAL_UINT(1, atomic_load(&io_deinit_count));
    atomic_store(&io_entered, false);
    TEST_ASSERT_EQUAL(ESP_OK, service->Start(service));
    for (unsigned i = 0; i < 100 && !atomic_load(&io_entered); ++i)
        vTaskDelay(pdMS_TO_TICKS(10));
    TEST_ASSERT_TRUE(atomic_load(&io_entered));
    command_release = xSemaphoreCreateBinary();
    TEST_ASSERT_NOT_NULL(command_release);
    atomic_store(&command_entered, false);
    atomic_store(&command_returned, false);
    const esp_console_cmd_t command = {.command = "target_block", .func = blocked_command};
    TEST_ASSERT_EQUAL(ESP_OK, console->RegisterCommand(console, &command));
    TEST_ASSERT_EQUAL(pdPASS, xTaskCreate(public_command_task, "console_public", 4096, NULL, 5, NULL));
    public_task_started = true;
    for (unsigned i = 0; i < 100 && !atomic_load(&command_entered); ++i)
        vTaskDelay(pdMS_TO_TICKS(10));
    TEST_ASSERT_TRUE(atomic_load(&command_entered));
    xSemaphoreGive(io_release);
    TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT, service->Stop(service));
    TEST_ASSERT_NULL(consoleService.consoleTaskHandle);
    TEST_ASSERT_TRUE(consoleService.sdkOwned);
    TEST_ASSERT_TRUE(consoleService.ioOwned);
    TEST_ASSERT_EQUAL_UINT(1, atomic_load(&io_deinit_count));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, service->Start(service));
    xSemaphoreGive(command_release);
    for (unsigned i = 0; i < 100 && !atomic_load(&command_returned); ++i)
        vTaskDelay(pdMS_TO_TICKS(10));
    TEST_ASSERT_TRUE(atomic_load(&command_returned));
    TEST_ASSERT_EQUAL(ESP_OK, service->Stop(service));
    TEST_ASSERT_EQUAL_UINT(2, atomic_load(&io_deinit_count));

    /* A runtime registrar owns the lifecycle gate without pausing the console
     * as if startup were still in progress. Stop must preserve that ownership. */
    atomic_store(&io_entered, false);
    TEST_ASSERT_EQUAL(ESP_OK, service->Start(service));
    for (unsigned i = 0; i < 100 && !atomic_load(&io_entered); ++i)
        vTaskDelay(pdMS_TO_TICKS(10));
    TEST_ASSERT_TRUE(atomic_load(&io_entered));
    owned = consoleService.consoleTaskHandle;
    registration_release = xSemaphoreCreateBinary();
    TEST_ASSERT_NOT_NULL(registration_release);
    atomic_store(&registration_entered, false);
    atomic_store(&registration_returned, false);
    atomic_store(&registration_violations, 0);
    TEST_ASSERT_EQUAL(pdPASS, xTaskCreate(registration_task, "console_register", 4096, console, 5, NULL));
    registration_task_started = true;
    for (unsigned i = 0; i < 100 && !atomic_load(&registration_entered); ++i)
        vTaskDelay(pdMS_TO_TICKS(10));
    TEST_ASSERT_TRUE(atomic_load(&registration_entered));
    TEST_ASSERT_FALSE(atomic_load(&consoleService.starting));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE,
        console->RegisterModule(console, "foreign_registration", ConsoleTarget_Register));
    TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT, service->Stop(service));
    TEST_ASSERT_EQUAL_PTR(owned, consoleService.consoleTaskHandle);
    TEST_ASSERT_FALSE(atomic_load(&consoleService.closing));
    TEST_ASSERT_TRUE(consoleService.sdkOwned);
    TEST_ASSERT_EQUAL_UINT(2, atomic_load(&io_deinit_count));
    xSemaphoreGive(registration_release);
    for (unsigned i = 0; i < 100 && !atomic_load(&registration_returned); ++i)
        vTaskDelay(pdMS_TO_TICKS(10));
    TEST_ASSERT_TRUE(atomic_load(&registration_returned));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, atomic_load(&registration_result));
    TEST_ASSERT_EQUAL_UINT(0, atomic_load(&registration_violations));
    xSemaphoreGive(io_release);
    TEST_ASSERT_EQUAL(ESP_OK, service->Stop(service));
    TEST_ASSERT_EQUAL_UINT(3, atomic_load(&io_deinit_count));
}
