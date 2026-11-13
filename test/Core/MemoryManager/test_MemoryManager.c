#include "unity.h"
#include <stdlib.h>
static unsigned allocationCalls, freeCalls, failAllocationAt;
static void *test_allocate(size_t size) {
    ++allocationCalls;
    return failAllocationAt == allocationCalls ? NULL : malloc(size);
}
static void test_release(void *pointer) { if (pointer) ++freeCalls; free(pointer); }
#define malloc test_allocate
#define free test_release
#include "Core/MemoryManager/MemoryManager.c"
#undef malloc
#undef free
#include "Core/MemoryManager/MemoryConsoleCommands.c"
void setUp(void) {
    IMemoryManager base = *GetMemoryManager();
    memset(&memManager,0,sizeof(memManager)); memManager.base = base;
    RESET_FAKE(mocked_xSemaphoreCreateMutex); RESET_FAKE(mocked_xSemaphoreTake);
    mocked_xSemaphoreCreateMutex_fake.return_val = (SemaphoreHandle_t)1;
    mocked_xSemaphoreTake_fake.return_val = pdTRUE;
    allocationCalls = freeCalls = failAllocationAt = 0;
}
void tearDown(void) {
    for (unsigned i=0;i<MEMORY_MANAGER_MAX_RECORDS;++i) {
        free(memManager.allocs[i].ptr); free(memManager.allocs[i].file);
    }
}
static IMemoryManager *start_memory(void) {
    IMemoryManager *manager = GetMemoryManager();
    TEST_ASSERT_EQUAL(0,manager->base.Initialize(&manager->base,NULL));
    TEST_ASSERT_EQUAL(0,manager->base.Start(&manager->base));
    return manager;
}
void test_MemoryManager_InitializationFailureAndRepeatedInitPreserveOwnership(void) {
    IMemoryManager *manager=GetMemoryManager();
    TEST_ASSERT_EQUAL(-1,manager->base.Start(&manager->base));
    mocked_xSemaphoreCreateMutex_fake.return_val=NULL;
    TEST_ASSERT_EQUAL(-1,manager->base.Initialize(&manager->base,NULL));
    TEST_ASSERT_EQUAL(SERVICE_STATE_UNINITIALIZED,manager->base.GetState(&manager->base));
    mocked_xSemaphoreCreateMutex_fake.return_val=(SemaphoreHandle_t)1;
    start_memory(); void *block=manager->Allocate(15); TEST_ASSERT_NOT_NULL(block);
    unsigned calls=mocked_xSemaphoreCreateMutex_fake.call_count;
    TEST_ASSERT_EQUAL(0,manager->base.Initialize(&manager->base,NULL));
    TEST_ASSERT_EQUAL(calls,mocked_xSemaphoreCreateMutex_fake.call_count);
    size_t total; TEST_ASSERT_EQUAL(0,MemoryManager_GetTotalAllocated(&total)); TEST_ASSERT_EQUAL(15,total);
    manager->Free(block);
}
void test_MemoryManager_CopiesSourceAndSnapshotLifetime(void) {
    IMemoryManager *manager=start_memory(); char source[]="dir/original.c";
    void *block=manager->AllocateTrack(23,source,7); TEST_ASSERT_NOT_NULL(block);
    memset(source,'x',sizeof(source)-1);
    MemoryAllocationRecord snapshot;
    TEST_ASSERT_EQUAL(1,MemoryManager_GetAllocRecords(&snapshot,1));
    manager->Free(block);
    TEST_ASSERT_EQUAL_STRING("original.c",snapshot.file);
    TEST_ASSERT_EQUAL(23,snapshot.size); TEST_ASSERT_EQUAL(7,snapshot.line);
    TEST_ASSERT_EQUAL(0,MemoryManager_GetAllocRecords(&snapshot,1));
}
void test_MemoryManager_RejectsForeignAndRepeatedFree(void) {
    IMemoryManager *manager=start_memory(); void *block=manager->Allocate(4);
    TEST_ASSERT_NOT_NULL(block); manager->Free(block); unsigned calls=freeCalls;
    manager->Free(block); int foreign; manager->Free(&foreign);
    TEST_ASSERT_EQUAL(calls,freeCalls);
}
void test_MemoryManager_LockFailurePreservesOwnersAndReadOutputs(void) {
    IMemoryManager *manager=start_memory(); void *block=manager->Allocate(9);
    TEST_ASSERT_NOT_NULL(block); mocked_xSemaphoreTake_fake.return_val=pdFALSE;
    size_t total=123; TEST_ASSERT_EQUAL(-1,MemoryManager_GetTotalAllocated(&total)); TEST_ASSERT_EQUAL(123,total);
    manager->Free(block); TEST_ASSERT_EQUAL(0,freeCalls);
    TEST_ASSERT_NULL(manager->Allocate(3)); TEST_ASSERT_EQUAL(2,freeCalls);
    MemoryAllocationRecord snapshot; TEST_ASSERT_EQUAL(-1,MemoryManager_GetAllocRecords(&snapshot,1));
    mocked_xSemaphoreTake_fake.return_val=pdTRUE;
    TEST_ASSERT_EQUAL(1,MemoryManager_GetAllocRecords(&snapshot,1)); manager->Free(block);
}
void test_MemoryManager_FullTableAndAllocationFailureRollback(void) {
    IMemoryManager *manager=start_memory(); failAllocationAt=2;
    TEST_ASSERT_NULL(manager->Allocate(4)); TEST_ASSERT_EQUAL(1,freeCalls);
    failAllocationAt=0;
    for (unsigned i=0;i<MEMORY_MANAGER_MAX_RECORDS;++i) TEST_ASSERT_NOT_NULL(manager->Allocate(1));
    unsigned releases=freeCalls;
    TEST_ASSERT_NULL(manager->Allocate(1)); TEST_ASSERT_EQUAL(releases+2,freeCalls);
    size_t total; TEST_ASSERT_EQUAL(0,MemoryManager_GetTotalAllocated(&total));
    TEST_ASSERT_EQUAL(MEMORY_MANAGER_MAX_RECORDS,total);
}
void test_MemoryManager_StopRetainsBlocksAndRejectsNewAllocation(void) {
    IMemoryManager *manager=start_memory(); void *block=manager->Allocate(8);
    TEST_ASSERT_EQUAL(0,manager->base.Stop(&manager->base)); TEST_ASSERT_NULL(manager->Allocate(1));
    manager->Free(block); TEST_ASSERT_EQUAL(0,manager->base.Start(&manager->base));
    size_t total; TEST_ASSERT_EQUAL(0,MemoryManager_GetTotalAllocated(&total)); TEST_ASSERT_EQUAL(0,total);
}
void test_MemoryManager_ValidatesInputsAndConsoleArguments(void) {
    IMemoryManager *manager=start_memory();
    TEST_ASSERT_NULL(manager->Allocate(0));
    char source[MEMORY_MANAGER_SOURCE_LIMIT]; memset(source,'x',sizeof(source));
    TEST_ASSERT_NULL(manager->AllocateTrack(1,source,0));
    TEST_ASSERT_EQUAL(-1,MemoryManager_GetAllocRecords(NULL,1));
    TEST_ASSERT_EQUAL(-1,MemoryManager_GetTotalAllocated(NULL));
    char *extra[]={"MEM","STATS","extra"}; TEST_ASSERT_EQUAL(1,cmd_mem(3,extra));
    char *stats[]={"MEM","STATS"}; mocked_xSemaphoreTake_fake.return_val=pdFALSE;
    TEST_ASSERT_EQUAL(1,cmd_mem(2,stats));
    TEST_ASSERT_EQUAL(1,cmd_mem(2,NULL));
    char *missing[]={"MEM",NULL}; TEST_ASSERT_EQUAL(1,cmd_mem(2,missing));
}

