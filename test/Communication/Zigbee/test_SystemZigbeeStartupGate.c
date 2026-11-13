#define RACOOL_STARTUP_SENTINEL_DIAGNOSTIC 1
#include "test_zigbee_shared.h"
#include "unity.h"
#include "Communication/Zigbee/Plugins/SystemZigbeePlugin.c"
static IZigbee radio;
static unsigned publications;
static int32_t connected(IZigbee *self) {(void)self;return 1;}
static int32_t publish(IZigbee *self,uint8_t endpoint,uint16_t cluster,uint16_t attribute,const void *value,uint8_t length) {
    (void)self;(void)endpoint;(void)cluster;(void)attribute;(void)value;(void)length;
    ++publications;return ZIGBEE_ERROR_OK;
}
void setUp(void) {
    RESET_FAKE(mocked_ServiceLocator_GetService);
    RESET_FAKE(mocked_esp_timer_get_time);
    radio=(IZigbee){.IsConnected=connected,.QueueAttributeUpdate=publish};
    mocked_ServiceLocator_GetService_fake.return_val=&radio.base;
    publications=0;
}
void tearDown(void) {}
void test_StartupDiagnosticGateOnlyHoldsTwoEndpointsAndReleasesAtDeadline(void) {
    float temperature=25;int32_t health=100;uint32_t uptime=1;
    mocked_esp_timer_get_time_fake.return_val=59999999LL;
    OnInternalTempUpdated((BusEvent){.data=&temperature,.dataSize=sizeof(temperature)});
    OnHealthUpdated((BusEvent){.data=&health,.dataSize=sizeof(health)});
    TEST_ASSERT_EQUAL(0,publications);
    OnUptimeUpdated((BusEvent){.data=&uptime,.dataSize=sizeof(uptime)});
    TEST_ASSERT_EQUAL(1,publications);
    mocked_esp_timer_get_time_fake.return_val=60000000LL;
    OnInternalTempUpdated((BusEvent){.data=&temperature,.dataSize=sizeof(temperature)});
    OnHealthUpdated((BusEvent){.data=&health,.dataSize=sizeof(health)});
    TEST_ASSERT_EQUAL(3,publications);
}
