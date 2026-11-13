#include "unity.h"
#define ServiceLocator_GetService MenuTarget_GetService
#define ServiceLocator_GetEventBus MenuTarget_GetEventBus
#include "../../../../src/Controllers/MenuManager/MenuManager.c"
#undef ServiceLocator_GetService
#undef ServiceLocator_GetEventBus
#include "../../../../src/Controllers/MenuManager/MenuNavigator.c"
#define TAG MenuTargetStateTag
#include "../../../../src/Controllers/MenuManager/MenuStateMachine.c"
#undef TAG
#include "../../../../src/Controllers/MenuManager/MenuPluginManager.c"
#include "../../../../src/Controllers/MenuManager/MenuManagerConfig.c"
#include "../../../../src/Controllers/MenuManager/MenuRangeEditor.c"
#include "../../../../src/Controllers/MenuManager/MenuEditSession.c"

IService *MenuTarget_GetService(const char *name) { (void)name; return NULL; }
IEventBus *MenuTarget_GetEventBus(void) { return NULL; }
static SemaphoreHandle_t callback_release;
static atomic_bool callback_entered;
static atomic_uint callback_count;
static bool initialized;
static bool public_task_started;
static atomic_bool public_task_returned;

static void blocked_menu_callback(UIState state, int8_t direction, bool pressed) {
    (void)state; (void)direction; (void)pressed;
    atomic_fetch_add(&callback_count, 1);
    atomic_store(&callback_entered, true);
    xSemaphoreTake(callback_release, portMAX_DELAY);
}

static void public_confirmation_task(void *unused) {
    (void)unused;
    impl.base.ConfirmAction(&impl.base, true);
    atomic_store(&public_task_returned, true);
    vTaskDelete(NULL);
}

void target_menu_worker_cleanup(void) {
    if (callback_release) xSemaphoreGive(callback_release);
    if (public_task_started) {
        for (unsigned i = 0; i < 200 && !atomic_load(&public_task_returned); ++i)
            vTaskDelay(pdMS_TO_TICKS(10));
        TEST_ASSERT_TRUE(atomic_load(&public_task_returned));
        public_task_started = false;
    }
    if (initialized) TEST_ASSERT_EQUAL(0, Stop(&impl.base.base));
    if (callback_release) {
        vSemaphoreDelete(callback_release);
        callback_release = NULL;
    }
}

