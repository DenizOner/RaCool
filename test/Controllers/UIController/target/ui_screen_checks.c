#include "unity.h"
#include <string.h>
#include "lvgl.h"
#include "Controllers/UIController/UIControllerInternal.h"
#include "Controllers/UIController/BootAnimation.h"
#include "Controllers/UIController/UITranslation.h"
#include "Hardware/LEDAnimations/LEDAnimationCatalog.h"
#include "Hardware/RGBLED/RGBZoneSelection.h"

/* Real LVGL objects; inject failure only at the application's create API boundary. */
UIControllerImpl uiControllerInstance;
static unsigned allocations, fail_at, live_objects, live_groups, boot_starts;
static lv_display_t *screen_display;
static lv_obj_t *original_screen;
static lv_group_t *original_group;
static bool screen_lock;
static void object_deleted(lv_event_t *event) { (void)event; live_objects--; }
static lv_obj_t *track_object(lv_obj_t *object) {
    if (object) { live_objects++; lv_obj_add_event_cb(object, object_deleted, LV_EVENT_DELETE, NULL); }
    return object;
}
static lv_obj_t *create_object(lv_obj_t *parent) {
    return ++allocations == fail_at ? NULL : track_object(lv_obj_create(parent));
}
static lv_obj_t *create_label(lv_obj_t *parent) {
    return ++allocations == fail_at ? NULL : track_object(lv_label_create(parent));
}
static lv_obj_t *create_image(lv_obj_t *parent) {
    return ++allocations == fail_at ? NULL : track_object(lv_image_create(parent));
}
static lv_obj_t *create_bar(lv_obj_t *parent) {
    return ++allocations == fail_at ? NULL : track_object(lv_bar_create(parent));
}
static lv_group_t *create_group(void) {
    if (++allocations == fail_at) return NULL;
    lv_group_t *group = lv_group_create();
    if (group) live_groups++;
    return group;
}
static void delete_group(lv_group_t *group) { lv_group_delete(group); live_groups--; }
void BootAnimation_Start(void) { boot_starts++; }
/* The screen tests exercise the post-boot renderer, not boot playback. */
bool BootAnimation_IsRunning(void) { return false; }
void BootAnimation_Tick(uint32_t nowMs) { (void)nowMs; TEST_FAIL_MESSAGE("Unexpected boot playback in post-boot screen test"); }
static ServiceState screen_state(IService *self) { (void)self; return SERVICE_STATE_STARTED; }
static bool screen_take(IOLED *self) { (void)self; TEST_ASSERT_FALSE(screen_lock); screen_lock = true; return true; }
static void screen_give(IOLED *self) { (void)self; TEST_ASSERT_TRUE(screen_lock); screen_lock = false; }
static IOLED screen_oled = {.base = {.GetState = screen_state}, .Lock = screen_take, .Unlock = screen_give};
static UIState screen_category,screen_selection;
static UIState target_category(const IMenuManager *self) { (void)self;return screen_category; }
static UIState target_selection(const IMenuManager *self) { (void)self;return screen_selection; }
static bool target_confirmation;
static bool target_confirm(const IMenuManager *self) { (void)self;return target_confirmation; }
static uint32_t target_activity(const IMenuManager *self) { (void)self;return 0; }
static bool target_label(const IMenuManager *self,UIState state,char *output,size_t capacity) {
    (void)self;
    /* Match the production interface: CopyLabel supplies a bounded registry
     * fallback; ScreenMenu resolves the full localized label separately. */
    const char *text=MenuManager_GetStateLabel(state);
    if(!text || strlen(text)>=capacity) return false;
    memcpy(output,text,strlen(text)+1);
    return true;
}
static IMenuManager screen_menu={.GetActiveCategory=target_category,.GetCurrentState=target_selection,
    .GetConfirmSelection=target_confirm,.CopyLabel=target_label,.IsEditing=target_confirm,
    .GetLastActivityTime=target_activity};
#define lv_obj_create create_object
#define lv_image_create create_image
#define lv_label_create create_label
#define lv_bar_create create_bar
#define lv_group_create create_group
#define lv_group_delete delete_group
#include "../../../../src/Controllers/UIController/Screens/ScreenMonitor.c"
#include "../../../../src/Controllers/UIController/Screens/ScreenMenu.c"
#include "../../../../src/Controllers/UIController/Screens/ScreenSubmenu.c"
#include "../../../../src/Controllers/UIController/Screens/ScreenZigbeePair.c"
#include "../../../../src/Controllers/UIController/Screens/ScreenBoot.c"
#include "../../../../src/Controllers/UIController/UIRenderer.c"
#undef lv_obj_create
#undef lv_image_create
#undef lv_label_create
#undef lv_bar_create
#undef lv_group_create
#undef lv_group_delete

