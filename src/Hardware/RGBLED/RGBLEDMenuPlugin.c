#include "Controllers/MenuManager/MenuEditSession.h"
#include "Controllers/MenuManager/MenuRangeEditor.h"
#include "Hardware/RGBLED/LEDBrightnessScale.h"
#include "Hardware/RGBLED/RGBLEDMenuPlugin.h"
#include "Hardware/RGBLED/IRGBLED.h"
#include "Hardware/RGBLED/RGBLEDColorEngine.h"
#include "Hardware/BuiltinLED/BuiltinLEDConfig.h"
#include "Hardware/BuiltinLED/IBuiltinLED.h"
#include "Hardware/LEDAnimations/LEDAnimationCatalog.h"
#include "Hardware/LEDAnimations/LEDPreviewConfig.h"
#include "Core/DI/IDIRegistry.h"
#include "Core/ServiceLocator/ServiceLocator.h"
#include "Core/ServiceLocator/ServiceNames.h"
#include "Core/EventBus/IEventBus.h"
#include "Controllers/MenuManager/IMenuManager.h"
#include "Controllers/MenuManager/MenuManagerEvents.h"
#include "Management/ConfigurationManager/IConfigurationManager.h"
#include "Management/ConfigurationManager/SystemConfigs.h"
#include <esp_log.h>
#include <string.h>
#include "Hardware/RGBLED/RGBZoneSelection.h"

static const char *TAG = "RGBLEDMenuPlugin";
static IMenuManager *menuManager = NULL;
static bool initialized = false;

/* ──────────────────────────────────────────
 * Menu Callback Logic
 * ────────────────────────────────────────── */
static bool IsAnimationSelection(UIState state) {
    return state==UI_STATE_SET_LED_ANIM || state==UI_STATE_SET_BUILTIN_STARTUP ||
        state==UI_STATE_SET_BUILTIN_IDLE || state==UI_STATE_SET_BUILTIN_CONNECTING || state==UI_STATE_SET_BUILTIN_FAULT;
}
static void PreviewSelectedAnimation(UIState state,const HardwareConfigs *hardware) {
    if (!IsAnimationSelection(state)) return;
    esp_err_t result=ESP_ERR_INVALID_STATE;
    if (state==UI_STATE_SET_LED_ANIM) {
        IRGBLED *rgb=GetRGBLEDService();
        if (rgb && rgb->PreviewAnimation) result=rgb->PreviewAnimation(rgb,(RGBLEDAnimType)hardware->ext_led_startup_anim,LED_ANIMATION_PREVIEW_MS);
    } else {
        IBuiltinLED *led=(IBuiltinLED *)ServiceLocator_GetService(SVC_NAME_BUILTIN_LED);
        uint8_t type=state==UI_STATE_SET_BUILTIN_STARTUP ? hardware->builtin_led_startup_anim :
            state==UI_STATE_SET_BUILTIN_IDLE ? hardware->builtin_led_idle_anim :
            state==UI_STATE_SET_BUILTIN_CONNECTING ? hardware->builtin_led_connecting_anim : hardware->builtin_led_fault_anim;
        if (led && led->PreviewAnimation) result=led->PreviewAnimation(led,(BuiltinLEDAnimationType)type,LED_ANIMATION_PREVIEW_MS);
    }
    if (result!=ESP_OK) ESP_LOGE(TAG,"Animation preview rejected: 0x%x",(unsigned)result);
}

