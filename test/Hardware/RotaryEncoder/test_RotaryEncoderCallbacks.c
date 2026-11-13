#include "test_mocks.h"
#include "unity.h"
#include "lvgl.h"
typedef struct { int16_t enc_diff; int state; } lv_indev_data_t;
#define LV_INDEV_STATE_PRESSED 1
#define LV_INDEV_STATE_RELEASED 0
#include "Hardware/RotaryEncoder/RotaryEncoder.c"

esp_err_t RotaryEncoder_InitPCNT(void) { return ESP_OK; }
esp_err_t RotaryEncoder_DeinitPCNT(void) { return ESP_OK; }
static esp_err_t reconfigure_result;
static unsigned reconfigure_calls;
esp_err_t RotaryEncoder_ReconfigurePCNT(uint8_t steps) {
    reconfigure_calls++;
    if (reconfigure_result==ESP_OK) rotaryEncoderService.runtime_config.steps_per_detent=steps;
    return reconfigure_result;
}
static esp_err_t clear_result;
esp_err_t pcnt_unit_clear_count(pcnt_unit_handle_t unit) { (void)unit; return clear_result; }
void rotary_button_isr_handler(void *arg) { (void)arg; }
void RotaryEncoder_ButtonPinISR(PinFunction_t func,bool level,void *arg) { (void)func;(void)level;(void)arg; }
esp_err_t register_encoder_commands(IDebugConsole *console) { (void)console; return ESP_OK; }
void RotaryEncoder_ResetGestureState(void) {}
static int32_t simulated_detents;
void RotaryEncoder_ProcessRotationDetents(int32_t count) { simulated_detents=count; }
void RotaryEncoder_ProcessButtonEvent(RotaryEncoderRawEvent *raw) { (void)raw; }
void RotaryEncoder_CheckGestures(void) {}
#define register_encoder_commands RotaryNative_RegisterCommands
#include "Hardware/RotaryEncoder/RotaryEncoderConsole.c"
#undef register_encoder_commands

static esp_err_t unavailable_encoder_diagnostics(IRotaryEncoder *self,RotaryEncoderDiagnostics *out) {
    (void)self;(void)out;return ESP_ERR_INVALID_STATE;
}
void test_Encoder_DiagnosticCommandsRejectUnavailableSummaryAndMalformedArguments(void) {
    RotaryEncoder_GetModule();
    IRotaryEncoder saved=rotaryEncoderService.base;
    char *diag[]={"ENCDIAG",NULL},*info[]={"ENC",NULL};
    TEST_ASSERT_EQUAL(1,cmd_enc_diag(1,NULL)); TEST_ASSERT_EQUAL(1,cmd_enc_info(1,NULL));
    TEST_ASSERT_EQUAL(1,cmd_enc_diag(2,diag)); TEST_ASSERT_EQUAL(1,cmd_enc_info(2,info));
    rotaryEncoderService.base.GetDiagnostics=unavailable_encoder_diagnostics;
    int diagnostic=cmd_enc_diag(1,diag),status=cmd_enc_info(1,info);
    rotaryEncoderService.base=saved;
    TEST_ASSERT_EQUAL(1,diagnostic);TEST_ASSERT_EQUAL(1,status);
}

