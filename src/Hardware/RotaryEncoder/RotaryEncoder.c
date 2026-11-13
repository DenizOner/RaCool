/**
 * @file RotaryEncoder.c
 * @brief KY-040 Rotary Encoder service — core lifecycle and event dispatch.
 *
 * Manages the service lifecycle (Init / Start / Stop), LVGL encoder
 * input-device integration, direct callback subscription, and the
 * main event processing task.  Rotation detection is fully interrupt-
 * driven via PCNT watch-point callbacks; no polling is required.
 */

#include <driver/gpio.h>
#include <esp_err.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <limits.h>
#include <string.h>
#include <driver/pulse_cnt.h>

#include "Communication/DebugConsole/IDebugConsole.h"
#include "Management/ConfigurationManager/IConfigurationManager.h"
#include "Core/DI/IDIRegistry.h"
#include "Core/ServiceLocator/ServiceLocator.h"
#include "Core/ServiceLocator/ServiceNames.h"
#include "Hardware/PinManager/IPinManager.h"
#include "Hardware/OLED/IOLED.h"
#include "Hardware/RotaryEncoder/RotaryEncoderEvents.h"
#include "Hardware/RotaryEncoder/RotaryEncoderInternal.h"
#include "lvgl.h"

#define TAG "RotaryEncoder"

/* ── Global Instance ─────────────────────────────────────────────────── */

RotaryEncoderServiceImpl rotaryEncoderService = {.spinlock = portMUX_INITIALIZER_UNLOCKED};

/* ── Forward Declarations ────────────────────────────────────────────── */

static void RotaryEncoder_Task(void *arg);
static int32_t RotaryEncoder_StopOwned(IService *self);
extern esp_err_t register_encoder_commands(IDebugConsole *console);
static int32_t RotaryEncoder_GetAbsolutePosition(IRotaryEncoder *self);
static bool RotaryEncoder_IsButtonHeld(IRotaryEncoder *self);

/* ── Critical Section Helpers ────────────────────────────────────────── */

static void RotaryEncoder_EnterCritical(void) {
    portENTER_CRITICAL(&rotaryEncoderService.spinlock);
}

static void RotaryEncoder_ExitCritical(void) {
    portEXIT_CRITICAL(&rotaryEncoderService.spinlock);
}

/* ── Default Configuration ───────────────────────────────────────────── */

static RotaryEncoderRuntimeConfig RotaryEncoder_GetDefaultRuntimeConfig(void) {
    RotaryEncoderRuntimeConfig config = {
        .invert_direction    = RE_DIRECTION_INVERTED,
        .steps_per_detent    = RE_PCNT_STEPS_PER_DETENT,
        .accel_enabled       = RE_ACCEL_ENABLED,
        .accel_threshold_ms  = RE_ACCEL_THRESHOLD_MS,
        .debounce_rotation_ms = 0,
        .debounce_button_ms  = RE_DEBOUNCE_BUTTON_MS,
        .long_press_ms       = RE_LONG_PRESS_MS,
        .double_click_ms     = RE_DOUBLE_CLICK_WINDOW_MS,
        .held_repeat_ms      = RE_HELD_REPEAT_MS,
    };
    return config;
}

uint8_t RotaryEncoder_ClampStepsPerDetent(uint8_t steps) {
    return (steps == 0) ? RE_PCNT_STEPS_PER_DETENT : steps;
}

/* ── Status & Diagnostics Accessors ──────────────────────────────────── */

static RotaryEncoderStatus RotaryEncoder_GetStatus(IRotaryEncoder *self) {
    (void)self;
    RotaryEncoderStatus status;
    RotaryEncoder_EnterCritical();
    status = rotaryEncoderService.status;
    RotaryEncoder_ExitCritical();
    return status;
}

static esp_err_t RotaryEncoder_GetDiagnostics(IRotaryEncoder *self,
                                               RotaryEncoderDiagnostics *diagnostics) {
    if (!diagnostics) {
        return ESP_ERR_INVALID_ARG;
    }
    RotaryEncoder_EnterCritical();
    *diagnostics = rotaryEncoderService.diagnostics;
    RotaryEncoder_ExitCritical();
    return ESP_OK;
}

static esp_err_t RotaryEncoder_GetRuntimeConfig(IRotaryEncoder *self,
                                                 RotaryEncoderRuntimeConfig *config) {
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }
    RotaryEncoder_EnterCritical();
    *config = rotaryEncoderService.runtime_config;
    RotaryEncoder_ExitCritical();
    return ESP_OK;
}

/* ── LVGL Input Device Callback ──────────────────────────────────────── */

static void RotaryEncoder_ReadCB(lv_indev_t *indev, lv_indev_data_t *data) {
    (void)indev;
    RotaryEncoder_EnterCritical();
    RotaryEncoderStatus status = rotaryEncoderService.status;
    if (atomic_load(&rotaryEncoderService.closing)) {
        rotaryEncoderService.lv_last_position = status.absolute_position;
        data->enc_diff = 0;
        data->state = LV_INDEV_STATE_RELEASED;
        RotaryEncoder_ExitCritical();
        return;
    }
    int64_t difference = (int64_t)status.absolute_position - rotaryEncoderService.lv_last_position;
    int16_t applied = difference > INT16_MAX ? INT16_MAX :
                      difference < INT16_MIN ? INT16_MIN : (int16_t)difference;
    data->enc_diff = applied;
    rotaryEncoderService.lv_last_position = (int32_t)((int64_t)rotaryEncoderService.lv_last_position + applied);
    data->state = status.button_held ? LV_INDEV_STATE_PRESSED
                                     : LV_INDEV_STATE_RELEASED;
    RotaryEncoder_ExitCritical();
}

/* ── Counter & Queue Reset ───────────────────────────────────────────── */

