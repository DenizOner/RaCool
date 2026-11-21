// FreeRTOS MUST be included before any ESP-IDF header that may pull in task.h
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

// ESP-IDF headers
#include <esp_err.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <esp_console.h>
#include <driver/i2c_master.h>
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_ssd1306.h"

// Project headers
#include "Hardware/I2CManager/II2CManager.h"
#include "Communication/DebugConsole/IDebugConsole.h"
#include "Core/DI/IDIRegistry.h"
#include "Core/ServiceLocator/ServiceLocator.h"
#include "Core/ServiceLocator/ServiceNames.h"
#include "Core/EventBus/IEventBus.h"
#include "Hardware/OLED/IOLED.h"
#include "Hardware/OLED/OLEDConfig.h"
#include "Management/ConfigurationManager/IConfigurationManager.h"
#include "Management/Watchdog/WatchdogWorker.h"

// Standard library
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "lvgl.h"

static const char *TAG = "OLED";

typedef struct {
    IOLED base;
    _Atomic ServiceState state;
    OLEDConfig config;

    esp_lcd_panel_io_handle_t io_handle;
    esp_lcd_panel_handle_t panel_handle;

    lv_display_t *display;
    SemaphoreHandle_t lvgl_mutex;
    WatchdogWorker worker;
    SemaphoreHandle_t control_mutex;
    II2CManager *bus_manager;
    bool bus_acquired;
    bool lvgl_initialized;
    bool console_registered;
    void *lv_buf;
    uint32_t frame_sequence;
    esp_err_t last_frame_result;
    OLEDFrameSnapshot frame;
    bool frame_valid;
} OLEDServiceImpl;

static OLEDServiceImpl oledService;

/* Caller holds the LVGL mutex. External SDK traffic shares the manager's
 * priority-inheriting transfer mutex with sensor reads, not only its bus lease. */
static esp_err_t OLED_Execute(esp_err_t (*operation)(void *), void *context) {
    II2CManager *manager = oledService.bus_manager;
    if (!oledService.bus_acquired || !manager || !manager->ExecuteExternal)
        return ESP_ERR_INVALID_STATE;
    return manager->ExecuteExternal(manager, &oledService, operation, context);
}

typedef struct {
    esp_err_t (*command)(esp_lcd_panel_handle_t, bool);
    bool value;
} OLEDPanelCommand;
static esp_err_t OLED_PanelCommand(void *context) {
    OLEDPanelCommand *command = context;
    return command->command(oledService.panel_handle, command->value);
}
static esp_err_t OLED_RunPanelCommand(esp_err_t (*command)(esp_lcd_panel_handle_t, bool), bool value) {
    OLEDPanelCommand context = {.command = command, .value = value};
    return OLED_Execute(OLED_PanelCommand, &context);
}
static esp_err_t OLED_ContrastCommand(void *context) {
    return esp_lcd_panel_io_tx_param(oledService.io_handle, 0x81, context, 1);
}
typedef struct { const uint8_t *pixels; unsigned page; } OLEDPageTransfer;
static esp_err_t OLED_DrawPage(void *context) {
    const OLEDPageTransfer *transfer = context;
    int first_row = (int)transfer->page * 8;
    return esp_lcd_panel_draw_bitmap(oledService.panel_handle, 0, first_row,
                                    OLED_WIDTH, first_row + 8, transfer->pixels);
}

/**
 * @brief LVGL task for periodic processing
 */
