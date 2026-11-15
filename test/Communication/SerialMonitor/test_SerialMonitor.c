#include "Communication/SerialMonitor/ISerialMonitor.h"
#include "Communication/SerialMonitor/SerialMonitorConfig.h"
#include "Communication/SerialMonitor/ISerialPort.h"
#include "Communication/SerialMonitor/ISerialBuffer.h"
#include "Communication/SerialMonitor/ISerialMonitorPlugin.h"
#include "Communication/DebugConsole/IDebugConsole.h"
#include "../../Infrastructure/support/test_mocks.h"
#include "unity.h"
#include <setjmp.h>

// Interface Mocks (Internal to this test file)
DECLARE_FAKE_VALUE_FUNC5(int32_t, port_init, ISerialPort *, uint8_t, uint32_t, uint16_t, uint16_t);
DECLARE_FAKE_VALUE_FUNC1(int32_t, port_deinit, ISerialPort *);
DECLARE_FAKE_VALUE_FUNC3(int32_t, port_write, ISerialPort *, const void *, size_t);
DECLARE_FAKE_VALUE_FUNC4(int32_t, port_read, ISerialPort *, void *, size_t, uint32_t);
DECLARE_FAKE_VALUE_FUNC2(int32_t, port_wait, ISerialPort *, uint32_t);
DECLARE_FAKE_VALUE_FUNC1(void *, port_queue, ISerialPort *);

DECLARE_FAKE_VALUE_FUNC3(int32_t, buf_init, ISerialBuffer *, size_t, size_t);
DECLARE_FAKE_VALUE_FUNC1(int32_t, buf_deinit, ISerialBuffer *);
DECLARE_FAKE_VALUE_FUNC4(int32_t, buf_push, ISerialBuffer *, const void *, size_t, uint32_t);
DECLARE_FAKE_VALUE_FUNC3(void *, buf_pop, ISerialBuffer *, size_t *, uint32_t);
DECLARE_FAKE_VOID_FUNC2(buf_return, ISerialBuffer *, void *);

DECLARE_FAKE_VALUE_FUNC1(int32_t, plugin_init, ISerialMonitorPlugin *);
DECLARE_FAKE_VOID_FUNC1(plugin_deinit, ISerialMonitorPlugin *);
DECLARE_FAKE_VOID_FUNC3(plugin_rx, ISerialMonitorPlugin *, const void *, size_t);
DECLARE_FAKE_VOID_FUNC3(plugin_tx, ISerialMonitorPlugin *, const void *, size_t);

DEFINE_FAKE_VALUE_FUNC5(int32_t, port_init, ISerialPort *, uint8_t, uint32_t, uint16_t, uint16_t);
DEFINE_FAKE_VALUE_FUNC1(int32_t, port_deinit, ISerialPort *);
DEFINE_FAKE_VALUE_FUNC3(int32_t, port_write, ISerialPort *, const void *, size_t);
DEFINE_FAKE_VALUE_FUNC4(int32_t, port_read, ISerialPort *, void *, size_t, uint32_t);
DEFINE_FAKE_VALUE_FUNC2(int32_t, port_wait, ISerialPort *, uint32_t);
DEFINE_FAKE_VALUE_FUNC1(void *, port_queue, ISerialPort *);

DEFINE_FAKE_VALUE_FUNC3(int32_t, buf_init, ISerialBuffer *, size_t, size_t);
DEFINE_FAKE_VALUE_FUNC1(int32_t, buf_deinit, ISerialBuffer *);
DEFINE_FAKE_VALUE_FUNC4(int32_t, buf_push, ISerialBuffer *, const void *, size_t, uint32_t);
DEFINE_FAKE_VALUE_FUNC3(void *, buf_pop, ISerialBuffer *, size_t *, uint32_t);
DEFINE_FAKE_VOID_FUNC2(buf_return, ISerialBuffer *, void *);

DEFINE_FAKE_VALUE_FUNC1(int32_t, plugin_init, ISerialMonitorPlugin *);
DEFINE_FAKE_VOID_FUNC1(plugin_deinit, ISerialMonitorPlugin *);
DEFINE_FAKE_VOID_FUNC3(plugin_rx, ISerialMonitorPlugin *, const void *, size_t);
DEFINE_FAKE_VOID_FUNC3(plugin_tx, ISerialMonitorPlugin *, const void *, size_t);

static ISerialPort mockPort = {
    .Initialize = port_init, .Deinitialize = port_deinit, .WriteBytes = port_write,
    .ReadBytes = port_read, .WaitTxDone = port_wait, .GetEventQueue = port_queue
};

static ISerialBuffer mockBuffer = {
    .Initialize = buf_init, .Deinitialize = buf_deinit, .PushData = buf_push,
    .PopData = buf_pop, .ReturnItem = buf_return
};

// Override instance getters for testing
// Note: These must be declared as non-static to override if they are weak, or 
// if they are just defined here and we include the .c file which uses them.
ISerialPort* SerialPort_GetESP32Instance(void) { return &mockPort; }
ISerialBuffer* SerialBuffer_GetInstance(void) { return &mockBuffer; }

#include "Communication/SerialMonitor/SerialMonitor.c"

// Internal state access (assuming SerialMonitor.c is included before/after in same TU)
extern SerialMonitorImpl serialMonitor;

static unsigned created_tasks;
static BaseType_t create_worker(TaskFunction_t fn, const char *name, uint32_t stack, void *arg, UBaseType_t priority, TaskHandle_t *out) {
  (void)fn; (void)name; (void)stack; (void)arg; (void)priority;
  *out = (TaskHandle_t)(uintptr_t)(0x200 + ++created_tasks); return pdPASS;
}
static BaseType_t stop_ack(QueueHandle_t queue, void *data, TickType_t timeout) {
  (void)queue; (void)timeout; *(int32_t *)data = 0; return pdTRUE;
}
void setUp_SM(void) {
  atomic_store(&serialMonitor.queued_bytes,0); atomic_store(&serialMonitor.tx_bytes,0);
  atomic_store(&serialMonitor.rx_bytes,0); atomic_store(&serialMonitor.rejected_packets,0);
  atomic_store(&serialMonitor.tx_dropped_bytes,0); atomic_store(&serialMonitor.tx_failures,0);
  atomic_store(&serialMonitor.rx_failures,0);
  RESET_FAKE(mocked_xSemaphoreCreateMutex);
  RESET_FAKE(mocked_xSemaphoreTake);
  RESET_FAKE(mocked_xSemaphoreGive);
  RESET_FAKE(mocked_xTaskCreate);
  mocked_xSemaphoreCreateMutex_fake.return_val = (SemaphoreHandle_t)0x100;
  mocked_xSemaphoreTake_fake.return_val = pdTRUE;
  serialMonitor.mutex = NULL;
  serialMonitor.tx_progress = (QueueHandle_t)0x112;
  atomic_store(&serialMonitor.pending_tx, 0);
  atomic_store(&serialMonitor.tx_failed, false);
  atomic_store(&serialMonitor.plugin_owner, NULL);
  serialMonitor.control = NULL;
  serialMonitor.stopping = serialMonitor.uart_owned = serialMonitor.service_stop_pending = false;
  memset(&serialMonitor.tx_worker, 0, sizeof(serialMonitor.tx_worker));
  memset(&serialMonitor.rx_worker, 0, sizeof(serialMonitor.rx_worker));
  RESET_FAKE(mocked_xQueueCreate); RESET_FAKE(mocked_xQueueReceive);
  RESET_FAKE(mocked_xTaskGetCurrentTaskHandle); RESET_FAKE(mocked_vTaskDelete);
  mocked_xQueueCreate_fake.return_val = (QueueHandle_t)0x111;
  mocked_xQueueReceive_fake.custom_fake = stop_ack;
  mocked_xTaskCreate_fake.custom_fake = create_worker; created_tasks = 0;
  // Reset local dependencies for each independent test:
  RESET_FAKE(port_init); RESET_FAKE(port_deinit); RESET_FAKE(port_write);
  RESET_FAKE(port_read); RESET_FAKE(port_wait); RESET_FAKE(port_queue);
  RESET_FAKE(buf_init); RESET_FAKE(buf_deinit); RESET_FAKE(buf_push);
  RESET_FAKE(buf_pop); RESET_FAKE(buf_return);
  RESET_FAKE(plugin_init); RESET_FAKE(plugin_deinit);
  RESET_FAKE(plugin_rx); RESET_FAKE(plugin_tx);
  
  // Re-init serialMonitor state
  serialMonitor.state = SERVICE_STATE_UNINITIALIZED;
  serialMonitor.plugin_count = 0;
  serialMonitor.active = false;
  // In monolithic TU, we use the real instances but they call mocked SDK functions
  serialMonitor.port = SerialPort_GetESP32Instance();
  serialMonitor.buffer = SerialBuffer_GetInstance();
}

