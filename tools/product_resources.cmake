# Build-local generated resources; no mutable artifacts in source directories.
if(CMAKE_SCRIPT_MODE_FILE)
    # ESP-IDF first includes every component in script mode to collect dependencies.
    # Generation and CONFIGURE_DEPENDS belong to the later real configure pass.
    return()
endif()
get_filename_component(RACOOL_RESOURCE_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(RACOOL_RESOURCE_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/racool_resources")
file(GLOB RACOOL_CATALOG_INPUTS CONFIGURE_DEPENDS
     "${RACOOL_RESOURCE_ROOT}/resources/translations/*.json")
file(GLOB_RECURSE RACOOL_FORMAT_INPUTS CONFIGURE_DEPENDS
     "${RACOOL_RESOURCE_ROOT}/src/*.c")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    ${RACOOL_CATALOG_INPUTS}
    ${RACOOL_FORMAT_INPUTS}
    "${RACOOL_RESOURCE_ROOT}/tools/generate_translations.py"
    "${RACOOL_RESOURCE_ROOT}/tools/generate_oled_fonts.mjs"
    "${RACOOL_RESOURCE_ROOT}/resources/fonts/NotoSans-SemiBold.ttf"
    "${RACOOL_RESOURCE_ROOT}/package-lock.json")
find_program(RACOOL_NODE_EXECUTABLE NAMES node)
if(NOT RACOOL_NODE_EXECUTABLE)
    message(FATAL_ERROR "Node.js is required for reproducible OLED font generation")
endif()
if(NOT PYTHON)
    message(FATAL_ERROR "ESP-IDF Python executable is required for product catalogs")
endif()
execute_process(COMMAND "${PYTHON}" "${RACOOL_RESOURCE_ROOT}/tools/generate_translations.py"
    "${RACOOL_RESOURCE_ROOT}/resources/translations" "${RACOOL_RESOURCE_OUTPUT}"
    RESULT_VARIABLE RACOOL_CATALOG_RESULT OUTPUT_VARIABLE RACOOL_CATALOG_OUTPUT
    ERROR_VARIABLE RACOOL_CATALOG_ERROR)
if(NOT RACOOL_CATALOG_RESULT EQUAL 0 OR NOT RACOOL_CATALOG_ERROR STREQUAL "")
    message(FATAL_ERROR "Catalog generation failed: ${RACOOL_CATALOG_OUTPUT}${RACOOL_CATALOG_ERROR}")
endif()
execute_process(COMMAND "${RACOOL_NODE_EXECUTABLE}" "${RACOOL_RESOURCE_ROOT}/tools/generate_oled_fonts.mjs"
    "${RACOOL_RESOURCE_OUTPUT}" "${RACOOL_RESOURCE_OUTPUT}/catalog-characters.txt"
    RESULT_VARIABLE RACOOL_FONT_RESULT OUTPUT_VARIABLE RACOOL_FONT_OUTPUT
    ERROR_VARIABLE RACOOL_FONT_ERROR)
if(NOT RACOOL_FONT_RESULT EQUAL 0 OR NOT RACOOL_FONT_ERROR STREQUAL "")
    message(FATAL_ERROR "Font generation failed: ${RACOOL_FONT_OUTPUT}${RACOOL_FONT_ERROR}")
endif()
set(RACOOL_RESOURCE_SOURCES
    "${RACOOL_RESOURCE_OUTPUT}/LocalizationCatalog.generated.c"
    "${RACOOL_RESOURCE_OUTPUT}/racool_font_10.c"
    "${RACOOL_RESOURCE_OUTPUT}/racool_font_14.c"
    "${RACOOL_RESOURCE_OUTPUT}/racool_font_20.c")
