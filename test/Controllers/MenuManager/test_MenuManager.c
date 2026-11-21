#include "Controllers/UIController/Widgets/WidgetTemp.h"
#include "Controllers/UIController/Widgets/WidgetLED.h"
#include "Controllers/UIController/Widgets/WidgetRGB.h"
#include "Hardware/LEDAnimations/LEDAnimationCatalog.h"
#include "Controllers/UIController/Widgets/WidgetSystem.h"
#include <unity.h>
#include "Controllers/MenuManager/IMenuManager.h"
#include "Controllers/MenuManager/MenuManagerConfig.h"
#include "Controllers/MenuManager/MenuNavigator.h"
#include "Controllers/MenuManager/MenuStateMachine.h"
#include "Controllers/MenuManager/MenuPluginManager.h"
#include <string.h>
#include <freertos/semphr.h>
static unsigned deleted_mutex_count;
static SemaphoreHandle_t deleted_mutex;
#undef vSemaphoreDelete
#define vSemaphoreDelete(handle) (deleted_mutex = (handle), ++deleted_mutex_count)
#include "Controllers/MenuManager/MenuManager.c"
#include "Controllers/MenuManager/MenuManagerConsoleCommands.c"

// Note: In a real native test, we would link with FFF mocks for FreeRTOS
// For this verification, we are testing the logic components (Navigator, StateMachine, PluginManager)
// which are the brains of the new MenuManager.

static SystemSettings editFixture;
static esp_err_t edit_fixture_read(IConfigurationManager *self,SystemSettings *out) {
    (void)self;*out=editFixture;return ESP_OK;
}
static esp_err_t edit_fixture_write(IConfigurationManager *self,const char *block,const ConfigurationFieldUpdate *fields,
                                   const void *const *expected,size_t count,bool persist) {
    (void)self;(void)block;(void)fields;(void)expected;(void)count;(void)persist;return ESP_OK;
}
static IConfigurationManager editOwner={.GetSnapshot=edit_fixture_read,.UpdateFieldsIfUnchanged=edit_fixture_write};
void setUp(void) {
    MenuEditSession_Cancel();
    memset(&editFixture,0,sizeof(editFixture));
    RESET_FAKE(mocked_ConfigurationManager_GetInstance);
    mocked_ConfigurationManager_GetInstance_fake.return_val=&editOwner;
    IMenuManager interface = *MenuManager_GetModule();
    memset(&impl, 0, sizeof(impl));
    impl.base = interface;
    impl.mutex = (void *)1;
    impl.config = MenuManager_GetDefaultConfig();
    RESET_FAKE(mocked_esp_timer_get_time);
    RESET_FAKE(mocked_ServiceLocator_GetService);
    RESET_FAKE(mocked_GetEventBus);
    RESET_FAKE(mocked_ServiceLocator_GetEventBus);
    RESET_FAKE(mocked_xSemaphoreGive);
    RESET_FAKE(mocked_xSemaphoreTake);
    RESET_FAKE(mocked_xSemaphoreCreateMutex);
    RESET_FAKE(mocked_xQueueCreate);
    RESET_FAKE(mocked_xTaskCreate);
    RESET_FAKE(mocked_xTaskGetCurrentTaskHandle);
    RESET_FAKE(mocked_xTaskGetTickCount);
    RESET_FAKE(mocked_xQueueReceive);
    RESET_FAKE(mocked_xQueueSend);
    RESET_FAKE(mocked_vQueueDelete);
    RESET_FAKE(mocked_vTaskDelete);
    deleted_mutex_count = 0;
    deleted_mutex = NULL;
    mocked_xSemaphoreTake_fake.return_val = pdTRUE;
    MenuNavigator_Init();
    MenuStateMachine_Init();
    MenuPluginManager_Init();
}

void tearDown(void) {
}

void test_MenuHealthCallbackExistsAndRejectsStoppedOrMissingWorker(void) {
    IMenuManager *menu = MenuManager_GetModule();
    TEST_ASSERT_NOT_NULL(menu->base.CheckHealth);
    TEST_ASSERT_NOT_EQUAL(0, menu->base.CheckHealth(&menu->base));
    impl.controlMutex = (void *)5;
    impl.eventQueue = (void *)3;
    impl.serviceState = SERVICE_STATE_STARTED;
    TEST_ASSERT_NOT_EQUAL(0, menu->base.CheckHealth(&menu->base));
    atomic_store(&impl.worker.task, (void *)4);
    TEST_ASSERT_EQUAL(0, menu->base.CheckHealth(&menu->base));
    char *args[] = {"menu", "test"};
    TEST_ASSERT_EQUAL(0, menu_console_cmd_handler(2, args));
    atomic_store(&impl.worker.stopRequested, true);
    TEST_ASSERT_NOT_EQUAL(0, menu->base.CheckHealth(&menu->base));
    atomic_store(&impl.worker.stopRequested, false);
    impl.serviceState = SERVICE_STATE_STOPPED;
    TEST_ASSERT_NOT_EQUAL(0, menu->base.CheckHealth(&menu->base));
    TEST_ASSERT_NOT_EQUAL(0, menu_console_cmd_handler(2, args));
}

void test_MenuNavigator_TreeTraversal(void) {
    // Root is MONITOR
    TEST_ASSERT_EQUAL(UI_STATE_MONITOR, MenuStateMachine_GetCurrentState());
    
    // Get first child of ROOT
    UIState first = MenuNavigator_GetFirstChild(UI_STATE_MONITOR);
    TEST_ASSERT_EQUAL(UI_STATE_CAT_TEMP, first);
    
    // Navigate siblings of root children
    UIState next = MenuNavigator_GetNextSibling(first, true);
    TEST_ASSERT_EQUAL(UI_STATE_CAT_LIGHT, next);
    
    next = MenuNavigator_GetNextSibling(next, true);
    TEST_ASSERT_EQUAL(UI_STATE_CAT_DEVICE, next);
    
    // Test circularity
    UIState last = UI_STATE_EXIT;
    TEST_ASSERT_EQUAL(UI_STATE_MONITOR, MenuNavigator_GetParent(last));
    TEST_ASSERT_EQUAL(UI_STATE_CAT_TEMP, MenuNavigator_GetNextSibling(last, true));
}

void test_MenuStateMachine_History(void) {
    MenuStateMachine_ForceState(UI_STATE_CAT_TEMP);
    MenuStateMachine_PushHistory();
    
    MenuStateMachine_TransitionTo(UI_STATE_SET_LOW_TEMP);
    TEST_ASSERT_EQUAL(UI_STATE_SET_LOW_TEMP, MenuStateMachine_GetCurrentState());
    
    UIState popped = MenuStateMachine_PopHistory();
    TEST_ASSERT_EQUAL(UI_STATE_CAT_TEMP, popped);
    TEST_ASSERT_EQUAL(UI_STATE_CAT_TEMP, MenuStateMachine_GetCurrentState());
}

void test_MenuStateSnapshotAndBoundedHistory(void) {
    for (unsigned state = 1; state <= MENU_HISTORY_CAPACITY; ++state) {
        MenuStateMachine_ForceState((UIState)state);
        TEST_ASSERT_TRUE(MenuStateMachine_PushHistory());
    }
    TEST_ASSERT_FALSE(MenuStateMachine_PushHistory());
    for (unsigned state = MENU_HISTORY_CAPACITY; state >= 1; --state) {
        TEST_ASSERT_EQUAL(state, MenuStateMachine_PopHistory());
        MenuStateSnapshot snapshot = MenuStateMachine_GetSnapshot();
        TEST_ASSERT_EQUAL(state, snapshot.currentState);
        TEST_ASSERT_EQUAL(state > 1 ? state - 1 : UI_STATE_MONITOR, snapshot.historyParent);
    }
    TEST_ASSERT_EQUAL(UI_STATE_MONITOR, MenuStateMachine_PopHistory());
    MenuStateMachine_ForceState(UI_STATE_CAT_LIGHT);
    MenuStateMachine_PushHistory();
    MenuStateMachine_ForceState(UI_STATE_SET_BUILTIN_IDLE);
    IMenuManager *menu = MenuManager_GetModule();
    mocked_xSemaphoreTake_fake.return_val = pdFALSE;
    unsigned takes = mocked_xSemaphoreTake_fake.call_count;
    TEST_ASSERT_EQUAL(UI_STATE_CAT_LIGHT, menu->GetActiveCategory(menu));
    TEST_ASSERT_EQUAL(UI_STATE_SET_BUILTIN_IDLE, menu->GetCurrentState(menu));
    TEST_ASSERT_FALSE(menu->IsEditing(menu));
    TEST_ASSERT_FALSE(menu->GetConfirmSelection(menu));
    TEST_ASSERT_EQUAL(0, menu->GetLastActivityTime(menu));
    TEST_ASSERT_EQUAL(takes, mocked_xSemaphoreTake_fake.call_count);
    MenuStateMachine_ClearHistory();
    TEST_ASSERT_EQUAL(UI_STATE_MONITOR, MenuStateMachine_GetSnapshot().historyParent);
}

static void dummy_handler(UIState state, int8_t direction, bool buttonPressed) {
    (void)state; (void)direction; (void)buttonPressed;
}

static const MenuPluginRegistration *active_batch;
static void batch_handler(UIState state, int8_t direction, bool button) {
    (void)state; (void)direction; (void)button;
    TEST_ASSERT_EQUAL(MENU_ERR_BUSY, MenuPluginManager_RegisterBatch(active_batch, 2));
    TEST_ASSERT_EQUAL(MENU_ERR_BUSY, MenuPluginManager_UnregisterBatch(active_batch, 2));
    TEST_ASSERT_TRUE(MenuPluginManager_HasPlugin(UI_STATE_SET_LOW_TEMP));
}
void test_MenuPluginBatchValidationBusyAndOwnershipAreAtomic(void) {
    MenuPluginRegistration batch[] = {
        {UI_STATE_SET_LOW_TEMP, "Low", NULL, dummy_handler},
        {UI_STATE_RESTART, "Restart", NULL, batch_handler}
    };
    IMenuManager *menu = MenuManager_GetModule();
    TEST_ASSERT_EQUAL(MENU_OK, menu->RegisterPluginBatch(menu, batch, 2));
    active_batch = batch;
    MenuPluginEvent event = {.state = UI_STATE_RESTART, .action = MENU_PLUGIN_ACTION_BUTTON};
    TEST_ASSERT_TRUE(MenuPluginManager_DispatchEvent(&event));
    batch[1].state = batch[0].state;
    TEST_ASSERT_EQUAL(MENU_ERR_INVALID_STATE, menu->RegisterPluginBatch(menu, batch, 2));
    batch[1].state = UI_STATE_RESTART;
    batch[1].legacy = dummy_handler;
    TEST_ASSERT_EQUAL(MENU_ERR_INVALID_STATE, menu->UnregisterPluginBatch(menu, batch, 2));
    TEST_ASSERT_TRUE(MenuPluginManager_HasPlugin(UI_STATE_SET_LOW_TEMP));
    batch[1].legacy = batch_handler;
    TEST_ASSERT_EQUAL(MENU_OK, menu->UnregisterPluginBatch(menu, batch, 2));
    TEST_ASSERT_FALSE(MenuPluginManager_HasPlugin(UI_STATE_SET_LOW_TEMP));
    TEST_ASSERT_FALSE(MenuPluginManager_HasPlugin(UI_STATE_RESTART));
    char long_label[MENU_LABEL_CAPACITY + 1];
    memset(long_label, 'x', MENU_LABEL_CAPACITY); long_label[MENU_LABEL_CAPACITY] = 0;
    batch[1].label = long_label;
    TEST_ASSERT_EQUAL(MENU_ERR_INVALID_STATE, menu->RegisterPluginBatch(menu, batch, 2));
    TEST_ASSERT_FALSE(MenuPluginManager_HasPlugin(UI_STATE_SET_LOW_TEMP));
}

