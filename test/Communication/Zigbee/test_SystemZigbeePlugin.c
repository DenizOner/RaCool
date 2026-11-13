#include "test_zigbee_shared.h"
#include "unity.h"
#include <math.h>
#include <string.h>
#include "Communication/Zigbee/Plugins/SystemZigbeePlugin.c"

static IZigbee radio;
static unsigned publications;
static uint8_t endpoint;
static uint8_t payload[8];
static int32_t connection;
static int32_t publication_result;
static int32_t connected(IZigbee *self) { (void)self; return connection; }
static int32_t publish(IZigbee *self, uint8_t ep, uint16_t cluster, uint16_t attribute,
        const void *value, uint8_t length) {
    (void)self; (void)cluster; (void)attribute;
    ++publications;
    endpoint = ep;
    TEST_ASSERT_LESS_OR_EQUAL(sizeof(payload), length);
    memcpy(payload, value, length);
    return publication_result;
}
void setUp(void) {
    RESET_FAKE(mocked_ServiceLocator_GetService);
    RESET_FAKE(mocked_heap_caps_get_total_size);
    radio = (IZigbee){.IsConnected = connected, .QueueAttributeUpdate = publish};
    mocked_ServiceLocator_GetService_fake.return_val = &radio.base;
    mocked_heap_caps_get_total_size_fake.return_val = 1000;
    connection = 1;
    publications = 0;
    publication_result = ZIGBEE_ERROR_OK;
    subscriptions = (EventSubscriptions){0};
    memset(payload, 0, sizeof(payload));
}
void tearDown(void) {}

void test_SystemZigbeePlugin_ValidSamplesReachTheirEndpoints(void) {
    float temperature = 34.25f;
    OnInternalTempUpdated((BusEvent){.data = &temperature, .dataSize = sizeof(temperature)});
    TEST_ASSERT_EQUAL_UINT8(ZIGBEE_EP_CPU_TEMP, endpoint);
    int16_t temperature_wire;
    memcpy(&temperature_wire, payload, sizeof(temperature_wire));
    TEST_ASSERT_EQUAL_INT16(3425, temperature_wire);
    uint32_t free_bytes = 250;
    OnMemoryUsageUpdated((BusEvent){.data = &free_bytes, .dataSize = sizeof(free_bytes)});
    TEST_ASSERT_EQUAL_UINT8(ZIGBEE_EP_MEMORY, endpoint);
    float memory_wire;
    memcpy(&memory_wire, payload, sizeof(memory_wire));
    TEST_ASSERT_EQUAL_FLOAT(25.0f, memory_wire);
    uint32_t uptime = 12345;
    OnUptimeUpdated((BusEvent){.data = &uptime, .dataSize = sizeof(uptime)});
    TEST_ASSERT_EQUAL_UINT8(ZIGBEE_EP_UPTIME, endpoint);
    float uptime_wire;
    memcpy(&uptime_wire, payload, sizeof(uptime_wire));
    TEST_ASSERT_EQUAL_FLOAT(12345.0f, uptime_wire);
    int32_t health = 97;
    OnHealthUpdated((BusEvent){.data = &health, .dataSize = sizeof(health)});
    TEST_ASSERT_EQUAL_UINT8(ZIGBEE_EP_SYSTEM_HEALTH, endpoint);
    TEST_ASSERT_EQUAL_UINT(4, publications);
}

void test_SystemZigbeePlugin_RejectsShortAndNullPayloads(void) {
    uint8_t bytes[4] = {0};
    BusEventHandler handlers[] = {OnInternalTempUpdated, OnMemoryUsageUpdated, OnUptimeUpdated, OnHealthUpdated};
    for (unsigned i = 0; i < sizeof(handlers)/sizeof(handlers[0]); ++i) {
        handlers[i]((BusEvent){.data = NULL, .dataSize = sizeof(bytes)});
        for (unsigned size = 0; size < sizeof(bytes); ++size)
            handlers[i]((BusEvent){.data = bytes, .dataSize = size});
    }
    TEST_ASSERT_EQUAL_UINT(0, publications);
    TEST_ASSERT_EQUAL_UINT(0, mocked_heap_caps_get_total_size_fake.call_count);
}

void test_SystemZigbeePlugin_RejectsNonFiniteAndUnrepresentableTemperature(void) {
    float bad[] = {NAN, INFINITY, -INFINITY, 327.68f, -327.68f, 1.0e20f};
    for (unsigned i = 0; i < sizeof(bad)/sizeof(bad[0]); ++i)
        OnInternalTempUpdated((BusEvent){.data = &bad[i], .dataSize = sizeof(float)});
    TEST_ASSERT_EQUAL_UINT(0, publications);
}