void tearDown_SM(void) {}

/**
 * @brief Test Lifecycle
 */
void test_SerialMonitor_Lifecycle(void) {
  setUp_SM();
  ISerialMonitor *mon = SerialMonitor_GetModule();
  IService *service = &(mon->base);

  TEST_ASSERT_EQUAL(0, service->Initialize(service, NULL));
  TEST_ASSERT_EQUAL(SerialPort_GetESP32Instance(), serialMonitor.port);
  TEST_ASSERT_EQUAL(SerialBuffer_GetInstance(), serialMonitor.buffer);

  buf_init_fake.return_val = 0;
  port_init_fake.return_val = 0;
  mocked_xTaskCreate_fake.return_val = pdPASS;

  TEST_ASSERT_EQUAL(0, service->Start(service));
  TEST_ASSERT_EQUAL(1, buf_init_fake.call_count);
  TEST_ASSERT_EQUAL(0, port_init_fake.call_count);
  TEST_ASSERT_EQUAL(0, mon->StartUART(mon));
  TEST_ASSERT_EQUAL(1, port_init_fake.call_count);
  TEST_ASSERT_EQUAL(2, mocked_xTaskCreate_fake.call_count); // TX and RX tasks

  TEST_ASSERT_EQUAL(0, service->Stop(service));
  TEST_ASSERT_EQUAL(1, buf_deinit_fake.call_count);
  TEST_ASSERT_EQUAL(1, port_deinit_fake.call_count);
}

/**
 * @brief Test Plugin Architecture
 */
void test_SerialMonitor_Plugins(void) {
  setUp_SM();
  ISerialMonitor *mon = SerialMonitor_GetModule();
  IService *service = &(mon->base);
  service->Initialize(service, NULL);

  ISerialMonitorPlugin testPlugin = {
      .name = "TestPlugin",
      .Init = plugin_init,
      .Deinit = plugin_deinit,
      .OnDataReceived = plugin_rx,
      .OnDataTransmitted = plugin_tx
  };

  plugin_init_fake.return_val = 0;
  TEST_ASSERT_EQUAL(0, mon->AddPlugin(mon, &testPlugin));
  TEST_ASSERT_EQUAL(1, serialMonitor.plugin_count);
  TEST_ASSERT_EQUAL(1, plugin_init_fake.call_count);

  TEST_ASSERT_EQUAL(0, mon->RemovePlugin(mon, &testPlugin));
  TEST_ASSERT_EQUAL(0, serialMonitor.plugin_count);
  TEST_ASSERT_EQUAL(1, plugin_deinit_fake.call_count);
}

/**
 * @brief Test Writing Through Interface
 */
void test_SerialMonitor_Writing(void) {
  setUp_SM();
  ISerialMonitor *mon = SerialMonitor_GetModule();
  IService *service = &(mon->base);
  service->Initialize(service, NULL);
  serialMonitor.state = SERVICE_STATE_STARTED;

  buf_push_fake.return_val = 0;
  TEST_ASSERT_EQUAL(0, mon->WriteLog(mon, "Hello"));
  TEST_ASSERT_EQUAL(1, buf_push_fake.call_count);
  TEST_ASSERT_EQUAL_STRING("Hello", (const char*)buf_push_fake.arg1_val);
}

#include "Communication/SerialMonitor/SerialMonitorConsoleCommands.h"
extern int32_t SerialMonitor_StartPerfTest(uint32_t frequency_hz);
void test_SerialMonitor_PerfRejectsInvalidFrequency(void) {
  RESET_FAKE(mocked_esp_timer_create);
  const char *invalid[] = {"0", "-1", "+1", "1.5", "abc", "5001", "4294967296", "99999999999999999999", ""};
  for (size_t i = 0; i < sizeof(invalid)/sizeof(invalid[0]); ++i) {
    char *args[] = {"SER_PERF", "start", (char *)invalid[i]};
    TEST_ASSERT_EQUAL(-1, SerialMonitor_HandlePerfCommand(3, args));
  }
  TEST_ASSERT_EQUAL(-1, SerialMonitor_StartPerfTest(0));
  TEST_ASSERT_EQUAL(-1, SerialMonitor_StartPerfTest(UINT32_MAX));
  TEST_ASSERT_EQUAL(0, mocked_esp_timer_create_fake.call_count);
  char *unknown[] = {"SER_PERF", "unknown"};
  TEST_ASSERT_EQUAL(-1, SerialMonitor_HandlePerfCommand(2, unknown));
  char *extra[] = {"SER_PERF", "stop", "1"};
  TEST_ASSERT_EQUAL(-1, SerialMonitor_HandlePerfCommand(3, extra));
}

