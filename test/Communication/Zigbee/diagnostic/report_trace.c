/* SDK 2.0.4 diagnostic only. Opaque internal pointers are forwarded unchanged;
 * no SDK layout, network storage or result is modified. Check the linked call
 * sites and live hook counts again when changing the installed SDK. */
#include "ezbee/zcl/zcl_general_cmd.h"
#include "ezbee/nwk.h"
#include "ezbee/zdo/zdo_nwk_mgmt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_log.h"
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifndef RACOOL_TEST_NATIVE
_Static_assert(ATOMIC_POINTER_LOCK_FREE == 2 && ATOMIC_INT_LOCK_FREE == 2,
               "Allocation tracing requires lock-free atomics");
#endif

typedef struct {
    unsigned init_calls, send_calls, aps_calls;
    unsigned clone_calls, clone_failures, pool_start, pool_end;
    int init_result, send_result, aps_result;
} ReportTrace;
static _Atomic(TaskHandle_t) report_trace_owner;
/* Only the matching task may dereference this stack-backed pointer. */
static ReportTrace *report_trace_current;
static ReportTrace *OwnedTrace(void);
static atomic_uint report_trace_sequence;
/* Recent call sites are diagnostic samples, not live allocation ownership. */
#define ZMSG_TRACE_SAMPLES 64u
static atomic_uint zmsg_trace_next;
static _Atomic(uintptr_t) zmsg_trace_samples[ZMSG_TRACE_SAMPLES];
static atomic_uint footer_trace_next;
static _Atomic(uintptr_t) footer_trace_samples[ZMSG_TRACE_SAMPLES];
#define ALLOCATION_EVENT_CAPACITY 256u
typedef struct {
    atomic_uint stamp,kind;
    _Atomic(uintptr_t) pointer,origin;
} AllocationEvent;
static AllocationEvent allocation_events[ALLOCATION_EVENT_CAPACITY];
static atomic_uint allocation_event_next;
static atomic_bool allocation_dumped;
static atomic_uint pool_drain_last_ms;
/* Counts begin at MCU boot; SDK bulk reinitialization is not a tracked free. */
static atomic_uint buffer_pool_live, buffer_pool_peak, buffer_pool_unmatched_frees;
#define BUFFER_TRACE_SLOTS 256u
#define ALLOC_CONTEXT_SLOTS 4u
typedef struct { _Atomic(uintptr_t) pointer, origin; } BufferTraceEntry;
typedef struct { _Atomic(TaskHandle_t) task; _Atomic(uintptr_t) origin; } AllocationContext;
static BufferTraceEntry buffer_trace[BUFFER_TRACE_SLOTS];
static AllocationContext allocation_contexts[ALLOC_CONTEXT_SLOTS];
static atomic_uint buffer_trace_overflow, buffer_trace_unmatched;

/* A temporary, read-only ZDO query to the local network address. Results are
 * copied in the callback and printed later by the report worker. The SDK
 * documents requests to remote devices; self-routing may be unsupported. */
#define BIND_PROBE_CAPACITY 32u
typedef struct {
    ezb_err_t error;
    uint8_t status, total, start, count;
    ezb_zdp_nwk_mgmt_bind_table_entry_t entries[BIND_PROBE_CAPACITY];
} BindProbePage;
static BindProbePage bind_probe_page;
static atomic_uint bind_probe_state; /* 0 ready to request, 1 pending, 2 response, 3 done */
static uint8_t bind_probe_start;
typedef struct { atomic_bool active; uint8_t start; } BindProbeContext;
static BindProbeContext bind_probe_contexts[BIND_PROBE_CAPACITY];

static void BindProbeCallback(const ezb_zdo_nwk_mgmt_bind_req_result_t *result, void *context) {
    BindProbeContext *reservation=context;
    if (!reservation || !atomic_exchange(&reservation->active,false) ||
        reservation->start!=bind_probe_start || atomic_load_explicit(&bind_probe_state,memory_order_acquire)!=1u) return;
    BindProbePage *page=&bind_probe_page;
    memset(page,0,sizeof(*page));
    page->error=result ? result->error : EZB_ERR_INV_ARG;
    page->status=0xffu;
    if (result && result->error==EZB_ERR_NONE && result->rsp) {
        const ezb_zdp_nwk_mgmt_bind_rsp_field_t *rsp=result->rsp;
        page->status=rsp->status; page->total=rsp->binding_table_entries;
        page->start=rsp->start_index; page->count=rsp->binding_table_list_count;
        if (page->status==0 && page->start==bind_probe_start && page->total<=BIND_PROBE_CAPACITY &&
            page->start<=page->total && page->count<=page->total-page->start &&
            (!page->count || rsp->binding_table_list)) {
            if (page->count) memcpy(page->entries,rsp->binding_table_list,page->count*sizeof(page->entries[0]));
        } else if (page->status==0) page->error=EZB_ERR_INV_ARG;
    }
    atomic_store_explicit(&bind_probe_state,2u,memory_order_release);
}

