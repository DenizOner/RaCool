#include "Communication/Zigbee/IZigbee.h"
#include "Management/ConfigurationManager/IConfigurationManager.h"
#include <stddef.h>
#include "Core/ServiceLocator/ServiceNames.h"
#include "Hardware/InternalTemperatureSensor/IInternalTemperatureSensor.h"
#include "Hardware/InfraredTemperatureSensor/IInfraredTemperatureSensor.h"
#include "Hardware/RGBLED/IRGBLED.h"
#include "Hardware/I2CManager/II2CManager.h"
#include "Hardware/PinManager/IPinManager.h"
#include "Communication/DebugConsole/IDebugConsole.h"
#include "Communication/DebugConsole/Core/IDashboard.h"
#include "Communication/DebugConsole/Core/ILogger.h"
#include "Communication/DebugConsole/DebugConsoleConfig.h"
#include "Communication/DebugConsole/Commands/SystemCommands.h"
#include "Core/Localization/Localization.h"
#include "Communication/DebugConsole/Commands/DiagnosticCommands.h"
#include "Management/DiagnosticsManager/IDiagnosticsManager.h"
#include "Communication/DebugConsole/Commands/HardwareCommands.h"
#include "Communication/DebugConsole/Core/CommandRegistry.h"
#include "Communication/DebugConsole/Core/InputParser.h"
#include "Communication/DebugConsole/Core/FlashLogger.h"
#include "Communication/DebugConsole/Core/HistoryManager.h"
#include "../../Infrastructure/support/test_mocks.h"
#include "unity.h"
#include "Infrastructure/support/console_capture.h"
#include <string.h>
#include <stdio.h>
#ifdef _WIN32
#include <io.h>
#define privacy_dup _dup
#define privacy_dup2 _dup2
#define privacy_close _close
#define privacy_fileno _fileno
#else
#include <unistd.h>
#define privacy_dup dup
#define privacy_dup2 dup2
#define privacy_close close
#define privacy_fileno fileno
#endif

#include "Communication/DebugConsole/DebugConsole.c"

FAKE_VALUE_FUNC1(esp_err_t, BuiltinLED_SaveEnabled, bool);
FAKE_VALUE_FUNC1(int32_t, test_console_io_initialize, IConsoleIO *);

/* Inspect the production singleton in this translation unit. */
extern DebugConsoleImpl consoleService;

static int provider_called = 0;
static void test_provider(char *buffer, size_t max_len) {
    provider_called++;
    snprintf(buffer, max_len, "TestVal");
}

void setUp_DC(void) {
    DeveloperMode_Reset();
    RESET_FAKE(mocked_xSemaphoreCreateMutex);
    RESET_FAKE(mocked_xSemaphoreTake);
    RESET_FAKE(mocked_xSemaphoreGive);
    RESET_FAKE(mocked_esp_console_cmd_register);
    mocked_xSemaphoreCreateMutex_fake.return_val = (SemaphoreHandle_t)0x100;
    mocked_xSemaphoreTake_fake.return_val = pdTRUE;
    mocked_xSemaphoreGive_fake.return_val = pdTRUE;
    provider_called = 0;
    CommandRegistry_Init();
    InputParser_Init();
    HistoryManager_Init();
    HistoryManager_Clear();

    consoleService.state = SERVICE_STATE_UNINITIALIZED;
    consoleService.io = NULL;
    consoleService.consoleTaskHandle = NULL;
    consoleService.lifecycleMutex = (SemaphoreHandle_t)0x100;
    consoleService.stopped = (QueueHandle_t)0x200;
    consoleService.closing = false;
    consoleService.starting = false;
    consoleService.startOwner = NULL;
    consoleService.registeringModule = false;
    consoleService.registrationOwner = NULL;
    consoleService.registrationError = ESP_OK;
    consoleService.activeCommands = 0;
    consoleService.stopPending = false;
    consoleService.sdkOwned = false;
    consoleService.ioOwned = false;
    consoleService.pluginCount = 0;
}

void tearDown_DC(void) {}

/* ──────────────────────────────────────────────
 * Initialization Tests
 * ────────────────────────────────────────────── */

void test_DebugConsole_Initialization(void) {
    setUp_DC();
    IDebugConsole *console = DebugConsole_GetModule();
    IService *service = (IService *)console;

    int result = service->Initialize(service, NULL);
    TEST_ASSERT_EQUAL(0, result);
    TEST_ASSERT_EQUAL(SERVICE_STATE_INITIALIZED, service->GetState(service));
}

void test_DebugConsole_StartStop(void) {
    setUp_DC();
    IDebugConsole *console = DebugConsole_GetModule();
    IService *service = (IService *)console;

    service->Initialize(service, NULL);
    consoleService.io = NULL;

    RESET_FAKE(mocked_xTaskCreate);
    TEST_ASSERT_EQUAL(-1, service->Start(service));
    TEST_ASSERT_EQUAL(SERVICE_STATE_INITIALIZED, service->GetState(service));
    TEST_ASSERT_EQUAL(0, mocked_xTaskCreate_fake.call_count);
    IConsoleIO io = {.Initialize = test_console_io_initialize};
    consoleService.io = &io;
    RESET_FAKE(test_console_io_initialize);
    test_console_io_initialize_fake.return_val = -1;
    TEST_ASSERT_EQUAL(-1, service->Start(service));
    TEST_ASSERT_EQUAL(0, mocked_xTaskCreate_fake.call_count);
    test_console_io_initialize_fake.return_val = 0;

    mocked_xTaskCreate_fake.return_val = pdPASS;
    mocked_esp_console_init_fake.return_val = ESP_OK;

    DeveloperMode_SetEnabled(true);
    service->Start(service);
    TEST_ASSERT_EQUAL(SERVICE_STATE_STARTED, service->GetState(service));
    TEST_ASSERT_TRUE(DeveloperMode_IsEnabled());

    DeveloperMode_SetEnabled(true);
    service->Stop(service);
    TEST_ASSERT_EQUAL(SERVICE_STATE_STOPPED, service->GetState(service));
    TEST_ASSERT_TRUE(DeveloperMode_IsEnabled());
    consoleService.io = NULL;
}

void test_DebugConsole_DoubleInitialize(void) {
    setUp_DC();
    IDebugConsole *console = DebugConsole_GetModule();
    IService *service = (IService *)console;

    service->Initialize(service, NULL);
    int result2 = service->Initialize(service, NULL);
    TEST_ASSERT_EQUAL(0, result2);
    TEST_ASSERT_EQUAL(SERVICE_STATE_INITIALIZED, service->GetState(service));
}

void test_DebugConsole_CheckHealth_NotStarted(void) {
    setUp_DC();
    IDebugConsole *console = DebugConsole_GetModule();
    IService *service = (IService *)console;

    service->Initialize(service, NULL);
    int32_t health = service->CheckHealth(service);
    TEST_ASSERT_LESS_THAN(0, health);
}

/* ──────────────────────────────────────────────
 * RegisterCommand Tests
 * ────────────────────────────────────────────── */

void test_RegisterCommand_Success(void) {
    setUp_DC();
    IDebugConsole *console = DebugConsole_GetModule();
    mocked_esp_console_cmd_register_fake.return_val = ESP_OK;

    esp_console_cmd_t cmd = {.command = "MY_CMD", .help = "test", .func = NULL};
    esp_err_t err = console->RegisterCommand(console, &cmd);
    TEST_ASSERT_EQUAL(ESP_OK, err);
}

void test_RegisterCommand_NullRejects(void) {
    setUp_DC();
    IDebugConsole *console = DebugConsole_GetModule();

    esp_err_t err = console->RegisterCommand(console, NULL);
    TEST_ASSERT_NOT_EQUAL(ESP_OK, err);
}

void test_RegisterCommandWithCategory(void) {
    setUp_DC();
    IDebugConsole *console = DebugConsole_GetModule();
    mocked_esp_console_cmd_register_fake.return_val = ESP_OK;

    esp_console_cmd_t cmd = {.command = "NET_TEST", .help = "test"};
    esp_err_t err = console->RegisterCommandWithCategory(
        console, &cmd, CMD_CAT_NETWORK);
    TEST_ASSERT_EQUAL(ESP_OK, err);

    CommandEntry found_copy;
    const CommandEntry *found = CommandRegistry_CopyByName("NET_TEST", &found_copy) ? &found_copy : NULL;
    TEST_ASSERT_NOT_NULL(found);
    TEST_ASSERT_EQUAL(CMD_CAT_NETWORK, found->category);
}

void test_GetRegisteredCommandCount(void) {
    setUp_DC();
    IDebugConsole *console = DebugConsole_GetModule();
    mocked_esp_console_cmd_register_fake.return_val = ESP_OK;

    TEST_ASSERT_EQUAL(0, console->GetRegisteredCommandCount(console));

    esp_console_cmd_t cmd = {.command = "CNT_TEST", .help = "test"};
    console->RegisterCommand(console, &cmd);
    TEST_ASSERT_EQUAL(1, console->GetRegisteredCommandCount(console));
}

/* ──────────────────────────────────────────────
 * RegisterModule Tests
 * ────────────────────────────────────────────── */

static int s_pluginRegistered = 0;
static void mock_plugin_register(IDebugConsole *console) {
    s_pluginRegistered++;
}

void test_RegisterModule_Success(void) {
    setUp_DC();
    s_pluginRegistered = 0;
    IDebugConsole *console = DebugConsole_GetModule();

    esp_err_t err = console->RegisterModule(console, "TestPlugin",
                                            mock_plugin_register);
    TEST_ASSERT_EQUAL(ESP_OK, err);
}

void test_RegisterModule_NullRejects(void) {
    setUp_DC();
    IDebugConsole *console = DebugConsole_GetModule();

    esp_err_t err = console->RegisterModule(console, NULL, mock_plugin_register);
    TEST_ASSERT_NOT_EQUAL(ESP_OK, err);

    err = console->RegisterModule(console, "test", NULL);
    TEST_ASSERT_NOT_EQUAL(ESP_OK, err);
}

/* ──────────────────────────────────────────────
 * CommandRegistry Tests
 * ────────────────────────────────────────────── */

void test_CommandRegistry_Registration(void) {
    setUp_DC();
    CommandRegistry_Init();
    esp_console_cmd_t cmd = {.command = "TEST_CMD", .help = "Test help"};
    bool ok = CommandRegistry_Register(&cmd, CMD_CAT_SYSTEM);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL(1, CommandRegistry_GetCount());
}

void test_CommandRegistry_FindByPrefix(void) {
    setUp_DC();
    CommandRegistry_Init();
    esp_console_cmd_t cmd1 = {.command = "WIFI_START", .help = "h"};
    esp_console_cmd_t cmd2 = {.command = "WIFI_STOP", .help = "h"};
    esp_console_cmd_t cmd3 = {.command = "STATUS", .help = "h"};

    CommandRegistry_Register(&cmd1, CMD_CAT_SYSTEM);
    CommandRegistry_Register(&cmd2, CMD_CAT_SYSTEM);
    CommandRegistry_Register(&cmd3, CMD_CAT_SYSTEM);

    CommandEntry matches[5];
    unsigned cursor = 0;
    int found_count = CommandRegistry_CopyMatches("WIFI", CMD_CAT_MAX, &cursor, matches, 5);
    TEST_ASSERT_EQUAL(2, found_count);
}

void test_CommandRegistry_Unregister(void) {
    setUp_DC();
    CommandRegistry_Init();
    esp_console_cmd_t cmd = {.command = "DEL_ME", .help = "h"};
    CommandRegistry_Register(&cmd, CMD_CAT_DEBUG);
    TEST_ASSERT_EQUAL(1, CommandRegistry_GetCount());

    bool removed = CommandRegistry_Unregister("DEL_ME");
    TEST_ASSERT_TRUE(removed);
    TEST_ASSERT_EQUAL(0, CommandRegistry_GetCount());

    /* Tekrar silme basarisiz olmali */
    removed = CommandRegistry_Unregister("DEL_ME");
    TEST_ASSERT_FALSE(removed);
}

void test_CommandRegistry_DuplicateUpdate(void) {
    setUp_DC();
    CommandRegistry_Init();
    esp_console_cmd_t cmd1 = {.command = "DUP_CMD", .help = "first"};
    esp_console_cmd_t cmd2 = {.command = "DUP_CMD", .help = "second"};

    CommandRegistry_Register(&cmd1, CMD_CAT_SYSTEM);
    CommandRegistry_Register(&cmd2, CMD_CAT_DIAGNOSTIC);

    TEST_ASSERT_EQUAL(1, CommandRegistry_GetCount());

    CommandEntry entry_copy;
    const CommandEntry *entry = CommandRegistry_CopyByName("DUP_CMD", &entry_copy) ? &entry_copy : NULL;
    TEST_ASSERT_NOT_NULL(entry);
    TEST_ASSERT_EQUAL(CMD_CAT_DIAGNOSTIC, entry->category);
}

