#include "unity.h"
#include "../../../../src/Communication/SerialMonitor/SerialBuffer.c"
static void *target_borrowed;
void target_serial_buffer_cleanup(void) {
    ISerialBuffer *buffer = SerialBuffer_GetInstance();
    if (target_borrowed) buffer->ReturnItem(buffer, target_borrowed);
    target_borrowed = NULL;
    TEST_ASSERT_EQUAL(0, buffer->Deinitialize(buffer));
}
void target_serial_buffer_verify_contract(void) {
    ISerialBuffer *buffer = SerialBuffer_GetInstance();
    TEST_ASSERT_EQUAL(-1, buffer->Initialize(buffer, 0, 0));
    TEST_ASSERT_EQUAL(0, buffer->Initialize(buffer, 64, 0));
    char data[] = "copy";
    TEST_ASSERT_EQUAL(0, buffer->PushData(buffer, data, sizeof(data), 0));
    memset(data, 'x', sizeof(data));
    size_t length = 0;
    target_borrowed = buffer->PopData(buffer, &length, 0);
    TEST_ASSERT_NOT_NULL(target_borrowed);
    TEST_ASSERT_EQUAL(5, length);
    TEST_ASSERT_EQUAL_MEMORY("copy", target_borrowed, 5);
    TEST_ASSERT_EQUAL(-1, buffer->Deinitialize(buffer));
    buffer->ReturnItem(buffer, target_borrowed); target_borrowed = NULL;
    length = 999;
    TEST_ASSERT_NULL(buffer->PopData(buffer, &length, 0));
    TEST_ASSERT_EQUAL(0, length);
    TEST_ASSERT_EQUAL(0, buffer->Deinitialize(buffer));
    TEST_ASSERT_EQUAL(0, buffer->Initialize(buffer, 128, 0));
}
