#ifndef UI_TEST_FIXTURE_H
#define UI_TEST_FIXTURE_H
#ifndef RACOOL_TEST_NATIVE
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#endif

#include "test_mocks.h"
#include "unity.h"
#include <stdio.h>

// Include full project headers first to declare types
#include "Controllers/MenuManager/IMenuManager.h"
#include "Hardware/RGBLED/RGBLEDColorEngine.h"
#include "Controllers/MenuManager/MenuNavigator.h"
#include "Controllers/MenuManager/MenuStateMachine.h"
#include "Controllers/SystemController/ISystemController.h"

// Define missing mock stubs
const char *RGBLEDColorEngine_GetPresetColorName(PresetColorIndex index) {
  (void)index;
  return "MOCK_COLOR";
}

PresetColorIndex RGBLEDColorEngine_XYtoNearestPreset(uint16_t zcl_x, uint16_t zcl_y) {
  (void)zcl_x; (void)zcl_y;
  return RGB_COLOR_WHITE;
}

void RGBLEDColorEngine_PresetToXY(PresetColorIndex index, uint16_t *out_x, uint16_t *out_y) {
  (void)index;
  if (out_x) *out_x = 0x3127u;
  if (out_y) *out_y = 0x3290u;
}

static unsigned ui_menu_items;
/* Parent relationships needed by the renderer; arbitrary allocation-test
 * children still use the injected tree below. The target uses the real tree. */
UIState MenuNavigator_GetParent(UIState node) {
  if (node == UI_STATE_CAT_ZIGBEE_REP) return UI_STATE_CAT_DEVICE;
  if (node == UI_STATE_CAT_RGB_XY) return UI_STATE_CAT_LIGHT;
  return UI_STATE_MENU_ROOT;
}
UIState MenuNavigator_GetFirstChild(UIState parent) {
  (void)parent;
  return ui_menu_items ? (UIState)1 : UI_STATE_COUNT;
}

UIState MenuNavigator_GetNextSibling(UIState current, bool wrap) {
  (void)current;
  (void)wrap;
  return UI_STATE_COUNT;
}

UIState MenuNavigator_GetNextSiblingIn(UIState current, UIState parentContext, bool wrap) {
  (void)parentContext; (void)wrap;
  return (unsigned)current < ui_menu_items ? (UIState)(current + 1) : UI_STATE_COUNT;
}

/* Allocation tests inject arbitrary tree sizes; production policy remains shared. */
UIState MenuNavigator_FirstVisible(UIState parent,const MenuVisibilityContext *context) {
  UIState node=MenuNavigator_GetFirstChild(parent);
  while(node!=UI_STATE_COUNT && !MenuNavigator_IsVisible(node,context)) node=MenuNavigator_GetNextSiblingIn(node,parent,false);
  return node;
}
UIState MenuNavigator_NextVisible(UIState node,UIState parent,bool circular,const MenuVisibilityContext *context) {
  (void)circular;
  UIState next=MenuNavigator_GetNextSiblingIn(node,parent,false);
  while(next!=UI_STATE_COUNT && !MenuNavigator_IsVisible(next,context)) next=MenuNavigator_GetNextSiblingIn(next,parent,false);
  return next==UI_STATE_COUNT ? node : next;
}

bool MenuStateMachine_RequiresConfirmation(UIState state) {
  (void)state;
  return false;
}

void MenuStateMachine_ForceState(UIState state) {
  (void)state;
}

void MenuStateMachine_ClearHistory(void) {
}

// SystemController mock
typedef struct {
  ISystemController base;
} MockSystemController;

static SystemState mockSystemState = SYSTEM_STATE_READY;
static SystemState mock_get_system_state(ISystemController *self) {
  (void)self;
  return mockSystemState;
}

static MockSystemController mockSystemCtrl = {
  .base = {
    .GetSystemState = mock_get_system_state
  }
};

ISystemController *GetSystemController(void) {
  return &mockSystemCtrl.base;
}

// MenuManager mocks
typedef struct {
  IMenuManager base;
  UIState currentState;
  UIState activeCategory;
  uint32_t lastActivityTime;
  bool isEditing;
} MockMenuManager;