extern int32_t SerialMonitor_StopPerfTest(void);
static esp_err_t create_perf_timer(const esp_timer_create_args_t *args, esp_timer_handle_t *out) {
  TEST_ASSERT_NOT_NULL(args->callback); *out = (esp_timer_handle_t)0x777; return ESP_OK;
}
void test_SerialMonitor_PerfTimerFailures(void) {
  setUp_SM(); ISerialMonitor *mon = SerialMonitor_GetModule();
  TEST_ASSERT_EQUAL(0, mon->base.Initialize(&mon->base, NULL));
  TEST_ASSERT_EQUAL(0, mon->base.Start(&mon->base));
  RESET_FAKE(mocked_esp_timer_create); RESET_FAKE(mocked_esp_timer_start_periodic); RESET_FAKE(mocked_esp_timer_stop);
  mocked_esp_timer_create_fake.return_val = ESP_ERR_NO_MEM;
  TEST_ASSERT_EQUAL(-1, SerialMonitor_StartPerfTest(10));
  TEST_ASSERT_EQUAL(0, mocked_esp_timer_start_periodic_fake.call_count);
  mocked_esp_timer_create_fake.custom_fake = create_perf_timer;
  mocked_esp_timer_start_periodic_fake.return_val = ESP_FAIL;
  TEST_ASSERT_EQUAL(-1, SerialMonitor_StartPerfTest(10));
  mocked_esp_timer_start_periodic_fake.return_val = ESP_OK;
  TEST_ASSERT_EQUAL(0, SerialMonitor_StartPerfTest(10));
  TEST_ASSERT_EQUAL(2, mocked_esp_timer_create_fake.call_count);
  TEST_ASSERT_EQUAL(100000, mocked_esp_timer_start_periodic_fake.arg1_val);
  TEST_ASSERT_EQUAL(-1, SerialMonitor_StartPerfTest(10));
  mocked_esp_timer_stop_fake.return_val = ESP_FAIL;
  TEST_ASSERT_EQUAL(-1, SerialMonitor_StopPerfTest());
  TEST_ASSERT_EQUAL(-1, SerialMonitor_StartPerfTest(10));
  mocked_esp_timer_stop_fake.return_val = ESP_OK;
  TEST_ASSERT_EQUAL(0, SerialMonitor_StopPerfTest());
  TEST_ASSERT_EQUAL(0, SerialMonitor_StartPerfTest(5000));
  TEST_ASSERT_EQUAL(200, mocked_esp_timer_start_periodic_fake.arg1_val);
  TEST_ASSERT_EQUAL(2, mocked_esp_timer_create_fake.call_count);
  TEST_ASSERT_EQUAL(0, SerialMonitor_StopPerfTest());
}

static char captured_perf_packet[32];
static size_t captured_perf_packet_length;
static int32_t capture_perf_packet(const void *data, size_t length) {
  if (length >= sizeof(captured_perf_packet)) return -1;
  memcpy(captured_perf_packet, data, length);
  captured_perf_packet[length] = '\0';
  captured_perf_packet_length = length;
  return 0;
}
void test_SerialMonitor_PerfTickPacketFormat(void) {
  setUp_SM(); ISerialMonitor *mon = SerialMonitor_GetModule();
  TEST_ASSERT_EQUAL(0, mon->base.Initialize(&mon->base, NULL));
  TEST_ASSERT_EQUAL(0, mon->base.Start(&mon->base));
  RESET_FAKE(mocked_esp_timer_create);
  RESET_FAKE(mocked_esp_timer_start_periodic);
  RESET_FAKE(mocked_esp_timer_stop);
  mocked_esp_timer_create_fake.custom_fake = create_perf_timer;
  mocked_esp_timer_start_periodic_fake.return_val = ESP_OK;
  TEST_ASSERT_EQUAL(0, SerialMonitor_StartPerfTest(10));
  SerialMonitor_PerfTickLocked(capture_perf_packet);
  TEST_ASSERT_EQUAL_STRING("TIMER_DATA:00000000\n", captured_perf_packet);
  TEST_ASSERT_EQUAL(sizeof("TIMER_DATA:00000000\n") - 1, captured_perf_packet_length);
  SerialMonitor_PerfTickLocked(capture_perf_packet);
  TEST_ASSERT_EQUAL_STRING("TIMER_DATA:00000001\n", captured_perf_packet);
  TEST_ASSERT_EQUAL(0, SerialMonitor_StopPerfTest());
}

void test_SerialMonitor_StopTimeoutPreservesResources(void) {
  setUp_SM(); ISerialMonitor *mon = SerialMonitor_GetModule();
  TEST_ASSERT_EQUAL(0, mon->base.Initialize(&mon->base, NULL));
  TEST_ASSERT_EQUAL(0, mon->base.Start(&mon->base)); TEST_ASSERT_EQUAL(0, mon->StartUART(mon));
  mocked_xQueueReceive_fake.custom_fake = NULL; mocked_xQueueReceive_fake.return_val = pdFALSE;
  TEST_ASSERT_EQUAL(-1, mon->base.Stop(&mon->base));
  TEST_ASSERT_EQUAL(0, port_deinit_fake.call_count); TEST_ASSERT_EQUAL(0, buf_deinit_fake.call_count);
  TEST_ASSERT_EQUAL(0, mocked_vTaskDelete_fake.call_count);
  TEST_ASSERT_EQUAL(-1, mon->WriteLog(mon, "late producer"));
  TEST_ASSERT_EQUAL(-1, mon->StartUART(mon)); TEST_ASSERT_EQUAL(-1, mon->base.CheckHealth(&mon->base));
  mocked_xQueueReceive_fake.custom_fake = stop_ack;
  TEST_ASSERT_EQUAL(0, mon->base.Stop(&mon->base));
  TEST_ASSERT_EQUAL(2, mocked_vTaskDelete_fake.call_count);
  TEST_ASSERT_EQUAL(1, port_deinit_fake.call_count); TEST_ASSERT_EQUAL(1, buf_deinit_fake.call_count);
  TEST_ASSERT_EQUAL(0, mon->base.Start(&mon->base));
}
static BaseType_t fail_second_worker(TaskFunction_t fn, const char *name, uint32_t stack, void *arg, UBaseType_t priority, TaskHandle_t *out) {
  if (created_tasks == 1) return pdFAIL;
  return create_worker(fn, name, stack, arg, priority, out);
}
void test_SerialMonitor_PartialStartWaitsForWorker(void) {
  setUp_SM(); ISerialMonitor *mon = SerialMonitor_GetModule();
  mon->base.Initialize(&mon->base, NULL); mon->base.Start(&mon->base);
  mocked_xTaskCreate_fake.custom_fake = fail_second_worker;
  mocked_xQueueReceive_fake.custom_fake = NULL; mocked_xQueueReceive_fake.return_val = pdFALSE;
  TEST_ASSERT_EQUAL(-1, mon->StartUART(mon));
  TEST_ASSERT_EQUAL(0, port_deinit_fake.call_count); TEST_ASSERT_EQUAL(0, buf_deinit_fake.call_count);
  TEST_ASSERT_TRUE(serialMonitor.uart_owned);
  mocked_xQueueReceive_fake.custom_fake = stop_ack;
  TEST_ASSERT_EQUAL(0, mon->StopUART(mon));
  TEST_ASSERT_EQUAL(1, mocked_vTaskDelete_fake.call_count); TEST_ASSERT_EQUAL(1, port_deinit_fake.call_count);
}
void test_SerialMonitor_CleanupFailuresAreRetryable(void) {
  setUp_SM(); ISerialMonitor *mon = SerialMonitor_GetModule();
  mon->base.Initialize(&mon->base, NULL); mon->base.Start(&mon->base); mon->StartUART(mon);
  port_deinit_fake.return_val = -1;
  TEST_ASSERT_EQUAL(-1, mon->base.Stop(&mon->base)); TEST_ASSERT_EQUAL(0, buf_deinit_fake.call_count);
  port_deinit_fake.return_val = 0; buf_deinit_fake.return_val = -1;
  TEST_ASSERT_EQUAL(-1, mon->base.Stop(&mon->base));
  TEST_ASSERT_EQUAL(-1, mon->WriteRaw(mon, "x", 1));
  TEST_ASSERT_EQUAL(2, port_deinit_fake.call_count);
  TEST_ASSERT_EQUAL(0, mon->StopUART(mon));
  TEST_ASSERT_EQUAL(-1, mon->WriteRaw(mon, "still stopping", 14));
  buf_deinit_fake.return_val = 0;
  TEST_ASSERT_EQUAL(0, mon->base.Stop(&mon->base));
  TEST_ASSERT_EQUAL(2, port_deinit_fake.call_count); TEST_ASSERT_EQUAL(2, buf_deinit_fake.call_count);
}
void test_SerialMonitor_WorkerCannotStopItself(void) {
  setUp_SM(); ISerialMonitor *mon = SerialMonitor_GetModule();
  mon->base.Initialize(&mon->base, NULL); mon->base.Start(&mon->base); mon->StartUART(mon);
  mocked_xTaskGetCurrentTaskHandle_fake.return_val = atomic_load(&serialMonitor.rx_worker.task);
  TEST_ASSERT_EQUAL(-1, mon->base.Stop(&mon->base)); TEST_ASSERT_EQUAL(-1, mon->StopUART(mon));
  TEST_ASSERT_EQUAL(0, port_deinit_fake.call_count); TEST_ASSERT_EQUAL(0, mocked_xQueueReceive_fake.call_count);
}
void test_SerialMonitor_AllocationAndProducerGate(void) {
  setUp_SM(); ISerialMonitor *mon = SerialMonitor_GetModule();
  mocked_xQueueCreate_fake.return_val = NULL;
  TEST_ASSERT_EQUAL(-1, mon->base.Initialize(&mon->base, NULL));
  TEST_ASSERT_EQUAL(SERVICE_STATE_UNINITIALIZED, mon->base.GetState(&mon->base));
  mocked_xQueueCreate_fake.return_val = (QueueHandle_t)0x111;
  TEST_ASSERT_EQUAL(0, mon->base.Initialize(&mon->base, NULL));
  TEST_ASSERT_EQUAL(0, mon->base.Start(&mon->base));
  mocked_xSemaphoreTake_fake.return_val = pdFALSE;
  TEST_ASSERT_EQUAL(-1, mon->WriteRaw(mon, "x", 1)); TEST_ASSERT_EQUAL(0, buf_push_fake.call_count);
  TEST_ASSERT_EQUAL(-1, mon->base.Stop(&mon->base)); TEST_ASSERT_EQUAL(0, buf_deinit_fake.call_count);
}

