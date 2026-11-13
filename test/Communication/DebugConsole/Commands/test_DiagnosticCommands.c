#ifdef _WIN32
#include <io.h>
#define diagnostic_dup _dup
#define diagnostic_dup2 _dup2
#define diagnostic_close _close
#define diagnostic_fileno _fileno
#else
#include <unistd.h>
#define diagnostic_dup dup
#define diagnostic_dup2 dup2
#define diagnostic_close close
#define diagnostic_fileno fileno
#endif
#include "unity.h"
#include "Infrastructure/support/test_mocks.h"
#include "Management/DiagnosticsManager/IDiagnosticsManager.h"
#include "Communication/DebugConsole/Core/IDashboard.h"
static IDiagnosticsManager fakeDiagnostics;
static DiagnosticsTestResult result;
static int32_t setterResult, triggerResult;
static unsigned setterCalls;
static bool lastEnable;
static uint32_t lastInterval;
static IDashboard fakeDashboard;
static IDashboard *dashboard;
static esp_err_t registrationResult;
static bool fieldResult;
static unsigned registrationCalls, fieldCalls;
static IDiagnosticsManager *DiagCommandTest_GetModule(void) { return &fakeDiagnostics; }
#include "Communication/DebugConsole/Commands/DiagnosticCommands.c"
#define DiagnosticsManager_GetModule DiagCommandTest_GetModule
#include "Management/DiagnosticsManager/DiagnosticsManagerConsoleCommands.c"
#undef DiagnosticsManager_GetModule
IDashboard *DashboardManager_GetModule(void) { return dashboard; }

static esp_err_t RegisterCommand(IDebugConsole *self, const esp_console_cmd_t *cmd, CommandCategory category) {
    (void)self; TEST_ASSERT_NOT_NULL(cmd->func);
    TEST_ASSERT_EQUAL(CMD_CAT_DIAGNOSTIC, category);
    ++registrationCalls; return registrationResult;
}
static bool RegisterField(IDashboard *self, const char *label, DashboardValueProvider provider) {
    (void)self; TEST_ASSERT_EQUAL_STRING("HEALTH", label); TEST_ASSERT_NOT_NULL(provider);
    ++fieldCalls; return fieldResult;
}

static int32_t SetPeriodic(IDiagnosticsManager *self, bool enabled, uint32_t interval) {
    (void)self; ++setterCalls;
    if (setterResult == DIAG_OK) { lastEnable = enabled; lastInterval = interval; }
    return setterResult;
}
static int32_t Trigger(IDiagnosticsManager *self) { (void)self; return triggerResult; }
static DiagnosticsTestResult ReadResult(IDiagnosticsManager *self) { (void)self; return result; }
void setUp(void) {
    memset(&fakeDiagnostics, 0, sizeof(fakeDiagnostics));
    fakeDiagnostics.SetPeriodicTest = SetPeriodic;
    fakeDiagnostics.StartAsyncSelfTest = Trigger;
    fakeDiagnostics.GetLastTestResult = ReadResult;
    memset(&result, 0, sizeof(result));
    setterResult = triggerResult = DIAG_OK;
    setterCalls = 0; lastEnable = false; lastInterval = 60000;
    dashboard = NULL; registrationResult = ESP_OK; fieldResult = true;
    registrationCalls = fieldCalls = 0;
    memset(&fakeDashboard, 0, sizeof(fakeDashboard));
    fakeDashboard.RegisterField = RegisterField;
    RESET_FAKE(mocked_ServiceLocator_GetService);
    mocked_ServiceLocator_GetService_fake.return_val = &fakeDiagnostics.base;
}
void tearDown(void) {}

