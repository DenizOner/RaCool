#include "test_mocks.h"
#include "unity.h"
/* Exercise history with the production 100 Hz tick rather than the mock default. */
#undef portTICK_PERIOD_MS
#define portTICK_PERIOD_MS 10
#include "Controllers/ClimateController/ClimateConsoleCommands.c"

static ClimateTempHistoryEntry history_entry;
static uint8_t history_count;
uint8_t ClimateController_GetHistory(ClimateTempHistoryEntry *out, uint8_t maxEntries) {
    if (history_count && maxEntries) out[0]=history_entry;
    return history_count;
}

static SystemSettings settings;
static unsigned writes;
static unsigned fail_at;
static esp_err_t snapshot_result;
static esp_err_t snapshot(IConfigurationManager *self, SystemSettings *out) {
    (void)self;
    if (snapshot_result != ESP_OK) return snapshot_result;
    *out = settings;
    return ESP_OK;
}
static esp_err_t fields(IConfigurationManager *self, const char *block,
                        const ConfigurationFieldUpdate *updates, size_t count, bool persist) {
    (void)self;
    TEST_ASSERT_FALSE(persist);
    ++writes;
    if (writes == fail_at) return ESP_FAIL;
    if (!strcmp(block,"control")) {
        ControlConfigs candidate=settings.control;
        for (size_t i=0;i<count;++i) memcpy((char *)&candidate+updates[i].offset,updates[i].data,updates[i].size);
        if (!SystemConfigs_IsFanSourceConfigValid(&candidate) || !SystemConfigs_IsFanSetpointPairValid(candidate.relay_open_temp,candidate.relay_close_temp)) return ESP_ERR_INVALID_ARG;
    }
    void *destination = !strcmp(block, "control") ? (void *)&settings.control : (void *)&settings.calibration;
    for (size_t i = 0; i < count; ++i) memcpy((char *)destination + updates[i].offset, updates[i].data, updates[i].size);
    return ESP_OK;
}
static IConfigurationManager manager = {.GetSnapshot = snapshot, .UpdateFields = fields};
void setUp(void) {
    memset(&settings, 0, sizeof(settings));
    settings.control.fan_delta_on_decic=20;settings.control.fan_delta_off_decic=10;
    settings.control.relay_open_temp = 35;
    settings.control.relay_close_temp = 25;
    history_count=0;
    writes = fail_at = 0;
    snapshot_result = ESP_OK;
    RESET_FAKE(mocked_ConfigurationManager_GetInstance);
    mocked_ConfigurationManager_GetInstance_fake.return_val = &manager;
}
void tearDown(void) {}
static unsigned override_reads;
static float read_cpu(IClimateController *self) { (void)self; return 27; }
static ClimateControllerConfig read_config(IClimateController *self) {
    (void)self; return (ClimateControllerConfig){.cpuTempProtectionEnabled=true};
}
static bool read_override(IClimateController *self) { (void)self; ++override_reads; return false; }
void test_ClimateCliCpuReadsActualOverrideInsteadOfDerivedFanMode(void) {
    IClimateController controller = {.GetCurrentCpuTemperature=read_cpu,.GetConfig=read_config,
        .IsCpuThermalOverrideActive=read_override};
    RESET_FAKE(mocked_ServiceLocator_GetService);
    mocked_ServiceLocator_GetService_fake.return_val = &controller.base;
    char *args[] = {"CPU", "extra"};
    override_reads = 0;
    TEST_ASSERT_EQUAL(1, do_climate_cpu(2, args));
    TEST_ASSERT_EQUAL(0, override_reads);
    /* GetFanMode is deliberately absent: the derived ON mode is not evidence. */
    TEST_ASSERT_EQUAL(0, do_climate_cpu(1, args));
    TEST_ASSERT_EQUAL(1, override_reads);
    mocked_ServiceLocator_GetService_fake.return_val = NULL;
}
void test_ClimateCliRejectsMalformedAndExcessArgumentsWithoutWrites(void) {
    char *cal[] = {"CAL", "OBJ", "" , "extra"};
    const char *bad[] = {"", "nan", "inf", "2bad", "1e1000", "999"};
    for (size_t i = 0; i < sizeof(bad)/sizeof(bad[0]); ++i) {
        cal[2] = (char *)bad[i];
        TEST_ASSERT_EQUAL_INT(1, do_climate_cal(3, cal));
        TEST_ASSERT_EQUAL_INT(1, do_climate_set(3, cal));
    }
    cal[2] = "0.1";
    TEST_ASSERT_EQUAL_INT(1, do_climate_cal(4, cal));
    char *mode[] = {"MODE", "BAD", "extra"};
    TEST_ASSERT_EQUAL_INT(1, do_climate_mode(2, mode));
    mode[1] = "ON";
    TEST_ASSERT_EQUAL_INT(1, do_climate_mode(3, mode));
    TEST_ASSERT_EQUAL_UINT(0, writes);
}
void test_ClimateCliFieldUpdatesAndFailuresPreserveUnrelatedFields(void) {
    settings.calibration.ir_ambient_cal = 1.2f;
    char *cal[] = {"CAL", "OBJ", "0.1"};
    TEST_ASSERT_EQUAL_INT(0, do_climate_cal(3, cal));
    TEST_ASSERT_EQUAL_FLOAT(0.1f, settings.calibration.ir_object_cal);
    TEST_ASSERT_EQUAL_FLOAT(1.2f, settings.calibration.ir_ambient_cal);
    char *set[] = {"SET", "HIGH", "40"};
    TEST_ASSERT_EQUAL_INT(0, do_climate_set(3, set));
    TEST_ASSERT_EQUAL_FLOAT(40, settings.control.relay_open_temp);
    TEST_ASSERT_EQUAL_FLOAT(25, settings.control.relay_close_temp);
    char *mode[] = {"MODE", "ON"};
    fail_at = writes + 1;
    TEST_ASSERT_EQUAL_INT(1, do_climate_mode(2, mode));
    TEST_ASSERT_FALSE(settings.control.fan_manual_mode);
    fail_at = 0;
    TEST_ASSERT_EQUAL_INT(0, do_climate_mode(2, mode));
    TEST_ASSERT_TRUE(settings.control.fan_manual_mode);
    TEST_ASSERT_TRUE(settings.control.fan_manual_state);
}
void test_ClimateCliResetReportsSecondBlockFailureAndCanRetry(void) {
    char *reset[] = {"RESET"};
    settings.calibration.ir_object_cal = 2;
    settings.control.fan_strategy=1;settings.control.object_temperature_source=2;
    settings.control.ambient_temperature_source=1;
    settings.control.fan_delta_on_decic=-10;settings.control.fan_delta_off_decic=-20;
    fail_at = 2;
    TEST_ASSERT_EQUAL_INT(1, do_climate_reset(1, reset));
    TEST_ASSERT_EQUAL_FLOAT(DEFAULT_RELAY_OPEN_TEMP, settings.control.relay_open_temp);
    TEST_ASSERT_EQUAL(0,settings.control.fan_strategy);
    TEST_ASSERT_EQUAL(0,settings.control.object_temperature_source);
    TEST_ASSERT_EQUAL(0,settings.control.ambient_temperature_source);
    TEST_ASSERT_EQUAL(DEFAULT_FAN_DELTA_ON_DECIC,settings.control.fan_delta_on_decic);
    TEST_ASSERT_EQUAL(DEFAULT_FAN_DELTA_OFF_DECIC,settings.control.fan_delta_off_decic);
    TEST_ASSERT_EQUAL_FLOAT(2, settings.calibration.ir_object_cal);
    fail_at = 0;
    TEST_ASSERT_EQUAL_INT(0, do_climate_reset(1, reset));
    TEST_ASSERT_EQUAL_FLOAT(DEFAULT_IR_OBJECT_CAL, settings.calibration.ir_object_cal);
}
void test_ClimateCliSnapshotFailureDoesNotExposeLiveStorage(void) {
    SystemSettings output;
    TEST_ASSERT_TRUE(get_settings(&output));
    output.control.relay_open_temp = 90;
    TEST_ASSERT_EQUAL_FLOAT(35, settings.control.relay_open_temp);
    snapshot_result = ESP_FAIL;
    TEST_ASSERT_FALSE(get_settings(&output));
}

