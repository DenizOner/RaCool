#include "unity.h"
#include "Infrastructure/support/test_mocks.h"
#include "Core/EventBus/IEventBus.h"
static IEventBus *EventConsoleTest_Bus(void);
#undef GetEventBus
#define GetEventBus EventConsoleTest_Bus
#include "Core/EventBus/EventBusConsoleCommands.c"
#undef GetEventBus
static IEventBus bus;
static int32_t publishResult;
static unsigned publishes;
static IEventBus *EventConsoleTest_Bus(void) { return &bus; }
static int32_t Publish(BusEvent event) { TEST_ASSERT_EQUAL_STRING("cli-test",event.eventName); ++publishes; return publishResult; }
void setUp(void) { bus=(IEventBus){.Publish=Publish}; publishResult=0; publishes=0; }
void tearDown(void) {}
void test_EventBusConsoleRejectsMalformedAndMissingMethods(void) {
    char *extra[]={"bus_publish","cli-test","extra"}; TEST_ASSERT_NOT_EQUAL(0,cmd_bus_publish(3,extra));
    TEST_ASSERT_NOT_EQUAL(0,cmd_bus_publish(2,NULL));
    extra[1]=""; TEST_ASSERT_NOT_EQUAL(0,cmd_bus_publish(2,extra));
    TEST_ASSERT_EQUAL(0,publishes);
    char *status[]={"bus_status"}; TEST_ASSERT_NOT_EQUAL(0,cmd_bus_status(1,status));
    bus.Publish=NULL; extra[1]="cli-test"; TEST_ASSERT_NOT_EQUAL(0,cmd_bus_publish(2,extra));
}
void test_EventBusConsolePublicationFailureIsNotSuccess(void) {
    char *args[]={"bus_publish","cli-test"}; publishResult=-4;
    TEST_ASSERT_NOT_EQUAL(0,cmd_bus_publish(2,args));
    publishResult=0; TEST_ASSERT_EQUAL(0,cmd_bus_publish(2,args)); TEST_ASSERT_EQUAL(2,publishes);
}