static jmp_buf worker_park;
static bool expect_tx_return;
static BaseType_t boundary_ack(QueueHandle_t queue, const void *value, TickType_t ticks) {
  (void)queue; (void)ticks;
  TEST_ASSERT_EQUAL(0, *(const int32_t *)value);
  if (expect_tx_return) TEST_ASSERT_EQUAL(1, buf_return_fake.call_count);
  else TEST_ASSERT_EQUAL(1, port_read_fake.call_count);
  return pdTRUE;
}
static void park_worker(TaskHandle_t task) { (void)task; longjmp(worker_park, 1); }
static void *held_tx_item(ISerialBuffer *buffer, size_t *size, uint32_t timeout) {
  (void)buffer; (void)timeout; static char byte = 'x'; *size = 1;
  atomic_store(&serialMonitor.pending_tx, 1);
  atomic_store(&serialMonitor.tx_worker.stopRequested, true); return &byte;
}
static int32_t complete_rx(ISerialPort *port, void *buffer, size_t size, uint32_t timeout) {
  (void)port; (void)buffer; (void)size; (void)timeout;
  atomic_store(&serialMonitor.rx_worker.stopRequested, true); return 0;
}
void test_SerialMonitor_WorkersAcknowledgeAtSafeBoundary(void) {
  setUp_SM(); ISerialMonitor *mon = SerialMonitor_GetModule();
  mon->base.Initialize(&mon->base, NULL); mon->base.Start(&mon->base);
  RESET_FAKE(mocked_xQueueSend); RESET_FAKE(mocked_vTaskSuspend);
  mocked_xQueueSend_fake.custom_fake = boundary_ack;
  mocked_vTaskSuspend_fake.custom_fake = park_worker;
  expect_tx_return = true; buf_pop_fake.custom_fake = held_tx_item;
  if (setjmp(worker_park) == 0) { SerialMonitor_TXTask(NULL); TEST_FAIL_MESSAGE("TX exited without acknowledgment"); }
  TEST_ASSERT_EQUAL(1, buf_pop_fake.call_count); TEST_ASSERT_EQUAL(2, mocked_xQueueSend_fake.call_count);
  expect_tx_return = false; port_read_fake.custom_fake = complete_rx;
  if (setjmp(worker_park) == 0) { SerialMonitor_RXTask(NULL); TEST_FAIL_MESSAGE("RX exited without acknowledgment"); }
  TEST_ASSERT_EQUAL(3, mocked_xQueueSend_fake.call_count);
}

void test_SerialMonitor_PerfStopFailurePreservesResources(void) {
  setUp_SM(); ISerialMonitor *mon = SerialMonitor_GetModule();
  mon->base.Initialize(&mon->base, NULL); mon->base.Start(&mon->base); mon->StartUART(mon);
  mocked_esp_timer_start_periodic_fake.return_val = ESP_OK;
  TEST_ASSERT_EQUAL(0, SerialMonitor_StartPerfTest(10));
  mocked_esp_timer_stop_fake.return_val = ESP_FAIL;
  TEST_ASSERT_EQUAL(-1, mon->base.Stop(&mon->base));
  TEST_ASSERT_EQUAL(0, port_deinit_fake.call_count); TEST_ASSERT_EQUAL(0, buf_deinit_fake.call_count);
  TEST_ASSERT_EQUAL(-1, mon->WriteLog(mon, "timer still running"));
  TEST_ASSERT_EQUAL(-1, SerialMonitor_StartPerfTest(20));
  mocked_esp_timer_stop_fake.return_val = ESP_OK;
  TEST_ASSERT_EQUAL(0, mon->base.Stop(&mon->base));
  TEST_ASSERT_EQUAL(1, port_deinit_fake.call_count); TEST_ASSERT_EQUAL(1, buf_deinit_fake.call_count);
}