void test_MemoryManager_PeakAndFailuresSurviveFreeStopAndRestart(void) {
    IMemoryManager *manager=start_memory();
    void *first=manager->Allocate(17), *second=manager->Allocate(31);
    TEST_ASSERT_NOT_NULL(first); TEST_ASSERT_NOT_NULL(second);
    manager->Free(first);
    TEST_ASSERT_EQUAL(0,manager->base.Stop(&manager->base));
    TEST_ASSERT_NULL(manager->Allocate(1));
    TEST_ASSERT_EQUAL(0,manager->base.Start(&manager->base));
    failAllocationAt=allocationCalls+2;
    TEST_ASSERT_NULL(manager->Allocate(10));
    MemoryManagerStats stats;
    TEST_ASSERT_EQUAL(0,MemoryManager_GetStats(&stats));
    TEST_ASSERT_EQUAL(31,stats.current_bytes); TEST_ASSERT_EQUAL(48,stats.peak_bytes);
    TEST_ASSERT_EQUAL(1,stats.live_blocks); TEST_ASSERT_EQUAL(2,stats.failed_requests);
    manager->Free(second);
    TEST_ASSERT_EQUAL(0,MemoryManager_GetStats(&stats));
    TEST_ASSERT_EQUAL(0,stats.current_bytes); TEST_ASSERT_EQUAL(48,stats.peak_bytes);
    TEST_ASSERT_EQUAL(0,stats.live_blocks);
}
void test_MemoryManager_FailedStatsReadPreservesCallerSnapshot(void) {
    start_memory(); MemoryManagerStats stats={1,2,3,4},before=stats;
    mocked_xSemaphoreTake_fake.return_val=pdFALSE;
    TEST_ASSERT_EQUAL(-1,MemoryManager_GetStats(&stats));
    TEST_ASSERT_EQUAL_MEMORY(&before,&stats,sizeof(stats));
    TEST_ASSERT_EQUAL(-1,MemoryManager_GetStats(NULL));
}
