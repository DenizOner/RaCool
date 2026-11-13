#include "test_mocks.h"
#include "unity.h"
#include "oled_sdk.h"
#include <setjmp.h>
// Model the production 100 Hz tick; the shared host mock otherwise uses 1 ms.
#undef pdMS_TO_TICKS
#define pdMS_TO_TICKS(ms) ((TickType_t)((ms) / 10U))
#define MOCK_LVGL_H
#define pdTICKS_TO_MS(value) (value)
typedef struct { int x1, y1, x2, y2; } lv_area_t;
typedef struct { int width, height; void *user; } lv_display_t;
#define LV_COLOR_FORMAT_I1 1
#define LV_DISPLAY_RENDER_MODE_FULL 1
typedef struct { size_t free_size, free_biggest_size; unsigned used_pct; } lv_mem_monitor_t;
FAKE_VOID_FUNC1(lv_mem_monitor, lv_mem_monitor_t *);
FAKE_VALUE_FUNC1(int, lv_display_get_render_mode, lv_display_t *);
#define LV_DRAW_BUF_SIZE(w,h,f) (((w + 7) / 8) * h + 8)
static uint32_t mock_stride = 16;
static uint32_t lv_draw_buf_width_to_stride(uint32_t width, int format) { (void)width; (void)format; return mock_stride; }
FAKE_VALUE_FUNC0(uint32_t, lv_timer_handler);
FAKE_VOID_FUNC0(lv_init);
typedef void lv_obj_t;
FAKE_VOID_FUNC1(lv_obj_invalidate, lv_obj_t *);
FAKE_VALUE_FUNC1(lv_obj_t *, lv_display_get_screen_active, lv_display_t *);
FAKE_VOID_FUNC1(lv_refr_now, lv_display_t *);
typedef uint32_t (*tick_cb_t)(void);
FAKE_VOID_FUNC1(lv_tick_set_cb, tick_cb_t);
FAKE_VALUE_FUNC2(lv_display_t *, lv_display_create, int32_t, int32_t);
FAKE_VOID_FUNC2(lv_display_set_color_format, lv_display_t *, int);
FAKE_VOID_FUNC5(lv_display_set_buffers, lv_display_t *, void *, void *, uint32_t, int);
typedef void (*flush_cb_t)(lv_display_t *, const lv_area_t *, uint8_t *);
FAKE_VOID_FUNC2(lv_display_set_flush_cb, lv_display_t *, flush_cb_t);
static void lv_display_set_user_data(lv_display_t *d, void *u) { d->user=u; }
static void *lv_display_get_user_data(lv_display_t *d) { return d->user; }
static int lv_display_get_horizontal_resolution(lv_display_t *d) { return d->width; }
static int lv_display_get_vertical_resolution(lv_display_t *d) { return d->height; }
FAKE_VOID_FUNC1(lv_display_flush_ready, lv_display_t *);
FAKE_VALUE_FUNC3(esp_err_t, esp_lcd_new_panel_io_i2c, i2c_master_bus_handle_t, const esp_lcd_panel_io_i2c_config_t *, esp_lcd_panel_io_handle_t *);
FAKE_VALUE_FUNC3(esp_err_t, esp_lcd_new_panel_ssd1306, esp_lcd_panel_io_handle_t, const esp_lcd_panel_dev_config_t *, esp_lcd_panel_handle_t *);
FAKE_VALUE_FUNC1(esp_err_t, esp_lcd_panel_reset, esp_lcd_panel_handle_t);
FAKE_VALUE_FUNC1(esp_err_t, esp_lcd_panel_init, esp_lcd_panel_handle_t);
FAKE_VALUE_FUNC3(esp_err_t, esp_lcd_panel_mirror, esp_lcd_panel_handle_t, bool, bool);
FAKE_VALUE_FUNC2(esp_err_t, esp_lcd_panel_disp_on_off, esp_lcd_panel_handle_t, bool);
FAKE_VALUE_FUNC2(esp_err_t, esp_lcd_panel_invert_color, esp_lcd_panel_handle_t, bool);
FAKE_VALUE_FUNC4(esp_err_t, esp_lcd_panel_io_tx_param, esp_lcd_panel_io_handle_t, int, const void *, size_t);
FAKE_VALUE_FUNC6(esp_err_t, esp_lcd_panel_draw_bitmap, esp_lcd_panel_handle_t, int, int, int, int, const void *);
FAKE_VALUE_FUNC1(esp_err_t, esp_lcd_panel_del, esp_lcd_panel_handle_t);
FAKE_VALUE_FUNC1(esp_err_t, esp_lcd_panel_io_del, esp_lcd_panel_io_handle_t);
static unsigned page_yields;
#define taskYIELD() (++page_yields)
#include "Hardware/OLED/OLED.c"
#undef taskYIELD