void RotaryEncoder_ResetCounters(void) {
    RotaryEncoder_EnterCritical();
    memset(&rotaryEncoderService.status, 0, sizeof(rotaryEncoderService.status));
    memset(&rotaryEncoderService.diagnostics, 0, sizeof(rotaryEncoderService.diagnostics));
    rotaryEncoderService.pcnt_pending_detents     = 0;
    rotaryEncoderService.lv_last_position = 0;
    rotaryEncoderService.button_has_time = false;
    rotaryEncoderService.pcnt_has_fire_time = false;
    rotaryEncoderService.pcnt_last_fire_time_us   = 0;
    rotaryEncoderService.pcnt_last_fire_direction = 0;
    RotaryEncoder_ExitCritical();

    if (rotaryEncoderService.eventQueue) {
        RotaryEncoderEvent event;
        uint32_t drained = 0;
        while (drained < RE_EVENT_QUEUE_SIZE &&
               xQueueReceive(rotaryEncoderService.eventQueue, &event, 0) == pdTRUE) {
            drained++;
        }
    }
    if (rotaryEncoderService.rawButtonQueue) {
        RotaryEncoderRawEvent raw_event;
        uint32_t drained = 0;
        while (drained < RE_EVENT_QUEUE_SIZE &&
               xQueueReceive(rotaryEncoderService.rawButtonQueue, &raw_event, 0) == pdTRUE) {
            drained++;
        }
    }
    RotaryEncoder_ResetGestureState();
}

/* ── Callback Registration ───────────────────────────────────────────── */

static esp_err_t RotaryEncoder_OnRotation(IRotaryEncoder *self,
                                           RotaryEncoderRotationCallback callback,
                                           void *ctx) {
    (void)self;
    if (!callback) {
        return ESP_ERR_INVALID_ARG;
    }
    RotaryEncoder_EnterCritical();
    if (atomic_load(&rotaryEncoderService.closing)) {
        RotaryEncoder_ExitCritical();
        return ESP_ERR_INVALID_STATE;
    }
    for (int i = 0; i < RE_MAX_CALLBACK_SUBSCRIBERS; i++) {
        if (rotaryEncoderService.rotation_slots[i].used &&
            rotaryEncoderService.rotation_slots[i].callback == callback) {
            bool identical = rotaryEncoderService.rotation_slots[i].ctx == ctx;
            RotaryEncoder_ExitCritical();
            return identical ? ESP_OK : ESP_ERR_INVALID_STATE;
        }
    }
    for (int i = 0; i < RE_MAX_CALLBACK_SUBSCRIBERS; i++) {
        if (!rotaryEncoderService.rotation_slots[i].used) {
            rotaryEncoderService.rotation_slots[i].callback = callback;
            rotaryEncoderService.rotation_slots[i].ctx = ctx;
            rotaryEncoderService.rotation_slots[i].used = true;
            RotaryEncoder_ExitCritical();
            return ESP_OK;
        }
    }
    RotaryEncoder_ExitCritical();
    return ESP_ERR_NO_MEM;
}

static esp_err_t RotaryEncoder_OnButton(IRotaryEncoder *self,
                                         RotaryEncoderButtonCallback callback,
                                         void *ctx) {
    (void)self;
    if (!callback) {
        return ESP_ERR_INVALID_ARG;
    }
    RotaryEncoder_EnterCritical();
    if (atomic_load(&rotaryEncoderService.closing)) {
        RotaryEncoder_ExitCritical();
        return ESP_ERR_INVALID_STATE;
    }
    for (int i = 0; i < RE_MAX_CALLBACK_SUBSCRIBERS; i++) {
        if (rotaryEncoderService.button_slots[i].used &&
            rotaryEncoderService.button_slots[i].callback == callback) {
            bool identical = rotaryEncoderService.button_slots[i].ctx == ctx;
            RotaryEncoder_ExitCritical();
            return identical ? ESP_OK : ESP_ERR_INVALID_STATE;
        }
    }
    for (int i = 0; i < RE_MAX_CALLBACK_SUBSCRIBERS; i++) {
        if (!rotaryEncoderService.button_slots[i].used) {
            rotaryEncoderService.button_slots[i].callback = callback;
            rotaryEncoderService.button_slots[i].ctx = ctx;
            rotaryEncoderService.button_slots[i].used = true;
            RotaryEncoder_ExitCritical();
            return ESP_OK;
        }
    }
    RotaryEncoder_ExitCritical();
    return ESP_ERR_NO_MEM;
}

static esp_err_t RotaryEncoder_UnsubscribeRotation(IRotaryEncoder *self,
                                                    RotaryEncoderRotationCallback callback) {
    (void)self;
    if (!callback) {
        return ESP_ERR_INVALID_ARG;
    }
    RotaryEncoder_EnterCritical();
    for (int i = 0; i < RE_MAX_CALLBACK_SUBSCRIBERS; i++) {
        if (rotaryEncoderService.rotation_slots[i].used &&
            rotaryEncoderService.rotation_slots[i].callback == callback) {
            if (rotaryEncoderService.rotation_slots[i].in_flight != 0) {
                RotaryEncoder_ExitCritical();
                return ESP_ERR_INVALID_STATE;
            }
            rotaryEncoderService.rotation_slots[i].used = false;
            rotaryEncoderService.rotation_slots[i].callback = NULL;
            rotaryEncoderService.rotation_slots[i].ctx = NULL;
            RotaryEncoder_ExitCritical();
            return ESP_OK;
        }
    }
    RotaryEncoder_ExitCritical();
    return ESP_ERR_NOT_FOUND;
}

