#include "Core/IService.h"
#include "unity.h"
static ServiceState state;
static unsigned initCalls, startCalls;
static int32_t result;
static int32_t initialize(IService *self, struct IDIRegistry *registry) {
    (void)self; (void)registry; ++initCalls; return result;
}
static int32_t start(IService *self) { (void)self; ++startCalls; return result; }
static ServiceState getState(IService *self) { (void)self; return state; }
static IService service = {.Initialize=initialize, .Start=start, .GetState=getState};
void setUp(void) { initCalls=0; startCalls=0; result=0; state=SERVICE_STATE_UNINITIALIZED; }
void tearDown(void) {}
void test_IServiceLifecycle_InitializeRecoversStoppedAndError(void) {
    const ServiceState states[] = {SERVICE_STATE_UNINITIALIZED, SERVICE_STATE_STOPPED, SERVICE_STATE_ERROR};
    result = -17;
    for (unsigned i=0; i<3; ++i) {
        state=states[i];
        TEST_ASSERT_EQUAL(-17, IService_SafeInitialize(&service, NULL));
        TEST_ASSERT_EQUAL(i+1, initCalls);
    }
}
void test_IServiceLifecycle_InitializeIsIdempotentOnlyWhenReady(void) {
    state=SERVICE_STATE_INITIALIZED;
    TEST_ASSERT_EQUAL(0, IService_SafeInitialize(&service, NULL));
    state=SERVICE_STATE_STARTED;
    TEST_ASSERT_EQUAL(0, IService_SafeInitialize(&service, NULL));
    TEST_ASSERT_EQUAL(0, initCalls);
}
void test_IServiceLifecycle_StartRejectsUninitializedAndFaulted(void) {
    TEST_ASSERT_EQUAL(-1, IService_SafeStart(&service));
    state=SERVICE_STATE_ERROR;
    TEST_ASSERT_EQUAL(-1, IService_SafeStart(&service));
    state=(ServiceState)99;
    TEST_ASSERT_EQUAL(-1, IService_SafeStart(&service));
    TEST_ASSERT_EQUAL(-1, IService_SafeInitialize(&service, NULL));
    TEST_ASSERT_EQUAL(0, startCalls);
    TEST_ASSERT_EQUAL(0, initCalls);
}
void test_IServiceLifecycle_StartReturnsImplementationResult(void) {
    state=SERVICE_STATE_INITIALIZED; result=-23;
    TEST_ASSERT_EQUAL(-23, IService_SafeStart(&service));
    state=SERVICE_STATE_STOPPED;
    TEST_ASSERT_EQUAL(-23, IService_SafeStart(&service));
    state=SERVICE_STATE_STARTED;
    TEST_ASSERT_EQUAL(0, IService_SafeStart(&service));
    TEST_ASSERT_EQUAL(2, startCalls);
}
void test_IServiceLifecycle_MissingInterfaceIsRejected(void) {
    IService empty={0};
    TEST_ASSERT_EQUAL(-1, IService_SafeStart(NULL));
    TEST_ASSERT_EQUAL(-1, IService_SafeInitialize(NULL, NULL));
    TEST_ASSERT_EQUAL(-1, IService_SafeStart(&empty));
    TEST_ASSERT_EQUAL(-1, IService_SafeInitialize(&empty, NULL));
}
