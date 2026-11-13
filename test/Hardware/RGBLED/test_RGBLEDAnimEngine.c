#include <unity.h>
#include "Hardware/RGBLED/RGBLEDTypes.h"
#include "Hardware/RGBLED/RGBLEDAnimations.h"
#include "Hardware/LEDAnimations/LEDAnimations.h"

static AnimationPlugin *modified_catalog;
static AnimationPlugin saved_catalog;

void setUp(void) {
    modified_catalog = NULL;
    RGBLEDAnimEngine_Init();
}

void tearDown(void) {
    if (modified_catalog) *modified_catalog = saved_catalog;
    RGBLEDAnimEngine_Deinit();
}

void test_AnimEngine_StartStop(void) {
    RGBColor base = {255, 0, 0};
    
    TEST_ASSERT_FALSE(RGBLEDAnimEngine_IsActive());
    
    esp_err_t err = RGBLEDAnimEngine_Start(RGBLED_ANIM_BREATHING, base, RGBLED_PRIO_LOW, 0);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_TRUE(RGBLEDAnimEngine_IsActive());
    TEST_ASSERT_EQUAL(RGBLED_ANIM_BREATHING, RGBLEDAnimEngine_GetCurrentType());
    
    err = RGBLEDAnimEngine_Stop();
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_FALSE(RGBLEDAnimEngine_IsActive());
}

void test_AnimEngine_Priority(void) {
    RGBColor base = {255, 0, 0};
    
    // Start low priority
    RGBLEDAnimEngine_Start(RGBLED_ANIM_BREATHING, base, RGBLED_PRIO_LOW, 0);
    TEST_ASSERT_EQUAL(RGBLED_PRIO_LOW, RGBLEDAnimEngine_GetCurrentPriority());
    
    // Try to overwrite with lower/same priority -> Should succeed since we don't have LOWER, but let's try medium
    RGBLEDAnimEngine_Start(RGBLED_ANIM_ALERT, base, RGBLED_PRIO_HIGH, 0);
    TEST_ASSERT_EQUAL(RGBLED_PRIO_HIGH, RGBLEDAnimEngine_GetCurrentPriority());
    TEST_ASSERT_EQUAL(RGBLED_ANIM_ALERT, RGBLEDAnimEngine_GetCurrentType());
    
    // Try to overwrite high with low -> Should fail
    esp_err_t err = RGBLEDAnimEngine_Start(RGBLED_ANIM_PULSE, base, RGBLED_PRIO_LOW, 0);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, err);
    TEST_ASSERT_EQUAL(RGBLED_ANIM_ALERT, RGBLEDAnimEngine_GetCurrentType()); // Should remain ALERT
}

void test_AnimEngine_Timeout(void) {
    RGBColor base = {255, 0, 0};
    RGBColor out;
    
    RGBLEDAnimEngine_Start(RGBLED_ANIM_PULSE, base, RGBLED_PRIO_LOW, 100);
    TEST_ASSERT_TRUE(RGBLEDAnimEngine_IsActive());
    
    // Tick 50ms (still active)
    bool updated = RGBLEDAnimEngine_Tick(50, &out);
    TEST_ASSERT_TRUE(updated);
    TEST_ASSERT_TRUE(RGBLEDAnimEngine_IsActive());
    
    // Tick another 60ms (110ms total, should timeout)
    updated = RGBLEDAnimEngine_Tick(60, &out);
    TEST_ASSERT_FALSE(RGBLEDAnimEngine_IsActive());
}