static lv_display_t fixture_display;
static unsigned acquired_bus, released_bus;
static esp_err_t release_result;
static esp_err_t execute_result;
static unsigned external_operations;
static esp_err_t execute_external(II2CManager *self, const void *owner,
                                  esp_err_t (*operation)(void *), void *context) {
    (void)self; TEST_ASSERT_EQUAL_PTR(&oledService, owner);
    external_operations++;
    return execute_result == ESP_OK ? operation(context) : execute_result;
}
static esp_err_t acquire_bus(II2CManager *self, const void *owner, i2c_master_bus_handle_t *out) {
    (void)self; TEST_ASSERT_EQUAL_PTR(&oledService, owner);
    acquired_bus++; *out = (void *)0x30; return ESP_OK;
}
static esp_err_t release_bus(II2CManager *self, const void *owner) {
    (void)self; TEST_ASSERT_EQUAL_PTR(&oledService, owner);
    released_bus++; return release_result;
}
static II2CManager fixture_manager = {.AcquireBus = acquire_bus, .ReleaseBus = release_bus,
    .ExecuteExternal = execute_external};
static void setup_transfer(void) {
    oledService.bus_manager = &fixture_manager;
    oledService.bus_acquired = true;
    oledService.panel_handle = (void *)1;
}
static IService *fixture_service(const char *name) {
    return strcmp(name, SVC_NAME_I2C_MANAGER) == 0 ? (IService *)&fixture_manager : NULL;
}
static esp_err_t create_io(i2c_master_bus_handle_t bus, const esp_lcd_panel_io_i2c_config_t *cfg,
                            esp_lcd_panel_io_handle_t *out) {
    (void)bus; (void)cfg; *out = (void *)0x40; return ESP_OK;
}
static esp_err_t create_panel(esp_lcd_panel_io_handle_t io, const esp_lcd_panel_dev_config_t *cfg,
                               esp_lcd_panel_handle_t *out) {
    (void)io; (void)cfg; *out = (void *)0x50; return ESP_OK;
}
static BaseType_t create_oled_task(TaskFunction_t task, const char *name, uint32_t size, void *arg,
                                  UBaseType_t priority, TaskHandle_t *out) {
    (void)task; (void)name; (void)size; (void)arg; (void)priority;
    *out = (void *)0x60; return pdPASS;
}
static BaseType_t acknowledge_stop(QueueHandle_t queue, void *out, TickType_t wait) {
    (void)queue; (void)wait; *(int32_t *)out = 0; return pdTRUE;
}
static void setup_start(void) {
    mocked_ServiceLocator_GetService_fake.custom_fake = fixture_service;
    esp_lcd_new_panel_io_i2c_fake.custom_fake = create_io;
    esp_lcd_new_panel_ssd1306_fake.custom_fake = create_panel;
    mocked_xTaskCreate_fake.custom_fake = create_oled_task;
    mocked_xQueueReceive_fake.custom_fake = acknowledge_stop;
    lv_display_create_fake.return_val = &fixture_display;
    TEST_ASSERT_EQUAL(0, OLED_Base_Initialize(&oledService.base.base, NULL));
}

