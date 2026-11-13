#include "unity.h"
#define ServiceLocator_GetService WatchdogTarget_GetService
#include "../../../../src/Management/Watchdog/Watchdog.c"
#undef ServiceLocator_GetService
#include "../../../../src/Management/Watchdog/WatchdogConfig.c"
#include "../../../../src/Management/Watchdog/WatchdogTimers.c"
#include "../../../../src/Management/Watchdog/WatchdogTaskRegistry.c"

static bool initialized;
static bool mutexHeld;
IService *WatchdogTarget_GetService(const char *name) { (void)name; return NULL; }

void target_watchdog_feed_cleanup(void) {
    if (mutexHeld) {
        xSemaphoreGive(watchdogService.mutex);
        mutexHeld = false;
    }
    if (initialized) {
        TEST_ASSERT_EQUAL(0, Watchdog_Stop(&watchdogService.base.base));
        TEST_ASSERT_EQUAL(ESP_OK, esp_task_wdt_deinit());
        initialized = false;
    }
}

void target_watchdog_feed_verify_contention(void) {
    IWatchdog *wd = Watchdog_GetModule();
    TEST_ASSERT_EQUAL(0, wd->base.Initialize(&wd->base, NULL));
    initialized = true;
    TEST_ASSERT_EQUAL(0, wd->base.Start(&wd->base));
    TEST_ASSERT_EQUAL(pdTRUE, xSemaphoreTake(watchdogService.mutex, pdMS_TO_TICKS(100)));
    mutexHeld = true;
    /* More than two real 4-second timer periods, with the 5-second SDK
     * watchdog still enabled. The old callback cannot feed under this lock. */
    vTaskDelay(pdMS_TO_TICKS(8500));
    xSemaphoreGive(watchdogService.mutex);
    mutexHeld = false;
    WatchdogStats_t stats;
    TEST_ASSERT_EQUAL(0, wd->GetStats(wd, &stats));
    TEST_ASSERT_GREATER_OR_EQUAL(2, stats.totalAutoFeeds);
    TEST_ASSERT_EQUAL(0, stats.totalTimeouts);
}
