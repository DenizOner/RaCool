#include "test_zigbee_shared.h"
#include "test_zigbee_mock_eventbus.h"
#include "unity.h"
#include "Communication/Zigbee/Endpoints/EndpointFan.c"
#include "Communication/Zigbee/Endpoints/EndpointAmbientTemp.c"
#include "Communication/Zigbee/Endpoints/EndpointCPUTemp.c"
#include "Communication/Zigbee/Endpoints/EndpointHealth.c"
#include "Communication/Zigbee/Endpoints/EndpointMemory.c"
#include "Communication/Zigbee/Endpoints/EndpointUptime.c"

static IZigbeeEndpoint *(*const factories[])(void) = {
    EndpointFan_Create, EndpointAmbientTemp_Create, EndpointCPUTemp_Create,
    EndpointHealth_Create, EndpointMemory_Create, EndpointUptime_Create
};
static const uint8_t endpoint_ids[] = {1, 4, 5, 6, 7, 8};
static ezb_zcl_reporting_info_t reporting_descriptor(uint8_t endpoint, uint16_t cluster,
    uint8_t role, uint16_t attribute, uint16_t manufacturer) {
    (void)endpoint; (void)role; (void)attribute; (void)manufacturer;
    if (!ezb_zcl_reporting_info_find_fake.return_val) return NULL;
    return (ezb_zcl_reporting_info_t)(uintptr_t)(cluster == EZB_ZCL_CLUSTER_ID_ON_OFF ? 2 : 1);
}

static void intervals(uint16_t value) {
    mock_settings.zigbee_reporting.fan_s = value;
    mock_settings.zigbee_reporting.ambient_s = value;
    mock_settings.zigbee_reporting.cpu_s = value;
    mock_settings.zigbee_reporting.health_s = value;
    mock_settings.zigbee_reporting.memory_s = value;
    mock_settings.zigbee_reporting.uptime_s = value;
}
static ezb_err_t checked_reporting_update(ezb_zcl_reporting_info_t info, uint16_t min,
    uint16_t max, const ezb_zcl_attr_variable_t *delta) {
    (void)info; (void)min; (void)max;
    /* Real SDK 2.0.4 reads this pointer even for discrete attributes. */
    TEST_ASSERT_NOT_NULL(delta);
    return ezb_zcl_reporting_info_update_fake.return_val;
}
void setUp(void) {
    ZIGBEE_COMMON_FAKES(RESET_FAKE);
    ConfigurationManager_GetInstance_fake.return_val = &mock_config_inst;
    mock_config_inst.GetSettings = mock_get_settings;
    ezb_zcl_reporting_info_find_fake.return_val = (ezb_zcl_reporting_info_t)1;
    ezb_zcl_reporting_info_find_fake.custom_fake = reporting_descriptor;
    ezb_zcl_reporting_info_update_fake.custom_fake = checked_reporting_update;
    intervals(0);
}
void tearDown(void) {}
void test_ZigbeeReporting_ZeroStopsEveryTelemetryAttribute(void) {
    for (unsigned i = 0; i < 6; ++i) {
        IZigbeeEndpoint *ep = factories[i]();
        unsigned before = ezb_zcl_reporting_info_update_fake.call_count;
        TEST_ASSERT_EQUAL(ESP_OK, ep->ConfigureReporting(ep));
        TEST_ASSERT_EQUAL(endpoint_ids[i], ezb_zcl_reporting_info_find_fake.arg0_val);
        TEST_ASSERT_EQUAL_PTR((void *)1, ezb_zcl_reporting_info_update_fake.arg0_history[before]);
        TEST_ASSERT_EQUAL(UINT16_MAX, ezb_zcl_reporting_info_update_fake.arg2_history[before]);
        if (i == 0) {
            TEST_ASSERT_EQUAL_PTR((void *)2, ezb_zcl_reporting_info_update_fake.arg0_history[before + 1]);
            TEST_ASSERT_EQUAL(0, ezb_zcl_reporting_info_update_fake.arg1_history[before + 1]);
            TEST_ASSERT_EQUAL(0, ezb_zcl_reporting_info_update_fake.arg2_history[before + 1]);
        }
    }
    TEST_ASSERT_EQUAL(6, ezb_zcl_reporting_stop_attr_report_fake.call_count);
    TEST_ASSERT_EQUAL(0, ezb_zcl_reporting_start_attr_report_fake.call_count);
}
void test_ZigbeeReporting_ReenableUsesRequestedInterval(void) {
    for (unsigned i = 0; i < 6; ++i) {
        IZigbeeEndpoint *ep = factories[i]();
        intervals(0); ep->ConfigureReporting(ep);
        unsigned before = ezb_zcl_reporting_info_update_fake.call_count;
        intervals(1); ep->ConfigureReporting(ep);
        TEST_ASSERT_EQUAL(1, ezb_zcl_reporting_info_update_fake.arg2_history[before]);
        TEST_ASSERT_LESS_OR_EQUAL(1, ezb_zcl_reporting_info_update_fake.arg1_history[before]);
        before = ezb_zcl_reporting_info_update_fake.call_count;
        intervals(600); ep->ConfigureReporting(ep);
        TEST_ASSERT_EQUAL(600, ezb_zcl_reporting_info_update_fake.arg2_history[before]);
    }
    TEST_ASSERT_EQUAL(6, ezb_zcl_reporting_stop_attr_report_fake.call_count);
    TEST_ASSERT_EQUAL(21, ezb_zcl_reporting_info_update_fake.call_count);
    TEST_ASSERT_EQUAL(0, ezb_zcl_reporting_start_attr_report_fake.call_count);
}
void test_ZigbeeReporting_MissingDescriptorDoesNotCallDriver(void) {
    ezb_zcl_reporting_info_find_fake.return_val = NULL;
    for (unsigned i = 0; i < 6; ++i) {
        IZigbeeEndpoint *ep = factories[i]();
        TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND, ep->ConfigureReporting(ep));
    }
    TEST_ASSERT_EQUAL(0, ezb_zcl_reporting_stop_attr_report_fake.call_count);
    TEST_ASSERT_EQUAL(0, ezb_zcl_reporting_start_attr_report_fake.call_count);
}
void test_ZigbeeReporting_UpdateFailureDoesNotStartReports(void) {
    intervals(10);
    ezb_zcl_reporting_info_update_fake.return_val = (ezb_err_t)1;
    for (unsigned i = 0; i < 6; ++i) {
        IZigbeeEndpoint *ep = factories[i]();
        TEST_ASSERT_EQUAL(ESP_FAIL, ep->ConfigureReporting(ep));
    }
    TEST_ASSERT_EQUAL(6, ezb_zcl_reporting_info_update_fake.call_count);
    TEST_ASSERT_EQUAL(0, ezb_zcl_reporting_start_attr_report_fake.call_count);
}