void test_CommandRegistry_FindByName(void) {
    setUp_DC();
    CommandRegistry_Init();
    esp_console_cmd_t cmd = {.command = "UNIQUE", .help = "unique help"};
    CommandRegistry_Register(&cmd, CMD_CAT_HARDWARE);

    CommandEntry found_copy;
    const CommandEntry *found = CommandRegistry_CopyByName("UNIQUE", &found_copy) ? &found_copy : NULL;
    TEST_ASSERT_NOT_NULL(found);
    TEST_ASSERT_EQUAL_STRING("UNIQUE", found->command);

    CommandEntry not_found_copy;
    const CommandEntry *not_found = CommandRegistry_CopyByName("NONEXIST", &not_found_copy) ? &not_found_copy : NULL;
    TEST_ASSERT_NULL(not_found);
}

void test_CommandRegistry_GetByCategory(void) {
    setUp_DC();
    CommandRegistry_Init();
    esp_console_cmd_t cmd1 = {.command = "SYS1", .help = "h"};
    esp_console_cmd_t cmd2 = {.command = "SYS2", .help = "h"};
    esp_console_cmd_t cmd3 = {.command = "HW1", .help = "h"};

    CommandRegistry_Register(&cmd1, CMD_CAT_SYSTEM);
    CommandRegistry_Register(&cmd2, CMD_CAT_SYSTEM);
    CommandRegistry_Register(&cmd3, CMD_CAT_HARDWARE);

    CommandEntry results[10];
    unsigned cursor = 0;
    int count = CommandRegistry_CopyMatches(NULL, CMD_CAT_SYSTEM, &cursor, results, 10);
    TEST_ASSERT_EQUAL(2, count);

    cursor = 0;
    count = CommandRegistry_CopyMatches(NULL, CMD_CAT_HARDWARE, &cursor, results, 10);
    TEST_ASSERT_EQUAL(1, count);

    cursor = 0;
    count = CommandRegistry_CopyMatches(NULL, CMD_CAT_NETWORK, &cursor, results, 10);
    TEST_ASSERT_EQUAL(0, count);
}

void test_CommandRegistry_Clear(void) {
    setUp_DC();
    CommandRegistry_Init();
    esp_console_cmd_t cmd = {.command = "CLEAR_ME", .help = "h"};
    CommandRegistry_Register(&cmd, CMD_CAT_SYSTEM);
    TEST_ASSERT_EQUAL(1, CommandRegistry_GetCount());

    CommandRegistry_Clear();
    TEST_ASSERT_EQUAL(0, CommandRegistry_GetCount());
}

void test_CommandRegistry_NullRejects(void) {
    setUp_DC();
    CommandRegistry_Init();
    TEST_ASSERT_FALSE(CommandRegistry_Register(NULL, CMD_CAT_SYSTEM));

    esp_console_cmd_t cmd = {.command = NULL, .help = "h"};
    TEST_ASSERT_FALSE(CommandRegistry_Register(&cmd, CMD_CAT_SYSTEM));
}

/* ──────────────────────────────────────────────
 * HistoryManager Tests
 * ────────────────────────────────────────────── */

void test_HistoryManager_AddAndGet(void) {
    setUp_DC();
    HistoryManager_Add("cmd1");
    HistoryManager_Add("cmd2");
    HistoryManager_Add("cmd3");

    int count = 0;
    const char *const *all = HistoryManager_GetAll(&count);
    TEST_ASSERT_EQUAL(3, count);
    TEST_ASSERT_EQUAL_STRING("cmd1", all[0]);
    TEST_ASSERT_EQUAL_STRING("cmd2", all[1]);
    TEST_ASSERT_EQUAL_STRING("cmd3", all[2]);
}

void test_HistoryManager_DuplicateFiltering(void) {
    setUp_DC();
    HistoryManager_Add("cmd1");
    HistoryManager_Add("cmd1");
    HistoryManager_Add("cmd1");

    TEST_ASSERT_EQUAL(1, HistoryManager_GetCount());
}

void test_HistoryManager_CircularOverwrite(void) {
    setUp_DC();
    for (int i = 0; i < DEBUG_CONSOLE_HISTORY_SIZE + 5; i++) {
        char buf[16];
        snprintf(buf, sizeof(buf), "cmd%d", i);
        HistoryManager_Add(buf);
    }

    TEST_ASSERT_EQUAL(DEBUG_CONSOLE_HISTORY_SIZE, HistoryManager_GetCount());
}

void test_HistoryManager_Navigation(void) {
    setUp_DC();
    HistoryManager_Add("first");
    HistoryManager_Add("second");
    HistoryManager_Add("third");

    /* Yukari: en son eklenen */
    const char *prev = HistoryManager_GetPrevious();
    TEST_ASSERT_NOT_NULL(prev);
    TEST_ASSERT_EQUAL_STRING("third", prev);

    prev = HistoryManager_GetPrevious();
    TEST_ASSERT_EQUAL_STRING("second", prev);

    /* Asagi: ileri */
    const char *next = HistoryManager_GetNext();
    TEST_ASSERT_EQUAL_STRING("third", next);
}

void test_HistoryManager_EmptyReturnsNull(void) {
    setUp_DC();
    const char *prev = HistoryManager_GetPrevious();
    TEST_ASSERT_NULL(prev);

    const char *next = HistoryManager_GetNext();
    TEST_ASSERT_NULL(next);
}

void test_HistoryManager_Clear(void) {
    setUp_DC();
    HistoryManager_Add("a");
    HistoryManager_Add("b");
    HistoryManager_Clear();
    TEST_ASSERT_EQUAL(0, HistoryManager_GetCount());
}

void test_HistoryManager_NullAndEmptyReject(void) {
    setUp_DC();
    TEST_ASSERT_FALSE(HistoryManager_Add(NULL));
    TEST_ASSERT_FALSE(HistoryManager_Add(""));
    TEST_ASSERT_EQUAL(0, HistoryManager_GetCount());
}

/* ──────────────────────────────────────────────
 * InputParser Tests
 * ────────────────────────────────────────────── */

void test_InputParser_BasicLine(void) {
    setUp_DC();
    char out[128] = {0};
    InputParser_Reset();

    /* 'abc' + Enter */
    InputParser_ProcessChar('a', out, sizeof(out));
    InputParser_ProcessChar('b', out, sizeof(out));
    InputParser_ProcessChar('c', out, sizeof(out));
    InputParserEvent evt = InputParser_ProcessChar('\r', out, sizeof(out));

    TEST_ASSERT_EQUAL(INPUT_EVENT_LINE_READY, evt);
    TEST_ASSERT_EQUAL_STRING("abc", out);
}

void test_InputParser_Backspace(void) {
    setUp_DC();
    char out[128] = {0};
    InputParser_Reset();

    InputParser_ProcessChar('a', out, sizeof(out));
    InputParser_ProcessChar('b', out, sizeof(out));
    InputParser_ProcessChar('\b', out, sizeof(out)); /* Backspace */
    InputParserEvent evt = InputParser_ProcessChar('\r', out, sizeof(out));

    TEST_ASSERT_EQUAL(INPUT_EVENT_LINE_READY, evt);
    TEST_ASSERT_EQUAL_STRING("a", out);
}

void test_InputParser_Tab(void) {
    setUp_DC();
    char out[128] = {0};
    InputParser_Reset();

    InputParser_ProcessChar('S', out, sizeof(out));
    InputParser_ProcessChar('T', out, sizeof(out));
    InputParserEvent evt = InputParser_ProcessChar('\t', out, sizeof(out));

    TEST_ASSERT_EQUAL(INPUT_EVENT_TAB, evt);
    TEST_ASSERT_EQUAL_STRING("ST", out);
}

void test_InputParser_CtrlC(void) {
    setUp_DC();
    char out[128] = {0};
    InputParser_Reset();

    InputParser_ProcessChar('a', out, sizeof(out));
    InputParserEvent evt = InputParser_ProcessChar(0x03, out, sizeof(out));

    TEST_ASSERT_EQUAL(INPUT_EVENT_CANCEL, evt);
}

void test_InputParser_EmptyEnter(void) {
    setUp_DC();
    char out[128] = {0};
    InputParser_Reset();

    InputParserEvent evt = InputParser_ProcessChar('\r', out, sizeof(out));
    TEST_ASSERT_EQUAL(INPUT_EVENT_NONE, evt);
}

void test_InputParser_UpArrow(void) {
    setUp_DC();
    char out[128] = {0};
    InputParser_Reset();

    /* \x1b[A = Yukari ok */
    InputParser_ProcessChar('\x1b', out, sizeof(out));
    InputParser_ProcessChar('[', out, sizeof(out));
    InputParserEvent evt = InputParser_ProcessChar('A', out, sizeof(out));

    TEST_ASSERT_EQUAL(INPUT_EVENT_HISTORY_PREV, evt);
}

void test_InputParser_DownArrow(void) {
    setUp_DC();
    char out[128] = {0};
    InputParser_Reset();

    InputParser_ProcessChar('\x1b', out, sizeof(out));
    InputParser_ProcessChar('[', out, sizeof(out));
    InputParserEvent evt = InputParser_ProcessChar('B', out, sizeof(out));

    TEST_ASSERT_EQUAL(INPUT_EVENT_HISTORY_NEXT, evt);
}

