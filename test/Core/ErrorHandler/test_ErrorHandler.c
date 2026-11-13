#include "unity.h"
#include <string.h>
#include "Core/ErrorHandler/ErrorHandlerConsoleCommands.h"

#ifdef RACOOL_TEST_NATIVE
#include "../../Infrastructure/support/test_mocks.h"
#endif

// Include implementation directly to test internal structures
#define TAG TAG_test_eh
#include "../../../src/Core/ErrorHandler/ErrorHandler.c"
#undef TAG
#include "../../../src/Core/ErrorHandler/ErrorHandlerConfig.c"

/* Exercise the single production CLI command table. */
static int (*selected_error_command)(int,char **);
static const char *selected_error_name;
static esp_err_t capture_selected_error_command(IDebugConsole *console,const esp_console_cmd_t *command,CommandCategory category) {
    (void)console;(void)category;
    if (strcmp(command->command,selected_error_name)==0) selected_error_command=command->func;
    return ESP_OK;
}
static int invoke_error_command(const char *name,int argc,char **argv) {
    IDebugConsole console={.RegisterCommandWithCategory=capture_selected_error_command};
    IService *previous=mocked_ServiceLocator_GetService_fake.return_val;
    mocked_ServiceLocator_GetService_fake.return_val=(IService *)&console;
    selected_error_name=name;selected_error_command=NULL;
    TEST_ASSERT_EQUAL(ESP_OK,ErrorHandler_RegisterConsoleCommands());
    TEST_ASSERT_NOT_NULL(selected_error_command);
    mocked_ServiceLocator_GetService_fake.return_val=previous;
    return selected_error_command(argc,argv);
}
static void callback_fixture(const char *tag,int32_t code) {(void)tag;(void)code;}
static BaseType_t create_error_worker(TaskFunction_t fn,const char *name,uint32_t stack,void *arg,uint32_t priority,TaskHandle_t *task) {
    (void)fn;(void)name;(void)stack;(void)arg;(void)priority;*task=(TaskHandle_t)3;return pdPASS;
}
static BaseType_t acknowledge_error_stop(QueueHandle_t queue,void *item,TickType_t ticks) {
    if (queue==errorHandlerInstance.worker.stopped && ticks) {*(int32_t *)item=0;return pdTRUE;}
    return pdFALSE;
}
static BaseType_t inspect_start_admission(TaskFunction_t fn,const char *name,uint32_t stack,void *arg,uint32_t priority,TaskHandle_t *task) {
    TEST_ASSERT_EQUAL(SERVICE_STATE_INITIALIZED,errorHandlerInstance.state);
    TEST_ASSERT_EQUAL(-2,EH_HandleErrorAsync("before_worker_ready",17));
    return create_error_worker(fn,name,stack,arg,priority,task);
}
void test_ErrorHandler_StartPublishesAdmissionOnlyAfterWorkerCreation(void) {
    IErrorHandler *handler=GetErrorHandler();
    TEST_ASSERT_EQUAL(0,handler->base.Initialize(&handler->base,NULL));
    mocked_xTaskCreate_fake.custom_fake=inspect_start_admission;
    TEST_ASSERT_EQUAL(0,handler->base.Start(&handler->base));
    TEST_ASSERT_EQUAL(SERVICE_STATE_STARTED,handler->base.GetState(&handler->base));
    TEST_ASSERT_EQUAL(0,handler->HandleErrorAsync("ready",17));
}
void setUp(void) {
    #ifdef RACOOL_TEST_NATIVE
    RESET_FAKE(mocked_ServiceLocator_GetService);
    RESET_FAKE(mocked_ServiceLocator_GetEventBus);
    RESET_FAKE(mocked_xQueueCreate);
    RESET_FAKE(mocked_xTaskCreate);
    RESET_FAKE(mocked_xSemaphoreCreateMutex);
    RESET_FAKE(mocked_xQueueSend);
    RESET_FAKE(mocked_xQueueSendFromISR);
    RESET_FAKE(mocked_xQueueReceive);
    RESET_FAKE(mocked_xSemaphoreTake);
    RESET_FAKE(mocked_xSemaphoreGive);
    RESET_FAKE(mocked_xTaskGetTickCount);
    RESET_FAKE(mocked_xTaskGetTickCountFromISR);

    // Default mock returns
    mocked_xQueueCreate_fake.return_val = (QueueHandle_t)1;
    mocked_xSemaphoreCreateMutex_fake.return_val = (SemaphoreHandle_t)2;
    mocked_xTaskCreate_fake.custom_fake=create_error_worker;
    mocked_xQueueReceive_fake.custom_fake=acknowledge_error_stop;
    mocked_xQueueSend_fake.return_val = pdPASS;
    mocked_xQueueSendFromISR_fake.return_val = pdPASS;
    mocked_xSemaphoreTake_fake.return_val = pdPASS;
    mocked_xSemaphoreGive_fake.return_val = pdPASS;
    mocked_xTaskGetTickCount_fake.return_val = 100;
    mocked_xTaskGetTickCountFromISR_fake.return_val = 100;
    #endif

    // IMPORTANT: Do NOT memset the whole struct — that zeroes vtable pointers.
    // The static vtable is process-owned and never re-assigned by the getter.
    // Only reset mutable fields; native tasks are mocked in this package.
    atomic_store(&selfTestActive,false);atomic_store(&selfTestCompleted,false);atomic_store(&selfTestCall,false);
    errorHandlerInstance.controlMutex=NULL;
    memset(&errorHandlerInstance.worker,0,sizeof(errorHandlerInstance.worker));
    errorHandlerInstance.state = SERVICE_STATE_UNINITIALIZED;
    errorHandlerInstance.lastErrorCode = 0;
    errorHandlerInstance.historyBuffer = NULL;
    errorHandlerInstance.stateMutex = NULL;
    errorHandlerInstance.errorQueue = NULL;
    errorHandlerInstance.queueProcessorTask = NULL;
    errorHandlerInstance.historyCount = 0;
    errorHandlerInstance.historyWriteIdx = 0;
    errorHandlerInstance.totalErrorCount = 0;
    errorHandlerInstance.callbackCount = 0;
    memset(errorHandlerInstance.callbacks, 0, sizeof(errorHandlerInstance.callbacks));

    // Ensure vtable is populated for the very first test call
    GetErrorHandler();
}

