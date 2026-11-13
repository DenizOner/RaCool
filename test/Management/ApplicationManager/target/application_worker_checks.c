#include "unity.h"
#define ServiceLocator_GetService AppTarget_GetService
#define ServiceLocator_GetEventBus AppTarget_GetEventBus
#include "../../../../src/Management/ApplicationManager/ApplicationManager.c"
#undef ServiceLocator_GetService
#undef ServiceLocator_GetEventBus

static SemaphoreHandle_t lookupRelease;
static atomic_bool lookupEntered;
static bool initialized;
static bool rotationRemovalBusy;
static unsigned rotationRegistrations, rotationRemovals;
static esp_err_t register_rotation(IRotaryEncoder *self, RotaryEncoderRotationCallback callback, void *ctx) {
    (void)self; (void)ctx;
    TEST_ASSERT_EQUAL_PTR(App_OnRotationDirect, callback);
    rotationRegistrations++;
    return ESP_OK;
}
static esp_err_t remove_rotation(IRotaryEncoder *self, RotaryEncoderRotationCallback callback) {
    (void)self;
    TEST_ASSERT_EQUAL_PTR(App_OnRotationDirect, callback);
    rotationRemovals++;
    return rotationRemovalBusy ? ESP_ERR_INVALID_STATE : ESP_OK;
}
static IRotaryEncoder ownedEncoder = {.OnRotation = register_rotation, .UnsubscribeRotation = remove_rotation};
static bool coordinatorMode;
static atomic_bool releaseNetwork;
static _Atomic(TaskHandle_t) resetGuardOwner;
static atomic_uint resetSettingsCalls, resetNetworkCalls, resetRestartCalls;
static ServiceState reset_ready(IService *self) { (void)self; return SERVICE_STATE_STARTED; }
static ServiceState reset_watchdog_not_ready(IService *self) { (void)self; return SERVICE_STATE_INITIALIZED; }
static esp_err_t reset_guard(IConfigurationManager *self, bool active) {
    (void)self;
    TaskHandle_t caller = xTaskGetCurrentTaskHandle();
    TaskHandle_t owner = atomic_load(&resetGuardOwner);
    if (owner && owner != caller) return ESP_ERR_NOT_FINISHED;
    atomic_store(&resetGuardOwner, active ? caller : NULL);
    return ESP_OK;
}
static esp_err_t reset_defaults(IConfigurationManager *self, const char *block) {
    (void)self;
    if (block || atomic_load(&resetGuardOwner) != xTaskGetCurrentTaskHandle()) return ESP_ERR_INVALID_STATE;
    atomic_fetch_add(&resetSettingsCalls, 1);
    return ESP_OK;
}
static esp_err_t reset_verify(IConfigurationManager *self, SystemSettings *out) {
    (void)self;
    if (atomic_load(&resetGuardOwner) != xTaskGetCurrentTaskHandle() || !atomic_load(&resetSettingsCalls))
        return ESP_ERR_INVALID_STATE;
    memset(out, 0, sizeof(*out));
    out->system.power_profile = POWER_MODE_BALANCED;
    out->system.zigbee_tx_profile = ZIGBEE_TX_NORMAL;
    return ESP_OK;
}
static int32_t reset_cpu(IPowerManager *self, PowerMode_t mode) {
    (void)self; return mode == POWER_MODE_BALANCED ? POWER_OK : POWER_ERR_INVALID_ARG;
}
static PowerMode_t reset_mode(IPowerManager *self) { (void)self; return POWER_MODE_BALANCED; }
static int32_t reset_selftest(IPowerManager *self) { (void)self; return POWER_OK; }
static bool reset_power_pending(IPowerManager *self) { (void)self; return false; }
static int32_t reset_restart(IPowerManager *self, uint32_t delay) {
    (void)self; (void)delay; atomic_fetch_add(&resetRestartCalls, 1); return POWER_ERR_BUSY;
}
static int32_t reset_tx(IZigbee *self, ZigbeeTxProfile profile) {
    (void)self; return profile == ZIGBEE_TX_NORMAL ? ESP_OK : ESP_ERR_INVALID_ARG;
}
static int32_t reset_tx_status(IZigbee *self, ZigbeeTxStatus *out) {
    (void)self; *out = (ZigbeeTxStatus){.profile = ZIGBEE_TX_NORMAL, .dbm = 10}; return ESP_OK;
}
static bool reset_pairing(IZigbee *self) { (void)self; return false; }
static int32_t reset_begin_network(IZigbee *self, uint32_t *receipt) {
    (void)self; *receipt = 7; atomic_fetch_add(&resetNetworkCalls, 1); return ESP_OK;
}
static int32_t reset_network_status(IZigbee *self, ZigbeeFactoryResetStatus *out) {
    (void)self;
    bool requested = atomic_load(&resetNetworkCalls) != 0;
    bool completed = requested && atomic_load(&releaseNetwork);
    *out = (ZigbeeFactoryResetStatus){.operation_id = requested ? 7 : 0,
        .pending = requested && !completed, .connected = !completed, .factory_new = completed};
    return ESP_OK;
}
static IConfigurationManager resetConfig = {.ResetToDefaults = reset_defaults,
    .VerifyDefaults = reset_verify, .SetFactoryResetGuard = reset_guard};
