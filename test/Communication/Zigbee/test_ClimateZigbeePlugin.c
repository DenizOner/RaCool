#include "test_zigbee_shared.h"
#include "test_zigbee_mock_eventbus.h"
#include "unity.h"
#include "Communication/Zigbee/Plugins/ClimateZigbeePlugin.c"

static IZigbee radio;
static SystemSettings settings;
static bool connected;
static unsigned updates, brightness_updates, profile_updates;
static int16_t last_warm_lower;
static uint8_t last_profile;
static uint8_t last_fan_mode;
static unsigned fan_mode_updates;
static uint8_t last_brightness;
static uint8_t last_external_brightness;
static unsigned external_brightness_updates;
static uint8_t display_modes[10];
static uint16_t display_times[10];
static uint16_t reporting_values[11][6];
static esp_err_t snapshot_result;
static int32_t is_connected(IZigbee *self) { (void)self; return connected; }
static esp_err_t get_snapshot(IConfigurationManager *self, SystemSettings *out) {
    (void)self; *out = settings; return snapshot_result;
}
static int32_t notify(IZigbee *self, uint8_t ep, uint16_t cluster, uint16_t attr, const void *value, uint8_t size) {
    (void)self; updates++;
    if (ep == ZIGBEE_EP_RGB_LED && cluster == EZB_ZCL_CLUSTER_ID_LEVEL && attr == EZB_ZCL_ATTR_LEVEL_CURRENT_LEVEL_ID) {
        TEST_ASSERT_EQUAL(1, size);
        last_external_brightness = *(const uint8_t *)value;
        external_brightness_updates++;
    }
    if (ep == ZIGBEE_EP_SENSOR_FAN && cluster == EZB_ZCL_CLUSTER_ID_THERMOSTAT && attr == EZB_ZCL_ATTR_THERMOSTAT_SYSTEM_MODE_ID) {
        TEST_ASSERT_EQUAL(1, size); last_fan_mode = *(const uint8_t *)value; fan_mode_updates++;
    }
    if (cluster == EZB_ZCL_CLUSTER_ID_THERMOSTAT_UI_CONFIG && attr >= 0x8030 && attr <= 0x8035) {
        TEST_ASSERT_TRUE(ep <= 10); TEST_ASSERT_EQUAL(2, size);
        memcpy(&reporting_values[ep][attr - 0x8030], value, size);
    }
    if (ep == ZIGBEE_EP_SYSTEM && cluster == EZB_ZCL_CLUSTER_ID_THERMOSTAT_UI_CONFIG && attr == ZIGBEE_ATTR_POWER_PROFILE) {
        TEST_ASSERT_EQUAL(1, size); last_profile = *(uint8_t *)value; profile_updates++;
    }
    if ((ep == 1 || ep == 9) && cluster == EZB_ZCL_CLUSTER_ID_THERMOSTAT_UI_CONFIG) {
        if (attr == 0x8020) { TEST_ASSERT_EQUAL(1, size); display_modes[ep] = *(uint8_t *)value; }
        if (attr == 0x8021) { TEST_ASSERT_EQUAL(2, size); memcpy(&display_times[ep], value, size); }
    }
    if (ep==ZIGBEE_EP_RGB_LED && cluster==EZB_ZCL_CLUSTER_ID_ON_OFF && attr==0x8022) {
        TEST_ASSERT_EQUAL(sizeof(last_warm_lower),size);memcpy(&last_warm_lower,value,sizeof(last_warm_lower));
    }
    if (ep == 2 && cluster == EZB_ZCL_CLUSTER_ID_ON_OFF && attr == 0x8004) {
        TEST_ASSERT_EQUAL(1, size);
        last_brightness = *(uint8_t *)value;
        brightness_updates++;
    }
    return ZIGBEE_ERROR_OK;
}
static IService *lookup(const char *name) {
    if (strcmp(name, SVC_NAME_ZIGBEE) == 0) return (IService *)&radio;
    if (strcmp(name, SVC_NAME_CONFIGURATION_MANAGER) == 0) return (IService *)&mock_config_inst;
    return NULL;
}
void setUp(void) {
    ZIGBEE_COMMON_FAKES(RESET_FAKE);
    memset(&radio, 0, sizeof(radio)); memset(&settings, 0, sizeof(settings));
    connected = true; updates = brightness_updates = profile_updates = 0; snapshot_result = ESP_OK;
    external_brightness_updates = 0;
    fan_mode_updates = 0;
    radio.IsConnected = is_connected; radio.QueueAttributeUpdate = notify;
    mock_config_inst.GetSnapshot = get_snapshot;
    ServiceLocator_GetService_fake.custom_fake = lookup;
    subscriptions = (EventSubscriptions){0};
    memset(reporting_values, 0, sizeof(reporting_values));
}
void tearDown(void) {}
void test_ClimateZigbee_FanModePublishesWithoutRelayTransition(void) {
    const uint8_t expected[] = {0, 1, 3};
    for (unsigned i = 0; i < 3; ++i) {
        settings.control.fan_manual_mode = i != 1;
        settings.control.fan_manual_state = i == 2;
        OnConfigChanged((BusEvent){0});
        TEST_ASSERT_EQUAL(expected[i], last_fan_mode);
        TEST_ASSERT_EQUAL(i * 2 + 1, fan_mode_updates);
        uint8_t relay = i == 2;
        OnRelayChanged((BusEvent){.data=&relay,.dataSize=sizeof(relay)});
        TEST_ASSERT_EQUAL(expected[i], last_fan_mode);
        TEST_ASSERT_EQUAL((i + 1) * 2, fan_mode_updates);
    }
}
void test_ClimateZigbee_DisplayMirrorsLegacyAndDedicatedRoutes(void) {
    settings.system.display_mode = 2; settings.system.oled_timeout_sec = 45;
    OnConfigChanged((BusEvent){0});
    TEST_ASSERT_EQUAL(2, display_modes[1]); TEST_ASSERT_EQUAL(2, display_modes[9]);
    TEST_ASSERT_EQUAL(45, display_times[1]); TEST_ASSERT_EQUAL(45, display_times[9]);
}
void test_ClimateZigbee_InternalBrightnessSync(void) {
    settings.hardware.builtin_led_brightness = 0;
    OnConfigChanged((BusEvent){0});
    TEST_ASSERT_EQUAL(1, brightness_updates); TEST_ASSERT_EQUAL(0, last_brightness);
    settings.hardware.builtin_led_brightness = 255;
    OnConfigChanged((BusEvent){0});
    TEST_ASSERT_EQUAL(2, brightness_updates); TEST_ASSERT_EQUAL(255, last_brightness);
}
void test_ClimateZigbee_ExternalBrightnessSyncIncludesZeroAndRestoredValue(void) {
    const uint8_t values[] = {0, 59, 254, 255};
    for (unsigned i = 0; i < sizeof(values); ++i) {
        settings.hardware.ext_led_brightness = values[i];
        OnConfigChanged((BusEvent){0});
        TEST_ASSERT_EQUAL(i + 1, external_brightness_updates);
        TEST_ASSERT_EQUAL(values[i] == 255 ? 254 : values[i], last_external_brightness);
    }
}
void test_ClimateZigbee_UnavailableSnapshotOrRadioDoesNotPublish(void) {
    snapshot_result = ESP_FAIL;
    OnConfigChanged((BusEvent){0}); TEST_ASSERT_EQUAL(0, updates);
    snapshot_result = ESP_OK; connected = false;
    OnConfigChanged((BusEvent){0}); TEST_ASSERT_EQUAL(0, updates);
}
void test_ClimateZigbee_RejectsMalformedMeasurements(void) {
    float values[2] = {25, 20};
    OnIRTempUpdated((BusEvent){.data = values, .dataSize = 1});
    TEST_ASSERT_EQUAL(0, updates);
    values[0] = NAN;
    OnIRTempUpdated((BusEvent){.data = values, .dataSize = sizeof(values)});
    TEST_ASSERT_EQUAL(0, updates);
    values[0] = 400;
    OnIRTempUpdated((BusEvent){.data = values, .dataSize = sizeof(values)});
    TEST_ASSERT_EQUAL(0, updates);
    values[0] = 25;
    OnIRTempUpdated((BusEvent){.data = values, .dataSize = sizeof(values)});
    TEST_ASSERT_EQUAL(2, updates);
    uint8_t invalid = 2;
    OnRelayChanged((BusEvent){.data = &invalid, .dataSize = 1});
    TEST_ASSERT_EQUAL(2, updates);
}

