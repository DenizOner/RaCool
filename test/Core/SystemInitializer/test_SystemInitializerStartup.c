#include "test_mocks.h"
#include "unity.h"
#include "Core/SystemInitializer/SystemInitializerStartup.h"
#include "Core/SystemInitializer/SystemInitializerStartup.c"

static ServiceState state;
static int32_t initialize_result, register_result, start_result, stop_result;
static unsigned initialize_calls, register_calls, start_calls, stop_calls;
static IService *registered;
static int32_t initialize(IService *self, IDIRegistry *registry) {
    (void)self;(void)registry;initialize_calls++;
    if (!initialize_result) state=SERVICE_STATE_INITIALIZED;
    return initialize_result;
}
static ServiceState get_state(IService *self) { (void)self;return state; }
static int32_t start(IService *self) {
    (void)self;start_calls++;
    if (!start_result) state=SERVICE_STATE_STARTED;
    return start_result;
}
static int32_t stop(IService *self) {
    (void)self;stop_calls++;
    if (!stop_result) state=SERVICE_STATE_STOPPED;
    return stop_result;
}
int32_t ServiceLocator_RegisterService(const char *name,IService *service) {
    TEST_ASSERT_EQUAL_STRING("fixture",name);register_calls++;
    if (!register_result) registered=service;
    return register_result;
}
static IService service={.Initialize=initialize,.Start=start,.Stop=stop,.GetState=get_state};
void setUp(void) {
    SystemInitializer_ResetStartupProgress();
    state=SERVICE_STATE_UNINITIALIZED;
    initialize_result=register_result=start_result=stop_result=0;
    initialize_calls=register_calls=start_calls=stop_calls=0;
    registered=NULL;
    RESET_FAKE(mocked_xTaskCreate);
    RESET_FAKE(mocked_xTaskGetTickCount);
    RESET_FAKE(mocked_vTaskDelay);
}
void tearDown(void) {}
void test_Startup_ServiceSuccessRequiresEveryPhase(void) {
    TEST_ASSERT_TRUE(SystemInitializer_StartService("fixture",&service,NULL));
    TEST_ASSERT_EQUAL_PTR(&service,registered);
    TEST_ASSERT_EQUAL(SERVICE_STATE_STARTED,state);
    TEST_ASSERT_EQUAL(1,initialize_calls);TEST_ASSERT_EQUAL(1,register_calls);TEST_ASSERT_EQUAL(1,start_calls);
}
void test_Startup_InitializationFailureDoesNotRegisterOrStart(void) {
    initialize_result=-17;
    TEST_ASSERT_FALSE(SystemInitializer_StartService("fixture",&service,NULL));
    TEST_ASSERT_EQUAL(0,register_calls);TEST_ASSERT_EQUAL(0,start_calls);TEST_ASSERT_EQUAL(0,stop_calls);
}
void test_Startup_RegistrationFailureStopsOnlyNewInitialization(void) {
    register_result=-23;
    TEST_ASSERT_FALSE(SystemInitializer_StartService("fixture",&service,NULL));
    TEST_ASSERT_EQUAL(1,stop_calls);TEST_ASSERT_EQUAL(0,start_calls);TEST_ASSERT_NULL(registered);
    TEST_ASSERT_EQUAL(SERVICE_STATE_STOPPED,state);
}
void test_Startup_RegistrationCleanupFailureDoesNotBecomeSuccess(void) {
    register_result=-23;stop_result=-31;
    TEST_ASSERT_FALSE(SystemInitializer_StartService("fixture",&service,NULL));
    TEST_ASSERT_EQUAL(SERVICE_STATE_INITIALIZED,state);
    TEST_ASSERT_EQUAL(1,stop_calls);TEST_ASSERT_EQUAL(0,start_calls);
}
void test_Startup_RegistrationConflictPreservesAlreadyOwnedService(void) {
    register_result=-23;
    const ServiceState existing[]={SERVICE_STATE_INITIALIZED,SERVICE_STATE_STARTED};
    for (unsigned i=0;i<2;i++) {
        state=existing[i];
        TEST_ASSERT_FALSE(SystemInitializer_StartService("fixture",&service,NULL));
        TEST_ASSERT_EQUAL(0,initialize_calls);TEST_ASSERT_EQUAL(0,stop_calls);
        TEST_ASSERT_EQUAL(existing[i],state);
    }
}
void test_Startup_StartFailureRetainsDiscoverableCleanupOwner(void) {
    start_result=-37;
    TEST_ASSERT_FALSE(SystemInitializer_StartService("fixture",&service,NULL));
    TEST_ASSERT_EQUAL_PTR(&service,registered);
    TEST_ASSERT_EQUAL(SERVICE_STATE_INITIALIZED,state);
    TEST_ASSERT_EQUAL(0,stop_calls);
}
void test_Startup_InvalidInputsDoNotMutateServices(void) {
    TEST_ASSERT_FALSE(SystemInitializer_StartService(NULL,&service,NULL));
    TEST_ASSERT_FALSE(SystemInitializer_StartService("",&service,NULL));
    TEST_ASSERT_FALSE(SystemInitializer_StartService("fixture",NULL,NULL));
    IService incomplete={0};
    TEST_ASSERT_FALSE(SystemInitializer_StartService("fixture",&incomplete,NULL));
    TEST_ASSERT_EQUAL(0,initialize_calls);TEST_ASSERT_EQUAL(0,register_calls);TEST_ASSERT_EQUAL(0,start_calls);
}
static void worker_entry(void *context) { (void)context; }
void test_Startup_WorkerCreationFailureReleasesOnlyItsContext(void) {
    SystemInitializerWorker worker={.done=true};
    IDIRegistry registry={0};
    TEST_ASSERT_FALSE(SystemInitializer_StartWorker(&worker,worker_entry,"fixture",&registry));
    TEST_ASSERT_TRUE(atomic_load(&worker.done));
    TEST_ASSERT_FALSE(atomic_load(&worker.succeeded));
    TEST_ASSERT_NULL(worker.registry);
    TEST_ASSERT_EQUAL(1,mocked_xTaskCreate_fake.call_count);
}
static TickType_t fake_ticks;
static TickType_t advancing_clock(void) { return fake_ticks++; }
void test_Startup_WorkerTimeoutRetainsRegistryAndRejectsContextReuse(void) {
    SystemInitializerWorker worker={.done=true};
    IDIRegistry registry={0};
    mocked_xTaskCreate_fake.return_val=pdPASS;
    TEST_ASSERT_TRUE(SystemInitializer_StartWorker(&worker,worker_entry,"fixture",&registry));
    TEST_ASSERT_EQUAL_PTR(&worker,mocked_xTaskCreate_fake.arg3_val);
    TEST_ASSERT_EQUAL_PTR(&registry,worker.registry);
    mocked_xTaskGetTickCount_fake.custom_fake=advancing_clock;fake_ticks=UINT32_MAX-1;
    TEST_ASSERT_FALSE(SystemInitializer_WaitWorker(&worker,3));
    TEST_ASSERT_FALSE(atomic_load(&worker.done));
    TEST_ASSERT_EQUAL_PTR(&registry,worker.registry);
    TEST_ASSERT_FALSE(SystemInitializer_StartWorker(&worker,worker_entry,"second",NULL));
    TEST_ASSERT_EQUAL(1,mocked_xTaskCreate_fake.call_count);
    TEST_ASSERT_EQUAL(0,mocked_vTaskDelete_fake.call_count);
    SystemInitializer_CompleteWorker(&worker,true);
    TEST_ASSERT_TRUE(SystemInitializer_WaitWorker(&worker,3));
    TEST_ASSERT_NULL(worker.registry);
}
void test_Startup_WorkerFailureIsNotSuccessfulReadiness(void) {
    SystemInitializerWorker worker={.done=false};
    SystemInitializer_CompleteWorker(&worker,false);
    TEST_ASSERT_FALSE(SystemInitializer_WaitWorker(&worker,3));
    TEST_ASSERT_TRUE(atomic_load(&worker.done));
    TEST_ASSERT_FALSE(SystemInitializer_WaitWorker(NULL,3));
    TEST_ASSERT_FALSE(SystemInitializer_WaitWorker(&worker,0));
}