void tearDown(void) {
    // Free any allocated resources from the test to avoid leaks between tests
    if (errorHandlerInstance.historyBuffer != NULL) {
        free(errorHandlerInstance.historyBuffer);
        errorHandlerInstance.historyBuffer = NULL;
    }
}

void test_ErrorHandler_StatisticsFailurePreservesOutputAndMachineRejects(void) {
    IErrorHandler *handler=GetErrorHandler();
    TEST_ASSERT_EQUAL(0,handler->base.Initialize(&handler->base,NULL));
    errorHandlerInstance.totalErrorCount=9;errorHandlerInstance.lastErrorCode=-7;
    errorHandlerInstance.callbackCount=2;
    ErrorHandlerStatistics statistics={.totalErrorCount=123};
    mocked_xSemaphoreTake_fake.return_val=pdFALSE;
    TEST_ASSERT_EQUAL(-2,handler->GetStatistics(&statistics));
    TEST_ASSERT_EQUAL(123,statistics.totalErrorCount);
    char *args[]={"error_status","--machine"};
    TEST_ASSERT_EQUAL(1,invoke_error_command("error_status",2,args));
    mocked_xSemaphoreTake_fake.return_val=pdTRUE;
    TEST_ASSERT_EQUAL(0,handler->GetStatistics(&statistics));
    TEST_ASSERT_EQUAL(9,statistics.totalErrorCount);
    TEST_ASSERT_EQUAL(-7,statistics.lastErrorCode);
    TEST_ASSERT_EQUAL(2,statistics.callbackCount);
    TEST_ASSERT_EQUAL(SERVICE_STATE_INITIALIZED,statistics.state);
    TEST_ASSERT_EQUAL(0,invoke_error_command("error_status",2,args));
    TEST_ASSERT_EQUAL(-1,handler->GetStatistics(NULL));
}
void test_ErrorHandler_CliRejectsMalformedInputBeforeQueueAdmission(void) {
    const char *invalid[]={"abc","12x","2147483648","-2147483649"," 1","+"};
    for (unsigned i=0;i<sizeof(invalid)/sizeof(invalid[0]);++i) {
        char *args[]={"error_trigger","test",(char *)invalid[i]};
        TEST_ASSERT_EQUAL(1,invoke_error_command("error_trigger",3,args));
        TEST_ASSERT_EQUAL(1,invoke_error_command("error_trigger_isr",3,args));
    }
    TEST_ASSERT_EQUAL(0,mocked_xQueueSend_fake.call_count);
    TEST_ASSERT_EQUAL(0,mocked_xQueueSendFromISR_fake.call_count);
    int32_t value; char *minimum[]={"error_trigger","test","-2147483648"};
    TEST_ASSERT_TRUE(ErrorHandler_ParseInput(3,minimum,&value));
    TEST_ASSERT_EQUAL(INT32_MIN,value);
}
void test_ErrorHandler_CliSimulationUsesTaskQueueAndPropagatesFailure(void) {
    IErrorHandler *eh=GetErrorHandler();
    TEST_ASSERT_EQUAL(0,eh->base.Initialize(&eh->base,NULL));
    TEST_ASSERT_EQUAL(0,eh->base.Start(&eh->base));
    char *args[]={"error_trigger_isr","test","3"};
    TEST_ASSERT_EQUAL(0,invoke_error_command("error_trigger_isr",3,args));
    TEST_ASSERT_EQUAL(1,mocked_xQueueSend_fake.call_count);
    TEST_ASSERT_EQUAL(0,mocked_xQueueSendFromISR_fake.call_count);
    mocked_xQueueSend_fake.return_val=pdFAIL;
    TEST_ASSERT_EQUAL(1,invoke_error_command("error_trigger_isr",3,args));
    TEST_ASSERT_EQUAL(1,invoke_error_command("error_trigger",3,args));
}

