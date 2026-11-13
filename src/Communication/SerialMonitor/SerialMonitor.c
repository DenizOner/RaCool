#include <stdlib.h>
#include "SerialMonitorInternal.h"
#include "Management/Watchdog/WatchdogWorker.h"
#include "Communication/SerialMonitor/ISerialMonitor.h"
#include "Communication/SerialMonitor/SerialMonitorConfig.h"
#include "Communication/SerialMonitor/ISerialPort.h"
#include "Communication/SerialMonitor/ISerialBuffer.h"
#include "Communication/DebugConsole/IDebugConsole.h"
#include "Core/ServiceLocator/ServiceLocator.h"
#include "Core/ServiceLocator/ServiceNames.h"
#include <esp_log.h>
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>

static const char *TAG = "SerialMonitor";

// Module internal state
typedef struct {
    ISerialMonitor base;
    _Atomic(ServiceState) state;
    SerialMonitorConfig config;
    
    ISerialPort *port;
    ISerialBuffer *buffer;
    
    ISerialMonitorPlugin *plugins[SERIAL_MONITOR_MAX_PLUGINS];
    uint8_t plugin_count;
    
    WatchdogWorker rx_worker;
    WatchdogWorker tx_worker;
    SemaphoreHandle_t control;
    atomic_bool stopping;
    bool uart_owned;
    bool service_stop_pending; /* Protected by control; only full Stop clears it. */
    SemaphoreHandle_t mutex;
    _Atomic(TaskHandle_t) plugin_owner;
    QueueHandle_t tx_progress;
    atomic_uint pending_tx;
    atomic_bool tx_failed;
    bool active; /* Protected by control; workers use their stop checkpoints. */
    atomic_uint queued_bytes, tx_bytes, rx_bytes, rejected_packets;
    atomic_uint tx_dropped_bytes, tx_failures, rx_failures;
} SerialMonitorImpl;

static SerialMonitorImpl serialMonitor;

// Helper function for mutex handling
static bool sm_owns_plugins(void) {
    TaskHandle_t current = xTaskGetCurrentTaskHandle();
    return current && atomic_load(&serialMonitor.plugin_owner) == current;
}
static inline bool sm_take_mutex(TickType_t timeout) {
    if (sm_owns_plugins() || !serialMonitor.mutex ||
        xSemaphoreTake(serialMonitor.mutex, timeout) != pdTRUE) return false;
    atomic_store(&serialMonitor.plugin_owner, xTaskGetCurrentTaskHandle());
    return true;
}

static inline void sm_give_mutex(void) {
    if (serialMonitor.mutex) {
        atomic_store(&serialMonitor.plugin_owner, NULL);
        xSemaphoreGive(serialMonitor.mutex);
    }
}

// Forward declarations for external instances
extern ISerialPort* SerialPort_GetESP32Instance(void);
extern ISerialBuffer* SerialBuffer_GetInstance(void);

/**
 * @brief TX Task - Moves data from buffer to UART and notifies plugins
 */
