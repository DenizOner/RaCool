/**
 * @file RotaryEncoderPCNT.c
 * @brief ESP32-C6 Pulse Counter (PCNT) driver for quadrature decoding.
 *
 * Configures a PCNT unit in full-quadrature mode with watch-point
 * callbacks so that every mechanical detent triggers an ISR that
 * notifies the encoder task immediately — no polling required.
 */

#include "Hardware/RotaryEncoder/RotaryEncoderConfig.h"
#include "Hardware/RotaryEncoder/RotaryEncoderInternal.h"
#include <driver/gpio.h>
#include <driver/pulse_cnt.h>
#include <esp_log.h>
#include <esp_timer.h>

#define TAG "RE_PCNT"

/* ── Button ISR ──────────────────────────────────────────────────────── */

/**
 * @brief GPIO ANYEDGE ISR for the push-button.
 *
 * Captures the raw press/release state with a noise filter and
 * enqueues it into the rawButtonQueue for task-level processing.
 */
static void IRAM_ATTR RotaryEncoder_QueueButtonLevelISR(bool level) {
    if (atomic_load(&rotaryEncoderService.closing)) return;

    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000ULL);
    portENTER_CRITICAL_ISR(&rotaryEncoderService.spinlock);
    if (atomic_load(&rotaryEncoderService.closing) ||
        (rotaryEncoderService.button_has_time &&
         level == rotaryEncoderService.button_last_level &&
         now - rotaryEncoderService.button_last_time_ms < RE_BUTTON_NOISE_FILTER_MS)) {
        portEXIT_CRITICAL_ISR(&rotaryEncoderService.spinlock);
        return;
    }
    rotaryEncoderService.button_last_time_ms = now;
    rotaryEncoderService.button_last_level = level;
    rotaryEncoderService.button_has_time = true;
    QueueHandle_t queue = rotaryEncoderService.rawButtonQueue;
    TaskHandle_t worker = rotaryEncoderService.taskHandle;
    portEXIT_CRITICAL_ISR(&rotaryEncoderService.spinlock);
    RotaryEncoderRawEvent event = {
        .pressed = !level,
        .timestamp_ms = now,
    };

    BaseType_t high_task_wakeup = pdFALSE;
    if (queue != NULL) {
        if (xQueueSendFromISR(queue, &event,
                              &high_task_wakeup) != pdTRUE) {
            portENTER_CRITICAL_ISR(&rotaryEncoderService.spinlock);
            rotaryEncoderService.diagnostics.queue_overflows++;
            portEXIT_CRITICAL_ISR(&rotaryEncoderService.spinlock);
        } else if (worker) {
            xTaskNotifyFromISR(worker,
                               RE_NOTIFY_BUTTON_BIT, eSetBits,
                               &high_task_wakeup);
        }
    }

    if (high_task_wakeup) {
        portYIELD_FROM_ISR();
    }
}

/* ── PCNT Watch-Point ISR ────────────────────────────────────────────── */

void IRAM_ATTR rotary_button_isr_handler(void *arg) {
    (void)arg;
    RotaryEncoder_QueueButtonLevelISR(gpio_get_level(rotaryEncoderService.sw_pin) != 0);
}

void IRAM_ATTR RotaryEncoder_ButtonPinISR(PinFunction_t func, bool level, void *arg) {
    (void)arg;
    if (func == PIN_FUNC_ROTARY_SW) RotaryEncoder_QueueButtonLevelISR(level);
}

/**
 * @brief PCNT watch-point callback — fires on every detent boundary.
 *
 * When the counter reaches +steps_per_detent or -steps_per_detent,
 * the hardware clears the count and this callback accumulates the
 * detent into pcnt_pending_detents, then notifies the encoder task.
 *
 * ISR-level bounce guard: mechanical contact chatter on cheap KY-040
 * modules can cause the PCNT counter to cross the watch-point again
 * immediately after the hardware auto-clear. Consecutive fires in
 * either direction that arrive within RE_PCNT_ISR_DEBOUNCE_US microseconds
 * are silently discarded here, before any task notification is sent.
 * The configured debounce window is preserved for both directions.
 */
