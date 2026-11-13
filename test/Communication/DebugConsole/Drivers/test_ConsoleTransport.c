#include "Infrastructure/support/test_mocks.h"
#include "unity.h"
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <limits.h>
#include "driver/uart.h"

/* POSIX flags are absent from the Windows host CRT. */
#ifndef F_GETFL
#define F_GETFL 3
#define F_SETFL 4
#define O_NONBLOCK 0x800
#endif
#define UART_NUM_MAX 2
#define UART_DATA 0
typedef struct { int type; } uart_event_t;
static int driver_deletes;
static bool driver_installed;
bool uart_is_driver_installed(int port) { (void)port; return driver_installed; }
esp_err_t uart_driver_delete(int port) { (void)port; driver_deletes++; return ESP_OK; }
int uart_read_bytes(int port, void *data, uint32_t count, TickType_t ticks) {
    (void)port; (void)data; (void)count; (void)ticks; return 0;
}
int uart_write_bytes(int port, const void *data, size_t count) {
    (void)port; (void)data; return (int)count;
}
esp_err_t uart_get_buffered_data_len(int port, size_t *length) {
    (void)port; *length = 0; return ESP_OK;
}

static int get_flags, set_flags, configured_flags;
static ssize_t read_result, write_result;
static int fake_fcntl(int fd, int command, int value) {
    (void)fd;
    if (command == F_GETFL) return get_flags;
    configured_flags = value;
    return set_flags;
}
static ssize_t fake_read(int fd, void *data, size_t count) {
    (void)fd; (void)data; (void)count; return read_result;
}
static ssize_t fake_write(int fd, const void *data, size_t count) {
    (void)fd; (void)data; (void)count; return write_result;
}
#define fcntl fake_fcntl
#define read fake_read
#define write fake_write
#include "Communication/DebugConsole/Drivers/UartConsoleIO.c"
#undef fcntl
#undef read
#undef write

void setUp(void) {
    IConsoleIO *io = StdioConsoleIO_GetDriver();
    io->Deinitialize(io);
    get_flags = O_RDWR; set_flags = configured_flags = 0;
    read_result = write_result = 0;
    driver_deletes = 0;
    driver_installed = false;
}
void tearDown(void) {}
void test_ConsoleTransport_StdioInitializationChecksFlagsAndCanRetry(void) {
    IConsoleIO *io = StdioConsoleIO_GetDriver();
    get_flags = -1;
    TEST_ASSERT_EQUAL(-1, io->Initialize(io));
    get_flags = O_RDWR; set_flags = -1;
    TEST_ASSERT_EQUAL(-1, io->Initialize(io));
    set_flags = 0;
    TEST_ASSERT_EQUAL(0, io->Initialize(io));
    TEST_ASSERT_TRUE(configured_flags & O_NONBLOCK);
    TEST_ASSERT_EQUAL(0, io->Deinitialize(io));
    TEST_ASSERT_EQUAL(0, driver_deletes);
}
void test_ConsoleTransport_NonblockingAndRealErrorsAreDistinct(void) {
    IConsoleIO *io = StdioConsoleIO_GetDriver();
    TEST_ASSERT_EQUAL(0, io->Initialize(io));
    char byte;
    read_result = write_result = -1; errno = EAGAIN;
    TEST_ASSERT_EQUAL(0, io->Read(io, &byte, 1));
    TEST_ASSERT_EQUAL(0, io->Write(io, &byte, 1));
    errno = EIO;
    TEST_ASSERT_EQUAL(-1, io->Read(io, &byte, 1));
    TEST_ASSERT_EQUAL(-1, io->Write(io, &byte, 1));
    read_result = write_result = 1;
    TEST_ASSERT_EQUAL(1, io->Read(io, &byte, 1));
    TEST_ASSERT_EQUAL(1, io->Write(io, &byte, 1));
    TEST_ASSERT_EQUAL(-1, io->Read(io, NULL, 1));
    TEST_ASSERT_EQUAL(-1, io->Write(io, &byte, (size_t)INT_MAX + 1));
    TEST_ASSERT_NULL(UartConsoleIO_GetDriver((uart_port_t)-1));
}
void test_ConsoleTransport_BorrowedUartMustExistAndSurviveDetach(void) {
    IConsoleIO *io = UartConsoleIO_GetDriver(1);
    TEST_ASSERT_EQUAL(-1, io->Initialize(io));
    driver_installed = true;
    TEST_ASSERT_EQUAL(0, io->Initialize(io));
    TEST_ASSERT_EQUAL(0, io->Deinitialize(io));
    TEST_ASSERT_TRUE(driver_installed);
    TEST_ASSERT_EQUAL(0, driver_deletes);
}