void test_AnimEngine_InvalidInputPreservesPlayback(void) {
    RGBColor base = {255,0,0}, out;
    TEST_ASSERT_EQUAL(ESP_OK, RGBLEDAnimEngine_Start(RGBLED_ANIM_PULSE, base, RGBLED_PRIO_LOW, 100));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, RGBLEDAnimEngine_Start((RGBLEDAnimType)-1, base, RGBLED_PRIO_HIGH, 0));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, RGBLEDAnimEngine_Start(RGBLED_ANIM_COUNT, base, RGBLED_PRIO_HIGH, 0));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, RGBLEDAnimEngine_Start(RGBLED_ANIM_ALERT, base, (RGBLEDPriority)-1, 0));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, RGBLEDAnimEngine_Start(RGBLED_ANIM_NONE, base, (RGBLEDPriority)99, 0));
    TEST_ASSERT_FALSE(RGBLEDAnimEngine_Tick(UINT32_MAX, NULL));
    TEST_ASSERT_EQUAL(RGBLED_ANIM_PULSE, RGBLEDAnimEngine_GetCurrentType());
    TEST_ASSERT_TRUE(RGBLEDAnimEngine_Tick(99, &out));
    TEST_ASSERT_FALSE(RGBLEDAnimEngine_Tick(1, &out));
}
void test_AnimEngine_TimeoutCannotWrap(void) {
    RGBColor out;
    TEST_ASSERT_EQUAL(ESP_OK, RGBLEDAnimEngine_Start(RGBLED_ANIM_PULSE, (RGBColor){255,0,0}, RGBLED_PRIO_LOW, UINT32_MAX));
    TEST_ASSERT_TRUE(RGBLEDAnimEngine_Tick(UINT32_MAX - 5U, &out));
    TEST_ASSERT_FALSE(RGBLEDAnimEngine_Tick(10, &out));
    TEST_ASSERT_FALSE(RGBLEDAnimEngine_IsActive());
}
void test_AnimEngine_PeriodicPhaseSurvivesLongGaps(void) {
    const RGBLEDAnimType types[] = {RGBLED_ANIM_BREATHING, RGBLED_ANIM_PULSE, RGBLED_ANIM_ALERT};
    const uint32_t periods[] = {3000, 500, 250};
    for (unsigned i = 0; i < 3; ++i) {
        RGBColor actual, expected;
        TEST_ASSERT_EQUAL(ESP_OK, RGBLEDAnimEngine_Start(types[i], (RGBColor){255,120,40}, RGBLED_PRIO_LOW, 0));
        TEST_ASSERT_TRUE(RGBLEDAnimEngine_Tick(UINT32_MAX, &actual));
        TEST_ASSERT_TRUE(RGBLEDAnimEngine_Tick(73, &actual));
        uint32_t phase = (uint32_t)(((uint64_t)UINT32_MAX + 73U) % periods[i]);
        TEST_ASSERT_EQUAL(ESP_OK, RGBLEDAnimEngine_Start(types[i], (RGBColor){255,120,40}, RGBLED_PRIO_LOW, 0));
        TEST_ASSERT_TRUE(RGBLEDAnimEngine_Tick(phase, &expected));
        TEST_ASSERT_EQUAL_MEMORY(&expected, &actual, sizeof(actual));
        for (unsigned cycle = 0; cycle < 100; ++cycle) {
            TEST_ASSERT_TRUE(RGBLEDAnimEngine_Tick(periods[i], &actual));
            TEST_ASSERT_EQUAL_MEMORY(&expected, &actual, sizeof(actual));
        }
    }
}

void test_AnimEngine_CommonDoubleBlinkDoesNotResetBuiltin(void) {
    AnimationPlugin *builtin = DoubleBlinkWhiteAnim_Get();
    builtin->Reset(builtin);
    TEST_ASSERT_EQUAL(0, builtin->Update(builtin, 80).r);
    TEST_ASSERT_EQUAL(ESP_OK, RGBLEDAnimEngine_Start(RGBLED_ANIM_DOUBLE_BLINK_WHITE,
                                                  (RGBColor){0}, RGBLED_PRIO_LOW, 0));
    RGBColor color;
    TEST_ASSERT_TRUE(RGBLEDAnimEngine_Tick(0, &color));
    TEST_ASSERT_EQUAL(255, color.r);
    TEST_ASSERT_EQUAL(0, builtin->Update(builtin, 0).r);
    TEST_ASSERT_TRUE(RGBLEDAnimEngine_Tick(80, &color));
    TEST_ASSERT_EQUAL(0, color.r);
    builtin->Reset(builtin);
    TEST_ASSERT_TRUE(RGBLEDAnimEngine_Tick(0, &color));
    TEST_ASSERT_EQUAL(0, color.r);
    TEST_ASSERT_TRUE(RGBLEDAnimEngine_Tick(120, &color));
    TEST_ASSERT_EQUAL(255, color.r);
}

