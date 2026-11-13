#ifndef TEST_CONSOLE_CAPTURE_H
#define TEST_CONSOLE_CAPTURE_H
#include <stdio.h>
#include <stddef.h>
#include "unity.h"
#ifdef _WIN32
#include <io.h>
#define console_capture_dup _dup
#define console_capture_dup2 _dup2
#define console_capture_close _close
#define console_capture_fileno _fileno
#else
#include <unistd.h>
#define console_capture_dup dup
#define console_capture_dup2 dup2
#define console_capture_close close
#define console_capture_fileno fileno
#endif
static int test_capture_console(int (*handler)(int, char **), int argc, char **argv, char *output, size_t capacity) {
    TEST_ASSERT_TRUE(capacity > 0); fflush(stdout);
    FILE *capture = tmpfile(); TEST_ASSERT_NOT_NULL(capture);
    int saved = console_capture_dup(console_capture_fileno(stdout)); TEST_ASSERT_TRUE(saved >= 0);
    TEST_ASSERT_TRUE(console_capture_dup2(console_capture_fileno(capture), console_capture_fileno(stdout)) >= 0);
    int result = handler(argc, argv); fflush(stdout);
    TEST_ASSERT_TRUE(console_capture_dup2(saved, console_capture_fileno(stdout)) >= 0);
    console_capture_close(saved); rewind(capture);
    size_t length = fread(output, 1, capacity-1, capture); output[length] = 0;
    fclose(capture); return result;
}
#endif
