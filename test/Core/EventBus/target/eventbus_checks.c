#include "unity.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "../../../../src/Core/EventBus/EventBus.c"
#include "../../../../src/Core/EventBus/EventBusConfig.c"

static SemaphoreHandle_t callback_release;
static QueueHandle_t callback_result;
static atomic_bool callback_entered;
static atomic_bool callback_failed;
static bool bus_initialized;

static void target_bus_callback(BusEvent event) {
    if (strcmp(event.eventName, "BLOCK") == 0) {
        atomic_store(&callback_entered, true);
        xSemaphoreTake(callback_release, portMAX_DELAY);
    } else if (event.dataSize != 80 || !event.data ||
               xQueueSend(callback_result, event.data, 0) != pdTRUE) {
        atomic_store(&callback_failed, true);
    }
}

void target_eventbus_cleanup(void) {
    if (callback_release) xSemaphoreGive(callback_release);
    int32_t result = 0;
    if (bus_initialized) result = GetEventBus()->base.Stop(&GetEventBus()->base);
    if (result == 0) {
        if (callback_release) vSemaphoreDelete(callback_release);
        if (callback_result) vQueueDelete(callback_result);
        callback_release = NULL;
        callback_result = NULL;
    }
    TEST_ASSERT_EQUAL(0, result);
}

static void wait_for_callback(void) {
    TickType_t start = xTaskGetTickCount();
    while (!atomic_load(&callback_entered) && xTaskGetTickCount()-start < pdMS_TO_TICKS(1000))
        vTaskDelay(pdMS_TO_TICKS(10));
    TEST_ASSERT_TRUE(atomic_load(&callback_entered));
}

void target_eventbus_verify_owned_payload_and_stop(void) {
    callback_release = xSemaphoreCreateBinary();
    callback_result = xQueueCreate(2, 80);
    TEST_ASSERT_NOT_NULL(callback_release);
    TEST_ASSERT_NOT_NULL(callback_result);
    atomic_store(&callback_entered, false);
    atomic_store(&callback_failed, false);
    IEventBus *bus = GetEventBus();
    TEST_ASSERT_EQUAL(0, bus->base.Initialize(&bus->base, NULL));
    bus_initialized = true;
    TEST_ASSERT_EQUAL(0, bus->Subscribe(bus, "BLOCK", target_bus_callback));
    TEST_ASSERT_EQUAL(0, bus->Subscribe(bus, "OWNED", target_bus_callback));
    TEST_ASSERT_EQUAL(0, bus->base.Start(&bus->base));
    TEST_ASSERT_EQUAL(0, bus->Publish((BusEvent){.eventName="BLOCK"}));
    wait_for_callback();
    TEST_ASSERT_EQUAL(EVENT_BUS_ERR_BUSY, bus->Unsubscribe(bus, "BLOCK", target_bus_callback));
    unsigned char source[80], received[80];
    memset(source, 0x6A, sizeof(source));
    char name[] = "OWNED";
    TEST_ASSERT_EQUAL(0, bus->Publish((BusEvent){.eventName=name,.data=source,.dataSize=sizeof(source)}));
    memset(source, 0, sizeof(source)); name[0]='X';
    xSemaphoreGive(callback_release);
    TEST_ASSERT_EQUAL(pdTRUE, xQueueReceive(callback_result, received, pdMS_TO_TICKS(1000)));
    for (size_t i=0;i<sizeof(received);++i) TEST_ASSERT_EQUAL_HEX8(0x6A, received[i]);

    atomic_store(&callback_entered, false);
    TEST_ASSERT_EQUAL(0, bus->Publish((BusEvent){.eventName="BLOCK"}));
    wait_for_callback();
    TEST_ASSERT_EQUAL(0, bus->Publish((BusEvent){.eventName="OWNED",.data=source,.dataSize=sizeof(source)}));
    TEST_ASSERT_NOT_EQUAL(0, bus->base.Stop(&bus->base));
    TEST_ASSERT_NOT_NULL(atomic_load(&busInstance.worker.task));
    TEST_ASSERT_NOT_EQUAL(0, bus->base.Start(&bus->base));
    xSemaphoreGive(callback_release);
    TEST_ASSERT_EQUAL(0, bus->base.Stop(&bus->base));
    TEST_ASSERT_NULL(atomic_load(&busInstance.worker.task));
    TEST_ASSERT_EQUAL(0, bus->base.Start(&bus->base));
    memset(source, 0x7B, sizeof(source));
    TEST_ASSERT_EQUAL(0, bus->Publish((BusEvent){.eventName="OWNED",.data=source,.dataSize=sizeof(source)}));
    TEST_ASSERT_EQUAL(pdTRUE, xQueueReceive(callback_result, received, pdMS_TO_TICKS(1000)));
    for (size_t i=0;i<sizeof(received);++i) TEST_ASSERT_EQUAL_HEX8(0x7B, received[i]);
    TEST_ASSERT_FALSE(atomic_load(&callback_failed));
    TEST_ASSERT_EQUAL(0, bus->base.Stop(&bus->base));
    TEST_ASSERT_EQUAL(0, bus->Unsubscribe(bus, "BLOCK", target_bus_callback));
}