static bool IRAM_ATTR pcnt_on_reach(pcnt_unit_handle_t unit,
                                     const pcnt_watch_event_data_t *edata,
                                     void *ctx) {
    (void)unit;
    (void)ctx;
    if (!edata || edata->watch_point_value == 0 || atomic_load(&rotaryEncoderService.closing) ||
        atomic_load(&rotaryEncoderService.pcnt_reconfiguring)) return false;

    int32_t detent_direction = (edata->watch_point_value > 0) ? 1 : -1;

    /* Unsigned elapsed time handles the microsecond counter wrap. */
    uint32_t now_us = (uint32_t)esp_timer_get_time();
    portENTER_CRITICAL_ISR(&rotaryEncoderService.spinlock);
    if (atomic_load(&rotaryEncoderService.closing) || atomic_load(&rotaryEncoderService.pcnt_reconfiguring) ||
        (rotaryEncoderService.pcnt_has_fire_time &&
         (now_us - rotaryEncoderService.pcnt_last_fire_time_us) < RE_PCNT_ISR_DEBOUNCE_US)) {
        portEXIT_CRITICAL_ISR(&rotaryEncoderService.spinlock);
        return false;
    }
    if ((detent_direction > 0 && rotaryEncoderService.pcnt_pending_detents == INT32_MAX) ||
        (detent_direction < 0 && rotaryEncoderService.pcnt_pending_detents == INT32_MIN)) {
        if (rotaryEncoderService.diagnostics.pcnt_overflow_count < UINT32_MAX)
            rotaryEncoderService.diagnostics.pcnt_overflow_count++;
        portEXIT_CRITICAL_ISR(&rotaryEncoderService.spinlock);
        return false;
    }
    rotaryEncoderService.pcnt_has_fire_time = true;
    rotaryEncoderService.pcnt_last_fire_time_us   = now_us;
    rotaryEncoderService.pcnt_last_fire_direction = detent_direction;


    rotaryEncoderService.pcnt_pending_detents += detent_direction;
    rotaryEncoderService.diagnostics.isr_count++;
    TaskHandle_t worker = rotaryEncoderService.taskHandle;
    portEXIT_CRITICAL_ISR(&rotaryEncoderService.spinlock);

    BaseType_t high_task_wakeup = pdFALSE;
    if (worker) {
        xTaskNotifyFromISR(worker,
                           RE_NOTIFY_ROTATION_BIT, eSetBits,
                           &high_task_wakeup);
    }

    return high_task_wakeup;
}


/* ── PCNT Initialization ─────────────────────────────────────────────── */