static void SerialMonitor_TXTask(void *pvParameters) {
    size_t item_size;
    while (true) {
        WatchdogWorker_Checkpoint(&serialMonitor.tx_worker, NULL, 0);
        void *item = serialMonitor.buffer->PopData(serialMonitor.buffer, &item_size, 100);
        if (item) {
            // Advance only by bytes accepted by the driver. A failed write is
            // not retried: discard the unsent suffix instead of busy-polling.
            size_t accepted = 0;
            while (accepted < item_size) {
                size_t remaining = item_size - accepted;
                int32_t written = serialMonitor.port->WriteBytes(
                    serialMonitor.port, (const uint8_t *)item + accepted, remaining);
                if (written <= 0 || (size_t)written > remaining) break;
                accepted += (size_t)written;
            }
            
            // Notify plugins safely
            if (accepted > 0 && sm_take_mutex(pdMS_TO_TICKS(50))) {
                for (uint8_t i = 0; i < serialMonitor.plugin_count; i++) {
                    if (serialMonitor.plugins[i]->OnDataTransmitted) {
                        serialMonitor.plugins[i]->OnDataTransmitted(serialMonitor.plugins[i], item, accepted);
                    }
                }
                sm_give_mutex();
            }
            
            atomic_fetch_add(&serialMonitor.tx_bytes, (uint32_t)accepted);
            if (accepted != item_size) {
                atomic_fetch_add(&serialMonitor.tx_dropped_bytes, (uint32_t)(item_size - accepted));
                atomic_fetch_add(&serialMonitor.tx_failures, 1);
                atomic_store(&serialMonitor.tx_failed, true);
            }
            serialMonitor.buffer->ReturnItem(serialMonitor.buffer, item);
            if (atomic_fetch_sub(&serialMonitor.pending_tx, 1) == 1) {
                const uint32_t completed = 0;
                // A full queue already contains the required wakeup.
                xQueueSend(serialMonitor.tx_progress, &completed, 0);
            }
        }
    }
}

/**
 * @brief RX Task - Reads from UART and notifies plugins
 */
static void SerialMonitor_RXTask(void *pvParameters) {
    uint8_t rx_data[128];
    while (true) {
        WatchdogWorker_Checkpoint(&serialMonitor.rx_worker, NULL, 0);
        int32_t len = serialMonitor.port->ReadBytes(serialMonitor.port, rx_data, sizeof(rx_data), 100);
        if (len > 0 && (size_t)len <= sizeof(rx_data)) {
            atomic_fetch_add(&serialMonitor.rx_bytes, (uint32_t)len);
            if (sm_take_mutex(pdMS_TO_TICKS(50))) {
                for (uint8_t i = 0; i < serialMonitor.plugin_count; i++) {
                    if (serialMonitor.plugins[i]->OnDataReceived) {
                        serialMonitor.plugins[i]->OnDataReceived(serialMonitor.plugins[i], rx_data, (size_t)len);
                    }
                }
                sm_give_mutex();
            }
        } else if (len < 0 || (size_t)len > sizeof(rx_data))
            atomic_fetch_add(&serialMonitor.rx_failures, 1);
    }
}

static bool sm_control(TickType_t timeout) {
    // Callbacks may enqueue without waiting, but must not invert the lock order
    // by waiting for lifecycle control while holding the plugin registry.
    if (timeout && sm_owns_plugins()) return false;
    return serialMonitor.control && xSemaphoreTake(serialMonitor.control, timeout) == pdTRUE;
}
static bool sm_is_worker(void) {
    TaskHandle_t task = xTaskGetCurrentTaskHandle();
    return task && (task == atomic_load(&serialMonitor.tx_worker.task) ||
                    task == atomic_load(&serialMonitor.rx_worker.task));
}
static int32_t sm_enqueue_locked(const void *data, size_t length) {
    int32_t result = -1;
    if (serialMonitor.state == SERVICE_STATE_STARTED && !serialMonitor.stopping) {
        atomic_fetch_add(&serialMonitor.pending_tx, 1);
        result = serialMonitor.buffer->PushData(serialMonitor.buffer, data, length, 0);
        if (result != 0) atomic_fetch_sub(&serialMonitor.pending_tx, 1);
    }
    if (result == 0) atomic_fetch_add(&serialMonitor.queued_bytes, (uint32_t)length);
    else atomic_fetch_add(&serialMonitor.rejected_packets, 1);
    return result;
}
static int32_t SerialMonitor_WriteRaw(ISerialMonitor *self, const void *data, size_t length) {
    if (!self || !data || !length) return -1;
    if (!sm_control(0)) { atomic_fetch_add(&serialMonitor.rejected_packets, 1); return -1; }
    int32_t result = sm_enqueue_locked(data, length);
    xSemaphoreGive(serialMonitor.control);
    return result;
}
void SerialMonitor_PerfTick(void) {
    if (!sm_control(0)) return;
    if (serialMonitor.state == SERVICE_STATE_STARTED && !serialMonitor.stopping)
        SerialMonitor_PerfTickLocked(sm_enqueue_locked);
    xSemaphoreGive(serialMonitor.control);
}
int32_t SerialMonitor_StartPerfTest(uint32_t frequency_hz) {
    if (!frequency_hz || frequency_hz > SERIAL_MONITOR_PERF_MAX_HZ ||
        !sm_control(pdMS_TO_TICKS(SERIAL_MONITOR_CONTROL_TIMEOUT_MS))) return -1;
    int32_t result = serialMonitor.state == SERVICE_STATE_STARTED && !serialMonitor.stopping ?
        SerialMonitor_PerfStartLocked(frequency_hz) : -1;
    xSemaphoreGive(serialMonitor.control);
    return result;
}
int32_t SerialMonitor_StopPerfTest(void) {
    if (!sm_control(pdMS_TO_TICKS(SERIAL_MONITOR_CONTROL_TIMEOUT_MS))) return -1;
    int32_t result = SerialMonitor_PerfStopLocked();
    xSemaphoreGive(serialMonitor.control);
    return result;
}
static int32_t SerialMonitor_WriteLog(ISerialMonitor *self, const char *message) {
    return message ? SerialMonitor_WriteRaw(self, message, strlen(message)) : -1;
}
/* Called under control, never under the plugin mutex. A failed stop keeps all
 * peripheral/buffer resources alive; late worker acknowledgments are retryable. */