static void oled_lvgl_task(void *pvParameter) {
    while (1) {
        WatchdogWorker_Checkpoint(&oledService.worker, NULL, (uint64_t)(esp_timer_get_time() / 1000));
        uint32_t time_till_next = 5;
        /* portMAX_DELAY is safe here because UIRenderer_Update is now called
         * OUTSIDE the UIController mutex — the nested-lock deadlock that
         * previously existed has been eliminated in UIController.c. */
        if (xSemaphoreTake(oledService.lvgl_mutex, portMAX_DELAY) == pdTRUE) {
            time_till_next = lv_timer_handler();
            xSemaphoreGive(oledService.lvgl_mutex);
        }

        if (time_till_next < 5)   time_till_next = 5;
        if (time_till_next > 500) time_till_next = 500;

        /* At 100 Hz, LVGL's 5 ms minimum rounds down to zero ticks. Yielding
         * without blocking can starve lower-priority I2C users between frames. */
        TickType_t wait_ticks = pdMS_TO_TICKS(time_till_next);
        if (wait_ticks == 0) wait_ticks = 1;
        vTaskDelay(wait_ticks);
    }
}

/**
 * @brief Tick callback for LVGL
 */
static uint32_t OLED_GetTick(void) {
    return pdTICKS_TO_MS(xTaskGetTickCount());
}

// FULL rendering does not require an area-rounding callback.

/**
 * @brief Flush callback for LVGL 9
 * Convert horizontally packed LVGL I1 pixels to vertical SSD1306 pages.
 * FULL rendering supplies the complete frame.
 */
static void OLED_FlushCB(lv_display_t * disp, const lv_area_t * area, uint8_t * px_map) {
    if (!disp) return;
    esp_lcd_panel_handle_t panel_handle = (esp_lcd_panel_handle_t)lv_display_get_user_data(disp);

    // Only a complete, supported frame fits the transfer buffer.
    const int disp_w = lv_display_get_horizontal_resolution(disp);
    const int disp_h = lv_display_get_vertical_resolution(disp);
    if (!panel_handle || !px_map || !area || disp_w != OLED_WIDTH || disp_h != OLED_HEIGHT ||
        area->x1 != 0 || area->y1 != 0 || area->x2 != disp_w - 1 || area->y2 != disp_h - 1) {
        oledService.frame_valid=false;
        ESP_LOGE(TAG, "Rejected invalid full-frame flush");
        lv_display_flush_ready(disp);
        return;
    }

    // Honor LVGL row alignment instead of assuming tightly packed rows.
    const int stride = lv_draw_buf_width_to_stride(disp_w, LV_COLOR_FORMAT_I1);

    // Skip the two-entry LVGL I1 palette (eight bytes).
    const uint8_t *pixel_data = px_map + 8;

    // SSD1306 uses one byte per eight vertical pixels.
    static uint8_t page_buf[OLED_WIDTH * OLED_HEIGHT / 8];
    memset(page_buf, 0, sizeof(page_buf));

    for (int y = 0; y < disp_h; y++) {
        const int page     = y / 8;
        const int bit_pos  = y % 8;
        const int row_base = y * stride;
        const int dst_base = page * disp_w;

        for (int x = 0; x < disp_w; x++) {
            // LVGL I1 rows are packed most-significant bit first.
            if (pixel_data[row_base + (x >> 3)] & (0x80 >> (x & 7))) {
                // The top pixel is the least-significant bit of a page byte.
                page_buf[dst_base + x] |= (1 << bit_pos);
            }
        }
    }

    /* Release the shared bus between native SSD1306 pages. Full-frame SDK
     * transfers have unbounded waits and can exceed sensor admission budgets
     * during startup load; the higher-priority sensor may acquire each boundary.
     * The LVGL mutex retains frame/panel ownership across all pages. */
    esp_err_t result = ESP_OK;
    for (unsigned page = 0; page < OLED_HEIGHT / 8; ++page) {
        OLEDPageTransfer transfer = {.pixels = page_buf + page * OLED_WIDTH, .page = page};
        result = OLED_Execute(OLED_DrawPage, &transfer);
        if (result != ESP_OK) break;
        /* FreeRTOS may retain inherited priority while the LVGL mutex is
         * still held. Rotate equal-priority ready tasks before reacquiring
         * the bus so a waiting thermal reader can claim this boundary. */
        if (page + 1 < OLED_HEIGHT / 8) taskYIELD();
    }
    oledService.last_frame_result = result;
    ++oledService.frame_sequence;
    oledService.frame_valid=result==ESP_OK;
    if (result==ESP_OK) {
        memcpy(oledService.frame.pixels,page_buf,sizeof(page_buf));
        oledService.frame.sequence=oledService.frame_sequence;
        oledService.frame.timestamp_ms=(uint32_t)(esp_timer_get_time()/1000);
    }
    if (result != ESP_OK) ESP_LOGE(TAG, "Frame transfer failed: %s", esp_err_to_name(result));
    lv_display_flush_ready(disp);
}