static esp_err_t RotaryEncoder_InitPCNTWithSteps(uint8_t steps) {
    /* A failed cleanup still owns its handles; never overwrite them. */
    if (rotaryEncoderService.pcnt_unit || rotaryEncoderService.pcnt_chan_a ||
        rotaryEncoderService.pcnt_chan_b) {
        return ESP_ERR_INVALID_STATE;
    }
    ESP_LOGI(TAG, "Initializing PCNT Unit on CLK:%d DT:%d (detent=%d edges)",
             rotaryEncoderService.clk_pin, rotaryEncoderService.dt_pin, steps);

    /* Unit config: limits set to ±detent so watch-points fire per click */
    pcnt_unit_config_t unit_config = {
        .high_limit = steps,
        .low_limit  = -steps,
        .flags.accum_count = 0,
    };

    esp_err_t err = pcnt_new_unit(&unit_config, &rotaryEncoderService.pcnt_unit);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create PCNT unit: 0x%x", err);
        return err;
    }

    /* Glitch filter */
    pcnt_glitch_filter_config_t filter_config = {
        .max_glitch_ns = RE_PCNT_GLITCH_FILTER_NS,
    };
    err = pcnt_unit_set_glitch_filter(rotaryEncoderService.pcnt_unit, &filter_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set glitch filter: 0x%x", err);
        goto err_unit;
    }

    /* Channel A: CLK edge, DT level */
    pcnt_chan_config_t chan_a_config = {
        .edge_gpio_num  = rotaryEncoderService.clk_pin,
        .level_gpio_num = rotaryEncoderService.dt_pin,
    };
    err = pcnt_new_channel(rotaryEncoderService.pcnt_unit, &chan_a_config,
                           &rotaryEncoderService.pcnt_chan_a);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create PCNT channel A: 0x%x", err);
        goto err_unit;
    }

    err = pcnt_channel_set_edge_action(rotaryEncoderService.pcnt_chan_a,
                                       PCNT_CHANNEL_EDGE_ACTION_DECREASE,
                                       PCNT_CHANNEL_EDGE_ACTION_INCREASE);
    if (err != ESP_OK) { goto err_chan_a; }

    err = pcnt_channel_set_level_action(rotaryEncoderService.pcnt_chan_a,
                                        PCNT_CHANNEL_LEVEL_ACTION_KEEP,
                                        PCNT_CHANNEL_LEVEL_ACTION_INVERSE);
    if (err != ESP_OK) { goto err_chan_a; }

    /* Channel B: DT edge, CLK level — completes full quadrature decoding */
    pcnt_chan_config_t chan_b_config = {
        .edge_gpio_num  = rotaryEncoderService.dt_pin,
        .level_gpio_num = rotaryEncoderService.clk_pin,
    };
    err = pcnt_new_channel(rotaryEncoderService.pcnt_unit, &chan_b_config,
                           &rotaryEncoderService.pcnt_chan_b);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create PCNT channel B: 0x%x", err);
        goto err_chan_a;
    }

    err = pcnt_channel_set_edge_action(rotaryEncoderService.pcnt_chan_b,
                                       PCNT_CHANNEL_EDGE_ACTION_INCREASE,
                                       PCNT_CHANNEL_EDGE_ACTION_DECREASE);
    if (err != ESP_OK) { goto err_chan_b; }

    err = pcnt_channel_set_level_action(rotaryEncoderService.pcnt_chan_b,
                                        PCNT_CHANNEL_LEVEL_ACTION_KEEP,
                                        PCNT_CHANNEL_LEVEL_ACTION_INVERSE);
    if (err != ESP_OK) { goto err_chan_b; }

    /* Watch points at ±detent — ISR fires on every mechanical click */
    err = pcnt_unit_add_watch_point(rotaryEncoderService.pcnt_unit, steps);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add +watch point: 0x%x", err);
        goto err_chan_b;
    }

    err = pcnt_unit_add_watch_point(rotaryEncoderService.pcnt_unit, -steps);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add -watch point: 0x%x", err);
        goto err_chan_b;
    }

    /* Register the ISR callback */
    pcnt_event_callbacks_t cbs = {
        .on_reach = pcnt_on_reach,
    };
    err = pcnt_unit_register_event_callbacks(rotaryEncoderService.pcnt_unit,
                                             &cbs, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register PCNT callbacks: 0x%x", err);
        goto err_chan_b;
    }

    /* Enable, clear, and start */
    err = pcnt_unit_enable(rotaryEncoderService.pcnt_unit);
    if (err != ESP_OK) { goto err_chan_b; }
    rotaryEncoderService.pcnt_enabled = true;

    err = pcnt_unit_clear_count(rotaryEncoderService.pcnt_unit);
    if (err != ESP_OK) { goto err_disable; }

    err = pcnt_unit_start(rotaryEncoderService.pcnt_unit);
    if (err != ESP_OK) { goto err_disable; }
    rotaryEncoderService.pcnt_running = true;

    ESP_LOGI(TAG, "PCNT initialized — interrupt-driven, %d edges/detent", steps);
    return ESP_OK;

err_disable:
err_chan_b:
err_chan_a:
err_unit:
    /* Return the initialization error, retaining any failed rollback ownership. */
    {
        esp_err_t cleanup = RotaryEncoder_DeinitPCNT();
        if (cleanup != ESP_OK) {
            ESP_LOGE(TAG, "PCNT initialization rollback failed: 0x%x", cleanup);
        }
    }
    return err;
}