static int32_t sm_stop_uart(void) {
    serialMonitor.stopping = true;
    serialMonitor.active = false;
    int32_t tx = WatchdogWorker_Stop(&serialMonitor.tx_worker, pdMS_TO_TICKS(SERIAL_MONITOR_STOP_TIMEOUT_MS));
    int32_t rx = WatchdogWorker_Stop(&serialMonitor.rx_worker, pdMS_TO_TICKS(SERIAL_MONITOR_STOP_TIMEOUT_MS));
    if (tx || rx) return -1;
    if (serialMonitor.uart_owned) {
        if (serialMonitor.port->Deinitialize(serialMonitor.port) != 0) return -1;
        serialMonitor.uart_owned = false;
    }
    return 0;
}
static int32_t SerialMonitor_StartUART(ISerialMonitor *self) {
    if (!self || sm_is_worker() || !sm_control(pdMS_TO_TICKS(SERIAL_MONITOR_CONTROL_TIMEOUT_MS))) return -1;
    int32_t result = -1;
    if (serialMonitor.state != SERVICE_STATE_STARTED || serialMonitor.stopping) goto done;
    if (serialMonitor.active) { result = 0; goto done; }
    // A failed adapter initialization can own resources pending cleanup.
    serialMonitor.uart_owned = true;
    if (serialMonitor.port->Initialize(serialMonitor.port, serialMonitor.config.uart_num,
        serialMonitor.config.baud_rate, serialMonitor.config.rx_buffer_size, serialMonitor.config.tx_buffer_size) != 0) goto failed;
    if (WatchdogWorker_Start(&serialMonitor.tx_worker, SerialMonitor_TXTask, "SerMonTX",
        SERIAL_MONITOR_WORKER_STACK_SIZE, NULL, serialMonitor.config.task_priority) != 0) goto failed;
    if (WatchdogWorker_Start(&serialMonitor.rx_worker, SerialMonitor_RXTask, "SerMonRX",
        SERIAL_MONITOR_WORKER_STACK_SIZE, NULL, serialMonitor.config.task_priority) != 0) goto failed;
    serialMonitor.active = true;
    result = 0;
    goto done;
failed:
    if (sm_stop_uart() == 0) serialMonitor.stopping = false;
done:
    xSemaphoreGive(serialMonitor.control);
    return result;
}
static int32_t SerialMonitor_StopUART(ISerialMonitor *self) {
    if (!self || sm_is_worker() || !sm_control(pdMS_TO_TICKS(SERIAL_MONITOR_CONTROL_TIMEOUT_MS))) return -1;
    int32_t result = sm_stop_uart();
    if (result == 0 && !serialMonitor.service_stop_pending) serialMonitor.stopping = false;
    xSemaphoreGive(serialMonitor.control);
    return result;
}
static int32_t SerialMonitor_Flush(ISerialMonitor *self) {
    if (!self || !sm_control(pdMS_TO_TICKS(SERIAL_MONITOR_CONTROL_TIMEOUT_MS))) return -1;
    int32_t result = -1;
    if (!serialMonitor.active || serialMonitor.stopping) goto done;
    TickType_t started = xTaskGetTickCount();
    const TickType_t budget = pdMS_TO_TICKS(SERIAL_MONITOR_FLUSH_TIMEOUT_MS);
    while (atomic_load(&serialMonitor.pending_tx)) {
        TickType_t elapsed = xTaskGetTickCount() - started;
        uint32_t completed;
        if (elapsed >= budget || xQueueReceive(serialMonitor.tx_progress, &completed, budget - elapsed) != pdTRUE)
            goto done;
    }
    TickType_t elapsed = xTaskGetTickCount() - started;
    if (elapsed >= budget) goto done;
    result = serialMonitor.port->WaitTxDone(serialMonitor.port, (budget - elapsed) * portTICK_PERIOD_MS);
    if (result == 0 && atomic_exchange(&serialMonitor.tx_failed, false)) result = -1;
done:
    xSemaphoreGive(serialMonitor.control);
    return result;
}
static int32_t SerialMonitor_SetConfig(ISerialMonitor *self, const void *config) {
    if (!self || !SerialMonitorConfig_IsValid(config) || !sm_control(pdMS_TO_TICKS(SERIAL_MONITOR_CONTROL_TIMEOUT_MS))) return -1;
    int32_t result = -1;
    if (serialMonitor.state != SERVICE_STATE_STARTED && !serialMonitor.stopping) {
        serialMonitor.config = *(const SerialMonitorConfig *)config;
        result = 0;
    }
    xSemaphoreGive(serialMonitor.control);
    return result;
}