void target_ui_screen_cleanup(void) {
    if (!screen_display) return;
    lv_screen_load(original_screen);
    lv_group_set_default(original_group);
    UIRenderer_DiscardPartialUI();
    lv_display_delete(screen_display);
    screen_display = NULL;
    screen_lock = false;
}
static void verify_status_ink(lv_obj_t *object, unsigned slot,
                              int left, int top, int width, int height) {
    int x = lv_obj_get_x(object) + left, y = lv_obj_get_y(object) + top;
    TEST_ASSERT_TRUE(x >= 83 + (int)slot * 15 && x + width <= 98 + (int)slot * 15);
    TEST_ASSERT_TRUE(y >= 43 && y + height <= 64);
    /* Even-width ink necessarily has a half-pixel center on a pixel grid. */
    TEST_ASSERT_INT_WITHIN(1, 2 * (90 + (int)slot * 15), 2 * x + width - 1);
    TEST_ASSERT_INT_WITHIN(1, 106, 2 * y + height - 1);
}
void target_ui_screen_verify_rollback(void) {
    if (!lv_is_initialized()) lv_init();
    memset(&uiControllerInstance, 0, sizeof(uiControllerInstance));
    screen_display = lv_display_create(128, 64);
    TEST_ASSERT_NOT_NULL(screen_display);
    lv_display_set_default(screen_display);
    original_screen = lv_screen_active();
    original_group = lv_group_get_default();
    uiControllerInstance.oled = &screen_oled;
    allocations = fail_at = live_objects = live_groups = boot_starts = 0;
    TEST_ASSERT_TRUE(UIRenderer_CreateUI());
    TEST_ASSERT_EQUAL(LV_ALIGN_TOP_RIGHT,lv_obj_get_style_align(uiControllerInstance.bootVersionLabel,0));
    lv_obj_update_layout(uiControllerInstance.bootScreen);
    TEST_ASSERT_TRUE(lv_obj_get_x(uiControllerInstance.bootLogoLabel)+lv_obj_get_width(uiControllerInstance.bootLogoLabel)<lv_obj_get_x(uiControllerInstance.bootVersionLabel));
    lv_point_t version_size;
    lv_text_get_size(&version_size,lv_label_get_text(uiControllerInstance.bootVersionLabel),&racool_font_10,0,0,LV_COORD_MAX,LV_TEXT_FLAG_NONE);
    TEST_ASSERT_TRUE(version_size.x<=lv_obj_get_content_width(uiControllerInstance.bootVersionLabel));
    TEST_ASSERT_EQUAL(LV_LABEL_LONG_MODE_CLIP,lv_label_get_long_mode(uiControllerInstance.bootVersionLabel));
    TEST_ASSERT_EQUAL(LV_TEXT_ALIGN_CENTER,lv_obj_get_style_text_align(uiControllerInstance.bootTaglineLabel,0));
    TEST_ASSERT_EQUAL(LV_TEXT_ALIGN_CENTER,lv_obj_get_style_text_align(uiControllerInstance.bootStatusLabel,0));
    TEST_ASSERT_TRUE(lv_obj_get_y(uiControllerInstance.bootLogoLabel)+lv_obj_get_height(uiControllerInstance.bootLogoLabel)<=lv_obj_get_y(uiControllerInstance.bootTopLine));
    TEST_ASSERT_TRUE(lv_obj_get_y(uiControllerInstance.bootTopLine)<lv_obj_get_y(uiControllerInstance.bootTaglineLabel));
    TEST_ASSERT_TRUE(lv_obj_get_y(uiControllerInstance.bootTaglineLabel)+lv_obj_get_height(uiControllerInstance.bootTaglineLabel)<=lv_obj_get_y(uiControllerInstance.bootBottomLine));
    TEST_ASSERT_TRUE(lv_obj_get_y(uiControllerInstance.bootProgressBar)+lv_obj_get_height(uiControllerInstance.bootProgressBar)<=lv_obj_get_y(uiControllerInstance.bootStatusLabel));
    unsigned creation_allocations=allocations;
    // Reproduce the old geometry with the actual font and LVGL animation engine.
    lv_obj_t *short_line=lv_label_create(uiControllerInstance.submenuScreen);
    TEST_ASSERT_NOT_NULL(short_line);
    lv_obj_set_size(short_line,128,16);
    lv_obj_set_style_pad_all(short_line,0,0);
    lv_obj_set_style_border_width(short_line,1,0);
    lv_obj_set_style_text_font(short_line,&racool_font_10,0);
    lv_label_set_long_mode(short_line,LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
    lv_label_set_text(short_line,"Celsius");
    lv_obj_update_layout(short_line);
    TEST_ASSERT_LESS_THAN(racool_font_10.line_height,lv_obj_get_content_height(short_line));
    TEST_ASSERT_NOT_NULL(lv_anim_get(short_line,NULL));
    lv_obj_delete(short_line);
    TEST_ASSERT_FALSE(lv_obj_is_scrollable(uiControllerInstance.menuScreen));
    TEST_ASSERT_FALSE(lv_obj_is_scrollable(uiControllerInstance.menuSidebar));
    TEST_ASSERT_TRUE(lv_obj_is_scrollable(uiControllerInstance.menuList));
    TEST_ASSERT_EQUAL(LV_DIR_VER, lv_obj_get_scroll_dir(uiControllerInstance.menuList));
    TEST_ASSERT_EQUAL(2, lv_obj_get_style_width(uiControllerInstance.menuList, LV_PART_SCROLLBAR));
    TEST_ASSERT_EQUAL(LV_OPA_COVER, lv_obj_get_style_bg_opa(uiControllerInstance.menuList, LV_PART_SCROLLBAR));
    TEST_ASSERT_TRUE(lv_color_eq(lv_color_white(), lv_obj_get_style_bg_color(uiControllerInstance.menuList, LV_PART_SCROLLBAR)));
    // Real LVGL overflow geometry, without attaching a physical OLED.
    lv_obj_t *overflow = lv_obj_create(uiControllerInstance.menuList);
    TEST_ASSERT_NOT_NULL(overflow);
    lv_obj_set_size(overflow, 80, 120);
    lv_obj_update_layout(uiControllerInstance.menuList);
    lv_area_t horizontal, vertical;
    lv_obj_get_scrollbar_area(uiControllerInstance.menuList, &horizontal, &vertical);
    TEST_ASSERT_EQUAL(2, lv_area_get_width(&vertical));
    TEST_ASSERT_GREATER_THAN(0, lv_area_get_height(&vertical));
    TEST_ASSERT_EQUAL(0, lv_area_get_width(&horizontal));
    lv_obj_delete(overflow);
    SystemSettings settings = {0};
    MenuNavigator_Init();
    uiControllerInstance.menu=&screen_menu;
    for (unsigned language=0; language<2; ++language) {
        const LocalizationCatalog *catalog=Localization_Find(language ? "en" : "tr");
        TEST_ASSERT_NOT_NULL(catalog);
        ScreenMonitor_Update(0,&settings,25,24,30,false,CLIMATE_FAN_OBJECT_SENSITIVE,true,3,catalog);
        lv_obj_update_layout(uiControllerInstance.mainScreen);
        TEST_ASSERT_EQUAL_STRING(language ? "System stable" : "Sistem kararlı",
                                lv_label_get_text(uiControllerInstance.statusLabel));
        TEST_ASSERT_EQUAL_PTR(&racool_font_10,lv_obj_get_style_text_font(uiControllerInstance.statusLabel,0));
        TEST_ASSERT_EQUAL_PTR(&racool_font_20,lv_obj_get_style_text_font(uiControllerInstance.tempLabel,0));
        TEST_ASSERT_EQUAL_PTR(&lv_font_montserrat_10,lv_obj_get_style_text_font(uiControllerInstance.zigbeeIcon,0));
        TEST_ASSERT_TRUE(lv_obj_get_y(uiControllerInstance.tempLabel)+lv_obj_get_height(uiControllerInstance.tempLabel)<=44);
        TEST_ASSERT_TRUE(lv_obj_get_x(uiControllerInstance.tempLabel)+lv_obj_get_width(uiControllerInstance.tempLabel)<=83);
        TEST_ASSERT_TRUE(lv_obj_get_y(uiControllerInstance.ambientLabel)+lv_obj_get_height(uiControllerInstance.ambientLabel)<=21);
        TEST_ASSERT_TRUE(lv_obj_get_y(uiControllerInstance.cpuLabel)+lv_obj_get_height(uiControllerInstance.cpuLabel)<=42);
        TEST_ASSERT_TRUE(lv_obj_get_y(uiControllerInstance.statusLabel)+lv_obj_get_height(uiControllerInstance.statusLabel)<=64);
        TEST_ASSERT_TRUE(lv_obj_get_x(uiControllerInstance.statusLabel)+lv_obj_get_width(uiControllerInstance.statusLabel)<=83);
        screen_category=UI_STATE_CAT_TEMP;screen_selection=UI_STATE_FAN_MODE;
        settings.control.fan_manual_mode=false;
        for(unsigned strategy=0;strategy<2;++strategy) {
            settings.control.fan_strategy=strategy;
            TEST_ASSERT_TRUE(ScreenMenu_Update(&settings,25,true,catalog));
            lv_obj_update_layout(uiControllerInstance.menuScreen);
            for (unsigned icon=0;icon<4;++icon) {
                TEST_ASSERT_EQUAL(2+icon*16,lv_obj_get_y(uiControllerInstance.menuIcons[icon]));
                TEST_ASSERT_EQUAL(1,lv_obj_get_style_pad_top(uiControllerInstance.menuIcons[icon],0));
            }
            TEST_ASSERT_EQUAL(3,lv_obj_get_child_count(uiControllerInstance.menuList));
        }
        settings.control.fan_manual_mode=true;
        TEST_ASSERT_TRUE(ScreenMenu_Update(&settings,25,true,catalog));
        TEST_ASSERT_EQUAL(2,lv_obj_get_child_count(uiControllerInstance.menuList));
        screen_category=UI_STATE_CAT_DEVICE;screen_selection=UI_STATE_SET_LANGUAGE;
        settings.system.display_mode=2;
        TEST_ASSERT_TRUE(ScreenMenu_Update(&settings,25,false,catalog));
        TEST_ASSERT_EQUAL(5,lv_obj_get_child_count(uiControllerInstance.menuList));
        settings.system.display_mode=1;
        TEST_ASSERT_TRUE(ScreenMenu_Update(&settings,25,true,catalog));
        TEST_ASSERT_EQUAL(5,lv_obj_get_child_count(uiControllerInstance.menuList));
        screen_category=UI_STATE_CAT_EXTERNAL_LED;screen_selection=UI_STATE_SET_LED_MODE;
        for(unsigned mode=0;mode<EXT_LED_MODE_COUNT;++mode) {
            settings.hardware.ext_led_mode=mode;
            TEST_ASSERT_TRUE(ScreenMenu_Update(&settings,25,true,catalog));
            TEST_ASSERT_EQUAL(mode==EXT_LED_MODE_TEMPERATURE ? 5 : (mode==EXT_LED_MODE_OFF ? 2 : 4),lv_obj_get_child_count(uiControllerInstance.menuList));
            lv_obj_update_layout(uiControllerInstance.menuScreen);
            for (unsigned icon=0;icon<4;++icon) {
                TEST_ASSERT_EQUAL(2+icon*16,lv_obj_get_y(uiControllerInstance.menuIcons[icon]));
                TEST_ASSERT_EQUAL(12,lv_obj_get_height(uiControllerInstance.menuIcons[icon]));
                TEST_ASSERT_EQUAL(1,lv_obj_get_style_pad_top(uiControllerInstance.menuIcons[icon],0));
                TEST_ASSERT_GREATER_OR_EQUAL(lv_font_montserrat_10.line_height,lv_obj_get_content_height(uiControllerInstance.menuIcons[icon]));
            }
        }
        screen_category=UI_STATE_CAT_DEVICE;screen_selection=UI_STATE_SET_LANGUAGE;
        TEST_ASSERT_TRUE(ScreenMenu_Update(&settings,25,true,catalog));
        lv_obj_update_layout(uiControllerInstance.menuScreen);
        for (unsigned icon=0;icon<4;++icon) {
            TEST_ASSERT_EQUAL(2+icon*16,lv_obj_get_y(uiControllerInstance.menuIcons[icon]));
            TEST_ASSERT_EQUAL(1,lv_obj_get_style_pad_top(uiControllerInstance.menuIcons[icon],0));
        }
        TEST_ASSERT_EQUAL_STRING(language ? "Preferences" : "Tercihler",
                                lv_label_get_text(uiControllerInstance.menuHeader));
        TEST_ASSERT_EQUAL(16,lv_obj_get_height(uiControllerInstance.menuHeader));
        screen_category=UI_STATE_CAT_CALIBRATION;screen_selection=UI_STATE_CALIBRATE_OBJECT;
        TEST_ASSERT_TRUE(ScreenMenu_Update(&settings,25,true,catalog));
        TEST_ASSERT_EQUAL(3,lv_obj_get_child_count(uiControllerInstance.menuList));
        const char *calibrationKeys[]={"menu.calibration_radiator","menu.calibration_ambient"};
        for(unsigned row=0;row<2;++row) {
            lv_obj_t *label=lv_obj_get_child(lv_obj_get_child(uiControllerInstance.menuList,(int32_t)row),0);
            const char *shortName=Localization_TextKey(catalog,calibrationKeys[row]);
            TEST_ASSERT_EQUAL_PTR(lv_label_get_text(label),strstr(lv_label_get_text(label),shortName));
        }
        screen_category=UI_STATE_CAT_ZIGBEE;screen_selection=UI_STATE_SET_ZIGBEE_TX_PROFILE;
        TEST_ASSERT_TRUE(ScreenMenu_Update(&settings,25,true,catalog));
        TEST_ASSERT_EQUAL(4,lv_obj_get_child_count(uiControllerInstance.menuList));
        const char *radioKeys[]={"menu.pairing_short","menu.transmit_power_short"};
        for(unsigned row=0;row<2;++row) {
            lv_obj_t *label=lv_obj_get_child(lv_obj_get_child(uiControllerInstance.menuList,(int32_t)row),0);
            const char *shortName=Localization_TextKey(catalog,radioKeys[row]);
            TEST_ASSERT_EQUAL_PTR(lv_label_get_text(label),strstr(lv_label_get_text(label),shortName));
        }
        {
            uint32_t savedInterval=settings.system.telemetry_interval_ms;
            screen_category=UI_STATE_CAT_ADVANCED;screen_selection=UI_STATE_SET_TELEMETRY;
            settings.system.telemetry_interval_ms=0;
            TEST_ASSERT_TRUE(ScreenMenu_Update(&settings,25,true,catalog));
            lv_obj_t *telemetryRow=lv_obj_get_child(lv_obj_get_child(uiControllerInstance.menuList,0),0);
            TEST_ASSERT_NOT_NULL(strstr(lv_label_get_text(telemetryRow),Localization_Text(catalog,LOC_COMMON_OFF)));
            TEST_ASSERT_NULL(strstr(lv_label_get_text(telemetryRow),"0 s"));
            settings.system.telemetry_interval_ms=savedInterval;
        }
        screen_category=UI_STATE_CAT_POWER;screen_selection=UI_STATE_SET_POWER_PROFILE;
        TEST_ASSERT_TRUE(ScreenMenu_Update(&settings,25,true,catalog));
        lv_obj_update_layout(uiControllerInstance.menuScreen);
        for (unsigned icon=0;icon<4;++icon) {
            TEST_ASSERT_EQUAL(2+icon*16,lv_obj_get_y(uiControllerInstance.menuIcons[icon]));
            TEST_ASSERT_EQUAL(1,lv_obj_get_style_pad_top(uiControllerInstance.menuIcons[icon],0));
        }
        screen_category=UI_STATE_CAT_DEVICE;screen_selection=UI_STATE_SET_LANGUAGE;
        TEST_ASSERT_TRUE(ScreenMenu_Update(&settings,25,true,catalog));
        screen_category=UI_STATE_CAT_ZIGBEE_REP;screen_selection=UI_STATE_ZB_REP_FAN;
        TEST_ASSERT_TRUE(ScreenMenu_Update(&settings,25,true,catalog));
        TEST_ASSERT_EQUAL(LV_OPA_COVER,lv_obj_get_style_bg_opa(uiControllerInstance.menuIcons[3],0));
        TEST_ASSERT_EQUAL(7,lv_obj_get_child_count(uiControllerInstance.menuList));
        const char *reportKeys[]={"menu.report_radiator_short","menu.report_ambient_short",
            "menu.report_cpu_short","menu.report_health_short","menu.report_memory_short","menu.report_uptime_short"};
        for(unsigned row=0;row<6;++row) {
            lv_obj_t *label=lv_obj_get_child(lv_obj_get_child(uiControllerInstance.menuList,(int32_t)row),0);
            const char *shortName=Localization_TextKey(catalog,reportKeys[row]);
            TEST_ASSERT_EQUAL_PTR(lv_label_get_text(label),strstr(lv_label_get_text(label),shortName));
        }
        screen_category=UI_STATE_CAT_ZONE_COLD;screen_selection=UI_STATE_SET_RGB_COLD;
        uint8_t savedXyMode=settings.hardware.ext_led_mode;
        settings.hardware.ext_led_mode=EXT_LED_MODE_TEMPERATURE;
        TEST_ASSERT_TRUE(ScreenMenu_Update(&settings,25,true,catalog));
        lv_obj_update_layout(uiControllerInstance.menuScreen);
        TEST_ASSERT_EQUAL(LV_OPA_COVER,lv_obj_get_style_bg_opa(uiControllerInstance.menuIcons[1],0));
        TEST_ASSERT_EQUAL(LV_OPA_TRANSP,lv_obj_get_style_bg_opa(uiControllerInstance.menuIcons[2],0));
        for (unsigned icon=0;icon<4;++icon)
            TEST_ASSERT_EQUAL(2+icon*16,lv_obj_get_y(uiControllerInstance.menuIcons[icon]));
        const UIState zoneCategories[]={UI_STATE_CAT_ZONE_FREEZE,UI_STATE_CAT_ZONE_COLD,
            UI_STATE_CAT_ZONE_WARM,UI_STATE_CAT_ZONE_HOT,UI_STATE_CAT_ZONE_DANGER};
        for(unsigned zone=0;zone<5;++zone) {
            screen_category=zoneCategories[zone];
            screen_selection=MenuNavigator_GetFirstChild(screen_category);
            TEST_ASSERT_TRUE(ScreenMenu_Update(&settings,25,true,catalog));
            TEST_ASSERT_EQUAL(3,lv_obj_get_child_count(uiControllerInstance.menuList));
            lv_obj_t *first=lv_obj_get_child(lv_obj_get_child(uiControllerInstance.menuList,0),0);
            lv_obj_t *second=lv_obj_get_child(lv_obj_get_child(uiControllerInstance.menuList,1),0);
            const char *boundary=Localization_TextKey(catalog,zone==0 || zone==4 ? "menu.zone_limit" : "menu.zone_range");
            const char *color=Localization_TextKey(catalog,"menu.zone_color");
            TEST_ASSERT_EQUAL_PTR(lv_label_get_text(first),strstr(lv_label_get_text(first),boundary));
            TEST_ASSERT_EQUAL_PTR(lv_label_get_text(second),strstr(lv_label_get_text(second),color));
        }
        settings.hardware.ext_led_mode=savedXyMode;
        screen_category=UI_STATE_CAT_DEVICE;screen_selection=UI_STATE_SET_LANGUAGE;
        TEST_ASSERT_TRUE(ScreenMenu_Update(&settings,25,true,catalog));
        TEST_ASSERT_EQUAL(16,lv_obj_get_y(uiControllerInstance.menuList));
        TEST_ASSERT_EQUAL(32,lv_obj_get_height(uiControllerInstance.menuList));
        TEST_ASSERT_EQUAL(48,lv_obj_get_y(uiControllerInstance.menuFooter));
        for (uint32_t row=0;row<lv_obj_get_child_count(uiControllerInstance.menuList);++row)
            TEST_ASSERT_EQUAL(16,lv_obj_get_height(lv_obj_get_child(uiControllerInstance.menuList,(int32_t)row)));
        ScreenSubmenu_Update(&settings,catalog);
        lv_obj_update_layout(uiControllerInstance.submenuScreen);
        TEST_ASSERT_FALSE(lv_obj_is_scrollable(uiControllerInstance.submenuScreen));
        TEST_ASSERT_GREATER_OR_EQUAL(racool_font_10.line_height,lv_obj_get_content_height(uiControllerInstance.submenuBreadcrumb));
        TEST_ASSERT_GREATER_OR_EQUAL(racool_font_10.line_height,lv_obj_get_content_height(uiControllerInstance.submenuFooter));
        TEST_ASSERT_EQUAL(0,lv_obj_get_y(uiControllerInstance.submenuBreadcrumb));
        TEST_ASSERT_EQUAL(64,lv_obj_get_y(uiControllerInstance.submenuFooter)+lv_obj_get_height(uiControllerInstance.submenuFooter));
        TEST_ASSERT_EQUAL_STRING(language ? "Language" : "Dil",lv_label_get_text(uiControllerInstance.submenuBreadcrumb));
        TEST_ASSERT_EQUAL_STRING(language ? "English" : "Türkçe",lv_label_get_text(uiControllerInstance.submenuValueLabel));
        TEST_ASSERT_TRUE(lv_obj_get_y(uiControllerInstance.submenuValueLabel)>=16);
        TEST_ASSERT_TRUE(lv_obj_get_y(uiControllerInstance.submenuValueLabel)+lv_obj_get_height(uiControllerInstance.submenuValueLabel)<=44);
        screen_selection=UI_STATE_SET_TEMP_UNIT;
        for (unsigned unit=0;unit<2;++unit) {
            settings.system.temp_unit=unit;
            ScreenSubmenu_Update(&settings,catalog);
            lv_obj_update_layout(uiControllerInstance.submenuScreen);
            TEST_ASSERT_NULL(lv_anim_get(uiControllerInstance.submenuBreadcrumb,NULL));
            TEST_ASSERT_NULL(lv_anim_get(uiControllerInstance.submenuFooter,NULL));
            TEST_ASSERT_NULL(lv_anim_get(uiControllerInstance.submenuValueLabel,NULL));
            TEST_ASSERT_TRUE(lv_obj_is_hidden(uiControllerInstance.submenuBar));
            TEST_ASSERT_FALSE(lv_obj_is_hidden(uiControllerInstance.submenuValueLabel));
            TEST_ASSERT_EQUAL_STRING(language ? "Display unit" : "Görüntüleme Birimi",lv_label_get_text(uiControllerInstance.submenuBreadcrumb));
            TEST_ASSERT_TRUE(lv_obj_is_hidden(uiControllerInstance.submenuFooter));
            TEST_ASSERT_EQUAL_STRING("",lv_label_get_text(uiControllerInstance.submenuFooter));
            TEST_ASSERT_EQUAL_STRING(unit ? "Fahrenheit °F" : "Celsius °C",lv_label_get_text(uiControllerInstance.submenuValueLabel));
            TEST_ASSERT_EQUAL(24,lv_obj_get_y(uiControllerInstance.submenuValueLabel));
            TEST_ASSERT_EQUAL(LV_TEXT_ALIGN_CENTER,lv_obj_get_style_text_align(uiControllerInstance.submenuFooter,0));
            lv_point_t unit_size;
            lv_text_get_size(&unit_size,lv_label_get_text(uiControllerInstance.submenuValueLabel),&racool_font_10,0,0,LV_COORD_MAX,LV_TEXT_FLAG_NONE);
            TEST_ASSERT_TRUE(unit_size.x<=lv_obj_get_content_width(uiControllerInstance.submenuValueLabel));
            for (unsigned row=0;row<2;++row) {
                lv_obj_t *choice=uiControllerInstance.unitChoiceLabels[row];
                TEST_ASSERT_FALSE(lv_obj_is_hidden(choice));
                TEST_ASSERT_EQUAL(24,lv_obj_get_y(choice));
                TEST_ASSERT_EQUAL(16,lv_obj_get_height(choice));
                TEST_ASSERT_NULL(lv_anim_get(choice,NULL));
                TEST_ASSERT_TRUE(lv_color_eq(lv_color_black(),lv_obj_get_style_bg_color(choice,0)));
                TEST_ASSERT_EQUAL_STRING(row ? ">" : "<",lv_label_get_text(choice));
                TEST_ASSERT_TRUE(lv_obj_is_hidden(uiControllerInstance.unitChoiceSymbols[row]));
            }
            TEST_ASSERT_GREATER_OR_EQUAL(18,lv_obj_get_y(uiControllerInstance.submenuValueLabel));
            TEST_ASSERT_TRUE(lv_obj_get_y(uiControllerInstance.submenuValueLabel)+lv_obj_get_height(uiControllerInstance.submenuValueLabel)<=46);
        }
        settings.system.temp_unit=0;
        screen_selection=UI_STATE_SET_HIGH_TEMP;settings.control.relay_open_temp=35;
        ScreenSubmenu_Update(&settings,catalog);
        lv_obj_update_layout(uiControllerInstance.submenuScreen);
        TEST_ASSERT_EQUAL_PTR(&racool_font_14,lv_obj_get_style_text_font(uiControllerInstance.submenuValueLabel,0));
        TEST_ASSERT_FALSE(lv_obj_is_hidden(uiControllerInstance.submenuBar));
        TEST_ASSERT_FALSE(lv_obj_is_hidden(uiControllerInstance.submenuValueLabel));
        TEST_ASSERT_TRUE(lv_obj_is_hidden(uiControllerInstance.unitChoiceLabels[0]));
        TEST_ASSERT_TRUE(lv_obj_is_hidden(uiControllerInstance.unitChoiceLabels[1]));
        TEST_ASSERT_TRUE(lv_obj_get_y(uiControllerInstance.submenuValueLabel)>=16);
        TEST_ASSERT_TRUE(lv_obj_get_y(uiControllerInstance.submenuValueLabel)+lv_obj_get_height(uiControllerInstance.submenuValueLabel)<=44);
        const UIState ranges[]={UI_STATE_SET_RGB_TEMP_HOT,UI_STATE_SET_RGB_TEMP_WARM,UI_STATE_SET_RGB_TEMP_COLD,UI_STATE_SET_LOW_TEMP,UI_STATE_FAN_DELTA_OFF};
        settings.hardware.ext_led_temp_cold_min=17;settings.hardware.ext_led_temp_cold_max=19;
        settings.hardware.ext_led_temp_warm_min=35;settings.hardware.ext_led_temp_warm_max=37;
        settings.hardware.ext_led_temp_hot_min=50;settings.hardware.ext_led_temp_hot_max=52;
        settings.control.relay_close_temp=25;settings.control.relay_open_temp=35;
        settings.control.fan_delta_off_decic=-15;settings.control.fan_delta_on_decic=20;
        const int expectedLow[]={500,350,170,250,-15},expectedHigh[]={520,370,190,350,20};
        for (unsigned range=0;range<5;++range) {
            screen_selection=ranges[range];
            ScreenSubmenu_Update(&settings,catalog);lv_obj_update_layout(uiControllerInstance.submenuScreen);
            TEST_ASSERT_EQUAL(LV_BAR_MODE_RANGE,lv_bar_get_mode(uiControllerInstance.submenuBar));
            TEST_ASSERT_EQUAL(expectedLow[range],lv_bar_get_start_value(uiControllerInstance.submenuBar));
            TEST_ASSERT_EQUAL(expectedHigh[range],lv_bar_get_value(uiControllerInstance.submenuBar));
            TEST_ASSERT_EQUAL(48,lv_obj_get_y(uiControllerInstance.submenuBar));
            TEST_ASSERT_TRUE(lv_obj_is_hidden(uiControllerInstance.submenuFooter));
            for (unsigned unit=0;unit<2;++unit) {
                settings.system.temp_unit=unit;
                ScreenSubmenu_Update(&settings,catalog);lv_obj_update_layout(uiControllerInstance.submenuScreen);
                TEST_ASSERT_EQUAL_STRING(unit ? "°F" : "°C",lv_label_get_text(uiControllerInstance.submenuValueLabel));
                TEST_ASSERT_EQUAL_PTR(&racool_font_14,lv_obj_get_style_text_font(uiControllerInstance.submenuValueLabel,0));
                lv_point_t unitSize;
                lv_text_get_size(&unitSize,lv_label_get_text(uiControllerInstance.submenuValueLabel),&racool_font_14,0,0,LV_COORD_MAX,LV_TEXT_FLAG_NONE);
                TEST_ASSERT_LESS_OR_EQUAL(lv_obj_get_content_width(uiControllerInstance.submenuValueLabel),unitSize.x);
                TEST_ASSERT_LESS_OR_EQUAL(lv_obj_get_content_height(uiControllerInstance.submenuValueLabel),unitSize.y);
            }
            settings.system.temp_unit=0;ScreenSubmenu_Update(&settings,catalog);
            for (unsigned thumb=0;thumb<2;++thumb) {
                lv_obj_t *endpoint=uiControllerInstance.unitChoiceLabels[thumb];
                lv_point_t textSize;
                TEST_ASSERT_EQUAL_PTR(&racool_font_14,lv_obj_get_style_text_font(endpoint,0));
                TEST_ASSERT_EQUAL(LV_OPA_TRANSP,lv_obj_get_style_bg_opa(endpoint,0));
                TEST_ASSERT_TRUE(lv_color_eq(lv_color_white(),lv_obj_get_style_text_color(endpoint,0)));
                lv_text_get_size(&textSize,lv_label_get_text(endpoint),&racool_font_14,0,0,LV_COORD_MAX,LV_TEXT_FLAG_NONE);
                TEST_ASSERT_LESS_OR_EQUAL(lv_obj_get_content_width(endpoint),textSize.x);
                TEST_ASSERT_LESS_OR_EQUAL(lv_obj_get_content_height(endpoint),textSize.y);
                TEST_ASSERT_FALSE(lv_obj_is_hidden(uiControllerInstance.unitChoiceSymbols[thumb]));
                TEST_ASSERT_EQUAL(5,lv_obj_get_width(uiControllerInstance.unitChoiceSymbols[thumb]));
                TEST_ASSERT_EQUAL(9,lv_obj_get_height(uiControllerInstance.unitChoiceSymbols[thumb]));
            }
            TEST_ASSERT_GREATER_OR_EQUAL(7,lv_obj_get_x(uiControllerInstance.unitChoiceSymbols[1])-lv_obj_get_x(uiControllerInstance.unitChoiceSymbols[0]));
            target_confirmation=true;
            TEST_ASSERT_TRUE(MenuRangeEditor_Begin(ranges[range],&settings));
            ScreenSubmenu_Update(&settings,catalog);
            TEST_ASSERT_TRUE(lv_obj_is_hidden(uiControllerInstance.submenuFooter));
            TEST_ASSERT_FALSE(MenuRangeEditor_Advance(ranges[range]));
            ScreenSubmenu_Update(&settings,catalog);
            TEST_ASSERT_TRUE(lv_obj_is_hidden(uiControllerInstance.submenuFooter));
            TEST_ASSERT_TRUE(MenuRangeEditor_Advance(ranges[range]));
            MenuRangeEditor_Failed(ranges[range]);
            ScreenSubmenu_Update(&settings,catalog);
            TEST_ASSERT_FALSE(lv_obj_is_hidden(uiControllerInstance.submenuFooter));
            TEST_ASSERT_EQUAL(0,lv_obj_get_style_border_width(uiControllerInstance.submenuFooter,0));
            TEST_ASSERT_TRUE(lv_obj_is_hidden(uiControllerInstance.submenuBar));
            TEST_ASSERT_TRUE(MenuRangeEditor_Rotate(ranges[range],1));
            ScreenSubmenu_Update(&settings,catalog);
            TEST_ASSERT_TRUE(lv_obj_is_hidden(uiControllerInstance.submenuFooter));
            TEST_ASSERT_FALSE(lv_obj_is_hidden(uiControllerInstance.submenuBar));
            MenuRangeEditor_Clear();target_confirmation=false;
        }
        const UIState reviewedNumeric[]={
            UI_STATE_CALIBRATE_OBJECT,UI_STATE_CALIBRATE_AMBIENT,
            UI_STATE_SET_HIGH_TEMP,
            UI_STATE_SET_RGB_BRIGHTNESS,
            UI_STATE_SET_RGB_TEMP_COLD_MAX,
            UI_STATE_SET_RGB_TEMP_WARM_MAX,
            UI_STATE_SET_RGB_TEMP_HOT_MAX,UI_STATE_SET_RGB_TEMP_DANGER,
            UI_STATE_SET_RGB_TEMP_FREEZE,UI_STATE_SET_BUILTIN_BRIGHTNESS,
            UI_STATE_SET_TIMEOUT,UI_STATE_SET_TELEMETRY,
            UI_STATE_ZB_REP_FAN,UI_STATE_ZB_REP_AMBIENT,UI_STATE_ZB_REP_CPU,
            UI_STATE_ZB_REP_HEALTH,UI_STATE_ZB_REP_MEMORY,UI_STATE_ZB_REP_UPTIME,
            UI_STATE_RGB_COLD_X,UI_STATE_RGB_COLD_Y,UI_STATE_RGB_WARM_X,
            UI_STATE_RGB_WARM_Y,
            UI_STATE_RGB_HOT_X,
            UI_STATE_RGB_HOT_Y,
            UI_STATE_RGB_DANGER_X,
            UI_STATE_RGB_DANGER_Y,
            UI_STATE_RGB_FREEZE_X,
            UI_STATE_RGB_FREEZE_Y,
            UI_STATE_FAN_DELTA_ON
        };
        for (unsigned page=0;page<sizeof(reviewedNumeric)/sizeof(reviewedNumeric[0]);++page) {
            screen_selection=reviewedNumeric[page];
            ScreenSubmenu_Update(&settings,catalog);
            lv_obj_update_layout(uiControllerInstance.submenuScreen);
            TEST_ASSERT_EQUAL(41,lv_obj_get_y(uiControllerInstance.submenuBar));
            TEST_ASSERT_EQUAL(4,lv_obj_get_height(uiControllerInstance.submenuBar));
            TEST_ASSERT_TRUE(lv_obj_get_y(uiControllerInstance.submenuValueLabel)+lv_obj_get_height(uiControllerInstance.submenuValueLabel)<lv_obj_get_y(uiControllerInstance.submenuBar));
            TEST_ASSERT_TRUE(lv_obj_get_y(uiControllerInstance.submenuBar)+lv_obj_get_height(uiControllerInstance.submenuBar)<lv_obj_get_y(uiControllerInstance.submenuFooter));
            TEST_ASSERT_EQUAL(LV_TEXT_ALIGN_CENTER,lv_obj_get_style_text_align(uiControllerInstance.submenuBreadcrumb,0));
            TEST_ASSERT_EQUAL(LV_TEXT_ALIGN_CENTER,lv_obj_get_style_text_align(uiControllerInstance.submenuFooter,0));
        }
        screen_selection=UI_STATE_SYSTEM_STATUS;
        ScreenSubmenu_Update(&settings,catalog);
        lv_obj_update_layout(uiControllerInstance.submenuScreen);
        TEST_ASSERT_TRUE(lv_obj_is_hidden(uiControllerInstance.submenuBar));
        TEST_ASSERT_TRUE(lv_obj_is_hidden(uiControllerInstance.unitChoiceLabels[0]));
        TEST_ASSERT_TRUE(lv_obj_is_hidden(uiControllerInstance.unitChoiceLabels[1]));
        TEST_ASSERT_EQUAL(24,lv_obj_get_y(uiControllerInstance.submenuValueLabel));
        TEST_ASSERT_EQUAL(LV_TEXT_ALIGN_CENTER,lv_obj_get_style_text_align(uiControllerInstance.submenuBreadcrumb,0));
        TEST_ASSERT_EQUAL(LV_TEXT_ALIGN_CENTER,lv_obj_get_style_text_align(uiControllerInstance.submenuFooter,0));
        TEST_ASSERT_EQUAL_STRING(language ? "Back" : "Geri",lv_label_get_text(uiControllerInstance.submenuFooter));
        screen_selection=UI_STATE_FAN_MODE;
        ControlConfigs savedControl=settings.control;
        const LocalizationMessage modeMessages[]={LOC_CLIMATE_OBJECT_SENSITIVE,
            LOC_CLIMATE_AMBIENT_SENSITIVE,LOC_CLIMATE_ALWAYS_ON,LOC_COMMON_OFF};
        for (unsigned mode=0;mode<4;++mode) {
            settings.control.fan_manual_mode=mode>=2;
            settings.control.fan_manual_state=mode==2;
            settings.control.fan_strategy=mode==1;
            ScreenSubmenu_Update(&settings,catalog);
            lv_obj_update_layout(uiControllerInstance.submenuScreen);
            TEST_ASSERT_TRUE(lv_obj_is_hidden(uiControllerInstance.submenuBar));
            TEST_ASSERT_EQUAL_STRING(Localization_Text(catalog,modeMessages[mode]),lv_label_get_text(uiControllerInstance.submenuValueLabel));
            TEST_ASSERT_TRUE(lv_obj_is_hidden(uiControllerInstance.submenuFooter));
            TEST_ASSERT_EQUAL_STRING("",lv_label_get_text(uiControllerInstance.submenuFooter));
            TEST_ASSERT_EQUAL(24,lv_obj_get_y(uiControllerInstance.submenuValueLabel));
            TEST_ASSERT_EQUAL(96,lv_obj_get_width(uiControllerInstance.submenuValueLabel));
            TEST_ASSERT_EQUAL(LV_TEXT_ALIGN_CENTER,lv_obj_get_style_text_align(uiControllerInstance.submenuBreadcrumb,0));
            TEST_ASSERT_EQUAL(LV_TEXT_ALIGN_CENTER,lv_obj_get_style_text_align(uiControllerInstance.submenuFooter,0));
            lv_point_t size;
            lv_text_get_size(&size,lv_label_get_text(uiControllerInstance.submenuValueLabel),&racool_font_10,0,0,LV_COORD_MAX,LV_TEXT_FLAG_NONE);
            TEST_ASSERT_TRUE(size.x<=96);
            TEST_ASSERT_NULL(lv_anim_get(uiControllerInstance.submenuValueLabel,NULL));
            TEST_ASSERT_NULL(lv_anim_get(uiControllerInstance.submenuBreadcrumb,NULL));
            for (unsigned direction=0;direction<2;++direction) {
                TEST_ASSERT_FALSE(lv_obj_is_hidden(uiControllerInstance.unitChoiceLabels[direction]));
                TEST_ASSERT_EQUAL_STRING(direction ? ">" : "<",lv_label_get_text(uiControllerInstance.unitChoiceLabels[direction]));
                TEST_ASSERT_EQUAL(24,lv_obj_get_y(uiControllerInstance.unitChoiceLabels[direction]));
            }
        }
        settings.control=savedControl;
        screen_selection=UI_STATE_SET_LED_MODE;
        uint8_t savedLedMode=settings.hardware.ext_led_mode;
        const LocalizationMessage ledModes[]={LOC_MENU_TEMPERATURE_DRIVEN,LOC_COMMON_ANIMATION,LOC_COMMON_OFF};
        for (unsigned mode=0;mode<EXT_LED_MODE_COUNT;++mode) {
            settings.hardware.ext_led_mode=mode;
            ScreenSubmenu_Update(&settings,catalog);
            lv_obj_update_layout(uiControllerInstance.submenuScreen);
            TEST_ASSERT_EQUAL_STRING(Localization_Text(catalog,ledModes[mode]),lv_label_get_text(uiControllerInstance.submenuValueLabel));
            TEST_ASSERT_TRUE(lv_obj_is_hidden(uiControllerInstance.submenuBar));
            TEST_ASSERT_FALSE(lv_obj_is_hidden(uiControllerInstance.unitChoiceLabels[0]));
            TEST_ASSERT_FALSE(lv_obj_is_hidden(uiControllerInstance.unitChoiceLabels[1]));
            TEST_ASSERT_EQUAL(24,lv_obj_get_y(uiControllerInstance.submenuValueLabel));
            TEST_ASSERT_EQUAL(LV_TEXT_ALIGN_CENTER,lv_obj_get_style_text_align(uiControllerInstance.submenuFooter,0));
        }
        settings.hardware.ext_led_mode=savedLedMode;
        screen_selection=UI_STATE_SET_LED_ANIM;
        uint8_t savedExternalAnimation=settings.hardware.ext_led_anim;
        size_t animationCount;
        const LEDAnimationCatalogEntry *animations=LEDAnimationCatalog_Entries(&animationCount);
        for (size_t animation=0;animation<animationCount;++animation) {
            settings.hardware.ext_led_anim=animations[animation].external_id;
            ScreenSubmenu_Update(&settings,catalog);
            lv_obj_update_layout(uiControllerInstance.submenuScreen);
            const char *label=UITranslation_Animation(animations[animation].external_id,false,catalog);
            TEST_ASSERT_EQUAL_STRING(label,lv_label_get_text(uiControllerInstance.submenuValueLabel));
            lv_point_t textSize;
            lv_text_get_size(&textSize,label,&racool_font_10,0,0,LV_COORD_MAX,LV_TEXT_FLAG_NONE);
            TEST_ASSERT_EQUAL(textSize.x>96 ? LV_LABEL_LONG_MODE_SCROLL_CIRCULAR : LV_LABEL_LONG_MODE_CLIP,
                              lv_label_get_long_mode(uiControllerInstance.submenuValueLabel));
            TEST_ASSERT_EQUAL(24,lv_obj_get_y(uiControllerInstance.submenuValueLabel));
            TEST_ASSERT_EQUAL(16,lv_obj_get_height(uiControllerInstance.submenuValueLabel));
            TEST_ASSERT_TRUE(lv_obj_is_hidden(uiControllerInstance.submenuBar));
            TEST_ASSERT_FALSE(lv_obj_is_hidden(uiControllerInstance.unitChoiceLabels[0]));
            TEST_ASSERT_FALSE(lv_obj_is_hidden(uiControllerInstance.unitChoiceLabels[1]));
        }
        settings.hardware.ext_led_anim=savedExternalAnimation;
        const UIState builtinAnimationPages[]={UI_STATE_SET_BUILTIN_IDLE,UI_STATE_SET_BUILTIN_CONNECTING,UI_STATE_SET_BUILTIN_FAULT,UI_STATE_SET_BUILTIN_STARTUP};
        HardwareConfigs savedHardware=settings.hardware;
        for (unsigned page=0;page<sizeof(builtinAnimationPages)/sizeof(builtinAnimationPages[0]);++page) {
            screen_selection=builtinAnimationPages[page];
            for (size_t animation=0;animation<animationCount;++animation) {
                settings.hardware.builtin_led_idle_anim=animations[animation].builtin_id;
                settings.hardware.builtin_led_connecting_anim=animations[animation].builtin_id;
                settings.hardware.builtin_led_fault_anim=animations[animation].builtin_id;
                settings.hardware.builtin_led_startup_anim=animations[animation].builtin_id;
                ScreenSubmenu_Update(&settings,catalog);
                lv_obj_update_layout(uiControllerInstance.submenuScreen);
                const char *label=UITranslation_Animation(animations[animation].builtin_id,true,catalog);
                TEST_ASSERT_EQUAL_STRING(label,lv_label_get_text(uiControllerInstance.submenuValueLabel));
                lv_point_t textSize;
                lv_text_get_size(&textSize,label,&racool_font_10,0,0,LV_COORD_MAX,LV_TEXT_FLAG_NONE);
                TEST_ASSERT_EQUAL(textSize.x>96 ? LV_LABEL_LONG_MODE_SCROLL_CIRCULAR : LV_LABEL_LONG_MODE_CLIP,
                                  lv_label_get_long_mode(uiControllerInstance.submenuValueLabel));
                TEST_ASSERT_EQUAL(24,lv_obj_get_y(uiControllerInstance.submenuValueLabel));
                TEST_ASSERT_EQUAL(16,lv_obj_get_height(uiControllerInstance.submenuValueLabel));
                TEST_ASSERT_TRUE(lv_obj_is_hidden(uiControllerInstance.submenuBar));
                TEST_ASSERT_FALSE(lv_obj_is_hidden(uiControllerInstance.unitChoiceLabels[0]));
                TEST_ASSERT_FALSE(lv_obj_is_hidden(uiControllerInstance.unitChoiceLabels[1]));
            }
        }
        settings.hardware=savedHardware;
        const UIState confirmationPages[]={UI_STATE_FACTORY_RESET,UI_STATE_RESTART,UI_STATE_SHUTDOWN};
        for (unsigned page=0;page<sizeof(confirmationPages)/sizeof(confirmationPages[0]);++page) {
            screen_selection=confirmationPages[page];
            for (unsigned choice=0;choice<2;++choice) {
                target_confirmation=choice!=0;
                ScreenSubmenu_Update(&settings,catalog);
                lv_obj_update_layout(uiControllerInstance.submenuScreen);
                TEST_ASSERT_EQUAL_STRING(Localization_Text(catalog,choice ? LOC_COMMON_YES : LOC_COMMON_NO),
                                         lv_label_get_text(uiControllerInstance.submenuValueLabel));
                TEST_ASSERT_EQUAL_STRING(Localization_Text(catalog,LOC_EDITOR_CONFIRM),lv_label_get_text(uiControllerInstance.submenuFooter));
                TEST_ASSERT_EQUAL(24,lv_obj_get_y(uiControllerInstance.submenuValueLabel));
                TEST_ASSERT_EQUAL(LV_TEXT_ALIGN_CENTER,lv_obj_get_style_text_align(uiControllerInstance.submenuFooter,0));
                TEST_ASSERT_TRUE(lv_obj_is_hidden(uiControllerInstance.submenuBar));
                TEST_ASSERT_FALSE(lv_obj_is_hidden(uiControllerInstance.unitChoiceLabels[0]));
                TEST_ASSERT_FALSE(lv_obj_is_hidden(uiControllerInstance.unitChoiceLabels[1]));
            }
        }
        target_confirmation=false;
        screen_selection=UI_STATE_SET_ZIGBEE_TX_PROFILE;
        uint8_t savedTxProfile=settings.system.zigbee_tx_profile;
        const LocalizationMessage txMessages[]={LOC_RADIO_ECO,LOC_RADIO_NORMAL,LOC_RADIO_BOOST};
        for (unsigned profile=0;profile<3;++profile) {
            settings.system.zigbee_tx_profile=profile;
            ScreenSubmenu_Update(&settings,catalog);
            lv_obj_update_layout(uiControllerInstance.submenuScreen);
            TEST_ASSERT_EQUAL_STRING(Localization_Text(catalog,txMessages[profile]),lv_label_get_text(uiControllerInstance.submenuValueLabel));
            TEST_ASSERT_TRUE(lv_obj_is_hidden(uiControllerInstance.submenuBar));
            TEST_ASSERT_FALSE(lv_obj_is_hidden(uiControllerInstance.unitChoiceLabels[0]));
            TEST_ASSERT_FALSE(lv_obj_is_hidden(uiControllerInstance.unitChoiceLabels[1]));
            TEST_ASSERT_EQUAL(24,lv_obj_get_y(uiControllerInstance.submenuValueLabel));
            TEST_ASSERT_EQUAL(LV_TEXT_ALIGN_CENTER,lv_obj_get_style_text_align(uiControllerInstance.submenuFooter,0));
        }
        settings.system.zigbee_tx_profile=savedTxProfile;
        screen_selection=UI_STATE_SET_POWER_PROFILE;
        uint8_t savedPowerProfile=settings.system.power_profile;
        const LocalizationMessage powerMessages[]={LOC_POWER_PERFORMANCE,LOC_POWER_BALANCED,LOC_POWER_ECONOMY};
        for (unsigned profile=0;profile<3;++profile) {
            settings.system.power_profile=profile;
            ScreenSubmenu_Update(&settings,catalog);
            lv_obj_update_layout(uiControllerInstance.submenuScreen);
            TEST_ASSERT_EQUAL_STRING(Localization_Text(catalog,powerMessages[profile]),lv_label_get_text(uiControllerInstance.submenuValueLabel));
            TEST_ASSERT_TRUE(lv_obj_is_hidden(uiControllerInstance.submenuBar));
            TEST_ASSERT_FALSE(lv_obj_is_hidden(uiControllerInstance.unitChoiceLabels[0]));
            TEST_ASSERT_FALSE(lv_obj_is_hidden(uiControllerInstance.unitChoiceLabels[1]));
            TEST_ASSERT_EQUAL(24,lv_obj_get_y(uiControllerInstance.submenuValueLabel));
            TEST_ASSERT_EQUAL(LV_TEXT_ALIGN_CENTER,lv_obj_get_style_text_align(uiControllerInstance.submenuFooter,0));
        }
        settings.system.power_profile=savedPowerProfile;
        screen_selection=UI_STATE_SET_LANGUAGE;
        ScreenSubmenu_Update(&settings,catalog);
        lv_obj_update_layout(uiControllerInstance.submenuScreen);
        TEST_ASSERT_EQUAL_STRING(catalog->name,lv_label_get_text(uiControllerInstance.submenuValueLabel));
        TEST_ASSERT_TRUE(lv_obj_is_hidden(uiControllerInstance.submenuBar));
        TEST_ASSERT_FALSE(lv_obj_is_hidden(uiControllerInstance.unitChoiceLabels[0]));
        TEST_ASSERT_FALSE(lv_obj_is_hidden(uiControllerInstance.unitChoiceLabels[1]));
        TEST_ASSERT_EQUAL(24,lv_obj_get_y(uiControllerInstance.submenuValueLabel));
        TEST_ASSERT_EQUAL(LV_TEXT_ALIGN_CENTER,lv_obj_get_style_text_align(uiControllerInstance.submenuFooter,0));
        const UIState colorPages[]={UI_STATE_SET_RGB_COLD,UI_STATE_SET_RGB_WARM,UI_STATE_SET_RGB_HOT,UI_STATE_SET_RGB_DANGER,UI_STATE_SET_RGB_FREEZE};
        for (unsigned page=0;page<sizeof(colorPages)/sizeof(colorPages[0]);++page) {
            screen_selection=colorPages[page];
            HardwareConfigs savedColors=settings.hardware;
            uint16_t savedColdX=settings.hardware.ext_led_color_cold_x;
            uint16_t savedColdY=settings.hardware.ext_led_color_cold_y;
            uint16_t savedWarmX=settings.hardware.ext_led_color_warm_x;
            uint16_t savedWarmY=settings.hardware.ext_led_color_warm_y;
            uint16_t savedHotX=settings.hardware.ext_led_color_hot_x;
            uint16_t savedHotY=settings.hardware.ext_led_color_hot_y;
            for (unsigned color=0;color<RGB_COLOR_COUNT;++color) {
                uint16_t x,y;
                RGBLEDColorEngine_PresetToXY((PresetColorIndex)color,&x,&y);
                settings.hardware.ext_led_color_cold_x=x;settings.hardware.ext_led_color_cold_y=y;
                settings.hardware.ext_led_color_warm_x=x;settings.hardware.ext_led_color_warm_y=y;
                settings.hardware.ext_led_color_hot_x=x;settings.hardware.ext_led_color_hot_y=y;
                settings.hardware.ext_led_color_danger_x=x;settings.hardware.ext_led_color_danger_y=y;
                settings.hardware.ext_led_color_freeze_x=x;settings.hardware.ext_led_color_freeze_y=y;
                ScreenSubmenu_Update(&settings,catalog);
                lv_obj_update_layout(uiControllerInstance.submenuScreen);
                TEST_ASSERT_EQUAL_STRING(UITranslation_Color(color,catalog),lv_label_get_text(uiControllerInstance.submenuValueLabel));
                TEST_ASSERT_TRUE(lv_obj_is_hidden(uiControllerInstance.submenuBar));
                TEST_ASSERT_FALSE(lv_obj_is_hidden(uiControllerInstance.unitChoiceLabels[0]));
                TEST_ASSERT_EQUAL(LV_TEXT_ALIGN_CENTER,lv_obj_get_style_text_align(uiControllerInstance.submenuFooter,0));
            }
            settings.hardware=savedColors;
            settings.hardware.ext_led_color_cold_x=savedColdX;
            settings.hardware.ext_led_color_cold_y=savedColdY;
            settings.hardware.ext_led_color_warm_x=savedWarmX;
            settings.hardware.ext_led_color_warm_y=savedWarmY;
            settings.hardware.ext_led_color_hot_x=savedHotX;
            settings.hardware.ext_led_color_hot_y=savedHotY;
        }
        screen_selection=UI_STATE_RESTART;
        ScreenSubmenu_Update(&settings,catalog);
        TEST_ASSERT_TRUE(lv_obj_is_hidden(uiControllerInstance.submenuBar));
        TEST_ASSERT_TRUE(lv_obj_is_hidden(uiControllerInstance.submenuUnitLabel));
        TEST_ASSERT_EQUAL_STRING(language ? "Confirm" : "Onayla",lv_label_get_text(uiControllerInstance.submenuFooter));
        ScreenZigbeePair_Update(1000,false,false,false,catalog);
        lv_obj_update_layout(uiControllerInstance.zigbeePairScreen);
        TEST_ASSERT_EQUAL_STRING(language ? "Ready to pair" : "Eşleşmeye hazır",lv_label_get_text(uiControllerInstance.zbStatusLabel));
        TEST_ASSERT_TRUE(lv_obj_get_y(uiControllerInstance.zbInfoLabel)>=32);
        TEST_ASSERT_TRUE(lv_obj_get_y(uiControllerInstance.zbInfoLabel)+lv_obj_get_height(uiControllerInstance.zbInfoLabel)<=48);
        TEST_ASSERT_TRUE(lv_obj_get_y(uiControllerInstance.zbBackLabel)>=0);
        TEST_ASSERT_TRUE(lv_obj_get_y(uiControllerInstance.zbBackLabel)+lv_obj_get_height(uiControllerInstance.zbBackLabel)<=16);
        TEST_ASSERT_FALSE(lv_obj_is_scrollable(uiControllerInstance.zigbeePairScreen));
        TEST_ASSERT_EQUAL(LV_TEXT_ALIGN_CENTER,lv_obj_get_style_text_align(uiControllerInstance.zbBreadcrumb,0));
        TEST_ASSERT_EQUAL(LV_TEXT_ALIGN_CENTER,lv_obj_get_style_text_align(uiControllerInstance.zbStatusLabel,0));
        ScreenZigbeePair_Update(1000,false,true,false,catalog);
        lv_obj_update_layout(uiControllerInstance.zigbeePairScreen);
        TEST_ASSERT_EQUAL_STRING(Localization_Text(catalog,LOC_PAIRING_NETWORK_CONNECTED),lv_label_get_text(uiControllerInstance.zbInfoLabel));
        TEST_ASSERT_EQUAL_STRING(language ? "Pair" : "Eşle",lv_label_get_text(uiControllerInstance.zbActionLabel));
        lv_point_t actionSize;
        lv_text_get_size(&actionSize,lv_label_get_text(uiControllerInstance.zbActionLabel),&racool_font_10,0,0,LV_COORD_MAX,LV_TEXT_FLAG_NONE);
        TEST_ASSERT_TRUE(actionSize.x<=lv_obj_get_content_width(uiControllerInstance.zbActionBtn));
        TEST_ASSERT_TRUE(actionSize.y<=lv_obj_get_content_height(uiControllerInstance.zbActionBtn));
        TEST_ASSERT_TRUE(lv_obj_is_hidden(uiControllerInstance.zbCountdownLabel));
        ScreenZigbeePair_Update(1000,true,false,false,catalog);
        lv_obj_update_layout(uiControllerInstance.zigbeePairScreen);
        TEST_ASSERT_TRUE(lv_obj_is_hidden(uiControllerInstance.zbInfoLabel));
        TEST_ASSERT_FALSE(lv_obj_is_hidden(uiControllerInstance.zbCountdownLabel));
        TEST_ASSERT_TRUE(lv_obj_is_hidden(uiControllerInstance.zbBackBtn));
        TEST_ASSERT_EQUAL(126,lv_obj_get_width(uiControllerInstance.zbActionBtn));
        TEST_ASSERT_EQUAL_STRING(Localization_Text(catalog,LOC_PAIRING_CANCEL),lv_label_get_text(uiControllerInstance.zbActionLabel));
        ScreenZigbeePair_Update(181001,true,false,false,catalog);
        ScreenZigbeePair_Update(181002,false,false,false,catalog);
        TEST_ASSERT_EQUAL_STRING(Localization_Text(catalog,LOC_PAIRING_TIMEOUT),lv_label_get_text(uiControllerInstance.zbStatusLabel));
        ScreenZigbeePair_Update(181003,false,true,true,catalog);
        TEST_ASSERT_EQUAL(LV_OPA_COVER,lv_obj_get_style_bg_opa(uiControllerInstance.zbBackBtn,0));
        TEST_ASSERT_EQUAL(lv_color_to_u32(lv_color_white()),lv_color_to_u32(lv_obj_get_style_bg_color(uiControllerInstance.zbBackBtn,0)));
    }
    screen_selection=UI_STATE_MONITOR;
    uiControllerInstance.isScreenOn=false;uiControllerInstance.wakeNavigationPending=false;
    settings.system.temp_unit=0;UIRenderer_Invalidate();
    UIRenderer_Update(100000,&settings,25,24,30);
    TEST_ASSERT_EQUAL_STRING("25.0°C",lv_label_get_text(uiControllerInstance.tempLabel));
    settings.system.temp_unit=1;
    /* Real LVGL labels must change even without a configuration notification. */
    UIRenderer_Update(100001,&settings,25,24,30);
    TEST_ASSERT_EQUAL_STRING("77.0°F",lv_label_get_text(uiControllerInstance.tempLabel));
    TEST_ASSERT_FALSE(uiControllerInstance.isScreenOn);
    TEST_ASSERT_FALSE(screen_lock);
    settings.system.temp_unit=0;
    for (unsigned mode = 0; mode < 4; ++mode) {
        ScreenMonitor_Update(0, &settings, 25, 24, 30, false, (ClimateFanMode)mode, true, 3, Localization_Snapshot());
        TEST_ASSERT_EQUAL_PTR(&fan_mode_images[mode], lv_image_get_src(uiControllerInstance.fanIcon));
        lv_obj_update_layout(uiControllerInstance.mainScreen);
        TEST_ASSERT_EQUAL(11, lv_obj_get_width(uiControllerInstance.fanIcon));
        TEST_ASSERT_EQUAL(11, lv_obj_get_height(uiControllerInstance.fanIcon));
        TEST_ASSERT_TRUE(lv_color_eq(lv_color_white(), lv_obj_get_style_image_recolor(uiControllerInstance.fanIcon, 0)));
        const lv_image_dsc_t *image = lv_image_get_src(uiControllerInstance.fanIcon);
        int left=11, top=11, right=-1, bottom=-1;
        for (int y=0;y<11;++y) for(int x=0;x<11;++x) if(image->data[y*11+x]) {
            if(x<left) left=x;
            if(x>right) right=x;
            if(y<top) top=y;
            if(y>bottom) bottom=y;
        }
        for (unsigned bars=0; bars<=4; ++bars) {
            ScreenMonitor_Update(0, &settings, 25, 24, 30, bars%2, (ClimateFanMode)mode, bars%2, bars, Localization_Snapshot());
            lv_obj_update_layout(uiControllerInstance.mainScreen);
            verify_status_ink(uiControllerInstance.fanIcon,1,left,top,right-left+1,bottom-top+1);
            verify_status_ink(uiControllerInstance.relayIcon,2,0,0,10,10);
            verify_status_ink(uiControllerInstance.zigbeeIconClip,0,bars>=3?0:bars==2?2:5,
                              bars>=3?1:0,bars>=3?12:bars==2?8:3,bars>=3?9:bars==2?6:3);
        }
    }
    /* Restore the original active screen before the ownership failure loop. */
    unsigned total = creation_allocations;
    TEST_ASSERT_GREATER_THAN(40, total);
    lv_screen_load(original_screen);
    lv_group_set_default(original_group);
    UIRenderer_DiscardPartialUI();
    for (unsigned fail = 1; fail <= total; ++fail) {
        allocations = boot_starts = 0;
        fail_at = fail;
        TEST_ASSERT_FALSE(UIRenderer_CreateUI());
        TEST_ASSERT_EQUAL(0, live_objects);
        TEST_ASSERT_EQUAL(0, live_groups);
        TEST_ASSERT_EQUAL(0, boot_starts);
        TEST_ASSERT_FALSE(screen_lock);
        TEST_ASSERT_EQUAL_PTR(original_screen, lv_screen_active());
        TEST_ASSERT_EQUAL_PTR(original_group, lv_group_get_default());
        fail_at = 0;
        TEST_ASSERT_TRUE(UIRenderer_CreateUI());
        TEST_ASSERT_EQUAL(1, boot_starts);
        lv_screen_load(original_screen);
        lv_group_set_default(original_group);
        UIRenderer_DiscardPartialUI();
    }
    printf("UI_SCREEN allocation failure points=%u; LVGL %d.%d.%d\n", total,
           LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH);
    target_ui_screen_cleanup();
    TEST_ASSERT_EQUAL(0, live_objects);
    TEST_ASSERT_EQUAL(0, live_groups);
}