void test_MenuPluginManager_Registration(void) {
    const char* label = "Test Plugin";
    
    MenuManagerError err = MenuPluginManager_RegisterLegacy(UI_STATE_SET_LOW_TEMP, label, dummy_handler);
    TEST_ASSERT_EQUAL(MENU_OK, err);
    
    TEST_ASSERT_TRUE(MenuPluginManager_HasPlugin(UI_STATE_SET_LOW_TEMP));
    char copied[MENU_LABEL_CAPACITY];
    TEST_ASSERT_TRUE(MenuPluginManager_CopyLabel(UI_STATE_SET_LOW_TEMP, copied, sizeof(copied)));
    TEST_ASSERT_EQUAL_STRING(label, copied);
    
    MenuPluginManager_Unregister(UI_STATE_SET_LOW_TEMP);
    TEST_ASSERT_FALSE(MenuPluginManager_HasPlugin(UI_STATE_SET_LOW_TEMP));
}

void test_MenuStateMachine_ConfirmationRequirement(void) {
    TEST_ASSERT_TRUE(MenuStateMachine_RequiresConfirmation(UI_STATE_RESTART));
    TEST_ASSERT_TRUE(MenuStateMachine_RequiresConfirmation(UI_STATE_FACTORY_RESET));
    TEST_ASSERT_FALSE(MenuStateMachine_RequiresConfirmation(UI_STATE_CAT_TEMP));
}

void test_MenuDoesNotExposeUnimplementedBuiltinHeatingSetting(void) {
    UIState item = MenuNavigator_GetFirstChild(UI_STATE_CAT_LIGHT);
    unsigned visited = 0;
    while (item != UI_STATE_BACK && visited++ < UI_STATE_COUNT) {
        TEST_ASSERT_NOT_EQUAL(UI_STATE_SET_BUILTIN_HEATING, item);
        item = MenuNavigator_GetNextSiblingIn(item, UI_STATE_CAT_LIGHT, false);
    }
    TEST_ASSERT_EQUAL(UI_STATE_BACK, item);
    TEST_ASSERT_EQUAL(UI_STATE_SET_BUILTIN_CONNECTING,
        MenuNavigator_GetNextSibling(UI_STATE_SET_BUILTIN_IDLE, false));
    SystemSettings settings = {0};
    char value[32] = "unchanged", unit[16] = "unchanged", label[64] = "unchanged";
    int percent = -1;
    TEST_ASSERT_FALSE(WidgetLED_GetValue(UI_STATE_SET_BUILTIN_HEATING, &settings,
        value, sizeof(value), unit, sizeof(unit), &percent, Localization_Snapshot()));
    TEST_ASSERT_FALSE(WidgetLED_FormatLabel(UI_STATE_SET_BUILTIN_HEATING, "Heating",
        &settings, label, sizeof(label), Localization_Snapshot()));
    TEST_ASSERT_EQUAL_STRING("unchanged", value);
    TEST_ASSERT_EQUAL_STRING("unchanged", unit);
    TEST_ASSERT_EQUAL_STRING("unchanged", label);
    TEST_ASSERT_EQUAL(-1, percent);
}

void test_MenuBuiltinBrightnessNavigationAndDisplay(void) {
    TEST_ASSERT_EQUAL(UI_STATE_CAT_BUILTIN_LED, MenuNavigator_GetParent(UI_STATE_SET_BUILTIN_BRIGHTNESS));
    TEST_ASSERT_EQUAL(UI_STATE_SET_BUILTIN_BRIGHTNESS, MenuNavigator_GetNextSibling(UI_STATE_SET_BUILTIN_MODE, false));
    SystemSettings settings = {0};
    char value[32], unit[16], label[64];
    int percent = -1;
    TEST_ASSERT_TRUE(WidgetLED_GetValue(UI_STATE_SET_BUILTIN_BRIGHTNESS, &settings, value, sizeof(value), unit, sizeof(unit), &percent, Localization_Snapshot()));
    TEST_ASSERT_EQUAL_STRING("0", value);
    TEST_ASSERT_EQUAL(0, percent);
    settings.hardware.builtin_led_brightness = 255;
    TEST_ASSERT_TRUE(WidgetLED_GetValue(UI_STATE_SET_BUILTIN_BRIGHTNESS, &settings, value, sizeof(value), unit, sizeof(unit), &percent, Localization_Snapshot()));
    TEST_ASSERT_EQUAL_STRING("100", value);
    TEST_ASSERT_EQUAL(100, percent);
    TEST_ASSERT_TRUE(WidgetLED_FormatLabel(UI_STATE_SET_BUILTIN_BRIGHTNESS, "Dahili Parlak", &settings, label, sizeof(label), Localization_Snapshot()));
    TEST_ASSERT_EQUAL_STRING("Dahili Parlak: 100%", label);
}

void test_MenuAnimationLabelsCoverBothCatalogsWithoutNullOrOutOfRangePercent(void) {
    static const char *const expected[2][22]={
      {"Kapalı","Kırmızı kalp atışı","Beyaz yanıp sönme","Kırmızı SOS","Yeşil nefes","Mavi geçiş","Beyaz flaş","Gökkuşağı","Polis","Sabit kırmızı","Sabit yeşil","Sabit mavi","Sabit beyaz","Kırmızı yanıp sönme","Kırmızı nefes","Beyaz çift yanma","Nefes alma","Hızlı nabız","Mum ışığı","Uyarı flaşı","Açılıp kapanma","Renk döngüsü"},
      {"Off","Red heartbeat","White blink","Red SOS","Green breathing","Blue fade","White strobe","Rainbow","Police","Solid red","Solid green","Solid blue","Solid white","Red blink","Red breathing","White double blink","Breathing","Pulse","Candle","Alert flash","Fade in/out","Color cycle"}
    };
    SystemSettings settings={0};size_t count;
    const LEDAnimationCatalogEntry *entries=LEDAnimationCatalog_Entries(&count);
    TEST_ASSERT_EQUAL(22,count);
    for(unsigned language=0;language<2;++language) {
      const LocalizationCatalog *catalog=Localization_Find(language ? "en" : "tr");
      for(size_t index=0;index<count;++index) {
        char builtin[64],external[64],unit[16],label[128];int first=-1,second=-1;
        settings.hardware.builtin_led_idle_anim=entries[index].builtin_id;
        settings.hardware.ext_led_startup_anim=entries[index].external_id;
        TEST_ASSERT_TRUE(WidgetLED_GetValue(UI_STATE_SET_BUILTIN_IDLE,&settings,builtin,sizeof(builtin),unit,sizeof(unit),&first,catalog));
        TEST_ASSERT_TRUE(WidgetLED_GetValue(UI_STATE_SET_LED_ANIM,&settings,external,sizeof(external),unit,sizeof(unit),&second,catalog));
        TEST_ASSERT_EQUAL_STRING(expected[language][index],builtin);
        TEST_ASSERT_EQUAL_STRING(builtin,external);TEST_ASSERT_EQUAL(first,second);
        TEST_ASSERT_INT_WITHIN(50,50,first);
        TEST_ASSERT_TRUE(WidgetLED_FormatLabel(UI_STATE_SET_LED_ANIM,"Anim",&settings,label,sizeof(label),catalog));
        TEST_ASSERT_NOT_NULL(strstr(label,external));
      }
    }
}

void test_MenuLanguageLivesBesideDisplayTimeout(void);
void test_MenuRGBBrightnessAndFanScaleAreAccurate(void);
// Main test runner for native environment
static unsigned action_calls, edit_calls;
static void tracked_legacy_handler(UIState state, int8_t direction, bool pressed) {
    (void)state;
    if (pressed) {
        TEST_ASSERT_EQUAL_PTR(impl.mutex, mocked_xSemaphoreGive_fake.arg0_val);
        ++action_calls;
        if(RGBZoneSelection_Zone(state)>=0)impl.base.SetEditing(&impl.base,false);
    } else if (direction) ++edit_calls;
}
static void click_menu(void) {
    mocked_esp_timer_get_time_fake.return_val += 300000;
    HandleButton();
}
void test_MenuPluginManager_AllDeclaredStatesFit(void) {
    for (unsigned state = 0; state < UI_STATE_COUNT; ++state) {
        TEST_ASSERT_EQUAL(MENU_OK, MenuPluginManager_RegisterLegacy((UIState)state, "Entry", dummy_handler));
    }
    for (unsigned state = 0; state < UI_STATE_COUNT; ++state)
        TEST_ASSERT_TRUE(MenuPluginManager_HasPlugin((UIState)state));
    TEST_ASSERT_EQUAL(MENU_ERR_INVALID_STATE, MenuPluginManager_RegisterLegacy((UIState)-1, "Invalid", dummy_handler));
    TEST_ASSERT_EQUAL(MENU_ERR_INVALID_STATE, MenuPluginManager_RegisterLegacy(UI_STATE_COUNT, "Invalid", dummy_handler));
}
void test_MenuPowerProfileNavigationAndDisplay(void) {
    TEST_ASSERT_EQUAL(UI_STATE_SET_POWER_PROFILE, MenuNavigator_GetFirstChild(UI_STATE_CAT_ENERGY));
    TEST_ASSERT_EQUAL(UI_STATE_CAT_ENERGY, MenuNavigator_GetParent(UI_STATE_SET_POWER_PROFILE));
    const char *labels[] = {"Performans", "Dengeli", "Tasarruf"};
    SystemSettings settings = {0};
    char value[32], unit[8];
    int percent;
    for (unsigned mode = 0; mode < POWER_PROFILE_COUNT; ++mode) {
        settings.system.power_profile = mode;
        TEST_ASSERT_TRUE(WidgetSystem_GetValue(UI_STATE_SET_POWER_PROFILE, &settings,
            value, sizeof(value), unit, sizeof(unit), &percent, Localization_Snapshot()));
        TEST_ASSERT_EQUAL_STRING(labels[mode], value);
    }
    settings.system.power_profile = 255;
    TEST_ASSERT_TRUE(WidgetSystem_GetValue(UI_STATE_SET_POWER_PROFILE, &settings,
        value, sizeof(value), unit, sizeof(unit), &percent, Localization_Snapshot()));
    TEST_ASSERT_EQUAL_STRING("Bilinmiyor", value);
}
void test_MenuLegacyLeafCanEnterEditRotateAndExit(void) {
    action_calls = edit_calls = 0;
    TEST_ASSERT_EQUAL(MENU_OK, MenuPluginManager_RegisterLegacy(UI_STATE_SET_LED_MODE, "LED", tracked_legacy_handler));
    MenuStateMachine_ForceState(UI_STATE_SET_LED_MODE);
    click_menu();
    TEST_ASSERT_TRUE(impl.isEditing);
    TEST_ASSERT_EQUAL(0, action_calls);
    mocked_esp_timer_get_time_fake.return_val += 300000;
    HandleEncoder(1);
    TEST_ASSERT_EQUAL(1, edit_calls);
    click_menu();
    TEST_ASSERT_FALSE(impl.isEditing);
    TEST_ASSERT_EQUAL(1, action_calls);
}
void test_MenuDestructiveActionRequiresConfirmationAndUnlocks(void) {
    action_calls = edit_calls = 0;
    TEST_ASSERT_EQUAL(MENU_OK, MenuPluginManager_RegisterLegacy(UI_STATE_RESTART, "Restart", tracked_legacy_handler));
    MenuStateMachine_ForceState(UI_STATE_RESTART);
    click_menu();
    TEST_ASSERT_TRUE(impl.isEditing);
    TEST_ASSERT_FALSE(impl.confirmSelection);
    TEST_ASSERT_EQUAL(0, action_calls);
    mocked_esp_timer_get_time_fake.return_val += 300000;
    HandleEncoder(1);
    TEST_ASSERT_TRUE(impl.confirmSelection);
    TEST_ASSERT_EQUAL(0, edit_calls);
    click_menu();
    TEST_ASSERT_EQUAL(1, action_calls);
    TEST_ASSERT_FALSE(impl.isEditing);
    click_menu();
    click_menu();
    TEST_ASSERT_EQUAL(1, action_calls);
}