static ServiceState menu_service_state;
static int32_t menu_init_result, menu_start_result;
static unsigned menu_init_calls, menu_start_calls;
static int32_t mock_menu_init_with_config(IMenuManager *self, const MenuManagerConfig *config, struct IDIRegistry *registry) {
  (void)self; (void)config; (void)registry;
  menu_init_calls++;
  if (menu_init_result == 0) menu_service_state = SERVICE_STATE_INITIALIZED;
  return menu_init_result;
}
static void mock_menu_process_encoder(IMenuManager *self, int8_t direction) {
  (void)self; (void)direction;
}
static void mock_menu_process_button(IMenuManager *self) {
  (void)self;
}
static UIState mock_menu_get_current_state(const IMenuManager *self) {
  return ((const MockMenuManager *)self)->currentState;
}
static bool mock_menu_is_editing(const IMenuManager *self) {
  return ((const MockMenuManager *)self)->isEditing;
}
static UIState mock_menu_get_active_category(const IMenuManager *self) {
  return ((const MockMenuManager *)self)->activeCategory;
}
static uint32_t mock_menu_get_last_activity_time(const IMenuManager *self) {
  return ((const MockMenuManager *)self)->lastActivityTime;
}
static void mock_menu_set_state(IMenuManager *self, UIState state) {
  ((MockMenuManager *)self)->currentState = state;
}
static bool mock_menu_copy_label(const IMenuManager *self, UIState state, char *output, size_t capacity) {
  (void)self; (void)state;
  if (!output || capacity < sizeof("MOCK_LABEL")) return false;
  memcpy(output, "MOCK_LABEL", sizeof("MOCK_LABEL"));
  return true;
}
static ServiceState mock_menu_get_state(IService *self) {
  (void)self;
  return menu_service_state;
}
static int32_t mock_menu_service_init(IService *self, struct IDIRegistry *reg) {
  (void)self; (void)reg;
  return 0;
}
static int32_t mock_menu_service_start(IService *self) {
  (void)self;
  menu_start_calls++;
  if (menu_start_result == 0) menu_service_state = SERVICE_STATE_STARTED;
  return menu_start_result;
}

static MockMenuManager mockMenuMgr = {
  .base = {
    .base = {
      .Initialize = mock_menu_service_init,
      .Start = mock_menu_service_start,
      .GetState = mock_menu_get_state
    },
    .InitializeWithConfig = mock_menu_init_with_config,
    .ProcessEncoder = mock_menu_process_encoder,
    .ProcessButton = mock_menu_process_button,
    .GetCurrentState = mock_menu_get_current_state,
    .IsEditing = mock_menu_is_editing,
    .GetActiveCategory = mock_menu_get_active_category,
    .GetLastActivityTime = mock_menu_get_last_activity_time,
    .SetState = mock_menu_set_state,
    .CopyLabel = mock_menu_copy_label
  },
  .currentState = UI_STATE_MONITOR,
  .activeCategory = UI_STATE_CAT_TEMP,
  .lastActivityTime = 0,
  .isEditing = false
};

IMenuManager *MenuManager_GetModule(void) {
  return &mockMenuMgr.base;
}

MenuManagerConfig MenuManager_GetDefaultConfig(void) {
  MenuManagerConfig cfg = {};
  return cfg;
}

#include "Core/EventBus/IEventBus.h"

static int32_t mock_eb_subscribe(IEventBus *self, const char *eventName, BusEventHandler handler) {
    (void)self; (void)eventName; (void)handler;
    return 0;
}
static int32_t ui_unsubscribe_result;
static int32_t mock_eb_unsubscribe(IEventBus *self, const char *eventName, BusEventHandler handler) {
    (void)self; (void)eventName; (void)handler;
    return ui_unsubscribe_result;
}
static int32_t mock_eb_publish(BusEvent event) {
    (void)event;
    return 0;
}

static IEventBus mock_event_bus = {
    .base            = {0},
    .Subscribe       = mock_eb_subscribe,
    .Unsubscribe     = mock_eb_unsubscribe,
    .Publish         = mock_eb_publish,
    .PublishFromISR  = NULL,
    .InitializeWithConfig = NULL,
};

#include "lvgl.h"
#include "Hardware/OLED/IOLED.h"
#include "Core/ServiceLocator/ServiceNames.h"

