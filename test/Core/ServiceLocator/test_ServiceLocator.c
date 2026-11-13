#include "unity.h"
#include "Infrastructure/support/test_mocks.h"
#include <stdlib.h>
#include <string.h>
#undef ServiceLocator_GetService
#undef ServiceLocator_GetEventBus

static bool fail_malloc, fail_calloc;
static void *locator_test_malloc(size_t size) { return fail_malloc ? NULL : malloc(size); }
static void *locator_test_calloc(size_t count, size_t size) { return fail_calloc ? NULL : calloc(count, size); }
#define malloc locator_test_malloc
#define calloc locator_test_calloc
#include "../../../src/Core/ServiceLocator/ServiceLocator.c"
#undef malloc
#undef calloc
#include "../../../src/Core/ServiceLocator/ServiceLocatorConfig.c"
#include "../../../src/Core/ServiceLocator/ServiceLocatorConsoleCommands.c"

static char *copy_name(const char *name) {
    char *copy = malloc(strlen(name) + 1);
    if (copy) strcpy(copy, name);
    return copy;
}
static ServiceState service_state(IService *self) { (void)self; return SERVICE_STATE_INITIALIZED; }
static int32_t start_result, stop_result;
static unsigned starts, stops;
static IService *stop_order[4];
static int32_t service_start(IService *self) { (void)self; ++starts; return start_result; }
static int32_t service_stop(IService *self) { stop_order[stops++] = self; return stop_result; }
static IService first, second;
void setUp(void) {
    RESET_FAKE(mocked_xSemaphoreCreateMutex); RESET_FAKE(mocked_xSemaphoreTake);
    RESET_FAKE(mocked_xSemaphoreGive); RESET_FAKE(mocked_strdup);
    mocked_xSemaphoreCreateMutex_fake.return_val = (SemaphoreHandle_t)1;
    mocked_xSemaphoreTake_fake.return_val = pdTRUE;
    mocked_strdup_fake.custom_fake = copy_name;
    fail_malloc = fail_calloc = false;
    starts = stops = 0; start_result = stop_result = 0;
    atomic_store(&lifecycleActive, false);
    ServiceLocator_Deinit();
    first = (IService){.Start = service_start, .Stop = service_stop, .GetState = service_state};
    second = first;
}
void tearDown(void) {
    mocked_xSemaphoreTake_fake.return_val = pdTRUE;
    fail_malloc = fail_calloc = false;
    atomic_store(&lifecycleActive, false);
    TEST_ASSERT_EQUAL(0, ServiceLocator_Deinit());
}
void test_ServiceLocator_ValidatesAndOwnsConfiguration(void) {
    ServiceLocatorConfig config = *ServiceLocatorConfig_GetDefault();
    config.maxServices = 0;
    TEST_ASSERT_NOT_EQUAL(0, ServiceLocator_Init(&config));
    config.maxServices = SERVICE_LOCATOR_MAX_SERVICES + 1;
    TEST_ASSERT_NOT_EQUAL(0, ServiceLocator_Init(&config));
    config.maxServices = 2; config.locatorTag = NULL;
    TEST_ASSERT_NOT_EQUAL(0, ServiceLocator_Init(&config));
    char tag[] = "owned tag"; config.locatorTag = tag;
    fail_calloc = true;
    TEST_ASSERT_NOT_EQUAL(0, ServiceLocator_Init(&config));
    TEST_ASSERT_FALSE(isInitialized);
    fail_calloc = false;
    TEST_ASSERT_EQUAL(0, ServiceLocator_Init(&config));
    tag[0] = 'X';
    TEST_ASSERT_EQUAL_STRING("owned tag", activeConfig.locatorTag);
}
void test_ServiceLocator_MutexAndLockFailuresCannotAdmitOperations(void) {
    locatorMutex = NULL;
    mocked_xSemaphoreCreateMutex_fake.return_val = NULL;
    TEST_ASSERT_NOT_EQUAL(0, ServiceLocator_Init(NULL));
    TEST_ASSERT_NULL(ServiceLocator_GetService("first"));
    mocked_xSemaphoreCreateMutex_fake.return_val = (SemaphoreHandle_t)1;
    mocked_xSemaphoreTake_fake.return_val = pdFALSE;
    TEST_ASSERT_NOT_EQUAL(0, ServiceLocator_RegisterService("first", &first));
    TEST_ASSERT_FALSE(isInitialized);
    TEST_ASSERT_EQUAL(pdMS_TO_TICKS(SERVICE_LOCATOR_LOCK_TIMEOUT_MS), mocked_xSemaphoreTake_fake.arg1_val);
}
void test_ServiceLocator_RejectsInvalidAndConflictingBindings(void) {
    TEST_ASSERT_NOT_EQUAL(0, ServiceLocator_RegisterService(NULL, &first));
    TEST_ASSERT_NOT_EQUAL(0, ServiceLocator_RegisterService("", &first));
    TEST_ASSERT_NOT_EQUAL(0, ServiceLocator_RegisterService("first", NULL));
    char long_name[SERVICE_LOCATOR_NAME_CAPACITY + 1]; memset(long_name, 'a', sizeof(long_name));
    long_name[sizeof(long_name) - 1] = 0;
    TEST_ASSERT_NOT_EQUAL(0, ServiceLocator_RegisterService(long_name, &first));
    ServiceLocatorConfig config = *ServiceLocatorConfig_GetDefault(); config.maxServices = 1;
    TEST_ASSERT_EQUAL(0, ServiceLocator_Init(&config));
    TEST_ASSERT_EQUAL(0, ServiceLocator_RegisterService("first", &first));
    TEST_ASSERT_EQUAL(0, ServiceLocator_RegisterService("first", &first));
    TEST_ASSERT_EQUAL(-3, ServiceLocator_RegisterService("first", &second));
    TEST_ASSERT_EQUAL(-4, ServiceLocator_RegisterService("second", &second));
    TEST_ASSERT_EQUAL_PTR(&first, ServiceLocator_GetService("first"));
}
void test_ServiceLocator_FailedNameAllocationPreservesRegistry(void) {
    mocked_strdup_fake.custom_fake = NULL;
    TEST_ASSERT_NOT_EQUAL(0, ServiceLocator_RegisterService("first", &first));
    TEST_ASSERT_EQUAL(0, locatorInstance.serviceCount);
    TEST_ASSERT_NULL(ServiceLocator_GetService("first"));
    mocked_strdup_fake.custom_fake = copy_name;
    TEST_ASSERT_EQUAL(0, ServiceLocator_RegisterService("first", &first));
}
void test_ServiceLocator_NameCopiesSurviveUnregisterAndRejectSmallBuffers(void) {
    char name[] = "first";
    TEST_ASSERT_EQUAL(0, ServiceLocator_RegisterService(name, &first));
    name[0] = 'X';
    char copy[16] = "unchanged";
    TEST_ASSERT_NOT_EQUAL(0, ServiceLocator_CopyServiceName(&first, copy, 2));
    TEST_ASSERT_EQUAL_STRING("unchanged", copy);
    TEST_ASSERT_EQUAL(0, ServiceLocator_CopyServiceName(&first, copy, sizeof(copy)));
    TEST_ASSERT_EQUAL(0, GetServiceLocator()->UnregisterService("first"));
    TEST_ASSERT_EQUAL_STRING("first", copy);
    TEST_ASSERT_NULL(ServiceLocator_GetService("first"));
}
void test_ServiceLocator_SnapshotFailurePreservesCallerOutputs(void) {
    TEST_ASSERT_EQUAL(0, ServiceLocator_RegisterService("first", &first));
    IService *services[2] = {&second, &second}; uint32_t count = 99;
    TEST_ASSERT_NOT_EQUAL(0, ServiceLocator_CopyServices(services, 0, &count));
    TEST_ASSERT_EQUAL(99, count); TEST_ASSERT_EQUAL_PTR(&second, services[0]);
    mocked_xSemaphoreTake_fake.return_val = pdFALSE;
    TEST_ASSERT_NOT_EQUAL(0, ServiceLocator_CopyServices(services, 2, &count));
    TEST_ASSERT_EQUAL(99, count);
    mocked_xSemaphoreTake_fake.return_val = pdTRUE;
    TEST_ASSERT_EQUAL(0, ServiceLocator_CopyServices(services, 2, &count));
    TEST_ASSERT_EQUAL(1, count); TEST_ASSERT_EQUAL_PTR(&first, services[0]);
    fail_malloc = true;
    TEST_ASSERT_NULL(ServiceLocator_GetAllServices(&count)); TEST_ASSERT_EQUAL(0, count);
    TEST_ASSERT_NULL(ServiceLocator_GetAllServices(NULL));
}
static int32_t lifecycle_reentry(IService *self) {
    TEST_ASSERT_EQUAL_PTR(self, ServiceLocator_GetService("first"));
    TEST_ASSERT_EQUAL(-2, GetServiceLocator()->UnregisterService("first"));
    TEST_ASSERT_EQUAL(0, ServiceLocator_RegisterService("second", &second));
    TEST_ASSERT_EQUAL(-2, ServiceLocator_Deinit());
    TEST_ASSERT_EQUAL(-2, GetServiceLocator()->StartAllServices());
    TEST_ASSERT_EQUAL(-2, GetServiceLocator()->StopAllServices());
    char name[16]; TEST_ASSERT_EQUAL(0, ServiceLocator_CopyServiceName(self, name, sizeof(name)));
    TEST_ASSERT_EQUAL_STRING("first", name);
    return 0;
}
void test_ServiceLocator_LifecycleCallbacksAllowReadsAndReserveRegistryLifetime(void) {
    first.Start = lifecycle_reentry;
    TEST_ASSERT_EQUAL(0, ServiceLocator_RegisterService("first", &first));
    TEST_ASSERT_EQUAL(0, GetServiceLocator()->StartAllServices());
    TEST_ASSERT_EQUAL(0, starts); // Appended bindings are outside the admitted snapshot.
    TEST_ASSERT_FALSE(atomic_load(&lifecycleActive));
    TEST_ASSERT_EQUAL(0, GetServiceLocator()->UnregisterService("first"));
    TEST_ASSERT_EQUAL(0, GetServiceLocator()->StartAllServices());
    TEST_ASSERT_EQUAL(1, starts);
}
void test_ServiceLocator_LifecycleFailureReleasesAdmissionAndPropagatesResult(void) {
    TEST_ASSERT_EQUAL(0, ServiceLocator_RegisterService("first", &first));
    TEST_ASSERT_EQUAL(0, ServiceLocator_RegisterService("second", &second));
    start_result = -17;
    TEST_ASSERT_EQUAL(-17, GetServiceLocator()->StartAllServices()); TEST_ASSERT_EQUAL(1, starts);
    TEST_ASSERT_FALSE(atomic_load(&lifecycleActive));
    start_result = 0;
    TEST_ASSERT_EQUAL(0, GetServiceLocator()->StartAllServices());
    stop_result = -9;
    TEST_ASSERT_EQUAL(-9, GetServiceLocator()->StopAllServices()); TEST_ASSERT_EQUAL(2, stops);
    TEST_ASSERT_EQUAL_PTR(&second, stop_order[0]); TEST_ASSERT_EQUAL_PTR(&first, stop_order[1]);
    TEST_ASSERT_EQUAL(0, ServiceLocator_Deinit());
}
void test_ServiceLocator_LifecycleAllocationFailureDoesNotRunCallbacks(void) {
    TEST_ASSERT_EQUAL(0, ServiceLocator_Init(NULL));
    TEST_ASSERT_EQUAL(0, GetServiceLocator()->StartAllServices());
    TEST_ASSERT_EQUAL(0, ServiceLocator_RegisterService("first", &first));
    fail_malloc = true;
    TEST_ASSERT_NOT_EQUAL(0, GetServiceLocator()->StartAllServices());
    TEST_ASSERT_EQUAL(0, starts); TEST_ASSERT_FALSE(atomic_load(&lifecycleActive));
}
void test_ServiceLocator_ConsoleHandlesMissingStateAndSnapshotFailure(void) {
    first.GetState = NULL;
    TEST_ASSERT_EQUAL(0, ServiceLocator_RegisterService("first", &first));
    char *args[] = {"svc_get", "first"};
    TEST_ASSERT_NOT_EQUAL(0, cmd_svc_get(2, args));
    mocked_xSemaphoreTake_fake.return_val = pdFALSE;
    TEST_ASSERT_NOT_EQUAL(0, cmd_svc_list(1, args));
}