void test_MenuSystemPercentClampsSharedLimits(void) {
    SystemSettings settings = {0}; char value[32], unit[16]; int percent;
    const uint32_t values[] = {0, OLED_SLEEP_ZB_MIN_SEC, OLED_SLEEP_ZB_MAX_SEC, UINT32_MAX};
    for (unsigned i = 0; i < 4; ++i) {
        settings.system.oled_timeout_sec = values[i];
        TEST_ASSERT_TRUE(WidgetSystem_GetValue(UI_STATE_SET_TIMEOUT, &settings, value, sizeof(value), unit, sizeof(unit), &percent, Localization_Snapshot()));
        TEST_ASSERT_EQUAL(i < 2 ? 0 : 100, percent);
    }
}

static unsigned confirmed_calls;
static void confirmed_handler(UIState state, int8_t direction, bool button) {
    (void)state; (void)direction;
    TEST_ASSERT_TRUE(button);
    TEST_ASSERT_EQUAL(mocked_xSemaphoreTake_fake.call_count, mocked_xSemaphoreGive_fake.call_count);
    confirmed_calls++;
}
void test_MenuConfirmApiDispatchesOnlyPendingAcceptedAction(void) {
    impl.controlMutex = (void *)5;
    impl.serviceState = SERVICE_STATE_STARTED;
    IMenuManager *menu = MenuManager_GetModule();
    confirmed_calls = 0;
    TEST_ASSERT_EQUAL(MENU_OK, menu->RegisterPlugin(menu, "Restart", UI_STATE_RESTART, confirmed_handler));
    MenuStateMachine_ForceState(UI_STATE_RESTART);
    menu->ConfirmAction(menu, true);
    TEST_ASSERT_EQUAL(0, confirmed_calls);
    TEST_ASSERT_FALSE(impl.isEditing);
    click_menu();
    menu->ConfirmAction(menu, false);
    TEST_ASSERT_EQUAL(0, confirmed_calls);
    TEST_ASSERT_FALSE(impl.isEditing);
    MenuStateMachine_ForceState(UI_STATE_RESTART);
    click_menu();
    menu->ConfirmAction(menu, true);
    TEST_ASSERT_EQUAL(1, confirmed_calls);
    TEST_ASSERT_FALSE(impl.isEditing);
    menu->ConfirmAction(menu, true);
    TEST_ASSERT_EQUAL(1, confirmed_calls);
    MenuStateMachine_ForceState(UI_STATE_SET_LOW_TEMP);
    menu->ConfirmAction(menu, true);
    TEST_ASSERT_FALSE(impl.isEditing);
}
void test_MenuConfirmStateLockFailureReleasesLifecycleAdmission(void) {
    IMenuManager *menu = MenuManager_GetModule();
    impl.controlMutex = (void *)5;
    impl.serviceState = SERVICE_STATE_STARTED;
    impl.isEditing = true;
    MenuStateMachine_ForceState(UI_STATE_RESTART);
    BaseType_t takes[] = {pdTRUE, pdFALSE};
    SET_RETURN_SEQ(mocked_xSemaphoreTake, takes, 2);
    menu->ConfirmAction(menu, true);
    TEST_ASSERT_FALSE(atomic_load(&impl.publicConfirmationActive));
    TEST_ASSERT_TRUE(impl.isEditing);
    TEST_ASSERT_EQUAL(1, mocked_xSemaphoreGive_fake.call_count);
    TEST_ASSERT_EQUAL_PTR(impl.controlMutex, mocked_xSemaphoreGive_fake.arg0_val);
}
static unsigned stop_during_confirmation_calls;
static void stop_during_confirmation(UIState state, int8_t direction, bool button) {
    (void)state; (void)direction; (void)button;
    IMenuManager *menu = MenuManager_GetModule();
    stop_during_confirmation_calls++;
    TEST_ASSERT_NOT_EQUAL(0, menu->base.Stop(&menu->base));
    TEST_ASSERT_EQUAL(SERVICE_STATE_ERROR, menu->base.GetState(&menu->base));
    TEST_ASSERT_EQUAL(0, mocked_vTaskDelete_fake.call_count);
}
void test_MenuStopRejectsOutstandingPublicConfirmationUntilReturn(void) {
    IMenuManager *menu = MenuManager_GetModule();
    impl.controlMutex = (void *)5;
    impl.worker.gate = (void *)6;
    impl.eventQueue = (void *)3;
    impl.serviceState = SERVICE_STATE_STARTED;
    stop_during_confirmation_calls = 0;
    TEST_ASSERT_EQUAL(MENU_OK, menu->RegisterPlugin(menu, "Restart", UI_STATE_RESTART, stop_during_confirmation));
    MenuStateMachine_ForceState(UI_STATE_RESTART);
    click_menu();
    menu->ConfirmAction(menu, true);
    TEST_ASSERT_EQUAL(1, stop_during_confirmation_calls);
    TEST_ASSERT_EQUAL(0, menu->base.Stop(&menu->base));
    TEST_ASSERT_EQUAL(SERVICE_STATE_STOPPED, menu->base.GetState(&menu->base));
    impl.isEditing = true;
    menu->ConfirmAction(menu, true);
    TEST_ASSERT_EQUAL(1, stop_during_confirmation_calls);
    TEST_ASSERT_TRUE(impl.isEditing);
}
void test_MenuConfirmApiLockFailureKeepsPendingAction(void) {
    impl.controlMutex = (void *)5;
    impl.serviceState = SERVICE_STATE_STARTED;
    IMenuManager *menu = MenuManager_GetModule();
    confirmed_calls = 0;
    TEST_ASSERT_EQUAL(MENU_OK, menu->RegisterPlugin(menu, "Restart", UI_STATE_RESTART, confirmed_handler));
    MenuStateMachine_ForceState(UI_STATE_RESTART);
    click_menu();
    unsigned gives = mocked_xSemaphoreGive_fake.call_count;
    mocked_xSemaphoreTake_fake.return_val = pdFALSE;
    menu->ConfirmAction(menu, true);
    TEST_ASSERT_TRUE(impl.isEditing);
    TEST_ASSERT_FALSE(impl.confirmSelection);
    TEST_ASSERT_EQUAL(0, confirmed_calls);
    TEST_ASSERT_EQUAL(gives, mocked_xSemaphoreGive_fake.call_count);
}
void test_MenuInvalidStatesDoNotChangeStateOrReadLabels(void) {
    const UIState invalid[] = {(UIState)-1, (UIState)INT32_MIN, UI_STATE_COUNT, (UIState)INT32_MAX};
    for (unsigned i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        MenuStateMachine_ForceState(UI_STATE_CAT_TEMP);
        TEST_ASSERT_FALSE(MenuStateMachine_TransitionTo(invalid[i]));
        MenuStateMachine_ForceState(invalid[i]);
        TEST_ASSERT_EQUAL(UI_STATE_CAT_TEMP, MenuStateMachine_GetCurrentState());
        TEST_ASSERT_EQUAL_STRING("Unknown", MenuManager_GetStateLabel(invalid[i]));
        TEST_ASSERT_EQUAL(UI_STATE_COUNT, MenuNavigator_GetParent(invalid[i]));
        TEST_ASSERT_FALSE(MenuNavigator_IsLeaf(invalid[i]));
        TEST_ASSERT_EQUAL(MENU_NODE_LEAF, MenuNavigator_GetNodeType(invalid[i]));
    }
}

void test_MenuInitializationAllocationFailureRollsBackAndRetries(void) {
    IMenuManager *menu = MenuManager_GetModule();
    impl.mutex = NULL;
    TEST_ASSERT_NOT_EQUAL(0, menu->InitializeWithConfig(menu, NULL, NULL));
    TEST_ASSERT_EQUAL(SERVICE_STATE_UNINITIALIZED, impl.serviceState);
    TEST_ASSERT_EQUAL(0, mocked_xQueueCreate_fake.call_count);
    TEST_ASSERT_NULL(impl.mutex);
    mocked_xSemaphoreCreateMutex_fake.return_val = (void *)2;
    TEST_ASSERT_NOT_EQUAL(0, menu->InitializeWithConfig(menu, NULL, NULL));
    TEST_ASSERT_EQUAL(1, deleted_mutex_count);
    TEST_ASSERT_EQUAL_PTR((void *)2, deleted_mutex);
    TEST_ASSERT_NULL(impl.mutex);
    TEST_ASSERT_NULL(impl.eventQueue);
    TEST_ASSERT_EQUAL(SERVICE_STATE_UNINITIALIZED, impl.serviceState);
    mocked_xQueueCreate_fake.return_val = (void *)3;
    TEST_ASSERT_EQUAL(0, menu->InitializeWithConfig(menu, NULL, NULL));
    TEST_ASSERT_EQUAL(SERVICE_STATE_INITIALIZED, impl.serviceState);
    TEST_ASSERT_EQUAL_PTR((void *)2, impl.mutex);
    TEST_ASSERT_EQUAL_PTR((void *)3, impl.eventQueue);
    unsigned allocations = mocked_xQueueCreate_fake.call_count;
    TEST_ASSERT_EQUAL(0, menu->InitializeWithConfig(menu, NULL, NULL));
    TEST_ASSERT_EQUAL(allocations, mocked_xQueueCreate_fake.call_count);
}

void test_MenuInitializationRejectsInvalidResourcesBeforeAllocation(void) {
    IMenuManager *menu = MenuManager_GetModule();
    MenuManagerConfig config = MenuManager_GetDefaultConfig();
    TEST_ASSERT_NOT_EQUAL(0, menu->InitializeWithConfig(NULL, &config, NULL));
    config.eventQueueSize = 0;
    TEST_ASSERT_NOT_EQUAL(0, menu->InitializeWithConfig(menu, &config, NULL));
    config = MenuManager_GetDefaultConfig();
    config.taskStackSize = 0;
    TEST_ASSERT_NOT_EQUAL(0, menu->InitializeWithConfig(menu, &config, NULL));
    config = MenuManager_GetDefaultConfig();
    config.taskPriority = configMAX_PRIORITIES;
    TEST_ASSERT_NOT_EQUAL(0, menu->InitializeWithConfig(menu, &config, NULL));
    TEST_ASSERT_EQUAL(0, mocked_xSemaphoreCreateMutex_fake.call_count);
    TEST_ASSERT_EQUAL(SERVICE_STATE_UNINITIALIZED, impl.serviceState);
}

static BaseType_t create_menu_task(TaskFunction_t fn, const char *name, uint32_t stack,
                                 void *arg, uint32_t priority, TaskHandle_t *task) {
    (void)fn; (void)name; (void)stack; (void)arg; (void)priority;
    *task = (void *)4;
    return pdPASS;
}
void test_MenuTaskCreationFailureDoesNotReportStartedAndCanRetry(void) {
    IMenuManager *menu = MenuManager_GetModule();
    impl.serviceState = SERVICE_STATE_INITIALIZED;
    impl.eventQueue = (void *)3;
    impl.controlMutex = (void *)5;
    impl.worker.gate = (void *)6;
    TEST_ASSERT_NOT_EQUAL(0, menu->base.Start(NULL));
    TEST_ASSERT_NOT_EQUAL(0, menu->base.Start(&menu->base));
    TEST_ASSERT_EQUAL(SERVICE_STATE_INITIALIZED, menu->base.GetState(&menu->base));
    TEST_ASSERT_NULL(impl.worker.task);
    mocked_xTaskCreate_fake.custom_fake = create_menu_task;
    TEST_ASSERT_EQUAL(0, menu->base.Start(&menu->base));
    TEST_ASSERT_EQUAL(SERVICE_STATE_STARTED, menu->base.GetState(&menu->base));
    TEST_ASSERT_EQUAL_PTR((void *)4, impl.worker.task);
    TEST_ASSERT_NOT_EQUAL(0, menu->base.Start(&menu->base));
    TEST_ASSERT_EQUAL(2, mocked_xTaskCreate_fake.call_count);
}

