/**
 * @file lv_conf.h
 * LVGL configuration file for RaCool project (Updated for v9.6.0)
 */

#ifndef LV_CONF_H
#define LV_CONF_H
#define LV_USE_DRAW_SW_ASM_HELIUM 0
#define LV_USE_DRAW_SW_ASM_NEON   0
#define LV_USE_DRAW_SW_ASM_CUSTOM 0

#include <stdint.h>

/*====================
   COLOR SETTINGS
 *====================*/

/*Preserve the RGB565 default; OLED explicitly selects its I1 display format.*/
#define LV_COLOR_FORMAT_DEFAULT LV_COLOR_FORMAT_RGB565

/* Preserve 4-bpp font strokes when rendering to the monochrome OLED.
 * Montserrat 10 uses alpha 119 for the right stem of M; the default
 * I1 luminance threshold (127) removes that stem entirely. */
#define LV_DRAW_SW_I1_LUM_THRESHOLD 112

/*=========================
   STDLIB SETTINGS
 *=========================*/

/*The default implementation of memory management, string, and sprintf functions.*/
#define LV_USE_STDLIB_MALLOC    LV_STDLIB_BUILTIN
#define LV_USE_STDLIB_STRING    LV_STDLIB_BUILTIN
#define LV_USE_STDLIB_SPRINTF  LV_STDLIB_BUILTIN

/*=========================
   MEMORY SETTINGS
 *=========================*/

/*Size of the memory used by `lv_malloc()` in bytes (>= 2kB)*/
#define LV_MEM_SIZE (48U * 1024U)          /*[bytes]*/

/*Adresses for the memory pool and a custom allocator can be set here too*/
#define LV_MEM_ADR          0              /*0: unused*/

/*====================
   HAL SETTINGS
 *====================*/

/*Stride alignment for draw buffers (bytes). Required for correct I1 buffer sizing.*/
#define LV_DRAW_BUF_ALIGN               4

/*Default display refresh period. LVGL will redraw the screen this many times a second.*/
#define LV_DEF_REFR_PERIOD  30      /*[ms]*/

/*Input device read period in milliseconds*/
#define LV_INDEV_DEF_READ_PERIOD 30 /*[ms]*/

/*Use a custom tick source that tells the elapsed time in milliseconds.
 *It removes the need to manually update the tick with `lv_tick_inc()`)*/
#define LV_TICK_CUSTOM 0

/*Default Dot Per Inch. Used to initialize default sizes such as widgets sized, style paddings.
 *(Not so important, you can adjust it to modify default sizes and spacings)*/
#define LV_DPI_DEF 130              /*[px/inch]*/

/*=========================
   OPERATING SYSTEM
 *=========================*/
/*Select an operating system to use. Possible values:
 *LV_OS_NONE, LV_OS_PTHREAD, LV_OS_FREERTOS, LV_OS_CMSIS_RTOS2, LV_OS_RTTHREAD, LV_OS_WINDOWS, LV_OS_MQX, LV_OS_CUSTOM*/
#define LV_USE_OS   LV_OS_NONE

/*========================
 * RENDERING CONFIGURATION
 *========================*/

/*Maximal horizontal and vertical resolution to support by the library.*/
#define LV_HOR_RES_MAX          (128)
#define LV_VER_RES_MAX          (64)

/*=======================
 * FEATURE CONFIGURATION
 *=======================*/

/*-------------
 * Logging
 *-----------*/

/*Enable the log module*/
#define LV_USE_LOG 1
#if LV_USE_LOG

/*How important log should be added:
 *LV_LOG_LEVEL_TRACE       A lot of logs to give detailed information
 *LV_LOG_LEVEL_INFO        Log important events
 *LV_LOG_LEVEL_WARN        Log if something unwanted happened but didn't cause a problem
 *LV_LOG_LEVEL_ERROR       Only critical issue, when the system may fail
 *LV_LOG_LEVEL_USER        Only logs added by the user
 *LV_LOG_LEVEL_NONE        Do not log anything*/