static IPowerManager resetPower = {.SetPowerMode = reset_cpu, .GetPowerMode = reset_mode,
    .SelfTest = reset_selftest, .RestartAsync = reset_restart, .IsTransitionPending = reset_power_pending};
static IZigbee resetRadio = {.SetTxProfile = reset_tx, .GetTxStatus = reset_tx_status,
    .BeginFactoryReset = reset_begin_network, .GetFactoryResetStatus = reset_network_status, .IsPairing = reset_pairing};
static IMenuManager resetMenu = {.base = {.GetState = reset_ready}};
static IWatchdog resetWatchdog = {.base = {.GetState = reset_watchdog_not_ready}};
static IRelay resetRelay;
static IClimateController resetClimate;
static IUIController resetUi;
static IDiagnosticsManager resetDiagnostics;
IService *AppTarget_GetService(const char *name) {
    if (coordinatorMode) {
        if (!strcmp(name, SVC_NAME_CONFIGURATION_MANAGER)) return &resetConfig.base;
        if (!strcmp(name, SVC_NAME_POWER_MANAGER)) return &resetPower.base;
        if (!strcmp(name, SVC_NAME_ZIGBEE)) return &resetRadio.base;
        if (!strcmp(name, SVC_NAME_ROTARY_ENCODER)) return &ownedEncoder.base;
        if (!strcmp(name, SVC_NAME_MENU_MANAGER)) return &resetMenu.base;
        if (!strcmp(name, SVC_NAME_WATCHDOG)) return &resetWatchdog.base;
        if (!strcmp(name, SVC_NAME_RELAY)) return &resetRelay.base;
        if (!strcmp(name, SVC_NAME_CLIMATE_CONTROLLER)) return &resetClimate.base;
        if (!strcmp(name, SVC_NAME_UI_CONTROLLER)) return &resetUi.base;
        if (!strcmp(name, SVC_NAME_DIAGNOSTICS_MANAGER)) return &resetDiagnostics.base;
        return NULL;
    }
    if (strcmp(name, SVC_NAME_ROTARY_ENCODER) == 0) {
        atomic_store(&lookupEntered, true);
        xSemaphoreTake(lookupRelease, portMAX_DELAY);
        xSemaphoreGive(lookupRelease);
    }
    return NULL;
}
IEventBus *AppTarget_GetEventBus(void) { return NULL; }
void target_application_worker_cleanup(void) {
    atomic_store(&releaseNetwork, true);
    rotationRemovalBusy = false;
    if (lookupRelease) xSemaphoreGive(lookupRelease);
    if (initialized) {
        for (unsigned i = 0; i < 100 && atomic_load(&appManager.factoryResetBusy); ++i)
            vTaskDelay(pdMS_TO_TICKS(10));
        TEST_ASSERT_EQUAL(0, App_Stop(&appManager.base.base));
        initialized = false;
    }
    if (lookupRelease) { vSemaphoreDelete(lookupRelease); lookupRelease = NULL; }
    coordinatorMode = false;
}

