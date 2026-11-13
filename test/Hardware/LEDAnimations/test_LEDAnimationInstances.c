#include "unity.h"
#include "Hardware/LEDAnimations/LEDAnimations.h"
#include "Hardware/LEDAnimations/LEDAnimationPatterns.h"
#include <string.h>

static AnimationPlugin *modified;
static AnimationPlugin saved_metadata;
void setUp(void) { modified = NULL; }
void tearDown(void) {
    if (modified) *modified = saved_metadata;
}

void test_LEDInstances_AllCatalogEntriesHaveOwnedState(void) {
    for (int type = 0; type < BUILTIN_LED_ANIM_COUNT; ++type) {
        LEDAnimationInstance first, second;
        AnimationPlugin *source = LEDAnimations_GetTemplate(type);
        TEST_ASSERT_NOT_NULL(source);
        TEST_ASSERT_EQUAL(type, source->type);
        TEST_ASSERT_EQUAL(ESP_OK, LEDAnimations_CreateInstance(type, &first));
        TEST_ASSERT_EQUAL(ESP_OK, LEDAnimations_CreateInstance(type, &second));
        TEST_ASSERT_EQUAL_PTR(source->Update, first.plugin.Update);
        if (source->context) {
            TEST_ASSERT_TRUE(source->context != first.plugin.context);
            TEST_ASSERT_TRUE(first.plugin.context != second.plugin.context);
        } else {
            TEST_ASSERT_NULL(first.plugin.context);
        }
    }
}

void test_LEDInstances_ResetDoesNotChangeOtherPlayersOrTemplate(void) {
    AnimationPlugin *source = DoubleBlinkWhiteAnim_Get();
    source->Reset(source);
    TEST_ASSERT_EQUAL(0, source->Update(source, 80).r);
    LEDAnimationInstance first, second;
    TEST_ASSERT_EQUAL(ESP_OK, LEDAnimations_CreateInstance(DOUBLE_BLINK_WHITE, &first));
    TEST_ASSERT_EQUAL(ESP_OK, LEDAnimations_CreateInstance(DOUBLE_BLINK_WHITE, &second));
    TEST_ASSERT_EQUAL(BUILTIN_LED_DEFAULT_BRIGHTNESS, first.plugin.Update(&first.plugin, 0).r);
    TEST_ASSERT_EQUAL(0, first.plugin.Update(&first.plugin, 80).r);
    TEST_ASSERT_EQUAL(BUILTIN_LED_DEFAULT_BRIGHTNESS, second.plugin.Update(&second.plugin, 0).r);
    second.plugin.Reset(&second.plugin);
    TEST_ASSERT_EQUAL(0, first.plugin.Update(&first.plugin, 0).r);
    TEST_ASSERT_EQUAL(0, source->Update(source, 0).r);
}

void test_LEDInstances_InvalidInputPreservesDestination(void) {
    LEDAnimationInstance instance, before;
    memset(&instance, 0xa5, sizeof(instance));
    memcpy(&before, &instance, sizeof(instance));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, LEDAnimations_CreateInstance(BUILTIN_LED_ANIM_NONE, &instance));
    TEST_ASSERT_EQUAL_MEMORY(&before, &instance, sizeof(instance));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, LEDAnimations_CreateInstance(BUILTIN_LED_ANIM_COUNT, &instance));
    TEST_ASSERT_EQUAL_MEMORY(&before, &instance, sizeof(instance));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, LEDAnimations_CreateInstance(HEARTBEAT_RED, NULL));
    TEST_ASSERT_NULL(LEDAnimations_GetTemplate(BUILTIN_LED_ANIM_NONE));
}

void test_LEDInstances_InconsistentMetadataIsRejected(void) {
    modified = DoubleBlinkWhiteAnim_Get();
    saved_metadata = *modified;
    LEDAnimationInstance instance, before;
    memset(&instance, 0xa5, sizeof(instance));
    memcpy(&before, &instance, sizeof(instance));
    modified->context_size = LED_ANIMATION_CONTEXT_CAPACITY + 1;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, LEDAnimations_CreateInstance(DOUBLE_BLINK_WHITE, &instance));
    TEST_ASSERT_EQUAL_MEMORY(&before, &instance, sizeof(instance));
    *modified = saved_metadata;
    modified->context = NULL;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, LEDAnimations_CreateInstance(DOUBLE_BLINK_WHITE, &instance));
    *modified = saved_metadata;
    modified->Update = NULL;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, LEDAnimations_CreateInstance(DOUBLE_BLINK_WHITE, &instance));
    *modified = saved_metadata;
    modified->type = BLINK_WHITE;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, LEDAnimations_CreateInstance(DOUBLE_BLINK_WHITE, &instance));
}