static esp_err_t unavailable_snapshot(IConfigurationManager *self, SystemSettings *out) {
    (void)self; (void)out;
    return ESP_FAIL;
}
void test_ZigbeeReporting_SnapshotFailurePreservesExistingDriverPolicy(void) {
    mock_config_inst.GetSnapshot = unavailable_snapshot;
    mock_config_inst.GetSettings = NULL;
    for (unsigned i = 0; i < 6; ++i) {
        IZigbeeEndpoint *ep = factories[i]();
        TEST_ASSERT_EQUAL(ESP_FAIL, ep->ConfigureReporting(ep));
    }
    TEST_ASSERT_EQUAL(0, ezb_zcl_reporting_info_find_fake.call_count);
    TEST_ASSERT_EQUAL(0, ezb_zcl_reporting_info_update_fake.call_count);
    TEST_ASSERT_EQUAL(0, ezb_zcl_reporting_stop_attr_report_fake.call_count);
    TEST_ASSERT_EQUAL(0, ezb_zcl_reporting_start_attr_report_fake.call_count);
}

void test_ZigbeeReporting_DisablePreservesMetadataFailureAndStillStops(void) {
    intervals(0);
    ezb_zcl_reporting_info_update_fake.return_val = (ezb_err_t)1;
    for (unsigned i = 0; i < 6; ++i) {
        IZigbeeEndpoint *ep = factories[i]();
        TEST_ASSERT_EQUAL(ESP_FAIL, ep->ConfigureReporting(ep));
    }
    TEST_ASSERT_EQUAL(6, ezb_zcl_reporting_stop_attr_report_fake.call_count);
}

void test_ZigbeeReporting_StopFailureIsReturned(void) {
    intervals(0);
    ezb_zcl_reporting_stop_attr_report_fake.return_val = (ezb_err_t)1;
    for (unsigned i = 0; i < 6; ++i) {
        IZigbeeEndpoint *ep = factories[i]();
        TEST_ASSERT_EQUAL(ESP_FAIL, ep->ConfigureReporting(ep));
    }
}
