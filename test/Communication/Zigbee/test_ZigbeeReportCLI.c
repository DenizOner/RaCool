#include "test_zigbee_shared.h"
#include "test_zigbee_mock_eventbus.h"
#include "unity.h"
FAKE_VALUE_FUNC2(ezb_err_t, router_next_route, ezb_nwk_info_iterator_t *, ezb_nwk_route_info_t *);
FAKE_VALUE_FUNC2(ezb_err_t, router_next_record, ezb_nwk_info_iterator_t *, ezb_nwk_route_record_info_t *);
#define ezb_nwk_get_next_route router_next_route
#define ezb_nwk_get_next_route_record router_next_record
#include "Communication/Zigbee/ZigbeeConsoleCommands.c"

static unsigned saves;
static unsigned resetCalls;
static int32_t resetResult;
static esp_err_t save_result;
static esp_err_t capture_fields(IConfigurationManager *self, const char *block,
    const ConfigurationFieldUpdate *fields, size_t count, bool persist) {
    (void)self; ++saves;
    TEST_ASSERT_EQUAL_STRING("zigbee_reporting", block);
    TEST_ASSERT_EQUAL(1, count); TEST_ASSERT_TRUE(persist);
    if (save_result == ESP_OK) memcpy((uint8_t *)&mock_settings.zigbee_reporting + fields[0].offset, fields[0].data, fields[0].size);
    return save_result;
}
static esp_err_t snapshot(IConfigurationManager *self, SystemSettings *out) {
    (void)self; *out = mock_settings; return ESP_OK;
}
void setUp(void) {
    ZIGBEE_COMMON_FAKES(RESET_FAKE);
    RESET_FAKE(router_next_route);
    RESET_FAKE(router_next_record);
    ConfigurationManager_GetInstance_fake.return_val = &mock_config_inst;
    mock_config_inst.UpdateFields = capture_fields;
    mock_config_inst.GetSnapshot = snapshot;
    memset(&mock_settings, 0, sizeof(mock_settings));
    saves = 0; save_result = ESP_OK;
    resetCalls = 0; resetResult = 0;
    Zigbee_ConsoleSdkStopped();
}
void tearDown(void) {}