void test_InputParser_SetLine(void) {
    setUp_DC();
    char out[128] = {0};
    InputParser_Reset();

    InputParser_SetLine("STATUS", out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("STATUS", out);
}

/* ──────────────────────────────────────────────
 * Dashboard Tests
 * ────────────────────────────────────────────── */

void test_Dashboard_Registration(void) {
    setUp_DC();
    IDashboard *dash = DashboardManager_GetModule();
    int before = dash->GetProviderCount(dash);
    TEST_ASSERT_TRUE(dash->RegisterField(dash, "TEST", test_provider));

    /* Preserve providers registered by the console and append this field. */
    TEST_ASSERT_EQUAL(before + 1, dash->GetProviderCount(dash));
    TEST_ASSERT_EQUAL_STRING("TEST", dash->GetProviderLabel(dash, before));
}

void test_Dashboard_Unregister(void) {
    setUp_DC();
    IDashboard *dash = DashboardManager_GetModule();
    int initial = dash->GetProviderCount(dash);

    dash->RegisterField(dash, "TEMP", test_provider);
    TEST_ASSERT_EQUAL(initial + 1, dash->GetProviderCount(dash));

    bool removed = dash->UnregisterField(dash, "TEMP");
    TEST_ASSERT_TRUE(removed);
    TEST_ASSERT_EQUAL(initial, dash->GetProviderCount(dash));
}

void test_Dashboard_DuplicateUpdate(void) {
    setUp_DC();
    IDashboard *dash = DashboardManager_GetModule();
    int before = dash->GetProviderCount(dash);

    dash->RegisterField(dash, "DUP", test_provider);
    dash->RegisterField(dash, "DUP", test_provider);

    TEST_ASSERT_EQUAL(before + 1, dash->GetProviderCount(dash));
}

void test_Dashboard_NullRejects(void) {
    setUp_DC();
    IDashboard *dash = DashboardManager_GetModule();

    bool ok = dash->RegisterField(dash, NULL, test_provider);
    TEST_ASSERT_FALSE(ok);

    ok = dash->RegisterField(dash, "X", NULL);
    TEST_ASSERT_FALSE(ok);
}

/* ──────────────────────────────────────────────
 * ExecuteBatch Test
 * ────────────────────────────────────────────── */

void test_ExecuteBatch_NullSafe(void) {
    setUp_DC();
    IDebugConsole *console = DebugConsole_GetModule();
    /* NULL batch cagirinca crash etmemeli */
    console->ExecuteBatch(console, NULL, 0);
}

/* ──────────────────────────────────────────────
 * GetCommandHistory Test
 * ────────────────────────────────────────────── */

void test_GetCommandHistory(void) {
    setUp_DC();
    IDebugConsole *console = DebugConsole_GetModule();

    HistoryManager_Add("cmd_a");
    HistoryManager_Add("cmd_b");

    int count = 0;
    const char *const *hist = console->GetCommandHistory(console, &count);
    TEST_ASSERT_EQUAL(2, count);
    TEST_ASSERT_NOT_NULL(hist);
    TEST_ASSERT_EQUAL_STRING("cmd_a", hist[0]);
    TEST_ASSERT_EQUAL_STRING("cmd_b", hist[1]);
}

/* ──────────────────────────────────────────────
 * Test Runner
 * ────────────────────────────────────────────── */

void test_DebugConsole_PrivateCommandsNotRetained(void) {
    setUp_DC();
    DeveloperMode_SetEnabled(true);
    const char *commands[] = {"sec auth dummy", "  sec set-pin 1234", "SEC status",
                              "\"sec\" set-token dummy", "s\\ec auth dummy", "'sec' auth dummy"};
    mocked_esp_console_run_fake.return_val = ESP_OK;
    for (size_t i = 0; i < sizeof(commands)/sizeof(commands[0]); ++i) {
        TEST_ASSERT_TRUE(ConsolePrivacy_IsPrivate(commands[i]));
        TEST_ASSERT_EQUAL(ESP_OK, DebugConsole_ProcessCommand(&consoleService.base, commands[i]));
        TEST_ASSERT_EQUAL_STRING(commands[i], mocked_esp_console_run_fake.arg0_val);
        TEST_ASSERT_EQUAL(0, HistoryManager_GetCount());
    }
    TEST_ASSERT_FALSE(ConsolePrivacy_IsPrivate("section status"));
    TEST_ASSERT_EQUAL(ESP_OK, DebugConsole_ProcessCommand(&consoleService.base, "power mode balanced"));
    TEST_ASSERT_EQUAL(1, HistoryManager_GetCount());
}

static esp_err_t failing_command(const char *cmd, int *result) {
    (void)cmd;
    *result = -1;
    return ESP_OK;
}

void test_DebugConsole_HandlerFailurePropagates(void) {
    setUp_DC();
    DeveloperMode_SetEnabled(true);
    mocked_esp_console_run_fake.custom_fake = failing_command;
    esp_err_t result = DebugConsole_ProcessCommand(&consoleService.base, "sec set-mode invalid");
    mocked_esp_console_run_fake.custom_fake = NULL;
    TEST_ASSERT_EQUAL(ESP_FAIL, result);
    TEST_ASSERT_EQUAL(0, HistoryManager_GetCount());
}

void test_DebugConsoleDeveloperGateKeepsProductCommandsAvailable(void) {
    setUp_DC();
    RESET_FAKE(mocked_esp_console_run);
    mocked_esp_console_run_fake.return_val = ESP_OK;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, DebugConsole_ProcessCommand(&consoleService.base, "LOGS tail 5"));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, DebugConsole_ProcessCommand(&consoleService.base, "DEVELOPER TOOLS diagnostics"));
    TEST_ASSERT_EQUAL(0, mocked_esp_console_run_fake.call_count);
    TEST_ASSERT_EQUAL(ESP_OK, DebugConsole_ProcessCommand(&consoleService.base, "CLIMATE MODE ON"));
    TEST_ASSERT_EQUAL(1, mocked_esp_console_run_fake.call_count);
    TEST_ASSERT_EQUAL(ESP_OK, DebugConsole_ProcessCommand(&consoleService.base, "\"CLIMATE\" \"MODE\" ON"));
    TEST_ASSERT_EQUAL(2, mocked_esp_console_run_fake.call_count);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, DebugConsole_ProcessCommand(&consoleService.base, "sec auth dummy"));
    TEST_ASSERT_EQUAL(2, mocked_esp_console_run_fake.call_count);
    DeveloperMode_SetEnabled(true);
    TEST_ASSERT_EQUAL(ESP_OK, DebugConsole_ProcessCommand(&consoleService.base, "LOGS tail 5"));
    TEST_ASSERT_EQUAL(3, mocked_esp_console_run_fake.call_count);
    DeveloperMode_Reset();
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, DebugConsole_ProcessCommand(&consoleService.base, "LOGS tail 5"));
    TEST_ASSERT_EQUAL(3, mocked_esp_console_run_fake.call_count);
}

void test_InternalSelfTestCanRunWithoutChangingDeveloperMode(void) {
    setUp_DC();
    RESET_FAKE(mocked_esp_console_run);
    mocked_esp_console_run_fake.return_val = ESP_OK;
    TEST_ASSERT_FALSE(DeveloperMode_IsEnabled());
    TEST_ASSERT_EQUAL(ESP_OK, DebugConsole_RunCommand(&consoleService.base, "SYS TASKS", true));
    TEST_ASSERT_EQUAL(1, mocked_esp_console_run_fake.call_count);
    TEST_ASSERT_FALSE(DeveloperMode_IsEnabled());
}

void test_DeveloperHistoryNavigationSkipsToolsWhenModeIsOff(void) {
    setUp_DC();
    TEST_ASSERT_TRUE(HistoryManager_Add("CLIMATE STATUS"));
    TEST_ASSERT_TRUE(HistoryManager_Add("LOGS tail 5"));
    char line[DEBUG_CONSOLE_LINE_BUFFER_SIZE];
    TEST_ASSERT_TRUE(dc_copy_visible_history(true, line, sizeof(line)));
    TEST_ASSERT_EQUAL_STRING("CLIMATE STATUS", line);
    DeveloperMode_SetEnabled(true);
    HistoryManager_ResetNavigation();
    TEST_ASSERT_TRUE(dc_copy_visible_history(true, line, sizeof(line)));
    TEST_ASSERT_EQUAL_STRING("LOGS tail 5", line);
    DeveloperMode_Reset();
}

void test_TabCompletionDoesNotSuggestDeveloperRoot(void) {
    setUp_DC();
    SystemCommands_Register(DebugConsole_GetModule());
    InputParser_Reset();
    handle_char('L');
    handle_char('\t');
    char line[DEBUG_CONSOLE_LINE_BUFFER_SIZE] = {0};
    TEST_ASSERT_EQUAL(INPUT_EVENT_TAB, InputParser_ProcessChar('\t', line, sizeof(line)));
    TEST_ASSERT_EQUAL_STRING("LANGUAGE", line);
    InputParser_Reset();
}

void test_DebugConsole_HardwareLEDUsesGlobalModeAndPropagatesFailure(void) {
    setUp_DC();
    RESET_FAKE(BuiltinLED_SaveEnabled);
    RESET_FAKE(mocked_ServiceLocator_GetService);
    mocked_ServiceLocator_GetService_fake.return_val = (void *)1;
    HardwareCommands_Register(DebugConsole_GetModule());
    CommandEntry entry_copy;
    const CommandEntry *entry = CommandRegistry_CopyByName("HW", &entry_copy) ? &entry_copy : NULL;
    TEST_ASSERT_NOT_NULL(entry);
    char *on[] = {"HW", "LED", "ON"};
    char *off[] = {"HW", "LED", "OFF"};
    TEST_ASSERT_EQUAL(0, entry->func(3, on));
    TEST_ASSERT_TRUE(BuiltinLED_SaveEnabled_fake.arg0_val);
    TEST_ASSERT_EQUAL(0, entry->func(3, off));
    TEST_ASSERT_FALSE(BuiltinLED_SaveEnabled_fake.arg0_val);
    BuiltinLED_SaveEnabled_fake.return_val = ESP_FAIL;
    TEST_ASSERT_NOT_EQUAL(0, entry->func(3, on));
    RESET_FAKE(mocked_ServiceLocator_GetService);
}



static unsigned config_field_calls;
static esp_err_t config_field_result;
static esp_err_t capture_config_fields(IConfigurationManager *self, const char *block,
                                      const ConfigurationFieldUpdate *fields, size_t count, bool persist) {
    (void)self;
    ++config_field_calls;
    TEST_ASSERT_TRUE(persist);
    TEST_ASSERT_EQUAL_STRING("hardware", block);
    TEST_ASSERT_EQUAL_UINT(1, count);
    TEST_ASSERT_EQUAL_UINT(offsetof(HardwareConfigs, ext_led_brightness), fields[0].offset);
    TEST_ASSERT_EQUAL_UINT(sizeof(uint8_t), fields[0].size);
    TEST_ASSERT_EQUAL_UINT8(255, *(const uint8_t *)fields[0].data);
    return config_field_result;
}

void test_SystemConfigRejectsMalformedValuesAndPropagatesCommitFailure(void) {
    setUp_DC();
    IConfigurationManager manager = {.UpdateFields = capture_config_fields};
    RESET_FAKE(mocked_ConfigurationManager_GetInstance);
    mocked_ConfigurationManager_GetInstance_fake.return_val = &manager;
    SystemCommands_Register(DebugConsole_GetModule());
    CommandEntry system_copy;
    const CommandEntry *system = CommandRegistry_CopyByName("SYSTEM", &system_copy) ? &system_copy : NULL;
    TEST_ASSERT_NOT_NULL(system);
    config_field_calls = 0;
    char *args[] = {"SYSTEM", "CONFIG", "SET", "brightness", "255", "extra"};
    const char *bad[] = {"", "256", "-1", "1.5", "12bad", "999999999999999999999999", " 1"};
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        args[4] = (char *)bad[i];
        TEST_ASSERT_EQUAL_INT(1, system->func(5, args));
    }
    args[3] = "cold_min";
    const char *bad_float[] = {"nan", "inf", "1junk", "", "1e1000"};
    for (size_t i = 0; i < sizeof(bad_float) / sizeof(bad_float[0]); ++i) {
        args[4] = (char *)bad_float[i];
        TEST_ASSERT_EQUAL_INT(1, system->func(5, args));
    }
    args[3] = "system_enabled"; args[4] = "2";
    TEST_ASSERT_EQUAL_INT(1, system->func(5, args));
    args[3] = "unknown";
    TEST_ASSERT_EQUAL_INT(1, system->func(5, args));
    TEST_ASSERT_EQUAL_UINT(0, config_field_calls);
    args[3] = "brightness"; args[4] = "255";
    TEST_ASSERT_EQUAL_INT(1, system->func(6, args));
    config_field_result = ESP_FAIL;
    TEST_ASSERT_EQUAL_INT(1, system->func(5, args));
    config_field_result = ESP_OK;
    TEST_ASSERT_EQUAL_INT(0, system->func(5, args));
    TEST_ASSERT_EQUAL_UINT(2, config_field_calls);
    mocked_ConfigurationManager_GetInstance_fake.return_val = NULL;
}

void test_DebugConsole_StartRejectsSdkAndTaskFailure(void) {
    setUp_DC();
    IDebugConsole *console = DebugConsole_GetModule();
    IService *service = (IService *)console;
    TEST_ASSERT_EQUAL(0, service->Initialize(service, NULL));
    IConsoleIO io = {.Initialize = test_console_io_initialize};
    consoleService.io = &io;
    RESET_FAKE(test_console_io_initialize);
    RESET_FAKE(mocked_esp_console_init);
    RESET_FAKE(mocked_esp_console_deinit);
    RESET_FAKE(mocked_xTaskCreate);
    mocked_esp_console_init_fake.return_val = ESP_FAIL;
    TEST_ASSERT_EQUAL(ESP_FAIL, service->Start(service));
    TEST_ASSERT_EQUAL(SERVICE_STATE_INITIALIZED, service->GetState(service));
    TEST_ASSERT_EQUAL(0, mocked_xTaskCreate_fake.call_count);
    mocked_esp_console_init_fake.return_val = ESP_OK;
    mocked_xTaskCreate_fake.return_val = pdFAIL;
    TEST_ASSERT_EQUAL(ESP_ERR_NO_MEM, service->Start(service));
    TEST_ASSERT_EQUAL(SERVICE_STATE_INITIALIZED, service->GetState(service));
    TEST_ASSERT_EQUAL(1, mocked_esp_console_deinit_fake.call_count);
    TEST_ASSERT_NULL(consoleService.consoleTaskHandle);
    consoleService.io = NULL;
}

static BaseType_t console_stop_ack(QueueHandle_t queue, void *value, TickType_t timeout) {
    (void)queue; (void)timeout;
    *(esp_err_t *)value = ESP_OK;
    return pdTRUE;
}

