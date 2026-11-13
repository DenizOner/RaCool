#ifndef ISERIAL_MONITOR_H
#define ISERIAL_MONITOR_H

#include "Core/IService.h"
#include "Communication/SerialMonitor/ISerialMonitorPlugin.h"
#include <stdbool.h>
#include <stdint.h>

/**
 * @brief Serial Monitor Interface (Facade)
 * Manages UART communication, buffering, and plugins.
 */
typedef struct ISerialMonitor ISerialMonitor;
/* Counters are independently sampled, process-lifetime modulo-2^32 values.
 * Queued bytes are admission, driver TX bytes are SDK acceptance; neither proves
 * external wiring throughput. Resource fields share the lifecycle lock. */
typedef struct {
  ServiceState state;
  bool uart_owned, active, stopping;
  uint32_t pending_tx, queued_bytes, tx_bytes, rx_bytes;
  uint32_t rejected_packets, tx_dropped_bytes, tx_failures, rx_failures;
} SerialMonitorStats;

struct ISerialMonitor {
  IService base;

  /**
   * @brief Writes a log message (Asynchronous if configured)
   * @param message Log message
   * @return 0 Success, -1 Error
   */
  int32_t (*WriteLog)(ISerialMonitor *self, const char *message);

  /**
   * @brief Writes raw binary data
   * @param data Data pointer
   * @param length Data length
   * @return 0 Success, -1 Error
   */
  int32_t (*WriteRaw)(ISerialMonitor *self, const void *data, size_t length);

  /**
   * @brief Starts UART communication
   * @return 0 Success, -1 Error
   */
  int32_t (*StartUART)(ISerialMonitor *self);

  /**
   * @brief Stops UART communication after both workers release their resources.
   * Timeout preserves UART and buffer ownership; retry StopUART or service Stop.
   * Calls from a UART worker callback are rejected.
   * @return 0 Success, -1 Error
   */
  int32_t (*StopUART)(ISerialMonitor *self);

  /**
   * @brief Drains queued/in-flight items, then waits for hardware TX completion.
   * New writes are rejected while flushing. A bounded timeout preserves pending
   * work; retry is allowed. A dropped TX suffix is reported once after draining.
   * Callback calls are rejected. StopUART alone preserves queued items.
   * @return 0 Success, -1 Error
   */
  int32_t (*Flush)(ISerialMonitor *self);

  /**
   * @brief Updates configuration while the service is stopped or initialized.
   * @param config SerialMonitorConfig pointer
   * @return 0 Success, -1 Error
   */
  int32_t (*SetConfig)(ISerialMonitor *self, const void *config);

  /**
   * @brief Registers a plugin to the monitor
   * @param plugin Plugin implementation
   * @return 0 Success, -1 Error (e.g. max plugins reached)
   */
  int32_t (*AddPlugin)(ISerialMonitor *self, ISerialMonitorPlugin *plugin);

  /**
   * @brief Removes a plugin
   * @param plugin Plugin implementation
   * @return 0 Success, -1 Not found
   */
  int32_t (*RemovePlugin)(ISerialMonitor *self, ISerialMonitorPlugin *plugin);

  /**
   * @brief Registers a CLI command (Compatibility layer)
   * @param cmd esp_console_cmd_t structure pointer
   */
  void (*RegisterCommand)(ISerialMonitor *self, const void *cmd);
  /* Task-only bounded snapshot. Failure preserves caller output. */
  int32_t (*GetStats)(ISerialMonitor *self, SerialMonitorStats *out);
};

/**
 * @brief Returns the Serial Monitor module singleton
 */
ISerialMonitor *SerialMonitor_GetModule(void);
/* Task-context timer-driven enqueue test. Start requires a started service.
 * Controls serialize with service shutdown. Busy timer ticks are skipped;
 * reported counts describe queued attempts, not physical UART throughput. */
int32_t SerialMonitor_StartPerfTest(uint32_t frequency_hz);
int32_t SerialMonitor_StopPerfTest(void);

#endif // ISERIAL_MONITOR_H