static unsigned save_calls;
static unsigned save_fail_at;
static esp_err_t save_block(IConfigurationManager *self, const char *block) {
    (void)self;
    ++save_calls;
    TEST_ASSERT_EQUAL_STRING(save_calls == 1 ? "control" : "calibration", block);
    return save_calls == save_fail_at ? ESP_FAIL : ESP_OK;
}
void test_ClimateCliSaveFailureStopsOrReportsPartialCommit(void) {
    char *save[] = {"SAVE", "extra"};
    manager.Save = save_block;
    save_calls = 0; save_fail_at = 1;
    TEST_ASSERT_EQUAL_INT(1, do_climate_save(2, save));
    TEST_ASSERT_EQUAL_UINT(0, save_calls);
    TEST_ASSERT_EQUAL_INT(1, do_climate_save(1, save));
    TEST_ASSERT_EQUAL_UINT(1, save_calls);
    save_calls = 0; save_fail_at = 2;
    TEST_ASSERT_EQUAL_INT(1, do_climate_save(1, save));
    TEST_ASSERT_EQUAL_UINT(2, save_calls);
    save_calls = 0; save_fail_at = 0;
    TEST_ASSERT_EQUAL_INT(0, do_climate_save(1, save));
    manager.Save = NULL;
}

#include "Infrastructure/support/console_registration_fixture.h"
void test_ClimateRegistration_RejectsMissingBoundary(void) {
    registration_calls = registration_fail_at = 0;
    check_registration_inputs(ClimateCommands_Register);
}
void test_ClimateRegistration_PreservesBothAliasFailuresAndRetries(void) {
    check_registration_failures(ClimateCommands_Register, 2);
}

