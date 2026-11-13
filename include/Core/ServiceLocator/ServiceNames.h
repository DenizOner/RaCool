#ifndef SERVICE_NAMES_H
#define SERVICE_NAMES_H

/**
 * @file ServiceNames.h
 * @brief ServiceLocator servis kayıt isimleri (Single Source of Truth)
 *
 * Tüm modüllerin ServiceLocator'a kayıt ve sorgulama sırasında kullandığı
 * servis isimlerini merkezi olarak tanımlar. Yazım hatası riskini ortadan
 * kaldırır ve bakımı kolaylaştırır.
 *
 * Kullanım:
 *   ServiceLocator_RegisterService(SVC_NAME_RELAY, (IService *)relay);
 *   IRelay *r = (IRelay *)ServiceLocator_GetService(SVC_NAME_RELAY);
 */

// --- Core Services ---
#define SVC_NAME_LOG_MANAGER "LogManager"
#define SVC_NAME_ERROR_HANDLER "ErrorHandler"
#define SVC_NAME_MEMORY_MANAGER "MemoryManager"
#define SVC_NAME_EVENT_BUS "EventBus"
#define SVC_NAME_PERFORMANCE_MONITOR "PerformanceMonitor"
#define SVC_NAME_I2C_MANAGER "I2CManager"
#define SVC_NAME_DEBUG_CONSOLE "DebugConsole"

// --- Configuration & Management ---
#define SVC_NAME_CONFIGURATION_MANAGER "ConfigurationManager"
#define SVC_NAME_RESOURCE_REGISTRY "ResourceRegistry"
#define SVC_NAME_PIN_MANAGER "PinManager"
#define SVC_NAME_DIAGNOSTICS_MANAGER "DiagnosticsManager"
#define SVC_NAME_POWER_MANAGER "PowerManager"
#define SVC_NAME_SECURITY_MANAGER "SecurityManager"
#define SVC_NAME_WATCHDOG "Watchdog"

// --- Communication ---
#define SVC_NAME_ZIGBEE "Zigbee"
#define SVC_NAME_SERIAL_MONITOR "SerialMonitor"

// --- Hardware ---
#define SVC_NAME_RELAY "Relay"
#define SVC_NAME_BUILTIN_LED "BuiltinLED"
#define SVC_NAME_RGB_LED "RGBLED"
#define SVC_NAME_OLED "OLED"
#define SVC_NAME_ROTARY_ENCODER "RotaryEncoder"
#define SVC_NAME_IR_TEMP_SENSOR "InfraredTemperatureSensor"
#define SVC_NAME_INTERNAL_TEMP_SENSOR "InternalTemperatureSensor"

// --- Controllers ---
#define SVC_NAME_CLIMATE_CONTROLLER "ClimateController"
#define SVC_NAME_MENU_MANAGER "MenuManager"
#define SVC_NAME_UI_CONTROLLER "UIController"
#define SVC_NAME_SYSTEM_CONTROLLER "SystemController"
#define SVC_NAME_APPLICATION_MANAGER "ApplicationManager"

#endif // SERVICE_NAMES_H