static const uint8_t tx_payload[] = {1, 2, 3, 4, 5};
static int32_t tx_results[3];
static unsigned tx_write_index;
static size_t tx_offset;
static void *tx_test_item(ISerialBuffer *buffer, size_t *size, uint32_t timeout) {
  (void)buffer; (void)timeout;
  *size = sizeof(tx_payload);
  atomic_store(&serialMonitor.pending_tx, 1);
  atomic_store(&serialMonitor.tx_worker.stopRequested, true);
  return (void *)tx_payload;
}
static int32_t tx_test_write(ISerialPort *port, const void *data, size_t size) {
  (void)port;
  TEST_ASSERT_LESS_THAN(3, tx_write_index);
  TEST_ASSERT_EQUAL_PTR(tx_payload + tx_offset, data);
  TEST_ASSERT_EQUAL(sizeof(tx_payload) - tx_offset, size);
  int32_t result = tx_results[tx_write_index++];
  if (result > 0 && (size_t)result <= size) tx_offset += (size_t)result;
  return result;
}
static void verify_tx_result(int32_t first, int32_t second, size_t accepted, unsigned calls) {
  setUp_SM(); ISerialMonitor *mon = SerialMonitor_GetModule();
  TEST_ASSERT_EQUAL(0, mon->base.Initialize(&mon->base, NULL));
  TEST_ASSERT_EQUAL(0, mon->base.Start(&mon->base));
  ISerialMonitorPlugin plugin = {.name = "tx evidence", .OnDataTransmitted = plugin_tx};
  TEST_ASSERT_EQUAL(0, mon->AddPlugin(mon, &plugin));
  tx_results[0] = first; tx_results[1] = second; tx_results[2] = -1;
  tx_write_index = 0; tx_offset = 0;
  buf_pop_fake.custom_fake = tx_test_item; port_write_fake.custom_fake = tx_test_write;
  RESET_FAKE(mocked_xQueueSend); RESET_FAKE(mocked_vTaskSuspend);
  mocked_xQueueSend_fake.custom_fake = boundary_ack;
  mocked_vTaskSuspend_fake.custom_fake = park_worker; expect_tx_return = true;
  if (setjmp(worker_park) == 0) { SerialMonitor_TXTask(NULL); TEST_FAIL_MESSAGE("TX failed to park"); }
  TEST_ASSERT_EQUAL(calls, port_write_fake.call_count);
  TEST_ASSERT_EQUAL(accepted ? 1 : 0, plugin_tx_fake.call_count);
  if (accepted) {
    TEST_ASSERT_EQUAL_PTR(tx_payload, plugin_tx_fake.arg1_val);
    TEST_ASSERT_EQUAL(accepted, plugin_tx_fake.arg2_val);
  }
  TEST_ASSERT_EQUAL(1, buf_return_fake.call_count);
  TEST_ASSERT_EQUAL_PTR(tx_payload, buf_return_fake.arg1_val);
  TEST_ASSERT_EQUAL(2, mocked_xQueueSend_fake.call_count);
  TEST_ASSERT_EQUAL(0, atomic_load(&serialMonitor.pending_tx));
  TEST_ASSERT_EQUAL(accepted != sizeof(tx_payload), atomic_load(&serialMonitor.tx_failed));
  SerialMonitorStats stats; TEST_ASSERT_EQUAL(0,mon->GetStats(mon,&stats));
  TEST_ASSERT_EQUAL(accepted,stats.tx_bytes);
  TEST_ASSERT_EQUAL(sizeof(tx_payload)-accepted,stats.tx_dropped_bytes);
  TEST_ASSERT_EQUAL(accepted != sizeof(tx_payload),stats.tx_failures);
  TEST_ASSERT_EQUAL(0, mon->RemovePlugin(mon, &plugin));
  TEST_ASSERT_EQUAL(0, mon->base.Stop(&mon->base));
}
void test_SerialMonitor_TxShortWriteAdvancesWithoutDuplication(void) {
  verify_tx_result(2, 3, 5, 2);
  verify_tx_result(5, -1, 5, 1);
}
void test_SerialMonitor_TxFailureNeverReportsUnacceptedBytes(void) {
  verify_tx_result(-1, -1, 0, 1);
  verify_tx_result(0, -1, 0, 1);
  verify_tx_result(6, -1, 0, 1);
  verify_tx_result(2, -1, 2, 2);
  verify_tx_result(2, 0, 2, 2);
  verify_tx_result(2, 4, 2, 2);
}

static void assert_plugin_reentry_rejected(ISerialMonitorPlugin *plugin) {
  ISerialMonitor *mon = SerialMonitor_GetModule();
  unsigned before = mocked_xSemaphoreTake_fake.call_count;
  TEST_ASSERT_EQUAL(-1, mon->AddPlugin(mon, plugin));
  TEST_ASSERT_EQUAL(-1, mon->RemovePlugin(mon, plugin));
  TEST_ASSERT_EQUAL(-1, mon->base.Stop(&mon->base));
  TEST_ASSERT_EQUAL(-1, mon->Flush(mon));
  TEST_ASSERT_EQUAL(before, mocked_xSemaphoreTake_fake.call_count);
  TEST_ASSERT_EQUAL(0, mon->WriteRaw(mon, "callback", 8));
}
static int32_t reentrant_init(ISerialMonitorPlugin *plugin) {
  assert_plugin_reentry_rejected(plugin); return 0;
}
static void reentrant_deinit(ISerialMonitorPlugin *plugin) {
  assert_plugin_reentry_rejected(plugin);
}
void test_SerialMonitor_PluginHooksRejectReentryWithoutWaiting(void) {
  setUp_SM(); ISerialMonitor *mon = SerialMonitor_GetModule();
  mon->base.Initialize(&mon->base, NULL); mon->base.Start(&mon->base);
  mocked_xTaskGetCurrentTaskHandle_fake.return_val = (TaskHandle_t)0x123;
  ISerialMonitorPlugin plugin = {.Init=reentrant_init, .Deinit=reentrant_deinit};
  TEST_ASSERT_EQUAL(0, mon->AddPlugin(mon, &plugin));
  TEST_ASSERT_EQUAL(1, serialMonitor.plugin_count);
  TEST_ASSERT_NULL(atomic_load(&serialMonitor.plugin_owner));
  TEST_ASSERT_EQUAL(0, mon->RemovePlugin(mon, &plugin));
  TEST_ASSERT_EQUAL(0, serialMonitor.plugin_count);
  TEST_ASSERT_NULL(atomic_load(&serialMonitor.plugin_owner));
  TEST_ASSERT_EQUAL(2, buf_push_fake.call_count);
  TEST_ASSERT_EQUAL(0, mon->base.Stop(&mon->base));
}
void test_SerialMonitor_PluginCapacityAndRemovalFailurePreserveRegistry(void) {
  setUp_SM(); ISerialMonitor *mon = SerialMonitor_GetModule();
  mon->base.Initialize(&mon->base, NULL);
  ISerialMonitorPlugin plugins[SERIAL_MONITOR_MAX_PLUGINS + 1] = {0};
  for (unsigned i=0; i<SERIAL_MONITOR_MAX_PLUGINS; ++i)
    TEST_ASSERT_EQUAL(0, mon->AddPlugin(mon, &plugins[i]));
  TEST_ASSERT_EQUAL(0, mon->AddPlugin(mon, &plugins[0]));
  TEST_ASSERT_EQUAL(-1, mon->AddPlugin(mon, &plugins[SERIAL_MONITOR_MAX_PLUGINS]));
  mocked_xSemaphoreTake_fake.return_val = pdFALSE;
  TEST_ASSERT_EQUAL(-1, mon->RemovePlugin(mon, &plugins[0]));
  TEST_ASSERT_EQUAL(SERIAL_MONITOR_MAX_PLUGINS, serialMonitor.plugin_count);
  mocked_xSemaphoreTake_fake.return_val = pdTRUE;
  for (unsigned i=0; i<SERIAL_MONITOR_MAX_PLUGINS; ++i)
    TEST_ASSERT_EQUAL(0, mon->RemovePlugin(mon, &plugins[i]));
}
static BaseType_t finish_pending_tx(QueueHandle_t queue, void *data, TickType_t timeout) {
  TEST_ASSERT_EQUAL_PTR(serialMonitor.tx_progress, queue);
  TEST_ASSERT_EQUAL(0, port_wait_fake.call_count);
  TEST_ASSERT_EQUAL(pdMS_TO_TICKS(SERIAL_MONITOR_FLUSH_TIMEOUT_MS), timeout);
  *(uint32_t *)data = 0;
  atomic_store(&serialMonitor.pending_tx, 0);
  mocked_xTaskGetTickCount_fake.return_val = 400;
  return pdTRUE;
}
void test_SerialMonitor_FlushDrainsBeforeHardwareWait(void) {
  setUp_SM(); ISerialMonitor *mon = SerialMonitor_GetModule();
  mon->base.Initialize(&mon->base, NULL); mon->base.Start(&mon->base); mon->StartUART(mon);
  RESET_FAKE(mocked_xTaskGetTickCount);
  TEST_ASSERT_EQUAL(0, mon->WriteRaw(mon, "one", 3));
  TEST_ASSERT_EQUAL(0, mon->WriteRaw(mon, "two", 3));
  TEST_ASSERT_EQUAL(2, atomic_load(&serialMonitor.pending_tx));
  mocked_xQueueReceive_fake.custom_fake = finish_pending_tx;
  TEST_ASSERT_EQUAL(0, mon->Flush(mon));
  TEST_ASSERT_EQUAL(1, port_wait_fake.call_count);
  TEST_ASSERT_EQUAL(SERIAL_MONITOR_FLUSH_TIMEOUT_MS - 400, port_wait_fake.arg1_val);
  TEST_ASSERT_EQUAL(0, atomic_load(&serialMonitor.pending_tx));
}
void test_SerialMonitor_FlushTimeoutAndFailureAreRetryable(void) {
  setUp_SM(); ISerialMonitor *mon = SerialMonitor_GetModule();
  mon->base.Initialize(&mon->base, NULL); mon->base.Start(&mon->base); mon->StartUART(mon);
  RESET_FAKE(mocked_xTaskGetTickCount);
  buf_push_fake.return_val = -1;
  TEST_ASSERT_EQUAL(-1, mon->WriteRaw(mon, "full", 4));
  TEST_ASSERT_EQUAL(0, atomic_load(&serialMonitor.pending_tx));
  atomic_store(&serialMonitor.pending_tx, 1);
  mocked_xQueueReceive_fake.custom_fake = NULL;
  mocked_xQueueReceive_fake.return_val = pdFALSE;
  TEST_ASSERT_EQUAL(-1, mon->Flush(mon));
  TEST_ASSERT_EQUAL(1, atomic_load(&serialMonitor.pending_tx));
  TEST_ASSERT_EQUAL(0, port_wait_fake.call_count);
  atomic_store(&serialMonitor.pending_tx, 0);
  atomic_store(&serialMonitor.tx_failed, true);
  port_wait_fake.return_val = -1;
  TEST_ASSERT_EQUAL(-1, mon->Flush(mon));
  TEST_ASSERT_TRUE(atomic_load(&serialMonitor.tx_failed));
  port_wait_fake.return_val = 0;
  TEST_ASSERT_EQUAL(-1, mon->Flush(mon));
  TEST_ASSERT_FALSE(atomic_load(&serialMonitor.tx_failed));
  TEST_ASSERT_EQUAL(0, mon->Flush(mon));
}