static unsigned first_calls, second_calls;
static int context_one, context_two;
static bool nested;
static void rotation_second(RotaryEncoderRotationEvent event, void *ctx) {
    (void)event; TEST_ASSERT_EQUAL_PTR(&context_two,ctx); second_calls++;
}
static void rotation_first(RotaryEncoderRotationEvent event, void *ctx) {
    TEST_ASSERT_EQUAL_PTR(&context_one,ctx); first_calls++;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE,RotaryEncoder_UnsubscribeRotation(NULL,rotation_first));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE,RotaryEncoder_UnsubscribeRotation(NULL,rotation_second));
    if (nested && first_calls == 1) RotaryEncoder_DispatchRotation(event);
}
static void button_second(RotaryEncoderButtonEvent event, void *ctx) {
    (void)event; TEST_ASSERT_EQUAL_PTR(&context_two,ctx); second_calls++;
}
static void button_first(RotaryEncoderButtonEvent event, void *ctx) {
    (void)event; TEST_ASSERT_EQUAL_PTR(&context_one,ctx); first_calls++;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE,RotaryEncoder_UnsubscribeButton(NULL,button_first));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE,RotaryEncoder_UnsubscribeButton(NULL,button_second));
}
static int32_t inspect_bus(BusEvent event) {
    (void)event;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE,RotaryEncoder_UnsubscribeRotation(NULL,rotation_first));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE,RotaryEncoder_UnsubscribeRotation(NULL,rotation_second));
    return 0;
}
void setUp(void) {
    memset(&rotaryEncoderService,0,sizeof(rotaryEncoderService));
    rotaryEncoderService.spinlock=(portMUX_TYPE)portMUX_INITIALIZER_UNLOCKED;
    first_calls=second_calls=0; nested=false;
    clear_result=ESP_OK;
    reconfigure_result=ESP_OK; reconfigure_calls=0;
    simulated_detents=0;
    encoder_command_owner=NULL;
    memset(encoder_command_registered,0,sizeof(encoder_command_registered));
    RESET_FAKE(mocked_xTaskNotify);
    RESET_FAKE(mocked_vTaskDelete); RESET_FAKE(mocked_xTaskGetTickCount);
    RESET_FAKE(mocked_gpio_isr_handler_remove); RESET_FAKE(mocked_xTaskCreate);
    RESET_FAKE(mocked_gpio_isr_handler_add); RESET_FAKE(mocked_gpio_config);
    RESET_FAKE(mocked_xQueueSend); RESET_FAKE(mocked_xQueueCreate);
    RESET_FAKE(mocked_vQueueDelete);
    RESET_FAKE(mocked_ServiceLocator_GetService); RESET_FAKE(mocked_ServiceLocator_GetEventBus);
    RESET_FAKE(mocked_ConfigurationManager_GetInstance);
}
void tearDown(void) {}
void test_Encoder_DetentControlPropagatesSDKFailureWithoutResettingCounters(void) {
    rotaryEncoderService.state=SERVICE_STATE_INITIALIZED;
    rotaryEncoderService.runtime_config.steps_per_detent=4;
    rotaryEncoderService.status.absolute_position=12;
    reconfigure_result=ESP_FAIL;
    TEST_ASSERT_EQUAL(ESP_FAIL,RotaryEncoder_SetStepsPerDetent(NULL,8));
    TEST_ASSERT_EQUAL(4,rotaryEncoderService.runtime_config.steps_per_detent);
    TEST_ASSERT_EQUAL(12,rotaryEncoderService.status.absolute_position);
    reconfigure_result=ESP_OK;
    TEST_ASSERT_EQUAL(ESP_OK,RotaryEncoder_SetStepsPerDetent(NULL,8));
    TEST_ASSERT_EQUAL(8,rotaryEncoderService.runtime_config.steps_per_detent);
    TEST_ASSERT_EQUAL(12,rotaryEncoderService.status.absolute_position);
    TEST_ASSERT_EQUAL(2,reconfigure_calls);
}
void test_Encoder_CLIRejectsTruncationMalformedBooleanAndExtraArguments(void) {
    rotaryEncoderService.base.SetStepsPerDetent=RotaryEncoder_SetStepsPerDetent;
    rotaryEncoderService.base.SetDirectionInverted=RotaryEncoder_SetDirectionInverted;
    rotaryEncoderService.state=SERVICE_STATE_INITIALIZED;
    rotaryEncoderService.runtime_config.steps_per_detent=4;
    rotaryEncoderService.runtime_config.invert_direction=true;
    const char *invalid[]={"0","-1","256","9999999999999999999999999","2x",""," 4","+4"};
    for (unsigned i=0;i<sizeof(invalid)/sizeof(invalid[0]);i++) {
        char *args[]={"ENCCONFIG","DETENTS",(char *)invalid[i]};
        TEST_ASSERT_EQUAL(1,cmd_enc_config(3,args));
    }
    char *extra[]={"ENCCONFIG","DETENTS","8","extra"};
    TEST_ASSERT_EQUAL(1,cmd_enc_config(4,extra));
    TEST_ASSERT_EQUAL(0,reconfigure_calls);
    TEST_ASSERT_EQUAL(4,rotaryEncoderService.runtime_config.steps_per_detent);
    char *invalid_bool[]={"ENCCONFIG","INVERT","OON"};
    TEST_ASSERT_EQUAL(1,cmd_enc_config(3,invalid_bool));
    TEST_ASSERT_TRUE(rotaryEncoderService.runtime_config.invert_direction);
    char *valid[]={"ENCCONFIG","DETENTS","8"};
    reconfigure_result=ESP_FAIL;
    TEST_ASSERT_EQUAL(1,cmd_enc_config(3,valid));
    TEST_ASSERT_EQUAL(4,rotaryEncoderService.runtime_config.steps_per_detent);
    reconfigure_result=ESP_OK;
    TEST_ASSERT_EQUAL(0,cmd_enc_config(3,valid));
    TEST_ASSERT_EQUAL(8,rotaryEncoderService.runtime_config.steps_per_detent);
}
void test_Encoder_CLIAccelerationAndDebounceValidateBeforeMutation(void) {
    rotaryEncoderService.base.SetAcceleration=RotaryEncoder_SetAcceleration;
    rotaryEncoderService.base.SetDebounceMs=RotaryEncoder_SetDebounceMs;
    const char *invalid[]={"-1","65536","1x","999999999999999999999",""};
    for (unsigned i=0;i<sizeof(invalid)/sizeof(invalid[0]);i++) {
        char *args[]={"ENCDEBOUNCE","12",(char *)invalid[i]};
        TEST_ASSERT_EQUAL(1,cmd_enc_debounce(3,args));
        TEST_ASSERT_EQUAL(0,rotaryEncoderService.runtime_config.debounce_rotation_ms);
    }
    char *debounce[]={"ENCDEBOUNCE","0","65535"};
    TEST_ASSERT_EQUAL(0,cmd_enc_debounce(3,debounce));
    TEST_ASSERT_EQUAL(65535,rotaryEncoderService.runtime_config.debounce_button_ms);
    char *accel[]={"ENCACCEL","ON","256"};
    TEST_ASSERT_EQUAL(1,cmd_enc_accel(3,accel));
    TEST_ASSERT_FALSE(rotaryEncoderService.runtime_config.accel_enabled);
    accel[2]="255";
    TEST_ASSERT_EQUAL(0,cmd_enc_accel(3,accel));
    TEST_ASSERT_EQUAL(255,rotaryEncoderService.runtime_config.accel_threshold_ms);
    char *extra[]={"ENCACCEL","OFF","0","extra"};
    TEST_ASSERT_EQUAL(1,cmd_enc_accel(4,extra));
    TEST_ASSERT_TRUE(rotaryEncoderService.runtime_config.accel_enabled);
}
static void register_rotation_pair(void) {
    TEST_ASSERT_EQUAL(ESP_OK,RotaryEncoder_OnRotation(NULL,rotation_first,&context_one));
    TEST_ASSERT_EQUAL(ESP_OK,RotaryEncoder_OnRotation(NULL,rotation_second,&context_two));
}
void test_Encoder_RotationCapturedAndWaitingContextsStayOwned(void) {
    register_rotation_pair();
    IEventBus bus={.Publish=inspect_bus}; rotaryEncoderService.eventBus=&bus;
    RotaryEncoder_DispatchRotation((RotaryEncoderRotationEvent){0});
    TEST_ASSERT_EQUAL(1,first_calls); TEST_ASSERT_EQUAL(1,second_calls);
    TEST_ASSERT_EQUAL(ESP_OK,RotaryEncoder_UnsubscribeRotation(NULL,rotation_first));
    TEST_ASSERT_EQUAL(ESP_OK,RotaryEncoder_UnsubscribeRotation(NULL,rotation_second));
}
void test_Encoder_ButtonCapturedAndWaitingContextsStayOwned(void) {
    TEST_ASSERT_EQUAL(ESP_OK,RotaryEncoder_OnButton(NULL,button_first,&context_one));
    TEST_ASSERT_EQUAL(ESP_OK,RotaryEncoder_OnButton(NULL,button_second,&context_two));
    RotaryEncoder_DispatchButton((RotaryEncoderButtonEvent){0});
    TEST_ASSERT_EQUAL(1,first_calls); TEST_ASSERT_EQUAL(1,second_calls);
    TEST_ASSERT_EQUAL(ESP_OK,RotaryEncoder_UnsubscribeButton(NULL,button_first));
    TEST_ASSERT_EQUAL(ESP_OK,RotaryEncoder_UnsubscribeButton(NULL,button_second));
}
void test_Encoder_NestedDispatchRetainsOuterReferences(void) {
    register_rotation_pair(); nested=true;
    RotaryEncoder_DispatchRotation((RotaryEncoderRotationEvent){0});
    TEST_ASSERT_EQUAL(2,first_calls); TEST_ASSERT_EQUAL(2,second_calls);
    TEST_ASSERT_EQUAL(0,rotaryEncoderService.rotation_slots[0].in_flight);
    TEST_ASSERT_EQUAL(0,rotaryEncoderService.rotation_slots[1].in_flight);
    TEST_ASSERT_EQUAL(ESP_OK,RotaryEncoder_UnsubscribeRotation(NULL,rotation_second));
}
void test_Encoder_DuplicateRegistrationIsIdempotentAndContextReplacementRejected(void) {
    register_rotation_pair();
    TEST_ASSERT_EQUAL(ESP_OK,RotaryEncoder_OnRotation(NULL,rotation_first,&context_one));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE,RotaryEncoder_OnRotation(NULL,rotation_first,&context_two));
    TEST_ASSERT_FALSE(rotaryEncoderService.rotation_slots[2].used);
    TEST_ASSERT_EQUAL(ESP_OK,RotaryEncoder_OnButton(NULL,button_first,&context_one));
    TEST_ASSERT_EQUAL(ESP_OK,RotaryEncoder_OnButton(NULL,button_first,&context_one));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE,RotaryEncoder_OnButton(NULL,button_first,&context_two));
}
void test_Encoder_ReferenceOverflowRejectsWholeDispatchWithoutPartialCapture(void) {
    register_rotation_pair(); rotaryEncoderService.rotation_slots[1].in_flight=UINT32_MAX;
    RotaryEncoder_DispatchRotation((RotaryEncoderRotationEvent){0});
    TEST_ASSERT_EQUAL(0,first_calls); TEST_ASSERT_EQUAL(0,second_calls);
    TEST_ASSERT_EQUAL(0,rotaryEncoderService.rotation_slots[0].in_flight);
    TEST_ASSERT_EQUAL(UINT32_MAX,rotaryEncoderService.rotation_slots[1].in_flight);
}
static TickType_t expiring_clock(void) { return mocked_xTaskGetTickCount_fake.call_count * (RE_STOP_TIMEOUT_MS + 1); }
void test_Encoder_StopTimeoutPreservesWorkerAndRetryCompletes(void) {
    rotaryEncoderService.state=SERVICE_STATE_STARTED;
    rotaryEncoderService.taskHandle=(void *)1;
    rotaryEncoderService.indev=(void *)2;
    mocked_xTaskGetTickCount_fake.custom_fake=expiring_clock;
    TEST_ASSERT_EQUAL(-1,RotaryEncoder_Stop(NULL));
    TEST_ASSERT_EQUAL_PTR((void *)1,rotaryEncoderService.taskHandle);
    TEST_ASSERT_EQUAL_PTR((void *)2,rotaryEncoderService.indev);
    TEST_ASSERT_EQUAL(0,mocked_vTaskDelete_fake.call_count);
    TEST_ASSERT_TRUE(atomic_load(&rotaryEncoderService.closing));
    TEST_ASSERT_EQUAL(-1,RotaryEncoder_Start(NULL));
    atomic_store(&rotaryEncoderService.worker_exited,true);
    TEST_ASSERT_EQUAL(0,RotaryEncoder_Stop(NULL));
    TEST_ASSERT_EQUAL(1,mocked_vTaskDelete_fake.call_count);
    TEST_ASSERT_NULL(rotaryEncoderService.taskHandle);
    TEST_ASSERT_EQUAL_PTR((void *)2,rotaryEncoderService.indev);
    TEST_ASSERT_EQUAL(SERVICE_STATE_STOPPED,rotaryEncoderService.state);
}
void test_Encoder_StopGPIOFailureRetainsOwnedHandlerAndWorker(void) {
    rotaryEncoderService.state=SERVICE_STATE_STARTED;
    rotaryEncoderService.button_attached=true;
    rotaryEncoderService.taskHandle=(void *)1;
    atomic_store(&rotaryEncoderService.worker_exited,true);
    mocked_gpio_isr_handler_remove_fake.return_val=ESP_FAIL;
    TEST_ASSERT_EQUAL(-1,RotaryEncoder_Stop(NULL));
    TEST_ASSERT_TRUE(rotaryEncoderService.button_attached);
    TEST_ASSERT_EQUAL(0,mocked_vTaskDelete_fake.call_count);
    mocked_gpio_isr_handler_remove_fake.return_val=ESP_OK;
    TEST_ASSERT_EQUAL(0,RotaryEncoder_Stop(NULL));
    TEST_ASSERT_FALSE(rotaryEncoderService.button_attached);
}
static void stop_inside_callback(RotaryEncoderRotationEvent event,void *ctx) {
    (void)event;(void)ctx;
    TEST_ASSERT_EQUAL(-1,RotaryEncoder_Stop(NULL));
    TEST_ASSERT_EQUAL(0,mocked_vTaskDelete_fake.call_count);
}
void test_Encoder_ReentrantStopCannotDeleteAcceptedCallback(void) {
    rotaryEncoderService.state=SERVICE_STATE_STARTED;
    TEST_ASSERT_EQUAL(ESP_OK,RotaryEncoder_OnRotation(NULL,stop_inside_callback,NULL));
    RotaryEncoder_DispatchRotation((RotaryEncoderRotationEvent){0});
    TEST_ASSERT_EQUAL(0,rotaryEncoderService.dispatches);
    TEST_ASSERT_TRUE(atomic_load(&rotaryEncoderService.closing));
    TEST_ASSERT_EQUAL(ESP_OK,RotaryEncoder_UnsubscribeRotation(NULL,stop_inside_callback));
    TEST_ASSERT_EQUAL(0,RotaryEncoder_Stop(NULL));
}
void test_Encoder_ClosingRejectsNewEventsAndSubscriptions(void) {
    atomic_store(&rotaryEncoderService.closing,true);
    rotaryEncoderService.eventQueue=(void *)1;
    RotaryEncoderEvent event={0}; RotaryEncoder_EnqueueEvent(&event);
    RotaryEncoder_EnqueueEvent(NULL);
    TEST_ASSERT_EQUAL(0,mocked_xQueueSend_fake.call_count);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE,RotaryEncoder_OnRotation(NULL,rotation_first,NULL));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE,RotaryEncoder_OnButton(NULL,button_first,NULL));
    RotaryEncoder_DispatchRotation((RotaryEncoderRotationEvent){0});
    TEST_ASSERT_EQUAL(0,rotaryEncoderService.dispatches);
}
void test_Encoder_ConcurrentLifecycleAdmissionDoesNotReleaseOtherOwner(void) {
    atomic_store(&rotaryEncoderService.lifecycle_busy,true);
    TEST_ASSERT_EQUAL(-1,RotaryEncoder_Start(NULL));
    TEST_ASSERT_EQUAL(-1,RotaryEncoder_Stop(NULL));
    TEST_ASSERT_EQUAL(-1,RotaryEncoder_Initialize(NULL,NULL));
    TEST_ASSERT_TRUE(atomic_load(&rotaryEncoderService.lifecycle_busy));
}
void test_Encoder_PartialQueueInitializationRollsBackOnlyNewQueue(void) {
    QueueHandle_t sequence[]={(void *)1,NULL};
    SET_RETURN_SEQ(mocked_xQueueCreate,sequence,2);
    TEST_ASSERT_EQUAL(-1,RotaryEncoder_Initialize(NULL,NULL));
    TEST_ASSERT_NULL(rotaryEncoderService.eventQueue);
    TEST_ASSERT_NULL(rotaryEncoderService.rawButtonQueue);
    TEST_ASSERT_EQUAL(1,mocked_vQueueDelete_fake.call_count);
    TEST_ASSERT_EQUAL_PTR((void *)1,mocked_vQueueDelete_fake.arg0_val);
}
void test_Encoder_LVGLDeltaIsBoundedWithoutSignedOverflow(void) {
    rotaryEncoderService.lv_last_position=INT32_MIN;
    rotaryEncoderService.status.absolute_position=INT32_MAX;
    lv_indev_data_t data={0};
    RotaryEncoder_ReadCB(NULL,&data);
    TEST_ASSERT_EQUAL(INT16_MAX,data.enc_diff);
    TEST_ASSERT_EQUAL(INT32_MIN+INT16_MAX,rotaryEncoderService.lv_last_position);
    rotaryEncoderService.lv_last_position=INT32_MAX;
    rotaryEncoderService.status.absolute_position=INT32_MIN;
    RotaryEncoder_ReadCB(NULL,&data);
    TEST_ASSERT_EQUAL(INT16_MIN,data.enc_diff);
    TEST_ASSERT_EQUAL(INT32_MAX+INT16_MIN,rotaryEncoderService.lv_last_position);
}
void test_Encoder_ResetAndClosingCannotGeneratePhantomLVGLRotation(void) {
    rotaryEncoderService.lv_last_position=100;
    rotaryEncoderService.status.absolute_position=100;
    rotaryEncoderService.button_has_time=true;
    rotaryEncoderService.pcnt_has_fire_time=true;
    RotaryEncoder_ResetCounters();
    lv_indev_data_t data={0}; RotaryEncoder_ReadCB(NULL,&data);
    TEST_ASSERT_EQUAL(0,data.enc_diff);
    TEST_ASSERT_FALSE(rotaryEncoderService.button_has_time);
    TEST_ASSERT_FALSE(rotaryEncoderService.pcnt_has_fire_time);
    atomic_store(&rotaryEncoderService.closing,true);
    rotaryEncoderService.status.absolute_position=25;
    rotaryEncoderService.status.button_held=true;
    RotaryEncoder_ReadCB(NULL,&data);
    TEST_ASSERT_EQUAL(0,data.enc_diff); TEST_ASSERT_EQUAL(LV_INDEV_STATE_RELEASED,data.state);
}
static BaseType_t execute_reset_notification(TaskHandle_t worker,uint32_t bits,eNotifyAction action) {
    (void)worker;(void)bits;(void)action;RotaryEncoder_PerformReset();return pdPASS;
}
void test_Encoder_ResetWorkerAcknowledgmentAndSDKFailurePreserveCounters(void) {
    rotaryEncoderService.state=SERVICE_STATE_STARTED;
    rotaryEncoderService.taskHandle=(void *)1;
    rotaryEncoderService.pcnt_unit=(void *)2;
    rotaryEncoderService.status.absolute_position=42;
    mocked_xTaskNotify_fake.custom_fake=execute_reset_notification;
    clear_result=ESP_FAIL;
    TEST_ASSERT_EQUAL(ESP_FAIL,RotaryEncoder_Reset(NULL));
    TEST_ASSERT_EQUAL(42,rotaryEncoderService.status.absolute_position);
    TEST_ASSERT_FALSE(rotaryEncoderService.control_active);
    clear_result=ESP_OK;
    TEST_ASSERT_EQUAL(ESP_OK,RotaryEncoder_Reset(NULL));
    TEST_ASSERT_EQUAL(0,rotaryEncoderService.status.absolute_position);
    TEST_ASSERT_TRUE(atomic_load(&rotaryEncoderService.reset_done));
}
void test_Encoder_ResetTimeoutRetainsSDKOwnershipUntilWorkerCancellation(void) {
    rotaryEncoderService.state=SERVICE_STATE_STARTED;
    rotaryEncoderService.taskHandle=(void *)1;
    rotaryEncoderService.pcnt_unit=(void *)2;
    mocked_xTaskNotify_fake.return_val=pdPASS;
    mocked_xTaskGetTickCount_fake.custom_fake=expiring_clock;
    TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT,RotaryEncoder_Reset(NULL));
    TEST_ASSERT_TRUE(rotaryEncoderService.control_active);
    TEST_ASSERT_EQUAL(-1,RotaryEncoder_Stop(NULL));
    TEST_ASSERT_EQUAL_PTR((void *)2,rotaryEncoderService.pcnt_unit);
    TEST_ASSERT_EQUAL(0,mocked_vTaskDelete_fake.call_count);
    TEST_ASSERT_TRUE(atomic_exchange(&rotaryEncoderService.reset_requested,false));
    RotaryEncoder_FinishReset(ESP_ERR_INVALID_STATE);
    TEST_ASSERT_FALSE(rotaryEncoderService.control_active);
}
void test_Encoder_ResetNotificationFailureRollsBackUnclaimedRequest(void) {
    rotaryEncoderService.state=SERVICE_STATE_STARTED;
    rotaryEncoderService.taskHandle=(void *)1;
    TEST_ASSERT_EQUAL(ESP_FAIL,RotaryEncoder_Reset(NULL));
    TEST_ASSERT_FALSE(rotaryEncoderService.control_active);
    TEST_ASSERT_FALSE(atomic_load(&rotaryEncoderService.reset_requested));
}
void test_Encoder_ResetCLIRejectsExtraArgumentsAndPropagatesSDKFailure(void) {
    rotaryEncoderService.base.Reset=RotaryEncoder_Reset;
    rotaryEncoderService.state=SERVICE_STATE_INITIALIZED;
    rotaryEncoderService.pcnt_unit=(void *)1;
    rotaryEncoderService.status.absolute_position=12;
    char *extra[]={"ENCRESET","extra"};
    TEST_ASSERT_EQUAL(1,cmd_enc_reset(2,extra));
    TEST_ASSERT_EQUAL(12,rotaryEncoderService.status.absolute_position);
    char *valid[]={"ENCRESET"}; clear_result=ESP_FAIL;
    TEST_ASSERT_EQUAL(1,cmd_enc_reset(1,valid));
    TEST_ASSERT_EQUAL(12,rotaryEncoderService.status.absolute_position);
    clear_result=ESP_OK;
    TEST_ASSERT_EQUAL(0,cmd_enc_reset(1,valid));
    TEST_ASSERT_EQUAL(0,rotaryEncoderService.status.absolute_position);
}
static int32_t typed_attach_result, typed_detach_result;
static unsigned typed_detaches;
static int32_t typed_configure(IPinManager *self,PinFunction_t role,PinMode_t mode,PinPull_t pull) {
    (void)self;TEST_ASSERT_EQUAL(PIN_FUNC_ROTARY_SW,role);TEST_ASSERT_EQUAL(PIN_MODE_INPUT,mode);TEST_ASSERT_EQUAL(PIN_PULL_UP,pull);return 0;
}
static int32_t typed_debounce(IPinManager *self,PinFunction_t role,uint32_t ms) {
    (void)self;TEST_ASSERT_EQUAL(PIN_FUNC_ROTARY_SW,role);TEST_ASSERT_EQUAL(0,ms);return 0;
}
static int32_t typed_attach(IPinManager *self,PinFunction_t role,PinInterruptType_t type,PinInterruptHandler_t handler,void *arg) {
    (void)self;(void)arg;TEST_ASSERT_EQUAL(PIN_FUNC_ROTARY_SW,role);TEST_ASSERT_EQUAL(PIN_INTR_ANYEDGE,type);
    TEST_ASSERT_TRUE(handler==RotaryEncoder_ButtonPinISR);return typed_attach_result;
}
static int32_t typed_detach(IPinManager *self,PinFunction_t role) {
    (void)self;TEST_ASSERT_EQUAL(PIN_FUNC_ROTARY_SW,role);typed_detaches++;return typed_detach_result;
}
static IPinManager typed_pins={.Configure=typed_configure,.SetDebounce=typed_debounce,.AttachInterrupt=typed_attach,.DetachInterrupt=typed_detach};
static void prepare_typed_start(void) {
    rotaryEncoderService.state=SERVICE_STATE_INITIALIZED;
    rotaryEncoderService.pin_manager=&typed_pins;
    typed_attach_result=typed_detach_result=0;typed_detaches=0;
}
void test_Encoder_TypedIRQConflictNeverDetachesAnotherBinding(void) {
    prepare_typed_start();typed_attach_result=PIN_MANAGER_ERR_BUSY;
    TEST_ASSERT_EQUAL(-1,RotaryEncoder_Start(NULL));
    TEST_ASSERT_EQUAL(0,typed_detaches);
    TEST_ASSERT_FALSE(rotaryEncoderService.button_attached);
    TEST_ASSERT_EQUAL(0,mocked_gpio_isr_handler_add_fake.call_count);
    TEST_ASSERT_EQUAL(0,mocked_gpio_config_fake.call_count);
}
void test_Encoder_TypedIRQTaskFailureRollsBackThroughOwner(void) {
    prepare_typed_start();
    TEST_ASSERT_EQUAL(-1,RotaryEncoder_Start(NULL));
    TEST_ASSERT_EQUAL(1,typed_detaches);
    TEST_ASSERT_FALSE(rotaryEncoderService.button_attached);
    TEST_ASSERT_EQUAL(0,mocked_gpio_isr_handler_remove_fake.call_count);
}
void test_Encoder_TypedIRQRollbackFailureRetainsCleanupForRetry(void) {
    prepare_typed_start();typed_attach_result=PIN_MANAGER_ERR_SDK;typed_detach_result=PIN_MANAGER_ERR_BUSY;
    TEST_ASSERT_EQUAL(-1,RotaryEncoder_Start(NULL));
    TEST_ASSERT_TRUE(rotaryEncoderService.button_attached);
    TEST_ASSERT_EQUAL(-1,RotaryEncoder_Start(NULL));
    typed_detach_result=0;
    TEST_ASSERT_EQUAL(0,RotaryEncoder_Stop(NULL));
    TEST_ASSERT_FALSE(rotaryEncoderService.button_attached);
    TEST_ASSERT_EQUAL(2,typed_detaches);
    TEST_ASSERT_EQUAL(0,mocked_gpio_isr_handler_remove_fake.call_count);
}

