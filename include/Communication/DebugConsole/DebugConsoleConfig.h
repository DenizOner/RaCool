#ifndef DEBUG_CONSOLE_CONFIG_H
#define DEBUG_CONSOLE_CONFIG_H

#include <driver/uart.h>

#ifndef DEBUG_CONSOLE_CONTROL_TIMEOUT_MS
#define DEBUG_CONSOLE_CONTROL_TIMEOUT_MS 1000
#endif

/**
 * @file DebugConsoleConfig.h
 * @brief Debug Console central configuration parameters.
 *
 * All subsystem parameters are managed centrally here.
 * Every parameter is guarded with #ifndef for build-time overrides.
 */

/* ─────────────────────────────────────────────
 * UART Configuration
 * ───────────────────────────────────────────── */
#ifndef DEBUG_CONSOLE_UART_NUM
#define DEBUG_CONSOLE_UART_NUM UART_NUM_0
#endif

#ifndef DEBUG_CONSOLE_UART_BAUD
#define DEBUG_CONSOLE_UART_BAUD 115200
#endif

#ifndef DEBUG_CONSOLE_BUF_SIZE
#define DEBUG_CONSOLE_BUF_SIZE 256
#endif

#ifndef DEBUG_CONSOLE_UART_QUEUE_SIZE
#define DEBUG_CONSOLE_UART_QUEUE_SIZE 20
#endif

/* ─────────────────────────────────────────────
 * RTOS Task Configuration
 * ───────────────────────────────────────────── */
#ifndef DEBUG_CONSOLE_TASK_STACK_SIZE
#define DEBUG_CONSOLE_TASK_STACK_SIZE 8192
#endif

#ifndef DEBUG_CONSOLE_TASK_PRIORITY
#define DEBUG_CONSOLE_TASK_PRIORITY 5
#endif

#ifndef DEBUG_CONSOLE_LOGGER_TASK_PRIORITY
#define DEBUG_CONSOLE_LOGGER_TASK_PRIORITY 2
#endif

#ifndef DEBUG_CONSOLE_LOGGER_TASK_STACK_SIZE
#define DEBUG_CONSOLE_LOGGER_TASK_STACK_SIZE 3072
#endif

#ifndef DEBUG_CONSOLE_LOGGER_QUEUE_SIZE
#define DEBUG_CONSOLE_LOGGER_QUEUE_SIZE 20
#endif

/* ─────────────────────────────────────────────
 * I2C Scanner Configuration
 * ───────────────────────────────────────────── */
#ifndef DEBUG_CONSOLE_I2C_SCAN_PORT
#define DEBUG_CONSOLE_I2C_SCAN_PORT 0
#endif

#ifndef DEBUG_CONSOLE_I2C_SCAN_SDA
#define DEBUG_CONSOLE_I2C_SCAN_SDA 6
#endif

#ifndef DEBUG_CONSOLE_I2C_SCAN_SCL
#define DEBUG_CONSOLE_I2C_SCAN_SCL 7
#endif

/* ─────────────────────────────────────────────
 * Feature Toggles
 * ───────────────────────────────────────────── */
#ifndef DEBUG_CONSOLE_ENABLE_HWTEST
#define DEBUG_CONSOLE_ENABLE_HWTEST 1
#endif

#ifndef DEBUG_CONSOLE_ENABLE_I2C_SCAN
#define DEBUG_CONSOLE_ENABLE_I2C_SCAN 1
#endif

#ifndef DEBUG_CONSOLE_ENABLE_LOGGING
#define DEBUG_CONSOLE_ENABLE_LOGGING 0
#endif

#ifndef DEBUG_CONSOLE_ENABLE_PERFMON
#define DEBUG_CONSOLE_ENABLE_PERFMON 1
#endif

#ifndef DEBUG_CONSOLE_ENABLE_EVENTBUS
#define DEBUG_CONSOLE_ENABLE_EVENTBUS 1
#endif

