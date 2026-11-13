#include "test_mocks.h"
#include "unity.h"
#include <stdlib.h>
#include <string.h>
#undef GetEventBus
#undef vSemaphoreDelete
FAKE_VOID_FUNC1(vSemaphoreDelete, SemaphoreHandle_t);
#include "Core/EventBus/EventBus.c"
static QueuedEvent queued;
static unsigned calls;
static int32_t stop_ack;
static int observed;
static void handler(BusEvent event) { ++calls; if (event.data) memcpy(&observed,event.data,sizeof(observed)); }
static char *copy_name(const char *name) { size_t n=strlen(name)+1; char *copy=malloc(n); if(copy) memcpy(copy,name,n); return copy; }
static BaseType_t capture_queue(QueueHandle_t q,const void *item,TickType_t wait) { (void)q;(void)wait; if(q==(QueueHandle_t)4) stop_ack=*(const int32_t *)item; else queued=*(const QueuedEvent *)item; return pdPASS; }
static QueueHandle_t create_queue(UBaseType_t length,UBaseType_t size) { (void)length; return size==sizeof(int32_t)?(QueueHandle_t)4:(QueueHandle_t)1; }
static BaseType_t create_worker(TaskFunction_t f,const char *n,uint32_t size,void *arg,uint32_t priority,TaskHandle_t *out) { (void)f;(void)n;(void)size;(void)arg;(void)priority; *out=(TaskHandle_t)3; return pdPASS; }
void setUp(void) {
    IEventBus interface=*GetEventBus();
    memset(&busInstance,0,sizeof(busInstance)); busInstance.base=interface;
    RESET_FAKE(mocked_xQueueCreate); RESET_FAKE(mocked_xQueueSend); RESET_FAKE(mocked_xSemaphoreCreateMutex);
    RESET_FAKE(mocked_xSemaphoreTake); RESET_FAKE(mocked_xSemaphoreGive); RESET_FAKE(mocked_xTaskCreate);
    RESET_FAKE(mocked_vQueueDelete); RESET_FAKE(vSemaphoreDelete); RESET_FAKE(mocked_strdup);
    mocked_xQueueCreate_fake.return_val=(QueueHandle_t)1;
    mocked_xQueueCreate_fake.custom_fake=create_queue;
    mocked_xSemaphoreCreateMutex_fake.return_val=(SemaphoreHandle_t)2;
    mocked_xSemaphoreTake_fake.return_val=pdTRUE;
    mocked_xTaskCreate_fake.custom_fake=create_worker;
    mocked_xQueueSend_fake.custom_fake=capture_queue;
    mocked_strdup_fake.custom_fake=copy_name;
    RESET_FAKE(mocked_xQueueReceive); RESET_FAKE(mocked_xTaskGetCurrentTaskHandle);
    RESET_FAKE(mocked_vTaskSuspend); RESET_FAKE(mocked_vTaskDelete);
    calls=0; observed=0;
}
void tearDown(void) {
    for(uint32_t i=0;i<busInstance.subscriberCount;++i) free(busInstance.subscribers[i].eventName);
    free(busInstance.subscribers); busInstance.subscribers=NULL;
}
static BaseType_t stop_ack_receive(QueueHandle_t q,void *result,TickType_t wait) {
    (void)q; (void)wait;
    mocked_xTaskGetCurrentTaskHandle_fake.return_val=(TaskHandle_t)3;
    WatchdogWorker_Checkpoint(&busInstance.worker,NULL,0);
    mocked_xTaskGetCurrentTaskHandle_fake.return_val=(TaskHandle_t)5;
    *(int32_t *)result = stop_ack;
    return pdTRUE;
}
static IEventBus *initialize_bus(void) { IEventBus *bus=GetEventBus(); TEST_ASSERT_EQUAL(0,bus->base.Initialize(&bus->base,NULL)); return bus; }
void test_EventBus_Initialize_Success(void) {
    IEventBus *bus=initialize_bus();
    TEST_ASSERT_EQUAL(SERVICE_STATE_INITIALIZED,bus->base.GetState(&bus->base));
    TEST_ASSERT_EQUAL(2,mocked_xQueueCreate_fake.call_count);
}
void test_EventBus_Initialize_FailureCleansPartialResources(void) {
    IEventBus *bus=GetEventBus();
    mocked_xSemaphoreCreateMutex_fake.return_val=NULL;
    TEST_ASSERT_NOT_EQUAL(0,bus->base.Initialize(&bus->base,NULL));
    TEST_ASSERT_NULL(busInstance.subscribers);
    TEST_ASSERT_EQUAL(1,mocked_vQueueDelete_fake.call_count);
}
void test_EventBus_Lifecycle(void) {
    IEventBus *bus=initialize_bus();
    TEST_ASSERT_EQUAL(0,bus->base.Start(&bus->base));
    TEST_ASSERT_EQUAL(0,bus->base.CheckHealth(&bus->base));
    mocked_xQueueReceive_fake.custom_fake=stop_ack_receive;
    TEST_ASSERT_EQUAL(0,bus->base.Stop(&bus->base));
    TEST_ASSERT_EQUAL(SERVICE_STATE_STOPPED,bus->base.GetState(&bus->base));
    TEST_ASSERT_EQUAL(0,bus->base.Initialize(&bus->base,NULL));
    TEST_ASSERT_EQUAL(0,bus->base.Start(&bus->base));
}
void test_EventBus_Subscribe_Unsubscribe(void) {
    IEventBus *bus=initialize_bus();
    TEST_ASSERT_EQUAL(0,bus->Subscribe(bus,"TEST",handler));
    TEST_ASSERT_EQUAL(0,bus->Subscribe(bus,"TEST",handler));
    TEST_ASSERT_EQUAL(1,busInstance.subscriberCount);
    TEST_ASSERT_EQUAL(0,bus->Unsubscribe(bus,"TEST",handler));
    TEST_ASSERT_NOT_EQUAL(0,bus->Unsubscribe(bus,"TEST",handler));
}
void test_EventBus_FullPoolPreservesExistingSubscriber(void) {
    IEventBus *bus=initialize_bus(); busInstance.maxSubscribers=1;
    TEST_ASSERT_EQUAL(0,bus->Subscribe(bus,"THERMAL",handler));
    TEST_ASSERT_NOT_EQUAL(0,bus->Subscribe(bus,"OTHER",handler));
    TEST_ASSERT_EQUAL_STRING("THERMAL",busInstance.subscribers[0].eventName);
}
void test_EventBus_PublishCopiesPayloadAndReportsQueueFailure(void) {
    IEventBus *bus=initialize_bus(); int value=42;
    BusEvent event={.eventName="TEST",.data=&value,.dataSize=sizeof(value)};
    TEST_ASSERT_NOT_EQUAL(0,bus->Publish(event));
    TEST_ASSERT_EQUAL(0,bus->base.Start(&bus->base));
    TEST_ASSERT_EQUAL(0,bus->Publish(event)); value=99;
    int copied; memcpy(&copied,queued.payload,sizeof(copied)); TEST_ASSERT_EQUAL(42,copied);
    mocked_xQueueSend_fake.custom_fake=NULL; mocked_xQueueSend_fake.return_val=pdFAIL;
    TEST_ASSERT_NOT_EQUAL(0,bus->Publish(event));
}
void test_EventBus_WorkerDispatchRestoresInlinePointer(void) {
    IEventBus *bus=initialize_bus(); TEST_ASSERT_EQUAL(0,bus->base.Start(&bus->base));
    TEST_ASSERT_EQUAL(0,bus->Subscribe(bus,"TEST",handler));
    int value=37; TEST_ASSERT_EQUAL(0,bus->Publish((BusEvent){.eventName="TEST",.data=&value,.dataSize=sizeof(value)}));
    value=98; EventBus_DispatchQueued(&queued);
    TEST_ASSERT_EQUAL(1,calls); TEST_ASSERT_EQUAL(37,observed);
}

