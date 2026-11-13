#ifndef RACOOL_TEST_NATIVE
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#endif
#include "Communication/Zigbee/Handlers/SignalHandlerCore.h"
#include "Infrastructure/support/test_mocks.h"
#include "unity.h"
#include "Communication/Zigbee/Handlers/SignalHandlerCore.c"

#include "../test_zigbee_mock_eventbus.h"

void setUp_SignalHandlerCore(void) {
    ZIGBEE_COMMON_FAKES(RESET_FAKE);
    mock_publish_called = 0;
    last_event_name = NULL;
    mock_event_bus.Publish = mock_publish;
    ServiceLocator_GetEventBus_fake.return_val = &mock_event_bus;
}

void tearDown_SignalHandlerCore(void) {}

void test_SignalHandlerCore_SkipStartup(void) {
    IZigbeeSignalHandler *handler = SignalHandlerCore_Create();
    
    handler->HandleSignal(handler, EZB_ZDO_SIGNAL_SKIP_STARTUP, ESP_OK, NULL);
    
    TEST_ASSERT_EQUAL(1, mocked_ezb_bdb_start_top_level_commissioning_fake.call_count);
    TEST_ASSERT_EQUAL(EZB_BDB_MODE_INITIALIZATION, mocked_ezb_bdb_start_top_level_commissioning_fake.arg0_val);
}

void test_SignalHandlerCore_FactoryNewWaitsForUser(void) {
    IZigbeeSignalHandler *handler = SignalHandlerCore_Create();
    
    mocked_ezb_bdb_is_factory_new_fake.return_val = true;
    
    handler->HandleSignal(handler, EZB_BDB_SIGNAL_DEVICE_FIRST_START, ESP_OK, NULL);
    
    TEST_ASSERT_EQUAL(0, mocked_ezb_bdb_start_top_level_commissioning_fake.call_count);
    TEST_ASSERT_EQUAL(0, mock_publish_called);
}

void test_SignalHandlerCore_SteeringSuccess(void) {
    IZigbeeSignalHandler *handler = SignalHandlerCore_Create();
    
    handler->HandleSignal(handler, EZB_BDB_SIGNAL_STEERING, ESP_OK, NULL);
    
    // Should publish zigbee_connected and zigbee_pairing_stop
    TEST_ASSERT_EQUAL(2, mock_publish_called);
    TEST_ASSERT_EQUAL_STRING("zigbee_pairing_stop", last_event_name);
}

void test_SignalHandlerCore_All(void) {
    RUN_TEST(test_SignalHandlerCore_SkipStartup);
    RUN_TEST(test_SignalHandlerCore_FactoryNewWaitsForUser);
    RUN_TEST(test_SignalHandlerCore_SteeringSuccess);
}

#ifndef RACOOL_TARGET_TESTING
int main(void) {
    UNITY_BEGIN();
    test_SignalHandlerCore_All();
    return UNITY_END();
}
#endif