static RotaryEncoderEvent simulated_event;
static BaseType_t capture_simulation(QueueHandle_t queue,const void *event,TickType_t timeout) {
    (void)queue;(void)timeout;simulated_event=*(const RotaryEncoderEvent *)event;return pdPASS;
}
void test_Encoder_SimulationUsesWorkerAndLeavesPhysicalButtonStateUntouched(void) {
    rotaryEncoderService.state=SERVICE_STATE_STARTED;
    rotaryEncoderService.taskHandle=(void *)1;
    rotaryEncoderService.eventQueue=(void *)2;
    rotaryEncoderService.status.button_held=true;
    rotaryEncoderService.status.total_clicks=7;
    mocked_xTaskNotify_fake.custom_fake=execute_reset_notification;
    mocked_xQueueSend_fake.custom_fake=capture_simulation;
    TEST_ASSERT_EQUAL(ESP_OK,RotaryEncoder_Simulate(RE_SIM_CCW,3));
    TEST_ASSERT_EQUAL(-3,simulated_detents);
    TEST_ASSERT_EQUAL(ESP_OK,RotaryEncoder_Simulate(RE_SIM_DOUBLE_CLICK,1));
    TEST_ASSERT_EQUAL(GESTURE_DOUBLE_CLICK,simulated_event.payload.button.gesture);
    TEST_ASSERT_EQUAL(2,simulated_event.payload.button.click_count);
    TEST_ASSERT_TRUE(rotaryEncoderService.status.button_held);
    TEST_ASSERT_EQUAL(7,rotaryEncoderService.status.total_clicks);
    TEST_ASSERT_FALSE(rotaryEncoderService.control_active);
    mocked_xQueueSend_fake.custom_fake=NULL;
    TEST_ASSERT_EQUAL(ESP_FAIL,RotaryEncoder_Simulate(RE_SIM_CLICK,1));
}
void test_Encoder_SimulationRejectsInvalidRequestsAndClosingWorker(void) {
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,RotaryEncoder_Simulate(RE_SIM_CW,0));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,RotaryEncoder_Simulate(RE_SIM_CW,32768));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,RotaryEncoder_Simulate(RE_SIM_CLICK,2));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,RotaryEncoder_Simulate((RotaryEncoderSimulation)99,1));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE,RotaryEncoder_Simulate(RE_SIM_CW,1));
    rotaryEncoderService.state=SERVICE_STATE_STARTED;
    rotaryEncoderService.taskHandle=(void *)1;
    atomic_store(&rotaryEncoderService.closing,true);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE,RotaryEncoder_Simulate(RE_SIM_CW,1));
    TEST_ASSERT_EQUAL(0,mocked_xTaskNotify_fake.call_count);
    char *invalid[]={"ENCSIM","CW","-1"};
    TEST_ASSERT_EQUAL(1,cmd_enc_sim(3,invalid));
    invalid[2]="32768";
    TEST_ASSERT_EQUAL(1,cmd_enc_sim(3,invalid));
    TEST_ASSERT_EQUAL(0,mocked_xTaskNotify_fake.call_count);
}
void test_Encoder_ReadOnlySelfTestDoesNotProduceInputOrResetState(void) {
    rotaryEncoderService.state=SERVICE_STATE_STARTED;
    rotaryEncoderService.base.base.CheckHealth=RotaryEncoder_CheckHealth;
    rotaryEncoderService.status.absolute_position=91;
    rotaryEncoderService.status.total_clicks=17;
    rotaryEncoderService.gesture.click_count=2;
    char *args[]={"ENCTEST"};
    TEST_ASSERT_EQUAL(0,cmd_enc_test(1,args));
    TEST_ASSERT_EQUAL(91,rotaryEncoderService.status.absolute_position);
    TEST_ASSERT_EQUAL(17,rotaryEncoderService.status.total_clicks);
    TEST_ASSERT_EQUAL(2,rotaryEncoderService.gesture.click_count);
    TEST_ASSERT_EQUAL(0,mocked_xQueueSend_fake.call_count);
    TEST_ASSERT_EQUAL(0,simulated_detents);
    atomic_store(&rotaryEncoderService.closing,true);
    TEST_ASSERT_EQUAL(1,cmd_enc_test(1,args));
}
static unsigned command_registration_calls,command_registration_failure;
static esp_err_t register_command_fixture(IDebugConsole *console,const esp_console_cmd_t *command,CommandCategory category) {
    (void)console;TEST_ASSERT_NOT_NULL(command->command);TEST_ASSERT_EQUAL(CMD_CAT_HARDWARE,category);
    return ++command_registration_calls==command_registration_failure?ESP_FAIL:ESP_OK;
}
void test_Encoder_CommandRegistrationChecksFailuresAndRetriesOnlyMissingCommands(void) {
    IDebugConsole console={.RegisterCommandWithCategory=register_command_fixture};
    IDebugConsole other={.RegisterCommandWithCategory=register_command_fixture};
    command_registration_calls=0;command_registration_failure=3;
    TEST_ASSERT_EQUAL(ESP_FAIL,RotaryNative_RegisterCommands(&console));
    TEST_ASSERT_TRUE(encoder_command_registered[0]);TEST_ASSERT_TRUE(encoder_command_registered[1]);
    TEST_ASSERT_FALSE(encoder_command_registered[2]);
    command_registration_failure=0;
    TEST_ASSERT_EQUAL(ESP_OK,RotaryNative_RegisterCommands(&console));
    TEST_ASSERT_EQUAL(10,command_registration_calls);
    for (size_t i=0;i<sizeof(encoder_command_registered)/sizeof(encoder_command_registered[0]);++i)
        TEST_ASSERT_TRUE(encoder_command_registered[i]);
    TEST_ASSERT_EQUAL(ESP_OK,RotaryNative_RegisterCommands(&console));
    TEST_ASSERT_EQUAL(10,command_registration_calls);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE,RotaryNative_RegisterCommands(&other));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,RotaryNative_RegisterCommands(NULL));
}