void test_Startup_ProgressFailureLatchesAndResetClears(void) {
    start_result=-1;
    TEST_ASSERT_FALSE(SystemInitializer_StartService("fixture",&service,NULL));
    SystemStartupProgress progress=SystemInitializer_GetStartupProgress();
    TEST_ASSERT_EQUAL(0,progress.activeStages);TEST_ASSERT_TRUE(progress.failed);
    SystemInitializer_ResetStartupProgress();
    progress=SystemInitializer_GetStartupProgress();
    TEST_ASSERT_EQUAL(0,progress.activeStages);TEST_ASSERT_FALSE(progress.failed);
}
void test_Startup_ParallelHardwareCompletionPreservesRadioProgress(void) {
    uint32_t radio=StartupStageMask(SVC_NAME_ZIGBEE), hardware=StartupStageMask(SVC_NAME_IR_TEMP_SENSOR);
    TEST_ASSERT_NOT_EQUAL(radio,hardware);
    atomic_fetch_or(&active_startup_stages,radio);
    atomic_fetch_or(&active_startup_stages,hardware);
    TEST_ASSERT_EQUAL(radio|hardware,SystemInitializer_GetStartupProgress().activeStages);
    TEST_ASSERT_TRUE(EndStartupStage(hardware,true));
    TEST_ASSERT_EQUAL(radio,SystemInitializer_GetStartupProgress().activeStages);
    TEST_ASSERT_FALSE(SystemInitializer_GetStartupProgress().failed);
    TEST_ASSERT_FALSE(EndStartupStage(radio,false));
    TEST_ASSERT_EQUAL(0,SystemInitializer_GetStartupProgress().activeStages);
    TEST_ASSERT_TRUE(SystemInitializer_GetStartupProgress().failed);
}
void test_Startup_ProgressClearsAfterSuccessfulService(void) {
    TEST_ASSERT_TRUE(SystemInitializer_StartService("fixture",&service,NULL));
    TEST_ASSERT_EQUAL(0,SystemInitializer_GetStartupProgress().activeStages);
    TEST_ASSERT_EQUAL(StartupStageMask("fixture"),SystemInitializer_GetStartupProgress().startedStages);
    TEST_ASSERT_EQUAL(StartupStageMask("fixture"),SystemInitializer_GetStartupProgress().completedStages);
    TEST_ASSERT_FALSE(SystemInitializer_GetStartupProgress().failed);
}