void setUp(void) {
    RESET_FAKE(lv_mem_monitor);
    RESET_FAKE(lv_display_get_render_mode);
    page_yields=0;
    mock_stride = 16;
    IOLED interface = *OLED_GetModule();
    memset(&oledService, 0, sizeof(oledService));
    oledService.base = interface;
    RESET_FAKE(mocked_xSemaphoreCreateMutex);
    RESET_FAKE(mocked_xSemaphoreTake);
    RESET_FAKE(mocked_xSemaphoreGive);
    RESET_FAKE(mocked_xQueueCreate);
    mocked_xSemaphoreCreateMutex_fake.return_val = (void *)0x10;
    mocked_xSemaphoreTake_fake.return_val = pdTRUE;
    mocked_xQueueCreate_fake.return_val = (void *)0x20;
    oledService.lvgl_mutex = (void *)0x10;
    acquired_bus = released_bus = 0;
    release_result = ESP_OK;
    execute_result = ESP_OK;
    external_operations = 0;
    fixture_display = (lv_display_t){128, 64, NULL};
    RESET_FAKE(esp_lcd_panel_del);
    RESET_FAKE(esp_lcd_panel_io_del);
    RESET_FAKE(esp_lcd_new_panel_io_i2c);
    RESET_FAKE(esp_lcd_new_panel_ssd1306);
    RESET_FAKE(esp_lcd_panel_reset);
    RESET_FAKE(esp_lcd_panel_init);
    RESET_FAKE(esp_lcd_panel_mirror);
    RESET_FAKE(lv_display_create);
    RESET_FAKE(lv_init);
    RESET_FAKE(mocked_xTaskCreate);
    RESET_FAKE(mocked_xQueueReceive);
    RESET_FAKE(mocked_xTaskGetCurrentTaskHandle);
    RESET_FAKE(mocked_vTaskDelete);
    RESET_FAKE(mocked_ServiceLocator_GetService);
    RESET_FAKE(esp_lcd_panel_disp_on_off);
    RESET_FAKE(esp_lcd_panel_io_tx_param);
    RESET_FAKE(esp_lcd_panel_invert_color);
    RESET_FAKE(esp_lcd_panel_draw_bitmap);
    RESET_FAKE(lv_display_flush_ready);
}
void tearDown(void) { free(oledService.lv_buf); oledService.lv_buf = NULL; }
static jmp_buf oled_task_exit;
static TickType_t observed_wait;
static void capture_task_delay(TickType_t ticks) {
    observed_wait = ticks;
    TEST_ASSERT_EQUAL(1, mocked_xSemaphoreGive_fake.call_count);
    longjmp(oled_task_exit, 1);
}
void test_OLED_LvglTaskBlocksAtCoarseTickRate(void) {
    const uint32_t lvgl_waits[] = {0, 1, 5, 9, 500, UINT32_MAX};
    for (unsigned i = 0; i < sizeof(lvgl_waits) / sizeof(lvgl_waits[0]); ++i) {
        RESET_FAKE(mocked_xSemaphoreTake);
        RESET_FAKE(mocked_xSemaphoreGive);
        RESET_FAKE(mocked_vTaskDelay);
        RESET_FAKE(lv_timer_handler);
        mocked_xSemaphoreTake_fake.return_val = pdTRUE;
        mocked_vTaskDelay_fake.custom_fake = capture_task_delay;
        lv_timer_handler_fake.return_val = lvgl_waits[i];
        if (setjmp(oled_task_exit) == 0) oled_lvgl_task(NULL);
        TEST_ASSERT_GREATER_THAN(0, observed_wait);
        TEST_ASSERT_LESS_OR_EQUAL(50, observed_wait);
        TEST_ASSERT_EQUAL(1, lv_timer_handler_fake.call_count);
    }
}
void test_OLED_Lifecycle(void) {
    IService *service = &OLED_GetModule()->base;
    TEST_ASSERT_EQUAL(0, service->Initialize(service, NULL));
    TEST_ASSERT_EQUAL(SERVICE_STATE_INITIALIZED, service->GetState(service));
    TEST_ASSERT_NOT_EQUAL(0, service->Start(service));
    TEST_ASSERT_EQUAL(SERVICE_STATE_INITIALIZED, service->GetState(service));
}
void test_OLED_Interface(void) {
    setup_transfer();
    oledService.panel_handle = NULL;
    IOLED *oled = OLED_GetModule();
    oled->SetPower(oled, true);
    TEST_ASSERT_EQUAL(0, esp_lcd_panel_disp_on_off_fake.call_count);
    oledService.panel_handle = (void *)1;
    oledService.io_handle = (void *)2;
    oled->SetPower(oled, false);
    TEST_ASSERT_EQUAL(1, esp_lcd_panel_disp_on_off_fake.call_count);
    TEST_ASSERT_FALSE(esp_lcd_panel_disp_on_off_fake.arg1_val);
    oled->SetContrast(oled, 0xAA);
    TEST_ASSERT_EQUAL(0x81, esp_lcd_panel_io_tx_param_fake.arg1_val);
    TEST_ASSERT_EQUAL(1, esp_lcd_panel_io_tx_param_fake.arg3_val);
    oled->InvertDisplay(oled, true);
    TEST_ASSERT_TRUE(esp_lcd_panel_invert_color_fake.arg1_val);
    TEST_ASSERT_EQUAL(3, external_operations);
}
void test_OLED_FlushBitAndPageBoundaries(void) {
    setup_transfer();
    lv_display_t display = {128,64,(void *)1};
    lv_area_t area = {0,0,127,63};
    uint8_t data[1032] = {0};
    memset(data, 0xFF, 8); // Palette bytes must never be drawn as pixels.
    data[8] = 0x81;
    data[8+7*16+1] = 0x80;
    data[8+8*16+1] = 0x80;
    data[1031] = 1;
    OLED_FlushCB(&display, &area, data);
    TEST_ASSERT_EQUAL(8, esp_lcd_panel_draw_bitmap_fake.call_count);
    TEST_ASSERT_EQUAL(8, external_operations);
    TEST_ASSERT_EQUAL(7,page_yields);
    const uint8_t *pages = esp_lcd_panel_draw_bitmap_fake.arg5_history[0];
    for (unsigned page=0;page<8;++page) {
        TEST_ASSERT_EQUAL(0,esp_lcd_panel_draw_bitmap_fake.arg1_history[page]);
        TEST_ASSERT_EQUAL(page*8,esp_lcd_panel_draw_bitmap_fake.arg2_history[page]);
        TEST_ASSERT_EQUAL(128,esp_lcd_panel_draw_bitmap_fake.arg3_history[page]);
        TEST_ASSERT_EQUAL((page+1)*8,esp_lcd_panel_draw_bitmap_fake.arg4_history[page]);
        TEST_ASSERT_EQUAL_PTR(pages+page*128,esp_lcd_panel_draw_bitmap_fake.arg5_history[page]);
    }
    for (int i=0; i<1024; ++i) {
        uint8_t expected = (i==0 || i==7 || i==136) ? 1 : ((i==8 || i==1023) ? 128 : 0);
        TEST_ASSERT_EQUAL_UINT8(expected, pages[i]);
    }
    TEST_ASSERT_EQUAL(1, lv_display_flush_ready_fake.call_count);
}

