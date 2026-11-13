#include "Management/DiagnosticsManager/DiagnosticsManagerConfig.h"
#include "Core/ServiceLocator/ServiceNames.h"
#include <stddef.h>

DiagnosticsManagerConfig DiagnosticsManagerConfig_GetDefault(void) {
    DiagnosticsManagerConfig config = {0};

    // Critical system services monitored sequentially
    config.criticalServices[0] = SVC_NAME_LOG_MANAGER;
    config.criticalServices[1] = SVC_NAME_EVENT_BUS;
    config.criticalServices[2] = SVC_NAME_ERROR_HANDLER;
    config.criticalServices[3] = SVC_NAME_MEMORY_MANAGER;
    config.criticalServices[4] = SVC_NAME_PIN_MANAGER;
    config.criticalServices[5] = SVC_NAME_I2C_MANAGER;
    config.criticalServices[6] = SVC_NAME_OLED;
    config.criticalServices[7] = SVC_NAME_SERIAL_MONITOR;
    config.criticalServices[8] = SVC_NAME_ZIGBEE;

    config.criticalServiceCount = 9;

    config.enablePeriodicTest = true;
    config.periodicIntervalMs = 60000;      // 60 Seconds
    config.serviceCheckTimeoutMs = 100;
    config.minFreeHeapThreshold = 49152;    // 48 KB threshold
    config.minFreeBlockThreshold = 16384;   // 16 KB max contiguous block threshold
    
    config.taskStackSize = 4096;
    config.taskPriority = 5;

    return config;
}

