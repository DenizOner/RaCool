#ifndef RACOOL_TEST_NATIVE
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#endif
#include "Communication/Zigbee/Endpoints/EndpointHealth.h"
#include "Communication/Zigbee/Endpoints/EndpointMemory.h"
#include "Communication/Zigbee/Endpoints/EndpointUptime.h"
#include "Infrastructure/support/test_mocks.h"
#include "unity.h"

// Include implementations for testing internal logic
#include "Communication/Zigbee/Endpoints/EndpointHealth.c"
#include "Communication/Zigbee/Endpoints/EndpointMemory.c"
#include "Communication/Zigbee/Endpoints/EndpointUptime.c"

void setUp_EndpointTelemetry(void) {
    ZIGBEE_COMMON_FAKES(RESET_FAKE);
}

void tearDown_EndpointTelemetry(void) {}

void test_EndpointTelemetry_Identification(void) {
    IZigbeeEndpoint *ep_health = EndpointHealth_Create();
    TEST_ASSERT_EQUAL_STRING("System Health", ep_health->GetName(ep_health));
    TEST_ASSERT_EQUAL_FLOAT(ZIGBEE_HEALTH_UNAVAILABLE, ((EndpointHealth *)ep_health)->present_value);

    IZigbeeEndpoint *ep_mem = EndpointMemory_Create();
    TEST_ASSERT_EQUAL_STRING("System Memory", ep_mem->GetName(ep_mem));

    IZigbeeEndpoint *ep_uptime = EndpointUptime_Create();
    TEST_ASSERT_EQUAL_STRING("System Uptime", ep_uptime->GetName(ep_uptime));
}

void test_EndpointTelemetry_RegisterClusters(void) {
    IZigbeeEndpoint *ep = EndpointHealth_Create();
    ezb_af_device_desc_t mock_device = (void *)0x2222;
    
    ep->RegisterClusters(ep, mock_device);
    
    TEST_ASSERT_EQUAL(1, mocked_ezb_af_create_endpoint_desc_fake.call_count);
    TEST_ASSERT_EQUAL(1, mocked_ezb_af_device_desc_add_endpoint_fake.call_count);
}

void test_EndpointTelemetry_All(void) {
    RUN_TEST(test_EndpointTelemetry_Identification);
    RUN_TEST(test_EndpointTelemetry_RegisterClusters);
}

#ifndef RACOOL_TARGET_TESTING
int main(void) {
    UNITY_BEGIN();
    test_EndpointTelemetry_All();
    return UNITY_END();
}
#endif