static esp_err_t RotaryEncoder_UnsubscribeButton(IRotaryEncoder *self,
                                                  RotaryEncoderButtonCallback callback) {
    (void)self;
    if (!callback) {
        return ESP_ERR_INVALID_ARG;
    }
    RotaryEncoder_EnterCritical();
    for (int i = 0; i < RE_MAX_CALLBACK_SUBSCRIBERS; i++) {
        if (rotaryEncoderService.button_slots[i].used &&
            rotaryEncoderService.button_slots[i].callback == callback) {
            if (rotaryEncoderService.button_slots[i].in_flight != 0) {
                RotaryEncoder_ExitCritical();
                return ESP_ERR_INVALID_STATE;
            }
            rotaryEncoderService.button_slots[i].used = false;
            rotaryEncoderService.button_slots[i].callback = NULL;
            rotaryEncoderService.button_slots[i].ctx = NULL;
            RotaryEncoder_ExitCritical();
            return ESP_OK;
        }
    }
    RotaryEncoder_ExitCritical();
    return ESP_ERR_NOT_FOUND;
}

/* ── Position & Button Accessors ─────────────────────────────────────── */

static int32_t RotaryEncoder_GetAbsolutePosition(IRotaryEncoder *self) {
    (void)self;
    int32_t position;
    RotaryEncoder_EnterCritical();
    position = rotaryEncoderService.status.absolute_position;
    RotaryEncoder_ExitCritical();
    return position;
}

static bool RotaryEncoder_IsButtonHeld(IRotaryEncoder *self) {
    (void)self;
    bool held;
    RotaryEncoder_EnterCritical();
    held = rotaryEncoderService.status.button_held;
    RotaryEncoder_ExitCritical();
    return held;
}

/* ── Runtime Configuration Setters ───────────────────────────────────── */

static esp_err_t RotaryEncoder_SetAcceleration(IRotaryEncoder *self, bool enabled,
                                                uint8_t threshold_ms) {
    (void)self;
    RotaryEncoder_EnterCritical();
    rotaryEncoderService.runtime_config.accel_enabled = enabled;
    rotaryEncoderService.runtime_config.accel_threshold_ms = threshold_ms;
    RotaryEncoder_ExitCritical();
    return ESP_OK;
}

static esp_err_t RotaryEncoder_SetDebounceMs(IRotaryEncoder *self, uint16_t rotation_ms,
                                              uint16_t button_ms) {
    (void)self;
    RotaryEncoder_EnterCritical();
    rotaryEncoderService.runtime_config.debounce_rotation_ms = rotation_ms;
    rotaryEncoderService.runtime_config.debounce_button_ms = button_ms;
    RotaryEncoder_ExitCritical();
    return ESP_OK;
}

static esp_err_t RotaryEncoder_SetLongPressMs(IRotaryEncoder *self, uint16_t duration_ms) {
    (void)self;
    RotaryEncoder_EnterCritical();
    rotaryEncoderService.runtime_config.long_press_ms = duration_ms;
    RotaryEncoder_ExitCritical();
    return ESP_OK;
}

static esp_err_t RotaryEncoder_SetDoubleClickMs(IRotaryEncoder *self, uint16_t window_ms) {
    (void)self;
    RotaryEncoder_EnterCritical();
    rotaryEncoderService.runtime_config.double_click_ms = window_ms;
    RotaryEncoder_ExitCritical();
    return ESP_OK;
}

static esp_err_t RotaryEncoder_SetHeldRepeatMs(IRotaryEncoder *self, uint16_t interval_ms) {
    (void)self;
    RotaryEncoder_EnterCritical();
    rotaryEncoderService.runtime_config.held_repeat_ms = interval_ms;
    RotaryEncoder_ExitCritical();
    return ESP_OK;
}

static esp_err_t RotaryEncoder_SetDirectionInverted(IRotaryEncoder *self, bool inverted) {
    (void)self;
    RotaryEncoder_EnterCritical();
    rotaryEncoderService.runtime_config.invert_direction = inverted;
    RotaryEncoder_ExitCritical();
    return ESP_OK;
}

static esp_err_t RotaryEncoder_RequestControl(bool reconfigure, uint8_t steps,
                                              RotaryEncoderSimulation simulation, uint16_t count);

static esp_err_t RotaryEncoder_SetStepsPerDetent(IRotaryEncoder *self, uint8_t steps) {
    (void)self;
    return RotaryEncoder_RequestControl(true, RotaryEncoder_ClampStepsPerDetent(steps), RE_SIM_NONE, 0);
}

static esp_err_t RotaryEncoder_PerformSimulation(void) {
    RotaryEncoderSimulation simulation = rotaryEncoderService.control_simulation;
    if (simulation == RE_SIM_CW || simulation == RE_SIM_CCW) {
        RotaryEncoderStatus before = RotaryEncoder_GetStatus(NULL);
        if (before.dropped_rotation_events == UINT32_MAX) return ESP_FAIL;
        int32_t count = rotaryEncoderService.control_count;
        RotaryEncoder_ProcessRotationDetents(simulation == RE_SIM_CW ? count : -count);
        return RotaryEncoder_GetStatus(NULL).dropped_rotation_events == before.dropped_rotation_events ? ESP_OK : ESP_FAIL;
    }
    /* Semantic button notifications do not change real held/gesture state. */
    const ButtonGesture gestures[] = {GESTURE_SINGLE_CLICK, GESTURE_DOUBLE_CLICK,
        GESTURE_LONG_PRESS, GESTURE_HELD, GESTURE_RELEASED};
    RotaryEncoderEvent event = {.type = ROTARY_ENCODER_EVENT_BUTTON};
    event.payload.button.gesture = gestures[simulation - RE_SIM_CLICK];
    event.payload.button.click_count = simulation == RE_SIM_CLICK ? 1 : simulation == RE_SIM_DOUBLE_CLICK ? 2 : 0;
    return rotaryEncoderService.eventQueue && xQueueSend(rotaryEncoderService.eventQueue, &event, 0) == pdPASS ? ESP_OK : ESP_FAIL;
}

esp_err_t RotaryEncoder_Simulate(RotaryEncoderSimulation simulation, uint16_t count) {
    if (simulation < RE_SIM_CW || simulation > RE_SIM_RELEASE || !count || count > INT16_MAX ||
        (simulation >= RE_SIM_CLICK && count != 1)) return ESP_ERR_INVALID_ARG;
    return RotaryEncoder_RequestControl(false, 0, simulation, count);
}

