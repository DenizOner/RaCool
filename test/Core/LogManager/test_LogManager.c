#include "unity.h"
#include <stdlib.h>
static bool failAllocation;
static void *allocate_logs(size_t count, size_t size) { return failAllocation ? NULL : calloc(count, size); }
#define calloc allocate_logs
#include "Core/LogManager/LogManager.c"
#undef calloc
#include "Core/LogManager/LogManagerConsoleCommands.c"
static bool stopInsideSink;
static unsigned sinkCalls;
static int previous_sink(const char *format, va_list args) {
    (void)format; (void)args; sinkCalls++;
    if (stopInsideSink) { stopInsideSink = false; TEST_ASSERT_EQUAL(0, LogManager_Stop(NULL)); }
    return 7;
}
static vprintf_like_t sdkSink;
vprintf_like_t esp_log_set_vprintf(vprintf_like_t fn) { vprintf_like_t previous = sdkSink; sdkSink = fn; return previous; }
esp_log_level_t esp_log_level_get(const char *tag) { (void)tag; return ESP_LOG_INFO; }
void esp_log_writev(esp_log_level_t level, const char *tag, const char *format, va_list args) {
    (void)level; (void)tag; sdkSink(format, args);
}
static ISerialMonitorPlugin plugin;
ISerialMonitorPlugin *LogManager_GetSerialPlugin(void) { return &plugin; }
static int emit_log(const char *format, ...) {
    va_list args; va_start(args, format); int result = LogManager_vprint(format, args); va_end(args); return result;
}
void setUp(void) {
    ILogManager base = *GetLogManager();
    free(logManagerInstance.logBuffer);
    memset(&logManagerInstance, 0, sizeof(logManagerInstance)); logManagerInstance.base = base;
    RESET_FAKE(mocked_xSemaphoreCreateMutex); RESET_FAKE(mocked_xSemaphoreTake);
    RESET_FAKE(mocked_ServiceLocator_GetService); RESET_FAKE(mocked_esp_log_level_set);
    mocked_xSemaphoreCreateMutex_fake.return_val = (SemaphoreHandle_t)1;
    mocked_xSemaphoreTake_fake.return_val = pdTRUE;
    sdkSink = previous_sink; stopInsideSink = failAllocation = false; sinkCalls = 0;
}
void tearDown(void) { if (logManagerInstance.controlMutex) TEST_ASSERT_EQUAL(0, LogManager_Stop(NULL)); }
static void start_capture(uint32_t capacity) {
    LogManagerConfig config = *LogManagerConfig_GetDefault(); config.maxLogEntries = capacity; config.enableInterception = true;
    TEST_ASSERT_EQUAL(0, LogManager_InitializeWithConfig(NULL, &config, NULL));
    config.maxLogEntries = 0; /* Caller storage is not retained. */
    TEST_ASSERT_EQUAL(0, LogManager_Start(NULL));
}
void test_LogManager_RejectsInvalidConfigurationAndAllocation(void) {
    LogManagerConfig config = *LogManagerConfig_GetDefault(); config.maxLogEntries = 0;
    TEST_ASSERT_EQUAL(-1, LogManager_InitializeWithConfig(NULL, &config, NULL));
    config = *LogManagerConfig_GetDefault(); config.vprintfBufferSize = LOG_MANAGER_MAX_FORMAT_SIZE + 1;
    TEST_ASSERT_EQUAL(-1, LogManager_InitializeWithConfig(NULL, &config, NULL));
    config = *LogManagerConfig_GetDefault(); config.defaultLogLevel = 99;
    TEST_ASSERT_EQUAL(-1, LogManager_InitializeWithConfig(NULL, &config, NULL));
    config = *LogManagerConfig_GetDefault(); failAllocation = true;
    TEST_ASSERT_EQUAL(-1, LogManager_InitializeWithConfig(NULL, &config, NULL));
    TEST_ASSERT_NULL(logManagerInstance.logBuffer);
}
void test_LogManager_ReturnsNewestEntriesAndWrapsRing(void) {
    start_capture(3);
    for (unsigned i = 0; i < 5; ++i) TEST_ASSERT_EQUAL(7, emit_log("I: message %u\n", i));
    SystemLogEntry entries[2]; TEST_ASSERT_EQUAL(2, LogManager_GetRecentLogs(entries, 2));
    TEST_ASSERT_EQUAL_STRING("message 3", entries[0].message);
    TEST_ASSERT_EQUAL_STRING("message 4", entries[1].message);
    TEST_ASSERT_EQUAL(0, LogManager_GetRecentLogs(NULL, 1));
    TEST_ASSERT_EQUAL(5, sinkCalls);
}
void test_LogManager_TemporaryCaptureOwnsHookAndBlocksForeignLifecycle(void) {
    ILogManager *manager=GetLogManager();
    TEST_ASSERT_EQUAL(0,manager->base.Initialize(&manager->base,NULL));
    TEST_ASSERT_EQUAL(0,manager->base.Start(&manager->base));
    TEST_ASSERT_EQUAL(0,manager->BeginCapture(manager));
    TEST_ASSERT_EQUAL(-1,manager->BeginCapture(manager));
    TEST_ASSERT_EQUAL(-1,manager->base.Stop(&manager->base));
    TEST_ASSERT_EQUAL(SERVICE_STATE_STARTED,manager->base.GetState(&manager->base));
    TEST_ASSERT_EQUAL(7,emit_log("I: temporary capture\n"));
    SystemLogEntry entry;
    TEST_ASSERT_EQUAL(1,manager->GetRecentLogs(&entry,1));
    TEST_ASSERT_EQUAL_STRING("temporary capture",entry.message);
    mocked_xSemaphoreTake_fake.return_val=pdFALSE;
    TEST_ASSERT_EQUAL(-1,manager->EndCapture(manager));
    TEST_ASSERT_TRUE(logManagerInstance.diagnosticCaptureOwned);
    mocked_xSemaphoreTake_fake.return_val=pdTRUE;
    TEST_ASSERT_EQUAL(0,manager->EndCapture(manager));
    TEST_ASSERT_FALSE(logManagerInstance.captureEnabled);
    TEST_ASSERT_EQUAL_PTR(previous_sink,sdkSink);
    TEST_ASSERT_EQUAL(0,manager->base.Stop(&manager->base));
}
void test_LogManager_StopDuringSinkCannotUseDetachedBuffer(void) {
    start_capture(3); stopInsideSink = true;
    TEST_ASSERT_EQUAL(7, emit_log("I: late callback\n"));
    TEST_ASSERT_NULL(logManagerInstance.logBuffer);
    TEST_ASSERT_EQUAL_PTR(previous_sink, sdkSink);
    TEST_ASSERT_EQUAL(SERVICE_STATE_STOPPED, logManagerInstance.state);
    TEST_ASSERT_EQUAL(7, emit_log("I: already captured SDK callback\n"));
    TEST_ASSERT_EQUAL(0, logManagerInstance.logCount);
}
void test_LogManager_InitializeStopAndRestart(void) {
    TEST_ASSERT_EQUAL(-1, LogManager_Start(NULL));
    TEST_ASSERT_EQUAL(0, LogManager_Initialize(NULL, NULL));
    TEST_ASSERT_EQUAL(0, LogManager_Stop(NULL));
    TEST_ASSERT_NULL(logManagerInstance.logBuffer);
    TEST_ASSERT_EQUAL(0, LogManager_Start(NULL));
    TEST_ASSERT_NOT_NULL(logManagerInstance.logBuffer);
    TEST_ASSERT_EQUAL(0, LogManager_CheckHealth(NULL));
}
void test_LogManager_ReinitializationCannotAcknowledgeIgnoredChanges(void) {
    LogManagerConfig config = *LogManagerConfig_GetDefault();
    TEST_ASSERT_EQUAL(0, LogManager_InitializeWithConfig(NULL, &config, NULL));
    TEST_ASSERT_EQUAL(0, LogManager_InitializeWithConfig(NULL, &config, NULL));
    config.maxLogEntries++;
    TEST_ASSERT_EQUAL(-1, LogManager_InitializeWithConfig(NULL, &config, NULL));
    TEST_ASSERT_EQUAL(0, LogManager_Start(NULL));
    TEST_ASSERT_EQUAL(-1, LogManager_InitializeWithConfig(NULL, &config, NULL));
    TEST_ASSERT_EQUAL(LogManagerConfig_GetDefault()->maxLogEntries, logManagerInstance.config.maxLogEntries);
}
void test_LogManager_FailedInitializationStopCannotEnableInvalidConfiguration(void) {
    failAllocation = true;
    TEST_ASSERT_EQUAL(-1, LogManager_Initialize(NULL, NULL));
    TEST_ASSERT_EQUAL(0, LogManager_Stop(NULL));
    TEST_ASSERT_EQUAL(SERVICE_STATE_UNINITIALIZED, logManagerInstance.state);
    TEST_ASSERT_EQUAL(-1, LogManager_Start(NULL));
    failAllocation = false;
    TEST_ASSERT_EQUAL(0, LogManager_Initialize(NULL, NULL));
    TEST_ASSERT_EQUAL(0, LogManager_Start(NULL));
}
static int removeResult;
static int32_t add_plugin(ISerialMonitor *self, ISerialMonitorPlugin *p) { (void)self; (void)p; return 0; }
static int32_t remove_plugin(ISerialMonitor *self, ISerialMonitorPlugin *p) { (void)self; (void)p; return removeResult; }
static ISerialMonitor serial = {.AddPlugin = add_plugin, .RemovePlugin = remove_plugin};
static IService *lookup_serial(const char *name) { return strcmp(name, SVC_NAME_SERIAL_MONITOR) == 0 ? &serial.base : NULL; }
void test_LogManager_PluginRemovalFailureRetainsOwnership(void) {
    mocked_ServiceLocator_GetService_fake.custom_fake = lookup_serial; start_capture(3);
    removeResult = -1;
    TEST_ASSERT_EQUAL(-1, LogManager_Stop(NULL));
    TEST_ASSERT_EQUAL_PTR(&serial, logManagerInstance.ownedSerial);
    TEST_ASSERT_NOT_NULL(logManagerInstance.logBuffer);
    TEST_ASSERT_EQUAL(-1, LogManager_Start(NULL));
    TEST_ASSERT_EQUAL(-1, LogManager_Initialize(NULL, NULL));
    removeResult = 0;
    TEST_ASSERT_EQUAL(0, LogManager_Stop(NULL));
    TEST_ASSERT_NULL(logManagerInstance.ownedSerial);
}
void test_LogManager_ValidatesLogArguments(void) {
    TEST_ASSERT_EQUAL(-1, LogManager_Log(LOG_LEVEL_INFO, NULL, "%s", "value"));
    TEST_ASSERT_EQUAL(-1, LogManager_Log((LogLevel)99, "Test", "%s", "value"));
    TEST_ASSERT_EQUAL(-1, LogManager_Log(LOG_LEVEL_INFO, "Test", NULL));
    LogManager_SetModuleLogLevel(NULL, ESP_LOG_INFO); LogManager_SetGlobalLogLevel(99);
    TEST_ASSERT_EQUAL(0, mocked_esp_log_level_set_fake.call_count);
}

void test_LogManager_CLIRejectsMalformedOverflowAndExtraArguments(void) {
    const char *bad[] = {"junk", "1x", "-1", "6", "999999999999999999999999"};
    for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        char *args[] = {"log_level_set", (char *)bad[i]};
        TEST_ASSERT_NOT_EQUAL(0, cmd_log_level_set(2, args));
    }
    TEST_ASSERT_EQUAL(0, mocked_esp_log_level_set_fake.call_count);
    char *level[] = {"log_level_set", "4"};
    TEST_ASSERT_EQUAL(0, cmd_log_level_set(2, level));
    char *recent[] = {"log_recent", "0"};
    TEST_ASSERT_NOT_EQUAL(0, cmd_log_recent(2, recent));
    char *module[] = {"log_module_set", "", "3"};
    TEST_ASSERT_NOT_EQUAL(0, cmd_log_module_set(3, module));
    TEST_ASSERT_NOT_EQUAL(0, cmd_log_status(2, level));
}