void test_DebugConsole_StopTimeoutPreservesOwnershipAndRetries(void) {
    setUp_DC();
    IDebugConsole *console = DebugConsole_GetModule();
    IService *service = (IService *)console;
    consoleService.state = SERVICE_STATE_STARTED;
    consoleService.consoleTaskHandle = (TaskHandle_t)0x300;
    consoleService.sdkOwned = true;
    RESET_FAKE(mocked_xTaskGetCurrentTaskHandle);
    RESET_FAKE(mocked_xQueueReceive);
    RESET_FAKE(mocked_vTaskDelete);
    RESET_FAKE(mocked_esp_console_deinit);
    TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT, service->Stop(service));
    TEST_ASSERT_EQUAL_PTR((TaskHandle_t)0x300, consoleService.consoleTaskHandle);
    TEST_ASSERT_TRUE(consoleService.sdkOwned);
    TEST_ASSERT_EQUAL(0, mocked_vTaskDelete_fake.call_count);
    TEST_ASSERT_EQUAL(0, mocked_esp_console_deinit_fake.call_count);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, service->Start(service));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, console->ProcessCommand(console, "help"));
    mocked_xQueueReceive_fake.custom_fake = console_stop_ack;
    mocked_esp_console_deinit_fake.return_val = ESP_FAIL;
    TEST_ASSERT_EQUAL(ESP_FAIL, service->Stop(service));
    TEST_ASSERT_NULL(consoleService.consoleTaskHandle);
    TEST_ASSERT_TRUE(consoleService.sdkOwned);
    TEST_ASSERT_EQUAL(1, mocked_vTaskDelete_fake.call_count);
    mocked_esp_console_deinit_fake.return_val = ESP_OK;
    TEST_ASSERT_EQUAL(ESP_OK, service->Stop(service));
    TEST_ASSERT_FALSE(consoleService.sdkOwned);
    TEST_ASSERT_FALSE(consoleService.closing);
    TEST_ASSERT_EQUAL(SERVICE_STATE_STOPPED, service->GetState(service));
    TEST_ASSERT_EQUAL(1, mocked_vTaskDelete_fake.call_count);
}

void test_DebugConsole_FailedRollbackRequiresCleanupBeforeRestart(void) {
    setUp_DC();
    IDebugConsole *console = DebugConsole_GetModule();
    IService *service = (IService *)console;
    TEST_ASSERT_EQUAL(0, service->Initialize(service, NULL));
    IConsoleIO io = {.Initialize = test_console_io_initialize};
    consoleService.io = &io;
    RESET_FAKE(test_console_io_initialize);
    RESET_FAKE(mocked_esp_console_init);
    RESET_FAKE(mocked_esp_console_deinit);
    RESET_FAKE(mocked_xTaskCreate);
    mocked_esp_console_init_fake.return_val = ESP_OK;
    mocked_esp_console_deinit_fake.return_val = ESP_FAIL;
    mocked_xTaskCreate_fake.return_val = pdFAIL;
    TEST_ASSERT_EQUAL(ESP_ERR_NO_MEM, service->Start(service));
    TEST_ASSERT_TRUE(consoleService.sdkOwned);
    TEST_ASSERT_TRUE(consoleService.ioOwned);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, service->Start(service));
    TEST_ASSERT_EQUAL(1, mocked_xTaskCreate_fake.call_count);
    TEST_ASSERT_EQUAL(ESP_FAIL, service->Stop(service));
    TEST_ASSERT_TRUE(consoleService.ioOwned);
    mocked_esp_console_deinit_fake.return_val = ESP_OK;
    TEST_ASSERT_EQUAL(ESP_OK, service->Stop(service));
    TEST_ASSERT_FALSE(consoleService.sdkOwned);
    TEST_ASSERT_FALSE(consoleService.ioOwned);
    consoleService.io = NULL;
}

void test_DebugConsole_RegistrationFailureDoesNotCommitRegistry(void) {
    setUp_DC();
    IDebugConsole *console = DebugConsole_GetModule();
    esp_console_cmd_t cmd = {.command = "transaction", .help = "original"};
    mocked_esp_console_cmd_register_fake.return_val = ESP_FAIL;
    TEST_ASSERT_EQUAL(ESP_FAIL, console->RegisterCommand(console, &cmd));
    CommandEntry missing;
    TEST_ASSERT_FALSE(CommandRegistry_CopyByName("transaction", &missing));
    mocked_esp_console_cmd_register_fake.return_val = ESP_OK;
    TEST_ASSERT_EQUAL(ESP_OK, console->RegisterCommand(console, &cmd));
    cmd.help = "replacement";
    mocked_esp_console_cmd_register_fake.return_val = ESP_ERR_NO_MEM;
    TEST_ASSERT_EQUAL(ESP_ERR_NO_MEM, console->RegisterCommandWithCategory(console, &cmd, CMD_CAT_HARDWARE));
    CommandEntry entry_copy;
    const CommandEntry *entry = CommandRegistry_CopyByName("transaction", &entry_copy) ? &entry_copy : NULL;
    TEST_ASSERT_NOT_NULL(entry);
    TEST_ASSERT_EQUAL_STRING("original", entry->help);
    TEST_ASSERT_EQUAL(CMD_CAT_UNCATEGORIZED, entry->category);
    consoleService.closing = true;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, console->RegisterCommand(console, &cmd));
    TEST_ASSERT_EQUAL_STRING("original", entry->help);
    consoleService.closing = false;
    mocked_esp_console_cmd_register_fake.return_val = ESP_OK;
}

void test_CommandRegistry_CopiesSurviveReplacementAndRemoval(void) {
    setUp_DC();
    esp_console_cmd_t command = {.command = "stable", .help = "original"};
    TEST_ASSERT_TRUE(CommandRegistry_Register(&command, CMD_CAT_SYSTEM));
    CommandEntry named, matches[2];
    unsigned cursor = 0;
    TEST_ASSERT_TRUE(CommandRegistry_CopyByName("stable", &named));
    TEST_ASSERT_EQUAL(1, CommandRegistry_CopyMatches("sta", CMD_CAT_MAX, &cursor, matches, 2));
    command.help = "new";
    TEST_ASSERT_TRUE(CommandRegistry_Register(&command, CMD_CAT_HARDWARE));
    TEST_ASSERT_TRUE(CommandRegistry_Unregister("stable"));
    CommandRegistry_Clear();
    TEST_ASSERT_EQUAL_STRING("original", named.help);
    TEST_ASSERT_EQUAL_STRING("stable", matches[0].command);
    TEST_ASSERT_EQUAL_STRING("original", matches[0].help);
    TEST_ASSERT_EQUAL(CMD_CAT_SYSTEM, matches[0].category);
    cursor = 0;
    TEST_ASSERT_EQUAL(0, CommandRegistry_CopyMatches(NULL, CMD_CAT_MAX, &cursor, matches, 2));
}

void test_CommandRegistry_CopyFailurePreservesCallerStorage(void) {
    setUp_DC();
    CommandEntry copy = {.command = "unchanged"};
    unsigned cursor = 0;
    mocked_xSemaphoreTake_fake.return_val = pdFALSE;
    TEST_ASSERT_EQUAL(-1, CommandRegistry_CopyMatches(NULL, CMD_CAT_MAX, &cursor, &copy, 1));
    TEST_ASSERT_FALSE(CommandRegistry_CopyByName("any", &copy));
    TEST_ASSERT_FALSE(CommandRegistry_Init());
    TEST_ASSERT_EQUAL_UINT(0, cursor);
    TEST_ASSERT_EQUAL_STRING("unchanged", copy.command);
    mocked_xSemaphoreTake_fake.return_val = pdTRUE;
    TEST_ASSERT_EQUAL(-1, CommandRegistry_CopyMatches(NULL, (CommandCategory)-1, &cursor, &copy, 1));
    TEST_ASSERT_EQUAL(-1, CommandRegistry_CopyMatches(NULL, CMD_CAT_MAX, &cursor, &copy, 0));
}

void test_DebugConsole_PluginAdmissionFailureReleasesCounter(void) {
    setUp_DC();
    IDebugConsole *console = DebugConsole_GetModule();
    mocked_xSemaphoreTake_fake.return_val = pdFALSE;
    TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT, console->RegisterModule(console, "blocked", mock_plugin_register));
    TEST_ASSERT_EQUAL_UINT(0, consoleService.activeCommands);
    TEST_ASSERT_EQUAL(0, consoleService.pluginCount);
    mocked_xSemaphoreTake_fake.return_val = pdTRUE;
    consoleService.closing = true;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, console->RegisterModule(console, "late", mock_plugin_register));
    TEST_ASSERT_EQUAL(0, consoleService.pluginCount);
    consoleService.closing = false;
}

void test_SystemHelpCategoryRejectsInvalidIndex(void) {
    setUp_DC();
    SystemCommands_Register(DebugConsole_GetModule());
    CommandEntry command;
    TEST_ASSERT_TRUE(CommandRegistry_CopyByName("SYSTEM", &command));
    char *args[] = {"SYSTEM", "HELP_CAT", "-1"};
    const char *bad[] = {"-1", "8", "garbage", "1tail", "999999999999999999999999999"};
    for (size_t i = 0; i < sizeof(bad)/sizeof(bad[0]); ++i) {
        args[2] = (char *)bad[i];
        TEST_ASSERT_NOT_EQUAL(0, command.func(3, args));
    }
    args[2] = "0";
    TEST_ASSERT_EQUAL(0, command.func(3, args));
}

void test_DebugConsole_PluginSnapshotLockFailureRollsBackStart(void) {
    setUp_DC();
    IDebugConsole *console = DebugConsole_GetModule();
    IService *service = (IService *)console;
    TEST_ASSERT_EQUAL(0, service->Initialize(service, NULL));
    IConsoleIO io = {.Initialize = test_console_io_initialize};
    consoleService.io = &io;
    RESET_FAKE(test_console_io_initialize);
    RESET_FAKE(mocked_esp_console_init);
    RESET_FAKE(mocked_esp_console_deinit);
    RESET_FAKE(mocked_xTaskCreate);
    mocked_esp_console_init_fake.return_val = ESP_OK;
    mocked_esp_console_deinit_fake.return_val = ESP_OK;
    mocked_xSemaphoreTake_fake.return_val = pdFALSE;
    /* Exercise the owned body: the outer lifecycle gate is independently tested. */
    TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT, DebugConsole_StartOwned(service));
    TEST_ASSERT_FALSE(consoleService.sdkOwned);
    TEST_ASSERT_FALSE(consoleService.ioOwned);
    TEST_ASSERT_EQUAL(0, mocked_xTaskCreate_fake.call_count);
    TEST_ASSERT_EQUAL(1, mocked_esp_console_deinit_fake.call_count);
    mocked_xSemaphoreTake_fake.return_val = pdTRUE;
    consoleService.io = NULL;
}

void test_DebugConsole_StartPropagatesIgnoredModuleRegistrationError(void) {
    setUp_DC();
    IDebugConsole *console = DebugConsole_GetModule();
    IService *service = (IService *)console;
    TEST_ASSERT_EQUAL(0, service->Initialize(service, NULL));
    IConsoleIO io = {.Initialize = test_console_io_initialize};
    consoleService.io = &io;
    RESET_FAKE(test_console_io_initialize);
    RESET_FAKE(mocked_esp_console_init);
    RESET_FAKE(mocked_esp_console_deinit);
    RESET_FAKE(mocked_xTaskCreate);
    mocked_esp_console_init_fake.return_val = ESP_OK;
    mocked_esp_console_deinit_fake.return_val = ESP_OK;
    mocked_esp_console_cmd_register_fake.return_val = ESP_FAIL;
    TEST_ASSERT_EQUAL(ESP_FAIL, service->Start(service));
    TEST_ASSERT_FALSE(consoleService.sdkOwned);
    TEST_ASSERT_FALSE(consoleService.ioOwned);
    TEST_ASSERT_FALSE(consoleService.starting);
    TEST_ASSERT_EQUAL(SERVICE_STATE_INITIALIZED, service->GetState(service));
    TEST_ASSERT_EQUAL(0, mocked_xTaskCreate_fake.call_count);
    consoleService.io = NULL;
}

void test_DebugConsole_StartupRejectsForeignOperations(void) {
    setUp_DC();
    IDebugConsole *console = DebugConsole_GetModule();
    consoleService.startOwner = (TaskHandle_t)0x900;
    consoleService.starting = true;
    RESET_FAKE(mocked_xTaskGetCurrentTaskHandle);
    mocked_xTaskGetCurrentTaskHandle_fake.return_val = (TaskHandle_t)0x901;
    esp_console_cmd_t command = {.command = "foreign"};
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, console->RegisterCommand(console, &command));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, console->RegisterModule(console, "foreign", mock_plugin_register));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, console->ProcessCommand(console, "help"));
    TEST_ASSERT_EQUAL_UINT(0, consoleService.activeCommands);
    TEST_ASSERT_EQUAL(0, consoleService.pluginCount);
    consoleService.starting = false;
    consoleService.startOwner = NULL;
    RESET_FAKE(mocked_xTaskGetCurrentTaskHandle);
}

static void register_nested_plugin(IDebugConsole *console) {
    ++s_pluginRegistered;
    TEST_ASSERT_EQUAL(ESP_OK, console->RegisterModule(console, "nested", mock_plugin_register));
}

