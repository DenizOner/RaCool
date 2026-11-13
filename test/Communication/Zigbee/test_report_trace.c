#include "unity.h"
#include "esp_log.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
static char trace_log[1024];
static void CaptureTrace(const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    size_t used=strlen(trace_log);
    vsnprintf(trace_log+used, sizeof(trace_log)-used, format, arguments);
    va_end(arguments);
}
#undef ESP_LOGI
#define ESP_LOGI(tag, format, ...) CaptureTrace(format, ##__VA_ARGS__)
static char *pcTaskGetName(TaskHandle_t task) { (void)task; return "report_worker"; }
static UBaseType_t uxTaskPriorityGet(TaskHandle_t task) { (void)task; return 4; }
#include "diagnostic/report_trace.c"

static int report_result, aps_result;
static uint8_t init_result, send_result;
static bool foreign_aps;
static bool clone_in_report;
static const void *forwarded_request;
static void *zmsg_result;
static unsigned zmsg_length;
static void *pool_result, *freed_pointer;
static unsigned pool_argument, freed_pool;
static const void *clone_argument;
static void *clone_result;
static bool clone_allocates;
static bool zmsg_allocates, init_allocates;
static ezb_err_t inner_report_result;
static uint8_t footer_result;
static const void *footer_data;
static unsigned footer_length;
static ezb_zdo_nwk_mgmt_bind_req_t bind_request;
static ezb_err_t bind_submit_result;
static unsigned bind_submissions;
ezb_shortaddr_t ezb_nwk_get_short_address(void) { return 0x1234; }
ezb_err_t ezb_zdo_nwk_mgmt_bind_req(const ezb_zdo_nwk_mgmt_bind_req_t *request) {
    bind_request=*request; ++bind_submissions; return bind_submit_result;
}
void *__real_zmsg_alloc(unsigned length) {
    zmsg_length=length;
    if (zmsg_allocates) (void)__wrap_mempool_malloc(0);
    return zmsg_result;
}
void *__real_zmsg_clone(const void *message) {
    clone_argument=message;
    if (clone_allocates) (void)__wrap_mempool_malloc(0);
    return clone_result;
}
uint8_t __real_zmsg_add_footer(void *message, const void *data, uint16_t length) {
    forwarded_request=message; footer_data=data; footer_length=length; return footer_result;
}
void *__real_mempool_malloc(unsigned pool) { pool_argument=pool; return pool_result; }
void __real_mempool_free(unsigned pool, void *pointer) { freed_pool=pool; freed_pointer=pointer; }
uint8_t __real_zcl_packet_init(void *packet, bool manufacturer_specific) {
    (void)manufacturer_specific; forwarded_request = packet;
    if (init_allocates) (void)__wrap_zmsg_alloc(47);
    return init_result;
}
ezb_err_t __real_zcl_report_attr_cmd_req(const ezb_zcl_report_attr_cmd_t *request) {
    (void)__wrap_zcl_packet_init((void *)request,false);
    return inner_report_result;
}
ezb_err_t __real_aps_apsde_data_request(const void *request) {
    forwarded_request = request; return aps_result;
}
uint8_t __real_zcl_packet_send(void *packet, const void *confirmation) {
    (void)confirmation;
    if (foreign_aps) mocked_xTaskGetCurrentTaskHandle_fake.return_val = (TaskHandle_t)2;
    __wrap_aps_apsde_data_request(packet);
    mocked_xTaskGetCurrentTaskHandle_fake.return_val = (TaskHandle_t)1;
    return send_result;
}
ezb_err_t __real_ezb_zcl_report_attr_cmd_req(const ezb_zcl_report_attr_cmd_t *request) {
    if (!__wrap_zcl_packet_init((void *)request, false)) __wrap_zcl_packet_send((void *)request, NULL);
    if (clone_in_report) (void)__wrap_zmsg_clone(request);
    return report_result;
}
void setUp(void) {
    report_result = aps_result = init_result = send_result = 0;
    foreign_aps = false; forwarded_request = NULL; trace_log[0] = 0;
    mocked_esp_timer_get_time_fake.return_val=0;
    bind_submit_result=EZB_ERR_NONE; bind_submissions=0; bind_probe_start=0;
    atomic_store(&bind_probe_state,0);
    for (unsigned i=0;i<BIND_PROBE_CAPACITY;++i) atomic_store(&bind_probe_contexts[i].active,false);
    clone_in_report=false;
    mocked_xTaskGetCurrentTaskHandle_fake.return_val = (TaskHandle_t)1;
    atomic_store(&report_trace_sequence, 0);
    atomic_store(&zmsg_trace_next,0);
    for (unsigned i=0;i<ZMSG_TRACE_SAMPLES;++i) atomic_store(&zmsg_trace_samples[i],0);
    zmsg_result=NULL; zmsg_length=0;
    pool_result=NULL; freed_pointer=NULL; pool_argument=freed_pool=0;
    clone_argument=NULL; clone_result=NULL; clone_allocates=false;
    zmsg_allocates=init_allocates=false;
    inner_report_result=EZB_ERR_NONE;
    footer_result=0; footer_data=NULL; footer_length=0; atomic_store(&footer_trace_next,0);
    for (unsigned i=0;i<ZMSG_TRACE_SAMPLES;++i) atomic_store(&footer_trace_samples[i],0);
    atomic_store(&allocation_event_next,0);
    atomic_store(&allocation_dumped,false);
    atomic_store(&pool_drain_last_ms,0);
    atomic_store(&buffer_pool_live,0);
    atomic_store(&buffer_pool_peak,0);
    atomic_store(&buffer_pool_unmatched_frees,0);
    atomic_store(&buffer_trace_overflow,0);
    atomic_store(&buffer_trace_unmatched,0);
    for (unsigned i=0;i<BUFFER_TRACE_SLOTS;++i) {
        atomic_store(&buffer_trace[i].pointer,0);
        atomic_store(&buffer_trace[i].origin,0);
    }
    for (unsigned i=0;i<ALLOC_CONTEXT_SLOTS;++i) {
        atomic_store(&allocation_contexts[i].task,NULL);
        atomic_store(&allocation_contexts[i].origin,0);
    }
    for (unsigned i=0;i<ALLOCATION_EVENT_CAPACITY;++i) atomic_store(&allocation_events[i].stamp,0);
}
void tearDown(void) {
    TEST_ASSERT_NULL(atomic_load(&report_trace_owner));
    TEST_ASSERT_NULL(report_trace_current);
}
static ezb_zcl_report_attr_cmd_t Request(void) {
    ezb_zcl_report_attr_cmd_t request = {.cmd_ctrl={.src_ep=1,.cluster_id=0x204},.payload={.attr_id=0x805e}};
    return request;
}
void test_ReportTracePreservesSuccessAndForwardsOpaquePointers(void) {
    ezb_zcl_report_attr_cmd_t request = Request();
    TEST_ASSERT_EQUAL(0, __wrap_ezb_zcl_report_attr_cmd_req(&request));
    TEST_ASSERT_EQUAL_PTR(&request, forwarded_request);
    TEST_ASSERT_NOT_NULL(strstr(trace_log, "init_calls=1 init_result=0 send_calls=1 send_result=0 aps_calls=1 aps_result=0"));
}
void test_ReportTracePreservesGenericErrorAndUnderlyingApsReason(void) {
    ezb_zcl_report_attr_cmd_t request = Request();
    report_result = -1; aps_result = 936; send_result = 1;
    TEST_ASSERT_EQUAL(-1, __wrap_ezb_zcl_report_attr_cmd_req(&request));
    TEST_ASSERT_NOT_NULL(strstr(trace_log, "send_result=1 aps_calls=1 aps_result=936"));
}
void test_ReportTraceDistinguishesPacketAllocationFromApsFailure(void) {
    ezb_zcl_report_attr_cmd_t request = Request();
    report_result = -1; init_result = 137;
    TEST_ASSERT_EQUAL(-1, __wrap_ezb_zcl_report_attr_cmd_req(&request));
    TEST_ASSERT_NOT_NULL(strstr(trace_log, "init_result=137 send_calls=0 send_result=0 aps_calls=0"));
}
void test_ReportTraceDoesNotAttributeForeignTaskTraffic(void) {
    ezb_zcl_report_attr_cmd_t request = Request();
    foreign_aps = true; aps_result = 936;
    TEST_ASSERT_EQUAL(0, __wrap_ezb_zcl_report_attr_cmd_req(&request));
    TEST_ASSERT_NOT_NULL(strstr(trace_log, "aps_calls=0 aps_result=0"));
}
void test_ReportTraceStandaloneHooksPreserveSdkResultsWithoutOwner(void) {
    int packet = 0; aps_result = 942; init_result = 137;
    TEST_ASSERT_EQUAL(942, __wrap_aps_apsde_data_request(&packet));
    TEST_ASSERT_EQUAL(137, __wrap_zcl_packet_init(&packet, false));
    TEST_ASSERT_EQUAL_PTR(&packet, forwarded_request);
    TEST_ASSERT_EQUAL_STRING("", trace_log);
}