void test_EventBus_InvalidSubscriptionsAndAllocationFailure(void) {
    IEventBus *bus=GetEventBus();
    TEST_ASSERT_NOT_EQUAL(0,bus->Subscribe(bus,"TEST",handler));
    initialize_bus();
    mocked_strdup_fake.custom_fake=NULL;
    TEST_ASSERT_NOT_EQUAL(0,bus->Subscribe(bus,"TEST",handler));
    TEST_ASSERT_EQUAL(0,busInstance.subscriberCount);
    TEST_ASSERT_NOT_EQUAL(0,bus->Unsubscribe(bus,NULL,handler));
    mocked_xSemaphoreTake_fake.return_val=pdFALSE;
    TEST_ASSERT_NOT_EQUAL(0,bus->Subscribe(bus,"TEST",handler));
    TEST_ASSERT_EQUAL(0,busInstance.subscriberCount);
}

void test_EventBus_CopiesLargePayloadAndMutableName(void) {
    IEventBus *bus=initialize_bus(); TEST_ASSERT_EQUAL(0,bus->base.Start(&bus->base));
    char name[]="TEMPORARY";
    unsigned char payload[EVENT_BUS_PAYLOAD_CAPACITY]; memset(payload,0xA5,sizeof(payload));
    TEST_ASSERT_EQUAL(0,bus->Publish((BusEvent){.eventName=name,.data=payload,.dataSize=sizeof(payload)}));
    memset(payload,0,sizeof(payload)); name[0]='X';
    TEST_ASSERT_EQUAL_STRING("TEMPORARY",queued.name);
    for(size_t i=0;i<sizeof(payload);++i) TEST_ASSERT_EQUAL_HEX8(0xA5,queued.payload[i]);
}

