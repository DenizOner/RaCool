#include "unity.h"
#define GetErrorHandler TargetError_GetModule
#define ServiceLocator_GetService TargetError_GetService
#define ServiceLocator_GetEventBus TargetError_GetEventBus
#include "../../../../src/Core/ErrorHandler/ErrorHandler.c"
#undef TAG
#include "../../../../src/Core/ErrorHandler/ErrorHandlerConsoleCommands.c"
#undef GetErrorHandler
#undef ServiceLocator_GetService
#undef ServiceLocator_GetEventBus
#include "../../../../src/Core/ErrorHandler/ErrorHandlerConfig.c"
IService *TargetError_GetService(const char *name) {(void)name;return NULL;}
IEventBus *TargetError_GetEventBus(void) {return NULL;}
static SemaphoreHandle_t callbackEntered,callbackRelease;
static int32_t callbackStartResult,callbackStopResult;
static void blocked_callback(const char *tag,int32_t code) {
    (void)tag;(void)code;
    callbackStartResult=EH_Start(NULL);
    callbackStopResult=EH_Stop(NULL);
    xSemaphoreGive(callbackEntered);
    xSemaphoreTake(callbackRelease,portMAX_DELAY);
}
void target_error_cleanup(void) {
    if (callbackRelease) xSemaphoreGive(callbackRelease);
    TEST_ASSERT_EQUAL(0,EH_Stop(NULL));
    if (callbackEntered) {vSemaphoreDelete(callbackEntered);callbackEntered=NULL;}
    if (callbackRelease) {vSemaphoreDelete(callbackRelease);callbackRelease=NULL;}
}
void target_error_verify_shutdown(void) {
    IErrorHandler *handler=TargetError_GetModule();
    TEST_ASSERT_EQUAL(0,handler->base.Initialize(&handler->base,NULL));
    callbackEntered=xSemaphoreCreateBinary();callbackRelease=xSemaphoreCreateBinary();
    TEST_ASSERT_NOT_NULL(callbackEntered);TEST_ASSERT_NOT_NULL(callbackRelease);
    TEST_ASSERT_EQUAL(0,handler->RegisterCallback(blocked_callback));
    TEST_ASSERT_EQUAL(0,handler->base.Start(&handler->base));
    TEST_ASSERT_EQUAL(0,handler->HandleErrorAsync("TargetError",123));
    TEST_ASSERT_EQUAL(pdTRUE,xSemaphoreTake(callbackEntered,pdMS_TO_TICKS(2000)));
    TEST_ASSERT_EQUAL(-1,callbackStartResult);
    TEST_ASSERT_EQUAL(-1,callbackStopResult);
    TaskHandle_t worker=errorHandlerInstance.queueProcessorTask;
    QueueHandle_t queue=errorHandlerInstance.errorQueue;
    TEST_ASSERT_EQUAL(-1,handler->base.Stop(&handler->base));
    TEST_ASSERT_EQUAL_PTR(worker,errorHandlerInstance.queueProcessorTask);
    TEST_ASSERT_EQUAL_PTR(queue,errorHandlerInstance.errorQueue);
    TEST_ASSERT_EQUAL(-1,handler->base.Start(&handler->base));
    TEST_ASSERT_EQUAL(-2,handler->HandleErrorAsync("closing",1));
    xSemaphoreGive(callbackRelease);
    TEST_ASSERT_EQUAL(0,handler->base.Stop(&handler->base));
    TEST_ASSERT_EQUAL_PTR(queue,errorHandlerInstance.errorQueue);
    TEST_ASSERT_EQUAL(0,handler->base.Start(&handler->base));
    // Disable the injected blocking callback only after its worker joined.
    TEST_ASSERT_EQUAL(pdTRUE,xSemaphoreTake(errorHandlerInstance.stateMutex,pdMS_TO_TICKS(1000)));
    errorHandlerInstance.callbackCount=0;
    xSemaphoreGive(errorHandlerInstance.stateMutex);
    TEST_ASSERT_EQUAL(0,handler->PerformSelfTest());
    TEST_ASSERT_EQUAL(0,errorHandlerInstance.callbackCount);
}