static void OnRGBLEDMenuAction(UIState state, int8_t direction, bool isButtonPressed) {
    if (state == UI_STATE_SET_BUILTIN_MODE) {
        if (isButtonPressed || !direction) return;
        IConfigurationManager *config=MenuEditSession_Config(ConfigurationManager_GetInstance());
        SystemSettings settings;
        if (!config || !config->GetSnapshot || !config->UpdateFields || config->GetSnapshot(config,&settings)!=ESP_OK) return;
        uint8_t mode=settings.hardware.builtin_led_anim_mode ? 0 : 1;
        ConfigurationFieldUpdate field={offsetof(HardwareConfigs,builtin_led_anim_mode),&mode,sizeof(mode)};
        esp_err_t result=config->UpdateFields(config,"hardware",&field,1,true);
        if(result!=ESP_OK) ESP_LOGE(TAG,"Built-in LED mode save failed: %d",result);
        return;
    }
    if (state == UI_STATE_SET_BUILTIN_BRIGHTNESS) {
        if (isButtonPressed || direction == 0) return;
        IConfigurationManager *config = MenuEditSession_Config(ConfigurationManager_GetInstance());
        SystemSettings snapshot;
        if (!config || !config->GetSnapshot || !config->UpdateFields ||
            config->GetSnapshot(config, &snapshot) != ESP_OK) return;
        uint8_t brightness = LEDBrightness_Step(snapshot.hardware.builtin_led_brightness, direction, 255);
        if (brightness == snapshot.hardware.builtin_led_brightness) return;
        ConfigurationFieldUpdate field = {offsetof(HardwareConfigs, builtin_led_brightness), &brightness, sizeof(brightness)};
        esp_err_t result = config->UpdateFields(config, "hardware", &field, 1, true);
        if (result != ESP_OK) ESP_LOGE(TAG, "Internal brightness save failed: 0x%x", (unsigned)result);
        else if (MenuEditSession_State()==state) {
            IBuiltinLED *led=(IBuiltinLED *)ServiceLocator_GetService(SVC_NAME_BUILTIN_LED);
            result=led && led->PreviewBrightness ? led->PreviewBrightness(led,brightness,LED_BRIGHTNESS_PREVIEW_MS) : ESP_ERR_INVALID_STATE;
            if(result!=ESP_OK) ESP_LOGE(TAG,"Internal brightness preview rejected: 0x%x",(unsigned)result);
        }
        return;
    }
    IConfigurationManager *configManager = MenuEditSession_Config(ConfigurationManager_GetInstance());
    if (!configManager) return;
    SystemSettings snapshot;
    if (!configManager->GetSnapshot || !configManager->UpdateFields ||
        configManager->GetSnapshot(configManager, &snapshot) != ESP_OK) return;
    if (MenuRangeEditor_Zone(state)>=0) {
        if (!isButtonPressed && !direction) { MenuRangeEditor_Begin(state,&snapshot); if (menuManager && menuManager->UpdateActivity) menuManager->UpdateActivity(menuManager); return; }
        if (!isButtonPressed) { MenuRangeEditor_Rotate(state,direction); if (menuManager && menuManager->UpdateActivity) menuManager->UpdateActivity(menuManager); return; }
        if (!MenuRangeEditor_Advance(state)) { if (menuManager && menuManager->UpdateActivity) menuManager->UpdateActivity(menuManager); return; }
        int16_t draftLow,draftHigh; unsigned phase;
        float oldLow,oldHigh; size_t lowOffset,highOffset;
        if (!MenuRangeEditor_Read(state,&draftLow,&draftHigh,&phase) ||
            !MenuRangeEditor_Values(state,&snapshot,&oldLow,&oldHigh,&lowOffset,&highOffset)) return;
        if (!MenuRangeEditor_Unchanged(state,&snapshot)) {
            MenuRangeEditor_Failed(state); if (menuManager && menuManager->UpdateActivity) menuManager->UpdateActivity(menuManager); ESP_LOGE(TAG,"Range changed externally; refusing stale draft"); return;
        }
        float low=draftLow/10.0f, high=draftHigh/10.0f;
        ConfigurationFieldUpdate fields[]={{lowOffset,&low,sizeof(low)},{highOffset,&high,sizeof(high)}};
        esp_err_t result=configManager->UpdateFields(configManager,"hardware",fields,2,true);
        if (result!=ESP_OK) { MenuRangeEditor_Failed(state); if (menuManager && menuManager->UpdateActivity) menuManager->UpdateActivity(menuManager); ESP_LOGE(TAG,"Range save failed: 0x%x",(unsigned)result); return; }
        MenuRangeEditor_Clear();
        if (menuManager && menuManager->SetEditing) menuManager->SetEditing(menuManager,false);
        IEventBus *bus=ServiceLocator_GetEventBus();
        if (bus && bus->Publish) bus->Publish((BusEvent){.eventName=EVENT_MENU_EDITING_FINISHED});
        return;
    }
    int zone = RGBZoneSelection_Zone(state);
    if (zone >= 0) {
        uint16_t x,y;
        size_t ox,oy;
        if (!SystemConfigs_RGBZoneOffsets((unsigned)zone,&ox,&oy) ||
            !SystemConfigs_GetRGBZoneXY(&snapshot.hardware,(unsigned)zone,&x,&y)) return;
        if (isButtonPressed) {
            if (!RGBZoneSelection_Read(state,&x,&y)) return;
            ConfigurationFieldUpdate fields[]={{ox,&x,sizeof(x)},{oy,&y,sizeof(y)}};
            esp_err_t result=configManager->UpdateFields(configManager,"hardware",fields,2,true);
            if (result!=ESP_OK) ESP_LOGE(TAG,"Zone color save failed: 0x%x",(unsigned)result);
            else {
                IRGBLED *rgb=GetRGBLEDService();
                int preset=RGBLEDColorEngine_XYtoExactPreset(x,y);
                RGBColor color=preset<0 ? RGBLEDColorEngine_XYtoRGB(x,y) : RGBLEDColorEngine_GetPresetColor((PresetColorIndex)preset);
                esp_err_t preview=rgb && rgb->PreviewColor ? rgb->PreviewColor(rgb,color,RGBLED_COLOR_PREVIEW_MS) : ESP_ERR_INVALID_STATE;
                if (preview!=ESP_OK) ESP_LOGE(TAG,"Saved color preview rejected: 0x%x",(unsigned)preview);
            }
            if(result==ESP_OK)RGBZoneSelection_Clear();
            if(result==ESP_OK && menuManager && menuManager->SetEditing) {
                menuManager->SetEditing(menuManager,false);
                IEventBus *bus=ServiceLocator_GetEventBus();
                if(bus && bus->Publish)bus->Publish((BusEvent){.eventName=EVENT_MENU_EDITING_FINISHED});
            }
        } else if (!direction) {
            RGBZoneSelection_Begin(state,x,y);
        } else {
            if (!RGBZoneSelection_Read(state,&x,&y)) RGBZoneSelection_Begin(state,x,y);
            if (!RGBZoneSelection_Rotate(state,direction,&x,&y)) return;
            int next=RGBLEDColorEngine_XYtoExactPreset(x,y);
            IRGBLED *rgb=GetRGBLEDService();
            esp_err_t result=rgb && rgb->PreviewColor ? rgb->PreviewColor(rgb,next<0 ? RGBLEDColorEngine_XYtoRGB(x,y) : RGBLEDColorEngine_GetPresetColor((PresetColorIndex)next),RGBLED_COLOR_PREVIEW_MS) : ESP_ERR_INVALID_STATE;
            if (result!=ESP_OK) ESP_LOGE(TAG,"Color preview rejected: 0x%x",(unsigned)result);
        }
        return;
    }
    HardwareConfigs original = snapshot.hardware;
    SystemSettings *settings = &snapshot;

    if (isButtonPressed) {
        if(state==UI_STATE_SET_LED_MODE) {
            IRGBLED *rgb=GetRGBLEDService();
            esp_err_t result=rgb && rgb->ApplySelectedMode ? rgb->ApplySelectedMode(rgb) : ESP_ERR_INVALID_STATE;
            MenuEditSession_ReportResult(state,result);
        }
        PreviewSelectedAnimation(state,&snapshot.hardware);
        return; 
    }
    if (state>=UI_STATE_RGB_COLD_X && state<=UI_STATE_RGB_FREEZE_Y) {
        if (!direction) return;
        unsigned coordinate=(unsigned)(state-UI_STATE_RGB_COLD_X);
        size_t offsetX,offsetY;
        uint16_t x,y;
        if (!SystemConfigs_RGBZoneOffsets(coordinate/2,&offsetX,&offsetY) ||
            !SystemConfigs_GetRGBZoneXY(&original,coordinate/2,&x,&y)) return;
        bool changeY=(coordinate&1u)!=0;
        int next=(changeY ? y : x)+(int)direction*RGB_XY_OLED_STEP;
        int minimum=changeY ? 1 : 0;
        if (next<minimum) next=minimum;
        if (next>(int)RGB_XY_MAX_COORDINATE) next=RGB_XY_MAX_COORDINATE;
        if (changeY) y=(uint16_t)next;else x=(uint16_t)next;
        ConfigurationFieldUpdate updates[]={{offsetX,&x,sizeof(x)},{offsetY,&y,sizeof(y)}};
        esp_err_t result=configManager->UpdateFields(configManager,"hardware",updates,2,true);
        if (result!=ESP_OK) ESP_LOGE(TAG,"Zone XY save failed: 0x%x",(unsigned)result);
        return;
    }

    // Encoder rotation handling
    bool changed = false;
    if (direction != 0) {
        if (state == UI_STATE_SET_LED_MODE) {
            int next = (int)settings->hardware.ext_led_mode + direction;
            next %= EXT_LED_MODE_COUNT;
            if (next < 0) next += EXT_LED_MODE_COUNT;
            settings->hardware.ext_led_mode = (uint8_t)next;
            changed = true;
        } 
        else if (state == UI_STATE_SET_BUILTIN_STARTUP) {
            settings->hardware.builtin_led_startup_anim = LEDAnimationCatalog_Next(
                settings->hardware.builtin_led_startup_anim, true, direction);
            changed = true;
        }
        else if (state == UI_STATE_SET_BUILTIN_IDLE) {
            settings->hardware.builtin_led_idle_anim = LEDAnimationCatalog_Next(
                settings->hardware.builtin_led_idle_anim, true, direction);
            changed = true;
        }

        else if (state == UI_STATE_SET_BUILTIN_CONNECTING) {
            settings->hardware.builtin_led_connecting_anim = LEDAnimationCatalog_Next(
                settings->hardware.builtin_led_connecting_anim, true, direction);
            changed = true;
        }
        else if (state == UI_STATE_SET_BUILTIN_FAULT) {
            settings->hardware.builtin_led_fault_anim = LEDAnimationCatalog_Next(
                settings->hardware.builtin_led_fault_anim, true, direction);
            changed = true;
        }
        else if (state == UI_STATE_SET_LED_ANIM) {
            settings->hardware.ext_led_startup_anim = LEDAnimationCatalog_Next(
                settings->hardware.ext_led_startup_anim, false, direction);
            
            changed = true;
        }
        else if (state == UI_STATE_SET_RGB_COLD) {
            PresetColorIndex cur_cold = RGBLEDColorEngine_XYtoNearestPreset(settings->hardware.ext_led_color_cold_x, settings->hardware.ext_led_color_cold_y);
            int8_t nextColor = (int8_t)cur_cold + direction;
            if (nextColor < 0) nextColor = RGB_COLOR_COUNT - 1;
            if (nextColor >= RGB_COLOR_COUNT) nextColor = 0;
            RGBLEDColorEngine_PresetToXY((PresetColorIndex)nextColor, &settings->hardware.ext_led_color_cold_x, &settings->hardware.ext_led_color_cold_y);
            changed = true;
        }
        else if (state == UI_STATE_SET_RGB_WARM) {
            PresetColorIndex cur_warm = RGBLEDColorEngine_XYtoNearestPreset(settings->hardware.ext_led_color_warm_x, settings->hardware.ext_led_color_warm_y);
            int8_t nextColor = (int8_t)cur_warm + direction;
            if (nextColor < 0) nextColor = RGB_COLOR_COUNT - 1;
            if (nextColor >= RGB_COLOR_COUNT) nextColor = 0;
            RGBLEDColorEngine_PresetToXY((PresetColorIndex)nextColor, &settings->hardware.ext_led_color_warm_x, &settings->hardware.ext_led_color_warm_y);
            changed = true;
        }
        else if (state == UI_STATE_SET_RGB_HOT) {
            PresetColorIndex cur_hot = RGBLEDColorEngine_XYtoNearestPreset(settings->hardware.ext_led_color_hot_x, settings->hardware.ext_led_color_hot_y);
            int8_t nextColor = (int8_t)cur_hot + direction;
            if (nextColor < 0) nextColor = RGB_COLOR_COUNT - 1;
            if (nextColor >= RGB_COLOR_COUNT) nextColor = 0;
            RGBLEDColorEngine_PresetToXY((PresetColorIndex)nextColor, &settings->hardware.ext_led_color_hot_x, &settings->hardware.ext_led_color_hot_y);
            changed = true;
        }
        else if (state == UI_STATE_SET_RGB_BRIGHTNESS) {
            uint8_t nextBr = LEDBrightness_Step(settings->hardware.ext_led_brightness, direction, 254);
            if (nextBr == settings->hardware.ext_led_brightness) return;
            settings->hardware.ext_led_brightness = nextBr;
            changed = true;
        }
        else if (state == UI_STATE_SET_RGB_TEMP_COLD) {
            float next = settings->hardware.ext_led_temp_cold_min + (float)direction;
            if (next < RGB_COLD_MIN_LIMIT) next = RGB_COLD_MIN_LIMIT;
            if (next >= settings->hardware.ext_led_temp_cold_max) next = settings->hardware.ext_led_temp_cold_max - 1.0f;
            if (next < RGB_COLD_MIN_LIMIT) next = RGB_COLD_MIN_LIMIT;
            settings->hardware.ext_led_temp_cold_min = next;
            changed = true;
        }
        else if (state == UI_STATE_SET_RGB_TEMP_COLD_MAX) {
            float next = settings->hardware.ext_led_temp_cold_max + (float)direction;
            if (next > RGB_COLD_MAX_LIMIT) next = RGB_COLD_MAX_LIMIT;
            if (next <= settings->hardware.ext_led_temp_cold_min) next = settings->hardware.ext_led_temp_cold_min + 1.0f;
            if (next > RGB_COLD_MAX_LIMIT) next = RGB_COLD_MAX_LIMIT;
            settings->hardware.ext_led_temp_cold_max = next;
            changed = true;
        }
        else if (state == UI_STATE_SET_RGB_TEMP_WARM) {
            float next = settings->hardware.ext_led_temp_warm_min + (float)direction;
            if (next < RGB_WARM_MIN_LIMIT) next = RGB_WARM_MIN_LIMIT;
            if (next >= settings->hardware.ext_led_temp_warm_max) next = settings->hardware.ext_led_temp_warm_max - 1.0f;
            if (next < RGB_WARM_MIN_LIMIT) next = RGB_WARM_MIN_LIMIT;
            settings->hardware.ext_led_temp_warm_min = next;
            changed = true;
        }
        else if (state == UI_STATE_SET_RGB_TEMP_WARM_MAX) {
            float next = settings->hardware.ext_led_temp_warm_max + (float)direction;
            if (next > RGB_WARM_MAX_LIMIT) next = RGB_WARM_MAX_LIMIT;
            if (next <= settings->hardware.ext_led_temp_warm_min) next = settings->hardware.ext_led_temp_warm_min + 1.0f;
            if (next > RGB_WARM_MAX_LIMIT) next = RGB_WARM_MAX_LIMIT;
            settings->hardware.ext_led_temp_warm_max = next;
            changed = true;
        }
        else if (state == UI_STATE_SET_RGB_TEMP_HOT) {
            float next = settings->hardware.ext_led_temp_hot_min + (float)direction;
            if (next < RGB_HOT_MIN_LIMIT) next = RGB_HOT_MIN_LIMIT;
            if (next >= settings->hardware.ext_led_temp_hot_max) next = settings->hardware.ext_led_temp_hot_max - 1.0f;
            if (next < RGB_HOT_MIN_LIMIT) next = RGB_HOT_MIN_LIMIT;
            settings->hardware.ext_led_temp_hot_min = next;
            changed = true;
        }
        else if (state == UI_STATE_SET_RGB_TEMP_HOT_MAX) {
            float next = settings->hardware.ext_led_temp_hot_max + (float)direction;
            if (next > RGB_HOT_MAX_LIMIT) next = RGB_HOT_MAX_LIMIT;
            if (next <= settings->hardware.ext_led_temp_hot_min) next = settings->hardware.ext_led_temp_hot_min + 1.0f;
            if (next > RGB_HOT_MAX_LIMIT) next = RGB_HOT_MAX_LIMIT;
            settings->hardware.ext_led_temp_hot_max = next;
            changed = true;
        }
        else if (state == UI_STATE_SET_RGB_TEMP_DANGER) {
            float next = settings->hardware.ext_led_temp_danger + (float)direction;
            if (next < 60.0f) next = 60.0f;
            if (next > 99.0f) next = 99.0f;
            settings->hardware.ext_led_temp_danger = next;
            changed = true;
        }
        else if (state == UI_STATE_SET_RGB_TEMP_FREEZE) {
            float next = settings->hardware.ext_led_temp_freeze + (float)direction;
            if (next < -18.0f) next = -18.0f;
            if (next > 6.0f) next = 6.0f;
            settings->hardware.ext_led_temp_freeze = next;
            changed = true;
        }
    }

    if (changed) {
        /* Persist only changed fields, never a stale whole-block snapshot.
         * ConfigurationManager validates and emits the applied-settings event. */
        ConfigurationFieldUpdate updates[2];
        size_t count = 0;
#define APPEND_CHANGED(field) do { \
    if (memcmp(&original.field, &settings->hardware.field, sizeof(original.field)) != 0) { \
        if (count == sizeof(updates) / sizeof(updates[0])) return; \
        updates[count++] = (ConfigurationFieldUpdate){offsetof(HardwareConfigs, field), \
            &settings->hardware.field, sizeof(settings->hardware.field)}; \
    } \
} while (0)
        APPEND_CHANGED(ext_led_mode);
        APPEND_CHANGED(builtin_led_idle_anim);
        APPEND_CHANGED(builtin_led_startup_anim);
        APPEND_CHANGED(builtin_led_connecting_anim);
        APPEND_CHANGED(builtin_led_fault_anim);
        APPEND_CHANGED(ext_led_startup_anim);
        APPEND_CHANGED(ext_led_anim);
        APPEND_CHANGED(ext_led_color_cold_x);
        APPEND_CHANGED(ext_led_color_cold_y);
        APPEND_CHANGED(ext_led_color_warm_x);
        APPEND_CHANGED(ext_led_color_warm_y);
        APPEND_CHANGED(ext_led_color_hot_x);
        APPEND_CHANGED(ext_led_color_hot_y);
        APPEND_CHANGED(ext_led_brightness);
        APPEND_CHANGED(ext_led_temp_cold_min);
        APPEND_CHANGED(ext_led_temp_cold_max);
        APPEND_CHANGED(ext_led_temp_warm_min);
        APPEND_CHANGED(ext_led_temp_warm_max);
        APPEND_CHANGED(ext_led_temp_hot_min);
        APPEND_CHANGED(ext_led_temp_hot_max);
        APPEND_CHANGED(ext_led_temp_danger);
        APPEND_CHANGED(ext_led_temp_freeze);
#undef APPEND_CHANGED
        if (count) {
            esp_err_t result = configManager->UpdateFields(configManager, "hardware", updates, count, true);
            if (result != ESP_OK) ESP_LOGE(TAG, "LED menu setting save failed: 0x%x", (unsigned)result);
            else if (state==UI_STATE_SET_RGB_BRIGHTNESS && MenuEditSession_State()==state) {
                IRGBLED *rgb=GetRGBLEDService();
                result=rgb && rgb->PreviewBrightness ? rgb->PreviewBrightness(rgb,settings->hardware.ext_led_brightness,LED_BRIGHTNESS_PREVIEW_MS) : ESP_ERR_INVALID_STATE;
                if(result!=ESP_OK) ESP_LOGE(TAG,"External brightness preview rejected: 0x%x",(unsigned)result);
            }
            else if (IsAnimationSelection(state)) PreviewSelectedAnimation(state,&settings->hardware);
            else if (state == UI_STATE_SET_LED_MODE && MenuEditSession_State()!=state) {
                IRGBLED *rgb = GetRGBLEDService();
                result = rgb && rgb->ApplySelectedMode ? rgb->ApplySelectedMode(rgb) : ESP_ERR_INVALID_STATE;
                if (result != ESP_OK) ESP_LOGE(TAG, "LED mode saved but worker command rejected: 0x%x", (unsigned)result);
            }
        }
    }
}