static unsigned encoder_save_calls;
static esp_err_t encoder_save_result;
static int32_t capture_encoder_save(IConfigurationManager *self, const char *block,
    const ConfigurationFieldUpdate *fields, size_t count, bool persist) {
    (void)self;
    TEST_ASSERT_EQUAL_STRING("hardware", block); TEST_ASSERT_EQUAL(4, count); TEST_ASSERT_TRUE(persist);
    TEST_ASSERT_EQUAL(2, *(const uint8_t *)fields[0].data);
    TEST_ASSERT_EQUAL(0, *(const uint8_t *)fields[1].data);
    TEST_ASSERT_EQUAL(50, *(const uint8_t *)fields[2].data);
    TEST_ASSERT_EQUAL(1, *(const uint8_t *)fields[3].data);
    ++encoder_save_calls; return encoder_save_result;
}
void test_EncoderSavePersistsOneCoherentRuntimeSnapshotAndReportsFailure(void) {
    RotaryEncoder_GetModule();
    rotaryEncoderService.runtime_config = RotaryEncoder_GetDefaultRuntimeConfig();
    rotaryEncoderService.runtime_config.steps_per_detent=2;
    rotaryEncoderService.runtime_config.accel_enabled=false;
    rotaryEncoderService.runtime_config.accel_threshold_ms=50;
    rotaryEncoderService.runtime_config.invert_direction=true;
    IConfigurationManager config={.UpdateFields=capture_encoder_save};
    mocked_ConfigurationManager_GetInstance_fake.return_val=&config;
    char *args[]={"ENCSAVE",NULL}; encoder_save_calls=0; encoder_save_result=ESP_FAIL;
    TEST_ASSERT_EQUAL(1,cmd_enc_save(1,args)); TEST_ASSERT_EQUAL(1,encoder_save_calls);
    encoder_save_result=ESP_OK;
    TEST_ASSERT_EQUAL(0,cmd_enc_save(1,args)); TEST_ASSERT_EQUAL(2,encoder_save_calls);
    TEST_ASSERT_EQUAL(1,cmd_enc_save(2,args)); TEST_ASSERT_EQUAL(2,encoder_save_calls);
    mocked_ConfigurationManager_GetInstance_fake.return_val=NULL;
}

