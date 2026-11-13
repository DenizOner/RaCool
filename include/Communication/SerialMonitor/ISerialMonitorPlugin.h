#ifndef ISERIAL_MONITOR_PLUGIN_H
#define ISERIAL_MONITOR_PLUGIN_H

#include <stdint.h>
#include <stddef.h>

/**
 * @brief Serial Monitor Plugin Interface
 * Allows external modules to subscribe to UART events (Receive/Transmit)
 * and register custom CLI commands.
 * All hooks run in task context under the plugin registry lock and must be
 * bounded. Recursive AddPlugin/RemovePlugin and blocking lifecycle calls are
 * rejected immediately. Nonblocking WriteRaw/WriteLog remains available.
 * Keep this object and its fields valid and unchanged until RemovePlugin
 * succeeds; success waits for outstanding hooks and completes Deinit. On a
 * removal timeout the plugin remains registered and its storage must survive.
 * Init must roll back its own resources if it returns an error.
 */
typedef struct ISerialMonitorPlugin ISerialMonitorPlugin;

struct ISerialMonitorPlugin {
    /**
     * @brief Unique name of the plugin
     */
    const char* name;

    /**
     * @brief Called when new data is received from UART
     * @param data Received data pointer
     * @param length Data length
     */
    void (*OnDataReceived)(ISerialMonitorPlugin *self, const void *data, size_t length);

    /**
     * @brief Called after the UART driver accepts a nonempty prefix of an item.
     * This is not physical TX completion. Short writes advance through the
     * remaining suffix; error, zero progress or an invalid count discards the
     * unsent suffix. No callback is made when no bytes were accepted.
     * @param data Borrowed item pointer, valid only during this callback.
     * @param length Number of bytes accepted by the driver.
     */
    void (*OnDataTransmitted)(ISerialMonitorPlugin *self, const void *data, size_t length);

    /**
     * @brief Initialization hook for the plugin
     * @return 0 Success, -1 Error
     */
    int32_t (*Init)(ISerialMonitorPlugin *self);

    /**
     * @brief Deinitialization hook
     */
    void (*Deinit)(ISerialMonitorPlugin *self);
};

#endif // ISERIAL_MONITOR_PLUGIN_H