void test_EventBus_InvalidPayloadAndNameRejected(void) {
    IEventBus *bus=initialize_bus(); TEST_ASSERT_EQUAL(0,bus->base.Start(&bus->base));
    char name[EVENT_BUS_NAME_CAPACITY+1]; memset(name,'n',sizeof(name)); name[sizeof(name)-1]=0;
    unsigned char data[EVENT_BUS_PAYLOAD_CAPACITY+1]={0};
    TEST_ASSERT_NOT_EQUAL(0,bus->Publish((BusEvent){.eventName=name}));
    TEST_ASSERT_NOT_EQUAL(0,bus->Publish((BusEvent){.eventName=""}));
    TEST_ASSERT_NOT_EQUAL(0,bus->Publish((BusEvent){.eventName="TEST",.dataSize=1}));
    TEST_ASSERT_NOT_EQUAL(0,bus->Publish((BusEvent){.eventName="TEST",.data=data,.dataSize=sizeof(data)}));
    TEST_ASSERT_EQUAL(0,mocked_xQueueSend_fake.call_count);
}

static BaseType_t capture_isr_queue(QueueHandle_t queue,const void *item,BaseType_t *woken) {
    (void)queue; queued=*(const QueuedEvent *)item; *woken=pdTRUE; return pdPASS;
}
void test_EventBus_ISRPayloadOwnershipAndFailureWakeFlag(void) {
    IEventBus *bus=initialize_bus(); TEST_ASSERT_EQUAL(0,bus->base.Start(&bus->base));
    RESET_FAKE(mocked_xQueueSendFromISR);
    mocked_xQueueSendFromISR_fake.custom_fake=capture_isr_queue;
    unsigned char payload[44]; memset(payload,0x4C,sizeof(payload)); bool woken=false;
    TEST_ASSERT_EQUAL(0,bus->PublishFromISR((BusEvent){.eventName="ISR",.data=payload,.dataSize=sizeof(payload)},&woken));
    TEST_ASSERT_TRUE(woken); memset(payload,0,sizeof(payload));
    for(size_t i=0;i<sizeof(payload);++i) TEST_ASSERT_EQUAL_HEX8(0x4C,queued.payload[i]);
    mocked_xQueueSendFromISR_fake.custom_fake=NULL;
    mocked_xQueueSendFromISR_fake.return_val=pdFAIL;
    TEST_ASSERT_NOT_EQUAL(0,bus->PublishFromISR((BusEvent){.eventName="ISR"},&woken));
    TEST_ASSERT_FALSE(woken);
}

