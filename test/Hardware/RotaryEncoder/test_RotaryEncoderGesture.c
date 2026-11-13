#include "test_mocks.h"
#include "unity.h"
#include "Hardware/RotaryEncoder/RotaryEncoderInternal.h"
#include "Hardware/RotaryEncoder/RotaryEncoderGesture.c"
RotaryEncoderServiceImpl rotaryEncoderService;
static RotaryEncoderEvent events[12];
static unsigned count, press_count;
void RotaryEncoder_EnqueueEvent(RotaryEncoderEvent *event) { if (event->type == ROTARY_ENCODER_EVENT_BUTTON && event->payload.button.gesture == GESTURE_PRESSED) { ++press_count; return; } TEST_ASSERT_LESS_THAN(12,count); events[count++]=*event; }
void setUp(void) {
    memset(&rotaryEncoderService,0,sizeof(rotaryEncoderService));
    rotaryEncoderService.runtime_config=(RotaryEncoderRuntimeConfig){.debounce_button_ms=50,.long_press_ms=800,.double_click_ms=300,.held_repeat_ms=200,.accel_threshold_ms=50};
    RESET_FAKE(mocked_esp_timer_get_time); count=press_count=0;
}
void tearDown(void) {}
static void edge(bool pressed,uint32_t when) { RotaryEncoderRawEvent event={.pressed=pressed,.timestamp_ms=when}; RotaryEncoder_ProcessButtonEvent(&event); }
void test_Gesture_RotationDirectionAndInversion(void) {
    mocked_esp_timer_get_time_fake.return_val=100000;
    RotaryEncoder_ProcessRotationDetents(2);
    TEST_ASSERT_EQUAL(2,events[0].payload.rotation.steps);
    TEST_ASSERT_EQUAL(ROTATION_CW,events[0].payload.rotation.direction);
    rotaryEncoderService.runtime_config.invert_direction=true;
    RotaryEncoder_ProcessRotationDetents(1);
    TEST_ASSERT_EQUAL(-1,events[1].payload.rotation.steps);
    TEST_ASSERT_EQUAL(1,rotaryEncoderService.status.absolute_position);
}
void test_Gesture_ShortBounceDoesNotClick(void) {
    edge(true,100); edge(false,120);
    TEST_ASSERT_EQUAL(0,count); TEST_ASSERT_FALSE(rotaryEncoderService.status.button_held);
}
void test_Gesture_SingleAndDoubleClick(void) {
    edge(true,100); edge(false,170);
    TEST_ASSERT_EQUAL(GESTURE_SINGLE_CLICK,events[0].payload.button.gesture);
    edge(true,220); edge(false,290);
    TEST_ASSERT_EQUAL(2,count);
    TEST_ASSERT_EQUAL(GESTURE_DOUBLE_CLICK,events[1].payload.button.gesture);
}
void test_Gesture_LongPressRepeatAndRelease(void) {
    edge(true,100);
    mocked_esp_timer_get_time_fake.return_val=900000;
    RotaryEncoder_CheckGestures(); RotaryEncoder_CheckGestures();
    TEST_ASSERT_EQUAL(1,count); TEST_ASSERT_EQUAL(GESTURE_LONG_PRESS,events[0].payload.button.gesture);
    mocked_esp_timer_get_time_fake.return_val=1100000; RotaryEncoder_CheckGestures();
    TEST_ASSERT_EQUAL(GESTURE_HELD,events[1].payload.button.gesture);
    edge(false,1150);
    TEST_ASSERT_EQUAL(GESTURE_RELEASED,events[2].payload.button.gesture);
}
void test_Gesture_ButtonDurationAcrossTimerWrap(void) {
    edge(true,UINT32_MAX-30); edge(false,40);
    TEST_ASSERT_EQUAL(1,count);
    TEST_ASSERT_EQUAL(71,events[0].payload.button.duration_ms);
}

void test_Gesture_UnpairedAndDuplicateEdgesDoNotGenerateClicks(void) {
    edge(false,100);
    TEST_ASSERT_EQUAL(0,count);
    edge(true,200); edge(true,230); edge(false,260); edge(false,400);
    TEST_ASSERT_EQUAL(1,count);
    TEST_ASSERT_EQUAL(60,events[0].payload.button.duration_ms);
    TEST_ASSERT_EQUAL(1,rotaryEncoderService.status.total_clicks);
}
void test_Gesture_OutOfRangeDetentsCannotWrapDirection(void) {
    RotaryEncoder_ProcessRotationDetents(INT32_MIN);
    RotaryEncoder_ProcessRotationDetents(INT32_MAX);
    rotaryEncoderService.runtime_config.invert_direction=true;
    RotaryEncoder_ProcessRotationDetents(INT16_MIN);
    TEST_ASSERT_EQUAL(0,count);
    TEST_ASSERT_EQUAL(0,rotaryEncoderService.status.absolute_position);
    TEST_ASSERT_EQUAL(3,rotaryEncoderService.status.dropped_rotation_events);
}
void test_Gesture_PositionAndRotationCountersSaturate(void) {
    rotaryEncoderService.status.absolute_position=INT32_MAX-1;
    rotaryEncoderService.status.total_rotations=UINT32_MAX-1;
    RotaryEncoder_ProcessRotationDetents(3);
    TEST_ASSERT_EQUAL(INT32_MAX,rotaryEncoderService.status.absolute_position);
    TEST_ASSERT_EQUAL(UINT32_MAX,rotaryEncoderService.status.total_rotations);
    rotaryEncoderService.status.absolute_position=INT32_MIN+1;
    RotaryEncoder_ProcessRotationDetents(-3);
    TEST_ASSERT_EQUAL(INT32_MIN,rotaryEncoderService.status.absolute_position);
}
void test_Gesture_AccelerationUsesConfiguredMaximum(void) {
    rotaryEncoderService.runtime_config.accel_enabled=true;
    rotaryEncoderService.runtime_config.accel_threshold_ms=UINT8_MAX;
    mocked_esp_timer_get_time_fake.return_val=1000;
    RotaryEncoder_ProcessRotationDetents(1);
    TEST_ASSERT_EQUAL(RE_ACCEL_MAX_MULTIPLIER,events[0].payload.rotation.velocity);
}

void test_Gesture_DefaultFiveSecondHoldDoesNotEmitReleaseClick(void) {
    rotaryEncoderService.runtime_config.long_press_ms = RE_LONG_PRESS_MS;
    TEST_ASSERT_EQUAL(5000, RE_LONG_PRESS_MS);
    edge(true, 100);
    mocked_esp_timer_get_time_fake.return_val = 5099000;
    RotaryEncoder_CheckGestures(); TEST_ASSERT_EQUAL(0, count);
    mocked_esp_timer_get_time_fake.return_val = 5100000;
    RotaryEncoder_CheckGestures(); RotaryEncoder_CheckGestures();
    TEST_ASSERT_EQUAL(1, count); TEST_ASSERT_EQUAL(GESTURE_LONG_PRESS, events[0].payload.button.gesture);
    edge(false, 5150);
    TEST_ASSERT_EQUAL(2, count); TEST_ASSERT_EQUAL(GESTURE_RELEASED, events[1].payload.button.gesture);
}

void test_Gesture_PressStartIsOncePerHoldAndDoesNotBecomeAClick(void) {
    edge(true,100); edge(true,200); TEST_ASSERT_EQUAL(1,press_count); TEST_ASSERT_EQUAL(0,count);
    edge(false,250); edge(true,400); TEST_ASSERT_EQUAL(2,press_count);
}
