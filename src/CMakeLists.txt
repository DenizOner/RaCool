FILE(GLOB_RECURSE app_sources
     "${CMAKE_CURRENT_LIST_DIR}/Management/ApplicationManager/*.c"
     "${CMAKE_CURRENT_LIST_DIR}/Controllers/ClimateController/*.c"
     "${CMAKE_CURRENT_LIST_DIR}/Controllers/UIController/*.c"
     "${CMAKE_CURRENT_LIST_DIR}/Hardware/BuiltinLED/*.c"
     "${CMAKE_CURRENT_LIST_DIR}/Hardware/LEDAnimations/*.c"
     "${CMAKE_CURRENT_LIST_DIR}/Management/ConfigurationManager/*.c"
     "${CMAKE_CURRENT_LIST_DIR}/Core/EventBus/*.c"
     "${CMAKE_CURRENT_LIST_DIR}/Core/PerformanceMonitor/*.c"
     "${CMAKE_CURRENT_LIST_DIR}/Core/SystemInitializer/*.c"
     "${CMAKE_CURRENT_LIST_DIR}/Communication/DebugConsole/*.c"
     "${CMAKE_CURRENT_LIST_DIR}/Communication/DebugConsole/**/*.c"
     "${CMAKE_CURRENT_LIST_DIR}/Management/DiagnosticsManager/*.c"
     "${CMAKE_CURRENT_LIST_DIR}/Core/ErrorHandler/*.c"
     "${CMAKE_CURRENT_LIST_DIR}/Hardware/InfraredTemperatureSensor/*.c"
     "${CMAKE_CURRENT_LIST_DIR}/Hardware/InternalTemperatureSensor/*.c"
     "${CMAKE_CURRENT_LIST_DIR}/Core/LogManager/*.c"
     "${CMAKE_CURRENT_LIST_DIR}/Core/Localization/*.c"
     "${CMAKE_CURRENT_LIST_DIR}/Core/MemoryManager/*.c"
     "${CMAKE_CURRENT_LIST_DIR}/Controllers/MenuManager/*.c"
     "${CMAKE_CURRENT_LIST_DIR}/Hardware/OLED/*.c"
     "${CMAKE_CURRENT_LIST_DIR}/Hardware/PinManager/*.c"
     "${CMAKE_CURRENT_LIST_DIR}/Hardware/ResourceRegistry/*.c"
     "${CMAKE_CURRENT_LIST_DIR}/Management/PowerManager/*.c"
     "${CMAKE_CURRENT_LIST_DIR}/Hardware/RGBLED/*.c"
     "${CMAKE_CURRENT_LIST_DIR}/Hardware/Relay/*.c"
     "${CMAKE_CURRENT_LIST_DIR}/Hardware/RotaryEncoder/*.c"
     "${CMAKE_CURRENT_LIST_DIR}/Hardware/I2CManager/*.c"
     "${CMAKE_CURRENT_LIST_DIR}/Management/SecurityManager/*.c"
     "${CMAKE_CURRENT_LIST_DIR}/Communication/SerialMonitor/*.c"
     "${CMAKE_CURRENT_LIST_DIR}/Core/ServiceLocator/*.c"
     "${CMAKE_CURRENT_LIST_DIR}/Controllers/SystemController/*.c"
     "${CMAKE_CURRENT_LIST_DIR}/Management/Watchdog/*.c"
     "${CMAKE_CURRENT_LIST_DIR}/Communication/Zigbee/*.c"
     "${CMAKE_CURRENT_LIST_DIR}/Communication/Zigbee/**/*.c"
     "${CMAKE_CURRENT_LIST_DIR}/main.c"
)

# Identify the target test build for diagnostics.
if(UNIT_TEST OR CMAKE_BINARY_DIR MATCHES "test" OR DEFINED PIO_UNIT_TESTING)
    message(STATUS "RaCool: Building in Test Mode")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/../tools/product_resources.cmake")