/* Allocation ownership model for the production screen constructors. */
static struct { void *parent; bool live; lv_color_t text_color; char label[96]; const void *image_source; const lv_font_t *font; uint32_t flags; int width, height, x, y, align; } ui_nodes[128];
static void ui_capture_font(lv_obj_t *object, const lv_font_t *font, int selector) {
  (void)selector;
  for (unsigned i = 0; i < 128; ++i) if ((void *)&ui_nodes[i] == (void *)object) ui_nodes[i].font = font;
}
#define lv_obj_set_style_text_font ui_capture_font
static void ui_capture_align(lv_obj_t *object, int align, int x, int y) {
  for (unsigned i = 0; i < 128; ++i) if ((void *)&ui_nodes[i] == (void *)object) {
    ui_nodes[i].align = align; ui_nodes[i].x = x; ui_nodes[i].y = y;
  }
}
#define lv_obj_align ui_capture_align
static void ui_capture_text_color(lv_obj_t *object, lv_color_t color, int selector) {
  (void)selector;
  for (unsigned i = 0; i < 128; ++i)
    if ((void *)&ui_nodes[i] == (void *)object) { ui_nodes[i].text_color = color; return; }
}
static inline lv_color_t ui_text_color(lv_obj_t *object) {
  for (unsigned i = 0; i < 128; ++i)
    if ((void *)&ui_nodes[i] == (void *)object) return ui_nodes[i].text_color;
  return 0;
}
static void ui_capture_label(lv_obj_t *object, const char *text) {
  for (unsigned i = 0; i < 128; ++i)
    if ((void *)&ui_nodes[i] == (void *)object) { snprintf(ui_nodes[i].label, sizeof(ui_nodes[i].label), "%s", text); return; }
}
static inline const char *ui_label_text(lv_obj_t *object) {
  for (unsigned i = 0; i < 128; ++i)
    if ((void *)&ui_nodes[i] == (void *)object) return ui_nodes[i].label;
  return "";
}
static void ui_capture_flag(lv_obj_t *object, uint32_t flag) {
  for(unsigned i=0;i<128;++i) if((void *)&ui_nodes[i]==(void *)object) ui_nodes[i].flags |= flag;
}
static void ui_remove_flag(lv_obj_t *object, uint32_t flag) {
  for(unsigned i=0;i<128;++i) if((void *)&ui_nodes[i]==(void *)object) ui_nodes[i].flags &= ~flag;
}
static void ui_capture_size(lv_obj_t *object, int width, int height) {
  for(unsigned i=0;i<128;++i) if((void *)&ui_nodes[i]==(void *)object) {ui_nodes[i].width=width;ui_nodes[i].height=height;}
}
#define lv_obj_add_flag ui_capture_flag
#define lv_obj_remove_flag ui_remove_flag
static void ui_hidden(lv_obj_t *object,bool hidden) { if(hidden) ui_capture_flag(object,LV_OBJ_FLAG_HIDDEN);else ui_remove_flag(object,LV_OBJ_FLAG_HIDDEN); }
#define lv_obj_set_hidden ui_hidden
#define lv_obj_set_size ui_capture_size
static void ui_capture_image(lv_obj_t *object, const void *source) {
  for (unsigned i = 0; i < 128; ++i)
    if ((void *)&ui_nodes[i] == (void *)object) { ui_nodes[i].image_source = source; return; }
}
static const void *ui_image_source(lv_obj_t *object) {
  for (unsigned i = 0; i < 128; ++i)
    if ((void *)&ui_nodes[i] == (void *)object) return ui_nodes[i].image_source;
  return NULL;
}
#define lv_image_set_src ui_capture_image
#define lv_label_set_text ui_capture_label
#define lv_obj_set_style_text_color ui_capture_text_color
static unsigned ui_alloc_calls, ui_fail_at, ui_live_nodes, ui_load_calls, ui_lock_depth;
static lv_obj_t *ui_active_screen;
static lv_group_t *ui_default_group;
static lv_obj_t *ui_test_create(lv_obj_t *parent) {
  if (++ui_alloc_calls == ui_fail_at) return NULL;
  for (unsigned i = 0; i < 128; ++i) if (!ui_nodes[i].live) {
    ui_nodes[i].live = true; ui_nodes[i].parent = parent; ui_live_nodes++;
    return (lv_obj_t *)&ui_nodes[i];
  }
  TEST_FAIL_MESSAGE("Mock LVGL ownership capacity exhausted"); return NULL;
}
static void ui_test_delete(lv_obj_t *object) {
  for (unsigned i = 0; i < 128; ++i) if ((void *)&ui_nodes[i] == (void *)object) {
    TEST_ASSERT_TRUE(ui_nodes[i].live);
    TEST_ASSERT_NOT_EQUAL((uintptr_t)ui_active_screen, (uintptr_t)object);
    for (unsigned j = 0; j < 128; ++j)
      if (ui_nodes[j].live && ui_nodes[j].parent == object) ui_test_delete((lv_obj_t *)&ui_nodes[j]);
    ui_nodes[i].live = false; ui_live_nodes--; return;
  }
  TEST_FAIL_MESSAGE("Deleting an unowned LVGL object");
}
static uint32_t ui_test_child_count(const lv_obj_t *parent) {
  uint32_t count = 0;
  for (unsigned i = 0; i < 128; ++i) if (ui_nodes[i].live && ui_nodes[i].parent == parent) count++;
  return count;
}
static lv_obj_t *ui_test_child(const lv_obj_t *parent, int32_t index) {
  for (unsigned i = 0; i < 128; ++i) if (ui_nodes[i].live && ui_nodes[i].parent == parent)
    if (index-- == 0) return (lv_obj_t *)&ui_nodes[i];
  return NULL;
}
static void ui_test_clean(lv_obj_t *parent) {
  lv_obj_t *child;
  while ((child = ui_test_child(parent, 0))) ui_test_delete(child);
}
#define lv_obj_get_child_count ui_test_child_count
#define lv_obj_get_child ui_test_child
#define lv_obj_clean ui_test_clean
static lv_group_t *ui_test_group_create(void) { return (lv_group_t *)ui_test_create(NULL); }
static void ui_test_group_delete(lv_group_t *group) { ui_test_delete((lv_obj_t *)group); }
static void ui_test_load(lv_obj_t *screen) { ui_active_screen = screen; ui_load_calls++; }
static lv_obj_t *ui_test_active(void) { return ui_active_screen; }
static void ui_test_default(lv_group_t *group) { ui_default_group = group; }
#define lv_obj_create ui_test_create
#define lv_image_create ui_test_create
#define lv_label_create ui_test_create
#define lv_list_create ui_test_create
#define lv_bar_create ui_test_create
#define lv_group_create ui_test_group_create
#define lv_obj_delete ui_test_delete
#define lv_group_delete ui_test_group_delete
#define lv_screen_load ui_test_load
#define lv_screen_active ui_test_active
#define lv_group_set_default ui_test_default
static ServiceState ui_oled_state;
static ServiceState ui_test_oled_state(IService *self) { (void)self; return ui_oled_state; }
static bool ui_lock_failure;
static bool ui_test_lock(IOLED *self) { (void)self; if (ui_lock_failure) return false; TEST_ASSERT_EQUAL(0, ui_lock_depth++); return true; }
static void ui_test_unlock(IOLED *self) { (void)self; TEST_ASSERT_EQUAL(1, ui_lock_depth--); }
static esp_err_t ui_present_result;
static unsigned ui_present_calls;
static esp_err_t ui_present(IOLED *self) { (void)self; ++ui_present_calls; return ui_present_result; }
static IOLED ui_test_oled = {.base = {.GetState = ui_test_oled_state}, .Lock = ui_test_lock, .Unlock = ui_test_unlock, .PresentAndWake = ui_present};
static IService *ui_test_services(const char *name) {
  return strcmp(name, SVC_NAME_OLED) == 0 ? &ui_test_oled.base : NULL;
}