static BaseType_t acknowledge_menu_stop(QueueHandle_t queue, void *output, TickType_t ticks) {
    (void)ticks;
    if (queue != impl.worker.stopped) return pdFALSE;
    *(int32_t *)output = 0;
    return pdTRUE;
}
void test_MenuStopTimeoutPreservesOwnershipRejectsInputAndRetries(void) {
    IMenuManager *menu = MenuManager_GetModule();
    impl.serviceState = SERVICE_STATE_STARTED;
    impl.controlMutex = (void *)5;
    impl.eventQueue = (void *)3;
    impl.worker.gate = (void *)6;
    impl.worker.stopped = (void *)7;
    impl.worker.task = (void *)4;
    mocked_xTaskGetCurrentTaskHandle_fake.return_val = (void *)8;
    TEST_ASSERT_NOT_EQUAL(0, menu->base.Stop(&menu->base));
    TEST_ASSERT_EQUAL(SERVICE_STATE_ERROR, impl.serviceState);
    TEST_ASSERT_EQUAL_PTR((void *)4, impl.worker.task);
    TEST_ASSERT_EQUAL(0, mocked_vTaskDelete_fake.call_count);
    TEST_ASSERT_NOT_EQUAL(0, menu->base.Start(&menu->base));
    menu->ProcessButton(menu);
    TEST_ASSERT_EQUAL(0, mocked_xQueueSend_fake.call_count);
    mocked_xQueueReceive_fake.custom_fake = acknowledge_menu_stop;
    TEST_ASSERT_EQUAL(0, menu->base.Stop(&menu->base));
    TEST_ASSERT_EQUAL(SERVICE_STATE_STOPPED, impl.serviceState);
    TEST_ASSERT_NULL(impl.worker.task);
    TEST_ASSERT_EQUAL_PTR((void *)4, mocked_vTaskDelete_fake.arg0_val);
    TEST_ASSERT_EQUAL_PTR((void *)3, impl.eventQueue);
    mocked_xTaskCreate_fake.custom_fake = create_menu_task;
    TEST_ASSERT_EQUAL(0, menu->base.Start(&menu->base));
    TEST_ASSERT_EQUAL(SERVICE_STATE_STARTED, impl.serviceState);
}
void test_MenuStopRejectsWorkerSelfStopAndControlLockFailure(void) {
    IMenuManager *menu = MenuManager_GetModule();
    impl.serviceState = SERVICE_STATE_STARTED;
    impl.controlMutex = (void *)5;
    impl.worker.task = (void *)4;
    mocked_xTaskGetCurrentTaskHandle_fake.return_val = (void *)4;
    TEST_ASSERT_NOT_EQUAL(0, menu->base.Stop(&menu->base));
    TEST_ASSERT_EQUAL(0, mocked_xSemaphoreTake_fake.call_count);
    mocked_xTaskGetCurrentTaskHandle_fake.return_val = (void *)8;
    mocked_xSemaphoreTake_fake.return_val = pdFALSE;
    TEST_ASSERT_NOT_EQUAL(0, menu->base.Stop(&menu->base));
    TEST_ASSERT_EQUAL(SERVICE_STATE_STARTED, impl.serviceState);
    TEST_ASSERT_EQUAL(0, mocked_xSemaphoreGive_fake.call_count);
    TEST_ASSERT_EQUAL(0, mocked_vTaskDelete_fake.call_count);
}
void test_MenuControlAllocationFailureReleasesPrivateResources(void) {
    IMenuManager *menu = MenuManager_GetModule();
    impl.mutex = NULL;
    SemaphoreHandle_t allocations[] = {(void *)2, NULL};
    SET_RETURN_SEQ(mocked_xSemaphoreCreateMutex, allocations, 2);
    mocked_xQueueCreate_fake.return_val = (void *)3;
    TEST_ASSERT_NOT_EQUAL(0, menu->InitializeWithConfig(menu, NULL, NULL));
    TEST_ASSERT_EQUAL(SERVICE_STATE_UNINITIALIZED, impl.serviceState);
    TEST_ASSERT_NULL(impl.eventQueue);
    TEST_ASSERT_NULL(impl.mutex);
    TEST_ASSERT_EQUAL_PTR((void *)3, mocked_vQueueDelete_fake.arg0_val);
    TEST_ASSERT_EQUAL_PTR((void *)2, deleted_mutex);
    TEST_ASSERT_EQUAL(1, deleted_mutex_count);
}

void test_MenuFailedStateLockDoesNotMutateOrReleaseUnownedMutex(void) {
    IMenuManager *menu = MenuManager_GetModule();
    MenuStateMachine_ForceState(UI_STATE_CAT_TEMP);
    MenuStateMachine_PushHistory();
    MenuStateMachine_ForceState(UI_STATE_SET_LOW_TEMP);
    impl.isEditing = false;
    mocked_xSemaphoreTake_fake.return_val = pdFALSE;
    mocked_esp_timer_get_time_fake.return_val = 1000000;
    menu->SetEditing(menu, true);
    menu->GoBack(menu);
    HandleButton();
    HandleEncoder(1);
    TEST_ASSERT_FALSE(impl.isEditing);
    TEST_ASSERT_EQUAL(UI_STATE_SET_LOW_TEMP, MenuStateMachine_GetCurrentState());
    TEST_ASSERT_EQUAL(UI_STATE_CAT_TEMP, MenuStateMachine_PeekHistory());
    TEST_ASSERT_EQUAL(0, impl.lastButtonPressTime);
    TEST_ASSERT_EQUAL(0, impl.lastEncoderTimeMs);
    TEST_ASSERT_EQUAL(0, mocked_xSemaphoreGive_fake.call_count);
}

static unsigned registry_calls;
static void registry_reentry(UIState state, int8_t direction, bool pressed) {
    (void)direction; (void)pressed;
    ++registry_calls;
    TEST_ASSERT_EQUAL(MENU_ERR_BUSY, MenuPluginManager_Unregister(state));
    TEST_ASSERT_EQUAL(MENU_ERR_BUSY, MenuPluginManager_SetEnabled(state, false));
    TEST_ASSERT_EQUAL(MENU_ERR_BUSY, MenuPluginManager_RegisterLegacy(state, "Replace", dummy_handler));
    MenuPluginEvent nested = {.state = state, .action = MENU_PLUGIN_ACTION_BUTTON};
    TEST_ASSERT_FALSE(MenuPluginManager_DispatchEvent(&nested));
    char label[MENU_LABEL_CAPACITY];
    TEST_ASSERT_TRUE(MenuPluginManager_CopyLabel(state, label, sizeof(label)));
    TEST_ASSERT_EQUAL_STRING("Original", label);
}
void test_MenuPluginActiveCallbackRejectsMutationAndRecursiveDispatch(void) {
    registry_calls = 0;
    TEST_ASSERT_EQUAL(MENU_OK, MenuPluginManager_RegisterLegacy(UI_STATE_RESTART, "Original", registry_reentry));
    MenuPluginEvent event = {.state = UI_STATE_RESTART, .action = MENU_PLUGIN_ACTION_BUTTON};
    TEST_ASSERT_TRUE(MenuPluginManager_DispatchEvent(&event));
    TEST_ASSERT_EQUAL(1, registry_calls);
    TEST_ASSERT_EQUAL(MENU_OK, MenuPluginManager_Unregister(UI_STATE_RESTART));
    TEST_ASSERT_FALSE(MenuPluginManager_DispatchEvent(&event));
    TEST_ASSERT_FALSE(event.handled);
}
void test_MenuLabelCopiesRemainOwnedAndFailuresPreserveOutput(void) {
    IMenuManager *menu = MenuManager_GetModule();
    char source[] = "First";
    char copy[MENU_LABEL_CAPACITY] = "unchanged";
    TEST_ASSERT_EQUAL(MENU_OK, menu->RegisterPlugin(menu, source, UI_STATE_RESTART, dummy_handler));
    source[0] = 'X';
    TEST_ASSERT_TRUE(menu->CopyLabel(menu, UI_STATE_RESTART, copy, sizeof(copy)));
    TEST_ASSERT_EQUAL_STRING("First", copy);
    TEST_ASSERT_EQUAL(MENU_OK, menu->RegisterPlugin(menu, "Second", UI_STATE_RESTART, dummy_handler));
    TEST_ASSERT_EQUAL_STRING("First", copy);
    TEST_ASSERT_FALSE(menu->CopyLabel(menu, UI_STATE_RESTART, copy, 2));
    TEST_ASSERT_EQUAL_STRING("First", copy);
    char oversized[MENU_LABEL_CAPACITY + 1];
    memset(oversized, 'A', sizeof(oversized));
    oversized[sizeof(oversized) - 1] = 0;
    TEST_ASSERT_EQUAL(MENU_ERR_INVALID_STATE, menu->RegisterPlugin(menu, oversized, UI_STATE_RESTART, dummy_handler));
    TEST_ASSERT_TRUE(menu->CopyLabel(menu, UI_STATE_RESTART, copy, sizeof(copy)));
    TEST_ASSERT_EQUAL_STRING("Second", copy);
    TEST_ASSERT_EQUAL(MENU_OK, menu->UnregisterPlugin(menu, UI_STATE_RESTART));
    TEST_ASSERT_TRUE(menu->CopyLabel(menu, UI_STATE_RESTART, copy, sizeof(copy)));
    TEST_ASSERT_EQUAL_STRING(MenuManager_GetStateLabel(UI_STATE_RESTART), copy);
    TEST_ASSERT_FALSE(menu->CopyLabel(menu, (UIState)-1, copy, sizeof(copy)));
    TEST_ASSERT_FALSE(menu->CopyLabel(NULL, UI_STATE_RESTART, copy, sizeof(copy)));
    TEST_ASSERT_FALSE(menu->CopyLabel(menu, UI_STATE_RESTART, NULL, sizeof(copy)));
}
static void registry_modern(const MenuPluginEvent *event) {
    TEST_ASSERT_EQUAL(MENU_PLUGIN_ACTION_ENCODER, event->action);
    ++registry_calls;
}
void test_MenuPluginModernDeliveryAndDisabledState(void) {
    registry_calls = 0;
    TEST_ASSERT_EQUAL(MENU_OK, MenuPluginManager_Register(UI_STATE_SET_LOW_TEMP, "Modern", registry_modern));
    MenuPluginEvent event = {.state = UI_STATE_SET_LOW_TEMP, .action = MENU_PLUGIN_ACTION_ENCODER, .direction = 1};
    TEST_ASSERT_TRUE(MenuPluginManager_DispatchEvent(&event));
    TEST_ASSERT_EQUAL(1, registry_calls);
    TEST_ASSERT_EQUAL(MENU_OK, MenuPluginManager_SetEnabled(event.state, false));
    TEST_ASSERT_FALSE(MenuPluginManager_HasPlugin(event.state));
    TEST_ASSERT_FALSE(MenuPluginManager_DispatchEvent(&event));
    TEST_ASSERT_FALSE(event.handled);
    TEST_ASSERT_EQUAL(MENU_ERR_INVALID_STATE, MenuPluginManager_Unregister((UIState)-1));
    TEST_ASSERT_EQUAL(MENU_ERR_INVALID_STATE, MenuPluginManager_SetEnabled(UI_STATE_COUNT, false));
}