static ServiceState lifecycleState;
static int32_t lifecycleStopResult, lifecycleStartResult;
static unsigned lifecycleStops, lifecycleStarts;
static ServiceState lifecycle_state(IService *self) { (void)self; return lifecycleState; }
static int32_t lifecycle_stop(IService *self) {
    (void)self; ++lifecycleStops;
    if (!lifecycleStopResult) lifecycleState = SERVICE_STATE_STOPPED;
    return lifecycleStopResult;
}
static int32_t lifecycle_start(IService *self) {
    (void)self; ++lifecycleStarts;
    if (!lifecycleStartResult) lifecycleState = SERVICE_STATE_STARTED;
    return lifecycleStartResult;
}
void test_ZigbeeRestartCLIRequiresAcknowledgedStopBeforeStart(void) {
    IZigbee radio = {.base={.GetState=lifecycle_state,.Stop=lifecycle_stop,.Start=lifecycle_start}};
    ServiceLocator_GetService_fake.return_val = &radio.base;
    lifecycleState = SERVICE_STATE_STARTED;
    lifecycleStops = lifecycleStarts = 0;
    lifecycleStopResult = ZIGBEE_ERROR_TIMEOUT; lifecycleStartResult = 0;
    char *args[] = {"ZB","RESTART","extra"};
    TEST_ASSERT_NOT_EQUAL(0, cmd_zigbee(3,args));
    TEST_ASSERT_EQUAL(0, lifecycleStops);
    TEST_ASSERT_NOT_EQUAL(0, cmd_zigbee(2,args));
    TEST_ASSERT_EQUAL(0, lifecycleStarts);
    lifecycleStopResult = 0; lifecycleStartResult = ZIGBEE_ERROR_STACK_FAIL;
    TEST_ASSERT_NOT_EQUAL(0, cmd_zigbee(2,args));
    TEST_ASSERT_EQUAL(1, lifecycleStarts);
    TEST_ASSERT_NOT_EQUAL(0, cmd_zigbee(2,args));
    TEST_ASSERT_EQUAL(2, lifecycleStops);
    lifecycleState = SERVICE_STATE_STARTED; lifecycleStartResult = 0;
    TEST_ASSERT_EQUAL(0, cmd_zigbee(2,args));
    TEST_ASSERT_EQUAL(2, lifecycleStarts);
}
void test_ZigbeeReportCLI_AllSixControlsAcceptZeroAndReadback(void) {
    char *names[] = {"fan", "ambient", "cpu", "health", "memory", "uptime"};
    for (unsigned i = 0; i < 6; ++i) {
        char *args[] = {"ZB", "REPORT", names[i], "0"};
        TEST_ASSERT_EQUAL(0, cmd_zigbee(4, args));
        TEST_ASSERT_EQUAL(0, cmd_zigbee(3, args));
        args[3] = "3600";
        TEST_ASSERT_EQUAL(0, cmd_zigbee(4, args));
        uint16_t value = 0;
        TEST_ASSERT_EQUAL(ESP_OK, ZigbeeReporting_GetInterval(&mock_config_inst, (ZigbeeReportKind)i, &value));
        TEST_ASSERT_EQUAL(3600, value);
    }
    TEST_ASSERT_EQUAL(12, saves);
}
void test_ZigbeeReportCLI_RejectsInvalidNumbersBeforeSaving(void) {
    char *values[] = {"-1", "+1", "", "1.5", "1x", "3601", "999999999999999999999999999"};
    for (unsigned i = 0; i < sizeof(values)/sizeof(values[0]); ++i) {
        char *args[] = {"ZB", "REPORT", "fan", values[i]};
        TEST_ASSERT_NOT_EQUAL(0, cmd_zigbee(4, args));
    }
    char *unknown[] = {"ZB", "REPORT", "unknown", "0"};
    TEST_ASSERT_NOT_EQUAL(0, cmd_zigbee(4, unknown));
    TEST_ASSERT_EQUAL(0, saves);
}
void test_ZigbeeReportCLI_SaveFailureDoesNotReportSuccess(void) {
    save_result = ESP_FAIL;
    char *args[] = {"ZB", "REPORT", "fan", "5"};
    TEST_ASSERT_NOT_EQUAL(0, cmd_zigbee(4, args));
    TEST_ASSERT_EQUAL(0, mock_settings.zigbee_reporting.fan_s);
}
FAKE_VALUE_FUNC0(ServiceLocator *, GetServiceLocator);
FAKE_VALUE_FUNC1(bool, esp_zigbee_lock_acquire, TickType_t);
FAKE_VOID_FUNC0(esp_zigbee_lock_release);
FAKE_VALUE_FUNC0(IDashboard *, DashboardManager_GetModule);

static int32_t reset_radio(IZigbee *self) { (void)self; ++resetCalls; return resetResult; }
void test_ZigbeeResetCLIPropagatesFailureAndRejectsExtraArguments(void) {
    IZigbee radio = {.ResetToFactorySettings = reset_radio};
    ServiceLocator_GetService_fake.return_val = &radio.base;
    char *args[] = {"RESET", "extra"};
    TEST_ASSERT_NOT_EQUAL(0, cmd_zreset(2, args));
    TEST_ASSERT_EQUAL(0, resetCalls);
    TEST_ASSERT_NOT_EQUAL(0, cmd_zreset(1, args));
    TEST_ASSERT_EQUAL(0, resetCalls);
    args[1] = "CONFIRM";
    resetResult = ZIGBEE_ERROR_LOCK_FAIL;
    TEST_ASSERT_NOT_EQUAL(0, cmd_zreset(2, args));
    resetResult = 0;
    TEST_ASSERT_EQUAL(0, cmd_zreset(2, args));
    TEST_ASSERT_EQUAL(2, resetCalls);
}

void test_ZigbeeRouterRequestsRetainBoundedContextsUntilCompletionOrSdkShutdown(void) {
    ezb_nwk_neighbor_info_t neighbor = {.short_addr = 1};
    RouterRequest *first = ReserveRouterRequest(&neighbor);
    TEST_ASSERT_NOT_NULL(first);
    TEST_ASSERT_NULL(ReserveRouterRequest(&neighbor));
    for (unsigned i = 1; i < ZIGBEE_ROUTER_REQUEST_CAPACITY; ++i) {
        neighbor.short_addr = (uint16_t)(i + 1);
        TEST_ASSERT_NOT_NULL(ReserveRouterRequest(&neighbor));
    }
    neighbor.short_addr = 42;
    TEST_ASSERT_NULL(ReserveRouterRequest(&neighbor));
    RouterAddressResolved(NULL, first);
    TEST_ASSERT_FALSE(first->occupied);
    TEST_ASSERT_NOT_NULL(ReserveRouterRequest(&neighbor));
    Zigbee_ConsoleSdkStopped();
    for (unsigned i = 0; i < ZIGBEE_ROUTER_REQUEST_CAPACITY; ++i)
        TEST_ASSERT_FALSE(routerRequests[i].occupied);
    TEST_ASSERT_NOT_NULL(ReserveRouterRequest(&neighbor));
}

