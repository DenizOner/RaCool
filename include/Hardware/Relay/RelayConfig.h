/**
 * @file RelayConfig.h
 * @brief Configuration settings and safety parameters for the Relay module.
 *
 * Provides parameters for active polarity, anti-chattering delays,
 * anti-short cycle protection, pulse limits, auto-off timeouts, and safety thresholds.
 */

#ifndef RELAY_CONFIG_H
#define RELAY_CONFIG_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Relay Active Polarity configuration.
 * true: Active High (Relay closes on GPIO 1)
 * false: Active Low (Relay closes on GPIO 0)
 */
#define RELAY_ACTIVE_HIGH true

/**
 * @brief Minimum delay between state switches in milliseconds (anti-chattering).
 */
#define RELAY_MIN_SWITCH_INTERVAL_MS 50

/** Bounded public operation admission and nonblocking timer retry cadence. */
#define RELAY_LOCK_TIMEOUT_MS 2500
#define RELAY_TIMER_CHECK_INTERVAL_MS 10

/**
 * @brief Minimum dwell time before switching state back (Anti-Short Cycle protection in ms).
 * Protects connected heavy loads (fans, compressors) from rapid cycling.
 */
#define RELAY_ANTI_SHORT_CYCLE_MIN_MS 2000

/**
 * @brief Maximum allowed pulse duration in milliseconds (Safety cap).
 */
#define RELAY_MAX_PULSE_DURATION_MS 3600000 // 1 hour

/**
 * @brief Default pulse duration in milliseconds when Relay_PulseAsync is called without explicit duration.
 */
#define RELAY_DEFAULT_PULSE_DURATION_MS 1000

/**
 * @brief Default auto-off timeout in milliseconds.
 */
#define RELAY_DEFAULT_AUTO_OFF_TIMEOUT_MS 300000 // 5 minutes

/**
 * @brief Temperature safety thresholds (in Celsius) for automatic fan/relay trigger.
 */
#define RELAY_TEMP_THRESHOLD_LOW 20.0f
#define RELAY_TEMP_THRESHOLD_HIGH 30.0f

/**
 * @brief Default control mode (0: Manual, 1: Automatic based on temperature/events).
 */
#define RELAY_DEFAULT_CONTROL_MODE 1

#ifdef __cplusplus
}
#endif

#endif // RELAY_CONFIG_H