void test_ReportTraceMessageAllocationProbePreservesSdkResult(void) {
    int packet=0;
    zmsg_result=&packet;
    TEST_ASSERT_EQUAL_PTR(&packet, __wrap_zmsg_alloc(47));
    TEST_ASSERT_EQUAL(47,zmsg_length);
    zmsg_result=NULL;
    TEST_ASSERT_NULL(__wrap_zmsg_alloc(91));
    TEST_ASSERT_EQUAL(91,zmsg_length);
    TEST_ASSERT_EQUAL(2,atomic_load(&zmsg_trace_next));
    TEST_ASSERT_EQUAL(0,atomic_load(&zmsg_trace_samples[0]) & 1u);
    TEST_ASSERT_EQUAL(1,atomic_load(&zmsg_trace_samples[1]) & 1u);
    ezb_zcl_report_attr_cmd_t request=Request();
    report_result=-1;
    TEST_ASSERT_EQUAL(-1,__wrap_ezb_zcl_report_attr_cmd_req(&request));
    TEST_ASSERT_NOT_NULL(strstr(trace_log,"samples=2 failed=1"));
}

void test_ReportTraceMessageSampleRingRemainsBoundedAfterWrap(void) {
    int packet=0;
    for (unsigned i=0;i<70;++i) {
        zmsg_result=(i%10u)==0 ? NULL : &packet;
        (void)__wrap_zmsg_alloc(i);
    }
    ezb_zcl_report_attr_cmd_t request=Request();
    report_result=-1;
    TEST_ASSERT_EQUAL(-1,__wrap_ezb_zcl_report_attr_cmd_req(&request));
    TEST_ASSERT_NOT_NULL(strstr(trace_log,"samples=64 failed=6"));
}

