#include "Core/Localization/Localization.h"
/**
 * @file SerialMonitorConsoleCommands.c
 * @brief Debug Console CLI commands for SerialMonitor Module.
 *
 * @author RaCool DeepMind Team
 * @date 2026-08-01
 */

#include "Communication/SerialMonitor/SerialMonitorConsoleCommands.h"
#include "Communication/SerialMonitor/ISerialMonitor.h"
#include "Core/ServiceLocator/ServiceLocator.h"
#include "Core/ServiceLocator/ServiceNames.h"
#include "Communication/DebugConsole/IDebugConsole.h"
#include "esp_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include "Communication/SerialMonitor/SerialMonitorConfig.h"
#include <string.h>

#include <esp_console.h>

static const char *SER_CMD_TAG = "SerialConsole";

// Command handler for SER_PERF
static int DispatchPerfCommand(int argc, char **argv) {
    const LocalizationCatalog *catalog = Localization_Snapshot();
    if (!argv || argc < 2 || argc > 3 || !argv[1]) {
        LOC_PRINTF(catalog, LOC_CLI_SERIAL_USAGE_SER_PERF_START_STOP_STATUS_UART_START_UART_STOP_FREQ_HZ, "Usage: SER_PERF <start|stop|status|uart-start|uart-stop> [freq_hz]\n");
        return -1;
    }
    
    if ((strcmp(argv[1], "uart-start") == 0 || strcmp(argv[1], "uart-stop") == 0) && argc == 2) {
        ISerialMonitor *monitor = SerialMonitor_GetModule();
        if (!monitor || !monitor->StartUART || !monitor->StopUART || !monitor->GetStats) return -1;
        bool start = strcmp(argv[1], "uart-start") == 0;
        int32_t result = start ? monitor->StartUART(monitor) : monitor->StopUART(monitor);
        SerialMonitorStats stats;
        if (result != 0 || monitor->GetStats(monitor, &stats) != 0 ||
            stats.stopping || stats.active != start || stats.uart_owned != start) {
            LOC_PRINTF(catalog, LOC_CLI_SERIAL_UART_LIFECYCLE_OPERATION_FAILED_OR_READBACK_UNAVAILABLE, "UART lifecycle operation failed or readback unavailable.\n");
            return -1;
        }
        LOC_PRINTF(catalog, LOC_CLI_SERIAL_UART_S_OWNERSHIP_AND_WORKER_STATE_VERIFIED, "UART %s; ownership and worker state verified.\n", start ? Localization_Text(catalog, LOC_CLI_SERIAL_STARTED) : Localization_Text(catalog, LOC_CLI_SERIAL_STOPPED));
        return 0;
    } else if (strcmp(argv[1], "status") == 0 &&
               (argc == 2 || (argc == 3 && strcmp(argv[2], "--machine") == 0))) {
        ISerialMonitor *monitor = SerialMonitor_GetModule(); SerialMonitorStats stats;
        if (!monitor || !monitor->GetStats || monitor->GetStats(monitor, &stats) != 0) return -1;
        if (argc == 3) {
            printf("RACOOL_CLI v=1 type=serial_monitor state=%d owned=%u active=%u closing=%u pending_tx=%lu queued_bytes=%lu tx_bytes=%lu rx_bytes=%lu rejected=%lu dropped=%lu tx_failures=%lu rx_failures=%lu\n",
                   (int)stats.state, (unsigned)stats.uart_owned, (unsigned)stats.active,
                   (unsigned)stats.stopping, (unsigned long)stats.pending_tx,
                   (unsigned long)stats.queued_bytes, (unsigned long)stats.tx_bytes,
                   (unsigned long)stats.rx_bytes, (unsigned long)stats.rejected_packets,
                   (unsigned long)stats.tx_dropped_bytes, (unsigned long)stats.tx_failures,
                   (unsigned long)stats.rx_failures);
            return 0;
        }
        LOC_PRINTF(catalog, LOC_CLI_SERIAL_SERIALMONITOR_STATE_D_UART_OWNED_U_ACTIVE_U_CLOSING_U_PENDING_TX_LU, "SerialMonitor State: %d; UART Owned: %u; Active: %u; Closing: %u; Pending TX: %lu\n",
            (int)stats.state, (unsigned)stats.uart_owned, (unsigned)stats.active,
            (unsigned)stats.stopping, (unsigned long)stats.pending_tx);
        LOC_PRINTF(catalog, LOC_CLI_SERIAL_QUEUED_BYTES_LU_DRIVER_TX_BYTES_LU_DRIVER_RX_BYTES_LU, "Queued Bytes: %lu; Driver TX Bytes: %lu; Driver RX Bytes: %lu\n",
            (unsigned long)stats.queued_bytes, (unsigned long)stats.tx_bytes, (unsigned long)stats.rx_bytes);
        LOC_PRINTF(catalog, LOC_CLI_SERIAL_REJECTED_PACKETS_LU_DROPPED_TX_BYTES_LU_TX_FAILURES_LU_RX_FAILURES_LU, "Rejected Packets: %lu; Dropped TX Bytes: %lu; TX Failures: %lu; RX Failures: %lu\n",
            (unsigned long)stats.rejected_packets, (unsigned long)stats.tx_dropped_bytes,
            (unsigned long)stats.tx_failures, (unsigned long)stats.rx_failures);
        return 0;
    } else if (strcmp(argv[1], "start") == 0) {
        uint32_t freq = SERIAL_MONITOR_PERF_DEFAULT_HZ;
        if (argc == 3) {
            const char *value = argv[2];
            if (!value || !*value) return -1;
            for (const char *p = value; *p; ++p) if (*p < '0' || *p > '9') return -1;
            errno = 0; char *end = NULL;
            unsigned long parsed = strtoul(value, &end, 10);
            if (errno || *end || parsed == 0 || parsed > SERIAL_MONITOR_PERF_MAX_HZ) return -1;
            freq = (uint32_t)parsed;
        }
        if (SerialMonitor_StartPerfTest(freq) == 0) {
            LOC_PRINTF(catalog, LOC_CLI_SERIAL_PERFORMANCE_TEST_STARTED_AT_LU_HZ, "Performance test started at %lu Hz\n", freq);
        } else {
            LOC_PRINTF(catalog, LOC_CLI_SERIAL_FAILED_TO_START_PERFORMANCE_TEST, "Failed to start performance test.\n");
            return -1;
        }
    } else if (strcmp(argv[1], "stop") == 0 && argc == 2) {
        if (SerialMonitor_StopPerfTest() != 0) {
            LOC_PRINTF(catalog, LOC_CLI_SERIAL_FAILED_TO_STOP_PERFORMANCE_TEST, "Failed to stop performance test.\n");
            return -1;
        }
        LOC_PRINTF(catalog, LOC_CLI_SERIAL_PERFORMANCE_TEST_STOPPED_CHECK_LOGS_FOR_RESULTS, "Performance test stopped. Check logs for results.\n");
    } else { return -1; }
    return 0;
}