void test_OLED_InvalidFrameIsReleasedWithoutTransfer(void) {
    lv_display_t display = {255,255,(void *)1};
    lv_area_t area = {0,0,254,254};
    uint8_t data[8] = {0};
    OLED_FlushCB(&display, &area, data);
    TEST_ASSERT_EQUAL(0, esp_lcd_panel_draw_bitmap_fake.call_count);
    TEST_ASSERT_EQUAL(1, lv_display_flush_ready_fake.call_count);
    display.width=128; display.height=64;
    area.x2=63; area.y2=63;
    OLED_FlushCB(&display, &area, data);
    TEST_ASSERT_EQUAL(0, esp_lcd_panel_draw_bitmap_fake.call_count);
    TEST_ASSERT_EQUAL(2, lv_display_flush_ready_fake.call_count);
}
void test_OLED_AlignedRowsAndTransferFailureReleaseFlush(void) {
    setup_transfer();
    lv_display_t display = {128,64,(void *)1};
    lv_area_t area = {0,0,127,63};
    uint8_t data[8+32*64] = {0};
    mock_stride=32;
    data[8+32] = 0x80;
    esp_lcd_panel_draw_bitmap_fake.return_val=ESP_FAIL;
    OLED_FlushCB(&display, &area, data);
    TEST_ASSERT_EQUAL(1,esp_lcd_panel_draw_bitmap_fake.call_count);
    TEST_ASSERT_EQUAL(1,external_operations);
    TEST_ASSERT_EQUAL(0,page_yields);
    const uint8_t *pages=esp_lcd_panel_draw_bitmap_fake.arg5_val;
    TEST_ASSERT_EQUAL_UINT8(2,pages[0]);
    TEST_ASSERT_EQUAL(1,lv_display_flush_ready_fake.call_count);
}
void test_OLED_BusContentionSkipsSdkAndReleasesFlush(void) {
    setup_transfer();
    oledService.io_handle = (void *)2;
    execute_result = ESP_ERR_TIMEOUT;
    lv_display_t display = {128,64,(void *)1};
    lv_area_t area = {0,0,127,63};
    uint8_t data[1032] = {0};
    OLED_FlushCB(&display, &area, data);
    TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT, oledService.base.SetPower(&oledService.base, false));
    TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT, oledService.base.SetContrast(&oledService.base, 10));
    TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT, oledService.base.InvertDisplay(&oledService.base, true));
    TEST_ASSERT_EQUAL(4, external_operations);
    TEST_ASSERT_EQUAL(0, esp_lcd_panel_draw_bitmap_fake.call_count);
    TEST_ASSERT_EQUAL(0, esp_lcd_panel_disp_on_off_fake.call_count);
    TEST_ASSERT_EQUAL(0, esp_lcd_panel_io_tx_param_fake.call_count);
    TEST_ASSERT_EQUAL(0, esp_lcd_panel_invert_color_fake.call_count);
    TEST_ASSERT_EQUAL(1, lv_display_flush_ready_fake.call_count);
}
void test_OLED_RejectUnsupportedConfiguration(void) {
    IOLED *oled=OLED_GetModule();
    OLEDConfig config=OLED_GetDefaultConfig();
    config.width=255;
    TEST_ASSERT_FALSE(oled->Initialize(oled,config));
    config.width=128;
    TEST_ASSERT_TRUE(oled->Initialize(oled,config));
}