void test_SerialMonitor_ConfigRejectsUnsupportedFeatures(void) {
  setUp_SM(); ISerialMonitor *mon = SerialMonitor_GetModule();
  TEST_ASSERT_EQUAL(0, mon->base.Initialize(&mon->base, NULL));
  SerialMonitorConfig baseline = SerialMonitorConfig_GetDefault();
  TEST_ASSERT_TRUE(SerialMonitorConfig_IsValid(&baseline));
  TEST_ASSERT_EQUAL(0, mon->SetConfig(mon, &baseline));
  SerialMonitorConfig config;
#define REJECT_OPTION(field, value) do { \
  config = baseline; config.field = value; \
  TEST_ASSERT_FALSE(SerialMonitorConfig_IsValid(&config)); \
  TEST_ASSERT_EQUAL(-1, mon->SetConfig(mon, &config)); \
  TEST_ASSERT_EQUAL_MEMORY(&baseline, &serialMonitor.config, sizeof(baseline)); \
} while (0)
  REJECT_OPTION(use_dma, true); REJECT_OPTION(use_async, false);
  REJECT_OPTION(echo_enabled, true); REJECT_OPTION(enable_binary_log, true);
  REJECT_OPTION(use_memory_pool, true); REJECT_OPTION(pool_block_size, 128);
  REJECT_OPTION(pool_block_count, 32); REJECT_OPTION(queue_size, 64);
  REJECT_OPTION(use_isr_queue, true); REJECT_OPTION(isr_queue_size, 10);
#undef REJECT_OPTION
}
void test_SerialMonitor_ConfigValidatesResourceBoundsBeforeApplying(void) {
  setUp_SM(); ISerialMonitor *mon = SerialMonitor_GetModule();
  TEST_ASSERT_EQUAL(0, mon->base.Initialize(&mon->base, NULL));
  SerialMonitorConfig base = SerialMonitorConfig_GetDefault(), config;
  TEST_ASSERT_FALSE(SerialMonitorConfig_IsValid(NULL));
#define REJECT_BOUND(field, value) do { \
  config = base; config.field = value; \
  TEST_ASSERT_EQUAL(-1, mon->SetConfig(mon, &config)); \
} while (0)
  REJECT_BOUND(uart_num, SERIAL_MONITOR_MAX_UART_NUM + 1);
  REJECT_BOUND(baud_rate, SERIAL_MONITOR_MIN_BAUD_RATE - 1);
  REJECT_BOUND(baud_rate, SERIAL_MONITOR_MAX_BAUD_RATE + 1);
  REJECT_BOUND(rx_buffer_size, 128); REJECT_BOUND(tx_buffer_size, 128);
  REJECT_BOUND(ring_buffer_size, SERIAL_MONITOR_MIN_RING_BUFFER_SIZE - 1);
  REJECT_BOUND(task_priority, 0); REJECT_BOUND(task_priority, configMAX_PRIORITIES);
#undef REJECT_BOUND
  config = base; config.rx_buffer_size = 129; config.tx_buffer_size = 0;
  config.ring_buffer_size = SERIAL_MONITOR_MIN_RING_BUFFER_SIZE;
  config.task_priority = configMAX_PRIORITIES - 1;
  TEST_ASSERT_EQUAL(0, mon->SetConfig(mon, &config));
  TEST_ASSERT_EQUAL(0, mon->base.Start(&mon->base));
  TEST_ASSERT_EQUAL(-1, mon->SetConfig(mon, &base));
  TEST_ASSERT_EQUAL_MEMORY(&config, &serialMonitor.config, sizeof(config));
  TEST_ASSERT_EQUAL(0, mon->base.Stop(&mon->base));
}