/* ──────────────────────────────────────────
 * Public Implementation
 * ────────────────────────────────────────── */
static const MenuPluginRegistration menuEntries[] = {
    {UI_STATE_RGB_COLD_X,"Soguk X",NULL,OnRGBLEDMenuAction},
    {UI_STATE_RGB_COLD_Y,"Soguk Y",NULL,OnRGBLEDMenuAction},
    {UI_STATE_RGB_WARM_X,"Ilik X",NULL,OnRGBLEDMenuAction},
    {UI_STATE_RGB_WARM_Y,"Ilik Y",NULL,OnRGBLEDMenuAction},
    {UI_STATE_RGB_HOT_X,"Sicak X",NULL,OnRGBLEDMenuAction},
    {UI_STATE_RGB_HOT_Y,"Sicak Y",NULL,OnRGBLEDMenuAction},
    {UI_STATE_RGB_DANGER_X,"Tehlike X",NULL,OnRGBLEDMenuAction},
    {UI_STATE_RGB_DANGER_Y,"Tehlike Y",NULL,OnRGBLEDMenuAction},
    {UI_STATE_RGB_FREEZE_X,"Donma X",NULL,OnRGBLEDMenuAction},
    {UI_STATE_RGB_FREEZE_Y,"Donma Y",NULL,OnRGBLEDMenuAction},
    {UI_STATE_SET_BUILTIN_MODE, "Built-in state", NULL, OnRGBLEDMenuAction},
    {UI_STATE_SET_LED_MODE, "Harici LED", NULL, OnRGBLEDMenuAction},
    {UI_STATE_SET_BUILTIN_BRIGHTNESS, "Dahili Parlak", NULL, OnRGBLEDMenuAction},
    {UI_STATE_SET_BUILTIN_IDLE, "Dahili Bosta", NULL, OnRGBLEDMenuAction},
    {UI_STATE_SET_BUILTIN_STARTUP, "Dahili Baslangic", NULL, OnRGBLEDMenuAction},
    {UI_STATE_SET_BUILTIN_CONNECTING, "Dahili Baglanti", NULL, OnRGBLEDMenuAction},
    {UI_STATE_SET_BUILTIN_FAULT, "Dahili Hata", NULL, OnRGBLEDMenuAction},
    {UI_STATE_SET_LED_ANIM, "Boot Anim", NULL, OnRGBLEDMenuAction},
    {UI_STATE_SET_RGB_COLD, "RGB Soguk", NULL, OnRGBLEDMenuAction},
    {UI_STATE_SET_RGB_WARM, "RGB Ilik", NULL, OnRGBLEDMenuAction},
    {UI_STATE_SET_RGB_HOT, "RGB Sicak", NULL, OnRGBLEDMenuAction},
    {UI_STATE_SET_RGB_DANGER, "Danger zone color", NULL, OnRGBLEDMenuAction},
    {UI_STATE_SET_RGB_FREEZE, "Freeze zone color", NULL, OnRGBLEDMenuAction},
    {UI_STATE_SET_RGB_BRIGHTNESS, "RGB Parlak", NULL, OnRGBLEDMenuAction},
    {UI_STATE_SET_RGB_TEMP_COLD, "Soguk Esik", NULL, OnRGBLEDMenuAction},
    {UI_STATE_SET_RGB_TEMP_COLD_MAX, "Soguk Ust Esik", NULL, OnRGBLEDMenuAction},
    {UI_STATE_SET_RGB_TEMP_WARM, "Ilik Esik", NULL, OnRGBLEDMenuAction},
    {UI_STATE_SET_RGB_TEMP_WARM_MAX, "Ilik Ust Esik", NULL, OnRGBLEDMenuAction},
    {UI_STATE_SET_RGB_TEMP_HOT, "Sicak Esik", NULL, OnRGBLEDMenuAction},
    {UI_STATE_SET_RGB_TEMP_HOT_MAX, "Sicak Ust Esik", NULL, OnRGBLEDMenuAction},
    {UI_STATE_SET_RGB_TEMP_DANGER, "Tehlike Siniri", NULL, OnRGBLEDMenuAction},
    {UI_STATE_SET_RGB_TEMP_FREEZE, "Donma Siniri", NULL, OnRGBLEDMenuAction},
};

