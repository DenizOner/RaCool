#ifndef ZIGBEECONFIG_H
#define ZIGBEECONFIG_H

/* ZCL invalid temperature and project analog health-unavailable values. */
#define ZIGBEE_TEMPERATURE_INVALID 0x8000u
#define ZIGBEE_HEALTH_UNAVAILABLE (-1.0f)

#define ZIGBEE_POWER_COMMAND_DELAY_MS 500u
#define ZIGBEE_SYSTEM_CONFIG_CLUSTER 0x0204u
#define ZIGBEE_ATTR_TX_PROFILE 0x8042u
/* Cooling-only thermostat systemMode values. The radiator fan's manual ON
 * selector must use Cool (3), not the unsupported Heat (4). */
#define ZIGBEE_FAN_MODE_OFF 0x00u
#define ZIGBEE_FAN_MODE_AUTO 0x01u
#define ZIGBEE_FAN_MODE_ON 0x03u
#define ZIGBEE_CONTROL_TIMEOUT_MS 1000u
/* SDK registration persists each reportable setting; measured cold registration exceeds 11 s. */
#define ZIGBEE_STARTUP_TIMEOUT_MS 20000u
#define ZIGBEE_STARTUP_WATCHDOG_FEED_MS 1000u
#define ZIGBEE_WORKER_STOP_TIMEOUT_MS 2000u
#define ZIGBEE_ROUTER_REQUEST_CAPACITY 16u
#define ZIGBEE_ROUTER_NEIGHBOR_CAPACITY 64u
#define ZIGBEE_ROUTER_ROUTE_CAPACITY 64u
#define ZIGBEE_ROUTER_RECORD_CAPACITY 32u
#define ZIGBEE_POWER_TASK_STACK_SIZE 4096u
#define ZIGBEE_POWER_TASK_PRIORITY 1u

/** @brief ESP32-C6 native Zigbee configuration for Home Assistant integration. */

// --- Zigbee service configuration ---
#define ZIGBEE_TASK_STACK_SIZE 16384u
#define ZIGBEE_TASK_PRIORITY 5
/* SDK reporting/storage work yields to visual and thermal workers. */
#define ZIGBEE_REPORT_WORKER_PRIORITY 4u
#define ZIGBEE_REPORT_WORKER_STACK_SIZE 6144u
#define ZIGBEE_REPORT_WORKER_POLL_MS 100u

// --- Network configuration ---
#ifndef ZIGBEE_CHANNEL
#define ZIGBEE_CHANNEL 11
#endif
#define RACOOL_ZB_PRIMARY_CHANNELS 0x07FFF800 // All channels 11-26
#define RACOOL_ZB_SECONDARY_CHANNELS 0x00000000

#ifndef ZIGBEE_PAN_ID
#define ZIGBEE_PAN_ID 0x1234
#endif

#ifndef ZIGBEE_EXTENDED_PAN_ID
#define ZIGBEE_EXTENDED_PAN_ID {0xDD, 0xDD, 0xDD, 0xDD, 0xDD, 0xDD, 0xDD, 0xDD}
#endif

#ifndef ZIGBEE_NETWORK_KEY
#define ZIGBEE_NETWORK_KEY                                                     \
  {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x00, 0x11,                             \
   0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99}
#endif

// --- Device configuration ---
#define ZIGBEE_DEVICE_TYPE                                                     \
  0x01 // 0x00: Coordinator, 0x01: Router, 0x02: End Device
#define ZIGBEE_MAX_RETRIES 3
#define ZIGBEE_TX_ECO_DBM 0
#define ZIGBEE_TX_NORMAL_DBM 10
#define ZIGBEE_TX_BOOST_DBM 20

// --- Router configuration ---
#define ZIGBEE_ROUTER_MAX_CHILDREN 20
#define ZIGBEE_ROUTER_LINK_KEY_REQUIRED false

// --- Endpoint identifiers ---
#define ZIGBEE_EP_SENSOR_FAN 1    // Radiator sensor and fan relay
#define ZIGBEE_EP_INTERNAL_LED 2  // Internal LED
#define ZIGBEE_EP_RGB_LED 3       // RGB LED
#define ZIGBEE_EP_AMBIENT_TEMP 4  // Ambient temperature
#define ZIGBEE_EP_CPU_TEMP 5      // CPU temperature
#define ZIGBEE_EP_SYSTEM_HEALTH 6 // System health (Analog Input)
#define ZIGBEE_EP_MEMORY 7        // RAM usage (Analog Input)
#define ZIGBEE_EP_UPTIME 8        // Uptime (Analog Input)
#define ZIGBEE_EP_DISPLAY 9       // OLED settings plus Groups/Scenes compatibility
#define ZIGBEE_EP_GROUPS ZIGBEE_EP_DISPLAY // Legacy factory alias
#define ZIGBEE_EP_SYSTEM 10       // System management and OTA client
#define ZIGBEE_EP_OTA ZIGBEE_EP_SYSTEM
#define ZIGBEE_ATTR_SYSTEM_COMMAND 0x8040
#define ZIGBEE_ATTR_POWER_PROFILE 0x8041
#define ZIGBEE_MAX_ENDPOINTS 16   // Maximum endpoint count

