#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "Hardware/Relay/RelayInternal.h"
#include "unity.h"
#include <stdatomic.h>

/* Inject only GPIO actuation; synchronization and both timers are real SDK paths. */
static atomic_uint physical_edges, probe_ticks;
static esp_err_t fixture_set_level(gpio_num_t pin, uint32_t level) {
    (void)pin; (void)level;
    atomic_fetch_add(&physical_edges, 1);
    return ESP_OK;
}
#define gpio_set_level fixture_set_level
#include "../../../../src/Hardware/Relay/RelayDriver.c"
#undef gpio_set_level
esp_err_t Relay_RegisterCommands(void) { return ESP_OK; }
#include "../../../../src/Hardware/Relay/RelayStats.c"
#include "../../../../src/Hardware/Relay/Relay.c"

static esp_timer_handle_t probe;
static bool fixture_owned;
static void probe_callback(void *arg) { (void)arg; atomic_fetch_add(&probe_ticks, 1); }

void target_relay_timer_cleanup(void) {
    if (!fixture_owned) return;
    TEST_ASSERT_EQUAL(ESP_OK, Relay_Lock());
    TEST_ASSERT_EQUAL(ESP_OK, Relay_CancelAutoOff());
    relayService.state = SERVICE_STATE_STOPPED;
    xSemaphoreGive(relayService.mutex);
    if (probe) {
        esp_err_t stopped = esp_timer_stop(probe);
        TEST_ASSERT_TRUE(stopped == ESP_OK || stopped == ESP_ERR_INVALID_STATE);
        TEST_ASSERT_EQUAL(ESP_OK, esp_timer_delete(probe));
        probe = NULL;
    }
    if (relayService.async_timer) {
        TEST_ASSERT_EQUAL(ESP_OK, esp_timer_delete(relayService.async_timer));
        relayService.async_timer = NULL;
    }
    /* Keep the mutex alive until chip reset, including any dispatched callback. */
    fixture_owned = false;
}

void target_relay_timer_verify_nonblocking(void) {
    Relay_GetModule();
    if (!relayService.mutex) relayService.mutex = xSemaphoreCreateMutex();
    TEST_ASSERT_NOT_NULL(relayService.mutex);
    fixture_owned = true;
    relayService.relay_pin = GPIO_NUM_0;
    relayService.eventBus = NULL;
    relayService.state = SERVICE_STATE_STARTED;
    relayService.current_status = RELAY_STATE_PULSING;
    relayService.auto_off_pending = false;
    relayService.last_switch_time_us = 0;
    atomic_store(&physical_edges, 0);
    atomic_store(&probe_ticks, 0);
    esp_timer_create_args_t args = {.callback = Relay_TimerCallback, .name = "target_relay"};
    TEST_ASSERT_EQUAL(ESP_OK, esp_timer_create(&args, &relayService.async_timer));
    args.callback = probe_callback;
    args.name = "target_timer_probe";
    TEST_ASSERT_EQUAL(ESP_OK, esp_timer_create(&args, &probe));
    TEST_ASSERT_EQUAL(ESP_OK, esp_timer_start_periodic(probe, 20000));
    TEST_ASSERT_EQUAL(ESP_OK, Relay_Lock());
    relayService.auto_off_deadline_us = (uint64_t)esp_timer_get_time() + 30000;
    relayService.auto_off_pending = true;
    esp_err_t started = esp_timer_start_periodic(relayService.async_timer,
        (uint64_t)RELAY_TIMER_CHECK_INTERVAL_MS * 1000ULL);
    if (started != ESP_OK) {
        relayService.auto_off_pending = false;
        xSemaphoreGive(relayService.mutex);
        TEST_ASSERT_EQUAL(ESP_OK, started);
        return;
    }
    vTaskDelay(pdMS_TO_TICKS(120));
    unsigned during_contention = atomic_load(&probe_ticks);
    unsigned blocked_edges = atomic_load(&physical_edges);
    xSemaphoreGive(relayService.mutex);
    TEST_ASSERT_TRUE(during_contention >= 3);
    TEST_ASSERT_EQUAL_UINT(0, blocked_edges);
    for (unsigned i = 0; i < 50 && atomic_load(&physical_edges) == 0; ++i)
        vTaskDelay(pdMS_TO_TICKS(10));
    TEST_ASSERT_EQUAL_UINT(1, atomic_load(&physical_edges));
    TEST_ASSERT_EQUAL(ESP_OK, Relay_Lock());
    bool pending = relayService.auto_off_pending;
    RelayState state = relayService.current_status;
    xSemaphoreGive(relayService.mutex);
    TEST_ASSERT_FALSE(pending);
    TEST_ASSERT_EQUAL(RELAY_STATE_CLOSED, state);
    vTaskDelay(pdMS_TO_TICKS(50));
    TEST_ASSERT_EQUAL_UINT(1, atomic_load(&physical_edges));
}