static int32_t SerialMonitor_AddPlugin(ISerialMonitor *self, ISerialMonitorPlugin *plugin) {
    if (!self || !plugin) return -1;
    if (!sm_take_mutex(pdMS_TO_TICKS(100))) return -1;

    // Check if already exists
    for (uint8_t i = 0; i < serialMonitor.plugin_count; i++) {
        if (serialMonitor.plugins[i] == plugin) {
            sm_give_mutex();
            return 0;
        }
    }
    
    if (serialMonitor.plugin_count >= SERIAL_MONITOR_MAX_PLUGINS ||
        (plugin->Init && plugin->Init(plugin) != 0)) {
        sm_give_mutex();
        return -1;
    }
    
    serialMonitor.plugins[serialMonitor.plugin_count++] = plugin;
    sm_give_mutex();
    // A concurrent removal may release plugin storage after the unlock.
    ESP_LOGI(TAG, "Plugin added.");
    return 0;
}

static int32_t SerialMonitor_RemovePlugin(ISerialMonitor *self, ISerialMonitorPlugin *plugin) {
    if (!self || !plugin) return -1;
    if (!sm_take_mutex(pdMS_TO_TICKS(100))) return -1;

    for (uint8_t i = 0; i < serialMonitor.plugin_count; i++) {
        if (serialMonitor.plugins[i] == plugin) {
            if (plugin->Deinit) plugin->Deinit(plugin);
            // Shift remaining
            for (uint8_t j = i; j < serialMonitor.plugin_count - 1; j++) {
                serialMonitor.plugins[j] = serialMonitor.plugins[j + 1];
            }
            serialMonitor.plugin_count--;
            sm_give_mutex();
            return 0;
        }
    }
    sm_give_mutex();
    return -1;
}