void test_AnimEngine_CommonCatalogMatchesEveryNamedPlugin(void) {
    const RGBLEDAnimType external[] = {
        RGBLED_ANIM_HEARTBEAT_RED, RGBLED_ANIM_BLINK_WHITE, RGBLED_ANIM_SOS_RED,
        RGBLED_ANIM_BREATHING_GREEN, RGBLED_ANIM_FADE_BLUE, RGBLED_ANIM_STROBE_WHITE,
        RGBLED_ANIM_POLICE_STROBE, RGBLED_ANIM_SOLID_RED, RGBLED_ANIM_SOLID_GREEN,
        RGBLED_ANIM_SOLID_BLUE, RGBLED_ANIM_SOLID_WHITE, RGBLED_ANIM_BLINK_RED,
        RGBLED_ANIM_BREATHING_RED, RGBLED_ANIM_DOUBLE_BLINK_WHITE
    };
    const BuiltinLEDAnimationType builtin[] = {
        HEARTBEAT_RED, BLINK_WHITE, SOS_RED, BREATHING_GREEN, FADE_BLUE, STROBE_WHITE,
        POLICE_STROBE, SOLID_RED, SOLID_GREEN, SOLID_BLUE, SOLID_WHITE, BLINK_RED,
        BREATHING_RED, DOUBLE_BLINK_WHITE
    };
    for (unsigned index = 0; index < sizeof(external)/sizeof(external[0]); ++index) {
        LEDAnimationInstance reference;
        TEST_ASSERT_EQUAL(ESP_OK, LEDAnimations_CreateInstance(builtin[index], &reference));
        reference.plugin.render_brightness = 255;
        TEST_ASSERT_EQUAL(ESP_OK, RGBLEDAnimEngine_Start(external[index],
                                                      (RGBColor){0}, RGBLED_PRIO_LOW, 0));
        for (unsigned frame = 0; frame < 200; ++frame) {
            RGBColor color;
            TEST_ASSERT_TRUE(RGBLEDAnimEngine_Tick(37, &color));
            LEDColor expected = reference.plugin.Update(&reference.plugin, 37);
            TEST_ASSERT_EQUAL(expected.r, color.r);
            TEST_ASSERT_EQUAL(expected.g, color.g);
            TEST_ASSERT_EQUAL(expected.b, color.b);
        }
    }
}

void test_AnimEngine_CommonDoubleBlinkHundredCycles(void) {
    TEST_ASSERT_EQUAL(ESP_OK, RGBLEDAnimEngine_Start(RGBLED_ANIM_DOUBLE_BLINK_WHITE,
                                                  (RGBColor){0}, RGBLED_PRIO_LOW, 0));
    bool previous = true;
    unsigned rises = 0, falls = 0;
    for (unsigned frame = 1; frame <= 20000; ++frame) {
        RGBColor color;
        TEST_ASSERT_TRUE(RGBLEDAnimEngine_Tick(10, &color));
        bool on = color.r != 0;
        TEST_ASSERT_EQUAL(on ? 255 : 0, color.r);
        TEST_ASSERT_EQUAL(color.r, color.g);
        TEST_ASSERT_EQUAL(color.r, color.b);
        if (on && !previous) ++rises;
        if (!on && previous) ++falls;
        previous = on;
    }
    TEST_ASSERT_EQUAL(200, rises);
    TEST_ASSERT_EQUAL(200, falls);
}

void test_AnimEngine_CatalogFailurePreservesPhaseAndPriority(void) {
    TEST_ASSERT_EQUAL(ESP_OK, RGBLEDAnimEngine_Start(RGBLED_ANIM_DOUBLE_BLINK_WHITE,
                                                  (RGBColor){0}, RGBLED_PRIO_LOW, 0));
    RGBColor color;
    TEST_ASSERT_TRUE(RGBLEDAnimEngine_Tick(40, &color));
    TEST_ASSERT_EQUAL(255, color.r);
    modified_catalog = DoubleBlinkWhiteAnim_Get();
    saved_catalog = *modified_catalog;
    modified_catalog->context_size = LED_ANIMATION_CONTEXT_CAPACITY + 1;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE,
                     RGBLEDAnimEngine_Start(RGBLED_ANIM_DOUBLE_BLINK_WHITE,
                                            (RGBColor){0}, RGBLED_PRIO_HIGH, 0));
    TEST_ASSERT_EQUAL(RGBLED_PRIO_LOW, RGBLEDAnimEngine_GetCurrentPriority());
    TEST_ASSERT_EQUAL(RGBLED_ANIM_DOUBLE_BLINK_WHITE, RGBLEDAnimEngine_GetCurrentType());
    TEST_ASSERT_TRUE(RGBLEDAnimEngine_Tick(40, &color));
    TEST_ASSERT_EQUAL(0, color.r);
}