void test_ErrorHandler_StopTimeoutPreservesResourcesAndRequiresRetry(void) {
    IErrorHandler *eh=GetErrorHandler();
    TEST_ASSERT_EQUAL(0,eh->base.Initialize(&eh->base,NULL));
    TEST_ASSERT_EQUAL(0,eh->base.Start(&eh->base));
    ErrorRecord *history=errorHandlerInstance.historyBuffer;
    mocked_xQueueReceive_fake.custom_fake=NULL; mocked_xQueueReceive_fake.return_val=pdFALSE;
    TEST_ASSERT_EQUAL(-1,eh->base.Stop(&eh->base));
    TEST_ASSERT_EQUAL(SERVICE_STATE_ERROR,eh->base.GetState(&eh->base));
    TEST_ASSERT_EQUAL_PTR(history,errorHandlerInstance.historyBuffer);
    TEST_ASSERT_NOT_NULL(errorHandlerInstance.queueProcessorTask);
    TEST_ASSERT_EQUAL(-1,eh->base.Start(&eh->base));
    TEST_ASSERT_EQUAL(-1,eh->base.Initialize(&eh->base,NULL));
    TEST_ASSERT_EQUAL(-2,eh->HandleErrorAsync("closing",1));
    mocked_xQueueReceive_fake.custom_fake=acknowledge_error_stop;
    TEST_ASSERT_EQUAL(0,eh->base.Stop(&eh->base));
    TEST_ASSERT_EQUAL(0,eh->base.Start(&eh->base));
}
void test_ErrorHandler_RepeatedStartAndInitCannotReplaceLiveWorker(void) {
    IErrorHandler *eh=GetErrorHandler();eh->base.Initialize(&eh->base,NULL);eh->base.Start(&eh->base);
    unsigned calls=mocked_xTaskCreate_fake.call_count;
    TEST_ASSERT_EQUAL(0,eh->base.Start(&eh->base));
    TEST_ASSERT_EQUAL(calls,mocked_xTaskCreate_fake.call_count);
    TEST_ASSERT_EQUAL(-1,eh->base.Initialize(&eh->base,NULL));
}
void test_ErrorHandler_SelfTestQueueFailureDoesNotChangeCallbackRegistry(void) {
    IErrorHandler *eh=GetErrorHandler();eh->base.Initialize(&eh->base,NULL);eh->base.Start(&eh->base);
    errorHandlerInstance.callbackCount=4;
    mocked_xQueueSend_fake.return_val=pdFALSE;
    TEST_ASSERT_EQUAL(-3,eh->PerformSelfTest());
    TEST_ASSERT_EQUAL(4,errorHandlerInstance.callbackCount);
    TEST_ASSERT_FALSE(atomic_load(&selfTestActive));
}