void test_LEDInstances_DoubleBlinkHundredCycles(void) {
    LEDAnimationInstance first, second;
    TEST_ASSERT_EQUAL(ESP_OK, LEDAnimations_CreateInstance(DOUBLE_BLINK_WHITE, &first));
    TEST_ASSERT_EQUAL(ESP_OK, LEDAnimations_CreateInstance(DOUBLE_BLINK_WHITE, &second));
    bool previous = true;
    unsigned rises = 0, falls = 0;
    for (unsigned step = 1; step <= 20000; ++step) {
        LEDColor a = first.plugin.Update(&first.plugin, 10);
        second.plugin.Update(&second.plugin, 3);
        LEDColor b = second.plugin.Update(&second.plugin, 7);
        TEST_ASSERT_EQUAL_MEMORY(&a, &b, sizeof(a));
        bool on = a.r != 0;
        if (on && !previous) ++rises;
        if (!on && previous) ++falls;
        previous = on;
    }
    TEST_ASSERT_EQUAL(200, rises);
    TEST_ASSERT_EQUAL(200, falls);
}

void test_LEDInstances_AllPlayersRemainIndependentWhenPeerResets(void) {
    for (int type = 0; type < BUILTIN_LED_ANIM_COUNT; ++type) {
        LEDAnimationInstance first, second, reference;
        TEST_ASSERT_EQUAL(ESP_OK, LEDAnimations_CreateInstance(type, &first));
        TEST_ASSERT_EQUAL(ESP_OK, LEDAnimations_CreateInstance(type, &second));
        TEST_ASSERT_EQUAL(ESP_OK, LEDAnimations_CreateInstance(type, &reference));
        for (unsigned step = 0; step < 20000; ++step) {
            first.plugin.Update(&first.plugin, 20);
            if (step % 37 == 0) first.plugin.Reset(&first.plugin);
            LEDColor b = second.plugin.Update(&second.plugin, 20);
            LEDColor expected = reference.plugin.Update(&reference.plugin, 20);
            TEST_ASSERT_EQUAL_MEMORY(&expected, &b, sizeof(b));
        }
    }
}

void test_LEDPatterns_FadePreservesChannelRatiosAndLoopBoundary(void) {
    LEDColor base = {200, 100, 40};
    uint32_t period = LEDAnimation_PatternPeriod(LED_PATTERN_FADE);
    LEDColor black = LEDAnimation_RenderPattern(LED_PATTERN_FADE, 0, base);
    LEDColor full = LEDAnimation_RenderPattern(LED_PATTERN_FADE, period / 2, base);
    LEDColor half = LEDAnimation_RenderPattern(LED_PATTERN_FADE, period / 4, base);
    TEST_ASSERT_EQUAL(0, black.r);
    TEST_ASSERT_EQUAL_MEMORY(&base, &full, sizeof(base));
    TEST_ASSERT_EQUAL(100, half.r);
    TEST_ASSERT_EQUAL(50, half.g);
    TEST_ASSERT_EQUAL(20, half.b);
    LEDColor boundary = LEDAnimation_RenderPattern(LED_PATTERN_FADE, period, base);
    TEST_ASSERT_EQUAL_MEMORY(&black, &boundary, sizeof(black));
}

void test_LEDPatterns_ColorCycleVisitsSixColorsAndWraps(void) {
    const LEDColor expected[] = {{80,0,0},{80,80,0},{0,80,0},
                                 {0,80,80},{0,0,80},{80,0,80}};
    uint32_t period = LEDAnimation_PatternPeriod(LED_PATTERN_COLOR_CYCLE);
    for (unsigned cycle = 0; cycle < 100; ++cycle) {
        for (unsigned frame = 0; frame < 6; ++frame) {
            LEDColor actual = LEDAnimation_RenderPattern(LED_PATTERN_COLOR_CYCLE,
                cycle * period + (period * frame + 5) / 6, (LEDColor){10,80,30});
            TEST_ASSERT_EQUAL_MEMORY(&expected[frame], &actual, sizeof(actual));
        }
    }
}

void test_LEDPatterns_AllPeriodicPlayersSurviveFullWidthSchedulingGap(void) {
    for (int pattern = 0; pattern < LED_PATTERN_COUNT; ++pattern) {
        if (pattern == LED_PATTERN_CANDLE) continue;
        uint32_t period = LEDAnimation_PatternPeriod(pattern);
        LEDAnimationInstance player;
        TEST_ASSERT_EQUAL(ESP_OK, LEDAnimations_CreateInstance(GENERIC_BREATHING + pattern, &player));
        uint32_t phase = 0;
        for (unsigned cycle = 0; cycle < 100; ++cycle) {
            uint32_t elapsed = UINT32_MAX - cycle;
            phase = (uint32_t)(((uint64_t)phase + elapsed) % period);
            LEDColor actual = player.plugin.Update(&player.plugin, elapsed);
            uint8_t brightness = LEDAnimation_GetBrightness(&player.plugin);
            LEDColor expected = LEDAnimation_RenderPattern(pattern, phase,
                (LEDColor){brightness, brightness, brightness});
            TEST_ASSERT_EQUAL_MEMORY(&expected, &actual, sizeof(actual));
        }
    }
    LEDColor invalid = LEDAnimation_RenderPattern(LED_PATTERN_COUNT, UINT32_MAX,
                                                   (LEDColor){255,255,255});
    TEST_ASSERT_EQUAL(0, invalid.r | invalid.g | invalid.b);
}

