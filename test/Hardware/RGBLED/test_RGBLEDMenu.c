#include "Controllers/MenuManager/MenuRangeEditor.h"
#include <math.h>
#include <unity.h>
#include "Hardware/RGBLED/RGBLEDMenuPlugin.c"

static unsigned builtin_animation_previews,external_animation_previews;
static unsigned builtin_brightness_previews,external_brightness_previews;
static uint8_t preview_brightness;
static esp_err_t builtin_brightness_preview(IBuiltinLED *self,uint8_t value,uint32_t duration) {
    (void)self; TEST_ASSERT_EQUAL(3000,duration); ++builtin_brightness_previews; preview_brightness=value; return ESP_OK;
}
static esp_err_t external_brightness_preview(IRGBLED *self,uint8_t value,uint32_t duration) {
    (void)self; TEST_ASSERT_EQUAL(3000,duration); ++external_brightness_previews; preview_brightness=value; return ESP_OK;
}
static uint8_t preview_animation;
static esp_err_t builtin_preview(IBuiltinLED *self,BuiltinLEDAnimationType type,uint32_t duration) {
    (void)self; TEST_ASSERT_EQUAL(5000,duration); ++builtin_animation_previews; preview_animation=(uint8_t)type; return ESP_OK;
}
static esp_err_t external_preview(IRGBLED *self,RGBLEDAnimType type,uint32_t duration) {
    (void)self; TEST_ASSERT_EQUAL(5000,duration); ++external_animation_previews; preview_animation=(uint8_t)type; return ESP_OK;
}
static IBuiltinLED builtin={.PreviewAnimation=builtin_preview,.PreviewBrightness=builtin_brightness_preview};
static SystemSettings saved;
static IConfigurationManager config;
static esp_err_t snapshot_result, save_result;
static unsigned saves;
static size_t saved_count;
static bool stop_during_save;
static unsigned mode_requests;
static esp_err_t request_mode(IRGBLED *self) { (void)self; ++mode_requests; return ESP_OK; }
static unsigned preview_requests;
static RGBColor preview_color;
static esp_err_t preview_result;
static esp_err_t request_preview(IRGBLED *self, RGBColor color, uint32_t duration) {
    (void)self; TEST_ASSERT_EQUAL(3000,duration);
    ++preview_requests; preview_color=color; return preview_result;
}
static IRGBLED rgb = {.ApplySelectedMode=request_mode,.PreviewColor=request_preview,.PreviewAnimation=external_preview,.PreviewBrightness=external_brightness_preview};
IRGBLED *GetRGBLEDService(void) { return &rgb; }

static MenuManagerError register_batch(IMenuManager *self, const MenuPluginRegistration *entries, size_t count) {
    (void)self; return MenuPluginManager_RegisterBatch(entries, count);
}
static MenuManagerError unregister_batch(IMenuManager *self, const MenuPluginRegistration *entries, size_t count) {
    (void)self; return MenuPluginManager_UnregisterBatch(entries, count);
}
static bool range_editing;
static void range_set_editing(IMenuManager *self,bool editing) { (void)self; range_editing=editing; }
static IMenuManager menu = {.RegisterPluginBatch = register_batch, .UnregisterPluginBatch = unregister_batch, .SetEditing=range_set_editing};
static IService *lookup_service(const char *name) { return strcmp(name,SVC_NAME_BUILTIN_LED)==0 ? (IService *)&builtin : mocked_ServiceLocator_GetService_fake.return_val; }