void test_OLED_StopTimeoutKeepsResourcesAndRetryPreservesDisplay(void) {
    setup_start();
    TEST_ASSERT_EQUAL(0, OLED_Start(&oledService.base.base));
    void *buffer = oledService.lv_buf;
    TEST_ASSERT_EQUAL(1, acquired_bus);
    mocked_xQueueReceive_fake.custom_fake = NULL;
    mocked_xQueueReceive_fake.return_val = pdFALSE;
    TEST_ASSERT_EQUAL(-1, OLED_Stop(&oledService.base.base));
    TEST_ASSERT_EQUAL_PTR((void *)0x60, atomic_load(&oledService.worker.task));
    TEST_ASSERT_EQUAL_PTR((void *)0x50, oledService.panel_handle);
    TEST_ASSERT_EQUAL(0, esp_lcd_panel_del_fake.call_count);
    TEST_ASSERT_EQUAL(0, released_bus);
    TEST_ASSERT_EQUAL(-1, OLED_Start(&oledService.base.base));
    mocked_xQueueReceive_fake.custom_fake = acknowledge_stop;
    TEST_ASSERT_EQUAL(0, OLED_Stop(&oledService.base.base));
    TEST_ASSERT_NULL(atomic_load(&oledService.worker.task));
    TEST_ASSERT_EQUAL(1, released_bus);
    TEST_ASSERT_EQUAL_PTR(buffer, oledService.lv_buf);
    TEST_ASSERT_EQUAL_PTR(&fixture_display, oledService.display);
    TEST_ASSERT_NULL(fixture_display.user);
    TEST_ASSERT_EQUAL(0, OLED_Start(&oledService.base.base));
    TEST_ASSERT_EQUAL(2, acquired_bus);
    TEST_ASSERT_EQUAL(1, lv_display_create_fake.call_count);
    TEST_ASSERT_EQUAL(1, lv_init_fake.call_count);
    TEST_ASSERT_EQUAL_PTR(buffer, oledService.lv_buf);
    TEST_ASSERT_EQUAL_PTR((void *)0x50, fixture_display.user);
    TEST_ASSERT_EQUAL(0, OLED_Stop(&oledService.base.base));
}

void test_OLED_PartialStartRollsBackPanelIoAndBus(void) {
    setup_start();
    esp_lcd_panel_init_fake.return_val = ESP_FAIL;
    TEST_ASSERT_EQUAL(-1, OLED_Start(&oledService.base.base));
    TEST_ASSERT_EQUAL(1, esp_lcd_panel_del_fake.call_count);
    TEST_ASSERT_EQUAL(1, esp_lcd_panel_io_del_fake.call_count);
    TEST_ASSERT_EQUAL(1, released_bus);
    TEST_ASSERT_NULL(oledService.panel_handle);
    TEST_ASSERT_NULL(oledService.io_handle);
    TEST_ASSERT_EQUAL(SERVICE_STATE_INITIALIZED, oledService.state);
    esp_lcd_panel_init_fake.return_val = ESP_OK;
    TEST_ASSERT_EQUAL(0, OLED_Start(&oledService.base.base));
    TEST_ASSERT_EQUAL(0, OLED_Stop(&oledService.base.base));
}

