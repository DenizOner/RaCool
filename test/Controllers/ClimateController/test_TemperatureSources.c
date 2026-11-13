#include "test_mocks.h"
#include "unity.h"
#include "Controllers/ClimateController/TemperatureSources.h"
#include "Controllers/ClimateController/FanCommands.h"

static ControlConfigs config;
static ExternalTemperatureSlot slots[3];
static unsigned mutations;
static esp_err_t apply(IConfigurationManager *self, const char *block,
    const ConfigurationFieldUpdate *fields, size_t count, bool persist) {
    (void)self; (void)persist;
    TEST_ASSERT_EQUAL_STRING("control", block);
    ControlConfigs candidate=config;
    for (size_t i=0;i<count;++i) memcpy((char *)&candidate+fields[i].offset,fields[i].data,fields[i].size);
    if (!SystemConfigs_IsFanSourceConfigValid(&candidate) ||
        !SystemConfigs_IsFanSetpointPairValid(candidate.relay_open_temp,candidate.relay_close_temp)) return ESP_ERR_INVALID_ARG;
    ++mutations; config=candidate; return ESP_OK;
}
static IConfigurationManager owner={.UpdateFields=apply};
void setUp(void) {
    config=(ControlConfigs){.relay_open_temp=35,.relay_close_temp=28,.fan_delta_on_decic=20,.fan_delta_off_decic=10,.version=2};
    memset(slots,0,sizeof(slots)); mutations=0;
}
void tearDown(void) {}
void test_ObjectSensitiveInclusiveBoundariesAndHysteresis(void) {
    TEST_ASSERT_FALSE(FanControl_EvaluateObjectSensitive(false,34.99f,35,28));
    TEST_ASSERT_TRUE(FanControl_EvaluateObjectSensitive(false,35,35,28));
    TEST_ASSERT_TRUE(FanControl_EvaluateObjectSensitive(true,28.01f,35,28));
    TEST_ASSERT_FALSE(FanControl_EvaluateObjectSensitive(true,28,35,28));
}
void test_AmbientDeltaDefaultBoundariesAndHysteresis(void) {
    TEST_ASSERT_FALSE(FanControl_EvaluateAmbientDelta(false,1.9f,20,10));
    TEST_ASSERT_TRUE(FanControl_EvaluateAmbientDelta(false,2,20,10));
    TEST_ASSERT_TRUE(FanControl_EvaluateAmbientDelta(true,3,20,10));
    TEST_ASSERT_TRUE(FanControl_EvaluateAmbientDelta(true,1.1f,20,10));
    TEST_ASSERT_FALSE(FanControl_EvaluateAmbientDelta(true,1,20,10));
}
void test_NegativeDeltaThresholdsKeepSignedSemantics(void) {
    TEST_ASSERT_TRUE(FanControl_EvaluateAmbientDelta(false,0,0,-10));
    TEST_ASSERT_FALSE(FanControl_EvaluateAmbientDelta(true,-1,0,-10));
    TEST_ASSERT_TRUE(FanControl_EvaluateAmbientDelta(false,-1,-10,-20));
    TEST_ASSERT_FALSE(FanControl_EvaluateAmbientDelta(true,-2,-10,-20));
    TEST_ASSERT_TRUE(FanControl_EvaluateAmbientDelta(true,-1.5f,-10,-20));
}
void test_ExternalStaleBoundaryFallbackAndRecovery(void) {
    TEST_ASSERT_EQUAL_UINT32(300000,EXTERNAL_TEMPERATURE_STALE_MS);
    slots[0]=(ExternalTemperatureSlot){21.7f,true,100};
    EffectiveTemperature value=TemperatureSources_Resolve(1,25,true,0,slots,100);
    TEST_ASSERT_EQUAL(1,value.effective_source); TEST_ASSERT_FLOAT_WITHIN(.001,21.7,value.value);
    value=TemperatureSources_Resolve(1,25,true,0,slots,100+EXTERNAL_TEMPERATURE_STALE_MS-1);
    TEST_ASSERT_EQUAL(1,value.effective_source);
    value=TemperatureSources_Resolve(1,25,true,0,slots,100+EXTERNAL_TEMPERATURE_STALE_MS);
    TEST_ASSERT_TRUE(value.valid); TEST_ASSERT_EQUAL(0,value.effective_source);
    TEST_ASSERT_EQUAL(TEMPERATURE_SOURCE_FALLBACK,value.status); TEST_ASSERT_EQUAL(TEMPERATURE_STALE,value.configured_state);
    slots[0].last_update_ms=100+EXTERNAL_TEMPERATURE_STALE_MS;
    value=TemperatureSources_Resolve(1,25,true,0,slots,slots[0].last_update_ms);
    TEST_ASSERT_EQUAL(1,value.effective_source); TEST_ASSERT_EQUAL(TEMPERATURE_SOURCE_NORMAL,value.status);
}
void test_IndependentSourcesAndCanonicalDifference(void) {
    slots[0]=(ExternalTemperatureSlot){20,true,0}; slots[1]=(ExternalTemperatureSlot){23,true,0};
    TemperatureSourceSnapshot values={0};
    values.object=TemperatureSources_Resolve(2,30,true,0,slots,0);
    values.ambient=TemperatureSources_Resolve(1,21,true,0,slots,0);
    TemperatureSources_ComputeDifference(&values);
    TEST_ASSERT_TRUE(values.difference_valid); TEST_ASSERT_EQUAL(2,values.object.effective_source);
    TEST_ASSERT_EQUAL(1,values.ambient.effective_source); TEST_ASSERT_FLOAT_WITHIN(.001,3,values.temperature_difference);
}
void test_EmptyRestartSlotsNeverReuseCachedExternalValues(void) {
    slots[0]=(ExternalTemperatureSlot){21.7f,true,12};
    memset(slots,0,sizeof(slots));
    EffectiveTemperature value=TemperatureSources_Resolve(1,25,true,0,slots,20);
    TEST_ASSERT_EQUAL(1,value.configured_source); TEST_ASSERT_EQUAL(0,value.effective_source);
    TEST_ASSERT_EQUAL(TEMPERATURE_WAITING_FOR_UPDATE,value.configured_state);
}
void test_InvalidInputsFailSafeAndFreshDataResumesControl(void) {
    TemperatureSourceSnapshot values={0};
    values.object=TemperatureSources_Resolve(1,NAN,false,10000,slots,10000);
    TEST_ASSERT_EQUAL(1,values.object.configured_source);
    TEST_ASSERT_FALSE(values.object.valid);
    TEST_ASSERT_EQUAL(TEMPERATURE_SOURCE_INVALID,values.object.status);
    TEST_ASSERT_TRUE(FanControl_Evaluate(false,&config,&values));
    values.object=(EffectiveTemperature){.valid=true,.value=20};
    TEST_ASSERT_FALSE(FanControl_Evaluate(true,&config,&values));
    config.fan_strategy=1;
    TEST_ASSERT_TRUE(FanControl_Evaluate(false,&config,&values));
    values.ambient=(EffectiveTemperature){.valid=true,.value=21};
    TemperatureSources_ComputeDifference(&values);
    TEST_ASSERT_FALSE(FanControl_Evaluate(true,&config,&values));
}
void test_DispatchRejectsInvalidPairsAndPreservesState(void) {
    const float pairs[][2]={{1,1},{-2,-1},{9.1f,1},{2,NAN},{2.05f,1}};
    for (unsigned i=0;i<sizeof(pairs)/sizeof(pairs[0]);++i)
        TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,FanCommands_Dispatch(&owner,(FanCommand){FAN_COMMAND_DELTA_PAIR,pairs[i][0],pairs[i][1]},true));
    TEST_ASSERT_EQUAL(0,mutations); TEST_ASSERT_EQUAL(20,config.fan_delta_on_decic);
}
void test_DispatchAtomicNegativePairsAndSourceBounds(void) {
    const float pairs[][2]={{2,1},{9,-9},{0,-1},{-1,-2}};
    for (unsigned i=0;i<4;++i) TEST_ASSERT_EQUAL(ESP_OK,FanCommands_Dispatch(&owner,(FanCommand){FAN_COMMAND_DELTA_PAIR,pairs[i][0],pairs[i][1]},true));
    TEST_ASSERT_EQUAL(-10,config.fan_delta_on_decic); TEST_ASSERT_EQUAL(-20,config.fan_delta_off_decic);
    TEST_ASSERT_EQUAL(ESP_OK,FanCommands_Dispatch(&owner,(FanCommand){FAN_COMMAND_OBJECT_SOURCE,2,0},true));
    TEST_ASSERT_EQUAL(ESP_OK,FanCommands_Dispatch(&owner,(FanCommand){FAN_COMMAND_AMBIENT_SOURCE,1,0},true));
    TEST_ASSERT_EQUAL(2,config.object_temperature_source); TEST_ASSERT_EQUAL(1,config.ambient_temperature_source);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,FanCommands_Dispatch(&owner,(FanCommand){FAN_COMMAND_OBJECT_SOURCE,4,0},true));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,FanCommands_Dispatch(&owner,(FanCommand){FAN_COMMAND_MODE,1.5f,0},true));
}
void test_ExternalTimeWrapAndFiniteBounds(void) {
    slots[0]=(ExternalTemperatureSlot){20,true,UINT32_MAX-49};
    TEST_ASSERT_EQUAL(TEMPERATURE_VALID,TemperatureSources_SlotState(&slots[0],50));
    TEST_ASSERT_FALSE(TemperatureSources_IsInputValid(NAN)); TEST_ASSERT_FALSE(TemperatureSources_IsInputValid(INFINITY));
    TEST_ASSERT_FALSE(TemperatureSources_IsInputValid(201)); TEST_ASSERT_TRUE(TemperatureSources_IsInputValid(-50));
}