static int32_t reject_refresh(IZigbee *self) { (void)self; return -1; }
static unsigned inventory_neighbors;
static ezb_err_t inventory_neighbor(ezb_nwk_info_iterator_t *iterator, ezb_nwk_neighbor_info_t *output) {
    (void)iterator;
    if (inventory_neighbors >= 3) return EZB_ERR_NOT_FOUND;
    *output = (ezb_nwk_neighbor_info_t){.short_addr=(uint16_t)(inventory_neighbors + 1),
        .device_type=(uint8_t)inventory_neighbors};
    ++inventory_neighbors;
    return EZB_ERR_NONE;
}
void test_RouterInventoryIncludesAllRolesAndPreservesSdkErrors(void) {
    RouterInventory inventory = {0}; inventory_neighbors = 0;
    mocked_ezb_nwk_get_next_neighbor_fake.custom_fake = inventory_neighbor;
    router_next_route_fake.return_val = EZB_ERR_NOT_FOUND;
    router_next_record_fake.return_val = EZB_ERR_NOT_FOUND;
    ReadRouterInventory(&inventory);
    TEST_ASSERT_EQUAL(3, inventory.neighbor_count);
    TEST_ASSERT_EQUAL(0, inventory.neighbors[0].device_type);
    TEST_ASSERT_EQUAL(1, inventory.neighbors[1].device_type);
    TEST_ASSERT_EQUAL(2, inventory.neighbors[2].device_type);
    TEST_ASSERT_EQUAL(EZB_ERR_NOT_FOUND, inventory.neighbor_result);
    TEST_ASSERT_FALSE(inventory.neighbor_truncated);
    inventory = (RouterInventory){0};
    router_next_route_fake.return_val = EZB_ERR_INV_ARG;
    ReadRouterInventory(&inventory);
    TEST_ASSERT_EQUAL(EZB_ERR_INV_ARG, inventory.route_result);
    TEST_ASSERT_EQUAL(0, inventory.route_count);
}
void test_RouterInventoryBoundsEveryTableWithoutOverwriting(void) {
    RouterInventory inventory = {0};
    mocked_ezb_nwk_get_next_neighbor_fake.return_val = EZB_ERR_NONE;
    router_next_route_fake.return_val = EZB_ERR_NONE;
    router_next_record_fake.return_val = EZB_ERR_NONE;
    ReadRouterInventory(&inventory);
    TEST_ASSERT_EQUAL(ZIGBEE_ROUTER_NEIGHBOR_CAPACITY, inventory.neighbor_count);
    TEST_ASSERT_EQUAL(ZIGBEE_ROUTER_ROUTE_CAPACITY, inventory.route_count);
    TEST_ASSERT_EQUAL(ZIGBEE_ROUTER_RECORD_CAPACITY, inventory.record_count);
    TEST_ASSERT_TRUE(inventory.neighbor_truncated);
    TEST_ASSERT_TRUE(inventory.route_truncated);
    TEST_ASSERT_TRUE(inventory.record_truncated);
}
void test_ZigbeeReportCLI_SchedulingFailureReportsPartialResult(void) {
    IZigbee radio={.ReconfigureAllReporting=reject_refresh};
    ServiceLocator_GetService_fake.return_val=&radio.base;
    char *args[]={"ZB","REPORT","fan","5"};
    TEST_ASSERT_NOT_EQUAL(0,cmd_zigbee(4,args));
    TEST_ASSERT_EQUAL(1,saves);
    TEST_ASSERT_EQUAL(5,mock_settings.zigbee_reporting.fan_s);
}