static void PrepareMenuAdmission(void) {
    impl.controlMutex = (void *)5;
    impl.eventQueue = (void *)3;
    impl.serviceState = SERVICE_STATE_STARTED;
    mocked_xSemaphoreTake_fake.return_val = pdTRUE;
    mocked_xQueueSend_fake.return_val = pdPASS;
}
void test_MenuPostEventRejectsInvalidInputsBeforeQueue(void) {
    PrepareMenuAdmission(); IMenuManager *menu = MenuManager_GetModule();
    MenuEvent event = {.type = MENU_EVENT_NAVIGATE_TO, .data.targetState = UI_STATE_COUNT};
    TEST_ASSERT_EQUAL(MENU_ERR_NULL_PTR,menu->PostEvent(NULL,&event));
    TEST_ASSERT_EQUAL(MENU_ERR_NULL_PTR,menu->PostEvent(menu,NULL));
    TEST_ASSERT_EQUAL(MENU_ERR_INVALID_STATE,menu->PostEvent(menu,&event));
    event.type = MENU_EVENT_TIMEOUT;
    TEST_ASSERT_EQUAL(MENU_ERR_INVALID_STATE,menu->PostEvent(menu,&event));
    event.type = MENU_EVENT_ENCODER_ROTATED; event.data.encoderDirection = 0;
    TEST_ASSERT_EQUAL(MENU_ERR_INVALID_STATE,menu->PostEvent(menu,&event));
    TEST_ASSERT_EQUAL(0,mocked_xQueueSend_fake.call_count);
}
void test_MenuPostEventReportsContentionClosureAndQueueFull(void) {
    PrepareMenuAdmission(); IMenuManager *menu = MenuManager_GetModule();
    MenuEvent event = {.type = MENU_EVENT_BUTTON_PRESSED};
    mocked_xSemaphoreTake_fake.return_val = pdFALSE;
    TEST_ASSERT_EQUAL(MENU_ERR_BUSY,menu->PostEvent(menu,&event));
    mocked_xSemaphoreTake_fake.return_val = pdTRUE; impl.serviceState = SERVICE_STATE_STOPPED;
    TEST_ASSERT_EQUAL(MENU_ERR_INVALID_STATE,menu->PostEvent(menu,&event));
    TEST_ASSERT_EQUAL(0,mocked_xQueueSend_fake.call_count);
    impl.serviceState = SERVICE_STATE_STARTED; mocked_xQueueSend_fake.return_val = pdFALSE;
    TEST_ASSERT_EQUAL(MENU_ERR_QUEUE_FULL,menu->PostEvent(menu,&event));
    TEST_ASSERT_EQUAL(2,mocked_xSemaphoreGive_fake.call_count);
    mocked_xQueueSend_fake.return_val = pdPASS;
    TEST_ASSERT_EQUAL(MENU_OK,menu->PostEvent(menu,&event));
}
void test_MenuConsoleMalformedNumbersNeverQueueOrPublish(void) {
    PrepareMenuAdmission();
    const char *bad[] = {"oops","1x","-1","999999999999999999999999","+"," 1"};
    for (unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);++i) {
        char *args[] = {"menu","nav",(char *)bad[i]};
        TEST_ASSERT_NOT_EQUAL(0,menu_console_cmd_handler(3,args));
    }
    const char *directions[] = {"0","128","-129","3x","999999999999999999999999"};
    for (unsigned i=0;i<sizeof(directions)/sizeof(directions[0]);++i) {
        char *args[] = {"menu","enc",(char *)directions[i]};
        TEST_ASSERT_NOT_EQUAL(0,menu_console_cmd_handler(3,args));
    }
    TEST_ASSERT_EQUAL(0,mocked_xQueueSend_fake.call_count);
    TEST_ASSERT_EQUAL(0,mocked_ServiceLocator_GetEventBus_fake.call_count);
}
void test_MenuConsoleRejectsExtraNullAndMissingArguments(void) {
    PrepareMenuAdmission();
    char *extra[] = {"menu","reset","extra"}, *missing[] = {"menu","nav"};
    char *null_value[] = {"menu","nav",NULL}, *click_extra[] = {"menu","click","extra"};
    TEST_ASSERT_NOT_EQUAL(0,menu_console_cmd_handler(2,NULL));
    TEST_ASSERT_NOT_EQUAL(0,menu_console_cmd_handler(3,extra));
    TEST_ASSERT_NOT_EQUAL(0,menu_console_cmd_handler(2,missing));
    TEST_ASSERT_NOT_EQUAL(0,menu_console_cmd_handler(3,null_value));
    TEST_ASSERT_NOT_EQUAL(0,menu_console_cmd_handler(3,click_extra));
    TEST_ASSERT_EQUAL(0,mocked_xQueueSend_fake.call_count);
}
void test_MenuConsoleNavigationAcknowledgesAdmissionWithoutApplyingState(void) {
    PrepareMenuAdmission(); char *args[] = {"menu","nav","7"};
    TEST_ASSERT_EQUAL(0,menu_console_cmd_handler(3,args));
    TEST_ASSERT_EQUAL(1,mocked_xQueueSend_fake.call_count);
    TEST_ASSERT_EQUAL(UI_STATE_MONITOR,MenuStateMachine_GetCurrentState());
    mocked_xQueueSend_fake.return_val = pdFALSE;
    TEST_ASSERT_NOT_EQUAL(0,menu_console_cmd_handler(3,args));
}
static unsigned menu_publish_calls;
static int32_t menu_publish_result;
static BusEvent menu_published_event;
static RotaryEncoderRotationEvent menu_published_rotation;
static int32_t PublishMenuFixture(BusEvent event) {
    ++menu_publish_calls; menu_published_event=event;
    if (event.dataSize == sizeof(menu_published_rotation)) memcpy(&menu_published_rotation,event.data,sizeof(menu_published_rotation));
    return menu_publish_result;
}
void test_MenuConsoleInputRejectsBusFailureAndPreservesTypedDirection(void) {
    PrepareMenuAdmission(); IEventBus bus = {.Publish=PublishMenuFixture};
    mocked_ServiceLocator_GetEventBus_fake.return_val=&bus;
    menu_publish_calls=0; menu_publish_result=-1;
    char *args[] = {"menu","enc","-128"};
    TEST_ASSERT_NOT_EQUAL(0,menu_console_cmd_handler(3,args));
    TEST_ASSERT_EQUAL(-128,menu_published_rotation.steps);
    TEST_ASSERT_EQUAL(ROTATION_CCW,menu_published_rotation.direction);
    TEST_ASSERT_EQUAL(1,menu_published_rotation.velocity);
    menu_publish_result=0;
    TEST_ASSERT_EQUAL(0,menu_console_cmd_handler(3,args));
    TEST_ASSERT_EQUAL(0,mocked_xQueueSend_fake.call_count);
    bus.Publish=NULL;
    TEST_ASSERT_NOT_EQUAL(0,menu_console_cmd_handler(3,args));
}
void test_MenuConfigurationRejectsUnsafeResourceBoundsBeforeAllocation(void) {
    IMenuManager *menu = MenuManager_GetModule();
    MenuManagerConfig config = MenuManager_GetDefaultConfig();
    config.taskStackSize = MENU_TASK_STACK_MIN_BYTES - 1;
    TEST_ASSERT_NOT_EQUAL(0,menu->InitializeWithConfig(menu,&config,NULL));
    config.taskStackSize = MENU_TASK_STACK_MAX_BYTES + 1;
    TEST_ASSERT_NOT_EQUAL(0,menu->InitializeWithConfig(menu,&config,NULL));
    config = MenuManager_GetDefaultConfig(); config.eventQueueSize = MENU_QUEUE_CAPACITY_MAX + 1;
    TEST_ASSERT_NOT_EQUAL(0,menu->InitializeWithConfig(menu,&config,NULL));
    TEST_ASSERT_EQUAL(0,mocked_xSemaphoreCreateMutex_fake.call_count);
    TEST_ASSERT_EQUAL(0,mocked_xQueueCreate_fake.call_count);
    config.eventQueueSize = MENU_QUEUE_CAPACITY_MAX; config.taskStackSize = MENU_TASK_STACK_MAX_BYTES;
    mocked_xSemaphoreCreateMutex_fake.return_val=(void *)2;
    mocked_xQueueCreate_fake.return_val=(void *)3;
    TEST_ASSERT_EQUAL(0,menu->InitializeWithConfig(menu,&config,NULL));
    TEST_ASSERT_EQUAL(MENU_QUEUE_CAPACITY_MAX,impl.config.eventQueueSize);
    TEST_ASSERT_EQUAL(MENU_TASK_STACK_MAX_BYTES,impl.config.taskStackSize);
}

void test_MenuActivityFullUptimePreservesLegacyViewAndCannotRegress(void) {
    IMenuManager *menu = MenuManager_GetModule();
    uint64_t now = (uint64_t)UINT32_MAX + 4321u;
    mocked_esp_timer_get_time_fake.return_val = (int64_t)(now * 1000u);
    menu->UpdateActivity(menu);
    TEST_ASSERT_TRUE(menu->GetLastActivityTimeMs(menu) == now);
    TEST_ASSERT_EQUAL((uint32_t)now, menu->GetLastActivityTime(menu));
    mocked_esp_timer_get_time_fake.return_val = (int64_t)((now - 1u) * 1000u);
    menu->UpdateActivity(menu);
    TEST_ASSERT_TRUE(menu->GetLastActivityTimeMs(menu) == now);
}
void test_MenuEncoderCooldownDoesNotBlockAfter32BitClockWrap(void) {
    uint64_t now = (uint64_t)UINT32_MAX + 10000u;
    impl.lastButtonPressTime = UINT32_MAX - 1000u;
    impl.lastEncoderTimeMs = UINT32_MAX - 500u;
    impl.lastEncoderDirection = 1;
    mocked_esp_timer_get_time_fake.return_val = (int64_t)(now * 1000u);
    HandleEncoder(1);
    TEST_ASSERT_TRUE(impl.lastEncoderTimeMs == now);
    TEST_ASSERT_TRUE(GetLastActivityTimeMs(&impl.base) == now);
}