static esp_err_t encoder_boot_snapshot_result;
static esp_err_t encoder_boot_snapshot(IConfigurationManager *self, SystemSettings *out) {
    (void)self;
    if (encoder_boot_snapshot_result != ESP_OK) return encoder_boot_snapshot_result;
    memset(out,0,sizeof(*out));
    out->hardware.encoder_steps_per_detent=2;
    out->hardware.encoder_accel_enabled=0;
    out->hardware.encoder_accel_threshold_ms=50;
    out->hardware.encoder_invert_direction=1;
    return ESP_OK;
}
void test_EncoderStartupAppliesPersistedControlsAndRejectsUnavailableSnapshot(void) {
    IConfigurationManager config={.GetSnapshot=encoder_boot_snapshot};
    mocked_ConfigurationManager_GetInstance_fake.return_val=&config;
    encoder_boot_snapshot_result=ESP_ERR_TIMEOUT;
    TEST_ASSERT_NOT_EQUAL(ESP_OK,RotaryEncoder_InitializeOwned(NULL,NULL));
    encoder_boot_snapshot_result=ESP_OK;
    mocked_xQueueCreate_fake.return_val=(QueueHandle_t)1;
    TEST_ASSERT_EQUAL(ESP_OK,RotaryEncoder_InitializeOwned(NULL,NULL));
    TEST_ASSERT_EQUAL(2,rotaryEncoderService.runtime_config.steps_per_detent);
    TEST_ASSERT_FALSE(rotaryEncoderService.runtime_config.accel_enabled);
    TEST_ASSERT_EQUAL(50,rotaryEncoderService.runtime_config.accel_threshold_ms);
    TEST_ASSERT_TRUE(rotaryEncoderService.runtime_config.invert_direction);
    mocked_ConfigurationManager_GetInstance_fake.return_val=NULL;
}