static esp_err_t snapshot(IConfigurationManager *self, SystemSettings *output) {
    (void)self;
    if (snapshot_result == ESP_OK) *output = saved;
    return snapshot_result;
}
static esp_err_t update(IConfigurationManager *self, const char *block, const ConfigurationFieldUpdate *fields, size_t count, bool persist) {
    (void)self;
    TEST_ASSERT_EQUAL_STRING("hardware", block);
    TEST_ASSERT_TRUE(persist);
    TEST_ASSERT_TRUE(count > 0 && count <= 2);
    saves++; saved_count = count;
    if (stop_during_save) {
        TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, RGBLEDMenuPlugin_Deinit());
        TEST_ASSERT_TRUE(initialized);
        TEST_ASSERT_TRUE(MenuPluginManager_HasPlugin(UI_STATE_SET_LED_MODE));
    }
    if (save_result == ESP_OK)
        for (size_t i = 0; i < count; ++i)
            memcpy((uint8_t *)&saved.hardware + fields[i].offset, fields[i].data, fields[i].size);
    return save_result;
}
static esp_err_t compare_update(IConfigurationManager *self,const char *block,const ConfigurationFieldUpdate *fields,const void *const *expected,size_t count,bool persist) {
    (void)expected; return update(self,block,fields,count,persist);
}
void setUp(void) {
    builtin_animation_previews=external_animation_previews=0;
    builtin_brightness_previews=external_brightness_previews=0;
    mode_requests = preview_requests = 0; preview_result=ESP_OK; RGBZoneSelection_Clear();
    initialized = false; menuManager = NULL;
    MenuPluginManager_Init();
    memset(&saved, 0, sizeof(saved));
    saved.hardware.builtin_led_brightness = 123;
    saved.hardware.ext_led_brightness = 100;
    config = (IConfigurationManager){.GetSnapshot = snapshot, .UpdateFields = update,.UpdateFieldsIfUnchanged=compare_update};
    snapshot_result = save_result = ESP_OK; saves = 0; saved_count = 0; stop_during_save = false;
    RESET_FAKE(mocked_ServiceLocator_GetService);
    RESET_FAKE(mocked_ConfigurationManager_GetInstance);
    mocked_ServiceLocator_GetService_fake.return_val=(IService *)&menu;
    mocked_ServiceLocator_GetService_fake.custom_fake=lookup_service;
    mocked_ConfigurationManager_GetInstance_fake.return_val = &config;
}
void tearDown(void) {}
static void dispatch(UIState state, int direction);
void test_LEDMenuBrightnessPreviewUsesDraftWithoutSaving(void) {
    TEST_ASSERT_EQUAL(ESP_OK,RGBLEDMenuPlugin_Init());
    const UIState states[]={UI_STATE_SET_BUILTIN_BRIGHTNESS,UI_STATE_SET_RGB_BRIGHTNESS};
    for (unsigned i=0;i<2;++i) {
        SystemSettings before=saved,draft;
        TEST_ASSERT_TRUE(MenuEditSession_Begin(states[i],&config,&saved));
        dispatch(states[i],1);
        TEST_ASSERT_EQUAL_MEMORY(&before,&saved,sizeof(saved));
        TEST_ASSERT_EQUAL(0,saves);
        TEST_ASSERT_EQUAL(ESP_OK,MenuEditSession_Config(&config)->GetSnapshot(MenuEditSession_Config(&config),&draft));
        TEST_ASSERT_EQUAL(i ? draft.hardware.ext_led_brightness : draft.hardware.builtin_led_brightness,preview_brightness);
        TEST_ASSERT_EQUAL(1,i ? external_brightness_previews : builtin_brightness_previews);
        MenuEditSession_Cancel();
    }
}
void test_LEDMenuEveryFreeXYPairClampsAndPreservesSettingsOnFailedSave(void) {
    TEST_ASSERT_EQUAL(ESP_OK, RGBLEDMenuPlugin_Init());
    for (unsigned zone=0;zone<RGB_ZONE_COUNT;++zone) {
        size_t ox,oy;
        TEST_ASSERT_TRUE(SystemConfigs_RGBZoneOffsets(zone,&ox,&oy));
        uint16_t x=RGB_XY_MAX_COORDINATE-1,y=2;
        memcpy((uint8_t *)&saved.hardware+ox,&x,sizeof(x));
        memcpy((uint8_t *)&saved.hardware+oy,&y,sizeof(y));
        UIState state=(UIState)(UI_STATE_RGB_COLD_X+zone*2);
        dispatch(state,1);
        TEST_ASSERT_EQUAL(2,saved_count);
        TEST_ASSERT_TRUE(SystemConfigs_GetRGBZoneXY(&saved.hardware,zone,&x,&y));
        TEST_ASSERT_EQUAL(RGB_XY_MAX_COORDINATE,x);
        TEST_ASSERT_EQUAL(2,y);
        dispatch((UIState)(state+1),-1);
        TEST_ASSERT_TRUE(SystemConfigs_GetRGBZoneXY(&saved.hardware,zone,&x,&y));
        TEST_ASSERT_EQUAL(1,y);
        HardwareConfigs before=saved.hardware;
        save_result=ESP_FAIL;dispatch(state,-1);
        TEST_ASSERT_EQUAL_MEMORY(&before,&saved.hardware,sizeof(before));
        save_result=ESP_OK;
    }
}
void test_LEDMenuAllThreeExternalModesAndWorkerReselection(void) {
    TEST_ASSERT_EQUAL(ESP_OK, RGBLEDMenuPlugin_Init());
    for (unsigned i = 1; i <= 3; ++i) {
        dispatch(UI_STATE_SET_LED_MODE, 1);
        TEST_ASSERT_EQUAL(i % 3, saved.hardware.ext_led_mode);
        TEST_ASSERT_EQUAL(i, mode_requests);
    }
    dispatch(UI_STATE_SET_LED_MODE, -1);
    TEST_ASSERT_EQUAL(EXT_LED_MODE_OFF, saved.hardware.ext_led_mode);
    TEST_ASSERT_EQUAL(4, mode_requests);
    save_result = ESP_FAIL;
    dispatch(UI_STATE_SET_LED_MODE, -1);
    TEST_ASSERT_EQUAL(EXT_LED_MODE_OFF, saved.hardware.ext_led_mode);
    TEST_ASSERT_EQUAL(4, mode_requests);
}
static void dispatch(UIState state, int direction) {
    MenuPluginEvent event = {.state = state, .action = MENU_PLUGIN_ACTION_ENCODER, .direction = direction};
    TEST_ASSERT_TRUE(MenuPluginManager_DispatchEvent(&event));
}
static void busy_init(UIState state, int8_t direction, bool pressed) {
    (void)state; (void)direction; (void)pressed;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, RGBLEDMenuPlugin_Init());
    TEST_ASSERT_FALSE(initialized);
    TEST_ASSERT_FALSE(MenuPluginManager_HasPlugin(UI_STATE_SET_BUILTIN_BRIGHTNESS));
}
void test_LEDMenuRegistrationIsAtomicAndRetryable(void) {
    mocked_ServiceLocator_GetService_fake.return_val = NULL;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, RGBLEDMenuPlugin_Init());
    mocked_ServiceLocator_GetService_fake.return_val=(IService *)&menu;
    mocked_ServiceLocator_GetService_fake.custom_fake=lookup_service;
    TEST_ASSERT_EQUAL(MENU_OK, MenuPluginManager_RegisterLegacy(UI_STATE_SET_LED_MODE, "Busy", busy_init));
    dispatch(UI_STATE_SET_LED_MODE, 1);
    TEST_ASSERT_EQUAL(ESP_OK, RGBLEDMenuPlugin_Init());
    for (size_t i = 0; i < sizeof(menuEntries) / sizeof(menuEntries[0]); ++i)
        TEST_ASSERT_TRUE(MenuPluginManager_HasPlugin(menuEntries[i].state));
    TEST_ASSERT_EQUAL(ESP_OK, RGBLEDMenuPlugin_Deinit());
    TEST_ASSERT_FALSE(initialized);
    TEST_ASSERT_FALSE(MenuPluginManager_HasPlugin(UI_STATE_SET_BUILTIN_BRIGHTNESS));
}
void test_LEDMenuFailedSaveOrSnapshotPreservesLiveSettings(void) {
    TEST_ASSERT_EQUAL(ESP_OK, RGBLEDMenuPlugin_Init());
    save_result = ESP_FAIL;
    dispatch(UI_STATE_SET_RGB_BRIGHTNESS, 1);
    TEST_ASSERT_EQUAL(100, saved.hardware.ext_led_brightness);
    TEST_ASSERT_EQUAL(1, saves);
    snapshot_result = ESP_FAIL;
    dispatch(UI_STATE_SET_RGB_BRIGHTNESS, 1);
    TEST_ASSERT_EQUAL(1, saves);
    snapshot_result = save_result = ESP_OK;
    dispatch(UI_STATE_SET_RGB_BRIGHTNESS, 1);
    TEST_ASSERT_EQUAL(102, saved.hardware.ext_led_brightness);
    TEST_ASSERT_EQUAL(1, saved_count);
    dispatch(UI_STATE_SET_BUILTIN_BRIGHTNESS, -1);
    TEST_ASSERT_EQUAL(120, saved.hardware.builtin_led_brightness);
}
void test_LEDMenuStopDuringCallbackPreservesRegistrationUntilReturn(void) {
    TEST_ASSERT_EQUAL(ESP_OK, RGBLEDMenuPlugin_Init());
    stop_during_save = true;
    dispatch(UI_STATE_SET_BUILTIN_BRIGHTNESS, 1);
    TEST_ASSERT_EQUAL(125, saved.hardware.builtin_led_brightness);
    TEST_ASSERT_EQUAL(ESP_OK, RGBLEDMenuPlugin_Deinit());
    TEST_ASSERT_FALSE(initialized);
    TEST_ASSERT_NULL(menuManager);
    TEST_ASSERT_EQUAL(ESP_OK, RGBLEDMenuPlugin_Init());
}
void test_LEDMenuColorFieldsCommitTogetherAndClampBrightness(void) {
    TEST_ASSERT_EQUAL(ESP_OK, RGBLEDMenuPlugin_Init());
    TEST_ASSERT_EQUAL(81, LEDBrightness_Percent(207, 255));
    TEST_ASSERT_EQUAL(100, LEDBrightness_Percent(255, 254));
    TEST_ASSERT_EQUAL(0, LEDBrightness_Step(0, -1, 254));
    TEST_ASSERT_EQUAL(255, LEDBrightness_Step(252, 1, 255));
    unsigned unchanged_saves = saves;
    dispatch(UI_STATE_SET_BUILTIN_BRIGHTNESS, 0);
    MenuPluginEvent unchanged_save = {.state=UI_STATE_SET_BUILTIN_BRIGHTNESS,.action=MENU_PLUGIN_ACTION_BUTTON};
    TEST_ASSERT_TRUE(MenuPluginManager_DispatchEvent(&unchanged_save));
    TEST_ASSERT_EQUAL(123, saved.hardware.builtin_led_brightness);
    TEST_ASSERT_EQUAL(unchanged_saves, saves);
    dispatch(UI_STATE_SET_RGB_COLD, 0);
    dispatch(UI_STATE_SET_RGB_COLD, 1);
    TEST_ASSERT_EQUAL(0,saves);
    TEST_ASSERT_EQUAL(1,preview_requests);
    MenuPluginEvent save={.state=UI_STATE_SET_RGB_COLD,.action=MENU_PLUGIN_ACTION_BUTTON};
    TEST_ASSERT_TRUE(MenuPluginManager_DispatchEvent(&save));
    TEST_ASSERT_EQUAL(2, saved_count);
    TEST_ASSERT_TRUE(saved.hardware.ext_led_color_cold_x != 0);
    TEST_ASSERT_TRUE(saved.hardware.ext_led_color_cold_y != 0);
    saved.hardware.builtin_led_brightness = 255;
    unsigned before = saves;
    dispatch(UI_STATE_SET_BUILTIN_BRIGHTNESS, 1);
    TEST_ASSERT_EQUAL(before, saves);
    saved.hardware.ext_led_brightness = 254;
    dispatch(UI_STATE_SET_RGB_BRIGHTNESS, 1);
    TEST_ASSERT_EQUAL(254, saved.hardware.ext_led_brightness);
}