void test_ClimateCliDeltaAndSourceShareCentralCandidateValidation(void) {
    char *delta[]={"DELTA","-1","-2"};
    TEST_ASSERT_EQUAL(0,do_climate_delta(3,delta));TEST_ASSERT_EQUAL(-10,settings.control.fan_delta_on_decic);TEST_ASSERT_EQUAL(-20,settings.control.fan_delta_off_decic);
    delta[2]="-1";TEST_ASSERT_EQUAL(1,do_climate_delta(3,delta));TEST_ASSERT_EQUAL(-20,settings.control.fan_delta_off_decic);
    char *source[]={"SOURCE","OBJECT","2"};TEST_ASSERT_EQUAL(0,do_climate_source(3,source));TEST_ASSERT_EQUAL(2,settings.control.object_temperature_source);
    source[1]="AMBIENT";source[2]="1";TEST_ASSERT_EQUAL(0,do_climate_source(3,source));TEST_ASSERT_EQUAL(1,settings.control.ambient_temperature_source);
    source[2]="4";TEST_ASSERT_EQUAL(1,do_climate_source(3,source));TEST_ASSERT_EQUAL(1,settings.control.ambient_temperature_source);
    char *mode[]={"MODE","AMBIENT_DELTA"};TEST_ASSERT_EQUAL(0,do_climate_mode(2,mode));TEST_ASSERT_EQUAL(1,settings.control.fan_strategy);
}