// --- Console Commands ---

static int cmd_oled_info(int argc, char **argv) {
    (void)argc;
    (void)argv;
    lv_mem_monitor_t memory = {0};
    int render_mode = -1;
    if (!oledService.lvgl_mutex ||
        xSemaphoreTake(oledService.lvgl_mutex, pdMS_TO_TICKS(OLED_CONTROL_TIMEOUT_MS)) != pdTRUE)
        return 1;
    if (oledService.display) render_mode = (int)lv_display_get_render_mode(oledService.display);
    if (oledService.lvgl_initialized) lv_mem_monitor(&memory);
    xSemaphoreGive(oledService.lvgl_mutex);
    printf("\n--- OLED Display Status ---\n");
    printf("State: %d\n", oledService.state);
    printf("Resolution: %dx%d\n", oledService.config.width, oledService.config.height);
    printf("I2C Address: 0x%02X\n", oledService.config.address);
    printf("Render mode: %d; LVGL free: %lu; largest: %lu; used: %u%%\n", render_mode,
           (unsigned long)memory.free_size, (unsigned long)memory.free_biggest_size,
           (unsigned)memory.used_pct);
    printf("---------------------------\n");
    return 0;
}

// --- IOLED Interface Implementation ---

static esp_err_t OLED_CopyFrame(IOLED *self,OLEDFrameSnapshot *output) {
    if (self!=&oledService.base || !output) return ESP_ERR_INVALID_ARG;
    if (!oledService.lvgl_mutex) return ESP_ERR_INVALID_STATE;
    if (xSemaphoreTake(oledService.lvgl_mutex,pdMS_TO_TICKS(OLED_CONTROL_TIMEOUT_MS))!=pdTRUE)
        return ESP_ERR_TIMEOUT;
    esp_err_t result=ESP_ERR_INVALID_STATE;
    if (oledService.state==SERVICE_STATE_STARTED && oledService.frame_valid) {
        *output=oledService.frame;
        result=ESP_OK;
    }
    xSemaphoreGive(oledService.lvgl_mutex);
    return result;
}

static esp_err_t OLED_PresentAndWake(IOLED *self) {
    if (self != &oledService.base) return ESP_ERR_INVALID_ARG;
    if (!oledService.display || !oledService.panel_handle || oledService.state != SERVICE_STATE_STARTED)
        return ESP_ERR_INVALID_STATE;
    uint32_t before = oledService.frame_sequence;
    lv_obj_invalidate(lv_display_get_screen_active(oledService.display));
    lv_refr_now(oledService.display);
    if (oledService.frame_sequence == before) return ESP_ERR_INVALID_STATE;
    if (oledService.last_frame_result != ESP_OK) return oledService.last_frame_result;
    esp_err_t result=OLED_RunPanelCommand(esp_lcd_panel_disp_on_off, true);
    if (result==ESP_OK) oledService.frame.powered=true;
    return result;
}