void test_SystemZigbeePlugin_RejectsInvalidHealthAndUnavailableHeap(void) {
    int32_t health = -1;
    OnHealthUpdated((BusEvent){.data = &health, .dataSize = sizeof(health)});
    health = 101;
    OnHealthUpdated((BusEvent){.data = &health, .dataSize = sizeof(health)});
    uint32_t free_bytes = 1001;
    OnMemoryUsageUpdated((BusEvent){.data = &free_bytes, .dataSize = sizeof(free_bytes)});
    free_bytes = 0;
    mocked_heap_caps_get_total_size_fake.return_val = 0;
    OnMemoryUsageUpdated((BusEvent){.data = &free_bytes, .dataSize = sizeof(free_bytes)});
    TEST_ASSERT_EQUAL_UINT(0, publications);
    mocked_heap_caps_get_total_size_fake.return_val = 1000;
    OnMemoryUsageUpdated((BusEvent){.data = &free_bytes, .dataSize = sizeof(free_bytes)});
    TEST_ASSERT_EQUAL_UINT(1, publications);
    float memory_wire;
    memcpy(&memory_wire, payload, sizeof(memory_wire));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, memory_wire);
}

void test_SystemZigbeePlugin_UnalignedSamplesAreCopiedSafely(void) {
    uint8_t storage[5];
    float temperature = 21.5f;
    memcpy(storage + 1, &temperature, sizeof(temperature));
    OnInternalTempUpdated((BusEvent){.data = storage + 1, .dataSize = sizeof(temperature)});
    int16_t temperature_wire;
    memcpy(&temperature_wire, payload, sizeof(temperature_wire));
    TEST_ASSERT_EQUAL_INT16(2150, temperature_wire);
    uint32_t uptime = 42;
    memcpy(storage + 1, &uptime, sizeof(uptime));
    OnUptimeUpdated((BusEvent){.data = storage + 1, .dataSize = sizeof(uptime)});
    TEST_ASSERT_EQUAL_UINT(2, publications);
}

void test_SystemZigbeePlugin_RejectsDisconnectedAndMissingMethods(void) {
    float temperature = 20.0f;
    BusEvent sample = {.data = &temperature, .dataSize = sizeof(temperature)};
    connection = -1;
    OnInternalTempUpdated(sample);
    connection = 0;
    OnInternalTempUpdated(sample);
    connection = 1;
    radio.IsConnected = NULL;
    OnInternalTempUpdated(sample);
    radio.IsConnected = connected;
    radio.QueueAttributeUpdate = NULL;
    OnInternalTempUpdated(sample);
    TEST_ASSERT_EQUAL_UINT(0, publications);
}

void test_SystemZigbeePlugin_FailedPublicationCanRecover(void) {
    uint32_t uptime = 50;
    publication_result = ZIGBEE_ERROR_ATTRIBUTE_FAIL;
    OnUptimeUpdated((BusEvent){.data = &uptime, .dataSize = sizeof(uptime)});
    publication_result = ZIGBEE_ERROR_OK;
    uptime = 51;
    OnUptimeUpdated((BusEvent){.data = &uptime, .dataSize = sizeof(uptime)});
    TEST_ASSERT_EQUAL_UINT(2, publications);
    float wire;
    memcpy(&wire, payload, sizeof(wire));
    TEST_ASSERT_EQUAL_FLOAT(51.0f, wire);
}

static unsigned subscribe_calls, unsubscribe_calls, fail_at;
static bool busy_removal;
static int32_t subscribe_handler(IEventBus *bus, const char *name, BusEventHandler handler) {
    (void)bus; (void)name; (void)handler;
    return ++subscribe_calls == fail_at ? ESP_FAIL : ESP_OK;
}
static int32_t unsubscribe_handler(IEventBus *bus, const char *name, BusEventHandler handler) {
    (void)bus; (void)name; (void)handler;
    ++unsubscribe_calls;
    return busy_removal ? EVENT_BUS_ERR_BUSY : ESP_OK;
}
static IEventBus bus = {.Subscribe = subscribe_handler, .Unsubscribe = unsubscribe_handler};

void test_SystemZigbeePlugin_PartialSubscriptionRollsBackAndRetries(void) {
    subscribe_calls = unsubscribe_calls = 0;
    fail_at = 2; busy_removal = false;
    RESET_FAKE(mocked_ServiceLocator_GetEventBus);
    mocked_ServiceLocator_GetEventBus_fake.return_val = &bus;
    TEST_ASSERT_EQUAL(ESP_FAIL, SystemZigbeePlugin_Init());
    TEST_ASSERT_EQUAL_UINT(0, subscriptions.owned);
    TEST_ASSERT_NULL(subscriptions.bus);
    TEST_ASSERT_EQUAL_UINT(1, unsubscribe_calls);
    fail_at = 0;
    TEST_ASSERT_EQUAL(ESP_OK, SystemZigbeePlugin_Init());
    TEST_ASSERT_EQUAL_UINT(31, subscriptions.owned);
    unsigned calls = subscribe_calls;
    TEST_ASSERT_EQUAL(ESP_OK, SystemZigbeePlugin_Init());
    TEST_ASSERT_EQUAL_UINT(calls, subscribe_calls);
}