void test_ErrorHandler_ClearAndHistoryLockFailurePreserveDataAndFailCli(void) {
    IErrorHandler *handler=GetErrorHandler();
    TEST_ASSERT_EQUAL(0,handler->base.Initialize(&handler->base,NULL));
    errorHandlerInstance.historyCount=1;errorHandlerInstance.lastErrorCode=23;
    mocked_xSemaphoreTake_fake.return_val=pdFALSE;
    TEST_ASSERT_EQUAL(-2,handler->ClearHistory());
    TEST_ASSERT_EQUAL(1,errorHandlerInstance.historyCount);
    TEST_ASSERT_EQUAL(23,handler->GetLastErrorCode());
    char *args[]={"error_clear"};
    TEST_ASSERT_EQUAL(1,invoke_error_command("error_clear",1,args));
    TEST_ASSERT_EQUAL(1,invoke_error_command("error_history",1,args));
    TEST_ASSERT_EQUAL(1,invoke_error_command("error_status",1,args));
    TEST_ASSERT_EQUAL(1,errorHandlerInstance.historyCount);
}
void test_ErrorHandler_NoArgumentCommandsRejectExtrasBeforeMutation(void) {
    char *args[]={"error_clear","extra"};
    TEST_ASSERT_EQUAL(1,invoke_error_command("error_clear",2,args));
    TEST_ASSERT_EQUAL(1,invoke_error_command("error_history",2,args));
    TEST_ASSERT_EQUAL(1,invoke_error_command("error_status",2,args));
    TEST_ASSERT_EQUAL(1,invoke_error_command("error_selftest",2,args));
    TEST_ASSERT_EQUAL(0,mocked_xQueueSend_fake.call_count);
    TEST_ASSERT_EQUAL(0,mocked_xSemaphoreTake_fake.call_count);
}
static int (*public_error_handlers[6])(int,char **);
static unsigned public_error_count;
static esp_err_t capture_public_error_handler(IDebugConsole *console,const esp_console_cmd_t *command,CommandCategory category) {
    (void)console;(void)category;
    TEST_ASSERT_LESS_THAN(6,public_error_count);
    public_error_handlers[public_error_count++]=command->func;return ESP_OK;
}
void test_ErrorHandler_PublicCliPropagatesHistoryAndClearFailures(void) {
    IErrorHandler *handler=GetErrorHandler();
    TEST_ASSERT_EQUAL(0,handler->base.Initialize(&handler->base,NULL));
    IDebugConsole console={.RegisterCommandWithCategory=capture_public_error_handler};
    mocked_ServiceLocator_GetService_fake.return_val=(IService *)&console;
    public_error_count=0;
    TEST_ASSERT_EQUAL(ESP_OK,ErrorHandler_RegisterConsoleCommands());
    TEST_ASSERT_EQUAL(6,public_error_count);
    errorHandlerInstance.historyCount=1;mocked_xSemaphoreTake_fake.return_val=pdFALSE;
    char *args[]={"error_clear","extra"};
    TEST_ASSERT_EQUAL(1,public_error_handlers[4](1,args));
    TEST_ASSERT_EQUAL(1,public_error_handlers[1](1,args));
    TEST_ASSERT_EQUAL(1,public_error_handlers[0](2,args));
    TEST_ASSERT_EQUAL(1,public_error_handlers[5](2,args));
    TEST_ASSERT_EQUAL(1,errorHandlerInstance.historyCount);
}
static TickType_t selftest_clock;
static TickType_t advance_selftest_clock(void) {selftest_clock+=25;return selftest_clock;}
void test_ErrorHandler_TimedOutSelfTestCannotAdoptLateCompletion(void) {
    IErrorHandler *handler=GetErrorHandler();
    TEST_ASSERT_EQUAL(0,handler->base.Initialize(&handler->base,NULL));
    TEST_ASSERT_EQUAL(0,handler->base.Start(&handler->base));
    TEST_ASSERT_EQUAL(0,handler->RegisterCallback(callback_fixture));
    selftest_clock=0;mocked_xTaskGetTickCount_fake.custom_fake=advance_selftest_clock;
    TEST_ASSERT_EQUAL(-4,handler->PerformSelfTest());
    TEST_ASSERT_TRUE(atomic_load(&selfTestActive));
    TEST_ASSERT_EQUAL(-2,handler->PerformSelfTest());
    atomic_store(&selfTestCompleted,true);
    TEST_ASSERT_EQUAL(-2,handler->PerformSelfTest());
    mocked_xQueueSend_fake.return_val=pdFALSE;
    TEST_ASSERT_EQUAL(-3,handler->PerformSelfTest());
    TEST_ASSERT_FALSE(atomic_load(&selfTestActive));
    TEST_ASSERT_EQUAL(1,errorHandlerInstance.callbackCount);
}