static esp_err_t OLED_SetPower(IOLED *self, bool on) {
    if (self != &oledService.base) return ESP_ERR_INVALID_ARG;
    if (!oledService.lvgl_mutex) return ESP_ERR_INVALID_STATE;
    if (xSemaphoreTake(oledService.lvgl_mutex, pdMS_TO_TICKS(OLED_CONTROL_TIMEOUT_MS)) != pdTRUE) return ESP_ERR_TIMEOUT;
    esp_err_t result = oledService.panel_handle ?
        OLED_RunPanelCommand(esp_lcd_panel_disp_on_off, on) : ESP_ERR_INVALID_STATE;
    if (result==ESP_OK) oledService.frame.powered=on;
    xSemaphoreGive(oledService.lvgl_mutex);
    return result;
}

static esp_err_t OLED_SetContrast(IOLED *self, uint8_t contrast) {
    if (self != &oledService.base) return ESP_ERR_INVALID_ARG;
    if (!oledService.lvgl_mutex) return ESP_ERR_INVALID_STATE;
    if (xSemaphoreTake(oledService.lvgl_mutex, pdMS_TO_TICKS(OLED_CONTROL_TIMEOUT_MS)) != pdTRUE) return ESP_ERR_TIMEOUT;
    esp_err_t result = oledService.io_handle ?
        OLED_Execute(OLED_ContrastCommand, &contrast) : ESP_ERR_INVALID_STATE;
    if (result == ESP_OK) oledService.config.contrast = contrast;
    xSemaphoreGive(oledService.lvgl_mutex);
    return result;
}

static esp_err_t OLED_InvertDisplay(IOLED *self, bool invert) {
    if (self != &oledService.base) return ESP_ERR_INVALID_ARG;
    if (!oledService.lvgl_mutex) return ESP_ERR_INVALID_STATE;
    if (xSemaphoreTake(oledService.lvgl_mutex, pdMS_TO_TICKS(OLED_CONTROL_TIMEOUT_MS)) != pdTRUE) return ESP_ERR_TIMEOUT;
    esp_err_t result = oledService.panel_handle ?
        OLED_RunPanelCommand(esp_lcd_panel_invert_color, invert) : ESP_ERR_INVALID_STATE;
    if (result==ESP_OK) oledService.frame.inverted=invert;
    xSemaphoreGive(oledService.lvgl_mutex);
    return result;
}

static bool OLED_Lock(IOLED *self) {
    return self == &oledService.base && oledService.lvgl_mutex &&
        xSemaphoreTake(oledService.lvgl_mutex, pdMS_TO_TICKS(OLED_CONTROL_TIMEOUT_MS)) == pdTRUE;
}

static void OLED_Unlock(IOLED *self) {
    (void)self;
    if (oledService.lvgl_mutex) {
        xSemaphoreGive(oledService.lvgl_mutex);
    }
}

/* The LVGL display, buffer and mutex survive Stop/Start because UIController
 * owns screen pointers in that display. Only panel/IO/bus resources are detached.
 * Lock order: lifecycle control -> LVGL -> I2C manager. The worker never takes
 * lifecycle control and acknowledges Stop outside the LVGL mutex. */
static esp_err_t OLED_DeletePanel(void *context) {
    (void)context;
    esp_err_t result;
    if (oledService.panel_handle) {
        result = esp_lcd_panel_del(oledService.panel_handle);
        if (result != ESP_OK) return result;
        oledService.panel_handle = NULL;
    }
    if (oledService.io_handle) {
        result = esp_lcd_panel_io_del(oledService.io_handle);
        if (result != ESP_OK) return result;
        oledService.io_handle = NULL;
    }
    return ESP_OK;
}
static esp_err_t OLED_DetachPanelLocked(void) {
    if (oledService.display) lv_display_set_user_data(oledService.display, NULL);
    esp_err_t result;
    if (oledService.panel_handle || oledService.io_handle) {
        result = OLED_Execute(OLED_DeletePanel, NULL);
        if (result != ESP_OK) return result;
    }
    if (oledService.bus_acquired) {
        result = oledService.bus_manager->ReleaseBus(oledService.bus_manager, &oledService);
        if (result != ESP_OK) return result;
        oledService.bus_acquired = false;
        oledService.bus_manager = NULL;
    }
    return ESP_OK;
}