static void SerialMonitor_RegisterCommand(ISerialMonitor *self, const void *cmd) {
    IDebugConsole *console = (IDebugConsole *)ServiceLocator_GetService(SVC_NAME_DEBUG_CONSOLE);
    if (console && cmd) {
        console->RegisterCommand(console, (const esp_console_cmd_t *)cmd);
    }
}

// IService Implementation
static int32_t SerialMonitor_Initialize(IService *self, IDIRegistry *registry) {
    (void)registry;
    if (!self) return -1;
    // Initialization is serialized by SystemInitializer before publication.
    if (!serialMonitor.control) serialMonitor.control = xSemaphoreCreateMutex();
    if (!serialMonitor.mutex) serialMonitor.mutex = xSemaphoreCreateMutex();
    if (!serialMonitor.tx_progress) serialMonitor.tx_progress = xQueueCreate(1, sizeof(uint32_t));
    if (!serialMonitor.control || !serialMonitor.mutex || !serialMonitor.tx_progress) return -1;
    if (!sm_control(pdMS_TO_TICKS(SERIAL_MONITOR_CONTROL_TIMEOUT_MS))) return -1;
    int32_t result = -1;
    if (serialMonitor.state == SERVICE_STATE_STARTED || serialMonitor.stopping) goto done;
    if (WatchdogWorker_Init(&serialMonitor.tx_worker) || WatchdogWorker_Init(&serialMonitor.rx_worker)) goto done;
    if (serialMonitor.state == SERVICE_STATE_UNINITIALIZED) serialMonitor.config = SerialMonitorConfig_GetDefault();
    serialMonitor.port = SerialPort_GetESP32Instance();
    serialMonitor.buffer = SerialBuffer_GetInstance();
    if (!serialMonitor.port || !serialMonitor.buffer) goto done;
    serialMonitor.state = SERVICE_STATE_INITIALIZED;
    result = 0;
done:
    xSemaphoreGive(serialMonitor.control);
    return result;
}
static int32_t SerialMonitor_Start(IService *self) {
    if (!self || !sm_control(pdMS_TO_TICKS(SERIAL_MONITOR_CONTROL_TIMEOUT_MS))) return -1;
    int32_t result = -1;
    if (serialMonitor.stopping || (serialMonitor.state != SERVICE_STATE_INITIALIZED && serialMonitor.state != SERVICE_STATE_STOPPED)) goto done;
    if (serialMonitor.buffer->Initialize(serialMonitor.buffer, serialMonitor.config.ring_buffer_size, serialMonitor.config.queue_size) != 0) goto done;
    atomic_store(&serialMonitor.pending_tx, 0);
    atomic_store(&serialMonitor.tx_failed, false);
    serialMonitor.state = SERVICE_STATE_STARTED;
    result = 0;
done:
    xSemaphoreGive(serialMonitor.control);
    return result;
}
static int32_t SerialMonitor_Stop(IService *self) {
    if (!self || sm_is_worker() || !sm_control(pdMS_TO_TICKS(SERIAL_MONITOR_CONTROL_TIMEOUT_MS))) return -1;
    int32_t result = -1;
    if (serialMonitor.state == SERVICE_STATE_STOPPED) { result = 0; goto done; }
    if (serialMonitor.state != SERVICE_STATE_STARTED) goto done;
    serialMonitor.service_stop_pending = true;
    serialMonitor.stopping = true;
    if (SerialMonitor_PerfStopLocked() != 0 || sm_stop_uart() != 0) goto done;
    if (serialMonitor.buffer->Deinitialize(serialMonitor.buffer) != 0) goto done;
    atomic_store(&serialMonitor.pending_tx, 0);
    serialMonitor.state = SERVICE_STATE_STOPPED;
    serialMonitor.service_stop_pending = false;
    serialMonitor.stopping = false;
    result = 0;
done:
    xSemaphoreGive(serialMonitor.control);
    return result;
}