void test_ErrorHandler_TagApiRejectsEmptyAndOversizedInput(void) {
    IErrorHandler *handler=GetErrorHandler();
    TEST_ASSERT_EQUAL(0,handler->base.Initialize(&handler->base,NULL));
    TEST_ASSERT_EQUAL(0,handler->base.Start(&handler->base));
    char tag[ERROR_HANDLER_TAG_CAPACITY+1];memset(tag,'x',sizeof(tag));tag[sizeof(tag)-1]=0;
    BaseType_t woken=pdFALSE;
    TEST_ASSERT_EQUAL(-1,handler->HandleErrorAsync("",9));
    TEST_ASSERT_EQUAL(-1,handler->HandleErrorAsync(tag,9));
    TEST_ASSERT_EQUAL(-1,handler->HandleErrorFromISR("",9,&woken));
    TEST_ASSERT_EQUAL(-1,handler->HandleErrorFromISR(tag,9,&woken));
    TEST_ASSERT_EQUAL(pdFALSE,woken);
    TEST_ASSERT_EQUAL(0,mocked_xQueueSend_fake.call_count);
    TEST_ASSERT_EQUAL(0,mocked_xQueueSendFromISR_fake.call_count);
    TEST_ASSERT_EQUAL(0,handler->GetLastErrorCode());
    tag[ERROR_HANDLER_TAG_CAPACITY-1]=0;
    TEST_ASSERT_EQUAL(0,handler->HandleErrorAsync(tag,9));
    TEST_ASSERT_EQUAL(0,handler->HandleErrorFromISR(tag,9,&woken));
}

void test_ErrorHandler_Initialize_Success(void) {
    IErrorHandler *eh = GetErrorHandler();
    TEST_ASSERT_NOT_NULL(eh);

    int32_t res = eh->base.Initialize((IService *)eh, NULL);
    TEST_ASSERT_EQUAL(0, res);
    TEST_ASSERT_EQUAL(SERVICE_STATE_INITIALIZED, eh->base.GetState((IService *)eh));
    TEST_ASSERT_NOT_NULL(errorHandlerInstance.historyBuffer);
    TEST_ASSERT_NOT_NULL(errorHandlerInstance.errorQueue);
    TEST_ASSERT_NOT_NULL(errorHandlerInstance.stateMutex);
}

void test_ErrorHandler_Initialize_Fail_Semaphore(void) {
    #ifdef RACOOL_TEST_NATIVE
    mocked_xSemaphoreCreateMutex_fake.return_val = NULL;
    #endif

    IErrorHandler *eh = GetErrorHandler();
    int32_t res = eh->base.Initialize((IService *)eh, NULL);
    TEST_ASSERT_EQUAL(-2, res);
    TEST_ASSERT_NULL(errorHandlerInstance.stateMutex);
    // historyBuffer was allocated but should have been freed on failure
    TEST_ASSERT_NULL(errorHandlerInstance.historyBuffer);
}

