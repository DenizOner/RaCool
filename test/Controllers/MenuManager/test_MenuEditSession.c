#include <unity.h>
#include <string.h>
#include "Controllers/MenuManager/MenuEditSession.h"
#include "Controllers/ClimateController/FanCommands.h"

static SystemSettings live;
static unsigned commits,rawWrites,lastCount;
static esp_err_t saveResult;
static bool cancelDuringCommit,reenterDuringCommit;
static void *block_data(SystemSettings *settings,const char *name) {
    if(!strcmp(name,"hardware"))return &settings->hardware;
    if(!strcmp(name,"control"))return &settings->control;
    if(!strcmp(name,"system"))return &settings->system;
    return NULL;
}
static esp_err_t snapshot(IConfigurationManager *self,SystemSettings *out) {
    (void)self;*out=live;return ESP_OK;
}
static esp_err_t raw_write(IConfigurationManager *self,const char *name,const ConfigurationFieldUpdate *fields,size_t count,bool persist) {
    (void)self;(void)name;(void)fields;(void)count;(void)persist;++rawWrites;return ESP_FAIL;
}
static esp_err_t checked(IConfigurationManager *self,const char *name,const ConfigurationFieldUpdate *fields,const void *const *expected,size_t count,bool persist) {
    (void)self;TEST_ASSERT_TRUE(persist);++commits;lastCount=(unsigned)count;
    uint8_t *data=block_data(&live,name);if(!data)return ESP_ERR_INVALID_ARG;
    for(size_t i=0;i<count;++i)if(memcmp(data+fields[i].offset,expected[i],fields[i].size))return ESP_ERR_INVALID_STATE;
    if(reenterDuringCommit) {
        TEST_ASSERT_EQUAL(ESP_ERR_NOT_FINISHED,MenuEditSession_Commit());
        TEST_ASSERT_FALSE(MenuEditSession_Begin(UI_STATE_SET_TIMEOUT,self,&live));
        uint8_t replacement=19;
        ConfigurationFieldUpdate field={offsetof(HardwareConfigs,builtin_led_brightness),&replacement,1};
        IConfigurationManager *draft=MenuEditSession_Config(self);
        TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE,draft->UpdateFields(draft,"hardware",&field,1,true));
    }
    if(cancelDuringCommit)MenuEditSession_Cancel();
    if(saveResult!=ESP_OK)return saveResult;
    for(size_t i=0;i<count;++i)memcpy(data+fields[i].offset,fields[i].data,fields[i].size);
    return ESP_OK;
}
static IConfigurationManager owner={.GetSnapshot=snapshot,.UpdateFields=raw_write,.UpdateFieldsIfUnchanged=checked};
void setUp(void) {
    MenuEditSession_Cancel();memset(&live,0,sizeof(live));
    live.hardware.builtin_led_brightness=255;live.hardware.builtin_led_anim_mode=1;
    live.control.relay_close_temp=28;live.control.relay_open_temp=30;
    live.control.fan_delta_off_decic=10;live.control.fan_delta_on_decic=20;
    commits=rawWrites=lastCount=0;saveResult=ESP_OK;cancelDuringCommit=reenterDuringCommit=false;
}
void tearDown(void) {MenuEditSession_Cancel();TEST_ASSERT_EQUAL(0,rawWrites);}
static IConfigurationManager *begin(UIState state) {
    TEST_ASSERT_TRUE(MenuEditSession_Begin(state,&owner,&live));
    return MenuEditSession_Config(&owner);
}
static esp_err_t brightness(IConfigurationManager *config,uint8_t value) {
    ConfigurationFieldUpdate field={offsetof(HardwareConfigs,builtin_led_brightness),&value,sizeof(value)};
    return config->UpdateFields(config,"hardware",&field,1,true);
}
void test_MenuDraftStagesAndCommitsOneCheckedWrite(void) {
    IConfigurationManager *config=begin(UI_STATE_SET_BUILTIN_BRIGHTNESS);
    TEST_ASSERT_EQUAL(ESP_OK,brightness(config,80));TEST_ASSERT_EQUAL(ESP_OK,brightness(config,90));
    TEST_ASSERT_EQUAL(0,commits);TEST_ASSERT_EQUAL(255,live.hardware.builtin_led_brightness);
    SystemSettings frame;TEST_ASSERT_TRUE(MenuEditSession_Overlay(&frame));TEST_ASSERT_EQUAL(90,frame.hardware.builtin_led_brightness);
    TEST_ASSERT_EQUAL(ESP_OK,MenuEditSession_Commit());TEST_ASSERT_EQUAL(1,commits);TEST_ASSERT_EQUAL(1,lastCount);
    TEST_ASSERT_EQUAL(90,live.hardware.builtin_led_brightness);TEST_ASSERT_EQUAL(UI_STATE_COUNT,MenuEditSession_State());
}
void test_MenuDraftCancelAndUnchangedSelectionNeverWrite(void) {
    IConfigurationManager *config=begin(UI_STATE_SET_BUILTIN_BRIGHTNESS);
    TEST_ASSERT_EQUAL(ESP_OK,brightness(config,80));MenuEditSession_Cancel();
    SystemSettings frame;TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE,config->GetSnapshot(config,&frame));
    TEST_ASSERT_EQUAL(255,live.hardware.builtin_led_brightness);TEST_ASSERT_EQUAL_PTR(&owner,MenuEditSession_Config(&owner));
    begin(UI_STATE_SET_TIMEOUT);TEST_ASSERT_EQUAL(ESP_OK,MenuEditSession_Commit());TEST_ASSERT_EQUAL(0,commits);
}
void test_MenuDraftStaleOwnerIsNotOverwritten(void) {
    IConfigurationManager *config=begin(UI_STATE_SET_BUILTIN_BRIGHTNESS);brightness(config,80);
    live.hardware.builtin_led_brightness=200;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE,MenuEditSession_Commit());TEST_ASSERT_EQUAL(200,live.hardware.builtin_led_brightness);
    TEST_ASSERT_EQUAL(UI_STATE_SET_BUILTIN_BRIGHTNESS,MenuEditSession_State());
}
void test_MenuDraftUnrelatedOwnerChangeSurvives(void) {
    IConfigurationManager *config=begin(UI_STATE_SET_BUILTIN_BRIGHTNESS);brightness(config,80);
    live.hardware.ext_led_brightness=40;
    TEST_ASSERT_EQUAL(ESP_OK,MenuEditSession_Commit());TEST_ASSERT_EQUAL(40,live.hardware.ext_led_brightness);
}
void test_MenuDraftFailedPersistenceKeepsDraftForRetry(void) {
    IConfigurationManager *config=begin(UI_STATE_SET_BUILTIN_BRIGHTNESS);brightness(config,80);saveResult=ESP_FAIL;
    TEST_ASSERT_EQUAL(ESP_FAIL,MenuEditSession_Commit());TEST_ASSERT_EQUAL(255,live.hardware.builtin_led_brightness);
    TEST_ASSERT_EQUAL(ESP_FAIL,MenuEditSession_Result(UI_STATE_SET_BUILTIN_BRIGHTNESS));
    saveResult=ESP_OK;TEST_ASSERT_EQUAL(ESP_OK,MenuEditSession_Commit());TEST_ASSERT_EQUAL(80,live.hardware.builtin_led_brightness);
}
void test_MenuDraftRejectsBadBatchWithoutPartialMutation(void) {
    IConfigurationManager *config=begin(UI_STATE_SET_BUILTIN_BRIGHTNESS);uint8_t value=30;
    ConfigurationFieldUpdate fields[]={{offsetof(HardwareConfigs,builtin_led_brightness),&value,1},{SIZE_MAX,&value,2}};
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,config->UpdateFields(config,"hardware",fields,2,true));
    SystemSettings frame;TEST_ASSERT_TRUE(MenuEditSession_Overlay(&frame));TEST_ASSERT_EQUAL(255,frame.hardware.builtin_led_brightness);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE,config->UpdateFields(config,"missing",fields,1,true));
    TEST_ASSERT_EQUAL(ESP_OK,MenuEditSession_Commit());TEST_ASSERT_EQUAL(0,commits);
}
void test_MenuDraftFanWizardCommitsModeAndPairTogether(void) {
    IConfigurationManager *config=begin(UI_STATE_FAN_MODE);
    TEST_ASSERT_EQUAL(ESP_OK,FanCommands_Dispatch(config,(FanCommand){FAN_COMMAND_MODE,CLIMATE_FAN_AMBIENT_DELTA,0},true));
    TEST_ASSERT_EQUAL(0,live.control.fan_strategy);TEST_ASSERT_EQUAL(0,commits);
    TEST_ASSERT_EQUAL(UI_STATE_FAN_DELTA_OFF,MenuEditSession_FanRange());TEST_ASSERT_TRUE(MenuEditSession_AutoCommit());
    TEST_ASSERT_EQUAL(ESP_OK,FanCommands_Dispatch(config,(FanCommand){FAN_COMMAND_DELTA_PAIR,1.5f,0.5f},true));
    TEST_ASSERT_EQUAL(1,commits);TEST_ASSERT_EQUAL(5,lastCount);TEST_ASSERT_EQUAL(1,live.control.fan_strategy);
    TEST_ASSERT_EQUAL(5,live.control.fan_delta_off_decic);TEST_ASSERT_EQUAL(15,live.control.fan_delta_on_decic);
}
void test_MenuDraftFixedFanModeSkipsThresholds(void) {
    IConfigurationManager *config=begin(UI_STATE_FAN_MODE);
    TEST_ASSERT_EQUAL(ESP_OK,FanCommands_Dispatch(config,(FanCommand){FAN_COMMAND_MODE,CLIMATE_FAN_MANUAL_OFF,0},true));
    TEST_ASSERT_EQUAL(UI_STATE_COUNT,MenuEditSession_FanRange());TEST_ASSERT_FALSE(MenuEditSession_AutoCommit());
    TEST_ASSERT_EQUAL(ESP_OK,MenuEditSession_Commit());TEST_ASSERT_EQUAL(3,lastCount);
    TEST_ASSERT_TRUE(live.control.fan_manual_mode);TEST_ASSERT_FALSE(live.control.fan_manual_state);
}
void test_MenuDraftRangeCommitRejectsConcurrentPairChange(void) {
    IConfigurationManager *config=begin(UI_STATE_SET_LOW_TEMP);live.control.relay_open_temp=40;
    float low=29,high=32;
    ConfigurationFieldUpdate fields[]={{offsetof(ControlConfigs,relay_close_temp),&low,sizeof(low)},{offsetof(ControlConfigs,relay_open_temp),&high,sizeof(high)}};
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE,config->UpdateFields(config,"control",fields,2,true));
    TEST_ASSERT_EQUAL(28,live.control.relay_close_temp);TEST_ASSERT_EQUAL(40,live.control.relay_open_temp);
}
void test_MenuDraftCancelDuringAdmittedSaveDrainsBeforeReuse(void) {
    IConfigurationManager *config=begin(UI_STATE_SET_BUILTIN_BRIGHTNESS);brightness(config,80);cancelDuringCommit=true;
    TEST_ASSERT_EQUAL(ESP_OK,MenuEditSession_Commit());TEST_ASSERT_EQUAL(80,live.hardware.builtin_led_brightness);
    TEST_ASSERT_TRUE(MenuEditSession_Begin(UI_STATE_SET_TIMEOUT,&owner,&live));
}
void test_MenuDraftRejectsForeignSessionAndMissingCheckedOwner(void) {
    IConfigurationManager incomplete={0};
    TEST_ASSERT_FALSE(MenuEditSession_Begin(UI_STATE_SET_TIMEOUT,&incomplete,&live));
    TEST_ASSERT_FALSE(MenuEditSession_Begin(UI_STATE_FACTORY_RESET,&owner,&live));
    begin(UI_STATE_SET_TIMEOUT);
    TEST_ASSERT_FALSE(MenuEditSession_Begin(UI_STATE_SET_BUILTIN_MODE,&owner,&live));
    TEST_ASSERT_EQUAL_PTR(&incomplete,MenuEditSession_Config(&incomplete));
}

