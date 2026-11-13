#include "test_mocks.h"
#include "unity.h"
#include "Hardware/RotaryEncoder/RotaryEncoderInternal.h"

static BaseType_t test_notify(TaskHandle_t task, uint32_t bits, eNotifyAction action, BaseType_t *wake) {
    (void)task; (void)bits; (void)action; (void)wake; return pdTRUE;
}
#define xTaskNotifyFromISR test_notify
#include "Hardware/RotaryEncoder/RotaryEncoderPCNT.c"
#undef xTaskNotifyFromISR

RotaryEncoderServiceImpl rotaryEncoderService;
static unsigned init_calls, fail_init, channel_number;
static unsigned stop_calls, disable_calls, delete_channel_calls, delete_unit_calls;
static esp_err_t stop_error, disable_error, channel_error, unit_error;
static int unit_steps[16];
static unsigned unit_step_count;
static esp_err_t next_init(void) { return ++init_calls == fail_init ? ESP_FAIL : ESP_OK; }
esp_err_t pcnt_new_unit(const pcnt_unit_config_t *cfg, pcnt_unit_handle_t *out) {
    TEST_ASSERT_TRUE(cfg->high_limit > 0 && cfg->high_limit <= UINT8_MAX);
    TEST_ASSERT_EQUAL(-cfg->high_limit, cfg->low_limit);
    TEST_ASSERT_TRUE(unit_step_count < 16);
    unit_steps[unit_step_count++] = cfg->high_limit;
    esp_err_t err=next_init(); if (err==ESP_OK) *out=(void *)1; return err;
}
esp_err_t pcnt_new_channel(pcnt_unit_handle_t unit, const pcnt_chan_config_t *cfg, pcnt_channel_handle_t *out) {
    (void)cfg; TEST_ASSERT_EQUAL_PTR((void *)1,unit);
    esp_err_t err=next_init(); if (err==ESP_OK) *out=(void *)(uintptr_t)(2+channel_number++); return err;
}
esp_err_t pcnt_unit_set_glitch_filter(pcnt_unit_handle_t unit,const pcnt_glitch_filter_config_t *cfg) { (void)unit; (void)cfg; return next_init(); }
esp_err_t pcnt_channel_set_edge_action(pcnt_channel_handle_t ch,pcnt_channel_edge_action_t a,pcnt_channel_edge_action_t b) { (void)ch;(void)a;(void)b;return next_init(); }
esp_err_t pcnt_channel_set_level_action(pcnt_channel_handle_t ch,pcnt_channel_level_action_t a,pcnt_channel_level_action_t b) { (void)ch;(void)a;(void)b;return next_init(); }
esp_err_t pcnt_unit_add_watch_point(pcnt_unit_handle_t unit,int point) { (void)unit;(void)point;return next_init(); }
esp_err_t pcnt_unit_register_event_callbacks(pcnt_unit_handle_t unit,const pcnt_event_callbacks_t *cb,void *ctx) { (void)unit;(void)ctx;TEST_ASSERT_NOT_NULL(cb->on_reach);return next_init(); }
esp_err_t pcnt_unit_enable(pcnt_unit_handle_t unit) { (void)unit;return next_init(); }
esp_err_t pcnt_unit_clear_count(pcnt_unit_handle_t unit) { (void)unit;return next_init(); }
esp_err_t pcnt_unit_start(pcnt_unit_handle_t unit) { (void)unit;return next_init(); }
esp_err_t pcnt_unit_stop(pcnt_unit_handle_t unit) { (void)unit;stop_calls++;return stop_error; }
esp_err_t pcnt_unit_disable(pcnt_unit_handle_t unit) { (void)unit;disable_calls++;return disable_error; }
esp_err_t pcnt_del_channel(pcnt_channel_handle_t ch) { (void)ch;delete_channel_calls++;return channel_error; }
esp_err_t pcnt_del_unit(pcnt_unit_handle_t unit) { (void)unit;delete_unit_calls++;return unit_error; }
void setUp(void) {
    memset(&rotaryEncoderService,0,sizeof(rotaryEncoderService));
    rotaryEncoderService.runtime_config.steps_per_detent=RE_PCNT_STEPS_PER_DETENT;
    unit_step_count=0;
    init_calls=fail_init=channel_number=0;
    stop_calls=disable_calls=delete_channel_calls=delete_unit_calls=0;
    stop_error=disable_error=channel_error=unit_error=ESP_OK;
    RESET_FAKE(mocked_esp_timer_get_time); RESET_FAKE(mocked_gpio_get_level);
    RESET_FAKE(mocked_xQueueSendFromISR);
}
void tearDown(void) {}
void test_PCNT_RuntimeDetentRebuildsLimitsAndPreservesPosition(void) {
    TEST_ASSERT_EQUAL(ESP_OK,RotaryEncoder_InitPCNT());
    rotaryEncoderService.status.absolute_position=27;
    rotaryEncoderService.pcnt_pending_detents=3;
    TEST_ASSERT_EQUAL(ESP_OK,RotaryEncoder_ReconfigurePCNT(8));
    TEST_ASSERT_EQUAL(8,unit_steps[1]);
    TEST_ASSERT_EQUAL(8,rotaryEncoderService.runtime_config.steps_per_detent);
    TEST_ASSERT_EQUAL(27,rotaryEncoderService.status.absolute_position);
    TEST_ASSERT_EQUAL(0,rotaryEncoderService.pcnt_pending_detents);
    TEST_ASSERT_TRUE(rotaryEncoderService.pcnt_running);
    TEST_ASSERT_FALSE(atomic_load(&rotaryEncoderService.pcnt_reconfiguring));
    TEST_ASSERT_EQUAL(ESP_OK,RotaryEncoder_ReconfigurePCNT(8));
    TEST_ASSERT_EQUAL(2,unit_step_count);
    TEST_ASSERT_EQUAL(ESP_OK,RotaryEncoder_ReconfigurePCNT(0));
    TEST_ASSERT_EQUAL(RE_PCNT_STEPS_PER_DETENT,unit_steps[2]);
    TEST_ASSERT_EQUAL(ESP_OK,RotaryEncoder_DeinitPCNT());
}
void test_PCNT_ReconfigurationFailureRestoresOldAppliedLimits(void) {
    TEST_ASSERT_EQUAL(ESP_OK,RotaryEncoder_InitPCNT());
    fail_init=init_calls+1;
    TEST_ASSERT_EQUAL(ESP_FAIL,RotaryEncoder_ReconfigurePCNT(255));
    TEST_ASSERT_EQUAL(255,unit_steps[1]);
    TEST_ASSERT_EQUAL(RE_PCNT_STEPS_PER_DETENT,unit_steps[2]);
    TEST_ASSERT_EQUAL(RE_PCNT_STEPS_PER_DETENT,rotaryEncoderService.runtime_config.steps_per_detent);
    TEST_ASSERT_TRUE(rotaryEncoderService.pcnt_running);
    TEST_ASSERT_FALSE(atomic_load(&rotaryEncoderService.closing));
    TEST_ASSERT_EQUAL(ESP_OK,RotaryEncoder_DeinitPCNT());
}
void test_PCNT_EveryReconfigurationSetupFailureRestoresOldLimits(void) {
    for (unsigned failure=1;failure<=14;failure++) {
        setUp();
        TEST_ASSERT_EQUAL(ESP_OK,RotaryEncoder_InitPCNT());
        fail_init=init_calls+failure;
        TEST_ASSERT_EQUAL(ESP_FAIL,RotaryEncoder_ReconfigurePCNT(8));
        TEST_ASSERT_EQUAL(RE_PCNT_STEPS_PER_DETENT,rotaryEncoderService.runtime_config.steps_per_detent);
        TEST_ASSERT_TRUE(rotaryEncoderService.pcnt_running);
        TEST_ASSERT_FALSE(atomic_load(&rotaryEncoderService.closing));
        TEST_ASSERT_EQUAL(RE_PCNT_STEPS_PER_DETENT,unit_steps[unit_step_count-1]);
        TEST_ASSERT_EQUAL(ESP_OK,RotaryEncoder_DeinitPCNT());
    }
}
void test_PCNT_ReconfigurationRollbackFailureRetainsCleanupOwnership(void) {
    TEST_ASSERT_EQUAL(ESP_OK,RotaryEncoder_InitPCNT());
    unit_error=ESP_FAIL;
    TEST_ASSERT_EQUAL(ESP_FAIL,RotaryEncoder_ReconfigurePCNT(8));
    TEST_ASSERT_EQUAL_PTR((void *)1,rotaryEncoderService.pcnt_unit);
    TEST_ASSERT_EQUAL(RE_PCNT_STEPS_PER_DETENT,rotaryEncoderService.runtime_config.steps_per_detent);
    TEST_ASSERT_TRUE(atomic_load(&rotaryEncoderService.closing));
    TEST_ASSERT_EQUAL(SERVICE_STATE_ERROR,rotaryEncoderService.state);
    unit_error=ESP_OK;
    TEST_ASSERT_EQUAL(ESP_OK,RotaryEncoder_DeinitPCNT());
}
void test_PCNT_CompleteLifecycleAndRepeatedCleanup(void) {
    TEST_ASSERT_EQUAL(ESP_OK,RotaryEncoder_InitPCNT());
    TEST_ASSERT_TRUE(rotaryEncoderService.pcnt_running);
    TEST_ASSERT_TRUE(rotaryEncoderService.pcnt_enabled);
    TEST_ASSERT_EQUAL(ESP_OK,RotaryEncoder_DeinitPCNT());
    TEST_ASSERT_EQUAL(1,stop_calls); TEST_ASSERT_EQUAL(1,disable_calls);
    TEST_ASSERT_EQUAL(2,delete_channel_calls); TEST_ASSERT_EQUAL(1,delete_unit_calls);
    TEST_ASSERT_EQUAL(ESP_OK,RotaryEncoder_DeinitPCNT()); TEST_ASSERT_EQUAL(1,delete_unit_calls);
}
void test_PCNT_EveryInitializationFailureRollsBack(void) {
    for (unsigned failure=1;failure<=14;failure++) {
        setUp(); fail_init=failure;
        TEST_ASSERT_EQUAL(ESP_FAIL,RotaryEncoder_InitPCNT());
        TEST_ASSERT_NULL(rotaryEncoderService.pcnt_unit);
        TEST_ASSERT_NULL(rotaryEncoderService.pcnt_chan_a);
        TEST_ASSERT_NULL(rotaryEncoderService.pcnt_chan_b);
        TEST_ASSERT_FALSE(rotaryEncoderService.pcnt_enabled);
        TEST_ASSERT_FALSE(rotaryEncoderService.pcnt_running);
    }
}
void test_PCNT_StopFailureKeepsAllOwnership(void) {
    TEST_ASSERT_EQUAL(ESP_OK,RotaryEncoder_InitPCNT()); stop_error=ESP_FAIL;
    TEST_ASSERT_EQUAL(ESP_FAIL,RotaryEncoder_DeinitPCNT());
    TEST_ASSERT_TRUE(rotaryEncoderService.pcnt_running); TEST_ASSERT_EQUAL(0,disable_calls);
    TEST_ASSERT_EQUAL(0,delete_channel_calls); TEST_ASSERT_EQUAL(0,delete_unit_calls);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE,RotaryEncoder_InitPCNT());
    stop_error=ESP_OK; TEST_ASSERT_EQUAL(ESP_OK,RotaryEncoder_DeinitPCNT());
}
void test_PCNT_DisableFailureRetriesWithoutStoppingAgain(void) {
    TEST_ASSERT_EQUAL(ESP_OK,RotaryEncoder_InitPCNT()); disable_error=ESP_FAIL;
    TEST_ASSERT_EQUAL(ESP_FAIL,RotaryEncoder_DeinitPCNT());
    TEST_ASSERT_FALSE(rotaryEncoderService.pcnt_running); TEST_ASSERT_TRUE(rotaryEncoderService.pcnt_enabled);
    TEST_ASSERT_EQUAL(0,delete_channel_calls);
    disable_error=ESP_OK; TEST_ASSERT_EQUAL(ESP_OK,RotaryEncoder_DeinitPCNT()); TEST_ASSERT_EQUAL(1,stop_calls);
}
void test_PCNT_ChannelDeleteFailureRetainsHandle(void) {
    TEST_ASSERT_EQUAL(ESP_OK,RotaryEncoder_InitPCNT()); channel_error=ESP_FAIL;
    TEST_ASSERT_EQUAL(ESP_FAIL,RotaryEncoder_DeinitPCNT());
    TEST_ASSERT_EQUAL_PTR((void *)3,rotaryEncoderService.pcnt_chan_b);
    TEST_ASSERT_EQUAL_PTR((void *)2,rotaryEncoderService.pcnt_chan_a);
    TEST_ASSERT_EQUAL(0,delete_unit_calls);
    channel_error=ESP_OK; TEST_ASSERT_EQUAL(ESP_OK,RotaryEncoder_DeinitPCNT());
    TEST_ASSERT_EQUAL(1,stop_calls); TEST_ASSERT_EQUAL(1,disable_calls);
}
void test_PCNT_UnitDeleteFailureDoesNotDeleteChannelsAgain(void) {
    TEST_ASSERT_EQUAL(ESP_OK,RotaryEncoder_InitPCNT()); unit_error=ESP_FAIL;
    TEST_ASSERT_EQUAL(ESP_FAIL,RotaryEncoder_DeinitPCNT());
    TEST_ASSERT_NOT_NULL(rotaryEncoderService.pcnt_unit); TEST_ASSERT_NULL(rotaryEncoderService.pcnt_chan_a);
    unit_error=ESP_OK; TEST_ASSERT_EQUAL(ESP_OK,RotaryEncoder_DeinitPCNT()); TEST_ASSERT_EQUAL(2,delete_channel_calls);
}
void test_PCNT_FailedInitializationRollbackCanBeRetried(void) {
    fail_init=14; disable_error=ESP_ERR_INVALID_STATE;
    TEST_ASSERT_EQUAL(ESP_FAIL,RotaryEncoder_InitPCNT());
    TEST_ASSERT_TRUE(rotaryEncoderService.pcnt_enabled); TEST_ASSERT_NOT_NULL(rotaryEncoderService.pcnt_unit);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE,RotaryEncoder_InitPCNT());
    disable_error=ESP_OK; TEST_ASSERT_EQUAL(ESP_OK,RotaryEncoder_DeinitPCNT());
    fail_init=0; TEST_ASSERT_EQUAL(ESP_OK,RotaryEncoder_InitPCNT());
    TEST_ASSERT_EQUAL(ESP_OK,RotaryEncoder_DeinitPCNT());
}
void test_PCNT_FirstZeroTimeAndWrapUseElapsedDebounce(void) {
    pcnt_watch_event_data_t event={.watch_point_value=4};
    pcnt_on_reach(NULL,&event,NULL);
    TEST_ASSERT_EQUAL(1,rotaryEncoderService.pcnt_pending_detents);
    mocked_esp_timer_get_time_fake.return_val=1;
    pcnt_on_reach(NULL,&event,NULL);
    TEST_ASSERT_EQUAL(1,rotaryEncoderService.pcnt_pending_detents);
    rotaryEncoderService.pcnt_last_fire_time_us=UINT32_MAX-RE_PCNT_ISR_DEBOUNCE_US;
    mocked_esp_timer_get_time_fake.return_val=100;
    pcnt_on_reach(NULL,&event,NULL);
    TEST_ASSERT_EQUAL(2,rotaryEncoderService.pcnt_pending_detents);
}
void test_PCNT_DetentOverflowAndClosingCannotCorruptAccumulator(void) {
    pcnt_watch_event_data_t event={.watch_point_value=4};
    rotaryEncoderService.pcnt_pending_detents=INT32_MAX;
    pcnt_on_reach(NULL,&event,NULL);
    TEST_ASSERT_EQUAL(INT32_MAX,rotaryEncoderService.pcnt_pending_detents);
    TEST_ASSERT_EQUAL(1,rotaryEncoderService.diagnostics.pcnt_overflow_count);
    event.watch_point_value=-4; rotaryEncoderService.pcnt_pending_detents=INT32_MIN;
    pcnt_on_reach(NULL,&event,NULL);
    TEST_ASSERT_EQUAL(INT32_MIN,rotaryEncoderService.pcnt_pending_detents);
    TEST_ASSERT_EQUAL(2,rotaryEncoderService.diagnostics.pcnt_overflow_count);
    atomic_store(&rotaryEncoderService.closing,true);
    event.watch_point_value=4; pcnt_on_reach(NULL,&event,NULL);
    TEST_ASSERT_EQUAL(INT32_MIN,rotaryEncoderService.pcnt_pending_detents);
    TEST_ASSERT_FALSE(pcnt_on_reach(NULL,NULL,NULL));
}
static RotaryEncoderRawEvent captured_button;
static BaseType_t copy_button(QueueHandle_t queue,const void *item,BaseType_t *wake) {
    (void)queue;(void)wake;captured_button=*(const RotaryEncoderRawEvent *)item; return pdTRUE;
}
void test_PCNT_ButtonShortReleaseIsDeliveredAndSameLevelNoiseIsFiltered(void) {
    rotaryEncoderService.rawButtonQueue=(void *)1;
    mocked_xQueueSendFromISR_fake.custom_fake=copy_button;
    rotary_button_isr_handler(NULL);
    TEST_ASSERT_TRUE(captured_button.pressed);
    TEST_ASSERT_EQUAL(1,mocked_xQueueSendFromISR_fake.call_count);
    mocked_esp_timer_get_time_fake.return_val=1000;
    rotary_button_isr_handler(NULL);
    TEST_ASSERT_EQUAL(1,mocked_xQueueSendFromISR_fake.call_count);
    mocked_gpio_get_level_fake.return_val=1;
    rotary_button_isr_handler(NULL);
    TEST_ASSERT_FALSE(captured_button.pressed);
    TEST_ASSERT_EQUAL(2,mocked_xQueueSendFromISR_fake.call_count);
    atomic_store(&rotaryEncoderService.closing,true);
    rotary_button_isr_handler(NULL);
    TEST_ASSERT_EQUAL(2,mocked_xQueueSendFromISR_fake.call_count);
}
void test_PCNT_TypedButtonUsesCapturedLevelAndRejectsOtherRole(void) {
    rotaryEncoderService.rawButtonQueue=(void *)1;
    mocked_xQueueSendFromISR_fake.custom_fake=copy_button;
    RotaryEncoder_ButtonPinISR(PIN_FUNC_ROTARY_CLK,true,NULL);
    TEST_ASSERT_EQUAL(0,mocked_xQueueSendFromISR_fake.call_count);
    RotaryEncoder_ButtonPinISR(PIN_FUNC_ROTARY_SW,true,NULL);
    TEST_ASSERT_FALSE(captured_button.pressed);
    TEST_ASSERT_EQUAL(1,mocked_xQueueSendFromISR_fake.call_count);
    TEST_ASSERT_EQUAL(0,mocked_gpio_get_level_fake.call_count);
}