void test_SerialMonitor_PerfControlsRespectLifecycleAndContention(void) {
  setUp_SM(); ISerialMonitor *mon = SerialMonitor_GetModule();
  TEST_ASSERT_EQUAL(-1, SerialMonitor_StartPerfTest(10));
  TEST_ASSERT_EQUAL(0, mon->base.Initialize(&mon->base, NULL));
  TEST_ASSERT_EQUAL(-1, SerialMonitor_StartPerfTest(10));
  TEST_ASSERT_EQUAL(0, mon->base.Start(&mon->base));
  mocked_esp_timer_start_periodic_fake.return_val = ESP_OK;
  TEST_ASSERT_EQUAL(0, SerialMonitor_StartPerfTest(10));
  unsigned starts = mocked_esp_timer_start_periodic_fake.call_count;
  unsigned stops = mocked_esp_timer_stop_fake.call_count;
  mocked_xSemaphoreTake_fake.return_val = pdFALSE;
  TEST_ASSERT_EQUAL(-1, SerialMonitor_StartPerfTest(20));
  TEST_ASSERT_EQUAL(-1, SerialMonitor_StopPerfTest());
  SerialMonitor_PerfTick();
  TEST_ASSERT_EQUAL(0, mocked_xSemaphoreTake_fake.arg1_val);
  TEST_ASSERT_EQUAL(0, buf_push_fake.call_count);
  TEST_ASSERT_EQUAL(starts, mocked_esp_timer_start_periodic_fake.call_count);
  TEST_ASSERT_EQUAL(stops, mocked_esp_timer_stop_fake.call_count);
  mocked_xSemaphoreTake_fake.return_val = pdTRUE;
  SerialMonitor_PerfTick();
  TEST_ASSERT_EQUAL(1, buf_push_fake.call_count);
  TEST_ASSERT_EQUAL(1, atomic_load(&serialMonitor.pending_tx));
  serialMonitor.stopping = true;
  TEST_ASSERT_EQUAL(-1, SerialMonitor_StartPerfTest(20));
  SerialMonitor_PerfTick(); TEST_ASSERT_EQUAL(1, buf_push_fake.call_count);
  serialMonitor.stopping = false;
  mocked_esp_timer_stop_fake.return_val = ESP_OK;
  TEST_ASSERT_EQUAL(0, SerialMonitor_StopPerfTest());
  SerialMonitor_PerfTick(); TEST_ASSERT_EQUAL(1, buf_push_fake.call_count);
  TEST_ASSERT_EQUAL(0, mon->base.Stop(&mon->base));
  TEST_ASSERT_EQUAL(-1, SerialMonitor_StartPerfTest(10));
}

static unsigned categorized_registrations, plain_registrations;
static esp_err_t registration_result;
static esp_err_t register_perf_category(IDebugConsole *console, const esp_console_cmd_t *cmd, CommandCategory category) {
  TEST_ASSERT_NOT_NULL(console); TEST_ASSERT_EQUAL_STRING("SER_PERF", cmd->command);
  TEST_ASSERT_TRUE(cmd->func == SerialMonitor_HandlePerfCommand);
  TEST_ASSERT_EQUAL(CMD_CAT_NETWORK, category); categorized_registrations++;
  return registration_result;
}
static esp_err_t register_perf_plain(IDebugConsole *console, const esp_console_cmd_t *cmd) {
  TEST_ASSERT_NOT_NULL(console); TEST_ASSERT_EQUAL_STRING("SER_PERF", cmd->command);
  TEST_ASSERT_TRUE(cmd->func == SerialMonitor_HandlePerfCommand); plain_registrations++;
  return registration_result;
}
static void reset_registration(void) {
  RESET_FAKE(mocked_ServiceLocator_GetService); RESET_FAKE(mocked_esp_console_cmd_register);
  categorized_registrations=plain_registrations=0;
}
void test_SerialMonitor_ConsoleCategoryFailureDoesNotBypassRegistry(void) {
  reset_registration(); IDebugConsole console={0};
  console.RegisterCommandWithCategory=register_perf_category;
  console.RegisterCommand=register_perf_plain;
  mocked_ServiceLocator_GetService_fake.return_val=&console.base;
  registration_result=ESP_ERR_NO_MEM;
  TEST_ASSERT_EQUAL(ESP_ERR_NO_MEM, SerialMonitor_RegisterConsoleCommands());
  TEST_ASSERT_EQUAL(1,categorized_registrations); TEST_ASSERT_EQUAL(0,plain_registrations);
  TEST_ASSERT_EQUAL(0,mocked_esp_console_cmd_register_fake.call_count);
  registration_result=ESP_OK;
  TEST_ASSERT_EQUAL(ESP_OK, SerialMonitor_RegisterConsoleCommands());
  TEST_ASSERT_EQUAL(2,categorized_registrations);
  TEST_ASSERT_EQUAL(0,mocked_esp_console_cmd_register_fake.call_count);
  reset_registration();
}
void test_SerialMonitor_ConsolePlainRegistryAndMissingMethods(void) {
  reset_registration(); IDebugConsole console={0}; console.RegisterCommand=register_perf_plain;
  mocked_ServiceLocator_GetService_fake.return_val=&console.base;
  registration_result=ESP_FAIL;
  TEST_ASSERT_EQUAL(ESP_FAIL,SerialMonitor_RegisterConsoleCommands());
  TEST_ASSERT_EQUAL(1,plain_registrations);
  registration_result=ESP_OK;
  TEST_ASSERT_EQUAL(ESP_OK,SerialMonitor_RegisterConsoleCommands());
  console.RegisterCommand=NULL;
  TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE,SerialMonitor_RegisterConsoleCommands());
  TEST_ASSERT_EQUAL(2,plain_registrations);
  TEST_ASSERT_EQUAL(0,mocked_esp_console_cmd_register_fake.call_count);
  reset_registration();
}
void test_SerialMonitor_ConsoleMissingBoundaryDoesNotCreateSdkOrphans(void) {
  reset_registration();
  mocked_esp_console_cmd_register_fake.return_val = ESP_OK;
  TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, SerialMonitor_RegisterConsoleCommands());
  TEST_ASSERT_EQUAL(0, mocked_esp_console_cmd_register_fake.call_count);
  reset_registration();
}