/* Runs from the existing report worker with SDK ownership, after startup. */
static void PollBindProbe(void) {
    if (esp_timer_get_time()<40000000LL) return;
    unsigned state=atomic_load_explicit(&bind_probe_state,memory_order_acquire);
    if (state==2u) {
        BindProbePage *page=&bind_probe_page;
        ESP_LOGI("RPTBIND", "error=%d status=%u total=%u start=%u count=%u",
            (int)page->error,(unsigned)page->status,(unsigned)page->total,(unsigned)page->start,(unsigned)page->count);
        if (page->error!=EZB_ERR_NONE || page->status!=0 || (!page->count && page->start<page->total)) {
            atomic_store(&bind_probe_state,3u); return;
        }
        for (unsigned i=0;i<page->count;++i) {
            const ezb_zdp_nwk_mgmt_bind_table_entry_t *entry=&page->entries[i];
            ESP_LOGI("RPTBIND", "index=%u source_ep=%u cluster=0x%04x destination_mode=%u destination_ep=%u",
                (unsigned)page->start+i,(unsigned)entry->src_ep,(unsigned)entry->cluster_id,
                (unsigned)entry->dst_addr_mode,(unsigned)entry->dst_ep);
        }
        unsigned next=(unsigned)page->start+page->count;
        if (next>=page->total) { atomic_store(&bind_probe_state,3u); return; }
        bind_probe_start=(uint8_t)next;
        atomic_store(&bind_probe_state,0u);
        state=0u;
    }
    if (state==0u) {
        unsigned expected=0;
        if (!atomic_compare_exchange_strong(&bind_probe_state,&expected,1u)) return;
        BindProbeContext *reservation=&bind_probe_contexts[bind_probe_start];
        reservation->start=bind_probe_start;
        atomic_store(&reservation->active,true);
        ezb_zdo_nwk_mgmt_bind_req_t request={.dst_nwk_addr=ezb_nwk_get_short_address(),
            .field={.start_index=bind_probe_start},.cb=BindProbeCallback,.user_ctx=reservation};
        ezb_err_t result=ezb_zdo_nwk_mgmt_bind_req(&request);
        if (result!=EZB_ERR_NONE) {
            atomic_store(&reservation->active,false);
            expected=1u;
            if (atomic_compare_exchange_strong(&bind_probe_state,&expected,3u))
                ESP_LOGI("RPTBIND", "request_error=%d start=%u",(int)result,(unsigned)bind_probe_start);
        }
    }
}

static TaskHandle_t TraceTask(void) {
#ifndef RACOOL_TEST_NATIVE
    if (xPortInIsrContext()) return NULL;
#endif
    return xTaskGetCurrentTaskHandle();
}

/* Context is task-scoped and never changes SDK memory. Unwrapped calls are unknown. */
static AllocationContext *EnterAllocation(TaskHandle_t task, uintptr_t origin, uintptr_t *previous) {
    if (!task) return NULL;
    for (unsigned i=0;i<ALLOC_CONTEXT_SLOTS;++i) {
        if (atomic_load(&allocation_contexts[i].task)==task) {
            *previous=atomic_exchange(&allocation_contexts[i].origin,origin);
            return &allocation_contexts[i];
        }
    }
    for (unsigned i=0;i<ALLOC_CONTEXT_SLOTS;++i) {
        TaskHandle_t vacant=NULL;
        if (atomic_compare_exchange_strong(&allocation_contexts[i].task,&vacant,task)) {
            *previous=0;
            atomic_store(&allocation_contexts[i].origin,origin);
            return &allocation_contexts[i];
        }
    }
    return NULL;
}

static uintptr_t AllocationOrigin(void) {
    TaskHandle_t task=TraceTask();
    if (task) for (unsigned i=0;i<ALLOC_CONTEXT_SLOTS;++i)
        if (atomic_load(&allocation_contexts[i].task)==task)
            return atomic_load(&allocation_contexts[i].origin);
    return 0;
}