void test_LEDMenuBothCatalogsVisitSameOrderIncludingOff(void) {
    TEST_ASSERT_EQUAL(ESP_OK, RGBLEDMenuPlugin_Init());
    saved.hardware.builtin_led_idle_anim = 255;
    saved.hardware.ext_led_anim = RGBLED_ANIM_NONE;
    size_t count;
    const LEDAnimationCatalogEntry *entries = LEDAnimationCatalog_Entries(&count);
    TEST_ASSERT_EQUAL(BUILTIN_LED_ANIM_COUNT + 1, count);
    for (size_t index = 1; index <= count; ++index) {
        dispatch(UI_STATE_SET_BUILTIN_IDLE, 1);
        dispatch(UI_STATE_SET_LED_ANIM, 1);
        const LEDAnimationCatalogEntry *entry = &entries[index % count];
        TEST_ASSERT_EQUAL(entry->builtin_id, saved.hardware.builtin_led_idle_anim);
        TEST_ASSERT_EQUAL(entry->external_id, saved.hardware.ext_led_anim);
        TEST_ASSERT_EQUAL_STRING(LEDAnimationCatalog_Label(entry->builtin_id, true),
                                 LEDAnimationCatalog_Label(entry->external_id, false));
    }
    dispatch(UI_STATE_SET_BUILTIN_IDLE, -1);
    dispatch(UI_STATE_SET_LED_ANIM, -1);
    TEST_ASSERT_EQUAL(entries[count - 1].builtin_id, saved.hardware.builtin_led_idle_anim);
    TEST_ASSERT_EQUAL(entries[count - 1].external_id, saved.hardware.ext_led_anim);
}