static void RotaryEncoder_FinishReset(esp_err_t result) {
    RotaryEncoder_EnterCritical();
    rotaryEncoderService.control_active = false;
    atomic_store(&rotaryEncoderService.reset_result, result);
    atomic_store(&rotaryEncoderService.reset_done, true);
    RotaryEncoder_ExitCritical();
}

static void RotaryEncoder_PerformReset(void) {
    if (!atomic_exchange(&rotaryEncoderService.reset_requested, false)) return;
    esp_err_t result;
    if (rotaryEncoderService.control_simulation != RE_SIM_NONE) {
        result = RotaryEncoder_PerformSimulation();
    } else if (rotaryEncoderService.control_reconfigure) {
        result = RotaryEncoder_ReconfigurePCNT(rotaryEncoderService.control_steps);
    } else {
        result = rotaryEncoderService.pcnt_unit ?
            pcnt_unit_clear_count(rotaryEncoderService.pcnt_unit) : ESP_OK;
        if (result == ESP_OK) RotaryEncoder_ResetCounters();
    }
    RotaryEncoder_FinishReset(result);
}

static esp_err_t RotaryEncoder_RequestControl(bool reconfigure, uint8_t steps,
                                              RotaryEncoderSimulation simulation, uint16_t count) {
    if (atomic_exchange(&rotaryEncoderService.lifecycle_busy, true)) return ESP_ERR_INVALID_STATE;
    RotaryEncoder_EnterCritical();
    if (atomic_load(&rotaryEncoderService.closing) || rotaryEncoderService.control_active ||
        rotaryEncoderService.dispatches || rotaryEncoderService.state == SERVICE_STATE_UNINITIALIZED ||
        (simulation != RE_SIM_NONE && (rotaryEncoderService.state != SERVICE_STATE_STARTED || !rotaryEncoderService.taskHandle)) ||
        (rotaryEncoderService.taskHandle && rotaryEncoderService.taskHandle == xTaskGetCurrentTaskHandle())) {
        RotaryEncoder_ExitCritical();
        atomic_store(&rotaryEncoderService.lifecycle_busy, false);
        return ESP_ERR_INVALID_STATE;
    }
    rotaryEncoderService.control_active = true;
    rotaryEncoderService.control_reconfigure = reconfigure;
    rotaryEncoderService.control_steps = steps;
    rotaryEncoderService.control_simulation = simulation;
    rotaryEncoderService.control_count = count;
    atomic_store(&rotaryEncoderService.reset_done, false);
    atomic_store(&rotaryEncoderService.reset_requested, true);
    TaskHandle_t worker = rotaryEncoderService.taskHandle;
    RotaryEncoder_ExitCritical();
    esp_err_t result;
    if (!worker) {
        RotaryEncoder_PerformReset();
        result = atomic_load(&rotaryEncoderService.reset_result);
    } else {
        BaseType_t notified = xTaskNotify(worker, RE_NOTIFY_BUTTON_BIT, eSetBits);
        if (notified != pdPASS) {
            if (atomic_exchange(&rotaryEncoderService.reset_requested, false)) RotaryEncoder_FinishReset(ESP_FAIL);
            result = ESP_FAIL;
        } else {
            TickType_t started = xTaskGetTickCount();
            TickType_t timeout = pdMS_TO_TICKS(RE_CONTROL_TIMEOUT_MS);
            if (!timeout) timeout = 1;
            while (!atomic_load(&rotaryEncoderService.reset_done) &&
                   (TickType_t)(xTaskGetTickCount() - started) < timeout) vTaskDelay(1);
            result = atomic_load(&rotaryEncoderService.reset_done) ?
                     atomic_load(&rotaryEncoderService.reset_result) : ESP_ERR_TIMEOUT;
        }
    }
    atomic_store(&rotaryEncoderService.lifecycle_busy, false);
    return result;
}

static esp_err_t RotaryEncoder_Reset(IRotaryEncoder *self) {
    (void)self;
    return RotaryEncoder_RequestControl(false, 0, RE_SIM_NONE, 0);
}

/* ── Event Queue & Dispatch ──────────────────────────────────────────── */

void RotaryEncoder_EnqueueEvent(RotaryEncoderEvent *event) {
    if (!event || atomic_load(&rotaryEncoderService.closing)) return;
    if (!rotaryEncoderService.eventQueue ||
        xQueueSend(rotaryEncoderService.eventQueue, event, 0) != pdPASS) {
        RotaryEncoder_EnterCritical();
        if (event->type == ROTARY_ENCODER_EVENT_ROTATION) {
            rotaryEncoderService.status.dropped_rotation_events++;
        } else {
            rotaryEncoderService.status.dropped_button_events++;
        }
        rotaryEncoderService.diagnostics.queue_overflows++;
        RotaryEncoder_ExitCritical();
    }
}

void RotaryEncoder_DispatchRotation(RotaryEncoderRotationEvent event) {
    RotaryEncoderRotationSlot captured[RE_MAX_CALLBACK_SUBSCRIBERS] = {0};
    RotaryEncoder_EnterCritical();
    if (atomic_load(&rotaryEncoderService.closing) || rotaryEncoderService.dispatches == UINT32_MAX) {
        RotaryEncoder_ExitCritical();
        return;
    }
    for (int i = 0; i < RE_MAX_CALLBACK_SUBSCRIBERS; i++) {
        if (rotaryEncoderService.rotation_slots[i].used &&
            rotaryEncoderService.rotation_slots[i].in_flight == UINT32_MAX) {
            RotaryEncoder_ExitCritical();
            return;
        }
    }
    for (int i = 0; i < RE_MAX_CALLBACK_SUBSCRIBERS; i++) {
        if (rotaryEncoderService.rotation_slots[i].used) {
            captured[i] = rotaryEncoderService.rotation_slots[i];
            rotaryEncoderService.rotation_slots[i].in_flight++;
        }
    }
    rotaryEncoderService.dispatches++;
    RotaryEncoder_ExitCritical();
    BusEvent bus_event = {
        .eventName = RE_EVENT_ROTATED,
        .type      = EVENT_TYPE_INFO,
        .data      = &event,
        .dataSize  = sizeof(event),
    };

    if (rotaryEncoderService.eventBus && rotaryEncoderService.eventBus->Publish) {
        rotaryEncoderService.eventBus->Publish(bus_event);
    }

    for (int i = 0; i < RE_MAX_CALLBACK_SUBSCRIBERS; i++) {
        if (captured[i].used) {
            captured[i].callback(event, captured[i].ctx);
            RotaryEncoder_EnterCritical();
            rotaryEncoderService.rotation_slots[i].in_flight--;
            RotaryEncoder_ExitCritical();
        }
    }
    RotaryEncoder_EnterCritical();
    rotaryEncoderService.dispatches--;
    RotaryEncoder_ExitCritical();
}