#ifndef DEBUG_CONSOLE_ENABLE_DASHBOARD
#define DEBUG_CONSOLE_ENABLE_DASHBOARD 1
#endif

#ifndef DEBUG_CONSOLE_ENABLE_NETWORK_CMDS
#define DEBUG_CONSOLE_ENABLE_NETWORK_CMDS 1
#endif

#ifndef DEBUG_CONSOLE_ENABLE_SELF_TEST
#define DEBUG_CONSOLE_ENABLE_SELF_TEST 0
#endif

/* ─────────────────────────────────────────────
 * Flash Logger Configuration
 * ───────────────────────────────────────────── */
#ifndef DEBUG_CONSOLE_FLASH_LOG_PATH
#define DEBUG_CONSOLE_FLASH_LOG_PATH "/littlefs/err_log.txt"
#endif

#ifndef DEBUG_CONSOLE_FLASH_LOG_MAX_SIZE
#define DEBUG_CONSOLE_FLASH_LOG_MAX_SIZE (512 * 1024)
#endif

#ifndef DEBUG_CONSOLE_FLASH_LOG_MAX_LINES
#define DEBUG_CONSOLE_FLASH_LOG_MAX_LINES 2000
#endif

#ifndef DEBUG_CONSOLE_FLASH_MOUNT_POINT
#define DEBUG_CONSOLE_FLASH_MOUNT_POINT "/littlefs"
#endif

/**
 * Flash Logger minimum log level.
 * Only this level and above will be written to flash.
 * ESP_LOG_ERROR=1, ESP_LOG_WARN=2, ESP_LOG_INFO=3
 */
#ifndef DEBUG_CONSOLE_FLASH_LOG_LEVEL
#define DEBUG_CONSOLE_FLASH_LOG_LEVEL 2
#endif

#ifndef DEBUG_CONSOLE_FLASH_LOG_MAX_FILES
#define DEBUG_CONSOLE_FLASH_LOG_MAX_FILES 4
#endif

#ifndef DEBUG_CONSOLE_FLASH_LOG_FLUSH_INTERVAL_MS
#define DEBUG_CONSOLE_FLASH_LOG_FLUSH_INTERVAL_MS 5000
#endif

/* ─────────────────────────────────────────────
 * Command History Configuration
 * ───────────────────────────────────────────── */
#ifndef DEBUG_CONSOLE_HISTORY_SIZE
#define DEBUG_CONSOLE_HISTORY_SIZE 10
#endif

/* ─────────────────────────────────────────────
 * Heartbeat Configuration
 * ───────────────────────────────────────────── */
#ifndef DEBUG_CONSOLE_HEARTBEAT_INTERVAL_MS
#define DEBUG_CONSOLE_HEARTBEAT_INTERVAL_MS 5000
#endif

/* ─────────────────────────────────────────────
 * Dashboard Configuration
 * ───────────────────────────────────────────── */
#ifndef DEBUG_CONSOLE_MAX_PROVIDERS
#define DEBUG_CONSOLE_MAX_PROVIDERS 20
#endif

#ifndef DEBUG_CONSOLE_DASHBOARD_REFRESH_MS
#define DEBUG_CONSOLE_DASHBOARD_REFRESH_MS 1000
#endif

/* ─────────────────────────────────────────────
 * Command Registry Configuration
 * ───────────────────────────────────────────── */
#ifndef DEBUG_CONSOLE_MAX_COMMANDS
#define DEBUG_CONSOLE_MAX_COMMANDS 128
#endif

#ifndef DEBUG_CONSOLE_MAX_CMD_CATEGORIES
#define DEBUG_CONSOLE_MAX_CMD_CATEGORIES 8
#endif

/* ─────────────────────────────────────────────
 * Input Parser Configuration
 * ───────────────────────────────────────────── */
#ifndef DEBUG_CONSOLE_ENABLE_TAB_COMPLETION
#define DEBUG_CONSOLE_ENABLE_TAB_COMPLETION 1
#endif