void test_ErrorHandler_Initialize_Fail_Queue(void) {
    #ifdef RACOOL_TEST_NATIVE
    mocked_xQueueCreate_fake.return_val = NULL;
    #endif

    IErrorHandler *eh = GetErrorHandler();
    int32_t res = eh->base.Initialize((IService *)eh, NULL);
    TEST_ASSERT_EQUAL(-3, res);
    // All resources must be cleaned up on failure
    TEST_ASSERT_NULL(errorHandlerInstance.errorQueue);
    TEST_ASSERT_NULL(errorHandlerInstance.stateMutex);
    TEST_ASSERT_NULL(errorHandlerInstance.historyBuffer);
}

void test_ErrorHandler_Start_Success(void) {
    IErrorHandler *eh = GetErrorHandler();
    eh->base.Initialize((IService *)eh, NULL);

    int32_t res = eh->base.Start((IService *)eh);
    TEST_ASSERT_EQUAL(0, res);
    TEST_ASSERT_EQUAL(SERVICE_STATE_STARTED, eh->base.GetState((IService *)eh));
}

void test_ErrorHandler_Stop_Success(void) {
    IErrorHandler *eh = GetErrorHandler();
    eh->base.Initialize((IService *)eh, NULL);
    eh->base.Start((IService *)eh);

    int32_t res = eh->base.Stop((IService *)eh);
    TEST_ASSERT_EQUAL(0, res);
    TEST_ASSERT_EQUAL(SERVICE_STATE_STOPPED, eh->base.GetState((IService *)eh));
    TEST_ASSERT_NOT_NULL(errorHandlerInstance.historyBuffer);
    TEST_ASSERT_NOT_NULL(errorHandlerInstance.errorQueue);
    TEST_ASSERT_NOT_NULL(errorHandlerInstance.stateMutex);
}

void test_ErrorHandler_CheckHealth(void) {
    IErrorHandler *eh = GetErrorHandler();
    TEST_ASSERT_EQUAL(-1, eh->base.CheckHealth((IService *)eh));

    eh->base.Initialize((IService *)eh, NULL);
    TEST_ASSERT_EQUAL(-1, eh->base.CheckHealth((IService *)eh));

    eh->base.Start((IService *)eh);
    TEST_ASSERT_EQUAL(0, eh->base.CheckHealth((IService *)eh));

    eh->base.Stop((IService *)eh);
    TEST_ASSERT_EQUAL(-1, eh->base.CheckHealth((IService *)eh));
}

void test_ErrorHandler_GetLastErrorCode_Default(void) {
    IErrorHandler *eh = GetErrorHandler();
    TEST_ASSERT_EQUAL(0, eh->GetLastErrorCode());
}

void test_ErrorHandler_HandleErrorAsync_BeforeStart_Fallback(void) {
    IErrorHandler *eh = GetErrorHandler();
    // Before start: queue is NULL, should fallback to sync logging and return -2
    int32_t res = eh->HandleErrorAsync("FallbackModule", 101);
    TEST_ASSERT_EQUAL(-2, res);
    TEST_ASSERT_EQUAL(101, eh->GetLastErrorCode());
}

void test_ErrorHandler_HandleErrorAsync_Queued(void) {
    IErrorHandler *eh = GetErrorHandler();
    eh->base.Initialize((IService *)eh, NULL);
    eh->base.Start((IService *)eh);

    int32_t res = eh->HandleErrorAsync("TestModule", 42);
    TEST_ASSERT_EQUAL(0, res);

    #ifdef RACOOL_TEST_NATIVE
    TEST_ASSERT_EQUAL(1, mocked_xQueueSend_fake.call_count);
    #endif
}

void test_ErrorHandler_HandleError_Legacy(void) {
    // HandleError should delegate to HandleErrorAsync without crashing
    IErrorHandler *eh = GetErrorHandler();
    eh->HandleError("LegacyTag", 55);
    // If service not started, fallback stores last error code
    TEST_ASSERT_EQUAL(55, eh->GetLastErrorCode());
}

void test_ErrorHandler_RegisterCallback(void) {
    IErrorHandler *eh = GetErrorHandler();
    eh->base.Initialize((IService *)eh, NULL);

    int32_t res = eh->RegisterCallback(callback_fixture);
    TEST_ASSERT_EQUAL(0, res);
    TEST_ASSERT_EQUAL(1, errorHandlerInstance.callbackCount);
}

