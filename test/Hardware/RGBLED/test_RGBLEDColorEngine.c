#include <unity.h>
#include "Hardware/RGBLED/RGBLEDTypes.h"
#include "Hardware/RGBLED/RGBLEDColorEngine.h"

void setUp(void) {}
void tearDown(void) {}

void test_HSVtoRGB_Red(void) {
    HSVColor hsv = {0, 255, 255}; // Red
    RGBColor rgb = RGBLEDColorEngine_HSVtoRGB(hsv);
    
    TEST_ASSERT_EQUAL_UINT8(255, rgb.r);
    TEST_ASSERT_EQUAL_UINT8(0, rgb.g);
    TEST_ASSERT_EQUAL_UINT8(0, rgb.b);
}

void test_HSVtoRGB_Green(void) {
    HSVColor hsv = {120, 255, 255}; // Green
    RGBColor rgb = RGBLEDColorEngine_HSVtoRGB(hsv);
    
    TEST_ASSERT_EQUAL_UINT8(0, rgb.r);
    TEST_ASSERT_EQUAL_UINT8(255, rgb.g);
    TEST_ASSERT_EQUAL_UINT8(0, rgb.b);
}

void test_HSVtoRGB_White(void) {
    HSVColor hsv = {0, 0, 255}; // White (Saturation 0)
    RGBColor rgb = RGBLEDColorEngine_HSVtoRGB(hsv);
    
    TEST_ASSERT_EQUAL_UINT8(255, rgb.r);
    TEST_ASSERT_EQUAL_UINT8(255, rgb.g);
    TEST_ASSERT_EQUAL_UINT8(255, rgb.b);
}

void test_TemperatureToColor_Cold(void) {
    RGBColor rgb = RGBLEDColorEngine_TemperatureToColor(
        18.0f, 17.0f, 19.0f, 35.0f, 37.0f, 50.0f, 52.0f, 60.0f, 2.0f,
        (RGBColor){0, 0, 255}, (RGBColor){0, 255, 0}, (RGBColor){255, 0, 0},
        (RGBColor){128, 0, 128}, (RGBColor){255, 255, 255});
    TEST_ASSERT_EQUAL_UINT8(0, rgb.r);
    TEST_ASSERT_EQUAL_UINT8(0, rgb.g);
    TEST_ASSERT_EQUAL_UINT8(255, rgb.b);
}

void test_TemperatureToColor_Hot(void) {
    RGBColor rgb = RGBLEDColorEngine_TemperatureToColor(
        51.0f, 17.0f, 19.0f, 35.0f, 37.0f, 50.0f, 52.0f, 60.0f, 2.0f,
        (RGBColor){0, 0, 255}, (RGBColor){0, 255, 0}, (RGBColor){255, 0, 0},
        (RGBColor){128, 0, 128}, (RGBColor){255, 255, 255});
    TEST_ASSERT_EQUAL_UINT8(255, rgb.r);
    TEST_ASSERT_EQUAL_UINT8(0, rgb.g);
    TEST_ASSERT_EQUAL_UINT8(0, rgb.b);
}

void test_TemperatureToColor_Warm(void) {
    RGBColor rgb = RGBLEDColorEngine_TemperatureToColor(
        25.0f, 17.0f, 19.0f, 35.0f, 37.0f, 50.0f, 52.0f, 60.0f, 2.0f,
        (RGBColor){0, 0, 255}, (RGBColor){0, 255, 0}, (RGBColor){255, 0, 0},
        (RGBColor){128, 0, 128}, (RGBColor){255, 255, 255});
    // The transition should contain both cold and warm channels.
    TEST_ASSERT_GREATER_THAN(0, rgb.b);
    TEST_ASSERT_GREATER_THAN(0, rgb.g);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_HSVtoRGB_Red);
    RUN_TEST(test_HSVtoRGB_Green);
    RUN_TEST(test_HSVtoRGB_White);
    RUN_TEST(test_TemperatureToColor_Cold);
    RUN_TEST(test_TemperatureToColor_Hot);
    RUN_TEST(test_TemperatureToColor_Warm);
    return UNITY_END();
}