static esp_err_t OLED_CreatePanel(void *context) {
    i2c_master_bus_handle_t bus = context;
    esp_err_t error;
    const esp_lcd_panel_io_i2c_config_t io_config = {
        .dev_addr = oledService.config.address, .scl_speed_hz = OLED_I2C_CLOCK_HZ,
        .control_phase_bytes = 1, .lcd_cmd_bits = 8, .lcd_param_bits = 8, .dc_bit_offset = 6,
    };
    error = esp_lcd_new_panel_io_i2c(bus, &io_config, &oledService.io_handle);
    if (error != ESP_OK) return error;
    if (!oledService.io_handle) { error = ESP_ERR_INVALID_STATE; return error; }
    esp_lcd_panel_ssd1306_config_t ssd1306 = {.height = oledService.config.height};
    const esp_lcd_panel_dev_config_t panel_config = {
        .bits_per_pixel = 1, .reset_gpio_num = -1, .vendor_config = &ssd1306,
    };
    error = esp_lcd_new_panel_ssd1306(oledService.io_handle, &panel_config, &oledService.panel_handle);
    if (error != ESP_OK) return error;
    if (!oledService.panel_handle) { error = ESP_ERR_INVALID_STATE; return error; }
    error = esp_lcd_panel_reset(oledService.panel_handle);
    if (error != ESP_OK) return error;
    oledService.frame_valid=false;
    oledService.frame.inverted=false;
    oledService.frame.flipped=oledService.config.flip_screen;
    error = esp_lcd_panel_init(oledService.panel_handle);
    if (error != ESP_OK) return error;
    if (oledService.config.flip_screen) {
        error = esp_lcd_panel_mirror(oledService.panel_handle, true, true);
        if (error != ESP_OK) return error;
    }
    error = esp_lcd_panel_disp_on_off(oledService.panel_handle, true);
    if (error != ESP_OK) return error;
    oledService.frame.powered=true;
    error = esp_lcd_panel_io_tx_param(oledService.io_handle, 0x81, &oledService.config.contrast, 1);
    if (error != ESP_OK) return error;
    return ESP_OK;
}