void test_ClimateZigbee_PowerProfileUsesSystemEndpoint(void) {
    for (uint8_t mode = 0; mode < 3; mode++) {
        settings.system.power_profile = mode;
        OnConfigChanged((BusEvent){0});
        TEST_ASSERT_EQUAL(mode, last_profile); TEST_ASSERT_EQUAL(mode + 1, profile_updates);
    }
}

static unsigned subscribe_count, unsubscribe_count;
static bool reject_relay_subscription;
static BusEventHandler connection_handler;
static int32_t climate_subscribe(IEventBus *bus, const char *name, BusEventHandler handler) {
    (void)bus; (void)handler;
    ++subscribe_count;
    if (strcmp(name, "ZIGBEE_CONNECTED") == 0) connection_handler = handler;
    return reject_relay_subscription && strcmp(name, EVENT_RELAY_STATE_CHANGED) == 0 ? ESP_FAIL : ESP_OK;
}
static int32_t climate_unsubscribe(IEventBus *bus, const char *name, BusEventHandler handler) {
    (void)bus; (void)name; (void)handler;
    ++unsubscribe_count;
    return ESP_OK;
}
void test_ClimateZigbee_PartialBindingsRollbackAndRetry(void) {
    IEventBus bus = {.Subscribe = climate_subscribe, .Unsubscribe = climate_unsubscribe};
    mocked_ServiceLocator_GetEventBus_fake.return_val = &bus;
    subscribe_count = unsubscribe_count = 0;
    reject_relay_subscription = true;
    TEST_ASSERT_EQUAL(ESP_FAIL, ClimateZigbeePlugin_Init());
    TEST_ASSERT_EQUAL_UINT(0, subscriptions.owned);
    TEST_ASSERT_EQUAL_UINT(2, unsubscribe_count);
    reject_relay_subscription = false;
    TEST_ASSERT_EQUAL(ESP_OK, ClimateZigbeePlugin_Init());
    TEST_ASSERT_EQUAL_UINT(31, subscriptions.owned);
    TEST_ASSERT_EQUAL_UINT(8, subscribe_count);
    TEST_ASSERT_EQUAL(ESP_OK, ClimateZigbeePlugin_Init());
    TEST_ASSERT_EQUAL_UINT(8, subscribe_count);
    TEST_ASSERT_EQUAL(ESP_OK, EventSubscriptions_Detach(&subscriptions, subscriptionTable, 5));
}