void RotaryEncoder_DispatchButton(RotaryEncoderButtonEvent event) {
    RotaryEncoderButtonSlot captured[RE_MAX_CALLBACK_SUBSCRIBERS] = {0};
    RotaryEncoder_EnterCritical();
    if (atomic_load(&rotaryEncoderService.closing) || rotaryEncoderService.dispatches == UINT32_MAX) {
        RotaryEncoder_ExitCritical();
        return;
    }
    for (int i = 0; i < RE_MAX_CALLBACK_SUBSCRIBERS; i++) {
        if (rotaryEncoderService.button_slots[i].used &&
            rotaryEncoderService.button_slots[i].in_flight == UINT32_MAX) {
            RotaryEncoder_ExitCritical();
            return;
        }
    }
    for (int i = 0; i < RE_MAX_CALLBACK_SUBSCRIBERS; i++) {
        if (rotaryEncoderService.button_slots[i].used) {
            captured[i] = rotaryEncoderService.button_slots[i];
            rotaryEncoderService.button_slots[i].in_flight++;
        }
    }
    rotaryEncoderService.dispatches++;
    RotaryEncoder_ExitCritical();
    const char *event_name = RE_EVENT_BUTTON_CLICK;
    switch (event.gesture) {
        case GESTURE_PRESSED:      event_name = RE_EVENT_BUTTON_PRESSED; break;
        case GESTURE_DOUBLE_CLICK: event_name = RE_EVENT_BUTTON_DBLCLICK;  break;
        case GESTURE_LONG_PRESS:   event_name = RE_EVENT_BUTTON_LONGPRESS; break;
        case GESTURE_HELD:         event_name = RE_EVENT_BUTTON_HELD;      break;
        case GESTURE_RELEASED:     event_name = RE_EVENT_BUTTON_RELEASED;  break;
        default:                   event_name = RE_EVENT_BUTTON_CLICK;     break;
    }

    BusEvent bus_event = {
        .eventName = event_name,
        .type      = EVENT_TYPE_INFO,
        .data      = &event,
        .dataSize  = sizeof(event),
    };

    if (rotaryEncoderService.eventBus && rotaryEncoderService.eventBus->Publish) {
        rotaryEncoderService.eventBus->Publish(bus_event);
    }

    for (int i = 0; i < RE_MAX_CALLBACK_SUBSCRIBERS; i++) {
        if (captured[i].used) {
            captured[i].callback(event, captured[i].ctx);
            RotaryEncoder_EnterCritical();
            rotaryEncoderService.button_slots[i].in_flight--;
            RotaryEncoder_ExitCritical();
        }
    }
    RotaryEncoder_EnterCritical();
    rotaryEncoderService.dispatches--;
    RotaryEncoder_ExitCritical();
}

void RotaryEncoder_DispatchQueuedEvents(void) {
    RotaryEncoderEvent event;
    while (rotaryEncoderService.eventQueue &&
           xQueueReceive(rotaryEncoderService.eventQueue, &event, 0) == pdTRUE) {
        if (event.type == ROTARY_ENCODER_EVENT_ROTATION) {
            RotaryEncoder_DispatchRotation(event.payload.rotation);
        } else {
            RotaryEncoder_DispatchButton(event.payload.button);
        }
    }
}

/* ── Service Lifecycle ───────────────────────────────────────────────── */