#include "Infrastructure/support/console_capture.h"
void test_EncoderMachineStatusIgnoresLocaleAndRejectsMissingRuntime(void) {
    RotaryEncoder_GetModule();
    rotaryEncoderService.runtime_config=RotaryEncoder_GetDefaultRuntimeConfig();
    char *args[]={"ENC","--machine"};char en[2048],tr[2048];
    TEST_ASSERT_TRUE(Localization_Apply("en"));
    TEST_ASSERT_EQUAL(0,test_capture_console(cmd_enc_info,2,args,en,sizeof(en)));
    TEST_ASSERT_NOT_NULL(strstr(en,"type=encoder_status"));
    TEST_ASSERT_TRUE(Localization_Apply("tr"));
    TEST_ASSERT_EQUAL(0,test_capture_console(cmd_enc_info,2,args,tr,sizeof(tr)));
    TEST_ASSERT_EQUAL_STRING(en,tr);
    IRotaryEncoder saved=rotaryEncoderService.base;
    rotaryEncoderService.base.GetRuntimeConfig=NULL;
    int result=test_capture_console(cmd_enc_info,2,args,tr,sizeof(tr));
    rotaryEncoderService.base=saved;
    TEST_ASSERT_EQUAL(1,result);TEST_ASSERT_NULL(strstr(tr,"type=encoder_status"));
    TEST_ASSERT_TRUE(Localization_Apply("en"));
}

void test_EncoderMachineRejectedInputDoesNotChangeRuntime(void) {
    RotaryEncoder_GetModule();
    RotaryEncoderRuntimeConfig before=rotaryEncoderService.runtime_config;
    char *args[]={"ENCCONFIG","DETENTS","999","--machine"};char output[1024];
    TEST_ASSERT_EQUAL(1,test_capture_console(cmd_enc_config_machine,4,args,output,sizeof(output)));
    TEST_ASSERT_NOT_NULL(strstr(output,"type=encoder_command command=ENCCONFIG result=rejected error=1"));
    TEST_ASSERT_EQUAL(before.steps_per_detent,rotaryEncoderService.runtime_config.steps_per_detent);
    TEST_ASSERT_EQUAL(1,cmd_enc_config_machine(4,NULL));
}
