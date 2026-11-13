#include "unity.h"
#define ServiceLocator_GetService BuiltinTarget_GetService
#define ServiceLocator_GetEventBus BuiltinTarget_GetEventBus
#define led_strip_new_spi_device BuiltinTarget_NewStrip
#define led_strip_set_pixel BuiltinTarget_SetPixel
#define led_strip_refresh BuiltinTarget_Refresh
#define led_strip_clear BuiltinTarget_Clear
#define led_strip_del BuiltinTarget_Delete
#include "../../../../src/Hardware/BuiltinLED/BuiltinLED.c"
#undef ServiceLocator_GetService
#undef ServiceLocator_GetEventBus
#undef led_strip_new_spi_device
#undef led_strip_set_pixel
#undef led_strip_refresh
#undef led_strip_clear
#undef led_strip_del
#include "../../../../src/Hardware/LEDAnimations/LEDTransition.c"
#include "../../../../src/Hardware/LEDAnimations/LEDAnimationInstances.c"

static SemaphoreHandle_t pluginRelease;
static atomic_bool pluginEntered;
static bool initialized;
static unsigned driverDeletes;
IService *BuiltinTarget_GetService(const char *name) { (void)name; return NULL; }
IEventBus *BuiltinTarget_GetEventBus(void) { return NULL; }
esp_err_t BuiltinTarget_NewStrip(const led_strip_config_t *cfg, const led_strip_spi_config_t *spi,
    led_strip_handle_t *out) { (void)cfg; (void)spi; *out = (led_strip_handle_t)1; return ESP_OK; }
esp_err_t BuiltinTarget_SetPixel(led_strip_handle_t s, uint32_t i, uint32_t r, uint32_t g, uint32_t b) {
    (void)s; (void)i; (void)r; (void)g; (void)b; return ESP_OK;
}
esp_err_t BuiltinTarget_Refresh(led_strip_handle_t s) { (void)s; return ESP_OK; }
esp_err_t BuiltinTarget_Clear(led_strip_handle_t s) { (void)s; return ESP_OK; }
esp_err_t BuiltinTarget_Delete(led_strip_handle_t s) { (void)s; driverDeletes++; return ESP_OK; }
static void reset_plugin(AnimationPlugin *self) { (void)self; }
static LEDColor blocked_update(AnimationPlugin *self, uint32_t delta) {
    (void)self; (void)delta;
    atomic_store(&pluginEntered, true);
    xSemaphoreTake(pluginRelease, portMAX_DELAY);
    xSemaphoreGive(pluginRelease);
    return (LEDColor){0};
}
static AnimationPlugin plugin = {.type = BLINK_WHITE, .name = "Blocked test plugin", .Reset = reset_plugin, .Update = blocked_update};
AnimationPlugin *BlinkWhiteAnim_Get(void) { return &plugin; }
AnimationPlugin *HeartbeatAnim_Get(void) { return NULL; }
AnimationPlugin *SOSRedAnim_Get(void) { return NULL; }
AnimationPlugin *BreathingGreenAnim_Get(void) { return NULL; }
AnimationPlugin *FadeBlueAnim_Get(void) { return NULL; }
AnimationPlugin *StrobeWhiteAnim_Get(void) { return NULL; }
AnimationPlugin *RainbowFlowAnim_Get(void) { return NULL; }
/* This lifecycle fixture has no Rainbow player or synchronized boot epoch. */
bool RainbowFlowAnim_IsSynchronizedPhase(void) { return false; }
AnimationPlugin *PoliceStrobeAnim_Get(void) { return NULL; }
AnimationPlugin *SolidRedAnim_Get(void) { return NULL; }
AnimationPlugin *SolidGreenAnim_Get(void) { return NULL; }
AnimationPlugin *SolidBlueAnim_Get(void) { return NULL; }
AnimationPlugin *SolidWhiteAnim_Get(void) { return NULL; }
AnimationPlugin *BlinkRedAnim_Get(void) { return NULL; }
AnimationPlugin *BreathingRedAnim_Get(void) { return NULL; }
AnimationPlugin *DoubleBlinkWhiteAnim_Get(void) { return NULL; }

void target_builtin_worker_cleanup(void) {
    if (pluginRelease) xSemaphoreGive(pluginRelease);
    if (initialized) {
        TEST_ASSERT_EQUAL(0, BuiltinLED_Stop(&builtinLEDService.base.base));
        initialized = false;
    }
    if (ledTrace.pages[0]) {
        char *stop[] = {"builtin_led_trace", "stop"};
        TEST_ASSERT_EQUAL(0, cmd_builtin_led_trace(2, stop));
    }
    if (pluginRelease) { vSemaphoreDelete(pluginRelease); pluginRelease = NULL; }
}
void target_builtin_worker_verify_shutdown(void) {
    IBuiltinLED *led = BuiltinLED_GetModule();
    TEST_ASSERT_EQUAL(0, led->base.Initialize(&led->base, NULL));
    char *traceStart[] = {"builtin_led_trace", "start"};
    char *traceStatus[] = {"builtin_led_trace", "status"};
    char *traceStop[] = {"builtin_led_trace", "stop"};
    TEST_ASSERT_EQUAL(0, cmd_builtin_led_trace(2, traceStart));
    BuiltinLEDTraceRow *ownedRows = ledTrace.pages[0];
    TEST_ASSERT_EQUAL(0, cmd_builtin_led_trace(2, traceStatus));
    TEST_ASSERT_EQUAL(1, cmd_builtin_led_trace(2, traceStart));
    TEST_ASSERT_EQUAL_PTR(ownedRows, ledTrace.pages[0]);
    TEST_ASSERT_EQUAL(0, cmd_builtin_led_trace(2, traceStop));
    pluginRelease = xSemaphoreCreateBinary();
    TEST_ASSERT_NOT_NULL(pluginRelease);
    TEST_ASSERT_EQUAL(0, led->base.Start(&led->base));
    initialized = true;
    for (unsigned i = 0; i < 100 && !atomic_load(&pluginEntered); ++i) vTaskDelay(pdMS_TO_TICKS(10));
    TEST_ASSERT_TRUE(atomic_load(&pluginEntered));
    TaskHandle_t task = atomic_load(&builtinLEDService.worker.task);
    TEST_ASSERT_NOT_EQUAL(0, led->base.Stop(&led->base));
    TEST_ASSERT_EQUAL_PTR(task, atomic_load(&builtinLEDService.worker.task));
    TEST_ASSERT_NOT_NULL(builtinLEDService.led_strip);
    TEST_ASSERT_EQUAL(0, driverDeletes);
    TEST_ASSERT_NOT_EQUAL(ESP_OK, led->TurnOn(led));
    TEST_ASSERT_NOT_EQUAL(0, led->base.Start(&led->base));
    xSemaphoreGive(pluginRelease);
    TEST_ASSERT_EQUAL(0, led->base.Stop(&led->base));
    initialized = false;
    TEST_ASSERT_NULL(atomic_load(&builtinLEDService.worker.task));
    TEST_ASSERT_EQUAL(1, driverDeletes);
    TEST_ASSERT_EQUAL(0, led->base.Start(&led->base));
    initialized = true;
    TEST_ASSERT_EQUAL(0, led->base.Stop(&led->base));
    initialized = false;
    TEST_ASSERT_EQUAL(2, driverDeletes);
}

#include "../../../../src/Hardware/LEDAnimations/LEDAnimationPatterns.c"
#include "../../../../src/Hardware/LEDAnimations/GenericPatternsAnim.c"