idf_component_register(SRCS ${app_sources} ${RACOOL_RESOURCE_SOURCES}
                       INCLUDE_DIRS "." "../include" "${RACOOL_RESOURCE_OUTPUT}" "${RACOOL_VERSION_OUTPUT}"
                       REQUIRES nvs_flash esp_event freertos log esp_common esp_system esp_timer driver esp_rom console espressif__esp-zigbee-lib espressif__esp-zboss-lib joltwallet__littlefs espressif__led_strip esp_driver_rmt esp_driver_pcnt esp_driver_i2s esp_adc esp_driver_gptimer esp_driver_tsens)

# ESP-IDF owns newlib syscalls and per-task reentrancy; no application overrides.

option(RACOOL_I2C_WAIT_TRACE "Instrument SDK semaphore waits in a diagnostic image" OFF)
if(RACOOL_I2C_WAIT_TRACE)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE RACOOL_I2C_WAIT_TRACE=1)
    target_sources(${COMPONENT_LIB} PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../test/Hardware/I2CManager/diagnostic/i2c_wait_trace.c")
    target_link_options(${COMPONENT_LIB} INTERFACE
        "-Wl,--wrap=xQueueSemaphoreTake" "-Wl,--wrap=i2c_master_transmit_receive"
        "-Wl,--wrap=i2c_master_transmit")
endif()

option(RACOOL_HEALTH_REPORT_DIAGNOSTIC "Enable a temporary real-service health reporting trigger" OFF)
if(RACOOL_HEALTH_REPORT_DIAGNOSTIC)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE RACOOL_HEALTH_REPORT_DIAGNOSTIC=1)
    target_sources(${COMPONENT_LIB} PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../test/Communication/SerialMonitor/diagnostic/health_report_command.c")
endif()

option(RACOOL_STARTUP_SENTINEL_DIAGNOSTIC "Hold CPU/health Zigbee publication for a bounded startup wire probe" OFF)
if(RACOOL_STARTUP_SENTINEL_DIAGNOSTIC)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE RACOOL_STARTUP_SENTINEL_DIAGNOSTIC=1)
endif()

option(RACOOL_BOOT_FRAME_DIAGNOSTIC "Extend the logo phase for checked USB pixel capture" OFF)
if(RACOOL_BOOT_FRAME_DIAGNOSTIC)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE RACOOL_BOOT_FRAME_DIAGNOSTIC=1)
endif()

option(RACOOL_LED_PHASE_DIAGNOSTIC "Trace bounded LED startup phase and output samples" OFF)
if(RACOOL_LED_PHASE_DIAGNOSTIC)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE RACOOL_LED_PHASE_DIAGNOSTIC=1)
endif()

option(RACOOL_ZIGBEE_REPORT_DIAGNOSTIC "Trace SDK report error conversion in a temporary image" OFF)
if(RACOOL_ZIGBEE_REPORT_DIAGNOSTIC)
    target_sources(${COMPONENT_LIB} PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../test/Communication/Zigbee/diagnostic/report_trace.c")
    target_link_options(${COMPONENT_LIB} INTERFACE
        "-Wl,--wrap=ezb_zcl_report_attr_cmd_req" "-Wl,--wrap=zcl_packet_init"
        "-Wl,--wrap=zcl_packet_send" "-Wl,--wrap=aps_apsde_data_request"
        "-Wl,--wrap=zmsg_alloc" "-Wl,--wrap=zmsg_clone" "-Wl,--wrap=zmsg_add_footer" "-Wl,--wrap=zcl_report_attr_cmd_req"
        "-Wl,--wrap=mempool_malloc" "-Wl,--wrap=mempool_free")
endif()

option(RACOOL_ZIGBEE_REPORT_LEGACY_CONTEXT_DIAGNOSTIC "Negative control: restore the old shared-timer report context" OFF)
if(RACOOL_ZIGBEE_REPORT_LEGACY_CONTEXT_DIAGNOSTIC)
    if(NOT RACOOL_ZIGBEE_REPORT_DIAGNOSTIC)
        message(FATAL_ERROR "Legacy report context requires the SDK report diagnostic")
    endif()
    set_property(SOURCE "${CMAKE_CURRENT_LIST_DIR}/Communication/Zigbee/Zigbee.c"
        APPEND PROPERTY COMPILE_DEFINITIONS RACOOL_ZIGBEE_REPORT_LEGACY_CONTEXT_DIAGNOSTIC=1)
endif()