void test_EventBus_StopTimeoutPreservesResourcesAndRejectsOldGeneration(void) {
    IEventBus *bus=initialize_bus(); TEST_ASSERT_EQUAL(0,bus->base.Start(&bus->base));
    TEST_ASSERT_EQUAL(0,bus->Subscribe(bus,"TEST",handler));
    int value=55;
    TEST_ASSERT_EQUAL(0,bus->Publish((BusEvent){.eventName="TEST",.data=&value,.dataSize=sizeof(value)}));
    QueuedEvent old=queued;
    QueueHandle_t queue=busInstance.eventQueue; SemaphoreHandle_t mutex=busInstance.busMutex;
    TEST_ASSERT_NOT_EQUAL(0,bus->base.Stop(&bus->base));
    TEST_ASSERT_EQUAL(0,mocked_vTaskDelete_fake.call_count);
    TEST_ASSERT_EQUAL_PTR(queue,busInstance.eventQueue);
    TEST_ASSERT_EQUAL_PTR(mutex,busInstance.busMutex);
    TEST_ASSERT_NOT_EQUAL(0,bus->base.Start(&bus->base));
    TEST_ASSERT_NOT_EQUAL(0,bus->Publish((BusEvent){.eventName="TEST"}));
    mocked_xQueueReceive_fake.custom_fake=stop_ack_receive;
    TEST_ASSERT_EQUAL(0,bus->base.Stop(&bus->base));
    TEST_ASSERT_EQUAL(1,mocked_vTaskSuspend_fake.call_count);
    TEST_ASSERT_EQUAL(1,mocked_vTaskDelete_fake.call_count);
    TEST_ASSERT_EQUAL(0,mocked_vQueueDelete_fake.call_count);
    TEST_ASSERT_EQUAL(0,bus->base.Start(&bus->base));
    EventBus_DispatchQueued(&old); TEST_ASSERT_EQUAL(0,calls);
    TEST_ASSERT_EQUAL(0,bus->Publish((BusEvent){.eventName="TEST",.data=&value,.dataSize=sizeof(value)}));
    EventBus_DispatchQueued(&queued); TEST_ASSERT_EQUAL(1,calls);
}

void test_EventBus_ConfigurationOwnedAndFailedStartReported(void) {
    IEventBus *bus=GetEventBus();
    EventBusConfig config=*EventBusConfig_GetDefault(); char name[]="test_worker"; config.taskName=name;
    TEST_ASSERT_EQUAL(0,bus->InitializeWithConfig(bus,&config,NULL));
    name[0]='X'; config.taskStackSize=1;
    TEST_ASSERT_EQUAL_STRING("test_worker",busInstance.config->taskName);
    TEST_ASSERT_EQUAL(4096,busInstance.config->taskStackSize);
    mocked_xTaskCreate_fake.custom_fake=NULL; mocked_xTaskCreate_fake.return_val=pdFAIL;
    TEST_ASSERT_NOT_EQUAL(0,bus->base.Start(&bus->base));
    TEST_ASSERT_NOT_EQUAL(0,bus->base.CheckHealth(&bus->base));
    TEST_ASSERT_EQUAL(SERVICE_STATE_INITIALIZED,busInstance.state);
}

static int32_t removal_during_dispatch;
static void removing_handler(BusEvent event) {
    removal_during_dispatch = GetEventBus()->Unsubscribe(GetEventBus(), event.eventName, handler);
}
void test_EventBus_UnsubscribeCannotReleasePendingSnapshotCallback(void) {
    IEventBus *bus = initialize_bus();
    TEST_ASSERT_EQUAL(0, bus->Subscribe(bus, "LIFETIME", removing_handler));
    TEST_ASSERT_EQUAL(0, bus->Subscribe(bus, "LIFETIME", handler));
    BusEvent event = {.eventName = "LIFETIME"};
    EventBus_Dispatch(&event);
    TEST_ASSERT_NOT_EQUAL(0, removal_during_dispatch);
    TEST_ASSERT_EQUAL(1, calls);
    TEST_ASSERT_EQUAL(0, bus->Unsubscribe(bus, "LIFETIME", handler));
    EventBus_Dispatch(&event);
    TEST_ASSERT_EQUAL(1, calls);
}