static int32_t RotaryEncoder_InitializeOwned(IService *self, IDIRegistry *registry) {
    (void)self;
    (void)registry;
    if (atomic_load(&rotaryEncoderService.closing) &&
        rotaryEncoderService.state != SERVICE_STATE_STOPPED) return -1;

    if (rotaryEncoderService.state == SERVICE_STATE_STARTED) {
        return 0;
    }
    if (rotaryEncoderService.state >= SERVICE_STATE_INITIALIZED) {
        return 0;
    }

    ESP_LOGI(TAG, "Initializing RotaryEncoder service");

    rotaryEncoderService.eventBus = ServiceLocator_GetEventBus();
    rotaryEncoderService.runtime_config = RotaryEncoder_GetDefaultRuntimeConfig();
    IConfigurationManager *configuration = ConfigurationManager_GetInstance();
    if (configuration) {
        SystemSettings snapshot;
        if (!configuration->GetSnapshot || configuration->GetSnapshot(configuration, &snapshot) != ESP_OK)
            return -1;
        if (!snapshot.hardware.encoder_steps_per_detent || snapshot.hardware.encoder_accel_enabled > 1 ||
            snapshot.hardware.encoder_invert_direction > 1) return -1;
        rotaryEncoderService.runtime_config.steps_per_detent = snapshot.hardware.encoder_steps_per_detent;
        rotaryEncoderService.runtime_config.accel_enabled = snapshot.hardware.encoder_accel_enabled != 0;
        rotaryEncoderService.runtime_config.accel_threshold_ms = snapshot.hardware.encoder_accel_threshold_ms;
        rotaryEncoderService.runtime_config.invert_direction = snapshot.hardware.encoder_invert_direction != 0;
    }
    memset(&rotaryEncoderService.diagnostics, 0, sizeof(rotaryEncoderService.diagnostics));
    memset(&rotaryEncoderService.rotation_slots, 0, sizeof(rotaryEncoderService.rotation_slots));
    memset(&rotaryEncoderService.button_slots, 0, sizeof(rotaryEncoderService.button_slots));
    memset(&rotaryEncoderService.status, 0, sizeof(rotaryEncoderService.status));
    memset(&rotaryEncoderService.gesture, 0, sizeof(rotaryEncoderService.gesture));
    rotaryEncoderService.pcnt_pending_detents = 0;

    IPinManager *pinMgr = (IPinManager *)ServiceLocator_GetService(SVC_NAME_PIN_MANAGER);
    if (pinMgr) {
        if (!pinMgr->GetPin) return -1;
        rotaryEncoderService.clk_pin = pinMgr->GetPin(pinMgr, PIN_FUNC_ROTARY_CLK);
        rotaryEncoderService.dt_pin  = pinMgr->GetPin(pinMgr, PIN_FUNC_ROTARY_DT);
        rotaryEncoderService.sw_pin  = pinMgr->GetPin(pinMgr, PIN_FUNC_ROTARY_SW);
    } else {
        rotaryEncoderService.clk_pin = (gpio_num_t)ROTARY_ENCODER_CLK_PIN;
        rotaryEncoderService.dt_pin  = (gpio_num_t)ROTARY_ENCODER_DT_PIN;
        rotaryEncoderService.sw_pin  = (gpio_num_t)ROTARY_ENCODER_SW_PIN;
    }
    if (rotaryEncoderService.clk_pin < 0 || rotaryEncoderService.clk_pin >= GPIO_NUM_MAX ||
        rotaryEncoderService.dt_pin < 0 || rotaryEncoderService.dt_pin >= GPIO_NUM_MAX ||
        rotaryEncoderService.sw_pin < 0 || rotaryEncoderService.sw_pin >= GPIO_NUM_MAX) return -1;
    rotaryEncoderService.pin_manager = pinMgr;

    bool new_event_queue = !rotaryEncoderService.eventQueue;
    bool new_button_queue = !rotaryEncoderService.rawButtonQueue;
    if (new_event_queue) {
        rotaryEncoderService.eventQueue =
            xQueueCreate(RE_EVENT_QUEUE_SIZE, sizeof(RotaryEncoderEvent));
    }
    if (new_button_queue) {
        rotaryEncoderService.rawButtonQueue =
            xQueueCreate(RE_EVENT_QUEUE_SIZE, sizeof(RotaryEncoderRawEvent));
    }
    if (!rotaryEncoderService.eventQueue || !rotaryEncoderService.rawButtonQueue) {
        ESP_LOGE(TAG, "Failed to create event queues");
        goto init_failure;
    }

    IOLED *oled = (IOLED *)ServiceLocator_GetService(SVC_NAME_OLED);
    if (oled) {
        if (!oled->Lock || !oled->Unlock || !oled->Lock(oled)) goto init_failure;
    }
    rotaryEncoderService.indev = lv_indev_create();
    if (rotaryEncoderService.indev) {
        lv_indev_set_type(rotaryEncoderService.indev, LV_INDEV_TYPE_ENCODER);
        lv_indev_set_read_cb(rotaryEncoderService.indev, RotaryEncoder_ReadCB);
    }
    if (oled && oled->Unlock) {
        oled->Unlock(oled);
    }
    if (!rotaryEncoderService.indev) goto init_failure;

    rotaryEncoderService.state = SERVICE_STATE_INITIALIZED;
    return 0;
init_failure:
    if (new_button_queue && rotaryEncoderService.rawButtonQueue) {
        vQueueDelete(rotaryEncoderService.rawButtonQueue);
        rotaryEncoderService.rawButtonQueue = NULL;
    }
    if (new_event_queue && rotaryEncoderService.eventQueue) {
        vQueueDelete(rotaryEncoderService.eventQueue);
        rotaryEncoderService.eventQueue = NULL;
    }
    return -1;
}