void test_SerialMonitor_StatsPreserveOwnershipAndAdmissionEvidence(void) {
  setUp_SM(); ISerialMonitor *mon=SerialMonitor_GetModule();
  TEST_ASSERT_EQUAL(0,mon->base.Initialize(&mon->base,NULL));
  TEST_ASSERT_EQUAL(0,mon->base.Start(&mon->base));
  TEST_ASSERT_EQUAL(0,mon->WriteRaw(mon,"bytes",5));
  buf_push_fake.return_val=-1; TEST_ASSERT_EQUAL(-1,mon->WriteRaw(mon,"full",4));
  SerialMonitorStats stats;
  TEST_ASSERT_EQUAL(0,mon->GetStats(mon,&stats));
  TEST_ASSERT_EQUAL(5,stats.queued_bytes); TEST_ASSERT_EQUAL(0,stats.tx_bytes);
  TEST_ASSERT_EQUAL(1,stats.rejected_packets); TEST_ASSERT_EQUAL(1,stats.pending_tx);
  TEST_ASSERT_FALSE(stats.uart_owned); TEST_ASSERT_FALSE(stats.active);
  SerialMonitorStats before=stats; mocked_xSemaphoreTake_fake.return_val=pdFALSE;
  TEST_ASSERT_EQUAL(-1,mon->GetStats(mon,&stats)); TEST_ASSERT_EQUAL_MEMORY(&before,&stats,sizeof(stats));
  mocked_xSemaphoreTake_fake.return_val=pdTRUE;
  TEST_ASSERT_EQUAL(0,mon->base.Stop(&mon->base));
  TEST_ASSERT_EQUAL(0,mon->GetStats(mon,&stats));
  TEST_ASSERT_EQUAL(SERVICE_STATE_STOPPED,stats.state); TEST_ASSERT_EQUAL(0,stats.pending_tx);
  TEST_ASSERT_EQUAL(5,stats.queued_bytes);
  TEST_ASSERT_EQUAL(-1,mon->GetStats(mon,NULL));
}
static int32_t diagnostic_rx_result;
static int32_t diagnostic_rx(ISerialPort *port,void *data,size_t size,uint32_t timeout) {
  (void)port;(void)data;(void)size;(void)timeout;
  atomic_store(&serialMonitor.rx_worker.stopRequested,true);return diagnostic_rx_result;
}
void test_SerialMonitor_RxStatsDistinguishDataTimeoutAndDriverFailure(void) {
  int32_t results[]={3,0,-1,129};
  for (unsigned i=0;i<4;++i) {
    setUp_SM(); ISerialMonitor *mon=SerialMonitor_GetModule();
    TEST_ASSERT_EQUAL(0,mon->base.Initialize(&mon->base,NULL)); TEST_ASSERT_EQUAL(0,mon->base.Start(&mon->base));
    diagnostic_rx_result=results[i];port_read_fake.custom_fake=diagnostic_rx;
    RESET_FAKE(mocked_xQueueSend);mocked_xQueueSend_fake.custom_fake=boundary_ack;expect_tx_return=false;
    RESET_FAKE(mocked_vTaskSuspend);mocked_vTaskSuspend_fake.custom_fake=park_worker;
    if (setjmp(worker_park)==0) {SerialMonitor_RXTask(NULL);TEST_FAIL_MESSAGE("RX failed to park");}
    SerialMonitorStats stats;TEST_ASSERT_EQUAL(0,mon->GetStats(mon,&stats));
    TEST_ASSERT_EQUAL(i==0?3:0,stats.rx_bytes);TEST_ASSERT_EQUAL(i>=2?1:0,stats.rx_failures);
  }
}
void test_SerialMonitor_UartConsoleChecksOwnedLifecycleAndFailures(void) {
  setUp_SM();
  ISerialMonitor *monitor = SerialMonitor_GetModule();
  TEST_ASSERT_EQUAL(0, monitor->base.Initialize(&monitor->base, NULL));
  TEST_ASSERT_EQUAL(0, monitor->base.Start(&monitor->base));
  char *start[] = {"SER_PERF", "uart-start"};
  char *stop[] = {"SER_PERF", "uart-stop"};
  char *extra[] = {"SER_PERF", "uart-start", "1"};
  TEST_ASSERT_EQUAL(-1, SerialMonitor_HandlePerfCommand(3, extra));
  port_init_fake.return_val = -1;
  TEST_ASSERT_EQUAL(-1, SerialMonitor_HandlePerfCommand(2, start));
  TEST_ASSERT_FALSE(serialMonitor.uart_owned);
  port_init_fake.return_val = 0;
  TEST_ASSERT_EQUAL(0, SerialMonitor_HandlePerfCommand(2, start));
  TEST_ASSERT_TRUE(serialMonitor.uart_owned);
  TEST_ASSERT_EQUAL(0, SerialMonitor_HandlePerfCommand(2, stop));
  TEST_ASSERT_FALSE(serialMonitor.uart_owned);
}

void test_SerialMonitor_MachineConsoleChecksLifecycleAndMalformedArguments(void) {
  setUp_SM();
  TEST_ASSERT_EQUAL(-1, SerialMonitor_HandlePerfCommand(3, NULL));
  char *missing[] = {"SER_PERF", "start", NULL};
  TEST_ASSERT_EQUAL(-1, SerialMonitor_HandlePerfCommand(3, missing));
  ISerialMonitor *monitor = SerialMonitor_GetModule();
  TEST_ASSERT_EQUAL(0, monitor->base.Initialize(&monitor->base, NULL));
  TEST_ASSERT_EQUAL(0, monitor->base.Start(&monitor->base));
  char *status[] = {"SER_PERF", "status", "--machine"};
  char *start[] = {"SER_PERF", "uart-start", "--machine"};
  char *stop[] = {"SER_PERF", "uart-stop", "--machine"};
  TEST_ASSERT_EQUAL(0, SerialMonitor_HandlePerfCommand(3, status));
  port_init_fake.return_val = -1;
  TEST_ASSERT_EQUAL(-1, SerialMonitor_HandlePerfCommand(3, start));
  TEST_ASSERT_FALSE(serialMonitor.uart_owned);
  port_init_fake.return_val = 0;
  TEST_ASSERT_EQUAL(0, SerialMonitor_HandlePerfCommand(3, start));
  TEST_ASSERT_TRUE(serialMonitor.uart_owned);
  TEST_ASSERT_EQUAL(0, SerialMonitor_HandlePerfCommand(3, stop));
  TEST_ASSERT_FALSE(serialMonitor.uart_owned);
}

void test_SerialMonitor_All(void) {
  RUN_TEST(test_SerialMonitor_MachineConsoleChecksLifecycleAndMalformedArguments);
  RUN_TEST(test_SerialMonitor_UartConsoleChecksOwnedLifecycleAndFailures);
  RUN_TEST(test_SerialMonitor_StatsPreserveOwnershipAndAdmissionEvidence);
  RUN_TEST(test_SerialMonitor_RxStatsDistinguishDataTimeoutAndDriverFailure);
  RUN_TEST(test_SerialMonitor_ConsoleCategoryFailureDoesNotBypassRegistry);
  RUN_TEST(test_SerialMonitor_ConsolePlainRegistryAndMissingMethods);
  RUN_TEST(test_SerialMonitor_ConsoleMissingBoundaryDoesNotCreateSdkOrphans);
  RUN_TEST(test_SerialMonitor_PerfControlsRespectLifecycleAndContention);
  RUN_TEST(test_SerialMonitor_ConfigRejectsUnsupportedFeatures);
  RUN_TEST(test_SerialMonitor_ConfigValidatesResourceBoundsBeforeApplying);
  RUN_TEST(test_SerialMonitor_FlushDrainsBeforeHardwareWait);
  RUN_TEST(test_SerialMonitor_FlushTimeoutAndFailureAreRetryable);
  RUN_TEST(test_SerialMonitor_PluginHooksRejectReentryWithoutWaiting);
  RUN_TEST(test_SerialMonitor_PluginCapacityAndRemovalFailurePreserveRegistry);
  RUN_TEST(test_SerialMonitor_TxShortWriteAdvancesWithoutDuplication);
  RUN_TEST(test_SerialMonitor_TxFailureNeverReportsUnacceptedBytes);
  RUN_TEST(test_SerialMonitor_PerfStopFailurePreservesResources);
  RUN_TEST(test_SerialMonitor_WorkersAcknowledgeAtSafeBoundary);
  RUN_TEST(test_SerialMonitor_StopTimeoutPreservesResources);
  RUN_TEST(test_SerialMonitor_PartialStartWaitsForWorker);
  RUN_TEST(test_SerialMonitor_CleanupFailuresAreRetryable);
  RUN_TEST(test_SerialMonitor_WorkerCannotStopItself);
  RUN_TEST(test_SerialMonitor_AllocationAndProducerGate);
  RUN_TEST(test_SerialMonitor_PerfTimerFailures);
  RUN_TEST(test_SerialMonitor_PerfRejectsInvalidFrequency);
  RUN_TEST(test_SerialMonitor_Lifecycle);
  RUN_TEST(test_SerialMonitor_Plugins);
  RUN_TEST(test_SerialMonitor_Writing);
}