static unsigned sync_status_calls;
static unsigned tx_calls;
static int32_t tx_result;
static ZigbeeTxStatus tx_status;
static int32_t cli_set_tx(IZigbee *self, ZigbeeTxProfile profile) {
    (void)self; ++tx_calls;
    if (!tx_result) tx_status = (ZigbeeTxStatus){.profile=profile,.dbm=(int8_t)(profile*10)};
    return tx_result;
}
static int32_t cli_get_tx(IZigbee *self, ZigbeeTxStatus *status) {
    (void)self; *status = tx_status; return ESP_OK;
}
void test_ZigbeeTxCLI_StrictModesAppliedReadbackAndFailure(void) {
    IZigbee radio = {.SetTxProfile=cli_set_tx,.GetTxStatus=cli_get_tx};
    ServiceLocator_GetService_fake.return_val = &radio.base;
    tx_calls = 0; tx_result = ESP_OK;
    const char *names[] = {"eco", "Normal", "BOOST"};
    for (unsigned i = 0; i < 3; ++i) {
        char *args[] = {"ZB", "TX", (char *)names[i]};
        TEST_ASSERT_EQUAL(0, cmd_zigbee(3,args));
        TEST_ASSERT_EQUAL(i, tx_status.profile);
    }
    char *bad[] = {"ZB", "TX", "20", "extra"};
    TEST_ASSERT_NOT_EQUAL(0, cmd_zigbee(3,bad));
    TEST_ASSERT_NOT_EQUAL(0, cmd_zigbee(4,bad));
    TEST_ASSERT_EQUAL(3, tx_calls);
    tx_result = ESP_FAIL;
    bad[2] = "Eco";
    TEST_ASSERT_NOT_EQUAL(0, cmd_zigbee(3,bad));
    TEST_ASSERT_EQUAL(ZIGBEE_TX_BOOST, tx_status.profile);
    TEST_ASSERT_EQUAL(0, cmd_zigbee(2,bad));
}
static int32_t read_sync_status(IZigbee *self, ZigbeeAttributeStatus *status) {
    (void)self; ++sync_status_calls;
    *status = (ZigbeeAttributeStatus){.pending=3,.applied=7,.failed=2,.lock_deferrals=9};
    return ZIGBEE_ERROR_OK;
}
void test_ZigbeeSyncCLI_ReadsApplicationCountersAndRejectsInvalidUsage(void) {
    IZigbee radio = {.GetAttributeStatus=read_sync_status};
    ServiceLocator_GetService_fake.return_val = &radio.base;
    sync_status_calls = 0;
    char *args[] = {"ZB","SYNC","extra"};
    TEST_ASSERT_EQUAL(0, cmd_zigbee(2,args));
    TEST_ASSERT_EQUAL(1, sync_status_calls);
    TEST_ASSERT_NOT_EQUAL(0, cmd_zigbee(3,args));
    TEST_ASSERT_EQUAL(1, sync_status_calls);
    radio.GetAttributeStatus = NULL;
    TEST_ASSERT_NOT_EQUAL(0, cmd_zigbee(2,args));
    TEST_ASSERT_EQUAL(0, saves);
}

static IZigbee *consoleRadio;
static IService *console_service(const char *name) { (void)name;return &consoleRadio->base; }
static int32_t console_network_failure(IZigbee *self,ZigbeeNetworkInfo *info) { (void)self;(void)info;return ZIGBEE_ERROR_INVALID_PARAM; }
void test_ZigbeeConsoleRejectsMalformedInputsAndMissingCallbacks(void) {
    unsigned before=saves;
    char *null_args[]={"ZB",NULL};
    TEST_ASSERT_NOT_EQUAL(0,cmd_zigbee(2,null_args));
    TEST_ASSERT_NOT_EQUAL(0,cmd_zigbee(-1,NULL));
    GetServiceLocator_fake.return_val=NULL;
    char *args[]={"ZB","INFO","--machine"};
    TEST_ASSERT_NOT_EQUAL(0,cmd_zigbee(3,args));
    IZigbee radio={0};consoleRadio=&radio;
    ServiceLocator locator={.GetService=console_service};
    GetServiceLocator_fake.return_val=&locator;
    TEST_ASSERT_NOT_EQUAL(0,cmd_zigbee(3,args));
    radio.GetNetworkInfo=console_network_failure;
    TEST_ASSERT_NOT_EQUAL(0,cmd_zigbee(3,args));
    args[1]="STEER";TEST_ASSERT_NOT_EQUAL(0,cmd_zigbee(3,args));
    args[1]="DIAG";TEST_ASSERT_NOT_EQUAL(0,cmd_zigbee(3,args));
    GetServiceLocator_fake.return_val=NULL;consoleRadio=NULL;
    TEST_ASSERT_EQUAL(before,saves);
}
