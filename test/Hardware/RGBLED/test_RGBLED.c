// Target device test suite for RGBLED module.
// Compiled only when RACOOL_TEST_NATIVE is NOT defined (esp32-c6-devkitc-1_test env).
#ifndef RACOOL_TEST_NATIVE

#include <unity.h>
#include "Hardware/RGBLED/IRGBLED.h"
#include "Core/DI/IDIRegistry.h"
#include "Core/ServiceLocator/ServiceLocator.h"
#include "Core/ServiceLocator/ServiceNames.h"

// Include source implementations directly so they link in the test binary.
// Redirect static 'TAG' per file to avoid single-TU redefinition errors.
#define TAG TAG_rgbled
#include "../../../src/Hardware/RGBLED/RGBLED.c"
#undef TAG

#define TAG TAG_rgbled_hw
#include "../../../src/Hardware/RGBLED/RGBLEDHardware.c"
#undef TAG

#include "../../../src/Hardware/RGBLED/RGBLEDColorEngine.c"

#define TAG TAG_rgbled_anim
#include "../../../src/Hardware/RGBLED/RGBLEDAnimEngine.c"
#undef TAG

#define TAG TAG_rgbled_cfg
#include "../../../src/Hardware/RGBLED/RGBLEDConfig.c"
#undef TAG

void TargetRGB_SetBrightnessConfig(uint8_t brightness);
void setUp(void) { TargetRGB_SetBrightnessConfig(255); }
void tearDown(void) { TEST_ASSERT_EQUAL(ESP_OK, GetRGBLEDService()->base.Stop((IService *)GetRGBLEDService())); }

void test_RGBLED_Lifecycle(void) {
    IRGBLED *service = GetRGBLEDService();
    TEST_ASSERT_NOT_NULL(service);
    TEST_ASSERT_NOT_NULL(service->base.Start);
    TEST_ASSERT_NOT_NULL(service->base.Stop);

    int32_t res = service->base.Start((IService*)service);
    TEST_ASSERT_EQUAL(0, res);

    res = service->base.Stop((IService*)service);
    TEST_ASSERT_EQUAL(0, res);
}

void test_RGBLED_SetColor(void) {
    IRGBLED *service = GetRGBLEDService();
    TEST_ASSERT_EQUAL(ESP_OK, service->base.Start((IService*)service));

    RGBColor color = {255, 128, 0};
    esp_err_t err = service->SetColorRGB(service, color);
    TEST_ASSERT_EQUAL(ESP_OK, err);

    vTaskDelay(pdMS_TO_TICKS(400));

    RGBLEDState state;
    TEST_ASSERT_EQUAL(ESP_OK, service->GetLEDState(service, &state));

    TEST_ASSERT_EQUAL_UINT8(255, state.targetColor.r);
    TEST_ASSERT_EQUAL_UINT8(128, state.targetColor.g);
    TEST_ASSERT_EQUAL_UINT8(0,   state.targetColor.b);
    TEST_ASSERT_TRUE(state.isOn);

    TEST_ASSERT_EQUAL(ESP_OK, service->base.Stop((IService*)service));
}

void test_RGBLED_Fade(void) {
    IRGBLED *service = GetRGBLEDService();
    TEST_ASSERT_EQUAL(ESP_OK, service->base.Start((IService*)service));

    RGBColor target = {0, 255, 128};
    esp_err_t err = service->FadeToColor(service, target, 100);
    TEST_ASSERT_EQUAL(ESP_OK, err);

    vTaskDelay(pdMS_TO_TICKS(400));

    RGBLEDState state;
    TEST_ASSERT_EQUAL(ESP_OK, service->GetLEDState(service, &state));

    TEST_ASSERT_EQUAL_UINT8(0,   state.targetColor.r);
    TEST_ASSERT_EQUAL_UINT8(255, state.targetColor.g);
    TEST_ASSERT_EQUAL_UINT8(128, state.targetColor.b);

    TEST_ASSERT_EQUAL(ESP_OK, service->base.Stop((IService*)service));
}