void test_ReportTracePoolHooksForwardResultAndRecordFree(void) {
    int packet=0;
    pool_result=&packet;
    TEST_ASSERT_EQUAL_PTR(&packet,__wrap_mempool_malloc(1));
    TEST_ASSERT_EQUAL(1,pool_argument);
    __wrap_mempool_free(1,&packet);
    TEST_ASSERT_EQUAL(1,freed_pool);
    TEST_ASSERT_EQUAL_PTR(&packet,freed_pointer);
    TEST_ASSERT_EQUAL(2,atomic_load(&allocation_event_next));
    TEST_ASSERT_EQUAL('M',atomic_load(&allocation_events[0].kind));
    TEST_ASSERT_EQUAL('F',atomic_load(&allocation_events[1].kind));
    TEST_ASSERT_EQUAL_PTR(&packet,(void *)atomic_load(&allocation_events[1].pointer));
}

void test_ReportTraceBufferPoolHooksRecordSuccessFailureAndFree(void) {
    int buffer=0;
    pool_result=&buffer;
    TEST_ASSERT_EQUAL_PTR(&buffer,__wrap_mempool_malloc(0));
    pool_result=NULL;
    TEST_ASSERT_NULL(__wrap_mempool_malloc(0));
    __wrap_mempool_free(0,&buffer);
    TEST_ASSERT_EQUAL(3,atomic_load(&allocation_event_next));
    TEST_ASSERT_EQUAL('B',atomic_load(&allocation_events[0].kind));
    TEST_ASSERT_EQUAL('C',atomic_load(&allocation_events[1].kind));
    TEST_ASSERT_EQUAL('D',atomic_load(&allocation_events[2].kind));
    TEST_ASSERT_EQUAL(0,atomic_load(&buffer_pool_live));
    TEST_ASSERT_EQUAL(1,atomic_load(&buffer_pool_peak));
    __wrap_mempool_free(0,&buffer);
    TEST_ASSERT_EQUAL(1,atomic_load(&buffer_pool_unmatched_frees));
}