// --- ZCL reporting configuration ---
#define ZIGBEE_REPORT_MIN_INTERVAL 1    // seconds
#define ZIGBEE_REPORT_MAX_INTERVAL 300   // seconds (5 minutes)
#define ZIGBEE_REPORT_TEMP_THRESHOLD 50  // 0.5 degrees in centidegrees
#define ZIGBEE_REPORT_ANALOG_THRESHOLD 1 // 1 unit

// --- Zigbee 3.0 security ---
#ifndef ZIGBEE_USE_INSTALL_CODE
#define ZIGBEE_USE_INSTALL_CODE false
#endif

#ifndef ZIGBEE_INSTALL_CODE
#define ZIGBEE_INSTALL_CODE                                                    \
  {0x83, 0xFE, 0xD3, 0x40, 0x7A, 0x93, 0x97, 0x23,                             \
   0xA5, 0xC6, 0x39, 0xB2, 0x69, 0x16, 0xD5, 0x05}
#endif

// --- Rejoin recovery strategy ---
#define ZIGBEE_REJOIN_ATTEMPTS 10
#define ZIGBEE_REJOIN_BACKOFF_BASE_MS 5000
#define ZIGBEE_REJOIN_MAX_BACKOFF_MS 60000

// --- OTA configuration ---
#define ZIGBEE_OTA_UPGRADE_HARDWARE_VERSION 1
#define ZIGBEE_OTA_UPGRADE_QUERY_INTERVAL 3600 // 1 hour
/* Local DIY/test OTA namespace (shared TEST_VENDOR_1), not a CSA
 * manufacturer allocation. Never publish this identity to a global OTA
 * catalog. Require an exact local image type, model/hardware/version
 * policy and signed ESP app verification before any future installation.
 * 0xFFFF is a wildcard; old 0x1234 was not RaCool's manufacturer code.
 * Switching existing devices requires an authorized signed-base migration;
 * changing this constant does not migrate already installed firmware. */
#define ZIGBEE_OTA_MANUFACTURER_CODE 0xFFF1u
/* Stable RaCool-local image family within the shared DIY test namespace. */
#define ZIGBEE_OTA_IMAGE_TYPE 0x1234u

// --- Firmware version ---
#include "ProductVersion.generated.h"
/* OTA file version is a protocol counter, separate from product SemVer. */
#define ZIGBEE_FIRMWARE_VERSION RACOOL_OTA_FILE_VERSION

// --- Device identification for Zigbee2MQTT discovery ---
#define ZIGBEE_MANUFACTURER_NAME                                               \
  "\x06"                                                                       \
  "RaCool"
#define ZIGBEE_MODEL_IDENTIFIER                                                \
  "\x0E"                                                                       \
  "RaCool-ESP32C6"
#define ZIGBEE_DATE_CODE                                                       \
  "\x08"                                                                       \
  "20260531"
#define ZIGBEE_POWER_SOURCE 0x01 // Mains power source

// --- Mutex timeout ---
#define ZIGBEE_LOCK_TIMEOUT_MS 200
/* Bound burst rate to ten outbound reports/second so SDK transmit buffers can drain. */
#define ZIGBEE_REPORT_APPLY_DELAY_MS 100
#define ZIGBEE_REPORT_MAX_ATTEMPTS 3u
/* Full settings startup plus thirteen fan source diagnostics can exceed 64
 * unique updates before the first SDK drain. Keep bounded static headroom. */
#define ZIGBEE_ATTRIBUTE_QUEUE_CAPACITY 96u
/* Bounded SDK packet headroom for startup controls, telemetry and router traffic. */
#define ZIGBEE_PACKET_BUFFER_CAPACITY 128u
/* Configuration bindings plus telemetry routes exceed the SDK default table. */
#define ZIGBEE_BINDING_TABLE_CAPACITY 32u
#define ZIGBEE_ATTRIBUTE_MAX_BYTES 8u
#define ZIGBEE_ATTRIBUTE_BATCH_SIZE 8u
/* Yield SDK ownership between outbound reports so the stack can release buffers. */
#define ZIGBEE_ATTRIBUTE_REPORT_BATCH_SIZE 1u
/* Bound local setter retries. Rejected report admission retains FIFO delivery
 * ownership until recovery or lifecycle cancellation, with capped backoff. */
#define ZIGBEE_ATTRIBUTE_MAX_ATTEMPTS 3u
/* Transient SDK admission needs recovery time, not three immediate timer ticks. */
#define ZIGBEE_ATTRIBUTE_RETRY_BASE_MS 100u
#define ZIGBEE_ATTRIBUTE_RETRY_MAX_MS 400u
#define ZIGBEE_ATTR_RGB_ZONE_XY_FIRST 0x8030u

#endif // ZIGBEECONFIG_H