void test_ServiceLocator_MissingStartCannotClaimSuccessfulBatch(void) {
    first.Start=NULL;
    TEST_ASSERT_EQUAL(0,ServiceLocator_RegisterService("lookup-only",&first));
    TEST_ASSERT_EQUAL(0,ServiceLocator_RegisterService("second",&second));
    TEST_ASSERT_EQUAL_PTR(&first,ServiceLocator_GetService("lookup-only"));
    TEST_ASSERT_NOT_EQUAL(0,GetServiceLocator()->StartAllServices());
    TEST_ASSERT_EQUAL(0,starts);TEST_ASSERT_FALSE(atomic_load(&lifecycleActive));
    TEST_ASSERT_EQUAL(0,GetServiceLocator()->UnregisterService("lookup-only"));
    TEST_ASSERT_EQUAL(0,GetServiceLocator()->StartAllServices());TEST_ASSERT_EQUAL(1,starts);
}
void test_ServiceLocator_MissingStopReportsErrorAndStillStopsOtherOwners(void) {
    second.Stop=NULL;
    TEST_ASSERT_EQUAL(0,ServiceLocator_RegisterService("first",&first));
    TEST_ASSERT_EQUAL(0,ServiceLocator_RegisterService("lookup-only",&second));
    TEST_ASSERT_NOT_EQUAL(0,GetServiceLocator()->StopAllServices());
    TEST_ASSERT_EQUAL(1,stops);TEST_ASSERT_EQUAL_PTR(&first,stop_order[0]);
    TEST_ASSERT_FALSE(atomic_load(&lifecycleActive));
    TEST_ASSERT_EQUAL(0,ServiceLocator_Deinit());
}