void test_BufferOriginsTrackContextAndUnmatchedFree(void) {
    int buffer=0, other=0;
    uintptr_t previous=0;
    AllocationContext *context=EnterAllocation((TaskHandle_t)1,0x1234,&previous);
    TEST_ASSERT_NOT_NULL(context);
    pool_result=&buffer;
    TEST_ASSERT_EQUAL_PTR(&buffer,__wrap_mempool_malloc(0));
    atomic_store(&context->origin,previous);
    atomic_store(&context->task,NULL);
    TEST_ASSERT_EQUAL(0,atomic_load(&buffer_trace_overflow));
    LogBufferOrigins();
    TEST_ASSERT_NOT_NULL(strstr(trace_log,"site=1234 count=1"));
    __wrap_mempool_free(0,&buffer);
    __wrap_mempool_free(0,&other);
    TEST_ASSERT_EQUAL(1,atomic_load(&buffer_trace_unmatched));
}

void test_BufferOriginsNestedAndForeignTasksRemainDistinct(void) {
    uintptr_t previous=0, nested=0;
    AllocationContext *outer=EnterAllocation((TaskHandle_t)1,0x1110,&previous);
    AllocationContext *inner=EnterAllocation((TaskHandle_t)1,0x2220,&nested);
    TEST_ASSERT_EQUAL_PTR(outer,inner);
    TEST_ASSERT_EQUAL_HEX(0x1110,nested);
    TEST_ASSERT_EQUAL_HEX(0x2220,AllocationOrigin());
    mocked_xTaskGetCurrentTaskHandle_fake.return_val=(TaskHandle_t)2;
    TEST_ASSERT_EQUAL_HEX(0,AllocationOrigin());
    mocked_xTaskGetCurrentTaskHandle_fake.return_val=(TaskHandle_t)1;
    atomic_store(&inner->origin,nested);
    TEST_ASSERT_EQUAL_HEX(0x1110,AllocationOrigin());
    atomic_store(&outer->origin,previous);
    atomic_store(&outer->task,NULL);
}

void test_BufferOriginTableReportsOverflowWithoutChangingPointer(void) {
    for (unsigned i=0;i<BUFFER_TRACE_SLOTS;++i) TrackBuffer(4u*(i+1u),0);
    TrackBuffer(4u*(BUFFER_TRACE_SLOTS+1u),0x1234);
    TEST_ASSERT_EQUAL(1,atomic_load(&buffer_trace_overflow));
    UntrackBuffer(4u*(BUFFER_TRACE_SLOTS+1u));
    TEST_ASSERT_EQUAL(1,atomic_load(&buffer_trace_unmatched));
}

void test_CloneProbeForwardsAndAttributesInternalBuffer(void) {
    int message=0, clone=0, buffer=0;
    clone_result=&clone; clone_allocates=true; pool_result=&buffer;
    TEST_ASSERT_EQUAL_PTR(&clone,__wrap_zmsg_clone(&message));
    TEST_ASSERT_EQUAL_PTR(&message,clone_argument);
    LogBufferOrigins();
    TEST_ASSERT_NOT_NULL(strstr(trace_log,"tracked=1 unknown=0"));
    __wrap_mempool_free(0,&buffer);
    TEST_ASSERT_EQUAL(0,atomic_load(&buffer_trace_unmatched));
}