void test_DebugConsole_ReentrantStartupPluginsRegisterOncePerStart(void) {
    setUp_DC();
    IDebugConsole *console = DebugConsole_GetModule();
    IService *service = (IService *)console;
    TEST_ASSERT_EQUAL(0, service->Initialize(service, NULL));
    TEST_ASSERT_EQUAL(ESP_OK, console->RegisterModule(console, "parent", register_nested_plugin));
    IConsoleIO io = {.Initialize = test_console_io_initialize};
    consoleService.io = &io;
    RESET_FAKE(test_console_io_initialize);
    RESET_FAKE(mocked_esp_console_init);
    RESET_FAKE(mocked_esp_console_deinit);
    RESET_FAKE(mocked_xTaskCreate);
    mocked_esp_console_init_fake.return_val = ESP_OK;
    mocked_esp_console_deinit_fake.return_val = ESP_OK;
    mocked_xTaskCreate_fake.return_val = pdFAIL;
    s_pluginRegistered = 0;
    TEST_ASSERT_EQUAL(ESP_ERR_NO_MEM, service->Start(service));
    TEST_ASSERT_EQUAL(2, s_pluginRegistered);
    TEST_ASSERT_EQUAL(2, consoleService.pluginCount);
    TEST_ASSERT_EQUAL(ESP_ERR_NO_MEM, service->Start(service));
    TEST_ASSERT_EQUAL(4, s_pluginRegistered);
    TEST_ASSERT_EQUAL(2, consoleService.pluginCount);
    consoleService.io = NULL;
}

static int runtime_command_stub(int argc, char **argv) {
    (void)argc; (void)argv;
    TEST_FAIL_MESSAGE("Registration unexpectedly executed a command"); return 1;
}
static void runtime_failing_registration(IDebugConsole *console) {
    const esp_console_cmd_t command = {.command = "runtime_fault", .func = runtime_command_stub};
    (void)console->RegisterCommand(console, &command);
    /* The first error must survive a later, different registration error. */
    (void)console->RegisterCommand(console, NULL);
}
static void runtime_successful_registration(IDebugConsole *console) {
    const esp_console_cmd_t command = {.command = "runtime_ok", .func = runtime_command_stub};
    TEST_ASSERT_EQUAL(ESP_OK, console->RegisterCommand(console, &command));
}
void test_DebugConsole_RuntimeRegistrationPreservesIgnoredErrorsAndAllowsRetry(void) {
    setUp_DC();
    IDebugConsole *console = DebugConsole_GetModule();
    consoleService.state = SERVICE_STATE_STARTED;
    mocked_esp_console_cmd_register_fake.return_val = ESP_ERR_NO_MEM;
    TEST_ASSERT_EQUAL(ESP_ERR_NO_MEM, console->RegisterModule(console, "runtime", runtime_failing_registration));
    TEST_ASSERT_FALSE(consoleService.registeringModule);
    TEST_ASSERT_FALSE(consoleService.starting);
    TEST_ASSERT_EQUAL_UINT(0, consoleService.activeCommands);
    mocked_esp_console_cmd_register_fake.return_val = ESP_OK;
    TEST_ASSERT_EQUAL(ESP_OK, console->RegisterModule(console, "runtime", runtime_successful_registration));
    TEST_ASSERT_EQUAL_UINT(0, consoleService.activeCommands);
}
static void runtime_nested_registration(IDebugConsole *console) {
    TEST_ASSERT_TRUE(consoleService.registeringModule);
    TEST_ASSERT_FALSE(consoleService.starting);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, console->base.Stop(&console->base));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, console->base.Start(&console->base));
    TEST_ASSERT_EQUAL(ESP_OK, console->RegisterModule(console, "runtime_child", runtime_successful_registration));
}
void test_DebugConsole_RuntimeRegistrationNestedCallbackDoesNotRestartTransport(void) {
    setUp_DC();
    IDebugConsole *console = DebugConsole_GetModule();
    consoleService.state = SERVICE_STATE_STARTED;
    RESET_FAKE(mocked_esp_console_deinit);
    TEST_ASSERT_EQUAL(ESP_OK, console->RegisterModule(console, "runtime_parent", runtime_nested_registration));
    TEST_ASSERT_EQUAL(2, consoleService.pluginCount);
    TEST_ASSERT_EQUAL(0, mocked_esp_console_deinit_fake.call_count);
    TEST_ASSERT_EQUAL_UINT(0, consoleService.activeCommands);
    TEST_ASSERT_FALSE(consoleService.closing);
}
static BaseType_t reject_registration_lifecycle(SemaphoreHandle_t mutex, TickType_t timeout) {
    (void)timeout;
    return mutex == consoleService.lifecycleMutex ? pdFALSE : pdTRUE;
}
void test_DebugConsole_RuntimeRegistrationLifecycleContentionReturnsTimeout(void) {
    setUp_DC();
    IDebugConsole *console = DebugConsole_GetModule();
    consoleService.state = SERVICE_STATE_STARTED;
    consoleService.lifecycleMutex = (SemaphoreHandle_t)0x101;
    mocked_xSemaphoreTake_fake.custom_fake = reject_registration_lifecycle;
    TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT, console->RegisterModule(console, "blocked_runtime", runtime_successful_registration));
    TEST_ASSERT_EQUAL_UINT(0, consoleService.activeCommands);
    TEST_ASSERT_FALSE(consoleService.registeringModule);
    mocked_xSemaphoreTake_fake.custom_fake = NULL;
}
void test_DebugConsole_RegistrationRejectsForeignInterface(void) {
    setUp_DC();
    IDebugConsole *console = DebugConsole_GetModule();
    IDebugConsole foreign = {0};
    const esp_console_cmd_t command = {.command = "foreign", .func = runtime_command_stub};
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, console->RegisterCommand(&foreign, &command));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, console->RegisterCommandWithCategory(&foreign, &command, CMD_CAT_SYSTEM));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, console->RegisterModule(&foreign, "foreign", runtime_successful_registration));
    TEST_ASSERT_EQUAL_UINT(0, consoleService.activeCommands);
}

static int (*temperature_hw_command)(int,char **);
static unsigned temperature_hw_writes;
static int32_t temperature_hw_result;
static int32_t temperature_hw_offset(IInternalTemperatureSensor *self,float value) {
    (void)self;TEST_ASSERT_FLOAT_WITHIN(0.0001f,0.1f,value);
    ++temperature_hw_writes;return temperature_hw_result;
}
static IInternalTemperatureSensor temperature_hw_sensor={.SetOffset=temperature_hw_offset};
static IService *temperature_hw_service(const char *name) {
    return !strcmp(name,SVC_NAME_INTERNAL_TEMP_SENSOR) ? &temperature_hw_sensor.base : NULL;
}
static esp_err_t temperature_hw_register(IDebugConsole *self,const esp_console_cmd_t *cmd,CommandCategory category) {
    (void)self;(void)category;temperature_hw_command=cmd->func;return ESP_OK;
}

static IRGBLED hardware_rgb;
static IPinManager hardware_pin;
static int32_t hardware_pin_result;
static int32_t hardware_pin_dump(IPinManager *self) {
    (void)self; return hardware_pin_result;
}
static II2CManager hardware_i2c;
static IInfraredTemperatureSensor hardware_ir;
static ServiceState hardware_ir_state;
static int32_t hardware_stop_result, hardware_start_result;
static esp_err_t hardware_scan_result, hardware_rgb_result;
static unsigned hardware_stop_calls, hardware_start_calls, hardware_scan_calls, hardware_rgb_calls;
static esp_err_t hardware_rgb_off(IRGBLED *self) {
    (void)self; ++hardware_rgb_calls; return hardware_rgb_result;
}
static ServiceState hardware_ir_get_state(IService *self) {
    (void)self; return hardware_ir_state;
}
static int32_t hardware_ir_stop(IService *self) {
    (void)self; ++hardware_stop_calls; return hardware_stop_result;
}
static int32_t hardware_ir_start(IService *self) {
    (void)self; ++hardware_start_calls; return hardware_start_result;
}
static esp_err_t hardware_scan(II2CManager *self, i2c_port_t port) {
    (void)self; TEST_ASSERT_EQUAL(0, port); ++hardware_scan_calls; return hardware_scan_result;
}
static IService *hardware_boundary_service(const char *name) {
    if (!strcmp(name,SVC_NAME_PIN_MANAGER)) return &hardware_pin.base;
    if (!strcmp(name,SVC_NAME_RGB_LED)) return &hardware_rgb.base;
    if (!strcmp(name,SVC_NAME_I2C_MANAGER)) return &hardware_i2c.base;
    if (!strcmp(name,SVC_NAME_IR_TEMP_SENSOR)) return &hardware_ir.base;
    return NULL;
}
static void setup_hardware_boundaries(void) {
    setUp_DC();
    IDebugConsole console={.RegisterCommandWithCategory=temperature_hw_register};
    HardwareCommands_Register(&console);
    memset(&hardware_rgb,0,sizeof(hardware_rgb));
    memset(&hardware_ir,0,sizeof(hardware_ir));
    memset(&hardware_i2c,0,sizeof(hardware_i2c));
    memset(&hardware_pin,0,sizeof(hardware_pin));
    hardware_pin.DumpPinMap=hardware_pin_dump;
    hardware_pin_result=0;
    hardware_stop_calls=hardware_start_calls=hardware_scan_calls=hardware_rgb_calls=0;
    hardware_stop_result=hardware_start_result=0;
    hardware_scan_result=hardware_rgb_result=ESP_OK;
    hardware_rgb.TurnOff=hardware_rgb_off;
    hardware_ir.base.GetState=hardware_ir_get_state;
    hardware_ir.base.Stop=hardware_ir_stop;
    hardware_ir.base.Start=hardware_ir_start;
    hardware_i2c.Scan=hardware_scan;
    mocked_ServiceLocator_GetService_fake.custom_fake=hardware_boundary_service;
}
void test_DebugConsole_HardwareRgbRejectsUnknownInputAndQueueFailure(void) {
    setup_hardware_boundaries();
    char *unknown[]={"HW","RGB","unsupported"};
    TEST_ASSERT_NOT_EQUAL(0,temperature_hw_command(3,unknown));
    TEST_ASSERT_EQUAL(0,hardware_rgb_calls);
    char *off[]={"HW","RGB","OFF","--machine"};
    hardware_rgb_result=ESP_FAIL;
    TEST_ASSERT_NOT_EQUAL(0,temperature_hw_command(4,off));
    hardware_rgb_result=ESP_OK;
    TEST_ASSERT_EQUAL(0,temperature_hw_command(4,off));
    hardware_rgb.TurnOff=NULL;
    TEST_ASSERT_NOT_EQUAL(0,temperature_hw_command(4,off));
    TEST_ASSERT_EQUAL(2,hardware_rgb_calls);
    TEST_ASSERT_NOT_EQUAL(0,temperature_hw_command(4,NULL));
    mocked_ServiceLocator_GetService_fake.custom_fake=NULL;
}
void test_DebugConsole_HardwarePinsPropagatesFailedSnapshot(void) {
    setup_hardware_boundaries();
    char *pins[]={"HW","PINS","--machine"};
    hardware_pin_result=-4;
    TEST_ASSERT_NOT_EQUAL(0,temperature_hw_command(3,pins));
    hardware_pin_result=0;
    TEST_ASSERT_EQUAL(0,temperature_hw_command(3,pins));
    hardware_pin.DumpPinMap=NULL;
    TEST_ASSERT_NOT_EQUAL(0,temperature_hw_command(3,pins));
    mocked_ServiceLocator_GetService_fake.custom_fake=NULL;
}
void test_DebugConsole_HardwareScanRestoresOnlyOriginallyRunningSensor(void) {
    setup_hardware_boundaries();
    char *scan[]={"HW","SCAN","0","--machine"};
    hardware_ir_state=SERVICE_STATE_STOPPED;
    TEST_ASSERT_EQUAL(0,temperature_hw_command(4,scan));
    TEST_ASSERT_EQUAL(0,hardware_stop_calls);
    TEST_ASSERT_EQUAL(0,hardware_start_calls);
    hardware_ir_state=SERVICE_STATE_STARTED;
    hardware_scan_result=ESP_FAIL;
    TEST_ASSERT_NOT_EQUAL(0,temperature_hw_command(4,scan));
    TEST_ASSERT_EQUAL(1,hardware_stop_calls);
    TEST_ASSERT_EQUAL(1,hardware_start_calls);
    hardware_stop_result=-1;
    TEST_ASSERT_NOT_EQUAL(0,temperature_hw_command(4,scan));
    TEST_ASSERT_EQUAL(2,hardware_scan_calls);
    TEST_ASSERT_EQUAL(1,hardware_start_calls);
    scan[2]="999999999999";
    TEST_ASSERT_NOT_EQUAL(0,temperature_hw_command(4,scan));
    TEST_ASSERT_EQUAL(2,hardware_stop_calls);
    mocked_ServiceLocator_GetService_fake.custom_fake=NULL;
}
void test_DebugConsole_HardwareTemperatureRejectsInvalidNumbersBeforeMutation(void) {
    setUp_DC();temperature_hw_writes=0;
    IDebugConsole console={.RegisterCommandWithCategory=temperature_hw_register};
    HardwareCommands_Register(&console);
    mocked_ServiceLocator_GetService_fake.custom_fake=temperature_hw_service;
    char *offset[]={"HW","TEMP","OFFSET","nan"};
    TEST_ASSERT_NOT_EQUAL(0,temperature_hw_command(4,offset));
    offset[3]="0.1junk";TEST_ASSERT_NOT_EQUAL(0,temperature_hw_command(4,offset));
    char *poll[]={"HW","TEMP","POLL","-1"};
    TEST_ASSERT_NOT_EQUAL(0,temperature_hw_command(4,poll));
    poll[3]="429496729600";TEST_ASSERT_NOT_EQUAL(0,temperature_hw_command(4,poll));
    char *limits[]={"HW","TEMP","LIMITS","-20","65536"};
    TEST_ASSERT_NOT_EQUAL(0,temperature_hw_command(5,limits));
    TEST_ASSERT_EQUAL(0,temperature_hw_writes);
    mocked_ServiceLocator_GetService_fake.custom_fake=NULL;
}
void test_DebugConsole_HardwareTemperaturePropagatesOffsetFailure(void) {
    setUp_DC();temperature_hw_writes=0;temperature_hw_result=-7;
    IDebugConsole console={.RegisterCommandWithCategory=temperature_hw_register};
    HardwareCommands_Register(&console);
    mocked_ServiceLocator_GetService_fake.custom_fake=temperature_hw_service;
    char *offset[]={"HW","TEMP","OFFSET","0.1"};
    TEST_ASSERT_NOT_EQUAL(0,temperature_hw_command(4,offset));
    TEST_ASSERT_EQUAL(1,temperature_hw_writes);
    temperature_hw_result=0;
    TEST_ASSERT_EQUAL(0,temperature_hw_command(4,offset));
    TEST_ASSERT_EQUAL(2,temperature_hw_writes);
    mocked_ServiceLocator_GetService_fake.custom_fake=NULL;
}