#include "Core/SystemInitializer/SystemInitializerStartup.h"
static SystemStartupProgress ui_startup_progress;
SystemStartupProgress SystemInitializer_GetStartupProgress(void) { return ui_startup_progress; }

// Include production dispatch and rendering code for native behavior tests.
#include "Controllers/UIController/BootAnimation.c"
#include "Controllers/UIController/Widgets/WidgetTemp.c"
#include "Controllers/UIController/Widgets/WidgetFan.c"
#include "Controllers/UIController/Widgets/WidgetLED.c"
#include "Controllers/UIController/Widgets/WidgetRGB.c"
#include "Controllers/UIController/Widgets/WidgetSystem.c"
#include "Controllers/UIController/Widgets/WidgetZigbeeReport.c"
#include "Controllers/UIController/UIWidgets.c"
#include "Controllers/UIController/Screens/ScreenBoot.c"
#include "Controllers/UIController/Screens/ScreenMonitor.c"
#include "Controllers/UIController/Screens/ScreenMenu.c"
#include "Controllers/UIController/Screens/ScreenSubmenu.c"
#include "Controllers/UIController/Screens/ScreenZigbeePair.c"
#include "Controllers/UIController/UIRenderer.c"
#include "Controllers/UIController/EventHandlers/UIEventHandlerTemp.c"
#include "Controllers/UIController/EventHandlers/UIEventHandlerRotary.c"
#include "Controllers/UIController/EventHandlers/UIEventHandlerRelay.c"
#include "Controllers/UIController/EventHandlers/UIEventHandlerConfig.c"
#include "Controllers/UIController/EventHandlers/UIEventHandlerZigbee.c"
#include "Management/Watchdog/WatchdogWorker.c"
#include "Controllers/UIController/UIController.c"

static UIEvent lastSentUIEvent;

static BaseType_t custom_xQueueSend(QueueHandle_t xQueue, const void * pvItemToQueue, TickType_t xTicksToWait) {
  (void)xQueue; (void)xTicksToWait;
  if (pvItemToQueue) {
    memcpy(&lastSentUIEvent, pvItemToQueue, sizeof(UIEvent));
  }
  return pdTRUE;
}