void test_OLED_CleanupFailureRetainsBusUntilRetry(void) {
    setup_start();
    TEST_ASSERT_EQUAL(0, OLED_Start(&oledService.base.base));
    esp_lcd_panel_io_del_fake.return_val = ESP_FAIL;
    TEST_ASSERT_EQUAL(-1, OLED_Stop(&oledService.base.base));
    TEST_ASSERT_NULL(oledService.panel_handle);
    TEST_ASSERT_NOT_NULL(oledService.io_handle);
    TEST_ASSERT_EQUAL(0, released_bus);
    TEST_ASSERT_EQUAL(SERVICE_STATE_ERROR, oledService.state);
    esp_lcd_panel_io_del_fake.return_val = ESP_OK;
    release_result = ESP_ERR_TIMEOUT;
    TEST_ASSERT_EQUAL(-1, OLED_Stop(&oledService.base.base));
    TEST_ASSERT_TRUE(oledService.bus_acquired);
    release_result = ESP_OK;
    TEST_ASSERT_EQUAL(0, OLED_Stop(&oledService.base.base));
    TEST_ASSERT_FALSE(oledService.bus_acquired);
    TEST_ASSERT_EQUAL(1, esp_lcd_panel_del_fake.call_count);
}

void test_OLED_StartFailureStagesCanRetryWithoutLosingBus(void) {
    for (unsigned stage = 0; stage < 4; ++stage) {
        setUp(); setup_start();
        if (stage == 0) {
            esp_lcd_new_panel_io_i2c_fake.custom_fake = NULL;
            esp_lcd_new_panel_io_i2c_fake.return_val = ESP_FAIL;
        } else if (stage == 1) {
            esp_lcd_new_panel_ssd1306_fake.custom_fake = NULL;
            esp_lcd_new_panel_ssd1306_fake.return_val = ESP_FAIL;
        } else if (stage == 2) lv_display_create_fake.return_val = NULL;
        else { mocked_xTaskCreate_fake.custom_fake = NULL; mocked_xTaskCreate_fake.return_val = pdFAIL; }
        TEST_ASSERT_EQUAL(-1, OLED_Start(&oledService.base.base));
        TEST_ASSERT_EQUAL(1, acquired_bus);
        TEST_ASSERT_EQUAL(1, released_bus);
        TEST_ASSERT_NULL(oledService.io_handle);
        TEST_ASSERT_NULL(oledService.panel_handle);
        TEST_ASSERT_NULL(atomic_load(&oledService.worker.task));
        if (stage == 2) TEST_ASSERT_NULL(oledService.lv_buf);
        setup_start();
        TEST_ASSERT_EQUAL(0, OLED_Start(&oledService.base.base));
        TEST_ASSERT_EQUAL(0, OLED_Stop(&oledService.base.base));
        tearDown();
    }
}

void test_OLED_PowerOffFailurePreservesPanelForStopRetry(void) {
    setup_start();
    TEST_ASSERT_EQUAL(0, OLED_Start(&oledService.base.base));
    esp_lcd_panel_disp_on_off_fake.return_val = ESP_FAIL;
    TEST_ASSERT_EQUAL(-1, OLED_Stop(&oledService.base.base));
    TEST_ASSERT_EQUAL(0, esp_lcd_panel_del_fake.call_count);
    TEST_ASSERT_TRUE(oledService.bus_acquired);
    esp_lcd_panel_disp_on_off_fake.return_val = ESP_OK;
    TEST_ASSERT_EQUAL(0, OLED_Stop(&oledService.base.base));
    TEST_ASSERT_EQUAL(1, released_bus);
}

void test_OLED_PublicLockReportsMissingResourceAndTimeout(void) {
    IOLED *oled = OLED_GetModule();
    oledService.lvgl_mutex = NULL;
    TEST_ASSERT_FALSE(oled->Lock(oled));
    oledService.lvgl_mutex = (SemaphoreHandle_t)0x100;
    TEST_ASSERT_FALSE(oled->Lock(NULL));
    mocked_xSemaphoreTake_fake.return_val = pdFALSE;
    TEST_ASSERT_FALSE(oled->Lock(oled));
    mocked_xSemaphoreTake_fake.return_val = pdTRUE;
    TEST_ASSERT_TRUE(oled->Lock(oled));
    TEST_ASSERT_EQUAL(pdMS_TO_TICKS(OLED_CONTROL_TIMEOUT_MS), mocked_xSemaphoreTake_fake.arg1_val);
    oled->Unlock(oled);
}

#include "Hardware/OLED/OLEDConsoleCommands.c"