void test_LEDPatterns_SmoothEffectsNeverReverseWithinHalfCycle(void) {
    const int types[] = {BREATHING_RED, BREATHING_GREEN, GENERIC_BREATHING, GENERIC_FADE};
    for (unsigned kind = 0; kind < sizeof(types)/sizeof(types[0]); ++kind) {
        uint32_t period = kind < 2 ? 4000U : 3000U;
        LEDAnimationInstance player;
        TEST_ASSERT_EQUAL(ESP_OK, LEDAnimations_CreateInstance(types[kind], &player));
        player.plugin.render_brightness = 255;
        for (unsigned cycle = 0; cycle < 100; ++cycle) {
            player.plugin.Reset(&player.plugin);
            LEDColor previous = player.plugin.Update(&player.plugin, 0);
            for (uint32_t time = 20; time <= period; time += 20) {
                LEDColor next = player.plugin.Update(&player.plugin, 20);
                uint8_t a = kind == 1 ? previous.g : previous.r;
                uint8_t b = kind == 1 ? next.g : next.r;
                if (time <= period / 2) TEST_ASSERT_TRUE(b >= a);
                else TEST_ASSERT_TRUE(b <= a);
                LEDColor held = player.plugin.Update(&player.plugin, 0);
                TEST_ASSERT_EQUAL_MEMORY(&next, &held, sizeof(next));
                previous = next;
            }
        }
    }
}

void test_LEDPatterns_CandleIsWarmIrregularAndFrameRateIndependent(void) {
    LEDAnimationInstance regular, irregular;
    TEST_ASSERT_EQUAL(ESP_OK, LEDAnimations_CreateInstance(GENERIC_CANDLE, &regular));
    TEST_ASSERT_EQUAL(ESP_OK, LEDAnimations_CreateInstance(GENERIC_CANDLE, &irregular));
    regular.plugin.render_brightness = irregular.plugin.render_brightness = 255;
    unsigned changes = 0;
    LEDColor previous = regular.plugin.Update(&regular.plugin, 0);
    for (uint32_t time = 20; time <= 300000; time += 20) {
        LEDColor next = regular.plugin.Update(&regular.plugin, 20);
        (void)irregular.plugin.Update(&irregular.plugin, 7);
        LEDColor split = irregular.plugin.Update(&irregular.plugin, 13);
        TEST_ASSERT_EQUAL_MEMORY(&next, &split, sizeof(next));
        TEST_ASSERT_TRUE(next.r > next.g && next.g > next.b);
        TEST_ASSERT_TRUE(next.r >= 165);
        TEST_ASSERT_INT_WITHIN(18, previous.r, next.r);
        if (next.r != previous.r) changes++;
        previous = next;
    }
    TEST_ASSERT_TRUE(changes > 1000);
    LEDColor zero = LEDAnimation_RenderPattern(LED_PATTERN_CANDLE, UINT32_MAX, (LEDColor){0});
    TEST_ASSERT_EQUAL(0, zero.r | zero.g | zero.b);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_LEDInstances_AllCatalogEntriesHaveOwnedState);
    RUN_TEST(test_LEDInstances_ResetDoesNotChangeOtherPlayersOrTemplate);
    RUN_TEST(test_LEDInstances_InvalidInputPreservesDestination);
    RUN_TEST(test_LEDInstances_InconsistentMetadataIsRejected);
    RUN_TEST(test_LEDInstances_DoubleBlinkHundredCycles);
    RUN_TEST(test_LEDInstances_AllPlayersRemainIndependentWhenPeerResets);
    RUN_TEST(test_LEDPatterns_FadePreservesChannelRatiosAndLoopBoundary);
    RUN_TEST(test_LEDPatterns_ColorCycleVisitsSixColorsAndWraps);
    RUN_TEST(test_LEDPatterns_AllPeriodicPlayersSurviveFullWidthSchedulingGap);
    RUN_TEST(test_LEDPatterns_SmoothEffectsNeverReverseWithinHalfCycle);
    RUN_TEST(test_LEDPatterns_CandleIsWarmIrregularAndFrameRateIndependent);
    return UNITY_END();
}