static int32_t RotaryEncoder_StartOwned(IService *self) {
    (void)self;

    if (rotaryEncoderService.state == SERVICE_STATE_STARTED) {
        return atomic_load(&rotaryEncoderService.closing) ? -1 : 0;
    }
    if ((rotaryEncoderService.state != SERVICE_STATE_INITIALIZED &&
         rotaryEncoderService.state != SERVICE_STATE_STOPPED) ||
        rotaryEncoderService.taskHandle || rotaryEncoderService.button_attached ||
        rotaryEncoderService.pcnt_unit) {
        return -1;
    }
    atomic_store(&rotaryEncoderService.closing, false);
    atomic_store(&rotaryEncoderService.worker_exited, false);

    ESP_LOGI(TAG, "Starting RotaryEncoder (interrupt-driven PCNT)");

    esp_err_t err;
    IPinManager *pins = rotaryEncoderService.pin_manager;
    if (pins) {
        if (!pins->Configure || !pins->SetDebounce || !pins->AttachInterrupt || !pins->DetachInterrupt) return -1;
        if (pins->Configure(pins, PIN_FUNC_ROTARY_SW, PIN_MODE_INPUT, PIN_PULL_UP) != 0 ||
            pins->SetDebounce(pins, PIN_FUNC_ROTARY_SW, 0) != 0) return -1;
        /* The encoder owns raw-edge filtering; a second filter can drop release. */
        int32_t attached = pins->AttachInterrupt(pins, PIN_FUNC_ROTARY_SW, PIN_INTR_ANYEDGE,
                                                 RotaryEncoder_ButtonPinISR, NULL);
        if (attached != 0) {
            if (attached == PIN_MANAGER_ERR_SDK && pins->DetachInterrupt(pins, PIN_FUNC_ROTARY_SW) != 0)
                rotaryEncoderService.button_attached = true;
            return -1;
        }
    } else {
        /* Standalone diagnostic images can omit the typed manager. */
        gpio_config_t sw_conf = {
            .pin_bit_mask  = (1ULL << rotaryEncoderService.sw_pin),
            .mode          = GPIO_MODE_INPUT,
            .pull_up_en    = GPIO_PULLUP_ENABLE,
            .pull_down_en  = GPIO_PULLDOWN_DISABLE,
            .intr_type     = GPIO_INTR_ANYEDGE,
        };

        err = gpio_config(&sw_conf);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to configure button GPIO: 0x%x", err);
            return -1;
        }

        err = gpio_isr_handler_add(rotaryEncoderService.sw_pin,
                                   rotary_button_isr_handler, NULL);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to add button ISR handler: 0x%x", err);
            return -1;
        }
    }
    rotaryEncoderService.button_attached = true;

    /* Create the event processing task BEFORE PCNT so taskHandle is valid */
    TaskHandle_t worker = NULL;
    BaseType_t res = xTaskCreate(RotaryEncoder_Task, RE_TASK_NAME,
                                 RE_TASK_STACK_SIZE, NULL, RE_TASK_PRIORITY,
                                 &worker);
    if (res != pdPASS) {
        ESP_LOGE(TAG, "Failed to create processing task");
        RotaryEncoder_StopOwned(self);
        return -1;
    }
    RotaryEncoder_EnterCritical();
    rotaryEncoderService.taskHandle = worker;
    RotaryEncoder_ExitCritical();

    /* Initialize PCNT with interrupt-driven watch-point callbacks */
    err = RotaryEncoder_InitPCNT();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init PCNT driver: 0x%x", err);
        RotaryEncoder_StopOwned(self);
        return -1;
    }

    /* Register debug console commands */
    IDebugConsole *console =
        (IDebugConsole *)ServiceLocator_GetService(SVC_NAME_DEBUG_CONSOLE);
    if (console) {
        err = register_encoder_commands(console);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Encoder command registration failed: 0x%x", err);
            RotaryEncoder_StopOwned(self);
            return -1;
        }
    }

    rotaryEncoderService.state = SERVICE_STATE_STARTED;
    ESP_LOGI(TAG, "RotaryEncoder started successfully");
    return 0;
}

static int32_t RotaryEncoder_StopOwned(IService *self) {
    (void)self;

    if (rotaryEncoderService.state != SERVICE_STATE_STARTED &&
        !rotaryEncoderService.taskHandle && !rotaryEncoderService.button_attached &&
        !rotaryEncoderService.pcnt_unit) {
        return 0;
    }
    RotaryEncoder_EnterCritical();
    atomic_store(&rotaryEncoderService.closing, true);
    bool active = rotaryEncoderService.dispatches != 0 || rotaryEncoderService.control_active;
    RotaryEncoder_ExitCritical();
    if (active) return -1;

    ESP_LOGI(TAG, "Stopping RotaryEncoder service");

    if (RotaryEncoder_DeinitPCNT() != ESP_OK) return -1;
    if (rotaryEncoderService.button_attached) {
        IPinManager *pins = rotaryEncoderService.pin_manager;
        if (pins) {
            if (!pins->DetachInterrupt || pins->DetachInterrupt(pins, PIN_FUNC_ROTARY_SW) != 0) return -1;
        } else if (gpio_isr_handler_remove(rotaryEncoderService.sw_pin) != ESP_OK) return -1;
        rotaryEncoderService.button_attached = false;
    }

    if (rotaryEncoderService.taskHandle) {
        if (rotaryEncoderService.taskHandle == xTaskGetCurrentTaskHandle()) return -1;
        xTaskNotify(rotaryEncoderService.taskHandle, RE_NOTIFY_BUTTON_BIT, eSetBits);
        TickType_t start = xTaskGetTickCount();
        TickType_t timeout = pdMS_TO_TICKS(RE_STOP_TIMEOUT_MS);
        if (timeout == 0) timeout = 1;
        while (!atomic_load(&rotaryEncoderService.worker_exited)) {
            if ((TickType_t)(xTaskGetTickCount() - start) >= timeout) return -1;
            vTaskDelay(1);
        }
        vTaskDelete(rotaryEncoderService.taskHandle);
        RotaryEncoder_EnterCritical();
        rotaryEncoderService.taskHandle = NULL;
        RotaryEncoder_ExitCritical();
    }

    /* UI groups can retain this device; keep it alive across Stop/restart. */

    RotaryEncoder_ResetCounters();
    rotaryEncoderService.state = SERVICE_STATE_STOPPED;
    return 0;
}

static ServiceState RotaryEncoder_GetState(IService *self) {
    (void)self;
    return rotaryEncoderService.state;
}

static int32_t RotaryEncoder_CheckHealth(IService *self) {
    (void)self;
    return rotaryEncoderService.state == SERVICE_STATE_STARTED &&
           !atomic_load(&rotaryEncoderService.closing) ? 0 : -1;
}

static int32_t RotaryEncoder_Initialize(IService *self, IDIRegistry *registry) {
    if (atomic_exchange(&rotaryEncoderService.lifecycle_busy, true)) return -1;
    int32_t result = RotaryEncoder_InitializeOwned(self, registry);
    atomic_store(&rotaryEncoderService.lifecycle_busy, false);
    return result;
}
static int32_t RotaryEncoder_Start(IService *self) {
    if (atomic_exchange(&rotaryEncoderService.lifecycle_busy, true)) return -1;
    int32_t result = RotaryEncoder_StartOwned(self);
    atomic_store(&rotaryEncoderService.lifecycle_busy, false);
    return result;
}
static int32_t RotaryEncoder_Stop(IService *self) {
    if (atomic_exchange(&rotaryEncoderService.lifecycle_busy, true)) return -1;
    int32_t result = RotaryEncoder_StopOwned(self);
    atomic_store(&rotaryEncoderService.lifecycle_busy, false);
    return result;
}