static void TrackBuffer(uintptr_t pointer, uintptr_t origin) {
    unsigned start=(unsigned)((pointer >> 2u) % BUFFER_TRACE_SLOTS);
    for (unsigned i=0;i<BUFFER_TRACE_SLOTS;++i) {
        BufferTraceEntry *entry=&buffer_trace[(start+i)%BUFFER_TRACE_SLOTS];
        uintptr_t vacant=0;
        /* 1 reserves a slot until its origin is published. */
        if (atomic_compare_exchange_strong(&entry->pointer,&vacant,1u)) {
            atomic_store(&entry->origin,origin);
            atomic_store_explicit(&entry->pointer,pointer,memory_order_release);
            return;
        }
    }
    atomic_fetch_add(&buffer_trace_overflow,1u);
}

static void UntrackBuffer(uintptr_t pointer) {
    unsigned start=(unsigned)((pointer >> 2u) % BUFFER_TRACE_SLOTS);
    for (unsigned i=0;i<BUFFER_TRACE_SLOTS;++i) {
        BufferTraceEntry *entry=&buffer_trace[(start+i)%BUFFER_TRACE_SLOTS];
        uintptr_t expected=pointer;
        if (atomic_compare_exchange_strong(&entry->pointer,&expected,0)) return;
    }
    atomic_fetch_add(&buffer_trace_unmatched,1u);
}

static void LogBufferOrigins(void) {
    uintptr_t origins[8]={0}; unsigned counts[8]={0}, unknown=0, other=0, tracked=0;
    for (unsigned i=0;i<BUFFER_TRACE_SLOTS;++i) {
        uintptr_t pointer=atomic_load_explicit(&buffer_trace[i].pointer,memory_order_acquire);
        uintptr_t origin=atomic_load(&buffer_trace[i].origin);
        if (pointer<=1u || pointer!=atomic_load(&buffer_trace[i].pointer)) continue;
        ++tracked;
        if (!origin) { ++unknown; continue; }
        unsigned j=0;
        while (j<8u && origins[j] && origins[j]!=origin) ++j;
        if (j==8u) { ++other; continue; }
        origins[j]=origin; ++counts[j];
    }
    ESP_LOGI("RPTORIGIN", "tracked=%u unknown=%u other=%u overflow=%u unmatched=%u",
        tracked,unknown,other,atomic_load(&buffer_trace_overflow),atomic_load(&buffer_trace_unmatched));
    for (unsigned i=0;i<8u && origins[i];++i)
        ESP_LOGI("RPTORIGIN", "site=%lx count=%u",(unsigned long)origins[i],counts[i]);
}

static void BufferAllocated(void) {
    unsigned live=atomic_fetch_add(&buffer_pool_live,1u)+1u;
    unsigned peak=atomic_load(&buffer_pool_peak);
    while (peak<live && !atomic_compare_exchange_weak(&buffer_pool_peak,&peak,live)) {}
}

static void BufferReleased(void) {
    unsigned live=atomic_load(&buffer_pool_live);
    do {
        if (!live) { atomic_fetch_add(&buffer_pool_unmatched_frees,1u); return; }
    } while (!atomic_compare_exchange_weak(&buffer_pool_live,&live,live-1u));
}

ezb_err_t __real_ezb_zcl_report_attr_cmd_req(const ezb_zcl_report_attr_cmd_t *request);
ezb_err_t __real_zcl_report_attr_cmd_req(const ezb_zcl_report_attr_cmd_t *request);
uint8_t __real_zcl_packet_init(void *packet, bool manufacturer_specific);
uint8_t __real_zcl_packet_send(void *packet, const void *confirmation);
ezb_err_t __real_aps_apsde_data_request(const void *request);
void *__real_zmsg_alloc(unsigned length);
void *__real_zmsg_clone(const void *message);
uint8_t __real_zmsg_add_footer(void *message, const void *data, uint16_t length);
void *__real_mempool_malloc(unsigned pool);
void __real_mempool_free(unsigned pool, void *pointer);

static void RecordAllocationEvent(unsigned kind, uintptr_t pointer, uintptr_t origin) {
    unsigned sequence=atomic_fetch_add_explicit(&allocation_event_next,1,memory_order_relaxed);
    AllocationEvent *event=&allocation_events[sequence % ALLOCATION_EVENT_CAPACITY];
    atomic_store_explicit(&event->stamp,0,memory_order_release);
    atomic_store_explicit(&event->kind,kind,memory_order_relaxed);
    atomic_store_explicit(&event->pointer,pointer,memory_order_relaxed);
    atomic_store_explicit(&event->origin,origin,memory_order_relaxed);
    atomic_store_explicit(&event->stamp,sequence+1u,memory_order_release);
}