void test_MenuWorkerWakeClearsHistoryAndPairingShortcutDoesNotResetNetwork(void);
void test_MenuFreeXYCategoryRoutesAllAxesAndFormatsExactCoordinates(void);
void test_MenuConsoleMachineStateLanguageAndQueuedAdmission(void);
static SystemSettings visibilitySettings;
static esp_err_t visibilityResult;
static bool visibilityConnected;
static esp_err_t visibility_snapshot(IConfigurationManager *self,SystemSettings *output) {
    (void)self;
    if(visibilityResult==ESP_OK) *output=visibilitySettings;
    return visibilityResult;
}
static int32_t visibility_connection(IZigbee *self) {(void)self;return visibilityConnected;}
static IConfigurationManager visibilityOwner={.GetSnapshot=visibility_snapshot};
static IZigbee visibilityRadio={.IsConnected=visibility_connection};
static IService *visibility_service(const char *name) {
    if(!strcmp(name,SVC_NAME_CONFIGURATION_MANAGER)) return (IService *)&visibilityOwner;
    if(!strcmp(name,SVC_NAME_ZIGBEE)) return (IService *)&visibilityRadio;
    return NULL;
}
void test_MenuVisibilityModesSkipInactiveSettingsAndRetainRecoveryControls(void) {
    SystemSettings settings={0};
    for(unsigned manual=0;manual<2;++manual) for(unsigned strategy=0;strategy<2;++strategy) {
        settings.control.fan_manual_mode=manual;settings.control.fan_strategy=strategy;
        MenuVisibilityContext context=MenuNavigator_Context(&settings,false);
        TEST_ASSERT_FALSE(MenuNavigator_IsVisible(UI_STATE_SET_HIGH_TEMP,&context));
        TEST_ASSERT_EQUAL(!manual && !strategy,MenuNavigator_IsVisible(UI_STATE_SET_LOW_TEMP,&context));
        TEST_ASSERT_FALSE(MenuNavigator_IsVisible(UI_STATE_FAN_DELTA_ON,&context));
        TEST_ASSERT_EQUAL(!manual && strategy,MenuNavigator_IsVisible(UI_STATE_FAN_DELTA_OFF,&context));
        TEST_ASSERT_TRUE(MenuNavigator_IsVisible(UI_STATE_FAN_MODE,&context));
        TEST_ASSERT_TRUE(MenuNavigator_IsVisible(UI_STATE_CALIBRATE_OBJECT,&context));
        TEST_ASSERT_FALSE(MenuNavigator_IsVisible(UI_STATE_OBJECT_SOURCE,&context));
        TEST_ASSERT_FALSE(MenuNavigator_IsVisible(UI_STATE_AMBIENT_SOURCE,&context));
        TEST_ASSERT_FALSE(MenuNavigator_IsVisible(UI_STATE_CAT_ZIGBEE_REP,&context));
        TEST_ASSERT_TRUE(MenuNavigator_IsVisible(UI_STATE_SET_TELEMETRY,&context));
        TEST_ASSERT_TRUE(MenuNavigator_IsVisible(UI_STATE_ZIGBEE_PAIR,&context));
        TEST_ASSERT_TRUE(MenuNavigator_IsVisible(UI_STATE_SET_ZIGBEE_TX_PROFILE,&context));
        UIState item=MenuNavigator_FirstVisible(UI_STATE_CAT_TEMP,&context);
        unsigned visits=0;
        while(item!=UI_STATE_COUNT) {
            TEST_ASSERT_TRUE(MenuNavigator_IsVisible(item,&context));TEST_ASSERT_LESS_THAN(UI_STATE_COUNT,++visits);
            UIState next=MenuNavigator_NextVisible(item,UI_STATE_CAT_TEMP,false,&context);
            if(next==item) break;item=next;
        }
        TEST_ASSERT_EQUAL(UI_STATE_BACK,item);
        TEST_ASSERT_EQUAL(MenuNavigator_FirstVisible(UI_STATE_CAT_TEMP,&context),MenuNavigator_NextVisible(item,UI_STATE_CAT_TEMP,true,&context));
        TEST_ASSERT_EQUAL(MenuNavigator_LastVisible(UI_STATE_CAT_TEMP,&context),MenuNavigator_PreviousVisible(MenuNavigator_FirstVisible(UI_STATE_CAT_TEMP,&context),UI_STATE_CAT_TEMP,true,&context));
    }
    settings.system.display_mode=2;
    MenuVisibilityContext alwaysOn=MenuNavigator_Context(&settings,true);
    TEST_ASSERT_TRUE(MenuNavigator_IsVisible(UI_STATE_SET_TIMEOUT,&alwaysOn));
    settings.system.display_mode=1;
    MenuVisibilityContext connected=MenuNavigator_Context(&settings,true);
    TEST_ASSERT_TRUE(MenuNavigator_IsVisible(UI_STATE_SET_TIMEOUT,&connected));
    TEST_ASSERT_TRUE(MenuNavigator_IsVisible(UI_STATE_CAT_ZIGBEE_REP,&connected));
    TEST_ASSERT_FALSE(MenuNavigator_IsVisible(UI_STATE_OBJECT_SOURCE,&connected));
    for(unsigned mode=0;mode<EXT_LED_MODE_COUNT;++mode) {
        settings.hardware.ext_led_mode=mode;MenuVisibilityContext context=MenuNavigator_Context(&settings,true);
        TEST_ASSERT_FALSE(MenuNavigator_IsVisible(UI_STATE_CAT_RGB_XY,&context));
        TEST_ASSERT_FALSE(MenuNavigator_IsVisible(UI_STATE_RGB_COLD_X,&context));
        TEST_ASSERT_EQUAL(mode==EXT_LED_MODE_TEMPERATURE,MenuNavigator_IsVisible(UI_STATE_SET_RGB_DANGER,&context));
        TEST_ASSERT_EQUAL(mode==EXT_LED_MODE_TEMPERATURE,MenuNavigator_IsVisible(UI_STATE_SET_RGB_FREEZE,&context));
        TEST_ASSERT_EQUAL(mode!=EXT_LED_MODE_OFF,MenuNavigator_IsVisible(UI_STATE_SET_LED_ANIM,&context));
        TEST_ASSERT_EQUAL(mode!=EXT_LED_MODE_OFF,MenuNavigator_IsVisible(UI_STATE_SET_RGB_BRIGHTNESS,&context));
        TEST_ASSERT_TRUE(MenuNavigator_IsVisible(UI_STATE_SET_LED_MODE,&context));
        TEST_ASSERT_EQUAL(settings.hardware.builtin_led_anim_mode!=0,MenuNavigator_IsVisible(UI_STATE_SET_BUILTIN_STARTUP,&context));
    }
}
void test_MenuVisibilityRefreshCancelsHiddenEditorAndPreservesOnReadFailure(void) {
    visibilitySettings=(SystemSettings){0};visibilitySettings.control.fan_strategy=1;
    visibilityResult=ESP_OK;visibilityConnected=true;
    mocked_ServiceLocator_GetService_fake.custom_fake=visibility_service;
    MenuStateMachine_ForceState(UI_STATE_CAT_TEMP);MenuStateMachine_PushHistory();
    MenuStateMachine_ForceState(UI_STATE_SET_LOW_TEMP);impl.isEditing=true;
    mocked_esp_timer_get_time_fake.return_val=9000000;
    impl.lastActivityTimeMs=123;impl.lastActivityTime=123;
    RefreshVisibility();
    TEST_ASSERT_EQUAL_UINT64(123,impl.lastActivityTimeMs);
    TEST_ASSERT_EQUAL_UINT32(123,impl.lastActivityTime);
    TEST_ASSERT_EQUAL(UI_STATE_FAN_MODE,MenuStateMachine_GetCurrentState());TEST_ASSERT_FALSE(impl.isEditing);
    TEST_ASSERT_EQUAL(UI_STATE_CAT_TEMP,MenuStateMachine_PeekHistory());
    MenuVisibilityContext original=impl.visibility;
    visibilityResult=ESP_ERR_TIMEOUT;visibilitySettings.control.fan_strategy=0;RefreshVisibility();
    TEST_ASSERT_EQUAL_MEMORY(&original,&impl.visibility,sizeof(original));
    TEST_ASSERT_EQUAL(UI_STATE_FAN_MODE,MenuStateMachine_GetCurrentState());
}
void test_MenuVisibilityDisconnectLeavesReportingCategoryWithoutStaleBackTarget(void) {
    visibilitySettings=(SystemSettings){0};visibilityResult=ESP_OK;visibilityConnected=false;
    mocked_ServiceLocator_GetService_fake.custom_fake=visibility_service;
    MenuStateMachine_ForceState(UI_STATE_CAT_DEVICE);MenuStateMachine_PushHistory();
    MenuStateMachine_ForceState(UI_STATE_CAT_ZIGBEE_REP);MenuStateMachine_PushHistory();
    MenuStateMachine_ForceState(UI_STATE_ZB_REP_FAN);impl.isEditing=true;
    RefreshVisibility();
    TEST_ASSERT_EQUAL(UI_STATE_ZIGBEE_PAIR,MenuStateMachine_GetCurrentState());TEST_ASSERT_FALSE(impl.isEditing);
    TEST_ASSERT_EQUAL(UI_STATE_CAT_DEVICE,MenuStateMachine_PeekHistory());
}

#include "Infrastructure/support/console_capture.h"
void test_MenuVisibilityConsoleReadsPolicyAndRejectsSnapshotFailure(void) {
    PrepareMenuAdmission();
    visibilitySettings=(SystemSettings){0};visibilityResult=ESP_OK;visibilityConnected=false;
    mocked_ServiceLocator_GetService_fake.custom_fake=visibility_service;
    char output[512];char state[16];snprintf(state,sizeof(state),"%u",UI_STATE_SET_LOW_TEMP);
    char *args[]={"menu","visible",state,"--machine"};
    TEST_ASSERT_EQUAL(0,test_capture_console(menu_console_cmd_handler,4,args,output,sizeof(output)));
    TEST_ASSERT_NOT_NULL(strstr(output,"visible=1"));
    visibilitySettings.control.fan_manual_mode=true;
    TEST_ASSERT_EQUAL(0,test_capture_console(menu_console_cmd_handler,4,args,output,sizeof(output)));
    TEST_ASSERT_NOT_NULL(strstr(output,"visible=0"));
    visibilityResult=ESP_ERR_TIMEOUT;
    TEST_ASSERT_NOT_EQUAL(0,test_capture_console(menu_console_cmd_handler,4,args,output,sizeof(output)));
    TEST_ASSERT_NULL(strstr(output,"type=menu_visibility"));
    TEST_ASSERT_EQUAL(UI_STATE_MONITOR,MenuStateMachine_GetCurrentState());
}

void test_MenuColorDraftBeginsOnEntryAndSavesAfterUnlock(void) {
    action_calls=edit_calls=0;
    TEST_ASSERT_EQUAL(MENU_OK,MenuPluginManager_RegisterLegacy(UI_STATE_SET_RGB_COLD,"Color",tracked_legacy_handler));
    MenuStateMachine_ForceState(UI_STATE_SET_RGB_COLD);
    click_menu(); TEST_ASSERT_TRUE(impl.isEditing); TEST_ASSERT_EQUAL(0,action_calls);
    click_menu(); TEST_ASSERT_FALSE(impl.isEditing); TEST_ASSERT_EQUAL(1,action_calls);
}

