#ifndef ROTARY_ENCODER_INTERNAL_H
#define ROTARY_ENCODER_INTERNAL_H

/**
 * @file RotaryEncoderInternal.h
 * @brief Private implementation details for the RotaryEncoder module.
 *
 * This header is shared among the .c files that compose the encoder
 * subsystem (RotaryEncoder.c, RotaryEncoderPCNT.c, RotaryEncoderGesture.c,
 * RotaryEncoderConsole.c) but must NOT be included by external consumers.
 */

#include "Hardware/RotaryEncoder/IRotaryEncoder.h"
#include "Hardware/RotaryEncoder/RotaryEncoderConfig.h"
#include "Hardware/RotaryEncoder/RotaryEncoderTypes.h"
#include "Core/EventBus/IEventBus.h"
#include "Hardware/PinManager/IPinManager.h"
#include <driver/gpio.h>
#include <driver/pulse_cnt.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <stdatomic.h>

/* ── Task Notification Bits ──────────────────────────────────────────── */

/** @brief Notification bit set by the PCNT watch-point ISR on rotation. */
#define RE_NOTIFY_ROTATION_BIT  (1u << 0)

/** @brief Notification bit set by the button GPIO ISR. */
#define RE_NOTIFY_BUTTON_BIT    (1u << 1)

/* ── Callback Slot Types ─────────────────────────────────────────────── */

/**
 * @brief Slot for a registered rotation callback subscriber.
 */
typedef struct {
    RotaryEncoderRotationCallback callback;
    void *ctx;
    bool used;
    uint32_t in_flight;
} RotaryEncoderRotationSlot;

/**
 * @brief Slot for a registered button callback subscriber.
 */
typedef struct {
    RotaryEncoderButtonCallback callback;
    void *ctx;
    bool used;
    uint32_t in_flight;
} RotaryEncoderButtonSlot;

/* ── Gesture Engine State ────────────────────────────────────────────── */

/**
 * @brief Internal state for the button gesture recognizer.
 *
 * Bundled into the service impl to avoid file-scope statics and
 * to enable clean reset semantics.
 */
typedef struct {
    uint32_t press_start_time;
    bool     long_press_reported;
    uint32_t last_held_trigger_time;
    uint32_t last_click_time;
    uint8_t  click_count;
    uint32_t last_rotation_time;
} RotaryEncoderGestureState;

typedef enum {
    RE_SIM_NONE = 0,
    RE_SIM_CW,
    RE_SIM_CCW,
    RE_SIM_CLICK,
    RE_SIM_DOUBLE_CLICK,
    RE_SIM_LONG_PRESS,
    RE_SIM_HELD,
    RE_SIM_RELEASE
} RotaryEncoderSimulation;

/* ── Service Implementation ──────────────────────────────────────────── */

/**
 * @brief Concrete implementation backing the IRotaryEncoder interface.
 *
 * Single global instance; all access is protected by @c spinlock for
 * ISR-safe fields or by task affinity for task-only fields.
 */
typedef struct {
    IRotaryEncoder base;
    _Atomic(ServiceState) state;

    /* GPIO pins resolved at init time */
    gpio_num_t clk_pin;
    gpio_num_t dt_pin;
    gpio_num_t sw_pin;
    IPinManager *pin_manager;

    /* PCNT hardware handles */
    pcnt_unit_handle_t    pcnt_unit;
    pcnt_channel_handle_t pcnt_chan_a;
    pcnt_channel_handle_t pcnt_chan_b;
    bool pcnt_enabled;
    bool pcnt_running;

    /* FreeRTOS primitives */
    QueueHandle_t eventQueue;
    QueueHandle_t rawButtonQueue;
    TaskHandle_t  taskHandle;
    atomic_bool lifecycle_busy;
    atomic_bool closing;
    atomic_bool worker_exited;
    bool button_attached;
    uint32_t dispatches;
    bool control_active;
    bool control_reconfigure;
    uint8_t control_steps;
    RotaryEncoderSimulation control_simulation;
    uint16_t control_count;
    atomic_bool pcnt_reconfiguring;
    atomic_bool reset_requested;
    atomic_bool reset_done;
    atomic_int reset_result;

    /* Shared state (protected by spinlock) */
    portMUX_TYPE               spinlock;
    RotaryEncoderStatus        status;
    RotaryEncoderDiagnostics   diagnostics;
    RotaryEncoderRuntimeConfig runtime_config;
    uint32_t                   last_health_log_ms;
    int32_t lv_last_position;
    uint32_t button_last_time_ms;
    bool button_has_time;
    bool button_last_level;
    bool pcnt_has_fire_time;

    /* ISR → Task communication: accumulated detent count from PCNT ISR */
    volatile int32_t pcnt_pending_detents;

    /* Bounce state is shared with Reset and protected by spinlock. */
    volatile uint32_t pcnt_last_fire_time_us;
    volatile int32_t  pcnt_last_fire_direction;

    /* EventBus integration */
    IEventBus *eventBus;

    /* Direct callback subscribers */
    RotaryEncoderRotationSlot rotation_slots[RE_MAX_CALLBACK_SUBSCRIBERS];
    RotaryEncoderButtonSlot   button_slots[RE_MAX_CALLBACK_SUBSCRIBERS];

    /* Gesture engine state */
    RotaryEncoderGestureState gesture;

    /* LVGL input device handle */
    struct _lv_indev_t *indev;
} RotaryEncoderServiceImpl;

