cmake_minimum_required(VERSION 3.16)

if(NOT PYTHON)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    set(PYTHON "${Python3_EXECUTABLE}")
endif()
set(RACOOL_VERSION_OUTPUT "${CMAKE_BINARY_DIR}/racool_version")
execute_process(COMMAND "${PYTHON}" "${CMAKE_SOURCE_DIR}/tools/product_version.py" generate
    --manifest "${CMAKE_SOURCE_DIR}/resources/product-version.json" --root "${CMAKE_SOURCE_DIR}"
    --output "${RACOOL_VERSION_OUTPUT}"
    RESULT_VARIABLE RACOOL_VERSION_RESULT ERROR_VARIABLE RACOOL_VERSION_ERROR)
if(NOT RACOOL_VERSION_RESULT EQUAL 0 OR NOT RACOOL_VERSION_ERROR STREQUAL "")
    message(FATAL_ERROR "Product version generation failed: ${RACOOL_VERSION_ERROR}")
endif()
include("${RACOOL_VERSION_OUTPUT}/ProductVersion.generated.cmake")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/resources/product-version.json" "${CMAKE_SOURCE_DIR}/tools/product_version.py")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(RaCool)

# SDK 2.0.4 discards native radio init/deinit return values; preserve them for its owner.
target_link_options(${CMAKE_PROJECT_NAME}.elf PRIVATE
                   "-Wl,--wrap=esp_ieee802154_enable"
                   "-Wl,--wrap=esp_ieee802154_disable")

# Make our shadow headers available to ALL components (including managed components)
idf_build_set_property(INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/include" APPEND)
