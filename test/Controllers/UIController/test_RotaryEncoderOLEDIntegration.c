#include "ui_fixture.h"
static unsigned power_calls, button_calls, encoder_calls, activity_calls;
static int8_t last_direction;
static bool last_power;
static esp_err_t capture_power(IOLED *self, bool on) { (void)self; ++power_calls; last_power=on; return ESP_OK; }
static void capture_button(IMenuManager *self) { (void)self; ++button_calls; }
static void capture_encoder(IMenuManager *self, int8_t direction) { (void)self; ++encoder_calls; last_direction=direction; }
static void capture_activity(IMenuManager *self) { ++activity_calls; ((MockMenuManager *)self)->lastActivityTime=1; }
static bool display_lock(IOLED *self) { (void)self; return true; }
static void display_unlock(IOLED *self) { (void)self; }
static IOLED output = {.SetPower=capture_power,.Lock=display_lock,.Unlock=display_unlock};
void setUp(void) {
    UIFixture_SetUp();
    IUIController *ui=UIController_GetModule();
    TEST_ASSERT_EQUAL(0,ui->InitializeWithConfig(ui,UIController_GetDefaultConfig(),NULL));
    uiControllerInstance.oled=&output;
    uiControllerInstance.menu=&mockMenuMgr.base;
    mockMenuMgr.base.UpdateActivity=capture_activity;
    mockMenuMgr.base.ProcessButton=capture_button;
    mockMenuMgr.base.ProcessEncoder=capture_encoder;
    BootAnimation_Skip();
    power_calls=button_calls=encoder_calls=activity_calls=0;
    last_direction=0;
    uiControllerInstance.isScreenOn=false;
}
void tearDown(void) { UIFixture_TearDown(); }
void test_EncoderClickWakesOLEDWithoutOpeningMenu(void) {
    UIController_OnRotaryClicked((BusEvent){.eventName=RE_EVENT_BUTTON_CLICK});
    TEST_ASSERT_EQUAL(UI_EVENT_ENCODER_CLICK,lastSentUIEvent.type);
    UIController_HandleEvent(&lastSentUIEvent);
    TEST_ASSERT_FALSE(uiControllerInstance.isScreenOn);
    TEST_ASSERT_TRUE(uiControllerInstance.wakeNavigationPending);
    uiControllerInstance.isScreenOn = true; uiControllerInstance.wakeNavigationPending = false;
    TEST_ASSERT_EQUAL(0,power_calls);

    TEST_ASSERT_EQUAL(0,button_calls);
    UIController_OnRotaryClicked((BusEvent){.eventName=RE_EVENT_BUTTON_CLICK});
    UIController_HandleEvent(&lastSentUIEvent);
    TEST_ASSERT_EQUAL(1,button_calls);
}
void test_EncoderRotationIgnoredWhileAsleepThenNavigatesWithCopiedDirection(void) {
    RotaryEncoderRotationEvent rotation={.steps=-1};
    UIController_OnRotaryRotated((BusEvent){.eventName=RE_EVENT_ROTATED,.data=&rotation,.dataSize=sizeof(rotation)});
    rotation.steps=7;
    TEST_ASSERT_EQUAL(-1,lastSentUIEvent.data.encoder.difference);
    for (unsigned i = 0; i < 32; ++i) UIController_HandleEvent(&lastSentUIEvent);
    TEST_ASSERT_FALSE(uiControllerInstance.isScreenOn);
    TEST_ASSERT_FALSE(uiControllerInstance.wakeNavigationPending);
    TEST_ASSERT_EQUAL(0,activity_calls);
    TEST_ASSERT_EQUAL(0,power_calls);
    TEST_ASSERT_EQUAL(0,encoder_calls);
    uiControllerInstance.isScreenOn = true; uiControllerInstance.wakeNavigationPending = false;
    UIController_HandleEvent(&lastSentUIEvent);
    TEST_ASSERT_EQUAL(1,encoder_calls);
    TEST_ASSERT_EQUAL(-1,last_direction);
    TEST_ASSERT_EQUAL(1,activity_calls);
}
void test_InvalidEncoderPayloadDoesNotReachDisplayQueue(void) {
    RESET_FAKE(mocked_xQueueSend);
    UIController_OnRotaryRotated((BusEvent){.eventName=RE_EVENT_ROTATED});
    uint8_t truncated=1;
    UIController_OnRotaryRotated((BusEvent){.eventName=RE_EVENT_ROTATED,.data=&truncated,.dataSize=1});
    TEST_ASSERT_EQUAL(0,mocked_xQueueSend_fake.call_count);
    TEST_ASSERT_EQUAL(0,power_calls);
}