void test_LEDMenuUnknownStoredSelectionRecoversWithoutOfferingReservedTemperature(void) {
    TEST_ASSERT_EQUAL(ESP_OK, RGBLEDMenuPlugin_Init());
    saved.hardware.builtin_led_fault_anim = 254;
    saved.hardware.ext_led_anim = RGBLED_ANIM_TEMPERATURE;
    dispatch(UI_STATE_SET_BUILTIN_FAULT, 1);
    dispatch(UI_STATE_SET_LED_ANIM, 1);
    TEST_ASSERT_EQUAL(HEARTBEAT_RED, saved.hardware.builtin_led_fault_anim);
    TEST_ASSERT_EQUAL(RGBLED_ANIM_HEARTBEAT_RED, saved.hardware.ext_led_anim);
    TEST_ASSERT_EQUAL_STRING("BILINMIYOR", LEDAnimationCatalog_Label(254, true));
}

void test_LEDMenuFiveZoneDraftsPreviewWithoutSavingAndFailedSavePreservesOwner(void) {
    TEST_ASSERT_EQUAL(ESP_OK,RGBLEDMenuPlugin_Init());
    UIState states[]={UI_STATE_SET_RGB_COLD,UI_STATE_SET_RGB_WARM,UI_STATE_SET_RGB_HOT,UI_STATE_SET_RGB_DANGER,UI_STATE_SET_RGB_FREEZE};
    for (unsigned i=0;i<5;++i) {
        HardwareConfigs before=saved.hardware;
        unsigned old_saves=saves;
        dispatch(states[i],0); dispatch(states[i],-1);
        TEST_ASSERT_EQUAL_MEMORY(&before,&saved.hardware,sizeof(before));
        TEST_ASSERT_EQUAL(old_saves,saves);
        uint16_t x,y; TEST_ASSERT_TRUE(RGBZoneSelection_Read(states[i],&x,&y));
        RGBColor expected=RGBLEDColorEngine_GetPresetColor(RGBLEDColorEngine_XYtoNearestPreset(x,y));
        TEST_ASSERT_EQUAL_MEMORY(&expected,&preview_color,sizeof(expected));
        save_result=ESP_FAIL;
        MenuPluginEvent save={.state=states[i],.action=MENU_PLUGIN_ACTION_BUTTON};
        TEST_ASSERT_TRUE(MenuPluginManager_DispatchEvent(&save));
        TEST_ASSERT_EQUAL_MEMORY(&before,&saved.hardware,sizeof(before));
        TEST_ASSERT_TRUE(RGBZoneSelection_Read(states[i],&x,&y));
        save_result=ESP_OK;
        dispatch(states[i],0); dispatch(states[i],1);
        TEST_ASSERT_TRUE(MenuPluginManager_DispatchEvent(&save));
        TEST_ASSERT_EQUAL(2,saved_count);
    }
    TEST_ASSERT_EQUAL(15,preview_requests);
}