void test_RGBLED_Brightness(void) {
    IRGBLED *service = GetRGBLEDService();
    TEST_ASSERT_EQUAL(ESP_OK, service->base.Start((IService*)service));

    esp_err_t err = service->SetBrightness(service, 150);
    TEST_ASSERT_EQUAL(ESP_OK, err);

    vTaskDelay(pdMS_TO_TICKS(400));

    RGBLEDState state;
    TEST_ASSERT_EQUAL(ESP_OK, service->GetLEDState(service, &state));
    TEST_ASSERT_EQUAL_UINT8(150, state.brightness);

    TEST_ASSERT_EQUAL(ESP_OK, service->base.Stop((IService*)service));
}

void test_RGBLED_Animations(void) {
    IRGBLED *service = GetRGBLEDService();
    TEST_ASSERT_EQUAL(ESP_OK, service->base.Start((IService*)service));

    esp_err_t err = service->StartAnimation(service, RGBLED_ANIM_BREATHING, RGBLED_PRIO_HIGH, 1000);
    TEST_ASSERT_EQUAL(ESP_OK, err);

    vTaskDelay(pdMS_TO_TICKS(400));

    RGBLEDState state;
    TEST_ASSERT_EQUAL(ESP_OK, service->GetLEDState(service, &state));
    TEST_ASSERT_EQUAL(RGBLED_COLOR_MODE_ANIMATION, state.colorMode);
    TEST_ASSERT_EQUAL(RGBLED_ANIM_BREATHING, state.activeAnim);
    TEST_ASSERT_EQUAL(RGBLED_PRIO_HIGH, state.animPriority);

    TEST_ASSERT_EQUAL(ESP_OK, service->base.Stop((IService*)service));
}

void test_RGBLED_TransitionAndBrightnessPreserveManualColorOnRealLedc(void) {
    IRGBLED *service = GetRGBLEDService();
    TEST_ASSERT_EQUAL(ESP_OK, service->base.Start(&service->base));
    TEST_ASSERT_EQUAL(ESP_OK, service->SetColorRGB(service, (RGBColor){255,0,0}));
    /* Sample mid-transition: gamma-corrected 8-bit PWM can remain zero
     * near the beginning even though the logical fade has started. */
    vTaskDelay(pdMS_TO_TICKS(170));
    RGBLEDState state;
    TEST_ASSERT_EQUAL(ESP_OK, service->GetLEDState(service, &state));
    TEST_ASSERT_TRUE(state.isFading);
    uint32_t intermediate = ledc_get_duty(LEDC_LOW_SPEED_MODE, RGBLED_LEDC_CHANNEL_R);
    TEST_ASSERT_GREATER_THAN(0, intermediate);
    TEST_ASSERT_LESS_THAN(255, intermediate);
    vTaskDelay(pdMS_TO_TICKS(350));
    TEST_ASSERT_EQUAL(ESP_OK, service->GetLEDState(service, &state));
    TEST_ASSERT_FALSE(state.isFading);
    TEST_ASSERT_EQUAL(255, ledc_get_duty(LEDC_LOW_SPEED_MODE, RGBLED_LEDC_CHANNEL_R));
    TargetRGB_SetBrightnessConfig(37);
    OnSettingsChanged((BusEvent){0});
    vTaskDelay(pdMS_TO_TICKS(350));
    TEST_ASSERT_EQUAL(ESP_OK, service->GetLEDState(service, &state));
    TEST_ASSERT_EQUAL(RGBLED_COLOR_MODE_RGB, state.colorMode);
    TEST_ASSERT_EQUAL(255, state.targetColor.r);
    TEST_ASSERT_EQUAL(37, state.brightness);
    TEST_ASSERT_EQUAL(37, ledc_get_duty(LEDC_LOW_SPEED_MODE, RGBLED_LEDC_CHANNEL_R));
    TEST_ASSERT_EQUAL(ESP_OK, service->ApplySelectedMode(service));
    vTaskDelay(pdMS_TO_TICKS(350));
    TEST_ASSERT_EQUAL(ESP_OK, service->GetLEDState(service, &state));
    TEST_ASSERT_EQUAL(RGBLED_COLOR_MODE_TEMPERATURE, state.colorMode);
    TEST_ASSERT_FALSE(state.isOn);
    TEST_ASSERT_EQUAL(0, ledc_get_duty(LEDC_LOW_SPEED_MODE, RGBLED_LEDC_CHANNEL_R));
}