void test_MenuEveryNestedCategoryHasARegistryLabel(void) {
    for(unsigned state=0;state<UI_STATE_COUNT;++state) {
        if(MenuNavigator_GetNodeType((UIState)state)!=MENU_NODE_CATEGORY)continue;
        char label[MENU_LABEL_CAPACITY]={0};
        TEST_ASSERT_TRUE(CopyLabel(&impl.base,(UIState)state,label,sizeof(label)));
        TEST_ASSERT_TRUE(label[0]!=0);
    }
}
void test_MenuNewModeSelectorsUseExistingHumanLabels(void) {
    SystemSettings settings={0};char value[64],unit[16];int percent;
    const LocalizationCatalog *catalog=Localization_Find("tr");
    settings.system.display_mode=2;
    TEST_ASSERT_TRUE(WidgetSystem_GetValue(UI_STATE_SET_DISPLAY_MODE,&settings,value,sizeof(value),unit,sizeof(unit),&percent,catalog));
    TEST_ASSERT_EQUAL_STRING("Daima açık",value);TEST_ASSERT_EQUAL(100,percent);
    settings.system.display_mode=1;
    TEST_ASSERT_TRUE(WidgetSystem_GetValue(UI_STATE_SET_DISPLAY_MODE,&settings,value,sizeof(value),unit,sizeof(unit),&percent,catalog));
    TEST_ASSERT_EQUAL_STRING("Otomatik",value);
    settings.hardware.builtin_led_anim_mode=1;
    TEST_ASSERT_TRUE(WidgetLED_GetValue(UI_STATE_SET_BUILTIN_MODE,&settings,value,sizeof(value),unit,sizeof(unit),&percent,catalog));
    TEST_ASSERT_EQUAL_STRING("Açık",value);
    settings.hardware.builtin_led_anim_mode=0;
    TEST_ASSERT_TRUE(WidgetLED_GetValue(UI_STATE_SET_BUILTIN_MODE,&settings,value,sizeof(value),unit,sizeof(unit),&percent,catalog));
    TEST_ASSERT_EQUAL_STRING("Kapalı",value);
}
void test_MenuNestedFocusSurvivesBackAndHidesUnavailableChild(void) {
    SystemSettings settings={0};settings.hardware.ext_led_mode=EXT_LED_MODE_TEMPERATURE;
    MenuVisibilityContext context=MenuNavigator_Context(&settings,true);
    MenuNavigator_RememberFocus(UI_STATE_CAT_EXTERNAL_LED,UI_STATE_CAT_RGB_REGIONS);
    TEST_ASSERT_EQUAL(UI_STATE_CAT_RGB_REGIONS,MenuNavigator_PreferredChild(UI_STATE_CAT_EXTERNAL_LED,&context));
    MenuNavigator_RememberFocus(UI_STATE_CAT_EXTERNAL_LED,UI_STATE_BACK);
    TEST_ASSERT_EQUAL(UI_STATE_CAT_RGB_REGIONS,MenuNavigator_PreferredChild(UI_STATE_CAT_EXTERNAL_LED,&context));
    context.external_led_mode=EXT_LED_MODE_OFF;
    TEST_ASSERT_EQUAL(UI_STATE_SET_LED_MODE,MenuNavigator_PreferredChild(UI_STATE_CAT_EXTERNAL_LED,&context));
}
void test_MenuDiagnosticNavigationBuildsCompleteNestedBackPath(void) {
    TEST_ASSERT_TRUE(NavigateBranch(UI_STATE_SET_RGB_TEMP_COLD));
    TEST_ASSERT_EQUAL(UI_STATE_CAT_ZONE_COLD,MenuStateMachine_PeekHistory());
    TEST_ASSERT_EQUAL(UI_STATE_CAT_ZONE_COLD,MenuStateMachine_PopHistory());
    TEST_ASSERT_EQUAL(UI_STATE_CAT_RGB_REGIONS,MenuStateMachine_PopHistory());
    TEST_ASSERT_EQUAL(UI_STATE_CAT_EXTERNAL_LED,MenuStateMachine_PopHistory());
    TEST_ASSERT_EQUAL(UI_STATE_CAT_LIGHT,MenuStateMachine_PopHistory());
    TEST_ASSERT_EQUAL(UI_STATE_MONITOR,MenuStateMachine_PopHistory());
}
void test_MenuNestedTopologyHasUniqueParentsAndBoundedDepth(void) {
    unsigned seen[UI_STATE_COUNT]={0};
    const UIState initial[]={UI_STATE_MONITOR,UI_STATE_CAT_TEMP,UI_STATE_CAT_LIGHT,UI_STATE_CAT_DEVICE,UI_STATE_CAT_POWER,UI_STATE_CAT_ZIGBEE_REP};
    for(unsigned category=0;category<sizeof(initial)/sizeof(initial[0])+15U;++category) {
        UIState parent=category<6 ? initial[category] : (UIState)(UI_STATE_CAT_BUILTIN_LED+category-6);
        TEST_ASSERT_EQUAL(parent==UI_STATE_MONITOR ? MENU_NODE_ROOT : MENU_NODE_CATEGORY,MenuNavigator_GetNodeType(parent));
        UIState child=MenuNavigator_GetFirstChild(parent);
        TEST_ASSERT_NOT_EQUAL(UI_STATE_COUNT,child);
        for(unsigned count=0;;++count) {
            TEST_ASSERT_LESS_THAN(UI_STATE_COUNT,count);
            if(child!=UI_STATE_BACK) {
                TEST_ASSERT_EQUAL(0,seen[child]++);
                TEST_ASSERT_EQUAL(parent,MenuNavigator_GetParent(child));
                unsigned depth=0;UIState ancestor=child;
                while(ancestor!=UI_STATE_MONITOR) {
                    TEST_ASSERT_LESS_THAN(MENU_HISTORY_CAPACITY,depth++);
                    ancestor=MenuNavigator_GetParent(ancestor);
                }
            }
            UIState next=MenuNavigator_GetNextSiblingIn(child,parent,false);
            if(next==child) break;
            child=next;
        }
    }
    TEST_ASSERT_EQUAL(1,seen[UI_STATE_SET_BUILTIN_MODE]);
    TEST_ASSERT_EQUAL(1,seen[UI_STATE_SET_DISPLAY_MODE]);
    TEST_ASSERT_EQUAL(1,seen[UI_STATE_SET_RGB_TEMP_COLD]);
    TEST_ASSERT_EQUAL(0,seen[UI_STATE_CAT_RGB_XY]);
}


static SystemSettings idleSettings;
static esp_err_t idleWriteResult,idleReadResult;
static bool idleIgnoreWrite;
static unsigned idleWrites;
static esp_err_t idle_snapshot(IConfigurationManager *self,SystemSettings *out) {
    (void)self;if(idleReadResult==ESP_OK)*out=idleSettings;return idleReadResult;
}
static esp_err_t idle_write(IConfigurationManager *self,const char *block,const ConfigurationFieldUpdate *fields,size_t count,bool persist) {
    (void)self;++idleWrites;TEST_ASSERT_EQUAL_STRING("system",block);TEST_ASSERT_EQUAL(1,count);TEST_ASSERT_TRUE(persist);
    TEST_ASSERT_EQUAL(offsetof(SystemBaseConfigs,menu_auto_exit_enabled),fields[0].offset);TEST_ASSERT_EQUAL(1,fields[0].size);
    if(idleWriteResult==ESP_OK && !idleIgnoreWrite)memcpy(&idleSettings.system.menu_auto_exit_enabled,fields[0].data,1);
    return idleWriteResult;
}
static IConfigurationManager idleOwner={.GetSnapshot=idle_snapshot,.UpdateFields=idle_write};
static IService *idle_service(const char *name) {
    return !strcmp(name,SVC_NAME_CONFIGURATION_MANAGER) ? (IService *)&idleOwner : NULL;
}
void test_MenuConsoleAutoExitPersistsStrictCommandsAndChecksReadback(void) {
    mocked_ServiceLocator_GetService_fake.custom_fake=idle_service;
    idleSettings=(SystemSettings){0};idleSettings.system.menu_auto_exit_enabled=1;
    idleWriteResult=idleReadResult=ESP_OK;idleIgnoreWrite=false;idleWrites=0;
    char output[1024];char *status[]={"menu","auto-exit","status","--machine"};
    char *off[]={"menu","auto-exit","off","--machine"};char *on[]={"menu","auto-exit","on","--machine"};
    TEST_ASSERT_EQUAL(0,test_capture_console(menu_console_cmd_handler,4,status,output,sizeof(output)));
    TEST_ASSERT_NOT_NULL(strstr(output,"type=menu_auto_exit result=ok enabled=1"));TEST_ASSERT_EQUAL(0,idleWrites);
    TEST_ASSERT_EQUAL(0,test_capture_console(menu_console_cmd_handler,4,off,output,sizeof(output)));
    TEST_ASSERT_NOT_NULL(strstr(output,"result=ok enabled=0"));TEST_ASSERT_EQUAL(1,idleWrites);
    idleWriteResult=ESP_FAIL;
    TEST_ASSERT_NOT_EQUAL(0,test_capture_console(menu_console_cmd_handler,4,on,output,sizeof(output)));
    TEST_ASSERT_NOT_NULL(strstr(output,"result=failed"));TEST_ASSERT_EQUAL(0,idleSettings.system.menu_auto_exit_enabled);
    idleWriteResult=ESP_OK;idleIgnoreWrite=true;
    TEST_ASSERT_NOT_EQUAL(0,test_capture_console(menu_console_cmd_handler,4,on,output,sizeof(output)));
    TEST_ASSERT_NOT_NULL(strstr(output,"result=failed"));
    idleIgnoreWrite=false;TEST_ASSERT_EQUAL(0,menu_console_cmd_handler(4,on));
    unsigned before=idleWrites;char *invalid[]={"menu","auto-exit","2"};char *missing[]={"menu","auto-exit"};
    char *extra[]={"menu","auto-exit","off","extra"};
    TEST_ASSERT_NOT_EQUAL(0,menu_console_cmd_handler(3,invalid));TEST_ASSERT_NOT_EQUAL(0,menu_console_cmd_handler(2,missing));
    TEST_ASSERT_NOT_EQUAL(0,menu_console_cmd_handler(4,extra));TEST_ASSERT_EQUAL(before,idleWrites);
    idleReadResult=ESP_FAIL;TEST_ASSERT_NOT_EQUAL(0,menu_console_cmd_handler(4,status));
    TEST_ASSERT_EQUAL(0,mocked_xQueueSend_fake.call_count);
}
int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_MenuEveryNestedCategoryHasARegistryLabel);
    RUN_TEST(test_MenuNewModeSelectorsUseExistingHumanLabels);
    RUN_TEST(test_MenuNestedFocusSurvivesBackAndHidesUnavailableChild);
    RUN_TEST(test_MenuDiagnosticNavigationBuildsCompleteNestedBackPath);
    RUN_TEST(test_MenuNestedTopologyHasUniqueParentsAndBoundedDepth);
    RUN_TEST(test_MenuVisibilityConsoleReadsPolicyAndRejectsSnapshotFailure);
    RUN_TEST(test_MenuVisibilityModesSkipInactiveSettingsAndRetainRecoveryControls);
    RUN_TEST(test_MenuVisibilityRefreshCancelsHiddenEditorAndPreservesOnReadFailure);
    RUN_TEST(test_MenuVisibilityDisconnectLeavesReportingCategoryWithoutStaleBackTarget);
    RUN_TEST(test_MenuWorkerWakeClearsHistoryAndPairingShortcutDoesNotResetNetwork);
    RUN_TEST(test_MenuActivityFullUptimePreservesLegacyViewAndCannotRegress);
    RUN_TEST(test_MenuEncoderCooldownDoesNotBlockAfter32BitClockWrap);
    RUN_TEST(test_MenuPluginActiveCallbackRejectsMutationAndRecursiveDispatch);
    RUN_TEST(test_MenuLabelCopiesRemainOwnedAndFailuresPreserveOutput);
    RUN_TEST(test_MenuPluginModernDeliveryAndDisabledState);
    RUN_TEST(test_MenuFailedStateLockDoesNotMutateOrReleaseUnownedMutex);
    RUN_TEST(test_MenuStopTimeoutPreservesOwnershipRejectsInputAndRetries);
    RUN_TEST(test_MenuStopRejectsWorkerSelfStopAndControlLockFailure);
    RUN_TEST(test_MenuControlAllocationFailureReleasesPrivateResources);
    RUN_TEST(test_MenuInitializationAllocationFailureRollsBackAndRetries);
    RUN_TEST(test_MenuInitializationRejectsInvalidResourcesBeforeAllocation);
    RUN_TEST(test_MenuTaskCreationFailureDoesNotReportStartedAndCanRetry);
    RUN_TEST(test_MenuConfirmApiDispatchesOnlyPendingAcceptedAction);
    RUN_TEST(test_MenuConfirmApiLockFailureKeepsPendingAction);
    RUN_TEST(test_MenuStopRejectsOutstandingPublicConfirmationUntilReturn);
    RUN_TEST(test_MenuConfirmStateLockFailureReleasesLifecycleAdmission);
    RUN_TEST(test_MenuInvalidStatesDoNotChangeStateOrReadLabels);
    RUN_TEST(test_MenuSystemPercentClampsSharedLimits);
    RUN_TEST(test_MenuFreeXYCategoryRoutesAllAxesAndFormatsExactCoordinates);
    RUN_TEST(test_MenuNavigator_TreeTraversal);
    RUN_TEST(test_MenuHealthCallbackExistsAndRejectsStoppedOrMissingWorker);
    RUN_TEST(test_MenuDoesNotExposeUnimplementedBuiltinHeatingSetting);
    RUN_TEST(test_MenuBuiltinBrightnessNavigationAndDisplay);
    RUN_TEST(test_MenuLanguageLivesBesideDisplayTimeout);
    RUN_TEST(test_MenuRGBBrightnessAndFanScaleAreAccurate);
    RUN_TEST(test_MenuAnimationLabelsCoverBothCatalogsWithoutNullOrOutOfRangePercent);
    RUN_TEST(test_MenuStateMachine_History);
    RUN_TEST(test_MenuStateSnapshotAndBoundedHistory);
    RUN_TEST(test_MenuPluginManager_Registration);
    RUN_TEST(test_MenuPluginBatchValidationBusyAndOwnershipAreAtomic);
    RUN_TEST(test_MenuColorDraftBeginsOnEntryAndSavesAfterUnlock);
    RUN_TEST(test_MenuStateMachine_ConfirmationRequirement);
    RUN_TEST(test_MenuPluginManager_AllDeclaredStatesFit);
    RUN_TEST(test_MenuPowerProfileNavigationAndDisplay);
    RUN_TEST(test_MenuLegacyLeafCanEnterEditRotateAndExit);
    RUN_TEST(test_MenuDestructiveActionRequiresConfirmationAndUnlocks);
    RUN_TEST(test_MenuPostEventRejectsInvalidInputsBeforeQueue);
    RUN_TEST(test_MenuPostEventReportsContentionClosureAndQueueFull);
    RUN_TEST(test_MenuConsoleMalformedNumbersNeverQueueOrPublish);
    RUN_TEST(test_MenuConsoleAutoExitPersistsStrictCommandsAndChecksReadback);
    RUN_TEST(test_MenuConsoleRejectsExtraNullAndMissingArguments);
    RUN_TEST(test_MenuConsoleNavigationAcknowledgesAdmissionWithoutApplyingState);
    RUN_TEST(test_MenuConsoleInputRejectsBusFailureAndPreservesTypedDirection);
    RUN_TEST(test_MenuConfigurationRejectsUnsafeResourceBoundsBeforeAllocation);
    RUN_TEST(test_MenuConsoleMachineStateLanguageAndQueuedAdmission);
    return UNITY_END();
}