void test_LEDMenuUnrotatedCustomColorRemainsExactAndPreviewFailureNeverPersists(void) {
    TEST_ASSERT_EQUAL(ESP_OK,RGBLEDMenuPlugin_Init());
    saved.hardware.ext_led_color_cold_x=1234; saved.hardware.ext_led_color_cold_y=4321;
    dispatch(UI_STATE_SET_RGB_COLD,0);
    MenuPluginEvent save={.state=UI_STATE_SET_RGB_COLD,.action=MENU_PLUGIN_ACTION_BUTTON};
    TEST_ASSERT_TRUE(MenuPluginManager_DispatchEvent(&save));
    TEST_ASSERT_EQUAL(1234,saved.hardware.ext_led_color_cold_x);
    TEST_ASSERT_EQUAL(4321,saved.hardware.ext_led_color_cold_y);
    HardwareConfigs original=saved.hardware;
    preview_result=ESP_ERR_TIMEOUT;
    dispatch(UI_STATE_SET_RGB_COLD,0); dispatch(UI_STATE_SET_RGB_COLD,1);
    TEST_ASSERT_EQUAL_MEMORY(&original,&saved.hardware,sizeof(original));
    TEST_ASSERT_EQUAL(1,saves);
    dispatch(UI_STATE_SET_RGB_COLD,0);
    uint16_t x,y; TEST_ASSERT_TRUE(RGBZoneSelection_Read(UI_STATE_SET_RGB_COLD,&x,&y));
    TEST_ASSERT_EQUAL(1234,x); TEST_ASSERT_EQUAL(4321,y);
}