void test_RGBLED_CooperativeStopPreservesBlockedWorker(void) {
    IRGBLED *service = GetRGBLEDService();
    TEST_ASSERT_EQUAL(0, service->base.Start(&service->base));
    TEST_ASSERT_EQUAL(pdTRUE, xSemaphoreTakeRecursive(instance.stateMutex, pdMS_TO_TICKS(1000)));
    /* Give the worker time to reach the held real mutex. */
    vTaskDelay(pdMS_TO_TICKS(100));
    TaskHandle_t task = atomic_load(&instance.worker.task);
    QueueHandle_t queue = instance.cmdQueue;
    int32_t stopped = service->base.Stop(&service->base);
    xSemaphoreGiveRecursive(instance.stateMutex);
    TEST_ASSERT_NOT_EQUAL(0, stopped);
    TEST_ASSERT_EQUAL_PTR(task, atomic_load(&instance.worker.task));
    TEST_ASSERT_EQUAL_PTR(queue, instance.cmdQueue);
    TEST_ASSERT_NOT_EQUAL(ESP_OK, service->SetBrightness(service, 12));
    TEST_ASSERT_NOT_EQUAL(0, service->base.Start(&service->base));
    TEST_ASSERT_EQUAL(0, service->base.Stop(&service->base));
    TEST_ASSERT_NULL(atomic_load(&instance.worker.task));
    TEST_ASSERT_EQUAL(0, service->base.Start(&service->base));
    TEST_ASSERT_EQUAL(0, service->base.Stop(&service->base));
}

void test_RGBLED_SmoothPatternsAndWarmCandleOnTarget(void) {
    const RGBLEDAnimType types[] = {RGBLED_ANIM_BREATHING_RED, RGBLED_ANIM_BREATHING, RGBLED_ANIM_FADE_IN_OUT};
    for (unsigned kind = 0; kind < sizeof(types)/sizeof(types[0]); ++kind) {
        uint32_t period = kind == 0 ? 4000U : 3000U;
        TEST_ASSERT_EQUAL(ESP_OK, RGBLEDAnimEngine_Start(types[kind], (RGBColor){255,255,255}, RGBLED_PRIO_HIGH, 0));
        RGBColor previous;
        TEST_ASSERT_TRUE(RGBLEDAnimEngine_Tick(0, &previous));
        for (unsigned cycle = 0; cycle < 100; ++cycle) {
            for (uint32_t time = 20; time <= period; time += 20) {
                RGBColor next;
                TEST_ASSERT_TRUE(RGBLEDAnimEngine_Tick(20, &next));
                if (time <= period / 2) TEST_ASSERT_TRUE(next.r >= previous.r);
                else TEST_ASSERT_TRUE(next.r <= previous.r);
                previous = next;
            }
            vTaskDelay(1); /* Let idle/SDK tasks run during the bounded simulation. */
        }
        TEST_ASSERT_EQUAL(ESP_OK, RGBLEDAnimEngine_Stop());
    }
    TEST_ASSERT_EQUAL(ESP_OK, RGBLEDAnimEngine_Start(RGBLED_ANIM_CANDLE, (RGBColor){255,255,255}, RGBLED_PRIO_HIGH, 0));
    for (unsigned frame = 0; frame < 15000; ++frame) {
        RGBColor next;
        TEST_ASSERT_TRUE(RGBLEDAnimEngine_Tick(20, &next));
        TEST_ASSERT_TRUE(next.r > next.g && next.g > next.b);
        if ((frame & 63U) == 0) vTaskDelay(1);
    }
    TEST_ASSERT_EQUAL(ESP_OK, RGBLEDAnimEngine_Stop());
}

