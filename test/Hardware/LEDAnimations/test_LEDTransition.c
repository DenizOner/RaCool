#include "unity.h"
#include "Hardware/LEDAnimations/LEDTransition.h"
static LEDTransition transition;
void setUp(void) { LEDTransition_Reset(&transition, (LEDFrame){255,0,0,255}); }
void tearDown(void) {}
void test_Transition_EndpointsAndConcurrentBrightness(void) {
    LEDFrame target = {0,0,255,127};
    LEDTransition_Start(&transition, 100, 300);
    LEDFrame frame = LEDTransition_Sample(&transition,target,100);
    TEST_ASSERT_EQUAL(255,frame.r); TEST_ASSERT_EQUAL(0,frame.b);
    frame = LEDTransition_Sample(&transition,target,250);
    TEST_ASSERT_EQUAL(128,frame.r); TEST_ASSERT_EQUAL(128,frame.b);
    TEST_ASSERT_EQUAL(191,frame.brightness);
    frame = LEDTransition_Sample(&transition,target,400);
    TEST_ASSERT_EQUAL_MEMORY(&target,&frame,sizeof(frame));
    TEST_ASSERT_FALSE(transition.active);
}
void test_Transition_RetargetStartsAtDisplayedFrame(void) {
    LEDTransition_Start(&transition,0,300);
    LEDFrame previous=LEDTransition_Sample(&transition,(LEDFrame){0,255,0,100},120);
    LEDTransition_Start(&transition,120,300);
    LEDFrame current=LEDTransition_Sample(&transition,(LEDFrame){0,0,255,255},120);
    TEST_ASSERT_EQUAL_MEMORY(&previous,&current,sizeof(current));
}
void test_Transition_ClockWrap(void) {
    LEDTransition_Start(&transition,UINT32_MAX-99,300);
    LEDFrame frame=LEDTransition_Sample(&transition,(LEDFrame){0,0,255,255},50);
    TEST_ASSERT_EQUAL(128,frame.r); TEST_ASSERT_EQUAL(128,frame.b);
}
void test_Transition_ZeroDurationAndShutdown(void) {
    LEDFrame black={0};
    LEDTransition_Start(&transition,0,0);
    LEDFrame frame=LEDTransition_Sample(&transition,black,0);
    TEST_ASSERT_EQUAL_MEMORY(&black,&frame,sizeof(frame));
    LEDTransition_Start(&transition,0,300);
    LEDTransition_Reset(&transition,black);
    TEST_ASSERT_FALSE(transition.active);
    TEST_ASSERT_EQUAL_MEMORY(&black,&transition.displayed,sizeof(black));
}
void test_Transition_DoesNotRestartForAnimationFrames(void) {
    LEDTransition_Start(&transition,0,300);
    LEDTransition_Sample(&transition,(LEDFrame){0,255,0,255},100);
    LEDFrame target={0,0,255,255};
    LEDFrame frame=LEDTransition_Sample(&transition,target,300);
    TEST_ASSERT_EQUAL_MEMORY(&target,&frame,sizeof(frame));
    target=(LEDFrame){255,0,0,255};
    frame=LEDTransition_Sample(&transition,target,320);
    TEST_ASSERT_EQUAL_MEMORY(&target,&frame,sizeof(frame));
}
