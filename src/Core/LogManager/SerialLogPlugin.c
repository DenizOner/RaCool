#include "Communication/SerialMonitor/ISerialMonitorPlugin.h"
#include "Core/LogManager/ILogManager.h"
#include <string.h>
#include <stdio.h>
#include "esp_log.h"

typedef struct {
    ISerialMonitorPlugin base;
    ILogManager *logManager;
} SerialLogPluginImpl;

static void SerialLog_OnDataReceived(ISerialMonitorPlugin *self, const void *data, size_t length) {
    // Optional: Log incoming commands if they represent binary data we care about
    // For now, we focus on transmitted data for log capturing
}

static void SerialLog_OnDataTransmitted(ISerialMonitorPlugin *self, const void *data, size_t length) {
    SerialLogPluginImpl *impl = (SerialLogPluginImpl *)self;
    if (!impl->logManager) return;
    
    // Convert raw transmitted bytes to a log entry (simplified)
    // Note: In a real system, we'd parse this better, but here we treat it
    // as a way to "spy" on console output that esp_log doesn't catch.
    
    // We only capture ASCII-like data for the log buffer
    const char *str = (const char *)data;
    if (length > 0 && (str[0] >= 32 || str[0] == '\n' || str[0] == '\r')) {
        // We could use LogManager->Log but that would cause recursion if LogManager intercepts printf
        // So we might need a direct bridge to the internal buffer or just log to a specific tag
        // impl->logManager->Log(LOG_LEVEL_DEBUG, "SERIAL_OUT", "%.*s", (int)length, str);
    }
}

static int32_t SerialLog_Init(ISerialMonitorPlugin *self) {
    SerialLogPluginImpl *impl = (SerialLogPluginImpl *)self;
    impl->logManager = GetLogManager();
    return 0;
}

static void SerialLog_Deinit(ISerialMonitorPlugin *self) {
    // Nothing to clean up
}

static SerialLogPluginImpl serialLogPlugin = {
    .base = {
        .name = "SerialLogPlugin",
        .OnDataReceived = SerialLog_OnDataReceived,
        .OnDataTransmitted = SerialLog_OnDataTransmitted,
        .Init = SerialLog_Init,
        .Deinit = SerialLog_Deinit
    },
    .logManager = NULL
};

ISerialMonitorPlugin* LogManager_GetSerialPlugin(void) {
    return &serialLogPlugin.base;
}
