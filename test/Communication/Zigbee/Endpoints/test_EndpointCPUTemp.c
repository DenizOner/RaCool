#ifndef RACOOL_TEST_NATIVE
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#endif
#include "Communication/Zigbee/Endpoints/EndpointCPUTemp.h"
#include "Infrastructure/support/test_mocks.h"
#include "unity.h"
#include "Communication/Zigbee/Endpoints/EndpointCPUTemp.c"

void setUp_EndpointCPUTemp(void) {
    ZIGBEE_COMMON_FAKES(RESET_FAKE);
}

void tearDown_EndpointCPUTemp(void) {}

void test_EndpointCPUTemp_Identification(void) {
    IZigbeeEndpoint *ep = EndpointCPUTemp_Create();
    TEST_ASSERT_EQUAL_STRING("CPUTempEndpoint", ep->GetName(ep));
}

void test_EndpointCPUTemp_RegisterClusters(void) {
    IZigbeeEndpoint *ep = EndpointCPUTemp_Create();
    ezb_af_device_desc_t mock_device = (void *)0x5678;
    
    ep->RegisterClusters(ep, &mock_device);
    
    TEST_ASSERT_EQUAL_HEX16(ZIGBEE_TEMPERATURE_INVALID, ((EndpointCPUTemp *)ep)->temp_val);
    TEST_ASSERT_EQUAL(1, mocked_ezb_af_create_endpoint_desc_fake.call_count);
    TEST_ASSERT_EQUAL(ZIGBEE_EP_CPU_TEMP, last_endpoint_config.ep_id);
    TEST_ASSERT_EQUAL(1, mocked_ezb_af_device_desc_add_endpoint_fake.call_count);
}

void test_EndpointCPUTemp_All(void) {
    RUN_TEST(test_EndpointCPUTemp_Identification);
    RUN_TEST(test_EndpointCPUTemp_RegisterClusters);
}

#ifndef RACOOL_TARGET_TESTING
int main(void) {
    UNITY_BEGIN();
    test_EndpointCPUTemp_All();
    return UNITY_END();
}
#endif