/* ── Global Instance ─────────────────────────────────────────────────── */

extern RotaryEncoderServiceImpl rotaryEncoderService;

/* ── PCNT Driver ─────────────────────────────────────────────────────── */

/**
 * @brief Initialize the PCNT unit with watch-point callbacks for
 *        interrupt-driven rotation detection.
 */
esp_err_t RotaryEncoder_InitPCNT(void);

/**
 * @brief Tear down the PCNT unit and release all hardware resources.
 */
esp_err_t RotaryEncoder_DeinitPCNT(void);

/** Rebuild PCNT limits; commit the setting only after successful SDK setup. */
esp_err_t RotaryEncoder_ReconfigurePCNT(uint8_t steps);

/**
 * @brief GPIO ISR handler for the push-button (ANYEDGE).
 */
void rotary_button_isr_handler(void *arg);
void RotaryEncoder_ButtonPinISR(PinFunction_t func, bool level, void *arg);

/* ── Event Processing ────────────────────────────────────────────────── */

/**
 * @brief Process an accumulated detent count from the PCNT ISR.
 *
 * Applies direction inversion, acceleration, updates status counters
 * and enqueues a RotaryEncoderRotationEvent.
 *
 * @param detents  Signed detent count (positive = CW before inversion).
 */
void RotaryEncoder_ProcessRotationDetents(int32_t detents);

/**
 * @brief Process a raw button press/release edge from the ISR queue.
 */
void RotaryEncoder_ProcessButtonEvent(RotaryEncoderRawEvent *raw);

/**
 * @brief Check for pending long-press / held-repeat gestures.
 */
void RotaryEncoder_CheckGestures(void);

/**
 * @brief Drain the event queue and dispatch to callbacks + EventBus.
 */
void RotaryEncoder_DispatchQueuedEvents(void);

/**
 * @brief Reset all status counters, diagnostics, and queues.
 */
void RotaryEncoder_ResetCounters(void);

/**
 * @brief Reset the gesture recognizer state machine.
 */
void RotaryEncoder_ResetGestureState(void);

/**
 * @brief Dispatch a single rotation event to subscribers and EventBus.
 */
void RotaryEncoder_DispatchRotation(RotaryEncoderRotationEvent event);

/**
 * @brief Dispatch a single button event to subscribers and EventBus.
 */
void RotaryEncoder_DispatchButton(RotaryEncoderButtonEvent event);

/**
 * @brief Enqueue an event for deferred dispatch from the task context.
 */
void RotaryEncoder_EnqueueEvent(RotaryEncoderEvent *event);

/**
 * @brief Clamp a steps-per-detent value to a valid range.
 */
uint8_t RotaryEncoder_ClampStepsPerDetent(uint8_t steps);

/** Submit a copied diagnostic event to the existing worker, without fake raw timestamps. */
esp_err_t RotaryEncoder_Simulate(RotaryEncoderSimulation simulation, uint16_t count);

#endif /* ROTARY_ENCODER_INTERNAL_H */