#include "Management/ApplicationManager/IApplicationManager.h"
static unsigned factory_settings_calls, factory_network_calls, factory_coordinator_calls;
static esp_err_t factory_settings_result;
static int32_t factory_network_result;
static ServiceState factory_state(IService *self) {(void)self;return SERVICE_STATE_STARTED;}
static esp_err_t factory_defaults(IConfigurationManager *self,const char *block) {
    (void)self;TEST_ASSERT_NULL(block);++factory_settings_calls;return factory_settings_result;
}
static int32_t factory_network(IZigbee *self) {(void)self;++factory_network_calls;return factory_network_result;}
static IZigbee factory_radio={.base={.GetState=factory_state},.ResetToFactorySettings=factory_network};
static esp_err_t factory_coordinator(IApplicationManager *self) {
    (void)self; ++factory_coordinator_calls; return factory_network_result;
}
static IApplicationManager factory_app = {.TriggerFactoryReset = factory_coordinator};
static IService *factory_service(const char *name) {
    if (!strcmp(name,SVC_NAME_APPLICATION_MANAGER)) return &factory_app.base;
    return !strcmp(name,SVC_NAME_ZIGBEE) ? &factory_radio.base : NULL;
}
static void factory_begin(void) {
    CommandEntry command;TEST_ASSERT_TRUE(CommandRegistry_CopyByName("SYSTEM",&command));
    char *args[]={"SYSTEM","FACTORY_RESET"};TEST_ASSERT_EQUAL(0,command.func(2,args));
}
void test_SystemFactoryResetForwardsAdmissionAndNeverBypassesCoordinator(void) {
    setUp_DC();factory_settings_calls=factory_network_calls=0;factory_settings_result=ESP_FAIL;
    IConfigurationManager manager={.ResetToDefaults=factory_defaults};
    mocked_ConfigurationManager_GetInstance_fake.return_val=&manager;
    mocked_ServiceLocator_GetService_fake.custom_fake=factory_service;
    factory_coordinator_calls=0;factory_network_result=ESP_FAIL;
    RESET_FAKE(mocked_nvs_flash_erase);RESET_FAKE(mocked_esp_restart);
    SystemCommands_Register(DebugConsole_GetModule());factory_begin();
    TEST_ASSERT_TRUE(SystemCommands_InterceptInput("yes"));
    TEST_ASSERT_EQUAL(1,factory_coordinator_calls);TEST_ASSERT_EQUAL(0,factory_settings_calls);TEST_ASSERT_EQUAL(0,factory_network_calls);
    factory_settings_result=ESP_OK;factory_network_result=-7;factory_begin();
    TEST_ASSERT_TRUE(SystemCommands_InterceptInput("y"));
    TEST_ASSERT_EQUAL(2,factory_coordinator_calls);TEST_ASSERT_EQUAL(0,factory_settings_calls);TEST_ASSERT_EQUAL(0,factory_network_calls);
    factory_network_result=0;factory_begin();TEST_ASSERT_TRUE(SystemCommands_InterceptInput("y"));
    TEST_ASSERT_EQUAL(3,factory_coordinator_calls);TEST_ASSERT_EQUAL(0,factory_settings_calls);TEST_ASSERT_EQUAL(0,factory_network_calls);
    TEST_ASSERT_EQUAL(0,mocked_nvs_flash_erase_fake.call_count);TEST_ASSERT_EQUAL(0,mocked_esp_restart_fake.call_count);
    mocked_ServiceLocator_GetService_fake.custom_fake=NULL;mocked_ConfigurationManager_GetInstance_fake.return_val=NULL;
}
void test_SystemFactoryResetRejectsExtraArgumentsAndCancellationDoesNotMutate(void) {
    setUp_DC();factory_settings_calls=factory_network_calls=0;
    SystemCommands_Register(DebugConsole_GetModule());
    CommandEntry command;TEST_ASSERT_TRUE(CommandRegistry_CopyByName("SYSTEM",&command));
    char *args[]={"SYSTEM","FACTORY_RESET","extra"};
    TEST_ASSERT_NOT_EQUAL(0,command.func(3,args));TEST_ASSERT_FALSE(SystemCommands_InterceptInput("yes"));
    TEST_ASSERT_FALSE(SystemCommands_InterceptInput(NULL));
    factory_begin();TEST_ASSERT_TRUE(SystemCommands_InterceptInput("n"));
    TEST_ASSERT_FALSE(SystemCommands_InterceptInput("y"));
    TEST_ASSERT_EQUAL(0,factory_settings_calls);TEST_ASSERT_EQUAL(0,factory_network_calls);
}

void test_SystemFactoryResetUnavailableOwnerDoesNotResetSettings(void) {
    setUp_DC();factory_settings_calls=factory_network_calls=0;
    IConfigurationManager manager={.ResetToDefaults=factory_defaults};
    mocked_ConfigurationManager_GetInstance_fake.return_val=&manager;
    mocked_ServiceLocator_GetService_fake.return_val=NULL;
    mocked_ServiceLocator_GetService_fake.custom_fake=NULL;
    SystemCommands_Register(DebugConsole_GetModule());factory_begin();
    TEST_ASSERT_TRUE(SystemCommands_InterceptInput("yes"));
    TEST_ASSERT_EQUAL(0,factory_settings_calls);TEST_ASSERT_EQUAL(0,factory_network_calls);
    mocked_ConfigurationManager_GetInstance_fake.return_val=NULL;
}

#include "Management/PowerManager/IPowerManager.h"
static unsigned system_restart_requests;
static int32_t system_restart_result;
static bool system_restart_pending;
static int32_t system_checked_restart(IPowerManager *self, uint32_t delay) {
    (void)self; TEST_ASSERT_EQUAL(DEBUG_CONSOLE_RESTART_DELAY_MS, delay);
    ++system_restart_requests;
    if (system_restart_result == POWER_OK) system_restart_pending = true;
    return system_restart_result;
}
static bool system_pending(IPowerManager *self) { (void)self; return system_restart_pending; }
static IPowerManager system_power = {.RestartAsync = system_checked_restart, .IsTransitionPending = system_pending};
static IService *system_power_resolve(const char *name) {
    return !strcmp(name, SVC_NAME_POWER_MANAGER) ? &system_power.base : NULL;
}
void test_SystemRestartDelegatesCheckedOwnerAndPreservesSingleFlight(void) {
    setUp_DC(); SystemCommands_Register(DebugConsole_GetModule());
    mocked_ServiceLocator_GetService_fake.custom_fake = system_power_resolve;
    system_restart_requests = 0; system_restart_pending = false;
    system_restart_result = POWER_ERR_BUSY;
    RESET_FAKE(mocked_esp_restart);
    CommandEntry command; TEST_ASSERT_TRUE(CommandRegistry_CopyByName("SYSTEM", &command));
    char *args[] = {"SYSTEM", "RESET", "extra"};
    TEST_ASSERT_NOT_EQUAL(0, command.func(3, args)); TEST_ASSERT_FALSE(SystemCommands_InterceptInput("yes"));
    TEST_ASSERT_EQUAL(0, command.func(2, args)); TEST_ASSERT_TRUE(SystemCommands_InterceptInput("y"));
    TEST_ASSERT_EQUAL(1, system_restart_requests); TEST_ASSERT_FALSE(system_restart_pending);
    system_restart_result = POWER_OK;
    TEST_ASSERT_EQUAL(0, command.func(2, args)); TEST_ASSERT_TRUE(SystemCommands_InterceptInput("yes"));
    TEST_ASSERT_EQUAL(2, system_restart_requests); TEST_ASSERT_TRUE(system_restart_pending);
    TEST_ASSERT_NOT_EQUAL(0, command.func(2, args)); TEST_ASSERT_FALSE(SystemCommands_InterceptInput("y"));
    TEST_ASSERT_EQUAL(0, mocked_esp_restart_fake.call_count);
    system_restart_pending = false;
    TEST_ASSERT_EQUAL(0, command.func(2, args)); TEST_ASSERT_TRUE(SystemCommands_InterceptInput("n"));
    TEST_ASSERT_EQUAL(2, system_restart_requests);
    mocked_ServiceLocator_GetService_fake.custom_fake = NULL;
}

void test_SystemRejectedAliasFeatureIsNotAdvertised(void) {
    setUp_DC();SystemCommands_Register(DebugConsole_GetModule());
    CommandEntry command;TEST_ASSERT_FALSE(CommandRegistry_CopyByName("ALIAS",&command));
    TEST_ASSERT_TRUE(CommandRegistry_CopyByName("SYSTEM",&command));
    char *args[]={"SYSTEM","ALIAS","quick","STATUS"};
    TEST_ASSERT_NOT_EQUAL(0,command.func(4,args));
}
void test_SystemLoggingRejectsInvalidArityAndNumericInput(void) {
    setUp_DC();SystemCommands_Register(DebugConsole_GetModule());
    CommandEntry logs;TEST_ASSERT_TRUE(CommandRegistry_CopyByName("LOGS",&logs));
    char *args[]={"LOGS","tail","99999999999999999999999"};TEST_ASSERT_NOT_EQUAL(0,logs.func(3,args));
    args[2]="1junk";TEST_ASSERT_NOT_EQUAL(0,logs.func(3,args));
    args[2]="0";TEST_ASSERT_NOT_EQUAL(0,logs.func(3,args));
    CommandEntry debug;TEST_ASSERT_TRUE(CommandRegistry_CopyByName("DEBUG",&debug));
    char *level[]={"DEBUG","LEVEL","tag","INFO","extra"};TEST_ASSERT_NOT_EQUAL(0,debug.func(5,level));
}

void test_ConsoleDisplayControlsRejectLockFailureWithoutMutation(void) {
    setUp_DC();IDebugConsole *console=DebugConsole_GetModule();
    consoleService.isLiveMode=false;consoleService.logsMuted=false;
    mocked_xSemaphoreTake_fake.return_val=pdFALSE;
    TEST_ASSERT_NOT_EQUAL(ESP_OK,console->SetLiveMode(console,true));
    TEST_ASSERT_NOT_EQUAL(ESP_OK,console->SetMuteLog(console,true));
    TEST_ASSERT_FALSE(consoleService.isLiveMode);TEST_ASSERT_FALSE(consoleService.logsMuted);
    mocked_xSemaphoreTake_fake.return_val=pdTRUE;
    TEST_ASSERT_EQUAL(ESP_OK,console->SetLiveMode(console,true));
    TEST_ASSERT_TRUE(consoleService.isLiveMode);
    TEST_ASSERT_EQUAL(ESP_OK,console->SetLiveMode(console,false));
}