static int32_t OLED_Start(IService *self) {
    if (self != &oledService.base.base || !oledService.control_mutex || !oledService.lvgl_mutex)
        return -1;
    if (xSemaphoreTake(oledService.control_mutex, pdMS_TO_TICKS(OLED_CONTROL_TIMEOUT_MS)) != pdTRUE)
        return -1;
    int32_t result = -1;
    ServiceState previous_state = oledService.state;
    if (oledService.state == SERVICE_STATE_STARTED) { result = 0; goto done; }
    if (oledService.state != SERVICE_STATE_INITIALIZED && oledService.state != SERVICE_STATE_STOPPED)
        goto done;
    if (xSemaphoreTake(oledService.lvgl_mutex, pdMS_TO_TICKS(OLED_CONTROL_TIMEOUT_MS)) != pdTRUE)
        goto done;
    II2CManager *manager = (II2CManager *)ServiceLocator_GetService(SVC_NAME_I2C_MANAGER);
    i2c_master_bus_handle_t bus = NULL;
    esp_err_t error = ESP_ERR_INVALID_STATE;
    if (!manager || !manager->AcquireBus || !manager->ReleaseBus || !manager->ExecuteExternal) goto failed;
    /* Apply persisted contrast from NVS (overrides default). */
    {
        IConfigurationManager *cfgMgr = ConfigurationManager_GetInstance();
        SystemSettings cfgSnap;
        if (cfgMgr && cfgMgr->GetSnapshot &&
            cfgMgr->GetSnapshot(cfgMgr, &cfgSnap) == ESP_OK) {
            oledService.config.contrast = cfgSnap.hardware.oled_contrast;
        }
    }
    error = manager->AcquireBus(manager, &oledService, &bus);
    if (error != ESP_OK) goto failed;
    oledService.bus_manager = manager;
    oledService.bus_acquired = true;
    if (!bus) { error = ESP_ERR_INVALID_STATE; goto failed; }
    error = OLED_Execute(OLED_CreatePanel, bus);
    if (error != ESP_OK) goto failed;
    if (!oledService.lvgl_initialized) {
        lv_init();
        lv_tick_set_cb(OLED_GetTick);
        oledService.lvgl_initialized = true;
    }
    if (!oledService.display) {
        uint32_t size = LV_DRAW_BUF_SIZE(oledService.config.width, oledService.config.height, LV_COLOR_FORMAT_I1);
        oledService.lv_buf = malloc(size);
        if (!oledService.lv_buf) { error = ESP_ERR_NO_MEM; goto failed; }
        oledService.display = lv_display_create(oledService.config.width, oledService.config.height);
        if (!oledService.display) {
            free(oledService.lv_buf);
            oledService.lv_buf = NULL;
            error = ESP_ERR_NO_MEM;
            goto failed;
        }
        lv_display_set_color_format(oledService.display, LV_COLOR_FORMAT_I1);
        lv_display_set_buffers(oledService.display, oledService.lv_buf, NULL, size, LV_DISPLAY_RENDER_MODE_FULL);
        lv_display_set_flush_cb(oledService.display, OLED_FlushCB);
    }
    lv_display_set_user_data(oledService.display, oledService.panel_handle);
    if (WatchdogWorker_Start(&oledService.worker, oled_lvgl_task, "OLED_LVGL",
                            OLED_TASK_STACK_SIZE, NULL, OLED_TASK_PRIORITY) != 0) {
        error = ESP_FAIL;
        if (atomic_load(&oledService.worker.task)) {
            oledService.state = SERVICE_STATE_ERROR;
            xSemaphoreGive(oledService.lvgl_mutex);
            goto done;
        }
        goto failed;
    }
    oledService.state = SERVICE_STATE_STARTED;
    xSemaphoreGive(oledService.lvgl_mutex);
    if (!oledService.console_registered) {
        IDebugConsole *console = (IDebugConsole *)ServiceLocator_GetService(SVC_NAME_DEBUG_CONSOLE);
        if (console && console->RegisterCommand) {
            static const esp_console_cmd_t command = {
                .command = "oled_info", .help = "Show OLED status", .func = cmd_oled_info,
            };
            esp_err_t registered = console->RegisterCommand(console, &command);
            oledService.console_registered = registered == ESP_OK;
            if (registered != ESP_OK) ESP_LOGW(TAG, "Console registration failed: %s", esp_err_to_name(registered));
        }
    }
    result = 0;
    goto done;
failed:
    ESP_LOGE(TAG, "OLED start failed: %s", esp_err_to_name(error));
    oledService.state = OLED_DetachPanelLocked() == ESP_OK ? previous_state : SERVICE_STATE_ERROR;
    xSemaphoreGive(oledService.lvgl_mutex);
done:
    xSemaphoreGive(oledService.control_mutex);
    return result;
}

static int32_t OLED_Stop(IService *self) {
    if (self != &oledService.base.base || !oledService.control_mutex || !oledService.lvgl_mutex)
        return -1;
    if (xSemaphoreTake(oledService.control_mutex, pdMS_TO_TICKS(OLED_CONTROL_TIMEOUT_MS)) != pdTRUE)
        return -1;
    int32_t result = -1;
    oledService.state = SERVICE_STATE_ERROR; /* Reject rendering until Stop succeeds. */
    if (WatchdogWorker_Stop(&oledService.worker, pdMS_TO_TICKS(OLED_CONTROL_TIMEOUT_MS)) != 0)
        goto done;
    if (xSemaphoreTake(oledService.lvgl_mutex, pdMS_TO_TICKS(OLED_CONTROL_TIMEOUT_MS)) != pdTRUE)
        goto done;
    esp_err_t error = ESP_OK;
    if (oledService.panel_handle) error = OLED_RunPanelCommand(esp_lcd_panel_disp_on_off, false);
    if (error == ESP_OK) error = OLED_DetachPanelLocked();
    xSemaphoreGive(oledService.lvgl_mutex);
    if (error == ESP_OK) { oledService.state = SERVICE_STATE_STOPPED; result = 0; }
    else ESP_LOGE(TAG, "OLED stop failed: %s", esp_err_to_name(error));
done:
    xSemaphoreGive(oledService.control_mutex);
    return result;
}