void test_DiagnosticCLIRejectsMalformedNumbersBeforeMutation(void) {
    const char *values[] = {"", "-1000", "+1000", " 1000", "1.5", "1000junk", "4294967296", "999", "0", "86400001"};
    for (unsigned i = 0; i < sizeof(values) / sizeof(values[0]); ++i) {
        char *argv[] = {"PERIODIC", "SET_INTERVAL", (char *)values[i]};
        TEST_ASSERT_NOT_EQUAL(0, cmd_diag_periodic(3, argv, Localization_Snapshot()));
    }
    TEST_ASSERT_EQUAL(0, setterCalls);
    TEST_ASSERT_EQUAL(60000, lastInterval);
}
void test_DiagnosticCLIValidBoundsAndFailurePropagation(void) {
    char *minimum[] = {"PERIODIC", "SET_INTERVAL", "1000"};
    TEST_ASSERT_EQUAL(0, cmd_diag_periodic(3, minimum, Localization_Snapshot()));
    TEST_ASSERT_TRUE(lastEnable);
    TEST_ASSERT_EQUAL(1000, lastInterval);
    char *maximum[] = {"PERIODIC", "SET_INTERVAL", "86400000"};
    TEST_ASSERT_EQUAL(0, cmd_diag_periodic(3, maximum, Localization_Snapshot()));
    TEST_ASSERT_EQUAL(86400000, lastInterval);
    setterResult = DIAG_ERR_BUSY;
    TEST_ASSERT_NOT_EQUAL(0, cmd_diag_periodic(3, minimum, Localization_Snapshot()));
    TEST_ASSERT_EQUAL(86400000, lastInterval);
}
void test_DiagnosticCLIEnableDisableAndArity(void) {
    char *disable[] = {"PERIODIC", "DISABLE"};
    TEST_ASSERT_EQUAL(0, cmd_diag_periodic(2, disable, Localization_Snapshot()));
    TEST_ASSERT_FALSE(lastEnable);
    TEST_ASSERT_EQUAL(0, lastInterval);
    char *bad[] = {"PERIODIC", "ENABLE", "extra"};
    TEST_ASSERT_NOT_EQUAL(0, cmd_diag_periodic(3, bad, Localization_Snapshot()));
    TEST_ASSERT_EQUAL(1, setterCalls);
}
void test_DiagnosticCLISelfTestFailureIsNotSuccess(void) {
    char *upper[] = {"RUN"};
    char *lower[] = {"diag", "selftest"};
    triggerResult = DIAG_ERR_BUSY;
    TEST_ASSERT_NOT_EQUAL(0, cmd_diag_run(1, upper, Localization_Snapshot()));
    TEST_ASSERT_NOT_EQUAL(0, diag_console_cmd_handler(2, lower));
    TEST_ASSERT_NOT_EQUAL(0, diag_console_cmd_handler(3, lower));
    triggerResult = DIAG_OK;
    TEST_ASSERT_EQUAL(0, cmd_diag_run(1, upper, Localization_Snapshot()));
    TEST_ASSERT_EQUAL(0, diag_console_cmd_handler(2, lower));
}
void test_DiagnosticDashboardUnknownIsNotMeasuredScore(void) {
    char buffer[64];
    result.health_score = 100;
    provider_health(buffer, sizeof(buffer));
    TEST_ASSERT_EQUAL_STRING("N/A", buffer);
    result.completed = true;
    provider_health(buffer, sizeof(buffer));
    TEST_ASSERT_EQUAL_STRING("N/A", buffer);
    result.services_checked = 9;
    provider_health(buffer, sizeof(buffer));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "100/100"));
}

void test_DiagnosticRegistrationPropagatesCommandAndFieldFailures(void) {
    IDebugConsole console = {.RegisterCommandWithCategory = RegisterCommand};
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, DiagnosticCommands_Register(NULL));
    dashboard = &fakeDashboard;
    registrationResult = ESP_ERR_NO_MEM;
    TEST_ASSERT_EQUAL(ESP_ERR_NO_MEM, DiagnosticCommands_Register(&console));
    TEST_ASSERT_EQUAL(0, fieldCalls);
    registrationResult = ESP_OK; fieldResult = false;
    TEST_ASSERT_EQUAL(ESP_FAIL, DiagnosticCommands_Register(&console));
    fieldResult = true;
    TEST_ASSERT_EQUAL(ESP_OK, DiagnosticCommands_Register(&console));
    TEST_ASSERT_EQUAL(3, registrationCalls);
    TEST_ASSERT_EQUAL(2, fieldCalls);
}
void test_DiagnosticLowerRegistrationRequiresOwnedConsole(void) {
    mocked_ServiceLocator_GetService_fake.return_val = NULL;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, DiagnosticsManager_RegisterConsoleCommands());
    TEST_ASSERT_EQUAL(0, registrationCalls);
    IDebugConsole console = {.RegisterCommandWithCategory = RegisterCommand};
    mocked_ServiceLocator_GetService_fake.return_val = &console.base;
    registrationResult = ESP_ERR_NO_MEM;
    TEST_ASSERT_EQUAL(ESP_ERR_NO_MEM, DiagnosticsManager_RegisterConsoleCommands());
    registrationResult = ESP_OK;
    TEST_ASSERT_EQUAL(ESP_OK, DiagnosticsManager_RegisterConsoleCommands());
    TEST_ASSERT_EQUAL(2, registrationCalls);
}