void test_RGBLED_ColorPreviewRestoresOutputAndRefreshesDeadlineOnRealLedc(void) {
    IRGBLED *service=GetRGBLEDService();
    TEST_ASSERT_EQUAL(ESP_OK,service->base.Start(&service->base));
    TEST_ASSERT_EQUAL(ESP_OK,service->SetColorRGB(service,(RGBColor){0,0,255}));
    vTaskDelay(pdMS_TO_TICKS(400));
    TEST_ASSERT_EQUAL(255,ledc_get_duty(LEDC_LOW_SPEED_MODE,RGBLED_LEDC_CHANNEL_B));
    TEST_ASSERT_EQUAL(ESP_OK,service->PreviewColor(service,(RGBColor){255,0,0},1000));
    vTaskDelay(pdMS_TO_TICKS(400));
    RGBLEDState state;
    TEST_ASSERT_EQUAL(ESP_OK,service->GetLEDState(service,&state));
    TEST_ASSERT_TRUE(state.previewActive);
    TEST_ASSERT_EQUAL(255,state.targetColor.b);
    TEST_ASSERT_EQUAL(255,ledc_get_duty(LEDC_LOW_SPEED_MODE,RGBLED_LEDC_CHANNEL_R));
    TEST_ASSERT_EQUAL(0,ledc_get_duty(LEDC_LOW_SPEED_MODE,RGBLED_LEDC_CHANNEL_B));
    TEST_ASSERT_EQUAL(ESP_OK,service->PreviewColor(service,(RGBColor){0,255,0},700));
    vTaskDelay(pdMS_TO_TICKS(400));
    TEST_ASSERT_EQUAL(255,ledc_get_duty(LEDC_LOW_SPEED_MODE,RGBLED_LEDC_CHANNEL_G));
    vTaskDelay(pdMS_TO_TICKS(700));
    TEST_ASSERT_EQUAL(ESP_OK,service->GetLEDState(service,&state));
    TEST_ASSERT_FALSE(state.previewActive);
    TEST_ASSERT_EQUAL(255,state.targetColor.b);
    TEST_ASSERT_EQUAL(255,ledc_get_duty(LEDC_LOW_SPEED_MODE,RGBLED_LEDC_CHANNEL_B));
    TEST_ASSERT_EQUAL(0,ledc_get_duty(LEDC_LOW_SPEED_MODE,RGBLED_LEDC_CHANNEL_G));
    TEST_ASSERT_EQUAL(ESP_OK,service->PreviewAnimation(service,RGBLED_ANIM_SOLID_RED,600));
    vTaskDelay(pdMS_TO_TICKS(400));
    TEST_ASSERT_EQUAL(255,ledc_get_duty(LEDC_LOW_SPEED_MODE,RGBLED_LEDC_CHANNEL_R));
    TEST_ASSERT_EQUAL(0,ledc_get_duty(LEDC_LOW_SPEED_MODE,RGBLED_LEDC_CHANNEL_B));
    vTaskDelay(pdMS_TO_TICKS(700));
    TEST_ASSERT_EQUAL(255,ledc_get_duty(LEDC_LOW_SPEED_MODE,RGBLED_LEDC_CHANNEL_B));
    TEST_ASSERT_EQUAL(ESP_OK,service->PreviewAnimation(service,RGBLED_ANIM_NONE,600));
    vTaskDelay(pdMS_TO_TICKS(400));
    TEST_ASSERT_EQUAL(0,ledc_get_duty(LEDC_LOW_SPEED_MODE,RGBLED_LEDC_CHANNEL_B));
    vTaskDelay(pdMS_TO_TICKS(700));
    TEST_ASSERT_EQUAL(255,ledc_get_duty(LEDC_LOW_SPEED_MODE,RGBLED_LEDC_CHANNEL_B));
}

void app_main(void) {
    // Allow the serial monitor to attach before emitting any Unity results.
    vTaskDelay(pdMS_TO_TICKS(10000));
    printf("TARGET_RGB_BEGIN\n");
    fflush(stdout);
    UNITY_BEGIN();
    RUN_TEST(test_RGBLED_CooperativeStopPreservesBlockedWorker);
    RUN_TEST(test_RGBLED_ColorPreviewRestoresOutputAndRefreshesDeadlineOnRealLedc);
    RUN_TEST(test_RGBLED_Lifecycle);
    RUN_TEST(test_RGBLED_SetColor);
    RUN_TEST(test_RGBLED_Fade);
    RUN_TEST(test_RGBLED_Brightness);
    RUN_TEST(test_RGBLED_Animations);
    RUN_TEST(test_RGBLED_TransitionAndBrightnessPreserveManualColorOnRealLedc);
    RUN_TEST(test_RGBLED_SmoothPatternsAndWarmCandleOnTarget);
    UNITY_END();
}

#endif // !RACOOL_TEST_NATIVE