void test_HistoryOwnedSnapshotSurvivesOverwriteAndRejectsLockFailure(void) {
    setUp_DC();IDebugConsole *console=DebugConsole_GetModule();
    TEST_ASSERT_TRUE(HistoryManager_Add("first"));TEST_ASSERT_TRUE(HistoryManager_Add("second"));
    char output[DEBUG_CONSOLE_HISTORY_SIZE][DEBUG_CONSOLE_LINE_BUFFER_SIZE];int count=-1;
    TEST_ASSERT_EQUAL(ESP_OK,console->CopyCommandHistory(console,output,DEBUG_CONSOLE_HISTORY_SIZE,&count));
    TEST_ASSERT_EQUAL(2,count);TEST_ASSERT_EQUAL_STRING("first",output[0]);
    HistoryManager_Clear();HistoryManager_Add("replacement");
    TEST_ASSERT_EQUAL_STRING("first",output[0]);TEST_ASSERT_EQUAL_STRING("second",output[1]);
    mocked_xSemaphoreTake_fake.return_val=pdFALSE;
    TEST_ASSERT_NOT_EQUAL(ESP_OK,console->CopyCommandHistory(console,output,DEBUG_CONSOLE_HISTORY_SIZE,&count));
    TEST_ASSERT_EQUAL(2,count);TEST_ASSERT_EQUAL_STRING("first",output[0]);
}
void test_HistoryOwnedNavigationCopiesLineAndPreservesOnFailure(void) {
    setUp_DC();HistoryManager_Add("one");HistoryManager_Add("two");
    char line[DEBUG_CONSOLE_LINE_BUFFER_SIZE];
    TEST_ASSERT_TRUE(HistoryManager_CopyNavigation(true,line,sizeof(line)));TEST_ASSERT_EQUAL_STRING("two",line);
    HistoryManager_Add("three");TEST_ASSERT_EQUAL_STRING("two",line);
    mocked_xSemaphoreTake_fake.return_val=pdFALSE;
    TEST_ASSERT_FALSE(HistoryManager_CopyNavigation(true,line,sizeof(line)));TEST_ASSERT_EQUAL_STRING("two",line);
    mocked_xSemaphoreTake_fake.return_val=pdTRUE;
    TEST_ASSERT_TRUE(HistoryManager_CopyNavigation(true,line,sizeof(line)));TEST_ASSERT_EQUAL_STRING("three",line);
    TEST_ASSERT_TRUE(HistoryManager_CopyNavigation(false,line,sizeof(line)));TEST_ASSERT_EQUAL_STRING("",line);
}
void test_SystemLiveAndMonitorRejectUnknownActions(void) {
    setUp_DC();SystemCommands_Register(DebugConsole_GetModule());CommandEntry debug;
    TEST_ASSERT_TRUE(CommandRegistry_CopyByName("DEBUG",&debug));
    CommandEntry system;TEST_ASSERT_TRUE(CommandRegistry_CopyByName("SYSTEM",&system));
    char *live[]={"SYSTEM","LIVE","invalid"};TEST_ASSERT_NOT_EQUAL(0,system.func(3,live));
    char *logs[]={"DEBUG","MONITOR","invalid"};TEST_ASSERT_NOT_EQUAL(0,debug.func(3,logs));
    logs[1]="STOP";TEST_ASSERT_NOT_EQUAL(0,debug.func(3,logs));
}

void test_ConsoleMuteRequiresSdkReadbackBeforeAppliedState(void) {
    extern int test_console_log_level_override;
    setUp_DC();IDebugConsole *console=DebugConsole_GetModule();consoleService.logsMuted=false;
    test_console_log_level_override=ESP_LOG_INFO;
    TEST_ASSERT_NOT_EQUAL(ESP_OK,console->SetMuteLog(console,true));TEST_ASSERT_FALSE(consoleService.logsMuted);
    test_console_log_level_override=-1;
    TEST_ASSERT_EQUAL(ESP_OK,console->SetMuteLog(console,true));TEST_ASSERT_TRUE(consoleService.logsMuted);
    TEST_ASSERT_EQUAL(ESP_OK,console->SetMuteLog(console,false));TEST_ASSERT_FALSE(consoleService.logsMuted);
}

void test_InputParser_PrivateEditingNeverEchoesArguments(void) {
    setUp_DC();
    FILE *capture = tmpfile();
    TEST_ASSERT_NOT_NULL(capture);
    fflush(stdout);
    int saved = privacy_dup(privacy_fileno(stdout));
    if (saved < 0) { fclose(capture); TEST_FAIL_MESSAGE("Cannot preserve stdout"); }
    if (privacy_dup2(privacy_fileno(capture), privacy_fileno(stdout)) < 0) {
        privacy_close(saved); fclose(capture); TEST_FAIL_MESSAGE("Cannot capture stdout");
    }
    char line[256] = {0};
    const char *command = "sec auth NEVER_ECHO_9";
    for (const char *c = command; *c; ++c) InputParser_ProcessChar(*c,line,sizeof(line));
    const char *editing = "\x1b[D\x1b[F\x01\x05\b9";
    for (const char *c = editing; *c; ++c) InputParser_ProcessChar(*c,line,sizeof(line));
    InputParser_ProcessChar('\t',line,sizeof(line));
    InputParser_SetLine(line,line,sizeof(line));
    InputParserEvent ready = InputParser_ProcessChar('\r',line,sizeof(line));
    char delivered[256]; memcpy(delivered,line,sizeof(delivered));
    InputParser_ProcessChar('\t',line,sizeof(line));
    InputParser_SetLine("\"sec\" auth NEVER_ECHO_9",line,sizeof(line));
    InputParser_ProcessChar(3,line,sizeof(line));
    InputParser_ProcessChar('\t',line,sizeof(line));
    fflush(stdout);
    privacy_dup2(saved,privacy_fileno(stdout)); privacy_close(saved);
    rewind(capture); char rendered[2048] = {0}; fread(rendered,1,sizeof(rendered)-1,capture); fclose(capture);
    TEST_ASSERT_EQUAL(INPUT_EVENT_LINE_READY,ready);
    TEST_ASSERT_EQUAL_STRING(command,delivered);
    TEST_ASSERT_NULL(strstr(rendered,"NEVER_ECHO_9"));
    TEST_ASSERT_NULL(strstr(rendered,"auth"));
    TEST_ASSERT_NOT_NULL(strchr(rendered,'*'));
    TEST_ASSERT_EQUAL_STRING("",line);
}

void test_InputParser_InvalidOutputDoesNotChangeInput(void) {
    setUp_DC(); char line[32] = {0};
    InputParser_ProcessChar('a',line,sizeof(line));
    TEST_ASSERT_EQUAL(INPUT_EVENT_NONE,InputParser_ProcessChar('b',NULL,sizeof(line)));
    TEST_ASSERT_EQUAL(INPUT_EVENT_NONE,InputParser_ProcessChar('b',line,0));
    InputParser_SetLine("changed",line,0);
    TEST_ASSERT_EQUAL(INPUT_EVENT_LINE_READY,InputParser_ProcessChar('\r',line,sizeof(line)));
    TEST_ASSERT_EQUAL_STRING("a",line);
}

void test_DiagnosticCommands_NullTokensAndMissingInterfacesAreRejected(void) {
    setUp_DC();
    TEST_ASSERT_EQUAL(ESP_OK, DiagnosticCommands_Register(DebugConsole_GetModule()));
    CommandEntry command;
    TEST_ASSERT_TRUE(CommandRegistry_CopyByName("DIAG", &command));
    char *missing[] = {"DIAG", NULL};
    char *empty[] = {"DIAG", ""};
    char *nullInterval[] = {"DIAG", "PERIODIC", "SET_INTERVAL", NULL};
    char *nullAction[] = {"DIAG", "PERIODIC", NULL};
    TEST_ASSERT_NOT_EQUAL(0, command.func(2, NULL));
    TEST_ASSERT_NOT_EQUAL(0, command.func(2, missing));
    TEST_ASSERT_NOT_EQUAL(0, command.func(2, empty));
    TEST_ASSERT_NOT_EQUAL(0, command.func(4, nullInterval));
    TEST_ASSERT_NOT_EQUAL(0, command.func(3, nullAction));
    IDiagnosticsManager partial = {0};
    mocked_ServiceLocator_GetService_fake.custom_fake = NULL;
    mocked_ServiceLocator_GetService_fake.return_val = (IService *)&partial;
    char *actions[] = {"RUN", "STATUS", "METRICS", "PERIODIC"};
    for (unsigned i = 0; i < 4; ++i) {
        char *args[] = {"DIAG", actions[i], "ENABLE"};
        TEST_ASSERT_NOT_EQUAL(0, command.func(i == 3 ? 3 : 2, args));
    }
    mocked_ServiceLocator_GetService_fake.return_val = NULL;
}

static unsigned language_write_calls;
static esp_err_t language_write_result;
static esp_err_t save_language(IConfigurationManager *self, const char *block,
                               const ConfigurationFieldUpdate *fields, size_t count, bool persist) {
    (void)self;
    ++language_write_calls;
    TEST_ASSERT_EQUAL_STRING("localization", block);
    TEST_ASSERT_TRUE(persist);
    TEST_ASSERT_EQUAL(1, count);
    TEST_ASSERT_EQUAL(offsetof(LocalizationConfigs, locale), fields[0].offset);
    TEST_ASSERT_EQUAL(LOCALIZATION_LOCALE_CAPACITY, fields[0].size);
    TEST_ASSERT_EQUAL_STRING("en", fields[0].data);
    if (language_write_result == ESP_OK) TEST_ASSERT_TRUE(Localization_Apply(fields[0].data));
    return language_write_result;
}
void test_LanguageCommandChecksCatalogAndPersistsBeforeApplying(void) {
    setUp_DC();
    TEST_ASSERT_TRUE(Localization_Apply("tr"));
    IConfigurationManager manager = {.UpdateFields = save_language};
    mocked_ServiceLocator_GetService_fake.return_val = (IService *)&manager;
    mocked_ServiceLocator_GetService_fake.custom_fake = NULL;
    SystemCommands_Register(DebugConsole_GetModule());
    CommandEntry command;
    TEST_ASSERT_TRUE(CommandRegistry_CopyByName("LANGUAGE", &command));
    char *args[] = {"LANGUAGE", "SET", "en", "extra"};
    language_write_calls = 0;
    TEST_ASSERT_NOT_EQUAL(0, command.func(3, NULL));
    TEST_ASSERT_NOT_EQUAL(0, command.func(4, args));
    args[2] = "missing";
    TEST_ASSERT_NOT_EQUAL(0, command.func(3, args));
    args[2] = "EN";
    TEST_ASSERT_NOT_EQUAL(0, command.func(3, args));
    TEST_ASSERT_EQUAL(0, language_write_calls);
    args[2] = "en"; language_write_result = ESP_FAIL;
    TEST_ASSERT_NOT_EQUAL(0, command.func(3, args));
    TEST_ASSERT_EQUAL_STRING("tr", Localization_Snapshot()->code);
    language_write_result = ESP_OK;
    TEST_ASSERT_EQUAL(0, command.func(3, args));
    TEST_ASSERT_EQUAL_STRING("en", Localization_Snapshot()->code);
    TEST_ASSERT_EQUAL(2, language_write_calls);
    TEST_ASSERT_EQUAL(0, command.func(1, args));
    args[1] = "LIST";
    TEST_ASSERT_EQUAL(0, command.func(2, args));
    TEST_ASSERT_EQUAL(2, language_write_calls);
    args[1] = "SET";
    mocked_ServiceLocator_GetService_fake.return_val = NULL;
    TEST_ASSERT_NOT_EQUAL(0, command.func(3, args));
    TEST_ASSERT_TRUE(Localization_Apply("tr"));
}