void test_ErrorHandler_RegisterCallback_Null(void) {
    IErrorHandler *eh = GetErrorHandler();
    eh->base.Initialize((IService *)eh, NULL);

    int32_t res = eh->RegisterCallback(NULL);
    TEST_ASSERT_EQUAL(-1, res);
    TEST_ASSERT_EQUAL(0, errorHandlerInstance.callbackCount);
}

void test_ErrorHandler_HistoryCopy(void) {
    IErrorHandler *eh = GetErrorHandler();
    eh->base.Initialize((IService *)eh, NULL);

    // Simulate processed errors in history buffer
    errorHandlerInstance.historyBuffer[0].errorCode = 100;
    strcpy(errorHandlerInstance.historyBuffer[0].tag, "A");
    errorHandlerInstance.historyBuffer[0].timestamp = 10;

    errorHandlerInstance.historyBuffer[1].errorCode = 200;
    strcpy(errorHandlerInstance.historyBuffer[1].tag, "B");
    errorHandlerInstance.historyBuffer[1].timestamp = 20;

    errorHandlerInstance.historyCount = 2;
    errorHandlerInstance.historyWriteIdx = 2;

    ErrorRecord out[2];
    uint32_t count = 0;
    int32_t res = eh->GetErrorHistory(out, 2, &count);
    TEST_ASSERT_EQUAL(0, res);
    TEST_ASSERT_EQUAL(2, count);

    // History is returned newest-first (reverse chronological order)
    TEST_ASSERT_EQUAL(200, out[0].errorCode);
    TEST_ASSERT_EQUAL_STRING("B", out[0].tag);
    TEST_ASSERT_EQUAL(100, out[1].errorCode);
    TEST_ASSERT_EQUAL_STRING("A", out[1].tag);
}

void test_ErrorHandler_GetErrorHistory_NullArgs(void) {
    IErrorHandler *eh = GetErrorHandler();
    eh->base.Initialize((IService *)eh, NULL);

    uint32_t count = 0;
    ErrorRecord out[2];
    TEST_ASSERT_EQUAL(-1, eh->GetErrorHistory(NULL, 2, &count));
    TEST_ASSERT_EQUAL(-1, eh->GetErrorHistory(out, 0, &count));
    TEST_ASSERT_EQUAL(-1, eh->GetErrorHistory(out, 2, NULL));
}

void test_ErrorHandler_ClearHistory(void) {
    IErrorHandler *eh = GetErrorHandler();
    eh->base.Initialize((IService *)eh, NULL);

    errorHandlerInstance.historyCount = 5;
    errorHandlerInstance.totalErrorCount = 10;
    errorHandlerInstance.lastErrorCode = 404;

    eh->ClearHistory();

    TEST_ASSERT_EQUAL(0, errorHandlerInstance.historyCount);
    TEST_ASSERT_EQUAL(0, errorHandlerInstance.totalErrorCount);
    TEST_ASSERT_EQUAL(0, errorHandlerInstance.lastErrorCode);
}

void test_ErrorHandler_HandleErrorFromISR_BeforeStart_Fallback(void) {
    IErrorHandler *eh = GetErrorHandler();
    BaseType_t woken = pdFALSE;
    int32_t res = eh->HandleErrorFromISR("ISR_Fallback", 888, &woken);
    TEST_ASSERT_EQUAL(-2, res);
    TEST_ASSERT_EQUAL(888, eh->GetLastErrorCode());
    TEST_ASSERT_EQUAL(pdFALSE, woken);
}

void test_ErrorHandler_HandleErrorFromISR_Success(void) {
    IErrorHandler *eh = GetErrorHandler();
    eh->base.Initialize((IService *)eh, NULL);
    eh->base.Start((IService *)eh);

    BaseType_t woken = pdFALSE;
    int32_t res = eh->HandleErrorFromISR("ISR_Module", 99, &woken);
    TEST_ASSERT_EQUAL(0, res);

    #ifdef RACOOL_TEST_NATIVE
    TEST_ASSERT_EQUAL(1, mocked_xQueueSendFromISR_fake.call_count);
    #endif
}


