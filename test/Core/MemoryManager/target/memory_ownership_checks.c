#include "unity.h"
#define GetMemoryManager TargetMemory_GetModule
#define MemoryManager_GetAllocRecords TargetMemory_GetRecords
#define MemoryManager_GetTotalAllocated TargetMemory_GetTotal
#include "../../../../src/Core/MemoryManager/MemoryManager.c"
#undef GetMemoryManager
#undef MemoryManager_GetAllocRecords
#undef MemoryManager_GetTotalAllocated
static SemaphoreHandle_t lockEntered, lockRelease, lockDone;
static TaskHandle_t lockTask;
static void hold_memory_lock(void *argument) {
    (void)argument;
    xSemaphoreTake(memManager.mutex,portMAX_DELAY);
    xSemaphoreGive(lockEntered);
    xSemaphoreTake(lockRelease,portMAX_DELAY);
    xSemaphoreGive(memManager.mutex);
    xSemaphoreGive(lockDone);
    vTaskSuspend(NULL);
}
void target_memory_cleanup(void) {
    if (lockRelease) xSemaphoreGive(lockRelease);
    if (lockTask) {
        TEST_ASSERT_EQUAL(pdTRUE,xSemaphoreTake(lockDone,pdMS_TO_TICKS(3000)));
        vTaskDelete(lockTask); lockTask=NULL;
    }
    for (unsigned i=0;i<MEMORY_MANAGER_MAX_RECORDS;++i)
        if (memManager.allocs[i].ptr) Mem_Free(memManager.allocs[i].ptr);
    if (lockEntered) {vSemaphoreDelete(lockEntered);lockEntered=NULL;}
    if (lockRelease) {vSemaphoreDelete(lockRelease);lockRelease=NULL;}
    if (lockDone) {vSemaphoreDelete(lockDone);lockDone=NULL;}
}
void target_memory_verify_ownership(void) {
    IMemoryManager *manager=TargetMemory_GetModule();
    TEST_ASSERT_EQUAL(0,manager->base.Initialize(&manager->base,NULL));
    TEST_ASSERT_EQUAL(0,manager->base.Start(&manager->base));
    char source[]="temporary.c";
    void *block=manager->AllocateTrack(32,source,5); TEST_ASSERT_NOT_NULL(block);
    memset(source,'x',sizeof(source)-1);
    TEST_ASSERT_EQUAL(0,manager->base.Initialize(&manager->base,NULL));
    lockEntered=xSemaphoreCreateBinary(); lockRelease=xSemaphoreCreateBinary(); lockDone=xSemaphoreCreateBinary();
    TEST_ASSERT_NOT_NULL(lockEntered);TEST_ASSERT_NOT_NULL(lockRelease);TEST_ASSERT_NOT_NULL(lockDone);
    TEST_ASSERT_EQUAL(pdPASS,xTaskCreate(hold_memory_lock,"MemoryLock",2048,NULL,5,&lockTask));
    TEST_ASSERT_EQUAL(pdTRUE,xSemaphoreTake(lockEntered,pdMS_TO_TICKS(2000)));
    manager->Free(block);
    size_t total=123; TEST_ASSERT_EQUAL(-1,TargetMemory_GetTotal(&total)); TEST_ASSERT_EQUAL(123,total);
    xSemaphoreGive(lockRelease);
    TEST_ASSERT_EQUAL(pdTRUE,xSemaphoreTake(lockDone,pdMS_TO_TICKS(2000)));
    vTaskDelete(lockTask);lockTask=NULL;
    MemoryAllocationRecord snapshot;
    TEST_ASSERT_EQUAL(1,TargetMemory_GetRecords(&snapshot,1));
    TEST_ASSERT_EQUAL_STRING("temporary.c",snapshot.file);
    TEST_ASSERT_EQUAL(32,snapshot.size);
    TEST_ASSERT_EQUAL(0,manager->base.Stop(&manager->base));
    TEST_ASSERT_NULL(manager->Allocate(1));
    manager->Free(block);manager->Free(block);
    TEST_ASSERT_EQUAL_STRING("temporary.c",snapshot.file);
    TEST_ASSERT_EQUAL(0,TargetMemory_GetTotal(&total)); TEST_ASSERT_EQUAL(0,total);
    TEST_ASSERT_EQUAL(0,manager->base.Start(&manager->base));
}