void test_AnimEngine_FullScaleCatalogColorsSaturateWithoutWrapping(void) {
    RGBColor color;
    TEST_ASSERT_EQUAL(ESP_OK, RGBLEDAnimEngine_Start(RGBLED_ANIM_POLICE_STROBE,
                                                  (RGBColor){0}, RGBLED_PRIO_LOW, 0));
    TEST_ASSERT_TRUE(RGBLEDAnimEngine_Tick(0, &color));
    TEST_ASSERT_EQUAL(255, color.r);
    TEST_ASSERT_EQUAL(0, color.b);
    TEST_ASSERT_EQUAL(ESP_OK, RGBLEDAnimEngine_Start(RGBLED_ANIM_STROBE_WHITE,
                                                  (RGBColor){0}, RGBLED_PRIO_LOW, 0));
    TEST_ASSERT_TRUE(RGBLEDAnimEngine_Tick(0, &color));
    TEST_ASSERT_EQUAL(255, color.r);
    TEST_ASSERT_EQUAL(255, color.g);
    TEST_ASSERT_EQUAL(255, color.b);
    AnimationPlugin *builtin = SolidWhiteAnim_Get();
    LEDColor original = builtin->Update(builtin, 0);
    TEST_ASSERT_EQUAL(BUILTIN_LED_DEFAULT_BRIGHTNESS, original.r);
}

void test_AnimEngine_ReservedTemperatureSelectionCannotReplacePlayback(void) {
    TEST_ASSERT_EQUAL(ESP_OK, RGBLEDAnimEngine_Start(RGBLED_ANIM_DOUBLE_BLINK_WHITE,
        (RGBColor){255,255,255}, RGBLED_PRIO_LOW, 0));
    RGBColor color;
    TEST_ASSERT_TRUE(RGBLEDAnimEngine_Tick(40, &color));
    TEST_ASSERT_EQUAL(255, color.r);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, RGBLEDAnimEngine_Start(RGBLED_ANIM_TEMPERATURE,
        (RGBColor){255,0,0}, RGBLED_PRIO_HIGH, 0));
    TEST_ASSERT_EQUAL(RGBLED_ANIM_DOUBLE_BLINK_WHITE, RGBLEDAnimEngine_GetCurrentType());
    TEST_ASSERT_TRUE(RGBLEDAnimEngine_Tick(40, &color));
    TEST_ASSERT_EQUAL(0, color.r);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_AnimEngine_FullScaleCatalogColorsSaturateWithoutWrapping);
    RUN_TEST(test_AnimEngine_ReservedTemperatureSelectionCannotReplacePlayback);
    RUN_TEST(test_AnimEngine_CommonDoubleBlinkDoesNotResetBuiltin);
    RUN_TEST(test_AnimEngine_CommonCatalogMatchesEveryNamedPlugin);
    RUN_TEST(test_AnimEngine_CommonDoubleBlinkHundredCycles);
    RUN_TEST(test_AnimEngine_CatalogFailurePreservesPhaseAndPriority);

    RUN_TEST(test_AnimEngine_InvalidInputPreservesPlayback);
    RUN_TEST(test_AnimEngine_TimeoutCannotWrap);
    RUN_TEST(test_AnimEngine_PeriodicPhaseSurvivesLongGaps);
    RUN_TEST(test_AnimEngine_StartStop);
    RUN_TEST(test_AnimEngine_Priority);
    RUN_TEST(test_AnimEngine_Timeout);
    return UNITY_END();
}