void test_ClimateZigbee_ReportingChangesMirrorOwnAndLegacyRoutes(void) {
    settings.zigbee_reporting.fan_s = 1; settings.zigbee_reporting.ambient_s = 2;
    settings.zigbee_reporting.cpu_s = 3; settings.zigbee_reporting.health_s = 4;
    settings.zigbee_reporting.memory_s = 5; settings.zigbee_reporting.uptime_s = 6;
    OnConfigChanged((BusEvent){0});
    const unsigned endpoints[] = {1,4,5,6,7,8};
    for (unsigned n = 0; n < 6; ++n) {
        TEST_ASSERT_EQUAL(n + 1, reporting_values[1][n]);
        TEST_ASSERT_EQUAL(n + 1, reporting_values[endpoints[n]][n]);
    }
}

void test_ClimateZigbee_ReconnectUsesCurrentConfigurationSnapshot(void) {
    IEventBus bus = {.Subscribe=climate_subscribe,.Unsubscribe=climate_unsubscribe};
    mocked_ServiceLocator_GetEventBus_fake.return_val = &bus;
    reject_relay_subscription = false; connection_handler = NULL;
    TEST_ASSERT_EQUAL(0, ClimateZigbeePlugin_Init());
    TEST_ASSERT_NOT_NULL(connection_handler);
    settings.system.display_mode = 2;
    settings.zigbee_reporting.cpu_s = 12;
    connection_handler((BusEvent){.eventName="ZIGBEE_CONNECTED"});
    TEST_ASSERT_EQUAL(2, display_modes[9]);
    TEST_ASSERT_EQUAL(12, reporting_values[5][2]);
    TEST_ASSERT_EQUAL(0, EventSubscriptions_Detach(&subscriptions, subscriptionTable, 5));
}

void test_ClimateZigbee_FractionalWarmThresholdRoundsToHundredths(void) {
    settings.hardware.ext_led_temp_warm_min=35.1f;
    OnConfigChanged((BusEvent){0});
    TEST_ASSERT_EQUAL(3510,last_warm_lower);
}