void test_LEDMenuCustomSlotSurvivesRotationUntilPresetSave(void) {
    TEST_ASSERT_EQUAL(ESP_OK,RGBLEDMenuPlugin_Init());
    saved.hardware.ext_led_color_cold_x=1234;
    saved.hardware.ext_led_color_cold_y=4321;
    dispatch(UI_STATE_SET_RGB_COLD,0);
    uint16_t x,y;
    for (int i=0;i<RGB_COLOR_COUNT;++i) dispatch(UI_STATE_SET_RGB_COLD,1);
    TEST_ASSERT_TRUE(RGBZoneSelection_Read(UI_STATE_SET_RGB_COLD,&x,&y));
    TEST_ASSERT_TRUE(x!=1234 || y!=4321);
    dispatch(UI_STATE_SET_RGB_COLD,1);
    TEST_ASSERT_TRUE(RGBZoneSelection_Read(UI_STATE_SET_RGB_COLD,&x,&y));
    TEST_ASSERT_EQUAL(1234,x); TEST_ASSERT_EQUAL(4321,y);
    dispatch(UI_STATE_SET_RGB_COLD,1);
    MenuPluginEvent save={.state=UI_STATE_SET_RGB_COLD,.action=MENU_PLUGIN_ACTION_BUTTON};
    unsigned requests=preview_requests;
    TEST_ASSERT_TRUE(MenuPluginManager_DispatchEvent(&save));
    TEST_ASSERT_EQUAL(requests+1,preview_requests);
    TEST_ASSERT_TRUE(RGBLEDColorEngine_XYtoExactPreset(saved.hardware.ext_led_color_cold_x,saved.hardware.ext_led_color_cold_y)>=0);
    dispatch(UI_STATE_SET_RGB_COLD,0);
    for (int i=0;i<RGB_COLOR_COUNT;++i) dispatch(UI_STATE_SET_RGB_COLD,1);
    TEST_ASSERT_TRUE(RGBZoneSelection_Read(UI_STATE_SET_RGB_COLD,&x,&y));
    TEST_ASSERT_EQUAL(saved.hardware.ext_led_color_cold_x,x);
    TEST_ASSERT_EQUAL(saved.hardware.ext_led_color_cold_y,y);
}

void test_LEDMenuAnimationPreviewsUseIndependentWorkersOnRotationAndSave(void) {
    TEST_ASSERT_EQUAL(ESP_OK,RGBLEDMenuPlugin_Init());
    UIState states[]={UI_STATE_SET_BUILTIN_STARTUP,UI_STATE_SET_BUILTIN_IDLE,UI_STATE_SET_BUILTIN_CONNECTING,UI_STATE_SET_BUILTIN_FAULT};
    for (unsigned i=0;i<4;++i) {
        dispatch(states[i],1);
        TEST_ASSERT_EQUAL(i*2+1,builtin_animation_previews);
        TEST_ASSERT_EQUAL(0,external_animation_previews);
        MenuPluginEvent save={.state=states[i],.action=MENU_PLUGIN_ACTION_BUTTON};
        TEST_ASSERT_TRUE(MenuPluginManager_DispatchEvent(&save));
        TEST_ASSERT_EQUAL(i*2+2,builtin_animation_previews);
    }
    dispatch(UI_STATE_SET_LED_ANIM,1);
    TEST_ASSERT_EQUAL(1,external_animation_previews);
    TEST_ASSERT_EQUAL(saved.hardware.ext_led_anim,preview_animation);
    MenuPluginEvent save={.state=UI_STATE_SET_LED_ANIM,.action=MENU_PLUGIN_ACTION_BUTTON};
    TEST_ASSERT_TRUE(MenuPluginManager_DispatchEvent(&save));
    TEST_ASSERT_EQUAL(2,external_animation_previews);
    save_result=ESP_FAIL;
    dispatch(UI_STATE_SET_LED_ANIM,1);
    TEST_ASSERT_EQUAL(2,external_animation_previews);
}

