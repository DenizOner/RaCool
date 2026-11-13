#include "unity.h"
#define ServiceLocator_GetService RotaryTarget_GetService
#define ServiceLocator_GetEventBus RotaryTarget_GetEventBus
#define register_encoder_commands RotaryTarget_RegisterCommands
#include "../../../../src/Hardware/RotaryEncoder/RotaryEncoder.c"
#undef ServiceLocator_GetService
#undef ServiceLocator_GetEventBus
#undef register_encoder_commands
#include "../../../../src/Hardware/RotaryEncoder/RotaryEncoderGesture.c"

static IPinManager *fixture_pin_manager;
void target_rotary_set_pin_manager(IPinManager *pins) { fixture_pin_manager = pins; }
IService *RotaryTarget_GetService(const char *name) {
    return fixture_pin_manager && strcmp(name, SVC_NAME_PIN_MANAGER) == 0 ?
           &fixture_pin_manager->base : NULL;
}
IEventBus *RotaryTarget_GetEventBus(void) { return NULL; }
esp_err_t RotaryTarget_RegisterCommands(IDebugConsole *console) { (void)console; return ESP_OK; }
static SemaphoreHandle_t entered, release_callback;
static bool target_rotary_owned, target_isr_service_owned;
static lv_display_t *fixture_display, *previous_display;
static uint8_t fixture_pixels[128 * 64 / 8 + 8];
static void fixture_flush(lv_display_t *display, const lv_area_t *area, uint8_t *pixels) {
    (void)area; (void)pixels; lv_display_flush_ready(display);
}
static void blocked_rotation(RotaryEncoderRotationEvent event, void *ctx) {
    (void)event; (void)ctx;
    xSemaphoreGive(entered);
    xSemaphoreTake(release_callback, portMAX_DELAY);
}
void target_rotary_worker_cleanup(void) {
    if (release_callback) xSemaphoreGive(release_callback);
    if (target_rotary_owned) {
        for (unsigned i = 0; i < 300 && rotaryEncoderService.taskHandle &&
             !atomic_load(&rotaryEncoderService.worker_exited); i++) vTaskDelay(1);
        TEST_ASSERT_EQUAL(0, RotaryEncoder_Stop(&rotaryEncoderService.base.base));
        if (rotaryEncoderService.indev) {
            lv_indev_delete(rotaryEncoderService.indev);
            rotaryEncoderService.indev = NULL;
        }
        if (rotaryEncoderService.eventQueue) vQueueDelete(rotaryEncoderService.eventQueue);
        if (rotaryEncoderService.rawButtonQueue) vQueueDelete(rotaryEncoderService.rawButtonQueue);
        rotaryEncoderService.eventQueue = rotaryEncoderService.rawButtonQueue = NULL;
        target_rotary_owned = false;
    }
    if (fixture_display) {
        lv_display_delete(fixture_display);
        fixture_display = NULL;
        if (previous_display) lv_display_set_default(previous_display);
        previous_display = NULL;
    }
    if (entered) { vSemaphoreDelete(entered); entered = NULL; }
    if (release_callback) { vSemaphoreDelete(release_callback); release_callback = NULL; }
    if (target_isr_service_owned) {
        gpio_uninstall_isr_service();
        target_isr_service_owned = false;
    }
}
void target_rotary_worker_verify_shutdown(void) {
    if (!lv_is_initialized()) lv_init();
    previous_display = lv_display_get_default();
    fixture_display = lv_display_create(128, 64);
    TEST_ASSERT_NOT_NULL(fixture_display);
    lv_display_set_color_format(fixture_display, LV_COLOR_FORMAT_I1);
    lv_display_set_buffers(fixture_display, fixture_pixels, NULL, sizeof(fixture_pixels), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(fixture_display, fixture_flush);
    lv_display_set_default(fixture_display);
    if (!fixture_pin_manager) {
        esp_err_t installed = gpio_install_isr_service(0);
        TEST_ASSERT_TRUE(installed == ESP_OK || installed == ESP_ERR_INVALID_STATE);
        target_isr_service_owned = installed == ESP_OK;
    }
    memset(&rotaryEncoderService, 0, sizeof(rotaryEncoderService));
    rotaryEncoderService.spinlock = (portMUX_TYPE)portMUX_INITIALIZER_UNLOCKED;
    target_rotary_owned = true;
    entered = xSemaphoreCreateBinary();
    release_callback = xSemaphoreCreateBinary();
    TEST_ASSERT_NOT_NULL(entered); TEST_ASSERT_NOT_NULL(release_callback);
    TEST_ASSERT_EQUAL(0, RotaryEncoder_Initialize(NULL, NULL));
    TEST_ASSERT_EQUAL(ESP_OK, RotaryEncoder_OnRotation(NULL, blocked_rotation, NULL));
    TEST_ASSERT_EQUAL(0, RotaryEncoder_Start(NULL));
    const uint8_t detent_steps[] = {2, 8, 255, RE_PCNT_STEPS_PER_DETENT};
    for (unsigned i = 0; i < sizeof(detent_steps) / sizeof(detent_steps[0]); i++) {
        TEST_ASSERT_EQUAL(ESP_OK, RotaryEncoder_SetStepsPerDetent(NULL, detent_steps[i]));
        TEST_ASSERT_EQUAL(detent_steps[i], rotaryEncoderService.runtime_config.steps_per_detent);
        TEST_ASSERT_TRUE(rotaryEncoderService.pcnt_running);
        TEST_ASSERT_FALSE(rotaryEncoderService.control_active);
    }
    if (fixture_pin_manager)
        TEST_ASSERT_EQUAL(PIN_MANAGER_ERR_BUSY, fixture_pin_manager->base.Stop(&fixture_pin_manager->base));
    TaskHandle_t worker = rotaryEncoderService.taskHandle;
    QueueHandle_t queue = rotaryEncoderService.eventQueue;
    lv_indev_t *input = rotaryEncoderService.indev;
    pcnt_unit_handle_t unit = rotaryEncoderService.pcnt_unit;
    RotaryEncoderEvent event = {.type = ROTARY_ENCODER_EVENT_ROTATION};
    TEST_ASSERT_EQUAL(ESP_OK, RotaryEncoder_Simulate(RE_SIM_CW, 1));
    TEST_ASSERT_EQUAL(pdTRUE, xSemaphoreTake(entered, pdMS_TO_TICKS(2000)));
    TEST_ASSERT_EQUAL(-1, RotaryEncoder_Stop(NULL));
    TEST_ASSERT_EQUAL_PTR(worker, rotaryEncoderService.taskHandle);
    TEST_ASSERT_EQUAL_PTR(queue, rotaryEncoderService.eventQueue);
    TEST_ASSERT_EQUAL_PTR(input, rotaryEncoderService.indev);
    TEST_ASSERT_EQUAL_PTR(unit, rotaryEncoderService.pcnt_unit);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, RotaryEncoder_UnsubscribeRotation(NULL, blocked_rotation));
    TEST_ASSERT_EQUAL(-1, RotaryEncoder_Start(NULL));
    UBaseType_t waiting = uxQueueMessagesWaiting(queue);
    RotaryEncoder_EnqueueEvent(&event);
    TEST_ASSERT_EQUAL(waiting, uxQueueMessagesWaiting(queue));
    xSemaphoreGive(release_callback);
    for (unsigned i = 0; i < 300 && !atomic_load(&rotaryEncoderService.worker_exited); i++) vTaskDelay(1);
    TEST_ASSERT_TRUE(atomic_load(&rotaryEncoderService.worker_exited));
    TEST_ASSERT_EQUAL(ESP_OK, RotaryEncoder_UnsubscribeRotation(NULL, blocked_rotation));
    TEST_ASSERT_EQUAL(0, RotaryEncoder_Stop(NULL));
    TEST_ASSERT_NULL(rotaryEncoderService.taskHandle);
    TEST_ASSERT_NULL(rotaryEncoderService.pcnt_unit);
    TEST_ASSERT_FALSE(rotaryEncoderService.button_attached);
    TEST_ASSERT_EQUAL_PTR(input, rotaryEncoderService.indev);
    TEST_ASSERT_EQUAL(0, RotaryEncoder_Start(NULL));
    RotaryEncoder_EnterCritical();
    rotaryEncoderService.status.absolute_position = 25;
    RotaryEncoder_ExitCritical();
    TEST_ASSERT_EQUAL(ESP_OK, RotaryEncoder_Reset(NULL));
    TEST_ASSERT_EQUAL(0, RotaryEncoder_GetAbsolutePosition(NULL));
    TEST_ASSERT_FALSE(rotaryEncoderService.control_active);
    uint32_t clicks = rotaryEncoderService.status.total_clicks;
    TEST_ASSERT_EQUAL(ESP_OK, RotaryEncoder_Simulate(RE_SIM_CLICK, 1));
    TEST_ASSERT_FALSE(rotaryEncoderService.status.button_held);
    TEST_ASSERT_EQUAL(clicks, rotaryEncoderService.status.total_clicks);
    TEST_ASSERT_EQUAL(0, RotaryEncoder_Stop(NULL));
}