static bool pairing_active;
static unsigned pairing_connect_calls, pairing_disconnect_calls;
static int32_t pairing_result;
static bool pairing_status(IZigbee *self) { (void)self; return pairing_active; }
static int32_t pairing_connect(IZigbee *self) { (void)self; ++pairing_connect_calls; return pairing_result; }
static int32_t pairing_disconnect(IZigbee *self) { (void)self; ++pairing_disconnect_calls; return pairing_result; }
static void pairing_edit(IMenuManager *self, bool editing) { ((MockMenuManager *)self)->isEditing = editing; }
static IZigbee pairing_radio;
static void prepare_pairing(bool active, bool back) {
  pairing_radio = (IZigbee){.IsPairing = pairing_status, .ConnectToNetwork = pairing_connect,
    .DisconnectFromNetwork = pairing_disconnect};
  pairing_active = active; pairing_connect_calls = pairing_disconnect_calls = 0; pairing_result = ESP_OK;
  uiControllerInstance.zigbee = &pairing_radio;
  uiControllerInstance.isScreenOn = true;
  uiControllerInstance.zbActionSelected = back;
  mockMenuMgr.currentState = UI_STATE_ZIGBEE_PAIR; mockMenuMgr.isEditing = true;
  mockMenuMgr.base.SetEditing = pairing_edit;
  RESET_FAKE(mocked_vTaskDelay);
}
void test_PairingStartUsesOneSdkRequestWithoutBlockingDelay(void) {
  prepare_pairing(false, false);
  UIEvent event = {.type = UI_EVENT_ENCODER_CLICK};
  UIController_HandleEvent(&event);
  TEST_ASSERT_EQUAL(1, pairing_connect_calls);
  TEST_ASSERT_EQUAL(0, pairing_disconnect_calls);
  TEST_ASSERT_EQUAL(0, mocked_vTaskDelay_fake.call_count);
}
void test_PairingBackFailurePreservesScreenAndSelection(void) {
  prepare_pairing(true, true); pairing_result = ESP_FAIL;
  UIEvent event = {.type = UI_EVENT_ENCODER_CLICK};
  UIController_HandleEvent(&event);
  TEST_ASSERT_EQUAL(1, pairing_disconnect_calls);
  TEST_ASSERT_EQUAL(UI_STATE_ZIGBEE_PAIR, mockMenuMgr.currentState);
  TEST_ASSERT_TRUE(mockMenuMgr.isEditing);
  TEST_ASSERT_TRUE(uiControllerInstance.zbActionSelected);
}
void test_PairingBackSuccessLeavesScreenAfterAcceptedCancellation(void) {
  prepare_pairing(true, true);
  UIEvent event = {.type = UI_EVENT_ENCODER_CLICK};
  UIController_HandleEvent(&event);
  TEST_ASSERT_EQUAL(1, pairing_disconnect_calls);
  TEST_ASSERT_EQUAL(UI_STATE_CAT_DEVICE, mockMenuMgr.currentState);
  TEST_ASSERT_FALSE(mockMenuMgr.isEditing);
  TEST_ASSERT_FALSE(uiControllerInstance.zbActionSelected);
}
void test_PairingMissingCallbacksCannotInvokeNetworkOrLeaveScreen(void) {
  prepare_pairing(true, true); pairing_radio.DisconnectFromNetwork = NULL;
  UIEvent event = {.type = UI_EVENT_ENCODER_CLICK}; UIController_HandleEvent(&event);
  TEST_ASSERT_EQUAL(UI_STATE_ZIGBEE_PAIR, mockMenuMgr.currentState);
  TEST_ASSERT_EQUAL(0, pairing_disconnect_calls);
  pairing_radio.IsPairing = NULL; uiControllerInstance.zbActionSelected = false;
  UIController_HandleEvent(&event);
  TEST_ASSERT_EQUAL(0, pairing_connect_calls);
}

void test_PairingTimeoutWrapAndFailureRetryPreserveSelection(void) {
  prepare_pairing(true, true); pairing_result = ESP_FAIL;
  uint32_t start = UINT32_MAX - 100u;
  TEST_ASSERT_TRUE(UI_UpdatePairingTimeout(start));
  TEST_ASSERT_TRUE(UI_UpdatePairingTimeout(start + UI_PAIRING_TIMEOUT_MS - 1u));
  TEST_ASSERT_EQUAL(0, pairing_disconnect_calls);
  TEST_ASSERT_TRUE(UI_UpdatePairingTimeout(start + UI_PAIRING_TIMEOUT_MS));
  TEST_ASSERT_EQUAL(1, pairing_disconnect_calls);
  TEST_ASSERT_TRUE(uiControllerInstance.zbActionSelected);
  TEST_ASSERT_TRUE(UI_UpdatePairingTimeout(start + UI_PAIRING_TIMEOUT_MS + UI_PAIRING_CANCEL_RETRY_MS - 1u));
  TEST_ASSERT_EQUAL(1, pairing_disconnect_calls);
  pairing_result = ESP_OK;
  TEST_ASSERT_TRUE(UI_UpdatePairingTimeout(start + UI_PAIRING_TIMEOUT_MS + UI_PAIRING_CANCEL_RETRY_MS));
  TEST_ASSERT_EQUAL(2, pairing_disconnect_calls);
  TEST_ASSERT_FALSE(uiControllerInstance.zbActionSelected);
  pairing_active = false;
  TEST_ASSERT_FALSE(UI_UpdatePairingTimeout(0));
  TEST_ASSERT_FALSE(uiControllerInstance.pairingTimeoutTracked);
}
void test_PairingTimeoutCanStartAtZeroAndCancelAfterFullInterval(void) {
  prepare_pairing(true, false);
  TEST_ASSERT_TRUE(UI_UpdatePairingTimeout(0));
  TEST_ASSERT_TRUE(UI_UpdatePairingTimeout(UI_PAIRING_TIMEOUT_MS - 1u));
  TEST_ASSERT_EQUAL(0, pairing_disconnect_calls);
  UI_UpdatePairingTimeout(UI_PAIRING_TIMEOUT_MS);
  TEST_ASSERT_EQUAL(1, pairing_disconnect_calls);
}