void *__wrap_mempool_malloc(unsigned pool) {
    void *result=__real_mempool_malloc(pool);
    if (pool==1u) RecordAllocationEvent(result ? 'M' : 'X',(uintptr_t)result,0);
    else if (pool==0u) {
        if (result) { BufferAllocated(); TrackBuffer((uintptr_t)result,AllocationOrigin()); }
        RecordAllocationEvent(result ? 'B' : 'C',(uintptr_t)result,0);
    }
    return result;
}

void __wrap_mempool_free(unsigned pool, void *pointer) {
    if (pool==1u && pointer) RecordAllocationEvent('F',(uintptr_t)pointer,0);
    else if (pool==0u && pointer) {
        UntrackBuffer((uintptr_t)pointer);
        BufferReleased();
        RecordAllocationEvent('D',(uintptr_t)pointer,0);
    }
    __real_mempool_free(pool,pointer);
}

__attribute__((noinline)) void *__wrap_zmsg_alloc(unsigned length) {
    uintptr_t caller=(uintptr_t)__builtin_return_address(0);
    uintptr_t previous=0;
    uintptr_t parent=AllocationOrigin();
    AllocationContext *context=EnterAllocation(TraceTask(),parent ? parent : caller,&previous);
    void *result=__real_zmsg_alloc(length);
    if (context) {
        atomic_store(&context->origin,previous);
        if (!previous) atomic_store(&context->task,NULL);
    }
    unsigned position=atomic_fetch_add_explicit(&zmsg_trace_next,1,memory_order_relaxed);
    /* Code addresses are aligned; the low bit records an allocation failure. */
    atomic_store_explicit(&zmsg_trace_samples[position % ZMSG_TRACE_SAMPLES],
                          (caller & ~(uintptr_t)1) | (result ? 0u : 1u),memory_order_relaxed);
    RecordAllocationEvent(result ? 'A' : 'Z',(uintptr_t)result,caller);
    return result;
}

__attribute__((noinline)) void *__wrap_zmsg_clone(const void *message) {
    uintptr_t previous=0;
    AllocationContext *context=EnterAllocation(TraceTask(),(uintptr_t)__builtin_return_address(0),&previous);
    void *result=__real_zmsg_clone(message);
    ReportTrace *trace=OwnedTrace();
    if (trace) { ++trace->clone_calls; if (!result) ++trace->clone_failures; }
    if (context) {
        atomic_store(&context->origin,previous);
        if (!previous) atomic_store(&context->task,NULL);
    }
    return result;
}

__attribute__((noinline)) uint8_t __wrap_zmsg_add_footer(void *message, const void *data, uint16_t length) {
    uintptr_t caller=(uintptr_t)__builtin_return_address(0);
    uint8_t result=__real_zmsg_add_footer(message,data,length);
    unsigned position=atomic_fetch_add_explicit(&footer_trace_next,1,memory_order_relaxed);
    atomic_store_explicit(&footer_trace_samples[position % ZMSG_TRACE_SAMPLES],
                          (caller & ~(uintptr_t)1) | (result ? 1u : 0u),memory_order_relaxed);
    return result;
}

static void LogFooterFailures(void) {
    uintptr_t sites[4]={0}; unsigned counts[4]={0}, failures=0, other=0;
    unsigned total=atomic_load_explicit(&footer_trace_next,memory_order_relaxed);
    unsigned count=total<ZMSG_TRACE_SAMPLES ? total : ZMSG_TRACE_SAMPLES;
    for (unsigned i=0;i<count;++i) {
        uintptr_t item=atomic_load_explicit(&footer_trace_samples[(total-1u-i)%ZMSG_TRACE_SAMPLES],memory_order_relaxed);
        if (!(item & 1u)) continue;
        uintptr_t site=item & ~(uintptr_t)1;
        ++failures;
        unsigned j=0;
        while (j<4 && sites[j] && sites[j]!=site) ++j;
        if (j==4) { ++other; continue; }
        sites[j]=site; ++counts[j];
    }
    ESP_LOGI("RPTFOOT", "samples=%u failed=%u other=%u sites=%lx:%u,%lx:%u,%lx:%u,%lx:%u",
        count,failures,other,(unsigned long)sites[0],counts[0],(unsigned long)sites[1],counts[1],
        (unsigned long)sites[2],counts[2],(unsigned long)sites[3],counts[3]);
}