static esp_err_t language_snapshot_result;
static const char *language_saved_locale;
static esp_err_t language_snapshot(IConfigurationManager *self, SystemSettings *output) {
    (void)self;
    if (language_snapshot_result != ESP_OK) return language_snapshot_result;
    memset(output, 0, sizeof(*output));
    output->localization.version = 1;
    snprintf(output->localization.locale, sizeof(output->localization.locale), "%s", language_saved_locale);
    return ESP_OK;
}
void test_LanguageMachineReadbackIsLocaleIndependentAndReadOnly(void) {
    setUp_DC();
    IConfigurationManager manager={.UpdateFields=save_language,.GetSnapshot=language_snapshot};
    mocked_ServiceLocator_GetService_fake.return_val=(IService *)&manager;
    mocked_ServiceLocator_GetService_fake.custom_fake=NULL;
    SystemCommands_Register(DebugConsole_GetModule());
    CommandEntry command;TEST_ASSERT_TRUE(CommandRegistry_CopyByName("LANGUAGE",&command));
    FILE *capture=tmpfile();TEST_ASSERT_NOT_NULL(capture);
    fflush(stdout);int saved=privacy_dup(privacy_fileno(stdout));
    if(saved<0){fclose(capture);TEST_FAIL_MESSAGE("Cannot preserve stdout");}
    if(privacy_dup2(privacy_fileno(capture),privacy_fileno(stdout))<0){
        privacy_close(saved);fclose(capture);TEST_FAIL_MESSAGE("Cannot capture stdout");
    }
    char *args[]={"LANGUAGE","--machine","extra"};language_write_calls=0;
    Localization_Apply("tr");int tr=command.func(2,args);
    Localization_Apply("en");int en=command.func(2,args);
    int extra=command.func(3,args);
    char *config_args[]={"LANGUAGE","CONFIG","--machine"};
    language_saved_locale="de";language_snapshot_result=ESP_OK;
    int fallback=command.func(3,config_args);
    language_snapshot_result=ESP_FAIL;int unavailable=command.func(3,config_args);
    char *list_args[]={"LANGUAGE","LIST","--machine"};int listing=command.func(3,list_args);
    char *set_args[]={"LANGUAGE","SET","missing","--machine"};int invalid=command.func(4,set_args);
    set_args[2]="en";language_write_result=ESP_FAIL;int failed=command.func(4,set_args);
    language_write_result=ESP_OK;int applied=command.func(4,set_args);
    fflush(stdout);privacy_dup2(saved,privacy_fileno(stdout));privacy_close(saved);
    rewind(capture);char output[2048]={0};fread(output,1,sizeof(output)-1,capture);fclose(capture);
    TEST_ASSERT_EQUAL(0,tr);TEST_ASSERT_EQUAL(0,en);TEST_ASSERT_NOT_EQUAL(0,extra);
    TEST_ASSERT_EQUAL(2,language_write_calls);
    TEST_ASSERT_EQUAL(0,fallback);TEST_ASSERT_NOT_EQUAL(0,unavailable);
    TEST_ASSERT_EQUAL(0,listing);TEST_ASSERT_NOT_EQUAL(0,invalid);
    TEST_ASSERT_NOT_EQUAL(0,failed);TEST_ASSERT_EQUAL(0,applied);
    TEST_ASSERT_NOT_NULL(strstr(output,"type=language_config saved=de active=en restorable=0\n"));
    TEST_ASSERT_NOT_NULL(strstr(output,"type=language_config result=unavailable\n"));
    TEST_ASSERT_NOT_NULL(strstr(output,"type=language_catalog_end count=2\n"));
    TEST_ASSERT_NOT_NULL(strstr(output,"type=language_set result=invalid_locale\n"));
    TEST_ASSERT_NOT_NULL(strstr(output,"type=language_set result=save_failed error="));
    TEST_ASSERT_NOT_NULL(strstr(output,"type=language_set result=applied locale=en\n"));
    TEST_ASSERT_NOT_NULL(strstr(output,"RACOOL_CLI v=1 type=language locale=tr\n"));
    TEST_ASSERT_NOT_NULL(strstr(output,"RACOOL_CLI v=1 type=language locale=en\n"));
    TEST_ASSERT_TRUE(Localization_Apply("tr"));
}

static unsigned xy_calls;
static esp_err_t xy_result;
static esp_err_t save_xy(IConfigurationManager *self,const char *block,const ConfigurationFieldUpdate *fields,size_t count,bool persist) {
    (void)self;
    TEST_ASSERT_EQUAL_STRING("hardware",block);TEST_ASSERT_TRUE(persist);
    TEST_ASSERT_EQUAL(2,count);++xy_calls;
    TEST_ASSERT_EQUAL_UINT16(1234,*(const uint16_t *)fields[0].data);
    TEST_ASSERT_EQUAL_UINT16(4321,*(const uint16_t *)fields[1].data);
    return xy_result;
}
void test_SystemConfigFreeXYRejectsPartialMalformedWritesAndPropagatesFailure(void) {
    setUp_DC();
    IConfigurationManager manager={.UpdateFields=save_xy};
    mocked_ConfigurationManager_GetInstance_fake.return_val=&manager;
    SystemCommands_Register(DebugConsole_GetModule());
    CommandEntry command;
    TEST_ASSERT_TRUE(CommandRegistry_CopyByName("SYSTEM",&command));
    char *args[]={"SYSTEM","CONFIG","XY","cold","1234","4321"};
    xy_calls=0;xy_result=ESP_OK;
    const char *bad[]={"", "-1", "65535", "1.5", "1234junk", "999999999999999999999"};
    for (unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);++i) {
        args[4]=(char *)bad[i];TEST_ASSERT_NOT_EQUAL(0,command.func(6,args));
    }
    args[4]="1234";args[5]="0";
    TEST_ASSERT_NOT_EQUAL(0,command.func(6,args));
    args[5]="4321";args[3]="unknown";
    TEST_ASSERT_NOT_EQUAL(0,command.func(6,args));
    args[3]="cold";
    TEST_ASSERT_NOT_EQUAL(0,command.func(5,args));
    TEST_ASSERT_EQUAL(0,xy_calls);
    xy_result=ESP_FAIL;TEST_ASSERT_NOT_EQUAL(0,command.func(6,args));
    xy_result=ESP_OK;TEST_ASSERT_EQUAL(0,command.func(6,args));
    TEST_ASSERT_EQUAL(2,xy_calls);
    mocked_ConfigurationManager_GetInstance_fake.return_val=NULL;
}

void test_CommandHelpUsesResponseLanguageWithoutReregistration(void) {
    setUp_DC();
    SystemCommands_Register(DebugConsole_GetModule());
    CommandEntry find; TEST_ASSERT_TRUE(CommandRegistry_CopyByName("FIND", &find));
    FILE *capture = tmpfile(); TEST_ASSERT_NOT_NULL(capture);
    fflush(stdout); int saved = privacy_dup(privacy_fileno(stdout));
    TEST_ASSERT_TRUE(saved >= 0);
    TEST_ASSERT_TRUE(privacy_dup2(privacy_fileno(capture), privacy_fileno(stdout)) >= 0);
    char *args[] = {"FIND", "HELP"};
    Localization_Apply("tr"); int tr = find.func(2, args);
    Localization_Apply("en"); int en = find.func(2, args);
    fflush(stdout); privacy_dup2(saved, privacy_fileno(stdout)); privacy_close(saved);
    rewind(capture); char output[4096] = {0}; fread(output, 1, sizeof(output)-1, capture); fclose(capture);
    Localization_Apply("tr");
    TEST_ASSERT_EQUAL(0, tr); TEST_ASSERT_EQUAL(0, en);
    TEST_ASSERT_NOT_NULL(strstr(output, "Komut yardımını göster"));
    TEST_ASSERT_NOT_NULL(strstr(output, "Show command help"));
    TEST_ASSERT_NOT_NULL(strstr(output, "İçin Arama Sonuçları"));
    TEST_ASSERT_NOT_NULL(strstr(output, "Search Results"));
}

static bool fail_developer_preference_save;
static esp_err_t save_developer_preference(IConfigurationManager *manager, const char *block,
                                            const ConfigurationFieldUpdate *fields, size_t count, bool persist) {
    (void)manager;
    if (strcmp(block, "system") || count != 1 || !persist ||
        fields[0].offset != offsetof(SystemBaseConfigs, developer_mode_enabled) || fields[0].size != 1)
        return ESP_ERR_INVALID_ARG;
    if (fail_developer_preference_save) return ESP_FAIL;
    DeveloperMode_SetEnabled(*(const uint8_t *)fields[0].data != 0);
    return ESP_OK;
}

void test_DeveloperModeCommandRejectsMalformedInputAndChangesOnlyMode(void) {
    setUp_DC();
    IConfigurationManager manager = {.UpdateFields = save_developer_preference};
    fail_developer_preference_save = true;
    mocked_ServiceLocator_GetService_fake.custom_fake = NULL;
    mocked_ServiceLocator_GetService_fake.return_val = (IService *)&manager;
    DeveloperMode_Reset();
    SystemCommands_Register(DebugConsole_GetModule());
    CommandEntry entry;
    TEST_ASSERT_TRUE(CommandRegistry_CopyByName("DEVELOPER", &entry));
    char *on[] = {"DEVELOPER", "on", "--machine"};
    char *bad[] = {"DEVELOPER", "invalid"};
    char *extra[] = {"DEVELOPER", "off", "extra"};
    char *off[] = {"DEVELOPER", "OFF"};
    TEST_ASSERT_FALSE(DeveloperMode_IsEnabled());
    TEST_ASSERT_NOT_EQUAL(0, entry.func(3, on));
    TEST_ASSERT_FALSE(DeveloperMode_IsEnabled());
    fail_developer_preference_save = false;
    TEST_ASSERT_EQUAL(0, entry.func(3, on));
    TEST_ASSERT_TRUE(DeveloperMode_IsEnabled());
    TEST_ASSERT_NOT_EQUAL(0, entry.func(2, bad));
    TEST_ASSERT_NOT_EQUAL(0, entry.func(3, extra));
    TEST_ASSERT_TRUE(DeveloperMode_IsEnabled());
    TEST_ASSERT_EQUAL(0, entry.func(2, off));
    TEST_ASSERT_FALSE(DeveloperMode_IsEnabled());
    mocked_ServiceLocator_GetService_fake.return_val = NULL;
}

void test_DeveloperHelpHidesDiagnosticsUntilRequestedInEnabledMode(void) {
    setUp_DC();
    SystemCommands_Register(DebugConsole_GetModule());
    esp_console_cmd_t diagnostic = {.command = "MEM", .help = "memory diagnostics"};
    TEST_ASSERT_TRUE(CommandRegistry_Register(&diagnostic, CMD_CAT_DIAGNOSTIC));
    CommandEntry help, developer;
    TEST_ASSERT_TRUE(CommandRegistry_CopyByName("HELP", &help));
    TEST_ASSERT_TRUE(CommandRegistry_CopyByName("DEVELOPER", &developer));
    FILE *capture = tmpfile(); TEST_ASSERT_NOT_NULL(capture);
    fflush(stdout); int saved = privacy_dup(privacy_fileno(stdout));
    TEST_ASSERT_TRUE(saved >= 0);
    TEST_ASSERT_TRUE(privacy_dup2(privacy_fileno(capture), privacy_fileno(stdout)) >= 0);
    char *help_args[] = {"HELP"};
    char *tools_args[] = {"DEVELOPER", "TOOLS", "diagnostics"};
    int normal = help.func(1, help_args);
    int disabled = developer.func(3, tools_args);
    fflush(stdout); long boundary = ftell(capture);
    DeveloperMode_SetEnabled(true);
    int normal_when_enabled = help.func(1, help_args);
    fflush(stdout); long tools_boundary = ftell(capture);
    int enabled = developer.func(3, tools_args);
    fflush(stdout); privacy_dup2(saved, privacy_fileno(stdout)); privacy_close(saved);
    rewind(capture); char output[12000] = {0}; size_t size = fread(output, 1, sizeof(output)-1, capture); fclose(capture);
    TEST_ASSERT_EQUAL(0, normal); TEST_ASSERT_NOT_EQUAL(0, disabled); TEST_ASSERT_EQUAL(0, normal_when_enabled); TEST_ASSERT_EQUAL(0, enabled);
    TEST_ASSERT_TRUE(boundary > 0 && tools_boundary > boundary && (size_t)tools_boundary < size);
    char *memory_entry = strstr(output, "\n    MEM ");
    TEST_ASSERT_NOT_NULL(memory_entry);
    TEST_ASSERT_TRUE(memory_entry >= output + tools_boundary);
    DeveloperMode_Reset();
}

void test_LogClearRequiresExplicitConfirmation(void) {
    setUp_DC();
    SystemCommands_Register(DebugConsole_GetModule());
    CommandEntry logs;
    TEST_ASSERT_TRUE(CommandRegistry_CopyByName("LOGS", &logs));
    char output[512];
    char *missing[] = {"LOGS", "clear"};
    char *wrong[] = {"LOGS", "clear", "wrong"};
    TEST_ASSERT_NOT_EQUAL(0, test_capture_console(logs.func, 2, missing, output, sizeof(output)));
    TEST_ASSERT_NOT_NULL(strstr(output, "LOGS clear CONFIRM"));
    TEST_ASSERT_NOT_EQUAL(0, test_capture_console(logs.func, 3, wrong, output, sizeof(output)));
    TEST_ASSERT_NOT_NULL(strstr(output, "LOGS clear CONFIRM"));
}