#include <setjmp.h>
static jmp_buf menu_navigation_exit;
static unsigned menu_navigation_step;
static BaseType_t receive_menu_navigation(QueueHandle_t queue, void *out, TickType_t ticks) {
    (void)queue; (void)ticks;
    if (menu_navigation_step == 1) {
        TEST_ASSERT_EQUAL(UI_STATE_MONITOR, MenuStateMachine_GetCurrentState());
        TEST_ASSERT_FALSE(impl.isEditing);
        TEST_ASSERT_EQUAL(UI_STATE_MONITOR, MenuStateMachine_PeekHistory());
    }
    if (menu_navigation_step == 2) {
        TEST_ASSERT_EQUAL(UI_STATE_ZIGBEE_PAIR, MenuStateMachine_GetCurrentState());
        TEST_ASSERT_TRUE(impl.isEditing); TEST_ASSERT_FALSE(impl.confirmSelection);
        longjmp(menu_navigation_exit, 1);
    }
    *(MenuEvent *)out = (MenuEvent){.type = menu_navigation_step++ ? MENU_EVENT_BUTTON_LONG_PRESSED : MENU_EVENT_WAKE_UP};
    return pdPASS;
}
void test_MenuWorkerWakeClearsHistoryAndPairingShortcutDoesNotResetNetwork(void) {
    impl.mutex = (void *)2; impl.eventQueue = (void *)3;
    MenuStateMachine_TransitionTo(UI_STATE_CAT_TEMP); MenuStateMachine_PushHistory();
    MenuStateMachine_TransitionTo(UI_STATE_SET_HIGH_TEMP); impl.isEditing = true; impl.confirmSelection = true;
    menu_navigation_step = 0; mocked_xQueueReceive_fake.custom_fake = receive_menu_navigation;
    if (setjmp(menu_navigation_exit) == 0) MenuTask(NULL);
    TEST_ASSERT_EQUAL(2, menu_navigation_step);
}

void test_MenuFreeXYCategoryRoutesAllAxesAndFormatsExactCoordinates(void) {
    TEST_ASSERT_FALSE(MenuNavigator_IsVisible(UI_STATE_CAT_RGB_XY,NULL));
    TEST_ASSERT_FALSE(MenuNavigator_IsVisible(UI_STATE_RGB_COLD_X,NULL));
    TEST_ASSERT_FALSE(MenuNavigator_IsVisible(UI_STATE_SET_RGB_TEMP_COLD_MAX,NULL));
    TEST_ASSERT_FALSE(MenuNavigator_IsVisible(UI_STATE_SET_RGB_TEMP_WARM_MAX,NULL));
    TEST_ASSERT_FALSE(MenuNavigator_IsVisible(UI_STATE_SET_RGB_TEMP_HOT_MAX,NULL));
    TEST_ASSERT_EQUAL(UI_STATE_COUNT,MenuNavigator_GetFirstChild(UI_STATE_CAT_RGB_XY));
    UIState state=UI_STATE_RGB_COLD_X;
    SystemSettings settings={0};
    for (unsigned coordinate=0;coordinate<RGB_ZONE_COUNT*2;++coordinate) {
        TEST_ASSERT_EQUAL(UI_STATE_RGB_COLD_X+coordinate,state);
        TEST_ASSERT_FALSE(MenuNavigator_IsVisible(state,NULL));
        size_t ox,oy;TEST_ASSERT_TRUE(SystemConfigs_RGBZoneOffsets(coordinate/2,&ox,&oy));
        uint16_t x=1234,y=4321;
        memcpy((uint8_t *)&settings.hardware+ox,&x,sizeof(x));
        memcpy((uint8_t *)&settings.hardware+oy,&y,sizeof(y));
        char value[32],unit[16],label[64];int percent=-1;
        TEST_ASSERT_TRUE(WidgetRGB_GetValue(state,&settings,value,sizeof(value),unit,sizeof(unit),&percent, Localization_Snapshot()));
        TEST_ASSERT_EQUAL_STRING(coordinate%2 ? "4321" : "1234",value);
        TEST_ASSERT_EQUAL_STRING("XY",unit);
        TEST_ASSERT_TRUE(WidgetRGB_FormatLabel(state,"Coordinate",&settings,label,sizeof(label), Localization_Snapshot()));
        TEST_ASSERT_EQUAL_STRING(coordinate%2 ? "Coordinate: 4321" : "Coordinate: 1234",label);
        state=(UIState)(state+1);
    }
    TEST_ASSERT_EQUAL(UI_STATE_RGB_FREEZE_Y+1,state);
}

void test_MenuLanguageLivesBesideDisplayTimeout(void) {
    MenuNavigator_Init();
    TEST_ASSERT_EQUAL(UI_STATE_CAT_SCREEN,MenuNavigator_GetFirstChild(UI_STATE_CAT_DEVICE));
    TEST_ASSERT_EQUAL(UI_STATE_SET_LANGUAGE,MenuNavigator_GetNextSiblingIn(UI_STATE_CAT_SCREEN,UI_STATE_CAT_DEVICE,false));
    TEST_ASSERT_EQUAL(UI_STATE_SET_TEMP_UNIT,MenuNavigator_GetNextSiblingIn(UI_STATE_SET_LANGUAGE,UI_STATE_CAT_DEVICE,false));
    SystemSettings settings={0};char value[128],unit[16];int percent;
    const LocalizationCatalog *english=Localization_Find("en"),*turkish=Localization_Find("tr");
    TEST_ASSERT_TRUE(WidgetSystem_GetValue(UI_STATE_SET_LANGUAGE,&settings,value,sizeof(value),unit,sizeof(unit),&percent,english));
    TEST_ASSERT_EQUAL_STRING("English",value);TEST_ASSERT_EQUAL_STRING("",unit);
    TEST_ASSERT_INT_WITHIN(50,50,percent);
    TEST_ASSERT_TRUE(WidgetSystem_GetValue(UI_STATE_SET_LANGUAGE,&settings,value,sizeof(value),unit,sizeof(unit),&percent,turkish));
    TEST_ASSERT_EQUAL_STRING("Türkçe",value);
}
void test_MenuRGBBrightnessAndFanScaleAreAccurate(void) {
    SystemSettings settings={0};char value[128],unit[16];int percent;
    unsigned levels[]={0,64,128,204,255},expected[]={0,25,50,80,100};
    for(unsigned i=0;i<5;++i) {
        settings.hardware.ext_led_brightness=levels[i];
        TEST_ASSERT_TRUE(WidgetRGB_GetValue(UI_STATE_SET_RGB_BRIGHTNESS,&settings,value,sizeof(value),unit,sizeof(unit),&percent,Localization_Find("tr")));
        TEST_ASSERT_EQUAL(expected[i],percent);TEST_ASSERT_EQUAL_STRING("%",unit);
    }
    float temperatures[]={20,59.5f,99};unsigned scales[]={0,50,100};
    for(unsigned i=0;i<3;++i) {
        settings.control.relay_open_temp=settings.control.relay_close_temp=temperatures[i];
        TEST_ASSERT_TRUE(WidgetTemp_GetValue(UI_STATE_SET_HIGH_TEMP,&settings,value,sizeof(value),unit,sizeof(unit),&percent,Localization_Find("tr")));
        TEST_ASSERT_EQUAL(scales[i],percent);
        TEST_ASSERT_TRUE(WidgetTemp_GetValue(UI_STATE_SET_LOW_TEMP,&settings,value,sizeof(value),unit,sizeof(unit),&percent,Localization_Find("tr")));
        TEST_ASSERT_EQUAL(scales[i],percent);
    }
}

#include "Infrastructure/support/console_capture.h"
void test_MenuConsoleMachineStateLanguageAndQueuedAdmission(void) {
    PrepareMenuAdmission(); char output[2048], originalOutput[2048];
    const LocalizationCatalog *baseline=Localization_Snapshot();
    char *status[]={"menu","status","--machine"};
    TEST_ASSERT_TRUE(Localization_Apply("tr"));
    TEST_ASSERT_EQUAL(0,test_capture_console(menu_console_cmd_handler,3,status,originalOutput,sizeof(originalOutput)));
    TEST_ASSERT_NOT_NULL(strstr(originalOutput,"RACOOL_CLI v=1 type=menu_status state=0 registry_label_hex="));
    TEST_ASSERT_TRUE(Localization_Apply("en"));
    TEST_ASSERT_EQUAL(0,test_capture_console(menu_console_cmd_handler,3,status,output,sizeof(output)));
    TEST_ASSERT_EQUAL_STRING(originalOutput,output);
    TEST_ASSERT_EQUAL(0,test_capture_console(menu_console_cmd_handler,2,status,output,sizeof(output)));
    TEST_ASSERT_NOT_NULL(strstr(output,"MenuManager Status"));
    TEST_ASSERT_TRUE(Localization_Apply("tr"));
    TEST_ASSERT_EQUAL(0,test_capture_console(menu_console_cmd_handler,2,status,output,sizeof(output)));
    TEST_ASSERT_NOT_NULL(strstr(output,"Menü Yöneticisi Durumu"));
    char *nav[]={"menu","nav","7","--machine"};
    TEST_ASSERT_EQUAL(0,test_capture_console(menu_console_cmd_handler,4,nav,output,sizeof(output)));
    TEST_ASSERT_NOT_NULL(strstr(output,"RACOOL_CLI v=1 type=menu_navigation result=queued state=7"));
    TEST_ASSERT_EQUAL(UI_STATE_MONITOR,MenuStateMachine_GetCurrentState());
    mocked_xQueueSend_fake.return_val=pdFALSE;
    TEST_ASSERT_NOT_EQUAL(0,test_capture_console(menu_console_cmd_handler,4,nav,output,sizeof(output)));
    TEST_ASSERT_NOT_NULL(strstr(output,"result=rejected"));
    TEST_ASSERT_TRUE(Localization_Apply(baseline->code));
}