void test_FooterProbePreservesArgumentsAndReportsFailures(void) {
    int message=0, data=0;
    footer_result=1;
    TEST_ASSERT_EQUAL(1,__wrap_zmsg_add_footer(&message,&data,4));
    TEST_ASSERT_EQUAL_PTR(&message,forwarded_request);
    TEST_ASSERT_EQUAL_PTR(&data,footer_data);
    TEST_ASSERT_EQUAL(4,footer_length);
    LogFooterFailures();
    TEST_ASSERT_NOT_NULL(strstr(trace_log,"samples=1 failed=1"));
}

void test_PoolDrainLogsOnlyAfterFailureAndAtBoundedIntervals(void) {
    mocked_esp_timer_get_time_fake.return_val=3000000;
    LogPoolDrain(0);
    TEST_ASSERT_EQUAL_STRING("",trace_log);
    atomic_store(&allocation_dumped,true);
    atomic_store(&buffer_pool_live,7);
    LogPoolDrain(1);
    TEST_ASSERT_NOT_NULL(strstr(trace_log,"t_ms=3000 result=1 live=7"));
    trace_log[0]=0;
    mocked_esp_timer_get_time_fake.return_val=4000000;
    LogPoolDrain(0);
    TEST_ASSERT_EQUAL_STRING("",trace_log);
    mocked_esp_timer_get_time_fake.return_val=5000000;
    LogPoolDrain(0);
    TEST_ASSERT_NOT_NULL(strstr(trace_log,"t_ms=5000 result=0 live=7"));
}

void test_ReportTraceMeasuresSameTaskClonesAndPoolSnapshots(void) {
    ezb_zcl_report_attr_cmd_t request=Request();
    clone_in_report=true;
    atomic_store(&buffer_pool_live,7);
    TEST_ASSERT_EQUAL(0,__wrap_ezb_zcl_report_attr_cmd_req(&request));
    TEST_ASSERT_NOT_NULL(strstr(trace_log,"clone_calls=1 clone_failures=1 pool_start=7 pool_end=7"));
}

void test_ZclInitPreservesCallerContextForNestedBufferAllocation(void) {
    int message=0, buffer=0;
    init_allocates=true; zmsg_allocates=true; zmsg_result=&message; pool_result=&buffer;
    TEST_ASSERT_EQUAL(0,__wrap_zcl_packet_init(&message,false));
    LogBufferOrigins();
    TEST_ASSERT_NOT_NULL(strstr(trace_log,"tracked=1 unknown=0"));
    for (unsigned i=0;i<ALLOC_CONTEXT_SLOTS;++i) TEST_ASSERT_NULL(atomic_load(&allocation_contexts[i].task));
    __wrap_mempool_free(0,&buffer);
}

void test_FirstSuccessfulReportCapturesInitialOriginsOnce(void) {
    int buffer=0;
    pool_result=&buffer;
    (void)__wrap_mempool_malloc(0);
    ezb_zcl_report_attr_cmd_t request=Request();
    TEST_ASSERT_EQUAL(0,__wrap_ezb_zcl_report_attr_cmd_req(&request));
    TEST_ASSERT_NOT_NULL(strstr(trace_log,"pool_start=1 pool_end=1"));
    TEST_ASSERT_NOT_NULL(strstr(trace_log,"tracked=1 unknown=1"));
    trace_log[0]=0;
    TEST_ASSERT_EQUAL(0,__wrap_ezb_zcl_report_attr_cmd_req(&request));
    TEST_ASSERT_NULL(strstr(trace_log,"tracked=1 unknown=1"));
    __wrap_mempool_free(0,&buffer);
}

