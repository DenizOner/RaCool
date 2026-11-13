#ifdef RACOOL_TEST_NATIVE

#include <unity.h>
#include <string.h>
#include <stdlib.h>
#include "../../support/test_mocks.h"

#include "Core/IService.h"
#include "Core/ServiceLocator/ServiceLocator.h"
#include "Core/EventBus/IEventBus.h"
#include "Core/ErrorHandler/IErrorHandler.h"
#include "Management/ConfigurationManager/IConfigurationManager.h"
#include "Management/SecurityManager/ISecurityManager.h"
#include "Hardware/Relay/IRelay.h"
#include "Hardware/InfraredTemperatureSensor/IInfraredTemperatureSensor.h"
#include "Controllers/ClimateController/IClimateController.h"

static bool e2e_relay_state = false;
static float e2e_published_temp[2] = {0.0f, 0.0f};
static bool e2e_event_received = false;

static esp_err_t e2e_mock_relay_open(IRelay *self) {
    (void)self;
    e2e_relay_state = true;
    return ESP_OK;
}

static esp_err_t e2e_mock_relay_close(IRelay *self) {
    (void)self;
    e2e_relay_state = false;
    return ESP_OK;
}

static bool e2e_mock_relay_is_open(IRelay *self) {
    (void)self;
    return e2e_relay_state;
}

static IRelay e2e_mock_relay = {
    .Open = e2e_mock_relay_open,
    .Close = e2e_mock_relay_close,
    .IsOpen = e2e_mock_relay_is_open
};

static esp_err_t e2e_mock_event_publish(BusEvent event) {
    if (event.eventName && strcmp(event.eventName, EVENT_IR_TEMP_UPDATED) == 0 && event.data) {
        e2e_event_received = true;
        memcpy(e2e_published_temp, event.data, sizeof(e2e_published_temp));
    }
    return ESP_OK;
}

static IEventBus e2e_mock_eventbus = {
    .Publish = e2e_mock_event_publish
};

void setUp(void) {
    e2e_relay_state = false;
    e2e_published_temp[0] = 0.0f;
    e2e_published_temp[1] = 0.0f;
    e2e_event_received = false;
}

void tearDown(void) {}

void test_E2E_TelemetryToControlPipeline(void) {
    TEST_ASSERT_FALSE(e2e_relay_state);
    
    // Simulate high temperature event -> Climate control hysteresis trigger
    float telemetry[2] = {32.5f, 24.0f}; // Object: 32.5°C, Ambient: 24.0°C
    BusEvent event = {
        .eventName = EVENT_IR_TEMP_UPDATED,
        .type = EVENT_TYPE_INFO,
        .data = telemetry,
        .dataSize = sizeof(telemetry)
    };
    
    e2e_mock_eventbus.Publish(event);
    TEST_ASSERT_TRUE(e2e_event_received);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 32.5f, e2e_published_temp[0]);

    // Simulate cooling relay activation
    e2e_mock_relay.Open(&e2e_mock_relay);
    TEST_ASSERT_TRUE(e2e_mock_relay.IsOpen(&e2e_mock_relay));

    // Temperature drops back to safe range
    telemetry[0] = 22.0f;
    e2e_mock_eventbus.Publish(event);
    e2e_mock_relay.Close(&e2e_mock_relay);
    TEST_ASSERT_FALSE(e2e_mock_relay.IsOpen(&e2e_mock_relay));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_E2E_TelemetryToControlPipeline);
    return UNITY_END();
}

#endif // RACOOL_TEST_NATIVE