void test_MenuDraftMergeKeepsFreshUnrelatedVisibilityAndSettings(void) {
    IConfigurationManager *config=begin(UI_STATE_SET_BUILTIN_BRIGHTNESS);brightness(config,80);
    live.hardware.builtin_led_anim_mode=0;live.hardware.ext_led_brightness=43;
    SystemSettings frame=live;
    TEST_ASSERT_TRUE(MenuEditSession_Merge(&frame));
    TEST_ASSERT_EQUAL(80,frame.hardware.builtin_led_brightness);
    TEST_ASSERT_EQUAL(0,frame.hardware.builtin_led_anim_mode);
    TEST_ASSERT_EQUAL(43,frame.hardware.ext_led_brightness);
}
void test_MenuDraftDuplicateBatchIsRejectedWithoutStaging(void) {
    IConfigurationManager *config=begin(UI_STATE_SET_BUILTIN_BRIGHTNESS);uint8_t value=30;
    ConfigurationFieldUpdate fields[]={{offsetof(HardwareConfigs,builtin_led_brightness),&value,1},{offsetof(HardwareConfigs,builtin_led_brightness),&value,1}};
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,config->UpdateFields(config,"hardware",fields,2,true));
    TEST_ASSERT_EQUAL(ESP_OK,MenuEditSession_Commit());TEST_ASSERT_EQUAL(0,commits);
}

void test_MenuCommitPayloadSurvivesRejectedReentrantSaveAndWrite(void) {
    IConfigurationManager *config=begin(UI_STATE_SET_BUILTIN_BRIGHTNESS);
    TEST_ASSERT_EQUAL(ESP_OK,brightness(config,80));reenterDuringCommit=true;
    TEST_ASSERT_EQUAL(ESP_OK,MenuEditSession_Commit());
    TEST_ASSERT_EQUAL(80,live.hardware.builtin_led_brightness);TEST_ASSERT_EQUAL(1,commits);
    TEST_ASSERT_EQUAL(UI_STATE_COUNT,MenuEditSession_State());
}