#include "../../Infrastructure/support/console_capture.h"
void test_ClimateCliHistoryUsesProductionTickAndWrapSafeAge(void) {
    history_count=1; history_entry=(ClimateTempHistoryEntry){.objectTemp=20,.ambientTemp=19,.cpuTemp=30,.tickMs=900};
    mocked_xTaskGetTickCount_fake.return_val=100;
    char output[2048]; char *args[]={"HIST"};
    TEST_ASSERT_EQUAL(0,test_capture_console(do_climate_hist,1,args,output,sizeof(output)));
    char *row=strstr(output,"\n  1 "); unsigned index;float object,ambient,cpu;unsigned long age;
    TEST_ASSERT_NOT_NULL(row);
    TEST_ASSERT_EQUAL(5,sscanf(row," %u %f %f %f %lu",&index,&object,&ambient,&cpu,&age));
    TEST_ASSERT_EQUAL_UINT32(100,age);
    history_entry.tickMs=UINT32_MAX-15u; mocked_xTaskGetTickCount_fake.return_val=2;
    TEST_ASSERT_EQUAL(0,test_capture_console(do_climate_hist,1,args,output,sizeof(output)));
    row=strstr(output,"\n  1 ");TEST_ASSERT_NOT_NULL(row);
    TEST_ASSERT_EQUAL(5,sscanf(row," %u %f %f %f %lu",&index,&object,&ambient,&cpu,&age));
    TEST_ASSERT_EQUAL_UINT32(36,age);
    history_entry.tickMs=0; mocked_xTaskGetTickCount_fake.return_val=5;
    TEST_ASSERT_EQUAL(0,test_capture_console(do_climate_hist,1,args,output,sizeof(output)));
    row=strstr(output,"\n  1 ");TEST_ASSERT_NOT_NULL(row);
    TEST_ASSERT_EQUAL(5,sscanf(row," %u %f %f %f %lu",&index,&object,&ambient,&cpu,&age));
    TEST_ASSERT_EQUAL_UINT32(50,age);
    history_count=0;
}

static bool read_false(IClimateController *self) { (void)self; return false; }
static uint32_t read_tick(IClimateController *self) { (void)self; return 123; }
static ClimateFanMode read_mode(IClimateController *self) { (void)self; return CLIMATE_FAN_OBJECT_SENSITIVE; }
void test_ClimateCliMachineStatusIsLanguageIndependentAndRejectsMissingBoundary(void) {
    IClimateController controller={.GetCurrentObjectTemperature=read_cpu,.GetCurrentAmbientTemperature=read_cpu,
        .GetCurrentCpuTemperature=read_cpu,.GetRelayState=read_false,.GetLastSensorReadTime=read_tick,
        .IsSensorStale=read_false,.GetRelayDebounceState=read_false,.GetFanMode=read_mode};
    RESET_FAKE(mocked_ServiceLocator_GetService);
    mocked_ServiceLocator_GetService_fake.return_val=&controller.base;
    settings.system.temp_unit=1;
    char *args[]={"CLIMATE","STATUS","--machine"}; char english[2048],turkish[2048];
    TEST_ASSERT_TRUE(Localization_Apply("en"));
    TEST_ASSERT_EQUAL(0,test_capture_console(cmd_climate,3,args,english,sizeof(english)));
    TEST_ASSERT_NOT_NULL(strstr(english,"object_c=27.00"));
    TEST_ASSERT_TRUE(Localization_Apply("tr"));
    TEST_ASSERT_EQUAL(0,test_capture_console(cmd_climate,3,args,turkish,sizeof(turkish)));
    TEST_ASSERT_EQUAL_STRING(english,turkish);
    controller.GetFanMode=NULL;
    TEST_ASSERT_EQUAL(1,test_capture_console(cmd_climate,3,args,turkish,sizeof(turkish)));
    TEST_ASSERT_NULL(strstr(turkish,"type=climate_status"));
    mocked_ServiceLocator_GetService_fake.return_val=NULL;
    TEST_ASSERT_TRUE(Localization_Apply("en"));
}

void test_ClimateCliLocalizedCommandKeepsMachineResultAndRejectsWrites(void) {
    char output[2048]; char *args[]={"CLIMATE","MODE","ON","--machine"};
    TEST_ASSERT_TRUE(Localization_Apply("tr"));
    TEST_ASSERT_EQUAL(0,test_capture_console(cmd_climate,4,args,output,sizeof(output)));
    TEST_ASSERT_NOT_NULL(strstr(output,"Fan modu Sürekli açık"));
    TEST_ASSERT_NOT_NULL(strstr(output,"type=climate_command command=MODE result=completed error=0"));
    args[2]="invalid";unsigned previous=writes;
    TEST_ASSERT_EQUAL(1,test_capture_console(cmd_climate,4,args,output,sizeof(output)));
    TEST_ASSERT_EQUAL(previous,writes);
    TEST_ASSERT_NOT_NULL(strstr(output,"type=climate_command command=MODE result=rejected error=1"));
    TEST_ASSERT_TRUE(Localization_Apply("en"));
}