static uint32_t DiagnosticTestUptime(IDiagnosticsManager *self) { (void)self; return 100; }
static uint32_t DiagnosticTestHeap(IDiagnosticsManager *self) { (void)self; return 12345; }
static DiagnosticsDetailedMetrics DiagnosticTestMetrics(IDiagnosticsManager *self) {
    (void)self;
    return (DiagnosticsDetailedMetrics){.free_heap=80000,.minimum_free_heap=60000,.largest_free_block=50000,
        .uptime_seconds=123,.task_count=12,.health_score=75,.health_status=DIAG_HEALTH_WARNING};
}
#include "Infrastructure/support/console_capture.h"
void test_DiagnosticMachineMetricsPreserveValuesAndRejectMissingCallback(void) {
    fakeDiagnostics.GetDetailedMetrics=DiagnosticTestMetrics;
    char *args[]={"DIAG","METRICS","--machine"};char output[1024];
    TEST_ASSERT_EQUAL(0,test_capture_console(cmd_diag,3,args,output,sizeof(output)));
    TEST_ASSERT_NOT_NULL(strstr(output,"type=diagnostic_metrics free_heap=80000 min_free_heap=60000 largest_block=50000 uptime_s=123 tasks=12 score=75 known=1"));
    fakeDiagnostics.GetDetailedMetrics=NULL;
    TEST_ASSERT_NOT_EQUAL(0,test_capture_console(cmd_diag,3,args,output,sizeof(output)));
    TEST_ASSERT_NULL(strstr(output,"type=diagnostic_metrics"));
}
void test_DiagnosticHumanLanguageAndStableMachineResults(void) {
    fakeDiagnostics.GetSystemUptime = DiagnosticTestUptime;
    fakeDiagnostics.GetFreeHeap = DiagnosticTestHeap;
    result.completed = true; result.services_checked = 9; result.services_healthy = 9; result.health_score = 100;
    FILE *capture = tmpfile(); TEST_ASSERT_NOT_NULL(capture);
    fflush(stdout); int saved = diagnostic_dup(diagnostic_fileno(stdout));
    TEST_ASSERT_TRUE(saved >= 0);
    TEST_ASSERT_TRUE(diagnostic_dup2(diagnostic_fileno(capture), diagnostic_fileno(stdout)) >= 0);
    char *human[] = {"DIAG", "STATUS"};
    char *machine[] = {"DIAG", "STATUS", "--machine"};
    char *run[] = {"DIAG", "RUN", "--machine"};
    Localization_Apply("tr"); int tr = cmd_diag(2, human); int trMachine = cmd_diag(3, machine);
    Localization_Apply("en"); int en = cmd_diag(2, human); int enMachine = cmd_diag(3, machine);
    result.completed = false; int unknown = cmd_diag(3, machine);
    triggerResult = DIAG_ERR_BUSY; int rejected = cmd_diag(3, run);
    triggerResult = DIAG_OK; int accepted = cmd_diag(3, run);
    fflush(stdout); diagnostic_dup2(saved, diagnostic_fileno(stdout)); diagnostic_close(saved);
    rewind(capture); char output[4096] = {0}; fread(output, 1, sizeof(output)-1, capture); fclose(capture);
    Localization_Apply("tr");
    TEST_ASSERT_EQUAL(0, tr); TEST_ASSERT_EQUAL(0, en);
    TEST_ASSERT_EQUAL(0, trMachine); TEST_ASSERT_EQUAL(0, enMachine); TEST_ASSERT_EQUAL(0, unknown);
    TEST_ASSERT_NOT_EQUAL(0, rejected); TEST_ASSERT_EQUAL(0, accepted);
    TEST_ASSERT_NOT_NULL(strstr(output, "Sistem Sağlığı"));
    TEST_ASSERT_NOT_NULL(strstr(output, "System Health Status"));
    const char *record = "RACOOL_CLI v=1 type=diagnostic_status known=1 score=100 running=0 checked=9 healthy=9\n";
    const char *first = strstr(output, record); TEST_ASSERT_NOT_NULL(first);
    TEST_ASSERT_NOT_NULL(strstr(first + strlen(record), record));
    TEST_ASSERT_NOT_NULL(strstr(output, "type=diagnostic_status known=0"));
    TEST_ASSERT_NOT_NULL(strstr(output, "type=diagnostic_run result=rejected\n"));
    TEST_ASSERT_NOT_NULL(strstr(output, "type=diagnostic_run result=accepted\n"));
}