int SerialMonitor_HandlePerfCommand(int argc, char **argv) {
    if (argc < 2 || argc > 4 || !argv) return -1;
    for (int index = 0; index < argc; ++index)
        if (!argv[index]) return -1;
    bool machine = strcmp(argv[argc - 1], "--machine") == 0;
    if (machine && strcmp(argv[1], "status") != 0 &&
        strcmp(argv[1], "start") != 0 && strcmp(argv[1], "stop") != 0 &&
        strcmp(argv[1], "uart-start") != 0 && strcmp(argv[1], "uart-stop") != 0)
        return -1;
    if (machine && strcmp(argv[1], "status") != 0) {
        int result = DispatchPerfCommand(argc - 1, argv);
        printf("RACOOL_CLI v=1 type=serial_command command=%s result=%s\n",
               argv[1], result == 0 ? "completed" : "rejected");
        return result;
    }
    return DispatchPerfCommand(argc, argv);
}

#ifdef RACOOL_HEALTH_REPORT_DIAGNOSTIC
extern esp_err_t HealthReportDiagnostic_Register(IDebugConsole *console);
#endif

esp_err_t SerialMonitor_RegisterConsoleCommands(void) {
  IDebugConsole *console = (IDebugConsole *)ServiceLocator_GetService(SVC_NAME_DEBUG_CONSOLE);
  static const esp_console_cmd_t perf_cmd = {
      .command = "SER_PERF",
      .help = "Serial Monitor timer test controls and resource/driver statistics",
      .hint = "<start|stop|status|uart-start|uart-stop> [freq_hz]",
      .func = &SerialMonitor_HandlePerfCommand,
      .argtable = NULL
  };

  #ifdef RACOOL_HEALTH_REPORT_DIAGNOSTIC
  esp_err_t diagnostic_result = HealthReportDiagnostic_Register(console);
  if (diagnostic_result != ESP_OK) return diagnostic_result;
#endif
  esp_err_t result;
  if (console && console->RegisterCommandWithCategory) {
    result = console->RegisterCommandWithCategory(console, &perf_cmd, CMD_CAT_NETWORK);
  } else if (console && console->RegisterCommand) {
    result = console->RegisterCommand(console, &perf_cmd);
  } else {
    result = ESP_ERR_INVALID_STATE;
  }
  if (result == ESP_OK) {
    ESP_LOGI(SER_CMD_TAG, "SerialMonitor console commands registered successfully");
  } else {
    ESP_LOGE(SER_CMD_TAG, "SER_PERF registration failed: %ld", (long)result);
  }
  return result;
}
