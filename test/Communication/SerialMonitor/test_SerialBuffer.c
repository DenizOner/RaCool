#include "unity.h"
#include "freertos/ringbuf.h"
#include <stdbool.h>
#include <string.h>
static unsigned creates, deletes, sends, receives, returns;
static bool fail_create, fail_send, has_item;
static size_t max_item;
static TickType_t last_wait;
static unsigned char storage[64];
static size_t stored_size;
RingbufHandle_t xRingbufferCreate(size_t size, RingbufferType_t type) {
  (void)size; TEST_ASSERT_EQUAL(RINGBUF_TYPE_NOSPLIT, type); creates++;
  return fail_create ? NULL : (RingbufHandle_t)0x123;
}
size_t xRingbufferGetMaxItemSize(RingbufHandle_t buffer) { (void)buffer; return max_item; }
void vRingbufferDelete(RingbufHandle_t buffer) { (void)buffer; deletes++; }
BaseType_t xRingbufferSend(RingbufHandle_t buffer, const void *data, size_t length, TickType_t wait) {
  (void)buffer; sends++; last_wait=wait;
  if (fail_send) return pdFALSE;
  memcpy(storage,data,length); stored_size=length; has_item=true; return pdTRUE;
}
void *xRingbufferReceive(RingbufHandle_t buffer, size_t *size, TickType_t wait) {
  (void)buffer; receives++; last_wait=wait;
  if (!has_item) return NULL;
  *size=stored_size; has_item=false; return storage;
}
void vRingbufferReturnItem(RingbufHandle_t buffer, void *item) {
  (void)buffer; TEST_ASSERT_EQUAL_PTR(storage,item); returns++;
}
#include "Communication/SerialMonitor/SerialBuffer.c"
void setUp(void) {
  ISerialBuffer api=serialBufferInstance.base;
  memset(&serialBufferInstance,0,sizeof(serialBufferInstance)); serialBufferInstance.base=api;
  creates=deletes=sends=receives=returns=0; fail_create=fail_send=has_item=false;
  max_item=24; stored_size=0; last_wait=0;
}
void tearDown(void) {}
void test_SerialBuffer_InitializationIsValidatedAndRetryable(void) {
  ISerialBuffer *b=SerialBuffer_GetInstance();
  TEST_ASSERT_EQUAL(-1,b->Initialize(NULL,64,0));
  TEST_ASSERT_EQUAL(-1,b->Initialize(b,0,0));
  TEST_ASSERT_EQUAL(-1,b->Initialize(b,SIZE_MAX,0));
  TEST_ASSERT_EQUAL(-1,b->Initialize(b,64,1));
  TEST_ASSERT_EQUAL(0,creates);
  fail_create=true; TEST_ASSERT_EQUAL(-1,b->Initialize(b,64,0));
  TEST_ASSERT_FALSE(serialBufferInstance.initialized);
  fail_create=false; max_item=0; TEST_ASSERT_EQUAL(-1,b->Initialize(b,64,0));
  TEST_ASSERT_EQUAL(1,deletes); TEST_ASSERT_NULL(serialBufferInstance.ring_buffer);
  max_item=24; TEST_ASSERT_EQUAL(0,b->Initialize(b,64,0));
  TEST_ASSERT_EQUAL(0,b->Initialize(b,64,0)); TEST_ASSERT_EQUAL(3,creates);
  TEST_ASSERT_EQUAL(-1,b->Initialize(b,128,0)); TEST_ASSERT_EQUAL(3,creates);
  TEST_ASSERT_EQUAL(0,b->Deinitialize(b)); TEST_ASSERT_EQUAL(0,b->Deinitialize(b));
  TEST_ASSERT_EQUAL(2,deletes);
}
void test_SerialBuffer_InvalidInputNeverReachesDriver(void) {
  ISerialBuffer *b=SerialBuffer_GetInstance(); size_t size=99;
  TEST_ASSERT_EQUAL(-1,b->PushData(b,"x",1,0));
  TEST_ASSERT_NULL(b->PopData(b,&size,0)); TEST_ASSERT_EQUAL(0,size);
  TEST_ASSERT_EQUAL(-1,b->Deinitialize(NULL)); b->ReturnItem(NULL,NULL);
  TEST_ASSERT_EQUAL(0,b->Initialize(b,64,0));
  TEST_ASSERT_EQUAL(-1,b->PushData(NULL,"x",1,0));
  TEST_ASSERT_EQUAL(-1,b->PushData(b,NULL,1,0));
  TEST_ASSERT_EQUAL(-1,b->PushData(b,"x",0,0));
  TEST_ASSERT_EQUAL(-1,b->PushData(b,"x",SIZE_MAX,0));
  TEST_ASSERT_EQUAL(-1,b->PushData(b,storage,25,0));
  TEST_ASSERT_NULL(b->PopData(b,NULL,0));
  TEST_ASSERT_NULL(b->PopData(NULL,&size,0));
  b->ReturnItem(b,NULL); b->ReturnItem(b,storage);
  TEST_ASSERT_EQUAL(0,sends); TEST_ASSERT_EQUAL(0,receives); TEST_ASSERT_EQUAL(0,returns);
}
void test_SerialBuffer_BorrowedItemPreventsDestruction(void) {
  ISerialBuffer *b=SerialBuffer_GetInstance(); size_t size;
  TEST_ASSERT_EQUAL(0,b->Initialize(b,64,0));
  TEST_ASSERT_EQUAL(0,b->PushData(b,"abc",3,0));
  void *item=b->PopData(b,&size,0); TEST_ASSERT_EQUAL(3,size);
  TEST_ASSERT_EQUAL(-1,b->Deinitialize(b)); TEST_ASSERT_EQUAL(0,deletes);
  TEST_ASSERT_EQUAL_MEMORY("abc",item,3);
  b->ReturnItem(b,item); TEST_ASSERT_EQUAL(1,returns);
  TEST_ASSERT_EQUAL(0,b->Deinitialize(b)); TEST_ASSERT_EQUAL(1,deletes);
  TEST_ASSERT_EQUAL(0,b->Initialize(b,128,0));
}
void test_SerialBuffer_EmptyReadAndFiniteTimeout(void) {
  ISerialBuffer *b=SerialBuffer_GetInstance(); size_t size=99;
  TEST_ASSERT_EQUAL(0,b->Initialize(b,64,0));
  TEST_ASSERT_NULL(b->PopData(b,&size,17)); TEST_ASSERT_EQUAL(0,size);
  TEST_ASSERT_EQUAL(17,last_wait);
  TEST_ASSERT_EQUAL(-1,b->PushData(b,"x",1,UINT32_MAX));
  TEST_ASSERT_NULL(b->PopData(b,&size,UINT32_MAX));
  TEST_ASSERT_EQUAL(1,receives); TEST_ASSERT_EQUAL(0,sends);
}
void test_SerialBuffer_SendFailureLeavesNoBorrowedItem(void) {
  ISerialBuffer *b=SerialBuffer_GetInstance();
  TEST_ASSERT_EQUAL(0,b->Initialize(b,64,0)); fail_send=true;
  TEST_ASSERT_EQUAL(-1,b->PushData(b,storage,24,3)); TEST_ASSERT_EQUAL(3,last_wait);
  TEST_ASSERT_EQUAL(0,b->Deinitialize(b)); TEST_ASSERT_EQUAL(1,deletes);
}