esp_err_t RotaryEncoder_InitPCNT(void) {
    uint8_t steps = rotaryEncoderService.runtime_config.steps_per_detent;
    return RotaryEncoder_InitPCNTWithSteps(steps ? steps : RE_PCNT_STEPS_PER_DETENT);
}

esp_err_t RotaryEncoder_ReconfigurePCNT(uint8_t steps) {
    if (!steps) steps = RE_PCNT_STEPS_PER_DETENT;
    uint8_t previous = rotaryEncoderService.runtime_config.steps_per_detent;
    if (!previous) previous = RE_PCNT_STEPS_PER_DETENT;
    if (!rotaryEncoderService.pcnt_unit) {
        portENTER_CRITICAL(&rotaryEncoderService.spinlock);
        rotaryEncoderService.runtime_config.steps_per_detent = steps;
        rotaryEncoderService.pcnt_pending_detents = 0;
        portEXIT_CRITICAL(&rotaryEncoderService.spinlock);
        return ESP_OK;
    }
    if (steps == previous) return ESP_OK;
    atomic_store(&rotaryEncoderService.pcnt_reconfiguring, true);
    esp_err_t result = RotaryEncoder_DeinitPCNT();
    if (result == ESP_OK) result = RotaryEncoder_InitPCNTWithSteps(steps);
    if (result != ESP_OK) {
        /* Clean retained new/old resources before restoring the applied setting. */
        esp_err_t rollback = RotaryEncoder_DeinitPCNT();
        if (rollback == ESP_OK) rollback = RotaryEncoder_InitPCNTWithSteps(previous);
        if (rollback != ESP_OK) {
            atomic_store(&rotaryEncoderService.closing, true);
            rotaryEncoderService.state = SERVICE_STATE_ERROR;
            ESP_LOGE(TAG, "PCNT reconfiguration rollback failed: 0x%x", rollback);
        }
    }
    portENTER_CRITICAL(&rotaryEncoderService.spinlock);
    if (result == ESP_OK) rotaryEncoderService.runtime_config.steps_per_detent = steps;
    rotaryEncoderService.pcnt_pending_detents = 0;
    rotaryEncoderService.pcnt_has_fire_time = false;
    portEXIT_CRITICAL(&rotaryEncoderService.spinlock);
    atomic_store(&rotaryEncoderService.pcnt_reconfiguring, false);
    return result;
}

/* ── PCNT De-initialization ──────────────────────────────────────────── */

esp_err_t RotaryEncoder_DeinitPCNT(void) {
    ESP_LOGI(TAG, "Deinitializing PCNT Unit");
    if (rotaryEncoderService.pcnt_unit) {
        esp_err_t err;
        if (rotaryEncoderService.pcnt_running) {
            err = pcnt_unit_stop(rotaryEncoderService.pcnt_unit);
            if (err != ESP_OK) return err;
            rotaryEncoderService.pcnt_running = false;
        }
        if (rotaryEncoderService.pcnt_enabled) {
            err = pcnt_unit_disable(rotaryEncoderService.pcnt_unit);
            if (err != ESP_OK) return err;
            rotaryEncoderService.pcnt_enabled = false;
        }
        if (rotaryEncoderService.pcnt_chan_b) {
            err = pcnt_del_channel(rotaryEncoderService.pcnt_chan_b);
            if (err != ESP_OK) return err;
            rotaryEncoderService.pcnt_chan_b = NULL;
        }
        if (rotaryEncoderService.pcnt_chan_a) {
            err = pcnt_del_channel(rotaryEncoderService.pcnt_chan_a);
            if (err != ESP_OK) return err;
            rotaryEncoderService.pcnt_chan_a = NULL;
        }
        err = pcnt_del_unit(rotaryEncoderService.pcnt_unit);
        if (err != ESP_OK) return err;
        rotaryEncoderService.pcnt_unit = NULL;
    }
    return ESP_OK;
}

#undef TAG