#define LV_LOG_LEVEL LV_LOG_LEVEL_WARN

/*1: Print the log with 'printf';
 *0: User need to register a callback with `lv_log_register_print_cb()`*/
#define LV_LOG_PRINTF 1

#endif /*LV_USE_LOG*/

/*-------------
 * Asserts
 *-----------*/

/*Enable asserts if an operation is failed or an invalid data is found.
 *If LV_USE_LOG is enabled an error message will be printed on failure*/
#define LV_USE_ASSERT_NULL          1   /*Check if the parameter is NULL. (Very fast, recommended)*/
#define LV_USE_ASSERT_MALLOC        1   /*Checks is the memory is successfully allocated or no. (Very fast, recommended)*/
#define LV_USE_ASSERT_STYLE         0   /*Check if the styles are properly initialized. (Very fast, recommended)*/
#define LV_USE_ASSERT_MEM_INTEGRITY 0   /*Check the integrity of `lv_mem` after critical operations. (Slow)*/
#define LV_USE_ASSERT_OBJ           0   /*Check if the object is valid (e.g. not deleted) after the operations. (Slow)*/

/*Add a custom handler when assert happens e.g. to restart the MCU*/
#define LV_ASSERT_USE_CUSTOM_INCLUDE 1
#define LV_ASSERT_CUSTOM_INCLUDE <stdlib.h>
/* Keep assertions active and preserve a panic backtrace instead of busy-spinning. */
#define LV_ASSERT_HANDLER abort();

/*==================
 *   FONT USAGE
 *===================*/

/*Montserrat fonts with ASCII range and some symbols using bpp = 4
 *https://fonts.google.com/specimen/Montserrat*/
#define LV_FONT_MONTSERRAT_10 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_20 1

/*Always set a default font*/
#define LV_FONT_DEFAULT &lv_font_montserrat_14

/*=================
 *  TEXT SETTINGS
 *=================*/

#define LV_TXT_ENC LV_TXT_ENC_UTF8
#define LV_TXT_BREAK_CHARS " ,.;:-_"
#define LV_TXT_LINE_BREAK_LONG_LEN 0
#define LV_TXT_LINE_BREAK_LONG_PRE_MIN_LEN 3
#define LV_TXT_LINE_BREAK_LONG_POST_MIN_LEN 3
#define LV_TXT_COLOR_CMD "#"

/*==================
 *  WIDGET USAGE
 *================*/

#define LV_USE_ARC          1
#define LV_USE_BAR          1
#define LV_USE_BUTTON       1
#define LV_USE_BTNMATRIX    1
#define LV_USE_CANVAS       1
#define LV_USE_CHECKBOX     1
#define LV_USE_DROPDOWN     1
#define LV_USE_IMAGE        1
#define LV_USE_LABEL        1
#define LV_USE_LINE         1
#define LV_USE_ROLLER       1
#define LV_USE_SLIDER       1
#define LV_USE_SWITCH       1
#define LV_USE_TEXTAREA     1
#define LV_USE_TABLE        1

/*==================
 * EXTRA FEATURES
 *==================*/

/*-----------
 * Themes
 *----------*/

#define LV_USE_THEME_DEFAULT 1
#if LV_USE_THEME_DEFAULT
    #define LV_THEME_DEFAULT_DARK 1
    #define LV_THEME_DEFAULT_GROW 1
    #define LV_THEME_DEFAULT_TRANSITION_TIME 80
#endif /*LV_USE_THEME_DEFAULT*/

#define LV_USE_THEME_BASIC 1

/*-----------
 * Layouts
 *----------*/

#define LV_USE_FLEX 1
#define LV_USE_GRID 1

/*==================
 * EXAMPLES
 *==================*/

/*Enable the examples to be built with the library*/
#define LV_BUILD_EXAMPLES 0

/*===================
 * DEMO USAGE
 ====================*/

#define LV_USE_DEMO_WIDGETS 0

/* Version metadata is provided by LVGL's own public headers. */

#endif /*LV_CONF_H*/