void test_SystemZigbeePlugin_BusyRollbackKeepsBusAndCallbackOwnership(void) {
    subscribe_calls = unsubscribe_calls = 0;
    fail_at = 2; busy_removal = true;
    RESET_FAKE(mocked_ServiceLocator_GetEventBus);
    mocked_ServiceLocator_GetEventBus_fake.return_val = &bus;
    TEST_ASSERT_EQUAL(ESP_FAIL, SystemZigbeePlugin_Init());
    TEST_ASSERT_EQUAL_UINT(1, subscriptions.owned);
    TEST_ASSERT_EQUAL_PTR(&bus, subscriptions.bus);
    IEventBus replacement = bus;
    mocked_ServiceLocator_GetEventBus_fake.return_val = &replacement;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, SystemZigbeePlugin_Init());
    TEST_ASSERT_EQUAL_PTR(&bus, subscriptions.bus);
    mocked_ServiceLocator_GetEventBus_fake.return_val = &bus;
    fail_at = 0; busy_removal = false;
    TEST_ASSERT_EQUAL(ESP_OK, SystemZigbeePlugin_Init());
    TEST_ASSERT_EQUAL_UINT(31, subscriptions.owned);
    TEST_ASSERT_EQUAL_UINT(6, subscribe_calls);
}

static BusEventHandler committed_health_handler;
static int32_t subscribe_committed_health(IEventBus *self, const char *name, BusEventHandler handler) {
    (void)self;
    if (!strcmp(name, EVENT_DIAG_HEALTH_CHANGED)) committed_health_handler = handler;
    return ESP_OK;
}
void test_SystemZigbeePlugin_CommittedHealthChangeDoesNotWaitForPeriodicTelemetry(void) {
    committed_health_handler = NULL;
    IEventBus source = {.Subscribe = subscribe_committed_health, .Unsubscribe = unsubscribe_handler};
    RESET_FAKE(mocked_ServiceLocator_GetEventBus);
    mocked_ServiceLocator_GetEventBus_fake.return_val = &source;
    TEST_ASSERT_EQUAL(ESP_OK, SystemZigbeePlugin_Init());
    TEST_ASSERT_NOT_NULL(committed_health_handler);
    int32_t value = 88;
    committed_health_handler((BusEvent){.eventName = EVENT_DIAG_HEALTH_CHANGED,
        .data = &value, .dataSize = sizeof(value)});
    TEST_ASSERT_EQUAL_UINT8(ZIGBEE_EP_SYSTEM_HEALTH, endpoint);
    float wire; memcpy(&wire, payload, sizeof(wire)); TEST_ASSERT_EQUAL_FLOAT(88, wire);
    TEST_ASSERT_EQUAL_UINT(1, publications);
    value = 100;
    committed_health_handler((BusEvent){.data = &value, .dataSize = sizeof(value)});
    memcpy(&wire, payload, sizeof(wire)); TEST_ASSERT_EQUAL_FLOAT(100, wire);
    TEST_ASSERT_EQUAL_UINT(2, publications);
}

void test_EventSubscriptions_RejectsChangedTableAndDuplicates(void) {
    EventSubscriptions state = {0};
    EventSubscription entries[] = {{"one", OnUptimeUpdated}, {"two", OnHealthUpdated}};
    EventSubscription duplicate[] = {{"one", OnUptimeUpdated}, {"one", OnUptimeUpdated}};
    EventSubscription changed[] = {{"other", OnUptimeUpdated}, {"two", OnHealthUpdated}};
    subscribe_calls = unsubscribe_calls = fail_at = 0; busy_removal = false;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, EventSubscriptions_Attach(&state, &bus, duplicate, 2));
    TEST_ASSERT_EQUAL_UINT(0, subscribe_calls);
    TEST_ASSERT_EQUAL(ESP_OK, EventSubscriptions_Attach(&state, &bus, entries, 2));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, EventSubscriptions_Detach(&state, changed, 2));
    TEST_ASSERT_EQUAL_UINT(3, state.owned);
    TEST_ASSERT_EQUAL_UINT(0, unsubscribe_calls);
    TEST_ASSERT_EQUAL(ESP_OK, EventSubscriptions_Detach(&state, entries, 2));
    TEST_ASSERT_EQUAL_UINT(0, state.owned);
    TEST_ASSERT_NULL(state.bus);
}

void test_EventSubscriptions_FullWidthOwnershipMask(void) {
    EventSubscriptions state = {0};
    EventSubscription entries[32];
    char names[32][8];
    for (unsigned i = 0; i < 32; ++i) {
        snprintf(names[i], sizeof(names[i]), "e%u", i);
        entries[i] = (EventSubscription){names[i], OnUptimeUpdated};
    }
    subscribe_calls = unsubscribe_calls = fail_at = 0; busy_removal = false;
    TEST_ASSERT_EQUAL(ESP_OK, EventSubscriptions_Attach(&state, &bus, entries, 32));
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, state.owned);
    TEST_ASSERT_EQUAL_UINT(32, subscribe_calls);
    TEST_ASSERT_EQUAL(ESP_OK, EventSubscriptions_Detach(&state, entries, 32));
    TEST_ASSERT_EQUAL_UINT(32, unsubscribe_calls);
    TEST_ASSERT_EQUAL_UINT32(0, state.owned);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, EventSubscriptions_Attach(&state, &bus, entries, 33));
}
