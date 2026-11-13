#include "test_mocks.h"
#include "Infrastructure/support/console_registration_fixture.h"
#include "Core/MemoryManager/MemoryConsoleCommands.c"
/* Registration tests do not execute the diagnostic handlers. */
IMemoryManager *GetMemoryManager(void) { return NULL; }
int MemoryManager_GetAllocRecords(MemoryAllocationRecord *dest, int max_count) {
    (void)dest; (void)max_count; return 0;
}
int32_t MemoryManager_GetTotalAllocated(size_t *total) { *total = 0; return 0; }
int32_t MemoryManager_GetStats(MemoryManagerStats *out) { *out = (MemoryManagerStats){0}; return 0; }
void setUp(void) { registration_calls = registration_fail_at = 0; }
void tearDown(void) {}
void test_MemoryRegistration_RejectsMissingBoundary(void) {
    check_registration_inputs(MemoryConsoleCommands_Register);
}
void test_MemoryRegistration_PreservesFailureAndAllowsRetry(void) {
    check_registration_failures(MemoryConsoleCommands_Register, 1);
}
