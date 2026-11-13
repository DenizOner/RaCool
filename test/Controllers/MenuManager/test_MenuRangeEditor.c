#include "unity.h"
#include "Controllers/MenuManager/MenuRangeEditor.h"
#include <math.h>
void test_MenuRangeEditor_AllKindsClampAndStage(void) {
    SystemSettings settings={0};
    settings.hardware.ext_led_temp_cold_min=17;settings.hardware.ext_led_temp_cold_max=19;
    settings.hardware.ext_led_temp_warm_min=35;settings.hardware.ext_led_temp_warm_max=37;
    settings.hardware.ext_led_temp_hot_min=50;settings.hardware.ext_led_temp_hot_max=52;
    settings.control.relay_close_temp=25;settings.control.relay_open_temp=35;
    settings.control.fan_delta_off_decic=-15;settings.control.fan_delta_on_decic=20;
    const UIState states[]={UI_STATE_SET_RGB_TEMP_COLD,UI_STATE_SET_RGB_TEMP_WARM,UI_STATE_SET_RGB_TEMP_HOT,UI_STATE_SET_LOW_TEMP,UI_STATE_FAN_DELTA_OFF};
    for (unsigned i=0;i<5;++i) {
        TEST_ASSERT_TRUE(MenuRangeEditor_Begin(states[i],&settings));
        int16_t low,high,minimum,maximum;unsigned phase;
        TEST_ASSERT_TRUE(MenuRangeEditor_Bounds(states[i],&minimum,&maximum));
        TEST_ASSERT_TRUE(MenuRangeEditor_Rotate(states[i],-2147483647));
        TEST_ASSERT_TRUE(MenuRangeEditor_Read(states[i],&low,&high,&phase));
        TEST_ASSERT_EQUAL(minimum,low);TEST_ASSERT_EQUAL(0,phase);
        TEST_ASSERT_FALSE(MenuRangeEditor_Advance(states[i]));
        TEST_ASSERT_TRUE(MenuRangeEditor_Rotate(states[i],2147483647));
        TEST_ASSERT_TRUE(MenuRangeEditor_Read(states[i],&low,&high,&phase));
        TEST_ASSERT_EQUAL(maximum,high);TEST_ASSERT_EQUAL(1,phase);
        TEST_ASSERT_TRUE(MenuRangeEditor_Unchanged(states[i],&settings));
        TEST_ASSERT_TRUE(MenuRangeEditor_Advance(states[i]));
        MenuRangeEditor_Failed(states[i]);
        TEST_ASSERT_TRUE(MenuRangeEditor_Read(states[i],&low,&high,&phase));TEST_ASSERT_EQUAL(3,phase);
        MenuRangeEditor_Clear();TEST_ASSERT_FALSE(MenuRangeEditor_Read(states[i],&low,&high,&phase));
    }
}
void test_MenuRangeEditor_RejectsInvalidAndForeignOwner(void) {
    SystemSettings settings={0};settings.control.relay_close_temp=25;settings.control.relay_open_temp=35;
    TEST_ASSERT_TRUE(MenuRangeEditor_Begin(UI_STATE_SET_LOW_TEMP,&settings));
    settings.control.relay_close_temp=25.01f;
    TEST_ASSERT_FALSE(MenuRangeEditor_Unchanged(UI_STATE_SET_LOW_TEMP,&settings));
    settings.control.relay_close_temp=NAN;
    TEST_ASSERT_FALSE(MenuRangeEditor_Begin(UI_STATE_SET_LOW_TEMP,&settings));
    settings.control.fan_delta_off_decic=20;settings.control.fan_delta_on_decic=20;
    TEST_ASSERT_FALSE(MenuRangeEditor_Begin(UI_STATE_FAN_DELTA_OFF,&settings));
    settings.control.fan_delta_off_decic=-91;settings.control.fan_delta_on_decic=20;
    TEST_ASSERT_FALSE(MenuRangeEditor_Begin(UI_STATE_FAN_DELTA_OFF,&settings));
    TEST_ASSERT_EQUAL(-1,MenuRangeEditor_Zone(UI_STATE_SET_HIGH_TEMP));
}