esp_err_t RGBLEDMenuPlugin_Init(void) {
    if (initialized) return ESP_OK;
    IMenuManager *menu = (IMenuManager *)ServiceLocator_GetService(SVC_NAME_MENU_MANAGER);
    if (!menu || !menu->RegisterPluginBatch || !menu->UnregisterPluginBatch) return ESP_ERR_INVALID_STATE;
    MenuManagerError result = menu->RegisterPluginBatch(menu, menuEntries, sizeof(menuEntries) / sizeof(menuEntries[0]));
    if (result != MENU_OK) {
        ESP_LOGE(TAG, "LED menu registration failed: %d", result);
        return result == MENU_ERR_BUSY ? ESP_ERR_INVALID_STATE : ESP_FAIL;
    }
    menuManager = menu;
    initialized = true;
    ESP_LOGI(TAG, "LED menu plugins registered");
    return ESP_OK;
}

esp_err_t RGBLEDMenuPlugin_Deinit(void) {
    if (!initialized) return ESP_OK;
    MenuManagerError result = menuManager->UnregisterPluginBatch(menuManager, menuEntries, sizeof(menuEntries) / sizeof(menuEntries[0]));
    if (result != MENU_OK) return result == MENU_ERR_BUSY ? ESP_ERR_INVALID_STATE : ESP_FAIL;
    menuManager = NULL;
    initialized = false;
    return ESP_OK;
}