static ServiceState OLED_GetState(IService *self) { return oledService.state; }

static int32_t OLED_CheckHealth(IService *self) {
    return (oledService.state == SERVICE_STATE_STARTED) ? 0 : -1;
}

static int32_t OLED_Base_Initialize(IService *self, IDIRegistry *registry) {
    (void)registry;
    if (self != &oledService.base.base) return -1;
    /* Initial service publication is serialized by the service manager. */
    if (!oledService.control_mutex) oledService.control_mutex = xSemaphoreCreateMutex();
    if (!oledService.control_mutex) return -1;
    if (xSemaphoreTake(oledService.control_mutex, pdMS_TO_TICKS(OLED_CONTROL_TIMEOUT_MS)) != pdTRUE) return -1;
    int32_t result = -1;
    if (oledService.state != SERVICE_STATE_UNINITIALIZED) {
        result = oledService.state == SERVICE_STATE_ERROR ? -1 : 0;
        goto done;
    }
    if (!oledService.lvgl_mutex) oledService.lvgl_mutex = xSemaphoreCreateMutex();
    if (!oledService.lvgl_mutex || WatchdogWorker_Init(&oledService.worker) != 0) goto done;
    oledService.config = OLED_GetDefaultConfig();
    oledService.state = SERVICE_STATE_INITIALIZED;
    result = 0;
done:
    xSemaphoreGive(oledService.control_mutex);
    return result;
}

static bool OLED_Initialize(IOLED *self, OLEDConfig config) {
    if (self != &oledService.base || config.width != OLED_WIDTH || config.height != OLED_HEIGHT ||
        config.address < 0x08 || config.address > 0x77) return false;
    bool locked = oledService.control_mutex != NULL;
    if (locked && xSemaphoreTake(oledService.control_mutex, pdMS_TO_TICKS(OLED_CONTROL_TIMEOUT_MS)) != pdTRUE)
        return false;
    ServiceState state = oledService.state;
    bool allowed = state == SERVICE_STATE_UNINITIALIZED || state == SERVICE_STATE_INITIALIZED || state == SERVICE_STATE_STOPPED;
    if (allowed) oledService.config = config;
    if (locked) xSemaphoreGive(oledService.control_mutex);
    return allowed;
}

IOLED *OLED_GetModule(void) {
    static bool initialized = false;
    if (!initialized) {
        oledService.base.base.Initialize = OLED_Base_Initialize;
        oledService.base.base.Start = OLED_Start;
        oledService.base.base.Stop = OLED_Stop;
        oledService.base.base.GetState = OLED_GetState;
        oledService.base.base.CheckHealth = OLED_CheckHealth;
        
        oledService.base.Initialize = OLED_Initialize;
        oledService.base.SetPower = OLED_SetPower;
        oledService.base.PresentAndWake = OLED_PresentAndWake;
        oledService.base.SetContrast = OLED_SetContrast;
        oledService.base.InvertDisplay = OLED_InvertDisplay;
        oledService.base.CopyFrame = OLED_CopyFrame;
        oledService.base.Lock = OLED_Lock;
        oledService.base.Unlock = OLED_Unlock;
        
        oledService.state = SERVICE_STATE_UNINITIALIZED;
        initialized = true;
    }
    return &oledService.base;
}