void test_OLED_CommandsReturnSdkFailureAndReleaseLock(void) {
    setup_transfer();
    oledService.io_handle = (void *)2;
    IOLED *oled = &oledService.base;
    esp_lcd_panel_disp_on_off_fake.return_val = ESP_FAIL;
    esp_lcd_panel_io_tx_param_fake.return_val = ESP_FAIL;
    esp_lcd_panel_invert_color_fake.return_val = ESP_FAIL;
    TEST_ASSERT_EQUAL(ESP_FAIL, oled->SetPower(oled, true));
    TEST_ASSERT_EQUAL(ESP_FAIL, oled->SetContrast(oled, 23));
    TEST_ASSERT_EQUAL(ESP_FAIL, oled->InvertDisplay(oled, true));
    TEST_ASSERT_EQUAL(3, mocked_xSemaphoreGive_fake.call_count);
    esp_lcd_panel_disp_on_off_fake.return_val = ESP_OK;
    esp_lcd_panel_io_tx_param_fake.return_val = ESP_OK;
    esp_lcd_panel_invert_color_fake.return_val = ESP_OK;
    TEST_ASSERT_EQUAL(ESP_OK, oled->SetPower(oled, true));
    TEST_ASSERT_EQUAL(ESP_OK, oled->SetContrast(oled, 23));
    TEST_ASSERT_EQUAL(ESP_OK, oled->InvertDisplay(oled, false));
}

void test_OLED_CliRejectsMalformedValuesWithoutSdkCalls(void) {
    setup_transfer();
    oledService.io_handle = (void *)2;
    char *values[] = {"", "-1", "256", "999999999999999999999", "1.5", "2x", " 2"};
    for (unsigned i = 0; i < sizeof(values) / sizeof(values[0]); ++i) {
        char *args[] = {"oled", "contrast", values[i]};
        TEST_ASSERT_NOT_EQUAL(0, cmd_oled_handler(3, args));
    }
    char *invert[] = {"oled", "invert", "2"};
    TEST_ASSERT_NOT_EQUAL(0, cmd_oled_handler(3, invert));
    char *extra[] = {"oled", "on", "extra"};
    TEST_ASSERT_NOT_EQUAL(0, cmd_oled_handler(3, extra));
    TEST_ASSERT_EQUAL(0, external_operations);
    char *valid[] = {"oled", "contrast", "255"};
    TEST_ASSERT_EQUAL(0, cmd_oled_handler(3, valid));
    esp_lcd_panel_io_tx_param_fake.return_val = ESP_FAIL;
    TEST_ASSERT_NOT_EQUAL(0, cmd_oled_handler(3, valid));
}

void test_OLED_CLIRejectsMissingTokensWithoutDriverOperations(void) {
  TEST_ASSERT_NOT_EQUAL(0, cmd_oled_handler(0, NULL));
  char *missing[] = {"oled", NULL}; TEST_ASSERT_NOT_EQUAL(0, cmd_oled_handler(2, missing));
  missing[1] = ""; TEST_ASSERT_NOT_EQUAL(0, cmd_oled_handler(2, missing));
  char *no_value[] = {"oled", "contrast", NULL}; TEST_ASSERT_NOT_EQUAL(0, cmd_oled_handler(3, no_value));
  TEST_ASSERT_EQUAL(0, external_operations);
}
void test_OLED_CLIMissingCallbacksAreNotSuccessfulStatusOrWrites(void) {
  IOLED *oled = OLED_GetModule(); IOLED saved = *oled;
  oled->base.GetState = NULL; char *status[] = {"oled", "status"}; int st = cmd_oled_handler(2, status);
  oled->SetPower = NULL; char *on[] = {"oled", "on"}; int power = cmd_oled_handler(2, on);
  oled->SetContrast = NULL; char *contrast[] = {"oled", "contrast", "128"}; int ctr = cmd_oled_handler(3, contrast);
  oled->InvertDisplay = NULL; char *invert[] = {"oled", "invert", "1"}; int inv = cmd_oled_handler(3, invert);
  *oled = saved;
  TEST_ASSERT_NOT_EQUAL(0, st); TEST_ASSERT_NOT_EQUAL(0, power); TEST_ASSERT_NOT_EQUAL(0, ctr); TEST_ASSERT_NOT_EQUAL(0, inv);
  TEST_ASSERT_EQUAL(0, external_operations);
}