void test_ServiceLocator_ConsoleRejectsMalformedArityBeforeLookup(void) {
    char *list[]={"svc_list","extra"};char *get[]={"svc_get","first","extra"};
    unsigned locks=mocked_xSemaphoreTake_fake.call_count;
    TEST_ASSERT_NOT_EQUAL(0,cmd_svc_list(2,list));TEST_ASSERT_NOT_EQUAL(0,cmd_svc_list(1,NULL));
    TEST_ASSERT_NOT_EQUAL(0,cmd_svc_get(3,get));TEST_ASSERT_NOT_EQUAL(0,cmd_svc_get(2,NULL));
    get[1]=NULL;TEST_ASSERT_NOT_EQUAL(0,cmd_svc_get(2,get));
    TEST_ASSERT_EQUAL(locks,mocked_xSemaphoreTake_fake.call_count);
}

void test_ServiceLocator_AliasesRunEachOwnerOnceInDependencyOrder(void) {
    TEST_ASSERT_EQUAL(0,ServiceLocator_RegisterService("first",&first));
    TEST_ASSERT_EQUAL(0,ServiceLocator_RegisterService("second",&second));
    TEST_ASSERT_EQUAL(0,ServiceLocator_RegisterService("first_alias",&first));
    TEST_ASSERT_EQUAL(0,GetServiceLocator()->StartAllServices());TEST_ASSERT_EQUAL(2,starts);
    TEST_ASSERT_EQUAL(0,GetServiceLocator()->StopAllServices());TEST_ASSERT_EQUAL(2,stops);
    TEST_ASSERT_EQUAL_PTR(&second,stop_order[0]);TEST_ASSERT_EQUAL_PTR(&first,stop_order[1]);
}
void test_ServiceLocator_BindingSnapshotsKeepDistinctNamesAfterRemoval(void) {
    char name[]="first";
    TEST_ASSERT_EQUAL(0,ServiceLocator_RegisterService(name,&first));
    TEST_ASSERT_EQUAL(0,ServiceLocator_RegisterService("alias",&first));
    name[0]='X';ServiceLocatorBinding output[2];uint32_t count=99;
    TEST_ASSERT_EQUAL(0,ServiceLocator_CopyBindings(output,2,&count));TEST_ASSERT_EQUAL(2,count);
    TEST_ASSERT_EQUAL_STRING("first",output[0].name);TEST_ASSERT_EQUAL_STRING("alias",output[1].name);
    TEST_ASSERT_EQUAL_PTR(&first,output[0].service);TEST_ASSERT_EQUAL_PTR(&first,output[1].service);
    TEST_ASSERT_EQUAL(0,GetServiceLocator()->UnregisterService("first"));
    TEST_ASSERT_EQUAL(0,GetServiceLocator()->UnregisterService("alias"));
    TEST_ASSERT_EQUAL_STRING("first",output[0].name);TEST_ASSERT_EQUAL_STRING("alias",output[1].name);
}
void test_ServiceLocator_BindingFailurePreservesOwnedSnapshotAndCount(void) {
    TEST_ASSERT_EQUAL(0,ServiceLocator_RegisterService("first",&first));
    TEST_ASSERT_EQUAL(0,ServiceLocator_RegisterService("second",&second));
    ServiceLocatorBinding output[2],before[2];memset(output,0xA5,sizeof(output));memcpy(before,output,sizeof(output));
    uint32_t count=99;
    TEST_ASSERT_NOT_EQUAL(0,ServiceLocator_CopyBindings(output,1,&count));
    TEST_ASSERT_EQUAL(99,count);TEST_ASSERT_EQUAL_MEMORY(before,output,sizeof(output));
    mocked_xSemaphoreTake_fake.return_val=pdFALSE;
    TEST_ASSERT_NOT_EQUAL(0,ServiceLocator_CopyBindings(output,2,&count));
    TEST_ASSERT_EQUAL(99,count);TEST_ASSERT_EQUAL_MEMORY(before,output,sizeof(output));
}

void test_ServiceLocator_OwnerSnapshotsDoNotDuplicateAliases(void) {
    TEST_ASSERT_EQUAL(0,ServiceLocator_RegisterService("first",&first));
    TEST_ASSERT_EQUAL(0,ServiceLocator_RegisterService("alias",&first));
    IService *output[1]={NULL};uint32_t count=99;
    TEST_ASSERT_EQUAL(0,ServiceLocator_CopyServices(output,1,&count));
    TEST_ASSERT_EQUAL(1,count);TEST_ASSERT_EQUAL_PTR(&first,output[0]);
    IService **allocated=ServiceLocator_GetAllServices(&count);
    TEST_ASSERT_NOT_NULL(allocated);TEST_ASSERT_EQUAL(1,count);TEST_ASSERT_EQUAL_PTR(&first,allocated[0]);free(allocated);
    ServiceLocatorBinding bindings[2];TEST_ASSERT_EQUAL(0,ServiceLocator_CopyBindings(bindings,2,&count));
    TEST_ASSERT_EQUAL(2,count);
}