void test_InnerReportProbePreservesResultAndNestedAllocationContext(void) {
    int buffer=0, message=0;
    ezb_zcl_report_attr_cmd_t request=Request();
    init_allocates=true; zmsg_allocates=true; zmsg_result=&message; pool_result=&buffer;
    inner_report_result=EZB_ERR_NO_MEM;
    TEST_ASSERT_EQUAL(EZB_ERR_NO_MEM,__wrap_zcl_report_attr_cmd_req(&request));
    LogBufferOrigins();
    TEST_ASSERT_NOT_NULL(strstr(trace_log,"tracked=1 unknown=0"));
    for (unsigned i=0;i<ALLOC_CONTEXT_SLOTS;++i) TEST_ASSERT_NULL(atomic_load(&allocation_contexts[i].task));
    __wrap_mempool_free(0,&buffer);
}

void test_BindProbePaginatesAndRejectsDuplicateCallback(void) {
    mocked_esp_timer_get_time_fake.return_val=40000000;
    PollBindProbe();
    TEST_ASSERT_EQUAL(1,bind_submissions);
    TEST_ASSERT_EQUAL_HEX16(0x1234,bind_request.dst_nwk_addr);
    ezb_zdo_nwk_mgmt_bind_req_t first=bind_request;
    ezb_zdp_nwk_mgmt_bind_table_entry_t entries[2]={{.src_ep=1,.cluster_id=6},{.src_ep=2,.cluster_id=8}};
    ezb_zdp_nwk_mgmt_bind_rsp_field_t response={.status=0,.binding_table_entries=3,.start_index=0,
        .binding_table_list_count=2,.binding_table_list=entries};
    ezb_zdo_nwk_mgmt_bind_req_result_t result={.error=EZB_ERR_NONE,.rsp=&response};
    first.cb(&result,first.user_ctx);
    PollBindProbe();
    TEST_ASSERT_EQUAL(2,bind_submissions);
    TEST_ASSERT_EQUAL(2,bind_request.field.start_index);
    first.cb(&result,first.user_ctx);
    TEST_ASSERT_EQUAL(1,atomic_load(&bind_probe_state));
    response.start_index=2; response.binding_table_list_count=1;
    bind_request.cb(&result,bind_request.user_ctx);
    PollBindProbe();
    TEST_ASSERT_EQUAL(3,atomic_load(&bind_probe_state));
    TEST_ASSERT_NOT_NULL(strstr(trace_log,"index=2 source_ep=1 cluster=0x0006"));
}

void test_BindProbeRejectsMalformedResponseWithoutRetry(void) {
    mocked_esp_timer_get_time_fake.return_val=40000000;
    PollBindProbe();
    ezb_zdp_nwk_mgmt_bind_rsp_field_t response={.status=0,.binding_table_entries=2,.start_index=0,
        .binding_table_list_count=1,.binding_table_list=NULL};
    ezb_zdo_nwk_mgmt_bind_req_result_t result={.error=EZB_ERR_NONE,.rsp=&response};
    bind_request.cb(&result,bind_request.user_ctx);
    PollBindProbe();
    TEST_ASSERT_EQUAL(3,atomic_load(&bind_probe_state));
    TEST_ASSERT_EQUAL(1,bind_submissions);
}

void test_BindProbeWaitsForCallbackAndDoesNotResend(void) {
    mocked_esp_timer_get_time_fake.return_val=39999000;
    PollBindProbe();
    TEST_ASSERT_EQUAL(0,bind_submissions);
    mocked_esp_timer_get_time_fake.return_val=40000000;
    PollBindProbe();
    PollBindProbe();
    TEST_ASSERT_EQUAL(1,bind_submissions);
    TEST_ASSERT_EQUAL(1,atomic_load(&bind_probe_state));
}

void test_BindProbeSubmissionFailureDoesNotRetry(void) {
    mocked_esp_timer_get_time_fake.return_val=40000000;
    bind_submit_result=EZB_ERR_INV_ARG;
    PollBindProbe();
    PollBindProbe();
    TEST_ASSERT_EQUAL(1,bind_submissions);
    TEST_ASSERT_EQUAL(3,atomic_load(&bind_probe_state));
    TEST_ASSERT_FALSE(atomic_load(&bind_probe_contexts[0].active));
}