static void present_refresh(lv_display_t *display) {
    static uint8_t pixels[1032];
    lv_area_t area = {0,0,127,63};
    TEST_ASSERT_EQUAL(0,esp_lcd_panel_disp_on_off_fake.call_count);
    OLED_FlushCB(display,&area,pixels);
}
void test_OLED_PresentRequiresSuccessfulNewFrameBeforePanelEnable(void) {
    TEST_ASSERT_EQUAL_PTR(OLED_PresentAndWake,OLED_GetModule()->PresentAndWake);
    setup_transfer(); oledService.display=&fixture_display;
    fixture_display=(lv_display_t){.width=128,.height=64,.user=oledService.panel_handle};
    oledService.state=SERVICE_STATE_STARTED;
    RESET_FAKE(lv_refr_now); RESET_FAKE(esp_lcd_panel_disp_on_off);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE,OLED_PresentAndWake(&oledService.base));
    lv_refr_now_fake.custom_fake=present_refresh;
    execute_result=ESP_ERR_TIMEOUT;
    TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT,OLED_PresentAndWake(&oledService.base));
    TEST_ASSERT_EQUAL(0,esp_lcd_panel_disp_on_off_fake.call_count);
    execute_result=ESP_OK;
    TEST_ASSERT_EQUAL(ESP_OK,OLED_PresentAndWake(&oledService.base));
    TEST_ASSERT_EQUAL(8,esp_lcd_panel_draw_bitmap_fake.call_count);
    TEST_ASSERT_EQUAL(1,esp_lcd_panel_disp_on_off_fake.call_count);
    TEST_ASSERT_TRUE(esp_lcd_panel_disp_on_off_fake.arg1_val);
}

void test_OLED_FrameSnapshotCopiesSuccessfulPayloadAndRejectsPartialFrame(void) {
    setup_transfer();oledService.state=SERVICE_STATE_STARTED;
    lv_display_t display={.width=128,.height=64,.user=oledService.panel_handle};
    lv_area_t area={0,0,127,63};uint8_t pixels[1032]={0};
    pixels[8]=0x80;pixels[8+63*16+15]=1;
    OLED_FlushCB(&display,&area,pixels);
    OLEDFrameSnapshot frame;
    TEST_ASSERT_EQUAL(ESP_OK,OLED_CopyFrame(&oledService.base,&frame));
    TEST_ASSERT_EQUAL(1,frame.pixels[0]);
    TEST_ASSERT_EQUAL(0x80,frame.pixels[1023]);
    TEST_ASSERT_EQUAL(oledService.frame_sequence,frame.sequence);
    OLEDFrameSnapshot before=frame;
    execute_result=ESP_ERR_TIMEOUT;
    OLED_FlushCB(&display,&area,pixels);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE,OLED_CopyFrame(&oledService.base,&frame));
    TEST_ASSERT_EQUAL_MEMORY(&before,&frame,sizeof(frame));
    execute_result=ESP_OK;OLED_FlushCB(&display,&area,pixels);
    mocked_xSemaphoreTake_fake.return_val=pdFALSE;
    TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT,OLED_CopyFrame(&oledService.base,&frame));
    TEST_ASSERT_EQUAL_MEMORY(&before,&frame,sizeof(frame));
    mocked_xSemaphoreTake_fake.return_val=pdTRUE;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,OLED_CopyFrame(NULL,&frame));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,OLED_CopyFrame(&oledService.base,NULL));
}

void test_oled_info_reads_render_diagnostics_only_under_owner_lock(void) {
    oledService.display = &fixture_display;
    oledService.lvgl_initialized = true;
    mocked_xSemaphoreTake_fake.return_val = pdFALSE;
    TEST_ASSERT_EQUAL(1, cmd_oled_info(0, NULL));
    TEST_ASSERT_EQUAL(0, lv_mem_monitor_fake.call_count);
    TEST_ASSERT_EQUAL(0, lv_display_get_render_mode_fake.call_count);
    mocked_xSemaphoreTake_fake.return_val = pdTRUE;
    TEST_ASSERT_EQUAL(0, cmd_oled_info(0, NULL));
    TEST_ASSERT_EQUAL(1, lv_mem_monitor_fake.call_count);
    TEST_ASSERT_EQUAL(1, lv_display_get_render_mode_fake.call_count);
    TEST_ASSERT_EQUAL_PTR(&fixture_display, lv_display_get_render_mode_fake.arg0_val);
}