#ifdef RACOOL_HEALTH_REPORT_DIAGNOSTIC
extern bool SerialMonitor_PerfDiagnosticActive(void);
int32_t SerialMonitor_DiagnosticHealthHold(void) {
    if (!sm_control(pdMS_TO_TICKS(SERIAL_MONITOR_CONTROL_TIMEOUT_MS))) return -1;
    bool idle = serialMonitor.state == SERVICE_STATE_STARTED && !serialMonitor.stopping &&
        !serialMonitor.active && !serialMonitor.uart_owned &&
        atomic_load(&serialMonitor.pending_tx) == 0 && !SerialMonitor_PerfDiagnosticActive();
    if (idle) serialMonitor.stopping = true;
    xSemaphoreGive(serialMonitor.control);
    return idle ? SerialMonitor_Stop(&serialMonitor.base.base) : -1;
}
#endif

static ServiceState SerialMonitor_GetState(IService *self) {
    return serialMonitor.state;
}
static int32_t SerialMonitor_GetStats(ISerialMonitor *self, SerialMonitorStats *out) {
    if (!self || !out || !sm_control(pdMS_TO_TICKS(SERIAL_MONITOR_CONTROL_TIMEOUT_MS))) return -1;
    SerialMonitorStats snapshot = {.state = serialMonitor.state,
        .uart_owned = serialMonitor.uart_owned, .active = serialMonitor.active,
        .stopping = serialMonitor.stopping, .pending_tx = atomic_load(&serialMonitor.pending_tx),
        .queued_bytes = atomic_load(&serialMonitor.queued_bytes), .tx_bytes = atomic_load(&serialMonitor.tx_bytes),
        .rx_bytes = atomic_load(&serialMonitor.rx_bytes), .rejected_packets = atomic_load(&serialMonitor.rejected_packets),
        .tx_dropped_bytes = atomic_load(&serialMonitor.tx_dropped_bytes),
        .tx_failures = atomic_load(&serialMonitor.tx_failures), .rx_failures = atomic_load(&serialMonitor.rx_failures)};
    xSemaphoreGive(serialMonitor.control);
    *out = snapshot;
    return 0;
}

static int32_t SerialMonitor_CheckHealth(IService *self) {
    return (serialMonitor.state == SERVICE_STATE_STARTED && !serialMonitor.stopping) ? 0 : -1;
}

ISerialMonitor *SerialMonitor_GetModule(void) {
    static bool initialized = false;
    if (!initialized) {
        serialMonitor.base.base.Initialize = SerialMonitor_Initialize;
        serialMonitor.base.base.Start = SerialMonitor_Start;
        serialMonitor.base.base.Stop = SerialMonitor_Stop;
        serialMonitor.base.base.GetState = SerialMonitor_GetState;
        serialMonitor.base.base.CheckHealth = SerialMonitor_CheckHealth;
        
        serialMonitor.base.WriteLog = SerialMonitor_WriteLog;
        serialMonitor.base.WriteRaw = SerialMonitor_WriteRaw;
        serialMonitor.base.StartUART = SerialMonitor_StartUART;
        serialMonitor.base.StopUART = SerialMonitor_StopUART;
        serialMonitor.base.Flush = SerialMonitor_Flush;
        serialMonitor.base.SetConfig = SerialMonitor_SetConfig;
        serialMonitor.base.AddPlugin = SerialMonitor_AddPlugin;
        serialMonitor.base.RemovePlugin = SerialMonitor_RemovePlugin;
        serialMonitor.base.RegisterCommand = SerialMonitor_RegisterCommand;
        serialMonitor.base.GetStats = SerialMonitor_GetStats;


        serialMonitor.state = SERVICE_STATE_UNINITIALIZED;
        initialized = true;
    }
    return &serialMonitor.base;
}