void test_LEDMenuRangesStageBothEndpointsAndRetainFailedSave(void) {
    TEST_ASSERT_EQUAL(ESP_OK,RGBLEDMenuPlugin_Init());
    const UIState states[]={UI_STATE_SET_RGB_TEMP_COLD,UI_STATE_SET_RGB_TEMP_WARM,UI_STATE_SET_RGB_TEMP_HOT};
    const float starts[]={17,35,50};
    for (unsigned i=0;i<3;++i) {
        float low=starts[i],high=low+2,oldLow,oldHigh; size_t a,b;
        TEST_ASSERT_TRUE(MenuRangeEditor_Values(states[i],&saved,&oldLow,&oldHigh,&a,&b));
        memcpy((uint8_t *)&saved.hardware+a,&low,sizeof(low)); memcpy((uint8_t *)&saved.hardware+b,&high,sizeof(high));
        range_editing=true; unsigned before=saves; dispatch(states[i],0); dispatch(states[i],1);
        TEST_ASSERT_EQUAL(before,saves);
        MenuPluginEvent press={.state=states[i],.action=MENU_PLUGIN_ACTION_BUTTON};
        TEST_ASSERT_TRUE(MenuPluginManager_DispatchEvent(&press)); dispatch(states[i],-1);
        save_result=ESP_FAIL; TEST_ASSERT_TRUE(MenuPluginManager_DispatchEvent(&press));
        TEST_ASSERT_TRUE(MenuRangeEditor_Values(states[i],&saved,&oldLow,&oldHigh,&a,&b));
        TEST_ASSERT_FLOAT_WITHIN(0.001f,low,oldLow); TEST_ASSERT_FLOAT_WITHIN(0.001f,high,oldHigh);
        int16_t draftLow,draftHigh; unsigned phase;
        TEST_ASSERT_TRUE(MenuRangeEditor_Read(states[i],&draftLow,&draftHigh,&phase)); TEST_ASSERT_EQUAL(3,phase); TEST_ASSERT_TRUE(range_editing);
        save_result=ESP_OK; TEST_ASSERT_TRUE(MenuPluginManager_DispatchEvent(&press));
        TEST_ASSERT_EQUAL(2,saved_count); TEST_ASSERT_FALSE(range_editing);
        TEST_ASSERT_TRUE(MenuRangeEditor_Values(states[i],&saved,&oldLow,&oldHigh,&a,&b));
        TEST_ASSERT_FLOAT_WITHIN(0.001f,low+0.1f,oldLow); TEST_ASSERT_FLOAT_WITHIN(0.001f,high-0.1f,oldHigh);
        TEST_ASSERT_FALSE(MenuRangeEditor_Read(states[i],&draftLow,&draftHigh,&phase));
    }
}
void test_LEDMenuRangeClampsGapAndRejectsExternalOwnerChange(void) {
    saved.hardware.ext_led_temp_cold_min=7; saved.hardware.ext_led_temp_cold_max=7.1f;
    TEST_ASSERT_TRUE(MenuRangeEditor_Begin(UI_STATE_SET_RGB_TEMP_COLD,&saved));
    TEST_ASSERT_TRUE(MenuRangeEditor_Rotate(UI_STATE_SET_RGB_TEMP_COLD,10000));
    int16_t low,high; unsigned phase;
    TEST_ASSERT_TRUE(MenuRangeEditor_Read(UI_STATE_SET_RGB_TEMP_COLD,&low,&high,&phase));
    TEST_ASSERT_EQUAL(70,low); TEST_ASSERT_EQUAL(71,high);
    TEST_ASSERT_FALSE(MenuRangeEditor_Advance(UI_STATE_SET_RGB_TEMP_COLD));
    TEST_ASSERT_TRUE(MenuRangeEditor_Rotate(UI_STATE_SET_RGB_TEMP_COLD,-10000));
    TEST_ASSERT_TRUE(MenuRangeEditor_Read(UI_STATE_SET_RGB_TEMP_COLD,&low,&high,&phase)); TEST_ASSERT_EQUAL(71,high);
    saved.hardware.ext_led_temp_cold_max=8;
    TEST_ASSERT_FALSE(MenuRangeEditor_Unchanged(UI_STATE_SET_RGB_TEMP_COLD,&saved));
    saved.hardware.ext_led_temp_cold_min=NAN;
    TEST_ASSERT_FALSE(MenuRangeEditor_Begin(UI_STATE_SET_RGB_TEMP_COLD,&saved));
}