void target_application_factory_reset_verify(void) {
    coordinatorMode = true;
    atomic_store(&releaseNetwork, false);
    atomic_store(&resetGuardOwner, NULL);
    atomic_store(&resetSettingsCalls, 0); atomic_store(&resetNetworkCalls, 0); atomic_store(&resetRestartCalls, 0);
    IApplicationManager *app = ApplicationManager_GetModule();
    TEST_ASSERT_EQUAL(0, app->base.Start(&app->base)); initialized = true;
    TEST_ASSERT_EQUAL(ESP_OK, app->TriggerFactoryReset(app));
    AppManagerStatus status = {0};
    for (unsigned i = 0; i < 300; ++i) {
        TEST_ASSERT_EQUAL(ESP_OK, app->GetStatus(app, &status));
        if (status.factoryResetPhase == FACTORY_RESET_WAIT_NETWORK) break;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    TEST_ASSERT_EQUAL(FACTORY_RESET_WAIT_NETWORK, status.factoryResetPhase);
    TEST_ASSERT_TRUE(status.factoryResetBusy);
    TEST_ASSERT_EQUAL(1, atomic_load(&resetSettingsCalls));
    TEST_ASSERT_EQUAL(1, atomic_load(&resetNetworkCalls));
    TEST_ASSERT_EQUAL(0, atomic_load(&resetRestartCalls));
    TEST_ASSERT_EQUAL_PTR(atomic_load(&appManager.worker.task), atomic_load(&resetGuardOwner));
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FINISHED, app->TriggerFactoryReset(app));
    TEST_ASSERT_NOT_EQUAL(0, app->base.Stop(&app->base));
    TEST_ASSERT_EQUAL(SERVICE_STATE_STARTED, app->base.GetState(&app->base));
    atomic_store(&releaseNetwork, true);
    for (unsigned i = 0; i < 300; ++i) {
        TEST_ASSERT_EQUAL(ESP_OK, app->GetStatus(app, &status));
        if (!status.factoryResetBusy) break;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    TEST_ASSERT_FALSE(status.factoryResetBusy);
    TEST_ASSERT_EQUAL(FACTORY_RESET_FAILED, status.factoryResetPhase);
    TEST_ASSERT_EQUAL(POWER_ERR_BUSY, status.factoryResetError);
    TEST_ASSERT_EQUAL(1, atomic_load(&resetRestartCalls));
    TEST_ASSERT_NULL(atomic_load(&resetGuardOwner));
    TEST_ASSERT_EQUAL(0, app->base.Stop(&app->base)); initialized = false;
    coordinatorMode = false;
}
void target_application_worker_verify_shutdown(void) {
    IApplicationManager *app = ApplicationManager_GetModule();
    TEST_ASSERT_EQUAL(0, app->base.Initialize(&app->base, NULL));
    lookupRelease = xSemaphoreCreateBinary();
    TEST_ASSERT_NOT_NULL(lookupRelease);
    TEST_ASSERT_EQUAL(0, app->base.Start(&app->base));
    initialized = true;
    for (unsigned i = 0; i < 100 && !atomic_load(&lookupEntered); ++i) vTaskDelay(pdMS_TO_TICKS(10));
    TEST_ASSERT_TRUE(atomic_load(&lookupEntered));
    /* The injected boundary retains a captured callback while the real worker
     * is blocked. Removal failure must preserve ownership after worker join. */
    appManager.encoder = &ownedEncoder;
    rotationRegistrations = rotationRemovals = 0;
    rotationRemovalBusy = true;
    TEST_ASSERT_EQUAL(ESP_OK, App_BindEncoderRotation());
    TaskHandle_t task = atomic_load(&appManager.worker.task);
    TEST_ASSERT_NOT_EQUAL(0, app->base.Stop(&app->base));
    TEST_ASSERT_EQUAL_PTR(task, atomic_load(&appManager.worker.task));
    AppManagerEvent event = {.id = APP_EVENT_ROTATION};
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, app->PostEvent(app, &event));
    TEST_ASSERT_NOT_EQUAL(0, app->base.Start(&app->base));
    xSemaphoreGive(lookupRelease);
    TEST_ASSERT_NOT_EQUAL(0, app->base.Stop(&app->base));
    TEST_ASSERT_NULL(atomic_load(&appManager.worker.task));
    TEST_ASSERT_EQUAL_PTR(&ownedEncoder, appManager.ownedEncoder);
    TEST_ASSERT_NOT_EQUAL(0, app->base.Start(&app->base));
    TEST_ASSERT_NOT_EQUAL(0, app->base.Initialize(&app->base, NULL));
    TEST_ASSERT_EQUAL(1, rotationRegistrations);
    TEST_ASSERT_EQUAL(1, rotationRemovals);
    rotationRemovalBusy = false;
    TEST_ASSERT_EQUAL(0, app->base.Stop(&app->base));
    TEST_ASSERT_NULL(appManager.ownedEncoder);
    TEST_ASSERT_EQUAL(2, rotationRemovals);
    initialized = false;
    TEST_ASSERT_NULL(atomic_load(&appManager.worker.task));
    TEST_ASSERT_EQUAL(0, app->base.Start(&app->base));
    initialized = true;
    TEST_ASSERT_EQUAL(0, app->base.Stop(&app->base));
    initialized = false;
}