/* One bounded, best-effort snapshot after a rejected report. A concurrent
 * writer can replace an entry; sequence validation makes that gap explicit. */
static void LogAllocationEvents(void) {
    bool expected=false;
    if (!atomic_compare_exchange_strong(&allocation_dumped,&expected,true)) return;
    ESP_LOGI("RPTPOOL", "pool=0 live=%u peak=%u unmatched_frees=%u",
        atomic_load(&buffer_pool_live),atomic_load(&buffer_pool_peak),atomic_load(&buffer_pool_unmatched_frees));
    LogBufferOrigins();
    unsigned end=atomic_load_explicit(&allocation_event_next,memory_order_acquire);
    unsigned first=end>ALLOCATION_EVENT_CAPACITY ? end-ALLOCATION_EVENT_CAPACITY : 0;
    ESP_LOGI("RPTEVENT", "window=%u:%u",first,end);
    for (unsigned base=first;base<end;base+=16u) {
        char line[768]; size_t used=0;
        unsigned limit=end-base<16u ? end-base : 16u;
        for (unsigned i=0;i<limit;++i) {
            unsigned sequence=base+i;
            AllocationEvent *event=&allocation_events[sequence % ALLOCATION_EVENT_CAPACITY];
            unsigned stamp=atomic_load_explicit(&event->stamp,memory_order_acquire);
            unsigned kind=atomic_load_explicit(&event->kind,memory_order_relaxed);
            uintptr_t pointer=atomic_load_explicit(&event->pointer,memory_order_relaxed);
            uintptr_t origin=atomic_load_explicit(&event->origin,memory_order_relaxed);
            if (stamp!=sequence+1u || atomic_load_explicit(&event->stamp,memory_order_acquire)!=stamp) {
                kind='G'; pointer=origin=0;
            }
            int written=snprintf(line+used,sizeof(line)-used,"%u:%c:%lx:%lx,",sequence,(char)kind,
                                 (unsigned long)pointer,(unsigned long)origin);
            if (written<0 || (size_t)written>=sizeof(line)-used) break;
            used+=(size_t)written;
        }
        line[used]='\0';
        ESP_LOGI("RPTEVENT", "data=%s",line);
    }
}

/* This diagnostic snapshot is sampled only from the report worker after an
 * observed failure; it is not a timer, an ownership proof or a traffic count. */
static void LogPoolDrain(ezb_err_t result) {
    if (!atomic_load(&allocation_dumped)) return;
    uint32_t now=(uint32_t)((uint64_t)esp_timer_get_time()/1000u);
    unsigned previous=atomic_load(&pool_drain_last_ms);
    if ((uint32_t)(now-previous)<2000u ||
        !atomic_compare_exchange_strong(&pool_drain_last_ms,&previous,now)) return;
    ESP_LOGI("RPTDRAIN", "t_ms=%u result=%d live=%u peak=%u unmatched_frees=%u",
        (unsigned)now,(int)result,atomic_load(&buffer_pool_live),
        atomic_load(&buffer_pool_peak),atomic_load(&buffer_pool_unmatched_frees));
}

static void LogRecentAllocations(void) {
    uintptr_t sites[4]={0}; unsigned counts[4]={0}, failures=0, other=0;
    unsigned total=atomic_load_explicit(&zmsg_trace_next,memory_order_relaxed);
    unsigned count=total<ZMSG_TRACE_SAMPLES ? total : ZMSG_TRACE_SAMPLES;
    for (unsigned i=0;i<count;++i) {
        uintptr_t item=atomic_load_explicit(&zmsg_trace_samples[(total-1u-i)%ZMSG_TRACE_SAMPLES],memory_order_relaxed);
        uintptr_t site=item & ~(uintptr_t)1;
        if (!site) continue;
        failures+=(unsigned)(item & 1u);
        unsigned j=0;
        while (j<4 && sites[j] && sites[j]!=site) ++j;
        if (j==4) { ++other; continue; }
        sites[j]=site; ++counts[j];
    }
    ESP_LOGI("RPTALLOC", "samples=%u failed=%u other=%u sites=%lx:%u,%lx:%u,%lx:%u,%lx:%u",
        count,failures,other,(unsigned long)sites[0],counts[0],(unsigned long)sites[1],counts[1],
        (unsigned long)sites[2],counts[2],(unsigned long)sites[3],counts[3]);
}