void target_menu_worker_verify_shutdown(void) {
    IMenuManager *menu = MenuManager_GetModule();
    TEST_ASSERT_EQUAL(0, menu->base.Initialize(&menu->base, NULL));
    initialized = true;
    callback_release = xSemaphoreCreateBinary();
    TEST_ASSERT_NOT_NULL(callback_release);
    atomic_store(&callback_entered, false);
    atomic_store(&callback_count, 0);
    TEST_ASSERT_EQUAL(MENU_OK, menu->RegisterPlugin(menu, "Block", UI_STATE_SET_LOW_TEMP, blocked_menu_callback));
    MenuStateMachine_ForceState(UI_STATE_SET_LOW_TEMP);
    menu->SetEditing(menu, true);
    TEST_ASSERT_EQUAL(0, menu->base.Start(&menu->base));
    menu->ProcessEncoder(menu, 1);
    for (unsigned i = 0; i < 100 && !atomic_load(&callback_entered); ++i)
        vTaskDelay(pdMS_TO_TICKS(10));
    TEST_ASSERT_TRUE(atomic_load(&callback_entered));
    /* The blocked worker leaves queue admission deterministic. The real RTOS
     * queue must own a copy and report capacity exhaustion to the caller. */
    MenuEvent request = {.type = MENU_EVENT_NAVIGATE_TO, .data.targetState = UI_STATE_SET_HIGH_TEMP};
    for (unsigned i = 0; i < impl.config.eventQueueSize; ++i)
        TEST_ASSERT_EQUAL(MENU_OK, menu->PostEvent(menu, &request));
    request.data.targetState = UI_STATE_MONITOR;
    MenuEvent copied_request = {0};
    TEST_ASSERT_EQUAL(pdTRUE, xQueuePeek(impl.eventQueue, &copied_request, 0));
    TEST_ASSERT_EQUAL(UI_STATE_SET_HIGH_TEMP, copied_request.data.targetState);
    TEST_ASSERT_EQUAL(MENU_ERR_QUEUE_FULL, menu->PostEvent(menu, &request));
    /* These readers must not wait for the state mutex held by MenuTask. */
    TEST_ASSERT_EQUAL(UI_STATE_SET_LOW_TEMP, menu->GetCurrentState(menu));
    TEST_ASSERT_EQUAL(UI_STATE_CAT_TEMP, menu->GetActiveCategory(menu));
    TEST_ASSERT_TRUE(menu->IsEditing(menu));
    TEST_ASSERT_FALSE(menu->GetConfirmSelection(menu));
    TEST_ASSERT_TRUE(menu->GetLastActivityTime(menu) > 0);
    TEST_ASSERT_NOT_NULL(menu->GetLastActivityTimeMs);
    menu->UpdateActivity(menu);
    uint64_t activity = menu->GetLastActivityTimeMs(menu);
    TEST_ASSERT_TRUE(activity > 0);
    TEST_ASSERT_TRUE(activity <= (uint64_t)esp_timer_get_time() / 1000ULL);
    TEST_ASSERT_EQUAL((uint32_t)activity, menu->GetLastActivityTime(menu));
    printf("MENU_ACTIVITY fullMs=%llu\n", (unsigned long long)activity);
    const MenuPluginRegistration blocked_batch[] = {
        {UI_STATE_SET_LOW_TEMP, "Block", NULL, blocked_menu_callback},
        {UI_STATE_RESTART, "New", NULL, blocked_menu_callback}
    };
    TEST_ASSERT_EQUAL(MENU_ERR_BUSY, menu->RegisterPluginBatch(menu, blocked_batch, 2));
    TEST_ASSERT_FALSE(MenuPluginManager_HasPlugin(UI_STATE_RESTART));
    TEST_ASSERT_EQUAL(MENU_ERR_BUSY, menu->UnregisterPluginBatch(menu, blocked_batch, 2));
    char label[MENU_LABEL_CAPACITY];
    TEST_ASSERT_TRUE(menu->CopyLabel(menu, UI_STATE_SET_LOW_TEMP, label, sizeof(label)));
    TEST_ASSERT_EQUAL_STRING("Block", label);
    TEST_ASSERT_EQUAL(MENU_ERR_BUSY, menu->UnregisterPlugin(menu, UI_STATE_SET_LOW_TEMP));
    TEST_ASSERT_EQUAL(MENU_ERR_BUSY, menu->RegisterPlugin(menu, "Changed", UI_STATE_SET_LOW_TEMP, blocked_menu_callback));
    TEST_ASSERT_EQUAL(MENU_ERR_BUSY, MenuPluginManager_SetEnabled(UI_STATE_SET_LOW_TEMP, false));
    TaskHandle_t worker = atomic_load(&impl.worker.task);
    QueueHandle_t queue = impl.eventQueue;
    SemaphoreHandle_t mutex = impl.mutex;
    menu->ProcessButton(menu); /* Pending event must not survive Stop/Start. */
    TEST_ASSERT_NOT_EQUAL(0, menu->base.Stop(&menu->base));
    TEST_ASSERT_EQUAL_PTR(worker, atomic_load(&impl.worker.task));
    TEST_ASSERT_EQUAL_PTR(queue, impl.eventQueue);
    TEST_ASSERT_EQUAL_PTR(mutex, impl.mutex);
    TEST_ASSERT_EQUAL(SERVICE_STATE_ERROR, menu->base.GetState(&menu->base));
    TEST_ASSERT_EQUAL(MENU_ERR_INVALID_STATE, menu->PostEvent(menu, &request));
    unsigned queued = uxQueueMessagesWaiting(queue);
    menu->ProcessButton(menu);
    TEST_ASSERT_EQUAL(queued, uxQueueMessagesWaiting(queue));
    TEST_ASSERT_NOT_EQUAL(0, menu->base.Start(&menu->base));
    xSemaphoreGive(callback_release);
    TEST_ASSERT_EQUAL(0, menu->base.Stop(&menu->base));
    TEST_ASSERT_NULL(atomic_load(&impl.worker.task));
    TEST_ASSERT_EQUAL(0, uxQueueMessagesWaiting(queue));
    TEST_ASSERT_EQUAL(SERVICE_STATE_STOPPED, menu->base.GetState(&menu->base));
    TEST_ASSERT_EQUAL(0, menu->base.Start(&menu->base));
    vTaskDelay(pdMS_TO_TICKS(100));
    TEST_ASSERT_EQUAL(1, atomic_load(&callback_count));
    TEST_ASSERT_TRUE(menu->IsEditing(menu));
    TEST_ASSERT_EQUAL(0, menu->base.Stop(&menu->base));
    TEST_ASSERT_EQUAL(MENU_OK, menu->UnregisterPlugin(menu, UI_STATE_SET_LOW_TEMP));

    /* A synchronous API callback belongs to another task, not MenuTask.
     * Stop must preserve ownership until that public call returns too. */
    atomic_store(&callback_entered, false);
    atomic_store(&callback_count, 0);
    atomic_store(&public_task_returned, false);
    TEST_ASSERT_EQUAL(MENU_OK, menu->RegisterPlugin(menu, "Confirm", UI_STATE_RESTART, blocked_menu_callback));
    MenuStateMachine_ForceState(UI_STATE_RESTART);
    menu->SetEditing(menu, true);
    TEST_ASSERT_EQUAL(0, menu->base.Start(&menu->base));
    TEST_ASSERT_EQUAL(pdPASS, xTaskCreate(public_confirmation_task, "MenuConfirmTest", 4096, NULL, 5, NULL));
    public_task_started = true;
    for (unsigned i = 0; i < 100 && !atomic_load(&callback_entered); ++i)
        vTaskDelay(pdMS_TO_TICKS(10));
    TEST_ASSERT_TRUE(atomic_load(&callback_entered));
    worker = atomic_load(&impl.worker.task);
    TEST_ASSERT_NOT_EQUAL(0, menu->base.Stop(&menu->base));
    TEST_ASSERT_EQUAL(SERVICE_STATE_ERROR, menu->base.GetState(&menu->base));
    TEST_ASSERT_EQUAL_PTR(worker, atomic_load(&impl.worker.task));
    TEST_ASSERT_EQUAL_PTR(queue, impl.eventQueue);
    TEST_ASSERT_NOT_EQUAL(0, menu->base.Start(&menu->base));
    menu->ConfirmAction(menu, true);
    TEST_ASSERT_EQUAL(1, atomic_load(&callback_count));
    TEST_ASSERT_FALSE(atomic_load(&public_task_returned));
    xSemaphoreGive(callback_release);
    for (unsigned i = 0; i < 100 && !atomic_load(&public_task_returned); ++i)
        vTaskDelay(pdMS_TO_TICKS(10));
    TEST_ASSERT_TRUE(atomic_load(&public_task_returned));
    public_task_started = false;
    TEST_ASSERT_EQUAL(0, menu->base.Stop(&menu->base));
    TEST_ASSERT_EQUAL(MENU_OK, menu->UnregisterPlugin(menu, UI_STATE_RESTART));
    TEST_ASSERT_EQUAL(0, menu->base.Start(&menu->base));
    TEST_ASSERT_EQUAL(0, menu->base.Stop(&menu->base));
}