#include "Infrastructure/support/console_registration_fixture.h"
static esp_err_t error_registration_adapter(IDebugConsole *console) {
    mocked_ServiceLocator_GetService_fake.custom_fake = NULL;
    mocked_ServiceLocator_GetService_fake.return_val = (IService *)console;
    return EH_RegisterConsoleCommands();
}
void test_ErrorInternalRegistration_PreservesFailuresAndRetries(void) {
    check_registration_failures(error_registration_adapter, 6);
}
void test_ErrorInternalRegistration_FailurePreventsWorkerCreation(void) {
    IService *service = (IService *)GetErrorHandler();
    TEST_ASSERT_EQUAL(0, service->Initialize(service, NULL));
    mocked_ServiceLocator_GetService_fake.custom_fake = NULL;
    mocked_ServiceLocator_GetService_fake.return_val = (IService *)&registration_console;
    registration_calls = 0;
    registration_fail_at = 1;
    registration_failure = ESP_ERR_NO_MEM;
    unsigned tasks = mocked_xTaskCreate_fake.call_count;
    TEST_ASSERT_EQUAL(ESP_ERR_NO_MEM, service->Start(service));
    TEST_ASSERT_EQUAL(tasks, mocked_xTaskCreate_fake.call_count);
    TEST_ASSERT_NOT_EQUAL(SERVICE_STATE_STARTED, service->GetState(service));
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_ErrorHandler_StatisticsFailurePreservesOutputAndMachineRejects);
    RUN_TEST(test_ErrorHandler_CliRejectsMalformedInputBeforeQueueAdmission);
    RUN_TEST(test_ErrorHandler_CliSimulationUsesTaskQueueAndPropagatesFailure);
    RUN_TEST(test_ErrorHandler_StartPublishesAdmissionOnlyAfterWorkerCreation);
    RUN_TEST(test_ErrorHandler_StopTimeoutPreservesResourcesAndRequiresRetry);
    RUN_TEST(test_ErrorHandler_RepeatedStartAndInitCannotReplaceLiveWorker);
    RUN_TEST(test_ErrorHandler_SelfTestQueueFailureDoesNotChangeCallbackRegistry);
    RUN_TEST(test_ErrorHandler_ClearAndHistoryLockFailurePreserveDataAndFailCli);
    RUN_TEST(test_ErrorHandler_NoArgumentCommandsRejectExtrasBeforeMutation);
    RUN_TEST(test_ErrorHandler_PublicCliPropagatesHistoryAndClearFailures);
    RUN_TEST(test_ErrorHandler_TimedOutSelfTestCannotAdoptLateCompletion);
    RUN_TEST(test_ErrorHandler_TagApiRejectsEmptyAndOversizedInput);
    RUN_TEST(test_ErrorHandler_Initialize_Success);
    RUN_TEST(test_ErrorHandler_Initialize_Fail_Semaphore);
    RUN_TEST(test_ErrorHandler_Initialize_Fail_Queue);
    RUN_TEST(test_ErrorHandler_Start_Success);
    RUN_TEST(test_ErrorHandler_Stop_Success);
    RUN_TEST(test_ErrorHandler_CheckHealth);
    RUN_TEST(test_ErrorHandler_GetLastErrorCode_Default);
    RUN_TEST(test_ErrorHandler_HandleErrorAsync_BeforeStart_Fallback);
    RUN_TEST(test_ErrorHandler_HandleErrorAsync_Queued);
    RUN_TEST(test_ErrorHandler_HandleError_Legacy);
    RUN_TEST(test_ErrorHandler_RegisterCallback);
    RUN_TEST(test_ErrorHandler_RegisterCallback_Null);
    RUN_TEST(test_ErrorHandler_HistoryCopy);
    RUN_TEST(test_ErrorHandler_GetErrorHistory_NullArgs);
    RUN_TEST(test_ErrorHandler_ClearHistory);
    RUN_TEST(test_ErrorHandler_HandleErrorFromISR_BeforeStart_Fallback);
    RUN_TEST(test_ErrorHandler_HandleErrorFromISR_Success);
    RUN_TEST(test_ErrorInternalRegistration_PreservesFailuresAndRetries);
    RUN_TEST(test_ErrorInternalRegistration_FailurePreventsWorkerCreation);
    return UNITY_END();
}