static BaseType_t custom_xTaskCreate(TaskFunction_t pxTaskCode,
                                     const char * const pcName,
                                     const uint32_t usStackDepth,
                                     void * const pvParameters,
                                     UBaseType_t uxPriority,
                                     TaskHandle_t * const pxCreatedTask) {
  (void)pxTaskCode; (void)pcName; (void)usStackDepth; (void)pvParameters; (void)uxPriority;
  if (pxCreatedTask) {
    *pxCreatedTask = (TaskHandle_t)0x9999;
  }
  return pdPASS;
}

static void UIFixture_SetUp(void) {
  mockSystemState = SYSTEM_STATE_READY;
  ui_startup_progress = (SystemStartupProgress){0};
  TEST_ASSERT_TRUE(Localization_Apply("tr"));
  IUIController saved_interface = *UIController_GetModule();
  memset(&uiControllerInstance, 0, sizeof(uiControllerInstance));
  uiControllerInstance.base = saved_interface;
  ui_lock_failure = false;
  ui_present_result = ESP_OK; ui_present_calls = 0;
  ui_menu_items = 0;
  memset(ui_nodes, 0, sizeof(ui_nodes));
  ui_alloc_calls = ui_fail_at = ui_live_nodes = ui_load_calls = ui_lock_depth = 0;
  ui_active_screen = (lv_obj_t *)0x100;
  ui_default_group = (lv_group_t *)0x200;
  ui_oled_state = SERVICE_STATE_STARTED;
  s_animState = BOOT_ANIM_STATE_IDLE;
  RESET_FAKE(mocked_ServiceLocator_GetService);
  mocked_ServiceLocator_GetService_fake.custom_fake = ui_test_services;

  menu_service_state = SERVICE_STATE_STARTED;
  menu_init_result = menu_start_result = 0;
  menu_init_calls = menu_start_calls = 0;
  ui_unsubscribe_result = 0;
  // Reset only state variables, DO NOT clear function pointers or service structures
  memset(&uiControllerInstance.worker, 0, sizeof(uiControllerInstance.worker));
  uiControllerInstance.state = SERVICE_STATE_UNINITIALIZED;
  uiControllerInstance.taskHandle = NULL;
  uiControllerInstance.lifecycleMutex = NULL;
  uiControllerInstance.ownedBus = NULL;
  uiControllerInstance.subscriptions = 0;
  uiControllerInstance.eventQueue = NULL;
  uiControllerInstance.mutex = NULL;
  uiControllerInstance.isScreenOn = true;
  uiControllerInstance.resetKeepsScreenOff = false;
  memset(&lastSentUIEvent, 0, sizeof(UIEvent));

  // Re-link interface function pointers if they got cleared
  UIController_GetModule();

  mockMenuMgr.base.PostEvent = NULL;
  mockMenuMgr.base.UpdateActivity = NULL;
  mockMenuMgr.currentState = UI_STATE_MONITOR;
  mockMenuMgr.activeCategory = UI_STATE_CAT_TEMP;
  mockMenuMgr.lastActivityTime = 0;
  mockMenuMgr.isEditing = false;

  // Setup FFF mock outputs to prevent initialization failures
  RESET_FAKE(mocked_xQueueCreate);
  RESET_FAKE(mocked_xSemaphoreCreateMutex);
  RESET_FAKE(mocked_xQueueSend);
  RESET_FAKE(mocked_xQueueReceive);
  RESET_FAKE(mocked_xTaskGetCurrentTaskHandle);
  RESET_FAKE(mocked_vTaskSuspend);
  RESET_FAKE(mocked_xTaskCreate);
  RESET_FAKE(mocked_xSemaphoreTake);
  RESET_FAKE(mocked_xSemaphoreGive);
  RESET_FAKE(mocked_ServiceLocator_GetEventBus);

  mocked_xQueueCreate_fake.return_val = (QueueHandle_t)0x1234;
  mocked_xSemaphoreCreateMutex_fake.return_val = (SemaphoreHandle_t)0x5678;
  mocked_xQueueSend_fake.custom_fake = custom_xQueueSend;
  mocked_xTaskCreate_fake.custom_fake = custom_xTaskCreate;
  mocked_xSemaphoreTake_fake.return_val = pdTRUE;
  mocked_xSemaphoreGive_fake.return_val = pdTRUE;
  mocked_ServiceLocator_GetEventBus_fake.return_val = &mock_event_bus;
}

static void UIFixture_TearDown(void) {
}


#endif