static ReportTrace *OwnedTrace(void) {
    TaskHandle_t caller = xTaskGetCurrentTaskHandle();
    return caller && atomic_load(&report_trace_owner) == caller ? report_trace_current : NULL;
}

__attribute__((noinline)) uint8_t __wrap_zcl_packet_init(void *packet, bool manufacturer_specific) {
    uintptr_t previous=0;
    uintptr_t parent=AllocationOrigin();
    AllocationContext *context=EnterAllocation(TraceTask(),parent ? parent : (uintptr_t)__builtin_return_address(0),&previous);
    uint8_t result = __real_zcl_packet_init(packet, manufacturer_specific);
    if (context) {
        atomic_store(&context->origin,previous);
        if (!previous) atomic_store(&context->task,NULL);
    }
    ReportTrace *trace = OwnedTrace();
    if (trace) { ++trace->init_calls; if (!trace->init_result) trace->init_result = result; }
    return result;
}

__attribute__((noinline)) ezb_err_t __wrap_zcl_report_attr_cmd_req(const ezb_zcl_report_attr_cmd_t *request) {
    uintptr_t previous=0;
    AllocationContext *context=EnterAllocation(TraceTask(),(uintptr_t)__builtin_return_address(0),&previous);
    ezb_err_t result=__real_zcl_report_attr_cmd_req(request);
    if (context) {
        atomic_store(&context->origin,previous);
        if (!previous) atomic_store(&context->task,NULL);
    }
    return result;
}
uint8_t __wrap_zcl_packet_send(void *packet, const void *confirmation) {
    uint8_t result = __real_zcl_packet_send(packet, confirmation);
    ReportTrace *trace = OwnedTrace();
    if (trace) { ++trace->send_calls; if (!trace->send_result) trace->send_result = result; }
    return result;
}
ezb_err_t __wrap_aps_apsde_data_request(const void *request) {
    ezb_err_t result = __real_aps_apsde_data_request(request);
    ReportTrace *trace = OwnedTrace();
    if (trace) { ++trace->aps_calls; if (!trace->aps_result) trace->aps_result = result; }
    return result;
}

ezb_err_t __wrap_ezb_zcl_report_attr_cmd_req(const ezb_zcl_report_attr_cmd_t *request) {
    TaskHandle_t caller = xTaskGetCurrentTaskHandle(), vacant = NULL;
    bool owned = caller && atomic_compare_exchange_strong(&report_trace_owner, &vacant, caller);
    ReportTrace trace = {0};
    int64_t start = 0;
    if (owned) { report_trace_current = &trace; start = esp_timer_get_time(); trace.pool_start=atomic_load(&buffer_pool_live); }
    ezb_err_t result = __real_ezb_zcl_report_attr_cmd_req(request);
    if (owned) {
        trace.pool_end=atomic_load(&buffer_pool_live);
        report_trace_current = NULL;
        atomic_store(&report_trace_owner, NULL);
        unsigned sequence = atomic_fetch_add(&report_trace_sequence, 1);
        if (result != EZB_ERR_NONE || sequence < 12 ||
            (request && request->cmd_ctrl.src_ep == 1 && request->payload.attr_id == 0x805e)) {
            ESP_LOGI("RPTTRACE", "seq=%u t=%lld ep=%u cluster=0x%04x attr=0x%04x result=%d init_calls=%u init_result=%d send_calls=%u send_result=%d aps_calls=%u aps_result=%d elapsed=%lld task=%s priority=%u clone_calls=%u clone_failures=%u pool_start=%u pool_end=%u",
                sequence, (long long)start, request ? request->cmd_ctrl.src_ep : 0,
                request ? request->cmd_ctrl.cluster_id : 0, request ? request->payload.attr_id : 0,
                result, trace.init_calls, trace.init_result, trace.send_calls, trace.send_result,
                trace.aps_calls, trace.aps_result, (long long)(esp_timer_get_time() - start),
                pcTaskGetName(NULL), (unsigned)uxTaskPriorityGet(NULL),trace.clone_calls,trace.clone_failures,
                trace.pool_start,trace.pool_end);
            if (sequence==0u && result==EZB_ERR_NONE) {
                ESP_LOGI("RPTSTART", "pool_start=%u pool_end=%u",trace.pool_start,trace.pool_end);
                LogBufferOrigins();
            }
            if (result != EZB_ERR_NONE) { LogRecentAllocations(); LogFooterFailures(); LogAllocationEvents(); }
        }
        LogPoolDrain(result);
        PollBindProbe();
    }
    return result;
}
