#include "unity.h"
#include "Hardware/RotaryEncoder/RotaryEncoderInternal.h"
#include <string.h>
#include "../../../../src/Hardware/RotaryEncoder/RotaryEncoderPCNT.c"

/* No encoder worker or synthetic pulses: this checks real SDK resource ownership. */
static bool target_pcnt_owned;
void target_rotary_pcnt_cleanup(void) {
    if (!target_pcnt_owned) return;
    TEST_ASSERT_EQUAL(ESP_OK, RotaryEncoder_DeinitPCNT());
    target_pcnt_owned = false;
}
void target_rotary_pcnt_verify_lifecycle(void) {
    memset(&rotaryEncoderService, 0, sizeof(rotaryEncoderService));
    rotaryEncoderService.spinlock = (portMUX_TYPE)portMUX_INITIALIZER_UNLOCKED;
    rotaryEncoderService.clk_pin = GPIO_NUM_4;
    rotaryEncoderService.dt_pin = GPIO_NUM_5;
    target_pcnt_owned = true;
    for (unsigned run = 0; run < 3; run++) {
        TEST_ASSERT_EQUAL(ESP_OK, RotaryEncoder_InitPCNT());
        TEST_ASSERT_NOT_NULL(rotaryEncoderService.pcnt_unit);
        TEST_ASSERT_TRUE(rotaryEncoderService.pcnt_enabled);
        TEST_ASSERT_TRUE(rotaryEncoderService.pcnt_running);
        TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, RotaryEncoder_InitPCNT());
        TEST_ASSERT_EQUAL(ESP_OK, RotaryEncoder_DeinitPCNT());
        TEST_ASSERT_NULL(rotaryEncoderService.pcnt_unit);
        TEST_ASSERT_NULL(rotaryEncoderService.pcnt_chan_a);
        TEST_ASSERT_NULL(rotaryEncoderService.pcnt_chan_b);
        TEST_ASSERT_FALSE(rotaryEncoderService.pcnt_enabled);
        TEST_ASSERT_FALSE(rotaryEncoderService.pcnt_running);
        TEST_ASSERT_EQUAL(ESP_OK, RotaryEncoder_DeinitPCNT());
    }
}