/* ── Main Processing Task ────────────────────────────────────────────── */

/**
 * @brief Notification-driven task loop.
 *
 * Wakes on PCNT rotation or button ISR notifications (or every
 * RE_IDLE_WAKE_MS for gesture timeout checks).  Drains the pending
 * detent count and raw button queue, runs gesture detection, then
 * dispatches all queued events to subscribers and EventBus.
 */
static void RotaryEncoder_Task(void *arg) {
    (void)arg;
    uint32_t notification_value = 0;
    ESP_LOGI(TAG, "Encoder task started (interrupt-driven mode)");

    while (1) {
        if (atomic_load(&rotaryEncoderService.closing)) {
            if (atomic_exchange(&rotaryEncoderService.reset_requested, false))
                RotaryEncoder_FinishReset(ESP_ERR_INVALID_STATE);
            atomic_store(&rotaryEncoderService.worker_exited, true);
            vTaskSuspend(NULL);
            continue;
        }
        RotaryEncoder_PerformReset();
        /* Block until notified or idle timeout for gesture checks */
        if (atomic_load(&rotaryEncoderService.closing)) continue;
        xTaskNotifyWait(0, ULONG_MAX, &notification_value,
                        pdMS_TO_TICKS(RE_IDLE_WAKE_MS) > 0 ? pdMS_TO_TICKS(RE_IDLE_WAKE_MS) : 1);

        /* Drain accumulated detents from PCNT ISR */
        RotaryEncoder_EnterCritical();
        int32_t detents = rotaryEncoderService.pcnt_pending_detents;
        rotaryEncoderService.pcnt_pending_detents = 0;
        RotaryEncoder_ExitCritical();

        if (detents != 0) {
            RotaryEncoder_ProcessRotationDetents(detents);
        }

        /* Drain raw button events */
        RotaryEncoderRawEvent raw_btn;
        while (rotaryEncoderService.rawButtonQueue &&
               xQueueReceive(rotaryEncoderService.rawButtonQueue, &raw_btn,
                             0) == pdTRUE) {
            RotaryEncoder_ProcessButtonEvent(&raw_btn);
        }

        /* Check for long-press / held gestures */
        RotaryEncoder_CheckGestures();

        /* Dispatch all queued events to subscribers and EventBus */
        RotaryEncoder_DispatchQueuedEvents();

        /* Periodic health log */
        uint32_t now = (uint32_t)(esp_timer_get_time() / 1000ULL);
        if (rotaryEncoderService.last_health_log_ms == 0 ||
            now - rotaryEncoderService.last_health_log_ms >= RE_HEALTH_CHECK_INTERVAL_MS) {
            rotaryEncoderService.last_health_log_ms = now;
            ESP_LOGD(TAG, "Health: pos=%ld rot=%lu clk=%lu drops=%lu isr=%lu",
                     (long)rotaryEncoderService.status.absolute_position,
                     (unsigned long)rotaryEncoderService.status.total_rotations,
                     (unsigned long)rotaryEncoderService.status.total_clicks,
                     (unsigned long)(rotaryEncoderService.status.dropped_rotation_events +
                                     rotaryEncoderService.status.dropped_button_events),
                     (unsigned long)rotaryEncoderService.diagnostics.isr_count);
        }
    }
}

/* ── Module Singleton ────────────────────────────────────────────────── */

IRotaryEncoder *RotaryEncoder_GetModule(void) {
    if (rotaryEncoderService.base.base.Initialize == NULL) {
        memset(&rotaryEncoderService, 0, sizeof(rotaryEncoderService));

        /* IService vtable */
        rotaryEncoderService.base.base.Initialize  = RotaryEncoder_Initialize;
        rotaryEncoderService.base.base.Start        = RotaryEncoder_Start;
        rotaryEncoderService.base.base.Stop         = RotaryEncoder_Stop;
        rotaryEncoderService.base.base.GetState     = RotaryEncoder_GetState;
        rotaryEncoderService.base.base.CheckHealth  = RotaryEncoder_CheckHealth;

        /* IRotaryEncoder vtable */
        rotaryEncoderService.base.OnRotation          = RotaryEncoder_OnRotation;
        rotaryEncoderService.base.OnButton            = RotaryEncoder_OnButton;
        rotaryEncoderService.base.UnsubscribeRotation = RotaryEncoder_UnsubscribeRotation;
        rotaryEncoderService.base.UnsubscribeButton   = RotaryEncoder_UnsubscribeButton;
        rotaryEncoderService.base.GetStatus           = RotaryEncoder_GetStatus;
        rotaryEncoderService.base.GetAbsolutePosition = RotaryEncoder_GetAbsolutePosition;
        rotaryEncoderService.base.IsButtonHeld        = RotaryEncoder_IsButtonHeld;
        rotaryEncoderService.base.GetDiagnostics      = RotaryEncoder_GetDiagnostics;
        rotaryEncoderService.base.GetRuntimeConfig    = RotaryEncoder_GetRuntimeConfig;
        rotaryEncoderService.base.SetAcceleration     = RotaryEncoder_SetAcceleration;
        rotaryEncoderService.base.SetDebounceMs       = RotaryEncoder_SetDebounceMs;
        rotaryEncoderService.base.SetLongPressMs      = RotaryEncoder_SetLongPressMs;
        rotaryEncoderService.base.SetDoubleClickMs    = RotaryEncoder_SetDoubleClickMs;
        rotaryEncoderService.base.SetHeldRepeatMs     = RotaryEncoder_SetHeldRepeatMs;
        rotaryEncoderService.base.SetDirectionInverted = RotaryEncoder_SetDirectionInverted;
        rotaryEncoderService.base.SetStepsPerDetent   = RotaryEncoder_SetStepsPerDetent;
        rotaryEncoderService.base.Reset               = RotaryEncoder_Reset;

        rotaryEncoderService.state = SERVICE_STATE_UNINITIALIZED;
    }
    return &rotaryEncoderService.base;
}

#undef TAG