#ifndef DEBUG_CONSOLE_ENABLE_HISTORY_NAV
#define DEBUG_CONSOLE_ENABLE_HISTORY_NAV 1
#endif

#ifndef DEBUG_CONSOLE_ENABLE_ESCAPE_SEQUENCES
#define DEBUG_CONSOLE_ENABLE_ESCAPE_SEQUENCES 1
#endif

#ifndef DEBUG_CONSOLE_LINE_BUFFER_SIZE
#define DEBUG_CONSOLE_LINE_BUFFER_SIZE 256
#endif

/* ─────────────────────────────────────────────
 * Plugin Configuration
 * ───────────────────────────────────────────── */
#ifndef DEBUG_CONSOLE_MAX_PLUGINS
#define DEBUG_CONSOLE_MAX_PLUGINS 12
#endif

/* ─────────────────────────────────────────────
 * Event Configuration
 * ───────────────────────────────────────────── */
#ifndef DEBUG_CONSOLE_EVENT_QUEUE_SIZE
#define DEBUG_CONSOLE_EVENT_QUEUE_SIZE 8
#endif

/* ─────────────────────────────────────────────
 * Log Filter Configuration
 * ───────────────────────────────────────────── */
#ifndef DEBUG_CONSOLE_MAX_FILTER_TAGS
#define DEBUG_CONSOLE_MAX_FILTER_TAGS 8
#endif




#ifndef DEBUG_CONSOLE_LOGGER_CONTROL_MS
#define DEBUG_CONSOLE_LOGGER_CONTROL_MS 1000u
#endif
#ifndef DEBUG_CONSOLE_LOGGER_LOCK_MS
#define DEBUG_CONSOLE_LOGGER_LOCK_MS 20u
#endif
#ifndef DEBUG_CONSOLE_LOGGER_STOP_MS
#define DEBUG_CONSOLE_LOGGER_STOP_MS 500u
#endif
#ifndef DEBUG_CONSOLE_LOGGER_FLUSH_MS
#define DEBUG_CONSOLE_LOGGER_FLUSH_MS 500u
#endif
#ifndef DEBUG_CONSOLE_LOGGER_POLL_MS
#define DEBUG_CONSOLE_LOGGER_POLL_MS 20u
#endif

#ifndef DEBUG_CONSOLE_RESTART_DELAY_MS
#define DEBUG_CONSOLE_RESTART_DELAY_MS 500u
#endif
#ifndef DEBUG_CONSOLE_LOG_TAG_CAPACITY
#define DEBUG_CONSOLE_LOG_TAG_CAPACITY 32u
#endif
#ifndef DEBUG_CONSOLE_LOGGER_MESSAGE_CAPACITY
#define DEBUG_CONSOLE_LOGGER_MESSAGE_CAPACITY 128u
#endif
#ifndef DEBUG_CONSOLE_LOGGER_LINE_CAPACITY
#define DEBUG_CONSOLE_LOGGER_LINE_CAPACITY (DEBUG_CONSOLE_LOGGER_MESSAGE_CAPACITY + 64u)
#endif
#ifndef DEBUG_CONSOLE_LOGGER_TAIL_LINES
#define DEBUG_CONSOLE_LOGGER_TAIL_LINES 20u
#endif
#ifndef DEBUG_CONSOLE_CAPTURE_MAX_SECONDS
#define DEBUG_CONSOLE_CAPTURE_MAX_SECONDS 120u
#endif
#ifndef DEBUG_CONSOLE_CAPTURE_MAX_RECORDS
#define DEBUG_CONSOLE_CAPTURE_MAX_RECORDS 256u
#endif
#ifndef DEBUG_CONSOLE_CAPTURE_BATCH
#define DEBUG_CONSOLE_CAPTURE_BATCH 8u
#endif
#ifndef DEBUG_CONSOLE_LOGGER_READ_MS
#define DEBUG_CONSOLE_LOGGER_READ_MS 500u
#endif
#endif /* DEBUG_CONSOLE_CONFIG_H */
