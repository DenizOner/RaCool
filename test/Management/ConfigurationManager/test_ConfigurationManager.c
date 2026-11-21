#include "Communication/DebugConsole/IDebugConsole.h"
#include "Controllers/ClimateController/ClimateConsoleCommands.h"
#include "Controllers/ClimateController/IClimateController.h"
#include "Hardware/LEDAnimations/LEDAnimationCatalog.h"
#include "Core/EventBus/IEventBus.h"
#include "Management/ConfigurationManager/ZigbeeReportingSettings.h"
#include <math.h>
#include <setjmp.h>
/**
 * @file test_ConfigurationManager.c
 * @brief Native unit test suite for the ConfigurationManager service.
 *
 * Tests verify the public IConfigurationManager vtable behaviour under
 * controlled FFF-mock conditions.  Because ConfigurationManager is a
 * singleton (static initialisation flag), we call GetInstance() once in
 * the first test and reuse the same pointer, using FFF mocks to control
 * the return values of RTOS and NVS calls.
 *
 * Mock environment (from test_mocks.h / test_mocks.c):
 *   - xSemaphoreCreateMutex â†’ returns (SemaphoreHandle_t)1 so mutex creation
 *     succeeds.
 *   - xSemaphoreTake / xSemaphoreGive â†’ pdTRUE so locking always succeeds.
 *   - nvs_open â†’ ESP_ERR_NVS_NOT_FOUND so first-boot defaults are preserved.
 *   - nvs_set_blob / nvs_commit â†’ ESP_OK for save tests.
 *   - nvs_get_blob â†’ ESP_OK for explicit load tests.
 *   - xTaskCreate â†’ pdPASS for async save tests.
 *   - GetEventBus â†’ NULL so publish is a safe no-op.
 *
 * @author RaCool Team
 * @date   2026
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "unity.h"
#include "Management/ConfigurationManager/IConfigurationManager.h"
#include "Management/ConfigurationManager/SystemConfigs.h"
#include "Core/Localization/Localization.h"

/*
 * test_mocks.h (injected via -include compiler flag) redirects
 * ConfigurationManager_GetInstance â†’ mocked_ConfigurationManager_GetInstance.
 * This test suite IS the ConfigurationManager implementation under test, so
 * we restore the real symbol here before any call site is compiled.
 */
#undef ConfigurationManager_GetInstance

/* Re-declare the real function now that the macro override has been removed. */
extern IConfigurationManager *ConfigurationManager_GetInstance(void);

/* =========================================================================
 * Module-level singleton pointer (retrieved once)
 * ========================================================================= */

/** @brief Shared manager pointer â€“ initialised in the first test. */
static IConfigurationManager *s_mgr = NULL;
#include <stdarg.h>
static int captured_config_diff=-1;
static unsigned captured_config_fields=0;
static bool captured_builtin_startup=false, captured_locale=false;
static int config_test_printf(const char *format,...) {
    va_list arguments;va_start(arguments,format);
    char message[512];int result=vsnprintf(message,sizeof(message),format,arguments);
    va_end(arguments);
    int count;
    if (sscanf(message,"RACOOL_CLI v=1 type=config_diff fields=%d",&count)==1) captured_config_diff=count;
    if (strncmp(message,"RACOOL_CLI v=1 type=config_field key=",sizeof("RACOOL_CLI v=1 type=config_field key=")-1)==0) ++captured_config_fields;
    if (strstr(message,"key=hardware.builtin_led_startup_anim value=")) captured_builtin_startup=true;
    if (strstr(message,"key=localization.locale value=")) captured_locale=true;
    fputs(message,stdout);
    return result;
}
#define printf config_test_printf
#include "Management/ConfigurationManager/ConfigurationManagerCommands.c"
#undef printf
#undef TAG


/* =========================================================================
 * setUp / tearDown
 * ========================================================================= */

static BaseType_t xTaskCreate_writes_handle(TaskFunction_t, const char *, uint32_t, void *, uint32_t, TaskHandle_t *);
static BaseType_t acknowledge_stop(QueueHandle_t queue, void *out, TickType_t wait) {
    (void)queue; (void)wait; *(int32_t *)out = 0; return pdTRUE;
}
static QueueHandle_t config_queue_create(uint32_t length, uint32_t size) {
    (void)size; return (QueueHandle_t)(uintptr_t)(length == 1 ? 3 : 2);
}

void setUp(void) {
    /* Mutex must succeed so Initialize() and GetInstance() do not fail. */
    mocked_xSemaphoreCreateMutex_fake.return_val = (SemaphoreHandle_t)1;
    mocked_xSemaphoreTake_fake.return_val        = pdTRUE;
    mocked_xSemaphoreGive_fake.return_val        = pdTRUE;

    RESET_FAKE(mocked_nvs_open);
    RESET_FAKE(mocked_nvs_set_blob);
    RESET_FAKE(mocked_nvs_commit);
    RESET_FAKE(mocked_nvs_get_blob);
    /* NVS: open returns NOT_FOUND so first-boot defaults are preserved. */
    mocked_nvs_open_fake.return_val     = ESP_ERR_NVS_NOT_FOUND;
    mocked_nvs_set_blob_fake.return_val = ESP_OK;
    mocked_nvs_commit_fake.return_val   = ESP_OK;
    mocked_nvs_get_blob_fake.return_val = ESP_OK;

    mocked_xQueueCreate_fake.custom_fake = config_queue_create;
    mocked_xQueueSend_fake.return_val = pdTRUE;
    mocked_xQueueSend_fake.custom_fake = NULL;
    mocked_xQueueReceive_fake.custom_fake = acknowledge_stop;
    mocked_xTaskCreate_fake.custom_fake = xTaskCreate_writes_handle;
    /* Worker task creation succeeds by default. */
    mocked_xTaskCreate_fake.return_val = pdPASS;

    /* EventBus is unavailable in native tests â€“ publish is a safe no-op. */
    mocked_GetEventBus_fake.return_val = NULL;
}

void tearDown(void) {}

/* =========================================================================
 * Internal helpers
 * ========================================================================= */

/**
 * @brief Returns (and lazily initialises) the module-level manager pointer.
 *
 * Calling ConfigurationManager_GetInstance() triggers nvs_open which is
 * already mocked.  Subsequent calls to this helper return the cached pointer.
 *
 * @return Non-NULL pointer to the IConfigurationManager singleton.
 */
static IConfigurationManager *mgr(void) {
    if (!s_mgr) {
        s_mgr = ConfigurationManager_GetInstance();
    }
    return s_mgr;
}

/* =========================================================================
 * Test 1: Singleton
 * ========================================================================= */

/**
 * @brief GetInstance() must return a non-NULL pointer on every call.
 */
void test_ConfigMgr_GetInstance_Singleton(void) {
    IConfigurationManager *a = ConfigurationManager_GetInstance();
    IConfigurationManager *b = ConfigurationManager_GetInstance();

    TEST_ASSERT_NOT_NULL(a);
    TEST_ASSERT_EQUAL_PTR(a, b);
    s_mgr = a; /* Cache for the rest of the suite. */
}

/* =========================================================================
 * Test 2: Initial state
 * ========================================================================= */

/**
 * @brief After GetInstance() the service must report a valid state.
 */
void test_ConfigMgr_InitialState(void) {
    ServiceState st = mgr()->base.GetState((IService *)mgr());
    TEST_ASSERT_TRUE(st >= SERVICE_STATE_INITIALIZED);
}

/* =========================================================================
 * Test 3: Initialize and Start lifecycle
 * ========================================================================= */

/**
 * @brief Initialize() must succeed; Start() must advance state to STARTED.
 */
static QueueHandle_t fail_stop_queue_create(uint32_t length, uint32_t size) {
    return length == 1 ? NULL : config_queue_create(length, size);
}
void test_ConfigMgr_ControlAllocationFailureCanRetry(void) {
    IService *service = &mgr()->base;
    mocked_xQueueCreate_fake.custom_fake = NULL;
    mocked_xQueueCreate_fake.return_val = NULL;
    TEST_ASSERT_NOT_EQUAL(0, service->Start(service));
    TEST_ASSERT_EQUAL(SERVICE_STATE_STOPPED, service->GetState(service));
    mocked_xQueueCreate_fake.custom_fake = config_queue_create;
    mocked_xSemaphoreCreateMutex_fake.return_val = NULL;
    TEST_ASSERT_NOT_EQUAL(0, service->Start(service));
    mocked_xSemaphoreCreateMutex_fake.return_val = (SemaphoreHandle_t)1;
    mocked_xQueueCreate_fake.custom_fake = fail_stop_queue_create;
    TEST_ASSERT_NOT_EQUAL(0, service->Start(service));
    mocked_xQueueCreate_fake.custom_fake = config_queue_create;
    TEST_ASSERT_EQUAL(0, service->Start(service));
    TEST_ASSERT_EQUAL(0, service->Stop(service));
}

void test_ConfigMgr_Initialize_And_Start(void) {
    int32_t init_res = mgr()->base.Initialize((IService *)mgr(), NULL);
    TEST_ASSERT_EQUAL_INT32(0, init_res);

    int32_t start_res = mgr()->base.Start((IService *)mgr());
    TEST_ASSERT_EQUAL_INT32(0, start_res);
    TEST_ASSERT_EQUAL(SERVICE_STATE_STARTED, mgr()->base.GetState((IService *)mgr()));
}

/* =========================================================================
 * Test 4: CheckHealth when healthy
 * ========================================================================= */

/**
 * @brief CheckHealth() must return 0 when the internal mutex is valid.
 */
void test_ConfigMgr_CheckHealth_Healthy(void) {
    int32_t health = mgr()->base.CheckHealth((IService *)mgr());
    TEST_ASSERT_EQUAL_INT32(0, health);
}

/* =========================================================================
 * Test 5: GetSettings not NULL
 * ========================================================================= */

/**
 * @brief GetSettings() must return a non-NULL pointer to SystemSettings.
 */
void test_ConfigMgr_GetSettings_NotNull(void) {
    SystemSettings *s = mgr()->GetSettings(mgr());
    TEST_ASSERT_NOT_NULL(s);
}

/* =========================================================================
 * Test 6: Default values are sane (NVS returns NOT_FOUND â†’ defaults kept)
 * ========================================================================= */

/**
 * @brief When NVS is absent the settings must reflect compile-time defaults.
 */
void test_ConfigMgr_DefaultValues_Sane(void) {
    SystemSettings *s = mgr()->GetSettings(mgr());
    TEST_ASSERT_NOT_NULL(s);

    TEST_ASSERT_EQUAL(DEFAULT_SYSTEM_ENABLED, s->system.system_enabled);
    TEST_ASSERT_EQUAL_UINT8(1, s->system.power_profile);
    TEST_ASSERT_EQUAL_UINT8(1, s->system.zigbee_tx_profile);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, s->calibration.ir_object_cal);
    TEST_ASSERT_EQUAL(255, s->hardware.builtin_led_brightness);
    TEST_ASSERT_EQUAL(RAINBOW_FLOW, s->hardware.builtin_led_startup_anim);
    TEST_ASSERT_EQUAL(255, s->hardware.ext_led_brightness);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 4.0f, s->hardware.ext_led_temp_freeze);
    TEST_ASSERT_EQUAL_UINT32(DEFAULT_TELEMETRY_INTERVAL_MS,
                             s->system.telemetry_interval_ms);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, DEFAULT_THRESHOLD_HIGH,
                             s->control.threshold_high);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, DEFAULT_RELAY_OPEN_TEMP,
                             s->control.relay_open_temp);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, DEFAULT_IR_OBJECT_CAL,
                             s->calibration.ir_object_cal);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, DEFAULT_IR_AMBIENT_CAL,
                             s->calibration.ir_ambient_cal);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, DEFAULT_CPU_TEMP_WARNING,
                             s->thermal.cpu_temp_warning);
}

/* =========================================================================
 * Test 7: Save all blocks succeeds
 * ========================================================================= */

/**
 * @brief Save(NULL) must succeed when NVS operations all return ESP_OK.
 */
void test_ConfigMgr_Save_AllBlocks_Success(void) {
    mocked_nvs_open_fake.return_val = ESP_OK; /* NVS present */
    uint32_t before = mocked_nvs_open_fake.call_count;

    esp_err_t err = mgr()->Save(mgr(), NULL);

    TEST_ASSERT_EQUAL(ESP_OK, err);
    /* Every persisted block opens its namespace. */
    TEST_ASSERT_GREATER_OR_EQUAL(before + CONFIG_STATUS_BLOCK_CAPACITY, mocked_nvs_open_fake.call_count);
}

/* =========================================================================
 * Test 8: Save single block
 * ========================================================================= */

/**
 * @brief Save("system") must open exactly one NVS namespace.
 */
void test_ConfigMgr_Save_SingleBlock(void) {
    mocked_nvs_open_fake.return_val = ESP_OK;
    uint32_t before = mocked_nvs_open_fake.call_count;

    esp_err_t err = mgr()->Save(mgr(), "system");

    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_EQUAL_UINT32(before + 1u, mocked_nvs_open_fake.call_count);
}

/* =========================================================================
 * Test 9: Save with unknown block name
 * ========================================================================= */

/**
 * @brief Save() with an unrecognised block name must return
 *        ESP_ERR_INVALID_ARG without opening any NVS namespace.
 */
void test_ConfigMgr_Save_UnknownBlock(void) {
    mocked_nvs_open_fake.return_val = ESP_OK;
    uint32_t before = mocked_nvs_open_fake.call_count;

    esp_err_t err = mgr()->Save(mgr(), "totally_unknown");

    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, err);
    TEST_ASSERT_EQUAL_UINT32(before, mocked_nvs_open_fake.call_count);
}

/* =========================================================================
 * Test 10: Load preserves defaults when NVS not found
 * ========================================================================= */

/**
 * @brief When all NVS reads return NOT_FOUND, Load() must return ESP_OK and
 *        leave the in-memory defaults unchanged.
 */
void test_ConfigMgr_Load_PreservesDefaultsOnMiss(void) {
    /* nvs_open returns NOT_FOUND â†’ treated as first-boot, not an error */
    mocked_nvs_open_fake.return_val = ESP_ERR_NVS_NOT_FOUND;

    esp_err_t err = mgr()->Load(mgr(), NULL);
    TEST_ASSERT_EQUAL(ESP_OK, err);

    SystemSettings *s = mgr()->GetSettings(mgr());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, DEFAULT_THRESHOLD_HIGH,
                             s->control.threshold_high);
}

/* =========================================================================
 * Test 11: UpdateSettings â€“ valid size
 * ========================================================================= */

/**
 * @brief UpdateSettings() with the correct size must update the live struct.
 */
void test_ConfigMgr_UpdateSettings_ValidSize(void) {
    SystemSettings new_s = *mgr()->GetSettings(mgr());
    new_s.control.threshold_high = 99.0f;

    esp_err_t err = mgr()->UpdateSettings(mgr(), &new_s, sizeof(SystemSettings));
    TEST_ASSERT_EQUAL(ESP_OK, err);

    SystemSettings *live = mgr()->GetSettings(mgr());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 99.0f, live->control.threshold_high);

    /* Restore defaults for subsequent tests. */
    new_s.control.threshold_high = DEFAULT_THRESHOLD_HIGH;
    mgr()->UpdateSettings(mgr(), &new_s, sizeof(SystemSettings));
}

/* =========================================================================
 * Test 12: UpdateSettings â€“ wrong size
 * ========================================================================= */

/**
 * @brief UpdateSettings() with an incorrect size must return
 *        ESP_ERR_INVALID_SIZE.
 */
void test_ConfigMgr_UpdateSettings_InvalidSize(void) {
    SystemSettings new_s = *mgr()->GetSettings(mgr());
    esp_err_t err = mgr()->UpdateSettings(mgr(), &new_s,
                                          sizeof(SystemSettings) - 1);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_SIZE, err);
}

/* =========================================================================
 * Test 13: UpdateSettings â€“ NULL pointer
 * ========================================================================= */

/**
 * @brief UpdateSettings() with a NULL data pointer must return
 *        ESP_ERR_INVALID_ARG.
 */
void test_ConfigMgr_UpdateSettings_NullPtr(void) {
    esp_err_t err = mgr()->UpdateSettings(mgr(), NULL, sizeof(SystemSettings));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, err);
}

/* =========================================================================
 * Test 14: IsRestartRequired default is false
 * ========================================================================= */

/**
 * @brief IsRestartRequired() must return false immediately after
 *        first-boot initialisation.
 */
void test_ConfigMgr_IsRestartRequired_DefaultFalse(void) {
    /* Ensure restart_required is false (it is set by ResetToDefaults or init) */
    SystemSettings s = *mgr()->GetSettings(mgr());
    s.restart_required = false;
    mgr()->UpdateSettings(mgr(), &s, sizeof(SystemSettings));

    TEST_ASSERT_FALSE(mgr()->IsRestartRequired(mgr()));
}

/* =========================================================================
 * Test 15: ValidateSettings â€“ compile-time defaults pass
 * ========================================================================= */

/**
 * @brief ValidateSettings() must return ESP_OK for the compile-time defaults.
 */
void test_ConfigMgr_ValidateSettings_DefaultsPass(void) {
    /* Restore known-good settings before validating. */
    SystemSettings s = *mgr()->GetSettings(mgr());
    s.control.threshold_high    = DEFAULT_THRESHOLD_HIGH;
    s.control.threshold_low     = DEFAULT_THRESHOLD_LOW;
    s.control.relay_open_temp   = DEFAULT_RELAY_OPEN_TEMP;
    s.control.relay_close_temp  = DEFAULT_RELAY_CLOSE_TEMP;
    s.calibration.ir_object_cal  = DEFAULT_IR_OBJECT_CAL;
    s.calibration.ir_ambient_cal = DEFAULT_IR_AMBIENT_CAL;
    s.thermal.cpu_temp_warning   = DEFAULT_CPU_TEMP_WARNING;
    s.thermal.cpu_temp_critical  = DEFAULT_CPU_TEMP_CRITICAL;
    s.thermal.cpu_temp_emergency = DEFAULT_CPU_TEMP_EMERGENCY;
    mgr()->UpdateSettings(mgr(), &s, sizeof(SystemSettings));

    esp_err_t err = mgr()->ValidateSettings(mgr());
    TEST_ASSERT_EQUAL(ESP_OK, err);
}

/* =========================================================================
 * Test 16: ValidateSettings â€“ inverted relay temps
 * ========================================================================= */

/**
 * @brief ValidateSettings() must return ESP_ERR_INVALID_ARG when
 *        relay_open_temp <= relay_close_temp (no hysteresis).
 */
void test_ConfigMgr_ValidateSettings_InvalidRelayTemps(void) {
    SystemSettings s = *mgr()->GetSettings(mgr());
    s.control.relay_open_temp  = 20.0f;
    s.control.relay_close_temp = 25.0f; /* close > open â†’ invalid */
    esp_err_t err = mgr()->UpdateSettings(mgr(), &s, sizeof(SystemSettings));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, err);
    TEST_ASSERT_EQUAL(ESP_OK, mgr()->ValidateSettings(mgr()));

    /* Restore defaults. */
    s.control.relay_open_temp  = DEFAULT_RELAY_OPEN_TEMP;
    s.control.relay_close_temp = DEFAULT_RELAY_CLOSE_TEMP;
    mgr()->UpdateSettings(mgr(), &s, sizeof(SystemSettings));
}

/* =========================================================================
 * Test 17: ResetToDefaults â€“ named block
 * ========================================================================= */

/**
 * @brief ResetToDefaults("control") must restore the control block to
 *        compile-time defaults and persist to NVS.
 */
void test_ConfigMgr_ResetToDefaults_SingleBlock(void) {
    mocked_nvs_open_fake.return_val = ESP_OK;

    /* Mutate the control block. */
    SystemSettings s = *mgr()->GetSettings(mgr());
    s.control.threshold_high = 99.0f; /* Valid, non-default setting. */
    mgr()->UpdateSettings(mgr(), &s, sizeof(SystemSettings));

    esp_err_t err = mgr()->ResetToDefaults(mgr(), "control");
    TEST_ASSERT_EQUAL(ESP_OK, err);

    SystemSettings *live = mgr()->GetSettings(mgr());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, DEFAULT_THRESHOLD_HIGH,
                             live->control.threshold_high);
}

/* =========================================================================
 * Test 18: GetVersion â€“ all known blocks
 * ========================================================================= */

/**
 * @brief GetVersion() must return the correct schema version for each block.
 *
 * Default versions: system=1, control=1, hardware=2, calibration=1,
 *                   thermal=1, zigbee_reporting=1.
 * Unknown block must return 0.
 */
void test_ConfigMgr_GetVersion_KnownBlocks(void) {
    TEST_ASSERT_EQUAL_UINT32(SYSTEM_CONFIG_SCHEMA_VERSION, mgr()->GetVersion(mgr(), "system"));
    TEST_ASSERT_EQUAL_UINT32(2u, mgr()->GetVersion(mgr(), "control"));
    TEST_ASSERT_EQUAL_UINT32(HARDWARE_CONFIG_SCHEMA_VERSION, mgr()->GetVersion(mgr(), "hardware"));
    TEST_ASSERT_EQUAL_UINT32(1u, mgr()->GetVersion(mgr(), "calibration"));
    TEST_ASSERT_EQUAL_UINT32(1u, mgr()->GetVersion(mgr(), "thermal"));
    TEST_ASSERT_EQUAL_UINT32(1u, mgr()->GetVersion(mgr(), "zigbee_reporting"));
    TEST_ASSERT_EQUAL_UINT32(0u, mgr()->GetVersion(mgr(), "unknown_block"));
}

/* =========================================================================
 * Test Group 5: SaveAsync
 * ========================================================================= */

static uint8_t captured_request;
static BaseType_t capture_request(QueueHandle_t queue, const void *item, TickType_t wait) {
    (void)queue; TEST_ASSERT_EQUAL(0, wait); captured_request = *(const uint8_t *)item; return pdTRUE;
}
void test_ConfigMgr_SaveAsync_QueuesCopiedBlockIdentifier(void) {
    RESET_FAKE(mocked_xTaskCreate);
    RESET_FAKE(mocked_xQueueSend);
    mocked_xQueueSend_fake.return_val = pdTRUE;
    mocked_xQueueSend_fake.custom_fake = capture_request;
    char block[] = "control";
    TEST_ASSERT_EQUAL(ESP_OK, mgr()->SaveAsync(mgr(), block));
    memset(block, 'x', sizeof(block) - 1);
    TEST_ASSERT_EQUAL(1, captured_request);
    TEST_ASSERT_EQUAL(0, mocked_xTaskCreate_fake.call_count);
    TEST_ASSERT_EQUAL(1, mocked_xQueueSend_fake.call_count);
    TEST_ASSERT_EQUAL(0, mocked_xQueueSend_fake.arg2_val);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, mgr()->SaveAsync(mgr(), "unknown"));
    TEST_ASSERT_EQUAL(1, mocked_xQueueSend_fake.call_count);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, mgr()->SaveAsync(NULL, "system"));
    mocked_xSemaphoreTake_fake.return_val = pdFALSE;
    TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT, mgr()->SaveAsync(mgr(), NULL));
    mocked_xSemaphoreTake_fake.return_val = pdTRUE;
    TEST_ASSERT_EQUAL(1, mocked_xQueueSend_fake.call_count);
}

void test_ConfigMgr_WorkerCreationFailureRollsBack(void) {
    IService *service = &mgr()->base;
    TEST_ASSERT_EQUAL(0, service->Stop(service));
    mocked_xTaskCreate_fake.custom_fake = NULL;
    mocked_xTaskCreate_fake.return_val = pdFAIL;
    TEST_ASSERT_NOT_EQUAL(0, service->Start(service));
    TEST_ASSERT_EQUAL(SERVICE_STATE_STOPPED, service->GetState(service));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, mgr()->SaveAsync(mgr(), NULL));
    mocked_xTaskCreate_fake.custom_fake = xTaskCreate_writes_handle;
    TEST_ASSERT_EQUAL(0, service->Start(service));
}

void test_ConfigMgr_SaveAsync_QueueFullDoesNotAllocate(void) {
    RESET_FAKE(mocked_strdup);
    RESET_FAKE(mocked_xTaskCreate);
    mocked_xQueueSend_fake.return_val = pdFALSE;
    TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT, mgr()->SaveAsync(mgr(), "system"));
    TEST_ASSERT_EQUAL(0, mocked_strdup_fake.call_count);
    TEST_ASSERT_EQUAL(0, mocked_xTaskCreate_fake.call_count);
    mocked_xQueueSend_fake.return_val = pdTRUE;
}

/* =========================================================================
 * Test Group 6: Stop lifecycle
 * ========================================================================= */

/** @brief Stop() transitions service from STARTED to STOPPED. */
void test_ConfigMgr_Stop_FromStarted(void) {
    IService *svc = (IService *)mgr();

    if (svc->GetState(svc) != SERVICE_STATE_STARTED) {
        svc->Initialize(svc, NULL);
        svc->Start(svc);
    }
    TEST_ASSERT_EQUAL(SERVICE_STATE_STARTED, svc->GetState(svc));

    int32_t ret = svc->Stop(svc);

    TEST_ASSERT_EQUAL_INT32(0, ret);
    TEST_ASSERT_EQUAL(SERVICE_STATE_STOPPED, svc->GetState(svc));

    /* Restore for subsequent tests. */
    svc->Initialize(svc, NULL);
    svc->Start(svc);
}

/**
 * @brief Captures the persistent worker entry and returns an owned test handle.
 */
static TaskFunction_t pending_save_task;
static void *pending_save_arg;
static BaseType_t xTaskCreate_writes_handle(TaskFunction_t fn, const char *nm,
                                             uint32_t stack, void *pv,
                                             uint32_t pri, TaskHandle_t *out) {
    (void)nm; (void)stack; (void)pri;
    pending_save_task = fn; pending_save_arg = pv;
    if (out) *out = (TaskHandle_t)0x1; /* Non-NULL sentinel. */
    return pdPASS;
}

/** @brief Stop timeout retains the worker; deletion requires acknowledgment. */
void test_ConfigMgr_Stop_PreservesPendingSave(void) {
    IService *svc = (IService *)mgr();

    if (svc->GetState(svc) != SERVICE_STATE_STARTED) {
        svc->Initialize(svc, NULL);
        svc->Start(svc);
    }

    RESET_FAKE(mocked_vTaskDelete);
    mocked_xQueueReceive_fake.custom_fake = NULL;
    mocked_xQueueReceive_fake.return_val = pdFALSE;
    TEST_ASSERT_NOT_EQUAL(0, svc->Stop(svc));
    TEST_ASSERT_EQUAL(0, mocked_vTaskDelete_fake.call_count);
    TEST_ASSERT_NOT_EQUAL(0, svc->Initialize(svc, NULL));
    TEST_ASSERT_NOT_EQUAL(0, svc->Start(svc));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, mgr()->SaveAsync(mgr(), NULL));
    mocked_xQueueReceive_fake.custom_fake = acknowledge_stop;
    TEST_ASSERT_EQUAL(0, svc->Stop(svc));
    TEST_ASSERT_EQUAL(1, mocked_vTaskDelete_fake.call_count);
    TEST_ASSERT_EQUAL(0, svc->Start(svc));
}

/* =========================================================================
 * Test Group 7: ValidateSettings â€“ extended checks
 * ========================================================================= */

/** @brief ValidateSettings fails when ir_object_cal exceeds CALIBRATION_MAX. */
void test_ConfigMgr_ValidateSettings_CalibrationOutOfRange(void) {
    SystemSettings s = *mgr()->GetSettings(mgr());
    s.calibration.ir_object_cal = CALIBRATION_MAX + 1.0f;

    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, mgr()->UpdateSettings(mgr(), &s, sizeof(s)));
    TEST_ASSERT_EQUAL(ESP_OK, mgr()->ValidateSettings(mgr()));

    mgr()->ResetToDefaults(mgr(), "calibration");
}

/** @brief ValidateSettings fails when cpu_temp_warning >= cpu_temp_critical. */
void test_ConfigMgr_ValidateSettings_ThermalOrdering(void) {
    SystemSettings s = *mgr()->GetSettings(mgr());
    s.thermal.cpu_temp_warning  = 110.0f;
    s.thermal.cpu_temp_critical =  90.0f;

    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, mgr()->UpdateSettings(mgr(), &s, sizeof(s)));
    TEST_ASSERT_EQUAL(ESP_OK, mgr()->ValidateSettings(mgr()));

    mgr()->ResetToDefaults(mgr(), "thermal");
}

/** @brief ValidateSettings fails when temp_unit is out of TempUnit range. */
void test_ConfigMgr_ValidateSettings_InvalidTempUnit(void) {
    SystemSettings s = *mgr()->GetSettings(mgr());
    s.system.temp_unit = (uint8_t)TEMP_UNIT_COUNT;

    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, mgr()->UpdateSettings(mgr(), &s, sizeof(s)));
    TEST_ASSERT_EQUAL(ESP_OK, mgr()->ValidateSettings(mgr()));

    mgr()->ResetToDefaults(mgr(), "system");
}

/* =========================================================================
 * Test Group 8: ResetToDefaults â€“ all blocks
 * ========================================================================= */

/** @brief ResetToDefaults(NULL) saves every block and returns ESP_OK. */
void test_ConfigMgr_ResetToDefaults_AllBlocks(void) {
    RESET_FAKE(mocked_nvs_open);
    RESET_FAKE(mocked_nvs_set_blob);
    RESET_FAKE(mocked_nvs_commit);
    mocked_nvs_open_fake.return_val     = ESP_OK;
    mocked_nvs_set_blob_fake.return_val = ESP_OK;
    mocked_nvs_commit_fake.return_val   = ESP_OK;

    esp_err_t err = mgr()->ResetToDefaults(mgr(), NULL);

    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_EQUAL_INT(CONFIG_STATUS_BLOCK_CAPACITY, mocked_nvs_open_fake.call_count);
}

/* =========================================================================
 * Test Group 9: GetVersion â€“ NULL argument
 * ========================================================================= */

/** @brief GetVersion returns 0 for a NULL block_name argument. */
void test_ConfigMgr_GetVersion_NullBlock(void) {
    TEST_ASSERT_EQUAL_UINT32(0u, mgr()->GetVersion(mgr(), NULL));
}

/* =========================================================================
 * Test Group 10: Load with NVS data present
 * ========================================================================= */

static esp_err_t fake_nvs_get_blob_system(nvs_handle_t h, const char *k,
                                           void *out, size_t *sz) {
    (void)h; (void)k;
    if (out && *sz >= sizeof(SystemBaseConfigs)) {
        SystemBaseConfigs *b  = (SystemBaseConfigs *)out;
        b->temp_unit          = TEMP_UNIT_FAHRENHEIT;
        b->oled_timeout_sec   = 300u;
        b->version            = 1u;
    }
    return ESP_OK;
}

/** @brief Load applies NVS data over in-memory defaults. */
void test_ConfigMgr_Load_WithNvsData(void) {
    mocked_nvs_open_fake.return_val       = ESP_OK;
    mocked_nvs_get_blob_fake.custom_fake  = fake_nvs_get_blob_system;

    esp_err_t err = mgr()->Load(mgr(), "system");

    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_EQUAL_UINT8(TEMP_UNIT_FAHRENHEIT,
                            mgr()->GetSettings(mgr())->system.temp_unit);

    mocked_nvs_get_blob_fake.custom_fake = NULL;
    mgr()->ResetToDefaults(mgr(), "system");
}

/* =========================================================================
 * Test Group 11: Dirty flag tracking
 * ========================================================================= */

/** @brief IsDirty is true for all blocks after UpdateSettings. */
void test_ConfigMgr_IsDirty_AfterUpdate(void) {
    SystemSettings s = *mgr()->GetSettings(mgr());
    s.system.oled_timeout_sec = 120u;

    TEST_ASSERT_EQUAL(ESP_OK, mgr()->UpdateSettings(mgr(), &s, sizeof(s)));

    TEST_ASSERT_TRUE(mgr()->IsDirty(mgr(), NULL));
    TEST_ASSERT_TRUE(mgr()->IsDirty(mgr(), "system"));
    TEST_ASSERT_TRUE(mgr()->IsDirty(mgr(), "control"));
}

/** @brief IsDirty clears for a saved block; other blocks stay dirty. */
void test_ConfigMgr_IsDirty_AfterSave(void) {
    RESET_FAKE(mocked_nvs_open);
    RESET_FAKE(mocked_nvs_set_blob);
    RESET_FAKE(mocked_nvs_commit);
    mocked_nvs_open_fake.return_val     = ESP_OK;
    mocked_nvs_set_blob_fake.return_val = ESP_OK;
    mocked_nvs_commit_fake.return_val   = ESP_OK;

    /* Dirty all blocks. */
    SystemSettings s = *mgr()->GetSettings(mgr());
    s.system.oled_timeout_sec = 120u;
    mgr()->UpdateSettings(mgr(), &s, sizeof(s));
    TEST_ASSERT_TRUE(mgr()->IsDirty(mgr(), "system"));

    /* Save only the system block. */
    TEST_ASSERT_EQUAL(ESP_OK, mgr()->Save(mgr(), "system"));

    /* System is now clean; control is still dirty. */
    TEST_ASSERT_FALSE(mgr()->IsDirty(mgr(), "system"));
    TEST_ASSERT_TRUE(mgr()->IsDirty(mgr(), "control"));

    mgr()->ResetToDefaults(mgr(), NULL);
}

/* =========================================================================
 * Test runner
 * ========================================================================= */


void test_ConfigMgr_ThermalRejectsNonFiniteAndInverted(void) {
    HardwareConfigs hw = mgr()->GetSettings(mgr())->hardware;
    TEST_ASSERT_TRUE(SystemConfigs_ValidateRGBThermal(&hw));
    hw.ext_led_temp_cold_min = hw.ext_led_temp_cold_max;
    TEST_ASSERT_FALSE(SystemConfigs_ValidateRGBThermal(&hw));
    hw = mgr()->GetSettings(mgr())->hardware;
    hw.ext_led_temp_danger = NAN;
    TEST_ASSERT_FALSE(SystemConfigs_ValidateRGBThermal(&hw));
}
static HardwareConfigs migration_fixture;
static size_t migration_size;
static esp_err_t load_migration_fixture(nvs_handle_t handle, const char *key, void *out, size_t *size) {
    (void)handle; (void)key;
    if (*size < migration_size) return ESP_ERR_NVS_INVALID_LENGTH;
    memcpy(out, &migration_fixture, migration_size);
    *size = migration_size;
    return ESP_OK;
}
void test_ConfigMgr_MigrationPreservesSettings(void) {
    HardwareConfigs backup = mgr()->GetSettings(mgr())->hardware;
    migration_fixture = backup;
    migration_fixture.version = 4;
    migration_fixture.ext_led_temp_cold_min = 18.0f;
    migration_fixture.ext_led_color_freeze_x = 0x3127;
    migration_fixture.ext_led_color_freeze_y = 0x3290;
    migration_fixture.ext_led_color_danger_x = 23000;
    migration_fixture.ext_led_color_danger_y = 21000;
    migration_size = sizeof(migration_fixture);
    mocked_nvs_open_fake.return_val = ESP_OK;
    mocked_nvs_get_blob_fake.custom_fake = load_migration_fixture;
    TEST_ASSERT_EQUAL(ESP_OK, mgr()->Load(mgr(), "hardware"));
    HardwareConfigs *h = &mgr()->GetSettings(mgr())->hardware;
    TEST_ASSERT_EQUAL(HARDWARE_CONFIG_SCHEMA_VERSION, h->version);
    TEST_ASSERT_EQUAL_FLOAT(18.0f, h->ext_led_temp_cold_min);
    TEST_ASSERT_EQUAL(RGBLED_PRESET_WHITE_X, h->ext_led_color_freeze_x);
    TEST_ASSERT_EQUAL(RGBLED_PRESET_WHITE_Y, h->ext_led_color_freeze_y);
    TEST_ASSERT_EQUAL(23000, h->ext_led_color_danger_x);
    *h = backup;
    mocked_nvs_get_blob_fake.custom_fake = NULL;
}
void test_ConfigMgr_MigrationV5Brightness(void) {
    HardwareConfigs backup = mgr()->GetSettings(mgr())->hardware;
    migration_fixture = backup;
    migration_fixture.version = 5;
    migration_fixture.builtin_led_brightness = 19; /* Unspecified legacy padding. */
    migration_fixture.ext_led_color_cold_x = 0x1300;
    migration_fixture.ext_led_color_cold_y = 0x1200;
    migration_size = sizeof(migration_fixture);
    mocked_nvs_open_fake.return_val = ESP_OK;
    mocked_nvs_get_blob_fake.custom_fake = load_migration_fixture;
    TEST_ASSERT_EQUAL(ESP_OK, mgr()->Load(mgr(), "hardware"));
    HardwareConfigs *h = &mgr()->GetSettings(mgr())->hardware;
    TEST_ASSERT_EQUAL(HARDWARE_CONFIG_SCHEMA_VERSION, h->version);
    TEST_ASSERT_EQUAL(DEFAULT_BUILTIN_LED_BRIGHTNESS, h->builtin_led_brightness);
    TEST_ASSERT_EQUAL(0x1300, h->ext_led_color_cold_x);
    TEST_ASSERT_EQUAL(0x1200, h->ext_led_color_cold_y);
    migration_fixture = *h;
    migration_fixture.builtin_led_brightness = 0;
    TEST_ASSERT_EQUAL(ESP_OK, mgr()->Load(mgr(), "hardware"));
    TEST_ASSERT_EQUAL(0, h->builtin_led_brightness);
    migration_fixture.version = 5;
    mocked_nvs_commit_fake.return_val = ESP_FAIL;
    HardwareConfigs before_failure = *h;
    TEST_ASSERT_EQUAL(ESP_FAIL, mgr()->Load(mgr(), "hardware"));
    TEST_ASSERT_EQUAL_MEMORY(&before_failure, h, sizeof(*h));
    mocked_nvs_commit_fake.return_val = ESP_OK;
    TEST_ASSERT_EQUAL(ESP_OK, mgr()->Load(mgr(), "hardware"));
    TEST_ASSERT_EQUAL(DEFAULT_BUILTIN_LED_BRIGHTNESS, h->builtin_led_brightness);
    *h = backup;
    mocked_nvs_get_blob_fake.custom_fake = NULL;
}
void test_ConfigMgr_MigrationV3Layout(void) {
    HardwareConfigs backup = mgr()->GetSettings(mgr())->hardware;
    migration_fixture = backup;
    migration_fixture.version = 3;
    migration_fixture.ext_led_anim = 4;
    migration_fixture.ext_led_temp_cold_min = 18.0f;
    size_t old_tail = offsetof(HardwareConfigs, ext_led_color_danger_x);
    size_t new_tail = offsetof(HardwareConfigs, ext_led_mode);
    migration_size = 64u;
    memmove((uint8_t *)&migration_fixture + old_tail, (uint8_t *)&migration_fixture + new_tail,
            72u - new_tail);
    uint32_t legacy_version = 3;
    memcpy((uint8_t *)&migration_fixture + 60u, &legacy_version, sizeof(legacy_version));
    mocked_nvs_open_fake.return_val = ESP_OK;
    mocked_nvs_get_blob_fake.custom_fake = load_migration_fixture;
    TEST_ASSERT_EQUAL(ESP_OK, mgr()->Load(mgr(), "hardware"));
    HardwareConfigs *h = &mgr()->GetSettings(mgr())->hardware;
    TEST_ASSERT_EQUAL(HARDWARE_CONFIG_SCHEMA_VERSION, h->version);
    TEST_ASSERT_EQUAL(4, h->ext_led_anim);
    TEST_ASSERT_EQUAL_FLOAT(18.0f, h->ext_led_temp_cold_min);
    TEST_ASSERT_EQUAL(RGBLED_PRESET_WHITE_X, h->ext_led_color_freeze_x);
    *h = backup;
    mocked_nvs_get_blob_fake.custom_fake = NULL;
}
void test_ConfigMgr_FanSetpointBounds(void) {
    TEST_ASSERT_TRUE(SystemConfigs_IsFanSetpointPairValid(99.0f, 20.0f));
    TEST_ASSERT_TRUE(SystemConfigs_IsFanSetpointPairValid(30.0f, 28.0f));
    TEST_ASSERT_FALSE(SystemConfigs_IsFanSetpointPairValid(99.01f, 20.0f));
    TEST_ASSERT_FALSE(SystemConfigs_IsFanSetpointPairValid(30.0f, 19.99f));
    TEST_ASSERT_FALSE(SystemConfigs_IsFanSetpointPairValid(20.0f, 20.0f));
    TEST_ASSERT_FALSE(SystemConfigs_IsFanSetpointPairValid(NAN, 20.0f));
    TEST_ASSERT_FALSE(SystemConfigs_IsFanSetpointPairValid(30.0f, INFINITY));
}


void test_ConfigMgr_UpdateRejectsNonFiniteWithoutMutation(void) {
    SystemSettings before = *mgr()->GetSettings(mgr());
    SystemSettings candidate = before;
    float *fields[] = {&candidate.control.threshold_high, &candidate.control.threshold_low,
        &candidate.calibration.ir_object_cal, &candidate.calibration.ir_ambient_cal,
        &candidate.thermal.cpu_temp_warning, &candidate.thermal.cpu_temp_critical,
        &candidate.thermal.cpu_temp_emergency};
    unsigned events_before = mocked_GetEventBus_fake.call_count;
    bool dirty_before = mgr()->IsDirty(mgr(), NULL);
    for (unsigned i = 0; i < sizeof(fields)/sizeof(fields[0]); ++i) {
        float value = *fields[i];
        *fields[i] = NAN;
        TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, mgr()->UpdateSettings(mgr(), &candidate, sizeof(candidate)));
        TEST_ASSERT_EQUAL_MEMORY(&before, mgr()->GetSettings(mgr()), sizeof(before));
        *fields[i] = INFINITY;
        TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, mgr()->UpdateSettings(mgr(), &candidate, sizeof(candidate)));
        *fields[i] = value;
    }
    TEST_ASSERT_EQUAL(events_before, mocked_GetEventBus_fake.call_count);
    TEST_ASSERT_EQUAL(dirty_before, mgr()->IsDirty(mgr(), NULL));
}
void test_ConfigMgr_UpdateLockFailurePreservesSettings(void) {
    SystemSettings before = *mgr()->GetSettings(mgr());
    SystemSettings candidate = before;
    candidate.system.oled_timeout_sec = 60;
    mocked_xSemaphoreTake_fake.return_val = pdFALSE;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, mgr()->UpdateSettings(mgr(), &candidate, sizeof(candidate)));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, mgr()->ValidateSettings(mgr()));
    TEST_ASSERT_EQUAL_MEMORY(&before, mgr()->GetSettings(mgr()), sizeof(before));
}


void test_ConfigMgr_FieldUpdatesPreserveUnrelatedValues(void) {
    SystemSettings before;
    TEST_ASSERT_EQUAL(ESP_OK, mgr()->GetSnapshot(mgr(), &before));
    float high = 95.0f, low = 22.0f;
    ConfigurationFieldUpdate first = {offsetof(ControlConfigs, threshold_high), &high, sizeof(high)};
    ConfigurationFieldUpdate second = {offsetof(ControlConfigs, threshold_low), &low, sizeof(low)};
    TEST_ASSERT_EQUAL(ESP_OK, mgr()->UpdateFields(mgr(), "control", &first, 1, false));
    TEST_ASSERT_EQUAL(ESP_OK, mgr()->UpdateFields(mgr(), "control", &second, 1, false));
    SystemSettings after;
    TEST_ASSERT_EQUAL(ESP_OK, mgr()->GetSnapshot(mgr(), &after));
    TEST_ASSERT_EQUAL_FLOAT(high, after.control.threshold_high);
    TEST_ASSERT_EQUAL_FLOAT(low, after.control.threshold_low);
    TEST_ASSERT_EQUAL_FLOAT(before.control.relay_open_temp, after.control.relay_open_temp);
    TEST_ASSERT_EQUAL_MEMORY(&before.hardware, &after.hardware, sizeof(before.hardware));
    TEST_ASSERT_EQUAL(ESP_OK, mgr()->UpdateSettings(mgr(), &before, sizeof(before)));
}
void test_ConfigMgr_FieldCommitFailureDoesNotChangeLiveState(void) {
    SystemSettings before;
    TEST_ASSERT_EQUAL(ESP_OK, mgr()->GetSnapshot(mgr(), &before));
    bool dirty = mgr()->IsDirty(mgr(), "system");
    uint32_t timeout = before.system.oled_timeout_sec == 60 ? 90 : 60;
    ConfigurationFieldUpdate field = {offsetof(SystemBaseConfigs, oled_timeout_sec), &timeout, sizeof(timeout)};
    mocked_nvs_open_fake.return_val = ESP_OK;
    mocked_nvs_commit_fake.return_val = ESP_FAIL;
    unsigned events = mocked_GetEventBus_fake.call_count;
    TEST_ASSERT_EQUAL(ESP_FAIL, mgr()->UpdateFields(mgr(), "system", &field, 1, true));
    TEST_ASSERT_EQUAL_MEMORY(&before, mgr()->GetSettings(mgr()), sizeof(before));
    TEST_ASSERT_EQUAL(dirty, mgr()->IsDirty(mgr(), "system"));
    TEST_ASSERT_EQUAL(events, mocked_GetEventBus_fake.call_count);
}
void test_ConfigMgr_FieldCommitOnlyCleansItsOwnBlock(void) {
    SystemSettings before;
    TEST_ASSERT_EQUAL(ESP_OK, mgr()->GetSnapshot(mgr(), &before));
    TEST_ASSERT_EQUAL(ESP_OK, mgr()->UpdateSettings(mgr(), &before, sizeof(before)));
    uint32_t timeout = 70;
    ConfigurationFieldUpdate field = {offsetof(SystemBaseConfigs, oled_timeout_sec), &timeout, sizeof(timeout)};
    mocked_nvs_open_fake.return_val = ESP_OK;
    TEST_ASSERT_EQUAL(ESP_OK, mgr()->UpdateFields(mgr(), "system", &field, 1, true));
    TEST_ASSERT_FALSE(mgr()->IsDirty(mgr(), "system"));
    TEST_ASSERT_TRUE(mgr()->IsDirty(mgr(), "control"));
    TEST_ASSERT_EQUAL(timeout, mgr()->GetSettings(mgr())->system.oled_timeout_sec);
    TEST_ASSERT_EQUAL(ESP_OK, mgr()->UpdateSettings(mgr(), &before, sizeof(before)));
}
void test_ConfigMgr_FieldValidationRejectsInvalidPair(void) {
    SystemSettings before = *mgr()->GetSettings(mgr());
    float open = 25, close = 30;
    ConfigurationFieldUpdate fields[] = {
        {offsetof(ControlConfigs, relay_open_temp), &open, sizeof(open)},
        {offsetof(ControlConfigs, relay_close_temp), &close, sizeof(close)}
    };
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, mgr()->UpdateFields(mgr(), "control", fields, 2, false));
    TEST_ASSERT_EQUAL_MEMORY(&before, mgr()->GetSettings(mgr()), sizeof(before));
}
void test_ConfigMgr_FieldBoundsAndOverlapAreRejected(void) {
    uint32_t value = 1;
    ConfigurationFieldUpdate fields[] = {{SIZE_MAX, &value, sizeof(value)}, {0, &value, sizeof(value)}};
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, mgr()->UpdateFields(mgr(), "system", fields, 1, false));
    fields[0].offset = offsetof(SystemBaseConfigs, version);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, mgr()->UpdateFields(mgr(), "system", fields, 1, false));
    fields[0].offset = offsetof(SystemBaseConfigs, oled_timeout_sec);
    fields[0].size = 1;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, mgr()->UpdateFields(mgr(), "system", fields, 1, false));
    fields[0].size = sizeof(value);
    fields[0].offset = fields[1].offset = offsetof(SystemBaseConfigs, oled_timeout_sec);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, mgr()->UpdateFields(mgr(), "system", fields, 2, false));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, mgr()->UpdateFields(mgr(), "unknown", fields, 1, false));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, mgr()->UpdateFields(mgr(), "system", fields, CONFIG_UPDATE_MAX_FIELDS+1, false));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, mgr()->UpdateFields(mgr(), "system", NULL, 1, false));
}
void test_ConfigMgr_SnapshotAndFieldLockFailure(void) {
    SystemSettings snapshot;
    TEST_ASSERT_EQUAL(ESP_OK, mgr()->GetSnapshot(mgr(), &snapshot));
    SystemSettings untouched = snapshot;
    mocked_xSemaphoreTake_fake.return_val = pdFALSE;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, mgr()->GetSnapshot(mgr(), &snapshot));
    TEST_ASSERT_EQUAL_MEMORY(&untouched, &snapshot, sizeof(snapshot));
    uint32_t timeout = 90;
    ConfigurationFieldUpdate field = {offsetof(SystemBaseConfigs, oled_timeout_sec), &timeout, sizeof(timeout)};
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, mgr()->UpdateFields(mgr(), "system", &field, 1, false));
    TEST_ASSERT_EQUAL_MEMORY(&untouched, mgr()->GetSettings(mgr()), sizeof(untouched));
}

void test_ConfigMgr_SnapshotRepresentationIsIndependentOfDestinationStorage(void) {
    SystemSettings first,second;
    memset(&first,0xa5,sizeof(first));memset(&second,0x5a,sizeof(second));
    TEST_ASSERT_EQUAL(ESP_OK,mgr()->GetSnapshot(mgr(),&first));
    TEST_ASSERT_EQUAL(ESP_OK,mgr()->GetSnapshot(mgr(),&second));
    TEST_ASSERT_EQUAL_MEMORY(&first,&second,sizeof(first));
    uint32_t timeout=second.system.oled_timeout_sec;
    first.system.oled_timeout_sec=timeout+1;
    TEST_ASSERT_EQUAL_UINT32(timeout,second.system.oled_timeout_sec);
    TEST_ASSERT_EQUAL(ESP_OK,mgr()->GetSnapshot(mgr(),&first));
    TEST_ASSERT_EQUAL_MEMORY(&first,&second,sizeof(first));
}

void test_ConfigMgr_ReportingZeroBoundsAndPersistence(void) {
    mocked_nvs_open_fake.return_val = ESP_OK;
    for (unsigned kind = 0; kind < ZIGBEE_REPORT_COUNT; ++kind) {
        const uint32_t valid[] = {0, 1, ZB_REPORT_MAX_S};
        for (unsigned i = 0; i < 3; ++i) {
            TEST_ASSERT_EQUAL(ESP_OK, ZigbeeReporting_SetInterval(mgr(), (ZigbeeReportKind)kind, valid[i]));
            uint16_t actual;
            TEST_ASSERT_EQUAL(ESP_OK, ZigbeeReporting_GetInterval(mgr(), (ZigbeeReportKind)kind, &actual));
            TEST_ASSERT_EQUAL(valid[i], actual);
            TEST_ASSERT_FALSE(mgr()->IsDirty(mgr(), "zigbee_reporting"));
        }
        TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, ZigbeeReporting_SetInterval(mgr(), (ZigbeeReportKind)kind, ZB_REPORT_MAX_S + 1));
        TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, ZigbeeReporting_SetInterval(mgr(), (ZigbeeReportKind)kind, UINT32_MAX));
    }
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, ZigbeeReporting_SetInterval(mgr(), ZIGBEE_REPORT_COUNT, 0));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, ZigbeeReporting_SetInterval(mgr(), (ZigbeeReportKind)-1, 0));
}
static SystemSettings published_snapshot;
static bool published_snapshot_owned;
static unsigned snapshot_events;
static int32_t capture_config_snapshot(BusEvent event) {
    if (event.dataSize == sizeof(SystemSettings)) {
        published_snapshot_owned = event.data != mgr()->GetSettings(mgr());
        memcpy(&published_snapshot, event.data, sizeof(published_snapshot));
        ++snapshot_events;
    }
    return 0;
}
void test_ConfigMgr_ChangedEventUsesOwnedSnapshot(void) {
    SystemSettings before;
    TEST_ASSERT_EQUAL(ESP_OK,mgr()->GetSnapshot(mgr(),&before));
    IEventBus bus={.Publish=capture_config_snapshot};
    mocked_GetEventBus_fake.return_val=&bus;
    snapshot_events=0; published_snapshot_owned=false;
    float value=before.calibration.ir_object_cal==0.1f?0.2f:0.1f;
    ConfigurationFieldUpdate field={offsetof(CalibrationConfigs,ir_object_cal),&value,sizeof(value)};
    TEST_ASSERT_EQUAL(ESP_OK,mgr()->UpdateFields(mgr(),"calibration",&field,1,false));
    mocked_GetEventBus_fake.return_val=NULL;
    TEST_ASSERT_EQUAL(1,snapshot_events);
    TEST_ASSERT_TRUE(published_snapshot_owned);
    TEST_ASSERT_EQUAL_FLOAT(value,published_snapshot.calibration.ir_object_cal);
    TEST_ASSERT_EQUAL_MEMORY(&before.hardware,&published_snapshot.hardware,sizeof(before.hardware));
    TEST_ASSERT_EQUAL(ESP_OK,mgr()->UpdateSettings(mgr(),&before,sizeof(before)));
}

static SystemBaseConfigs system_migration_fixture;
#include "Communication/DebugConsole/Core/DeveloperMode.h"
static esp_err_t load_system_fixture(nvs_handle_t handle, const char *key, void *out, size_t *size) {
    (void)handle; (void)key;
    if (*size != sizeof(system_migration_fixture)) return ESP_ERR_NVS_INVALID_LENGTH;
    memcpy(out, &system_migration_fixture, *size);
    return ESP_OK;
}
void test_ConfigMgr_DeveloperPreferenceMigratesAndRollsBackFailedWrites(void) {
    SystemBaseConfigs backup = mgr()->GetSettings(mgr())->system;
    mocked_nvs_open_fake.return_val = ESP_OK;
    mocked_nvs_get_blob_fake.custom_fake = load_system_fixture;
    system_migration_fixture = backup;
    system_migration_fixture.version = 4;
    system_migration_fixture.developer_mode_enabled = 255;
    TEST_ASSERT_EQUAL(ESP_OK, mgr()->Load(mgr(), "system"));
    TEST_ASSERT_FALSE(DeveloperMode_IsEnabled());
    TEST_ASSERT_EQUAL(0, mgr()->GetSettings(mgr())->system.developer_mode_enabled);
    uint8_t value = 1;
    ConfigurationFieldUpdate field = {offsetof(SystemBaseConfigs, developer_mode_enabled), &value, sizeof(value)};
    uint32_t timeout = backup.oled_timeout_sec == 20 ? 21 : 20;
    ConfigurationFieldUpdate pending = {offsetof(SystemBaseConfigs, oled_timeout_sec), &timeout, sizeof(timeout)};
    TEST_ASSERT_EQUAL(ESP_OK, mgr()->UpdateFields(mgr(), "system", &pending, 1, false));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, mgr()->UpdateFields(mgr(), "system", &field, 1, true));
    TEST_ASSERT_FALSE(DeveloperMode_IsEnabled());
    TEST_ASSERT_TRUE(mgr()->IsDirty(mgr(), "system"));
    timeout = backup.oled_timeout_sec;
    TEST_ASSERT_EQUAL(ESP_OK, mgr()->UpdateFields(mgr(), "system", &pending, 1, true));
    mocked_nvs_commit_fake.return_val = ESP_FAIL;
    TEST_ASSERT_NOT_EQUAL(ESP_OK, mgr()->UpdateFields(mgr(), "system", &field, 1, true));
    TEST_ASSERT_FALSE(DeveloperMode_IsEnabled());
    TEST_ASSERT_EQUAL(0, mgr()->GetSettings(mgr())->system.developer_mode_enabled);
    mocked_nvs_commit_fake.return_val = ESP_OK;
    TEST_ASSERT_EQUAL(ESP_OK, mgr()->UpdateFields(mgr(), "system", &field, 1, true));
    TEST_ASSERT_TRUE(DeveloperMode_IsEnabled());
    system_migration_fixture = mgr()->GetSettings(mgr())->system;
    DeveloperMode_Reset();
    TEST_ASSERT_EQUAL(ESP_OK, mgr()->Load(mgr(), "system"));
    TEST_ASSERT_TRUE(DeveloperMode_IsEnabled());
    value = 2;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, mgr()->UpdateFields(mgr(), "system", &field, 1, true));
    TEST_ASSERT_TRUE(DeveloperMode_IsEnabled());
    mgr()->GetSettings(mgr())->system = backup;
    DeveloperMode_SetEnabled(backup.developer_mode_enabled != 0);
    mocked_nvs_get_blob_fake.custom_fake = NULL;
}
void test_ConfigMgr_MenuAutoExitMigrationPreservesOwnerAndRollback(void) {
    SystemBaseConfigs backup=mgr()->GetSettings(mgr())->system;
    TEST_ASSERT_EQUAL(20,sizeof(backup));TEST_ASSERT_EQUAL(15,offsetof(SystemBaseConfigs,menu_auto_exit_enabled));
    TEST_ASSERT_EQUAL(1,DEFAULT_MENU_AUTO_EXIT_ENABLED);
    mocked_nvs_open_fake.return_val=ESP_OK;mocked_nvs_get_blob_fake.custom_fake=load_system_fixture;
    for(unsigned old=0;old<2;++old) {
        system_migration_fixture=backup;system_migration_fixture.version=3;
        system_migration_fixture.menu_auto_exit_enabled=old ? 255 : 0;
        SystemBaseConfigs expected=system_migration_fixture;
        expected.version=SYSTEM_CONFIG_SCHEMA_VERSION;expected.menu_auto_exit_enabled=1;expected.developer_mode_enabled=0;
        TEST_ASSERT_EQUAL(ESP_OK,mgr()->Load(mgr(),"system"));
        TEST_ASSERT_EQUAL_MEMORY(&expected,&mgr()->GetSettings(mgr())->system,sizeof(expected));
    }
    SystemBaseConfigs before=mgr()->GetSettings(mgr())->system;
    mocked_nvs_commit_fake.return_val=ESP_FAIL;
    TEST_ASSERT_EQUAL(ESP_FAIL,mgr()->Load(mgr(),"system"));
    TEST_ASSERT_EQUAL_MEMORY(&before,&mgr()->GetSettings(mgr())->system,sizeof(before));
    mocked_nvs_commit_fake.return_val=ESP_OK;
    system_migration_fixture=before;system_migration_fixture.menu_auto_exit_enabled=0;
    TEST_ASSERT_EQUAL(ESP_OK,mgr()->Load(mgr(),"system"));
    TEST_ASSERT_EQUAL(0,mgr()->GetSettings(mgr())->system.menu_auto_exit_enabled);
    mgr()->GetSettings(mgr())->system=backup;
    mocked_nvs_get_blob_fake.custom_fake=NULL;
}
void test_ConfigMgr_MenuAutoExitWriteValidatesAndPersists(void) {
    SystemBaseConfigs backup=mgr()->GetSettings(mgr())->system;
    mocked_nvs_open_fake.return_val=ESP_OK;
    uint8_t enabled=0;ConfigurationFieldUpdate field={offsetof(SystemBaseConfigs,menu_auto_exit_enabled),&enabled,sizeof(enabled)};
    TEST_ASSERT_EQUAL(ESP_OK,mgr()->UpdateFields(mgr(),"system",&field,1,true));
    TEST_ASSERT_EQUAL(0,mgr()->GetSettings(mgr())->system.menu_auto_exit_enabled);
    enabled=2;TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,mgr()->UpdateFields(mgr(),"system",&field,1,true));
    TEST_ASSERT_EQUAL(0,mgr()->GetSettings(mgr())->system.menu_auto_exit_enabled);
    enabled=1;mocked_nvs_commit_fake.return_val=ESP_FAIL;
    TEST_ASSERT_EQUAL(ESP_FAIL,mgr()->UpdateFields(mgr(),"system",&field,1,true));
    TEST_ASSERT_EQUAL(0,mgr()->GetSettings(mgr())->system.menu_auto_exit_enabled);
    mocked_nvs_commit_fake.return_val=ESP_OK;
    TEST_ASSERT_EQUAL(ESP_OK,mgr()->UpdateFields(mgr(),"system",&field,1,true));
    TEST_ASSERT_EQUAL(1,mgr()->GetSettings(mgr())->system.menu_auto_exit_enabled);
    mgr()->GetSettings(mgr())->system=backup;
}
void test_ConfigMgr_SystemProfileMigrationPreservesExistingSettings(void) {
    SystemBaseConfigs backup = mgr()->GetSettings(mgr())->system;
    system_migration_fixture = backup;
    system_migration_fixture.version = 1;
    system_migration_fixture.power_profile = 255; /* Former unspecified padding. */
    system_migration_fixture.oled_timeout_sec = 73;
    mocked_nvs_open_fake.return_val = ESP_OK;
    mocked_nvs_get_blob_fake.custom_fake = load_system_fixture;
    TEST_ASSERT_EQUAL(ESP_OK, mgr()->Load(mgr(), "system"));
    SystemBaseConfigs *current = &mgr()->GetSettings(mgr())->system;
    TEST_ASSERT_EQUAL(SYSTEM_CONFIG_SCHEMA_VERSION, current->version);
    TEST_ASSERT_EQUAL(DEFAULT_POWER_PROFILE, current->power_profile);
    TEST_ASSERT_EQUAL(73, current->oled_timeout_sec);
    system_migration_fixture = *current;
    system_migration_fixture.power_profile = 0;
    TEST_ASSERT_EQUAL(ESP_OK, mgr()->Load(mgr(), "system"));
    TEST_ASSERT_EQUAL(0, current->power_profile);
    SystemBaseConfigs before = *current;
    system_migration_fixture.version = 1;
    mocked_nvs_commit_fake.return_val = ESP_FAIL;
    TEST_ASSERT_EQUAL(ESP_FAIL, mgr()->Load(mgr(), "system"));
    TEST_ASSERT_EQUAL_MEMORY(&before, current, sizeof(before));
    *current = backup;
    mocked_nvs_get_blob_fake.custom_fake = NULL;
    mocked_nvs_commit_fake.return_val = ESP_OK;
}

void test_ConfigMgr_TxProfileMigrationPreservesV2CpuAndDisplay(void) {
    SystemBaseConfigs backup = mgr()->GetSettings(mgr())->system;
    system_migration_fixture = backup;
    system_migration_fixture.version = 2;
    system_migration_fixture.power_profile = 2;
    system_migration_fixture.zigbee_tx_profile = 255; /* Unspecified V2 padding. */
    system_migration_fixture.oled_timeout_sec = 91;
    mocked_nvs_open_fake.return_val = ESP_OK;
    mocked_nvs_get_blob_fake.custom_fake = load_system_fixture;
    TEST_ASSERT_EQUAL(ESP_OK, mgr()->Load(mgr(), "system"));
    SystemBaseConfigs *current = &mgr()->GetSettings(mgr())->system;
    TEST_ASSERT_EQUAL(SYSTEM_CONFIG_SCHEMA_VERSION, current->version);
    TEST_ASSERT_EQUAL(2, current->power_profile);
    TEST_ASSERT_EQUAL(91, current->oled_timeout_sec);
    TEST_ASSERT_EQUAL(DEFAULT_ZIGBEE_TX_PROFILE, current->zigbee_tx_profile);
    SystemBaseConfigs before = *current;
    mocked_nvs_commit_fake.return_val = ESP_FAIL;
    TEST_ASSERT_EQUAL(ESP_FAIL, mgr()->Load(mgr(), "system"));
    TEST_ASSERT_EQUAL_MEMORY(&before, current, sizeof(before));
    *current = backup;
    mocked_nvs_get_blob_fake.custom_fake = NULL;
    mocked_nvs_commit_fake.return_val = ESP_OK;
}

void test_ConfigMgr_TxProfileFieldValidationPersistenceAndRollback(void) {
    SystemBaseConfigs backup = mgr()->GetSettings(mgr())->system;
    mocked_nvs_open_fake.return_val = ESP_OK;
    uint8_t mode = 0;
    ConfigurationFieldUpdate update = {offsetof(SystemBaseConfigs, zigbee_tx_profile), &mode, sizeof(mode)};
    for (mode = 0; mode < ZIGBEE_TX_PROFILE_COUNT; ++mode) {
        TEST_ASSERT_EQUAL(ESP_OK, mgr()->UpdateFields(mgr(), "system", &update, 1, true));
        TEST_ASSERT_EQUAL(mode, mgr()->GetSettings(mgr())->system.zigbee_tx_profile);
        TEST_ASSERT_EQUAL(backup.power_profile, mgr()->GetSettings(mgr())->system.power_profile);
        TEST_ASSERT_FALSE(mgr()->IsDirty(mgr(), "system"));
    }
    SystemBaseConfigs before = mgr()->GetSettings(mgr())->system;
    TEST_ASSERT_NOT_EQUAL(ESP_OK, mgr()->UpdateFields(mgr(), "system", &update, 1, true));
    TEST_ASSERT_EQUAL_MEMORY(&before, &mgr()->GetSettings(mgr())->system, sizeof(before));
    mode = 0;
    mocked_nvs_commit_fake.return_val = ESP_FAIL;
    TEST_ASSERT_EQUAL(ESP_FAIL, mgr()->UpdateFields(mgr(), "system", &update, 1, true));
    TEST_ASSERT_EQUAL_MEMORY(&before, &mgr()->GetSettings(mgr())->system, sizeof(before));
    mocked_nvs_commit_fake.return_val = ESP_OK;
    mgr()->GetSettings(mgr())->system = backup;
}

void test_ConfigMgr_PowerProfileRejectsUnknownValue(void) {
    uint8_t invalid = POWER_PROFILE_COUNT;
    SystemBaseConfigs before = mgr()->GetSettings(mgr())->system;
    ConfigurationFieldUpdate update = {offsetof(SystemBaseConfigs, power_profile), &invalid, sizeof(invalid)};
    TEST_ASSERT_NOT_EQUAL(ESP_OK, mgr()->UpdateFields(mgr(), "system", &update, 1, true));
    TEST_ASSERT_EQUAL_MEMORY(&before, &mgr()->GetSettings(mgr())->system, sizeof(before));
}

void test_ConfigMgr_PowerProfilePersistsThroughFieldAPI(void) {
    SystemBaseConfigs before = mgr()->GetSettings(mgr())->system;
    mocked_nvs_open_fake.return_val = ESP_OK;
    uint8_t mode = 0;
    ConfigurationFieldUpdate update = {offsetof(SystemBaseConfigs, power_profile), &mode, sizeof(mode)};
    for (mode = 0; mode < POWER_PROFILE_COUNT; ++mode) {
        TEST_ASSERT_EQUAL(ESP_OK, mgr()->UpdateFields(mgr(), "system", &update, 1, true));
        TEST_ASSERT_EQUAL(mode, mgr()->GetSettings(mgr())->system.power_profile);
        TEST_ASSERT_EQUAL(before.oled_timeout_sec, mgr()->GetSettings(mgr())->system.oled_timeout_sec);
        TEST_ASSERT_FALSE(mgr()->IsDirty(mgr(), "system"));
    }
    mode = 0;
    mocked_nvs_commit_fake.return_val = ESP_FAIL;
    TEST_ASSERT_EQUAL(ESP_FAIL, mgr()->UpdateFields(mgr(), "system", &update, 1, true));
    TEST_ASSERT_EQUAL(2, mgr()->GetSettings(mgr())->system.power_profile);
    mgr()->GetSettings(mgr())->system = before;
    mocked_nvs_commit_fake.return_val = ESP_OK;
}

static void (*captured_save_handler)(BusEvent);
static int32_t remove_save_subscription(IEventBus *self, const char *name, BusEventHandler handler) {
    (void)self; (void)name; (void)handler; return 0;
}
static int32_t capture_save_subscription(IEventBus *self, const char *name, void (*handler)(BusEvent)) {
    (void)self;
    if (strcmp(name, EVENT_CONFIG_SAVE_REQ) == 0) captured_save_handler = handler;
    return 0;
}
void test_ConfigMgr_SaveEventRejectsUnterminatedAndUnknownPayload(void) {
    IService *service = &mgr()->base;
    TEST_ASSERT_EQUAL(0, service->Stop(service));
    TEST_ASSERT_EQUAL(0, service->Initialize(service, NULL));
    IEventBus bus = {.Subscribe=capture_save_subscription,.Unsubscribe=remove_save_subscription};
    mocked_GetEventBus_fake.return_val = &bus;
    captured_save_handler = NULL;
    TEST_ASSERT_EQUAL(0, service->Start(service));
    TEST_ASSERT_NOT_NULL(captured_save_handler);
    mocked_GetEventBus_fake.return_val = NULL;
    RESET_FAKE(mocked_xQueueSend);
    mocked_xQueueSend_fake.return_val = pdTRUE;
    char unterminated[] = {'s','y','s','t','e','m'};
    captured_save_handler((BusEvent){.data=unterminated,.dataSize=sizeof(unterminated)});
    captured_save_handler((BusEvent){.data=NULL,.dataSize=1});
    char unknown[] = "unknown";
    captured_save_handler((BusEvent){.data=unknown,.dataSize=sizeof(unknown)});
    TEST_ASSERT_EQUAL(0, mocked_xQueueSend_fake.call_count);
    char valid[] = "system";
    captured_save_handler((BusEvent){.data=valid,.dataSize=sizeof(valid)});
    TEST_ASSERT_EQUAL(1, mocked_xQueueSend_fake.call_count);
    captured_save_handler((BusEvent){0});
    TEST_ASSERT_EQUAL(2, mocked_xQueueSend_fake.call_count);
    TEST_ASSERT_EQUAL(0, service->Stop(service));
    TEST_ASSERT_EQUAL(0, service->Start(service));
}

static unsigned subscription_calls, removal_calls;
static unsigned fail_subscription;
static bool removal_busy;
static int32_t subscribe_control(IEventBus *self, const char *name, BusEventHandler handler) {
    (void)self; (void)name; (void)handler;
    return ++subscription_calls == fail_subscription ? -1 : 0;
}
static int32_t unsubscribe_control(IEventBus *self, const char *name, BusEventHandler handler) {
    (void)self; (void)name; (void)handler; ++removal_calls;
    return removal_busy ? EVENT_BUS_ERR_BUSY : 0;
}
void test_ConfigMgr_EarlySubscriptionFailureCanStopBeforeWorkerInit(void) {
    IService *service = &mgr()->base;
    IEventBus bus = {.Subscribe=subscribe_control,.Unsubscribe=unsubscribe_control};
    mocked_GetEventBus_fake.return_val = &bus;
    subscription_calls = removal_calls = 0; fail_subscription = 2; removal_busy = true;
    TEST_ASSERT_NOT_EQUAL(0, service->Start(service));
    TEST_ASSERT_EQUAL(SERVICE_STATE_ERROR, service->GetState(service));
    TEST_ASSERT_NOT_EQUAL(0, service->Stop(service));
    removal_busy = false;
    TEST_ASSERT_EQUAL(0, service->Stop(service));
    TEST_ASSERT_EQUAL(SERVICE_STATE_STOPPED, service->GetState(service));
    mocked_GetEventBus_fake.return_val = NULL;
}

void test_ConfigMgr_PartialSubscriptionsRetainBusyOwnership(void) {
    IService *service = &mgr()->base;
    TEST_ASSERT_EQUAL(0, service->Stop(service));
    IEventBus bus = {.Subscribe=subscribe_control,.Unsubscribe=unsubscribe_control};
    mocked_GetEventBus_fake.return_val = &bus;
    subscription_calls = removal_calls = 0; fail_subscription = 2; removal_busy = true;
    TEST_ASSERT_NOT_EQUAL(0, service->Start(service));
    TEST_ASSERT_EQUAL(2, subscription_calls);
    TEST_ASSERT_EQUAL(1, removal_calls);
    TEST_ASSERT_EQUAL(SERVICE_STATE_ERROR, service->GetState(service));
    TEST_ASSERT_NOT_EQUAL(0, service->Start(service));
    TEST_ASSERT_NOT_EQUAL(0, service->Initialize(service, NULL));
    TEST_ASSERT_NOT_EQUAL(0, service->Stop(service));
    removal_busy = false;
    TEST_ASSERT_EQUAL(0, service->Stop(service));
    fail_subscription = 0;
    TEST_ASSERT_EQUAL(0, service->Start(service));
    removal_calls = 0; removal_busy = true;
    TEST_ASSERT_NOT_EQUAL(0, service->Stop(service));
    TEST_ASSERT_EQUAL(3, removal_calls);
    TEST_ASSERT_NOT_EQUAL(0, service->Start(service));
    removal_busy = false;
    TEST_ASSERT_EQUAL(0, service->Stop(service));
    TEST_ASSERT_EQUAL(6, removal_calls);
    mocked_GetEventBus_fake.return_val = NULL;
    TEST_ASSERT_EQUAL(0, service->Start(service));
}

static jmp_buf worker_exit;
static unsigned worker_receive_count;
static BaseType_t receive_one_request(QueueHandle_t queue, void *out, TickType_t wait) {
    (void)queue; TEST_ASSERT_TRUE(wait > 0);
    if (worker_receive_count++) longjmp(worker_exit, 1);
    *(uint8_t *)out = 0;
    return pdTRUE;
}
void test_ConfigMgr_WorkerReportsSaveFailureAndRecovery(void) {
    TEST_ASSERT_NOT_NULL(pending_save_task);
    mocked_xQueueReceive_fake.custom_fake = receive_one_request;
    worker_receive_count = 0;
    mocked_nvs_open_fake.return_val = ESP_FAIL;
    if (setjmp(worker_exit) == 0) pending_save_task(pending_save_arg);
    TEST_ASSERT_NOT_EQUAL(0, mgr()->base.CheckHealth(&mgr()->base));
    worker_receive_count = 0;
    mocked_nvs_open_fake.return_val = ESP_OK;
    if (setjmp(worker_exit) == 0) pending_save_task(pending_save_arg);
    TEST_ASSERT_EQUAL(0, mgr()->base.CheckHealth(&mgr()->base));
    mocked_xQueueReceive_fake.custom_fake = acknowledge_stop;
}

static int32_t racing_stop_result;
static BaseType_t receive_during_stop(QueueHandle_t queue, void *out, TickType_t wait) {
    if (queue == (QueueHandle_t)3) return pdFALSE;
    if (wait > 0) {
        racing_stop_result = mgr()->base.Stop(&mgr()->base);
        *(uint8_t *)out = 0; return pdTRUE;
    }
    if (worker_receive_count++ == 0) { *(uint8_t *)out = 1; return pdTRUE; }
    return pdFALSE;
}
static void exit_parked_worker(TaskHandle_t task) { (void)task; longjmp(worker_exit, 1); }
void test_ConfigMgr_StopRaceDrainsAcceptedRequestsBeforeAck(void) {
    worker_receive_count = 0;
    RESET_FAKE(mocked_nvs_commit);
    mocked_nvs_open_fake.return_val = ESP_OK;
    mocked_nvs_commit_fake.return_val = ESP_OK;
    mocked_xQueueReceive_fake.custom_fake = receive_during_stop;
    mocked_vTaskSuspend_fake.custom_fake = exit_parked_worker;
    if (setjmp(worker_exit) == 0) pending_save_task(pending_save_arg);
    TEST_ASSERT_NOT_EQUAL(0, racing_stop_result);
    TEST_ASSERT_EQUAL(2, mocked_nvs_commit_fake.call_count);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, mgr()->SaveAsync(mgr(), NULL));
    RESET_FAKE(mocked_vTaskSuspend);
    mocked_xQueueReceive_fake.custom_fake = acknowledge_stop;
    TEST_ASSERT_EQUAL(0, mgr()->base.Stop(&mgr()->base));
    TEST_ASSERT_EQUAL(0, mgr()->base.Start(&mgr()->base));
}

void test_ConfigMgr_AllSharedAnimationSelectionsValidateThroughFieldAPI(void) {
    mocked_nvs_open_fake.return_val = ESP_OK;
    size_t count;
    const LEDAnimationCatalogEntry *entries = LEDAnimationCatalog_Entries(&count);
    for (size_t index = 0; index < count; ++index) {
        uint8_t internal = entries[index].builtin_id, external = entries[index].external_id;
        ConfigurationFieldUpdate fields[] = {
            {offsetof(HardwareConfigs, builtin_led_startup_anim), &internal, 1},
            {offsetof(HardwareConfigs, builtin_led_idle_anim), &internal, 1},
            {offsetof(HardwareConfigs, builtin_led_connecting_anim), &internal, 1},
            {offsetof(HardwareConfigs, builtin_led_fault_anim), &internal, 1},
            {offsetof(HardwareConfigs, ext_led_anim), &external, 1},
        };
        TEST_ASSERT_EQUAL(ESP_OK, mgr()->UpdateFields(mgr(), "hardware", fields, 5, true));
    }
    SystemSettings before, after;
    TEST_ASSERT_EQUAL(ESP_OK, mgr()->GetSnapshot(mgr(), &before));
    uint8_t invalid = 254;
    ConfigurationFieldUpdate field = {offsetof(HardwareConfigs, builtin_led_idle_anim), &invalid, 1};
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, mgr()->UpdateFields(mgr(), "hardware", &field, 1, true));
    invalid = RGBLED_ANIM_TEMPERATURE;
    field.offset = offsetof(HardwareConfigs, ext_led_anim);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, mgr()->UpdateFields(mgr(), "hardware", &field, 1, true));
    TEST_ASSERT_EQUAL(ESP_OK, mgr()->GetSnapshot(mgr(), &after));
    TEST_ASSERT_EQUAL_MEMORY(&before, &after, sizeof(before));
}

void test_ConfigMgr_LegacyTemperatureAnimationMigratesToRealColorMode(void) {
    mocked_nvs_open_fake.return_val = ESP_OK;
    mgr()->GetSettings(mgr())->hardware.version = 6;
    mgr()->GetSettings(mgr())->hardware.ext_led_anim = RGBLED_ANIM_TEMPERATURE;
    mgr()->GetSettings(mgr())->hardware.ext_led_mode = EXT_LED_MODE_ANIMATION;
    mgr()->GetSettings(mgr())->hardware.builtin_led_idle_anim = 254;
    TEST_ASSERT_EQUAL(ESP_OK, mgr()->Load(mgr(), "hardware"));
    TEST_ASSERT_EQUAL(RGBLED_ANIM_NONE, mgr()->GetSettings(mgr())->hardware.ext_led_anim);
    TEST_ASSERT_EQUAL(EXT_LED_MODE_TEMPERATURE, mgr()->GetSettings(mgr())->hardware.ext_led_mode);
    TEST_ASSERT_EQUAL(255, mgr()->GetSettings(mgr())->hardware.builtin_led_idle_anim);
    TEST_ASSERT_GREATER_THAN(0, mocked_nvs_commit_fake.call_count);
}

void test_ConfigMgr_AnimationMigrationCommitFailurePreservesOriginalSelection(void) {
    mocked_nvs_open_fake.return_val = ESP_OK;
    mocked_nvs_commit_fake.return_val = ESP_FAIL;
    SystemSettings *settings = mgr()->GetSettings(mgr());
    settings->hardware.version = 6;
    settings->hardware.ext_led_anim = RGBLED_ANIM_TEMPERATURE;
    settings->hardware.ext_led_mode = EXT_LED_MODE_ANIMATION;
    SystemSettings original = *settings;
    TEST_ASSERT_EQUAL(ESP_FAIL, mgr()->Load(mgr(), "hardware"));
    TEST_ASSERT_EQUAL_MEMORY(&original, settings, sizeof(original));
}

static unsigned print_snapshot_calls;
static esp_err_t print_snapshot_result;

static esp_err_t get_print_snapshot(IConfigurationManager *self, SystemSettings *output) {
    (void)self;
    ++print_snapshot_calls;
    memset(output, 0, sizeof(*output));
    return print_snapshot_result;
}

void test_ConfigMgr_PrintUsesOwnedSnapshotAndHandlesUnavailableSnapshot(void) {
    IConfigurationManager reader = {.GetSnapshot = get_print_snapshot};
    print_snapshot_calls = 0;
    print_snapshot_result = ESP_OK;
    /* GetSettings is deliberately absent: printing must not borrow live storage. */
    TEST_ASSERT_EQUAL(ESP_OK, SystemConfigs_Print(&reader));
    TEST_ASSERT_EQUAL_UINT(1, print_snapshot_calls);
    print_snapshot_result = ESP_FAIL;
    TEST_ASSERT_EQUAL(ESP_FAIL, SystemConfigs_Print(&reader));
    TEST_ASSERT_EQUAL_UINT(2, print_snapshot_calls);
    reader.GetSnapshot = NULL;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, SystemConfigs_Print(&reader));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, SystemConfigs_Print(NULL));
    TEST_ASSERT_EQUAL_UINT(2, print_snapshot_calls);
}


uint8_t ClimateController_GetHistory(ClimateTempHistoryEntry *out, uint8_t maxEntries) {
    (void)out; (void)maxEntries;
    return 0;
}
static esp_console_cmd_func_t climate_command;
static esp_err_t register_climate_command(IDebugConsole *self, const esp_console_cmd_t *cmd, CommandCategory category) {
    (void)self; (void)category;
    if (!strcmp(cmd->command, "CLIMATE")) climate_command = cmd->func;
    return ESP_OK;
}
void test_ConfigMgr_ClimateCliUsesRealFieldValidationAndSaveContract(void) {
    mocked_nvs_open_fake.return_val = ESP_OK;
    TEST_ASSERT_EQUAL(ESP_OK, s_mgr->ResetToDefaults(s_mgr, NULL));
    mocked_ConfigurationManager_GetInstance_fake.return_val = s_mgr;
    IDebugConsole console = {.RegisterCommandWithCategory = register_climate_command};
    ClimateCommands_Register(&console);
    TEST_ASSERT_NOT_NULL(climate_command);
    char *reset[] = {"CLIMATE", "RESET"};
    TEST_ASSERT_EQUAL_INT(0, climate_command(2, reset));
    char *set[] = {"CLIMATE", "SET", "HIGH", "19"};
    TEST_ASSERT_EQUAL_INT(1, climate_command(4, set));
    set[3] = "40";
    TEST_ASSERT_EQUAL_INT(0, climate_command(4, set));
    char *cal[] = {"CLIMATE", "CAL", "OBJ", "0.1"};
    TEST_ASSERT_EQUAL_INT(0, climate_command(4, cal));
    SystemSettings snapshot;
    TEST_ASSERT_EQUAL(ESP_OK, s_mgr->GetSnapshot(s_mgr, &snapshot));
    TEST_ASSERT_EQUAL_FLOAT(40, snapshot.control.relay_open_temp);
    TEST_ASSERT_EQUAL_FLOAT(0.1f, snapshot.calibration.ir_object_cal);
    mocked_nvs_open_fake.return_val = ESP_OK;
    mocked_nvs_commit_fake.return_val = ESP_FAIL;
    char *save[] = {"CLIMATE", "SAVE"};
    TEST_ASSERT_EQUAL_INT(1, climate_command(2, save));
    mocked_nvs_commit_fake.return_val = ESP_OK;
    TEST_ASSERT_EQUAL_INT(0, climate_command(2, save));
}


void test_ConfigFactoryLoadFailureDoesNotPublishAndRetriesOwnedResources(void) {
    mocked_nvs_open_fake.return_val = ESP_FAIL;
    TEST_ASSERT_NULL(ConfigurationManager_GetInstance());
    unsigned allocations = mocked_xSemaphoreCreateMutex_fake.call_count;
    mocked_nvs_open_fake.return_val = ESP_ERR_NVS_NOT_FOUND;
    s_mgr = ConfigurationManager_GetInstance();
    TEST_ASSERT_NOT_NULL(s_mgr);
    TEST_ASSERT_EQUAL(allocations,mocked_xSemaphoreCreateMutex_fake.call_count);
}

static void PrepareResetFailureFixture(void) {
    mocked_nvs_open_fake.return_val = ESP_OK;
    TEST_ASSERT_EQUAL(ESP_OK,mgr()->ResetToDefaults(mgr(),NULL));
    SystemSettings changed;
    TEST_ASSERT_EQUAL(ESP_OK,mgr()->GetSnapshot(mgr(),&changed));
    changed.control.threshold_high=99.0f;
    changed.system.power_profile=2;
    TEST_ASSERT_EQUAL(ESP_OK,mgr()->UpdateSettings(mgr(),&changed,sizeof(changed)));
}
void test_ConfigResetCommitFailurePreservesLiveSettingsAndDirtyFlag(void) {
    PrepareResetFailureFixture(); SystemSettings before,after;
    TEST_ASSERT_EQUAL(ESP_OK,mgr()->GetSnapshot(mgr(),&before));
    TEST_ASSERT_TRUE(mgr()->IsDirty(mgr(),"control"));
    mocked_nvs_commit_fake.return_val=ESP_FAIL;
    TEST_ASSERT_EQUAL(ESP_FAIL,mgr()->ResetToDefaults(mgr(),"control"));
    TEST_ASSERT_EQUAL(ESP_OK,mgr()->GetSnapshot(mgr(),&after));
    TEST_ASSERT_EQUAL_MEMORY(&before,&after,sizeof(before));
    TEST_ASSERT_TRUE(mgr()->IsDirty(mgr(),"control"));
}
static unsigned reset_blob_calls,reset_publish_calls;
static SystemSettings reset_published_settings;
static esp_err_t FailFirstResetBlob(nvs_handle_t handle,const char *key,const void *value,size_t size) {
    (void)handle;(void)key;(void)value;(void)size; return reset_blob_calls++ == 0 ? ESP_FAIL : ESP_OK;
}
static int32_t CaptureResetPublication(BusEvent event) {
    ++reset_publish_calls;
    TEST_ASSERT_EQUAL(sizeof(reset_published_settings),event.dataSize);
    memcpy(&reset_published_settings,event.data,sizeof(reset_published_settings));return 0;
}
void test_ConfigResetPartialCommitRetainsFailedBlockAndPublishesAppliedBlocks(void) {
    PrepareResetFailureFixture(); reset_blob_calls=reset_publish_calls=0;
    IEventBus bus={.Publish=CaptureResetPublication}; mocked_GetEventBus_fake.return_val=&bus;
    mocked_nvs_set_blob_fake.custom_fake=FailFirstResetBlob;
    esp_err_t result=mgr()->ResetToDefaults(mgr(),NULL);
    mocked_nvs_set_blob_fake.custom_fake=NULL;mocked_GetEventBus_fake.return_val=NULL;
    TEST_ASSERT_EQUAL(ESP_FAIL,result);
    TEST_ASSERT_EQUAL(CONFIG_STATUS_BLOCK_CAPACITY,reset_blob_calls);TEST_ASSERT_EQUAL(1,reset_publish_calls);
    TEST_ASSERT_EQUAL(2,reset_published_settings.system.power_profile);
    TEST_ASSERT_FLOAT_WITHIN(0.001f,DEFAULT_THRESHOLD_HIGH,reset_published_settings.control.threshold_high);
    TEST_ASSERT_TRUE(mgr()->IsDirty(mgr(),"system"));TEST_ASSERT_FALSE(mgr()->IsDirty(mgr(),"control"));
}
void test_ConfigResetLockFailureAndUnknownLoadHaveNoSideEffects(void) {
    PrepareResetFailureFixture(); SystemSettings before,after;
    TEST_ASSERT_EQUAL(ESP_OK,mgr()->GetSnapshot(mgr(),&before));
    unsigned writes=mocked_nvs_set_blob_fake.call_count,gives=mocked_xSemaphoreGive_fake.call_count;
    mocked_xSemaphoreTake_fake.return_val=pdFALSE;
    esp_err_t result=mgr()->ResetToDefaults(mgr(),"control");
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE,result);
    TEST_ASSERT_EQUAL(gives,mocked_xSemaphoreGive_fake.call_count);
    mocked_xSemaphoreTake_fake.return_val=pdTRUE;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,mgr()->Load(mgr(),"unknown"));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,mgr()->ResetToDefaults(mgr(),"unknown"));
    TEST_ASSERT_EQUAL(writes,mocked_nvs_set_blob_fake.call_count);
    TEST_ASSERT_EQUAL(ESP_OK,mgr()->GetSnapshot(mgr(),&after));TEST_ASSERT_EQUAL_MEMORY(&before,&after,sizeof(before));
}
void test_ConfigStatusCopiesMetadataAndPreservesOutputOnFailure(void) {
    PrepareResetFailureFixture(); ConfigurationStatus snapshot;
    TEST_ASSERT_EQUAL(ESP_OK,mgr()->GetStatus(mgr(),&snapshot));
    TEST_ASSERT_EQUAL(CONFIG_STATUS_BLOCK_CAPACITY,snapshot.block_count);
    TEST_ASSERT_EQUAL_STRING("system",snapshot.blocks[0].name);
    TEST_ASSERT_EQUAL_STRING("control",snapshot.blocks[1].name);
    TEST_ASSERT_TRUE(snapshot.blocks[0].dirty);TEST_ASSERT_TRUE(snapshot.blocks[1].dirty);
    TEST_ASSERT_EQUAL(ESP_OK,mgr()->Save(mgr(),"control"));
    TEST_ASSERT_TRUE(snapshot.blocks[1].dirty);
    ConfigurationStatus output,before;memset(&output,0xA5,sizeof(output));memcpy(&before,&output,sizeof(before));
    unsigned gives=mocked_xSemaphoreGive_fake.call_count;
    mocked_xSemaphoreTake_fake.return_val=pdFALSE;
    TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT,mgr()->GetStatus(mgr(),&output));
    TEST_ASSERT_EQUAL_MEMORY(&before,&output,sizeof(output));TEST_ASSERT_EQUAL(gives,mocked_xSemaphoreGive_fake.call_count);
    mocked_xSemaphoreTake_fake.return_val=pdTRUE;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,mgr()->GetStatus(NULL,&output));
    TEST_ASSERT_EQUAL_MEMORY(&before,&output,sizeof(output));
}
void test_ConfigConsoleMetadataFailureDoesNotReportCleanOrVersionZero(void) {
    mocked_ServiceLocator_GetService_fake.return_val=(IService *)mgr();
    mocked_xSemaphoreTake_fake.return_val=pdFALSE;
    char *args[] = {"config"};
    unsigned takes = mocked_xSemaphoreTake_fake.call_count;
    int status=cmd_config_status(1,args),version=cmd_config_version(1,args);
    mocked_xSemaphoreTake_fake.return_val=pdTRUE;mocked_ServiceLocator_GetService_fake.return_val=NULL;
    TEST_ASSERT_NOT_EQUAL(0,status);TEST_ASSERT_NOT_EQUAL(0,version);
    TEST_ASSERT_EQUAL(takes + 2, mocked_xSemaphoreTake_fake.call_count);
}
static unsigned notification_attempts;
static unsigned notification_failures;
static bool notification_reentrant_update;
static SystemSettings notification_snapshot;

static int32_t notification_publish(BusEvent event) {
    notification_attempts++;
    memcpy(&notification_snapshot, event.data, sizeof(notification_snapshot));
    if (notification_reentrant_update) {
        notification_reentrant_update = false;
        uint8_t value = 2;
        ConfigurationFieldUpdate field = {offsetof(SystemBaseConfigs, power_profile), &value, sizeof(value)};
        TEST_ASSERT_EQUAL(ESP_OK, mgr()->UpdateFields(mgr(), "system", &field, 1, false));
    }
    if (notification_failures) { notification_failures--; return -1; }
    return 0;
}

static BaseType_t finish_notification_iteration(QueueHandle_t queue, void *out, TickType_t wait) {
    (void)queue; (void)out;
    TEST_ASSERT_TRUE(wait > 0);
    longjmp(worker_exit, 1);
    return pdFALSE;
}

static void run_notification_iteration(void) {
    mocked_xQueueReceive_fake.custom_fake = finish_notification_iteration;
    if (setjmp(worker_exit) == 0) pending_save_task(pending_save_arg);
    mocked_xQueueReceive_fake.custom_fake = acknowledge_stop;
}

static void set_notification_profile(uint8_t value) {
    ConfigurationFieldUpdate field = {offsetof(SystemBaseConfigs, power_profile), &value, sizeof(value)};
    TEST_ASSERT_EQUAL(ESP_OK, mgr()->UpdateFields(mgr(), "system", &field, 1, false));
}

void test_ConfigNotificationRejectedUpdateRetriesLatestSnapshot(void) {
    IEventBus bus = {.Publish = notification_publish};
    mocked_GetEventBus_fake.return_val = &bus;
    notification_attempts = 0; notification_failures = 2; notification_reentrant_update = false;
    set_notification_profile(0);
    set_notification_profile(1);
    ConfigurationStatus status;
    TEST_ASSERT_EQUAL(ESP_OK, mgr()->GetStatus(mgr(), &status));
    TEST_ASSERT_TRUE(status.notification_pending);
    TEST_ASSERT_EQUAL(2, notification_attempts);
    run_notification_iteration();
    TEST_ASSERT_EQUAL(3, notification_attempts);
    TEST_ASSERT_EQUAL(1, notification_snapshot.system.power_profile);
    TEST_ASSERT_EQUAL(ESP_OK, mgr()->GetStatus(mgr(), &status));
    TEST_ASSERT_FALSE(status.notification_pending);
    run_notification_iteration();
    TEST_ASSERT_EQUAL(3, notification_attempts);
    mocked_GetEventBus_fake.return_val = NULL;
}

void test_ConfigNotificationMissingCallbackRetainsUntilWorkerRecovery(void) {
    IEventBus bus = {0};
    mocked_GetEventBus_fake.return_val = &bus;
    set_notification_profile(0);
    ConfigurationStatus status;
    TEST_ASSERT_EQUAL(ESP_OK, mgr()->GetStatus(mgr(), &status));
    TEST_ASSERT_TRUE(status.notification_pending);
    notification_attempts = notification_failures = 0; notification_reentrant_update = false;
    bus.Publish = notification_publish;
    run_notification_iteration();
    TEST_ASSERT_EQUAL(1, notification_attempts);
    TEST_ASSERT_EQUAL(0, notification_snapshot.system.power_profile);
    TEST_ASSERT_EQUAL(ESP_OK, mgr()->GetStatus(mgr(), &status));
    TEST_ASSERT_FALSE(status.notification_pending);
    mocked_GetEventBus_fake.return_val = NULL;
}

void test_ConfigNotificationConcurrentUpdateDoesNotRecursivelyPublishOrLoseLatest(void) {
    IEventBus bus = {.Publish = notification_publish};
    mocked_GetEventBus_fake.return_val = &bus;
    notification_attempts = notification_failures = 0; notification_reentrant_update = false;
    set_notification_profile(1);
    notification_attempts = notification_failures = 0; notification_reentrant_update = true;
    set_notification_profile(0);
    TEST_ASSERT_EQUAL(1, notification_attempts);
    TEST_ASSERT_EQUAL(0, notification_snapshot.system.power_profile);
    ConfigurationStatus status;
    TEST_ASSERT_EQUAL(ESP_OK, mgr()->GetStatus(mgr(), &status));
    TEST_ASSERT_TRUE(status.notification_pending);
    run_notification_iteration();
    TEST_ASSERT_EQUAL(2, notification_attempts);
    TEST_ASSERT_EQUAL(2, notification_snapshot.system.power_profile);
    TEST_ASSERT_EQUAL(ESP_OK, mgr()->GetStatus(mgr(), &status));
    TEST_ASSERT_FALSE(status.notification_pending);
    mocked_GetEventBus_fake.return_val = NULL;
}

void test_ConfigNotificationPendingSurvivesStopAndRestartWithoutExtraTasks(void) {
    IEventBus bus = {.Publish = notification_publish};
    mocked_GetEventBus_fake.return_val = &bus;
    notification_attempts = 0; notification_failures = 1; notification_reentrant_update = false;
    unsigned creations = mocked_xTaskCreate_fake.call_count;
    set_notification_profile(0);
    TEST_ASSERT_EQUAL(creations, mocked_xTaskCreate_fake.call_count);
    mocked_GetEventBus_fake.return_val = NULL;
    TEST_ASSERT_EQUAL(0, mgr()->base.Stop(&mgr()->base));
    ConfigurationStatus status;
    TEST_ASSERT_EQUAL(ESP_OK, mgr()->GetStatus(mgr(), &status));
    TEST_ASSERT_TRUE(status.notification_pending);
    TEST_ASSERT_EQUAL(0, mgr()->base.Start(&mgr()->base));
    mocked_GetEventBus_fake.return_val = &bus;
    run_notification_iteration();
    TEST_ASSERT_EQUAL(2, notification_attempts);
    TEST_ASSERT_EQUAL(ESP_OK, mgr()->GetStatus(mgr(), &status));
    TEST_ASSERT_FALSE(status.notification_pending);
    mocked_GetEventBus_fake.return_val = NULL;
}

static uint8_t load_boundary_blob[sizeof(SystemSettings)];
static size_t load_boundary_size;
static esp_err_t load_boundary_result;
static esp_err_t load_boundary(nvs_handle_t handle, const char *key, void *out, size_t *size) {
    (void)handle; (void)key;
    size_t copied = load_boundary_size < *size ? load_boundary_size : *size;
    memcpy(out, load_boundary_blob, copied);
    *size = load_boundary_size;
    return load_boundary_result;
}

static void prepare_load_boundary(const void *blob, size_t size, esp_err_t result) {
    memcpy(load_boundary_blob, blob, size);
    load_boundary_size = size;
    load_boundary_result = result;
    mocked_nvs_open_fake.return_val = ESP_OK;
    mocked_nvs_get_blob_fake.custom_fake = load_boundary;
}

void test_ConfigLoadShortSuccessfulReadRejectsWithoutPartialMutation(void) {
    SystemSettings before, after;
    TEST_ASSERT_EQUAL(ESP_OK, mgr()->GetSnapshot(mgr(), &before));
    uint32_t short_blob = 0xffffffff;
    prepare_load_boundary(&short_blob, sizeof(short_blob), ESP_OK);
    esp_err_t result = mgr()->Load(mgr(), "system");
    mocked_nvs_get_blob_fake.custom_fake = NULL;
    TEST_ASSERT_EQUAL(ESP_FAIL, result);
    TEST_ASSERT_EQUAL(ESP_OK, mgr()->GetSnapshot(mgr(), &after));
    TEST_ASSERT_EQUAL_MEMORY(&before, &after, sizeof(before));
    TEST_ASSERT_EQUAL(0, mocked_nvs_set_blob_fake.call_count);
}

void test_ConfigLoadSdkPartialErrorPreservesAllSettingsAndMetadata(void) {
    SystemSettings before, after;
    ConfigurationStatus status_before, status_after;
    TEST_ASSERT_EQUAL(ESP_OK, mgr()->GetSnapshot(mgr(), &before));
    TEST_ASSERT_EQUAL(ESP_OK, mgr()->GetStatus(mgr(), &status_before));
    uint32_t partial_blob = 0xffffffff;
    prepare_load_boundary(&partial_blob, sizeof(partial_blob), ESP_ERR_NVS_INVALID_LENGTH);
    esp_err_t result = mgr()->Load(mgr(), "system");
    mocked_nvs_get_blob_fake.custom_fake = NULL;
    TEST_ASSERT_EQUAL(ESP_FAIL, result);
    TEST_ASSERT_EQUAL(ESP_OK, mgr()->GetSnapshot(mgr(), &after));
    TEST_ASSERT_EQUAL(ESP_OK, mgr()->GetStatus(mgr(), &status_after));
    TEST_ASSERT_EQUAL_MEMORY(&before, &after, sizeof(before));
    TEST_ASSERT_EQUAL_MEMORY(&status_before, &status_after, sizeof(status_before));
}

void test_ConfigLoadNonFiniteCalibrationRollsBackWithoutNotification(void) {
    SystemSettings before, after;
    TEST_ASSERT_EQUAL(ESP_OK, mgr()->GetSnapshot(mgr(), &before));
    CalibrationConfigs blob = before.calibration;
    blob.ir_object_cal = NAN;
    IEventBus bus = {.Publish = notification_publish};
    mocked_GetEventBus_fake.return_val = &bus;
    notification_attempts = notification_failures = 0; notification_reentrant_update = false;
    prepare_load_boundary(&blob, sizeof(blob), ESP_OK);
    esp_err_t result = mgr()->Load(mgr(), "calibration");
    mocked_nvs_get_blob_fake.custom_fake = NULL;
    mocked_GetEventBus_fake.return_val = NULL;
    TEST_ASSERT_EQUAL(ESP_FAIL, result);
    TEST_ASSERT_EQUAL(ESP_OK, mgr()->GetSnapshot(mgr(), &after));
    TEST_ASSERT_EQUAL_MEMORY(&before, &after, sizeof(before));
    TEST_ASSERT_EQUAL(0, notification_attempts);
    TEST_ASSERT_EQUAL(0, mocked_nvs_set_blob_fake.call_count);
}

void test_ConfigLoadRejectsInvalidCandidateBeforeMigrationCommit(void) {
    SystemSettings before, after;
    TEST_ASSERT_EQUAL(ESP_OK, mgr()->GetSnapshot(mgr(), &before));
    SystemBaseConfigs blob = before.system;
    blob.version = 2;
    blob.zigbee_tx_profile = 255;
    blob.temp_unit = 255;
    prepare_load_boundary(&blob, sizeof(blob), ESP_OK);
    esp_err_t result = mgr()->Load(mgr(), "system");
    mocked_nvs_get_blob_fake.custom_fake = NULL;
    TEST_ASSERT_EQUAL(ESP_FAIL, result);
    TEST_ASSERT_EQUAL(0, mocked_nvs_set_blob_fake.call_count);
    TEST_ASSERT_EQUAL(0, mocked_nvs_commit_fake.call_count);
    TEST_ASSERT_EQUAL(ESP_OK, mgr()->GetSnapshot(mgr(), &after));
    TEST_ASSERT_EQUAL_MEMORY(&before, &after, sizeof(before));
}

void test_ConfigRuntimeLoadPublishesChangedSnapshotOnlyOnce(void) {
    SystemSettings before;
    TEST_ASSERT_EQUAL(ESP_OK, mgr()->GetSnapshot(mgr(), &before));
    SystemBaseConfigs blob = before.system;
    blob.oled_timeout_sec = blob.oled_timeout_sec == 73 ? 74 : 73;
    IEventBus bus = {.Publish = notification_publish};
    mocked_GetEventBus_fake.return_val = &bus;
    notification_attempts = notification_failures = 0; notification_reentrant_update = false;
    prepare_load_boundary(&blob, sizeof(blob), ESP_OK);
    esp_err_t first = mgr()->Load(mgr(), "system");
    esp_err_t second = mgr()->Load(mgr(), "system");
    mocked_nvs_get_blob_fake.custom_fake = NULL;
    mocked_GetEventBus_fake.return_val = NULL;
    TEST_ASSERT_EQUAL(ESP_OK, first);
    TEST_ASSERT_EQUAL(ESP_OK, second);
    TEST_ASSERT_EQUAL(1, notification_attempts);
    TEST_ASSERT_EQUAL(blob.oled_timeout_sec, notification_snapshot.system.oled_timeout_sec);
    TEST_ASSERT_EQUAL_MEMORY(&before.hardware, &notification_snapshot.hardware, sizeof(before.hardware));
    TEST_ASSERT_EQUAL(ESP_OK, mgr()->UpdateSettings(mgr(), &before, sizeof(before)));
}

void test_ConfigConsoleMalformedCommandsCannotReachPersistence(void) {
    int (*handlers[])(int, char **) = {cmd_config_show, cmd_config_save, cmd_config_load,
        cmd_config_reset, cmd_config_validate, cmd_config_version, cmd_config_status, cmd_config_diff};
    char *extra[] = {"config", "system", "unexpected"};
    char *missing[] = {"config", NULL};
    char *empty[] = {"config", ""};
    char *missing_command[] = {NULL};
    unsigned opens = mocked_nvs_open_fake.call_count;
    unsigned takes = mocked_xSemaphoreTake_fake.call_count;
    for (size_t i = 0; i < sizeof(handlers)/sizeof(handlers[0]); ++i) {
        TEST_ASSERT_NOT_EQUAL(0, handlers[i](3, extra));
        TEST_ASSERT_NOT_EQUAL(0, handlers[i](0, NULL));
        TEST_ASSERT_NOT_EQUAL(0, handlers[i](-1, NULL));
        TEST_ASSERT_NOT_EQUAL(0, handlers[i](1, missing_command));
        TEST_ASSERT_NOT_EQUAL(0, handlers[i](2, missing));
        TEST_ASSERT_NOT_EQUAL(0, handlers[i](2, empty));
    }
    TEST_ASSERT_EQUAL(opens, mocked_nvs_open_fake.call_count);
    TEST_ASSERT_EQUAL(takes, mocked_xSemaphoreTake_fake.call_count);
}

void test_ConfigConsoleMissingCallbacksAreFailures(void) {
    IConfigurationManager empty = {0};
    mocked_ServiceLocator_GetService_fake.return_val = &empty.base;
    char *args[] = {"config"};
    TEST_ASSERT_NOT_EQUAL(0, cmd_config_show(1,args));
    TEST_ASSERT_NOT_EQUAL(0, cmd_config_save(1,args));
    TEST_ASSERT_NOT_EQUAL(0, cmd_config_load(1,args));
    TEST_ASSERT_NOT_EQUAL(0, cmd_config_reset(1,args));
    TEST_ASSERT_NOT_EQUAL(0, cmd_config_validate(1,args));
    TEST_ASSERT_NOT_EQUAL(0, cmd_config_version(1,args));
    TEST_ASSERT_NOT_EQUAL(0, cmd_config_status(1,args));
    TEST_ASSERT_NOT_EQUAL(0, cmd_config_diff(1,args));
    mocked_ServiceLocator_GetService_fake.return_val = NULL;
}

void test_ConfigConsoleShowPropagatesSnapshotFailure(void) {
    IConfigurationManager reader = {.GetSnapshot = get_print_snapshot};
    mocked_ServiceLocator_GetService_fake.return_val = &reader.base;
    print_snapshot_calls = 0;
    print_snapshot_result = ESP_ERR_TIMEOUT;
    char *args[] = {"config.show"};
    int result = cmd_config_show(1,args);
    mocked_ServiceLocator_GetService_fake.return_val = NULL;
    TEST_ASSERT_NOT_EQUAL(0, result);
    TEST_ASSERT_EQUAL(1, print_snapshot_calls);
}

void test_ConfigConsoleUnknownBlocksLeaveLiveAndNvsUntouched(void) {
    SystemSettings before, after;
    TEST_ASSERT_EQUAL(ESP_OK, mgr()->GetSnapshot(mgr(), &before));
    mocked_ServiceLocator_GetService_fake.return_val = &mgr()->base;
    char *args[] = {"config", "not_a_block"};
    unsigned opens = mocked_nvs_open_fake.call_count;
    int save = cmd_config_save(2,args), load = cmd_config_load(2,args), reset = cmd_config_reset(2,args);
    mocked_ServiceLocator_GetService_fake.return_val = NULL;
    TEST_ASSERT_NOT_EQUAL(0, save);
    TEST_ASSERT_NOT_EQUAL(0, load);
    TEST_ASSERT_NOT_EQUAL(0, reset);
    TEST_ASSERT_EQUAL(opens, mocked_nvs_open_fake.call_count);
    TEST_ASSERT_EQUAL(ESP_OK, mgr()->GetSnapshot(mgr(), &after));
    TEST_ASSERT_EQUAL_MEMORY(&before, &after, sizeof(before));
}

void test_ConfigModeFieldsRejectSentinelsBeforePersistence(void) {
    SystemSettings before, after;
    TEST_ASSERT_EQUAL(ESP_OK,mgr()->GetSnapshot(mgr(),&before));
    uint8_t display = SYSTEM_DISPLAY_MODE_COUNT, led = BUILTIN_LED_MODE_COUNT;
    ConfigurationFieldUpdate display_field = {offsetof(SystemBaseConfigs,display_mode),&display,1};
    ConfigurationFieldUpdate led_field = {offsetof(HardwareConfigs,builtin_led_anim_mode),&led,1};
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,mgr()->UpdateFields(mgr(),"system",&display_field,1,true));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,mgr()->UpdateFields(mgr(),"hardware",&led_field,1,true));
    TEST_ASSERT_EQUAL(0,mocked_nvs_set_blob_fake.call_count);
    TEST_ASSERT_EQUAL(ESP_OK,mgr()->GetSnapshot(mgr(),&after));
    TEST_ASSERT_EQUAL_MEMORY(&before,&after,sizeof(before));
}

void test_ConfigBooleanFieldRejectsNoncanonicalStorageWithoutMutation(void) {
    SystemSettings before, after;
    TEST_ASSERT_EQUAL(ESP_OK,mgr()->GetSnapshot(mgr(),&before));
    uint8_t invalid = 2;
    const struct {const char *block;size_t offset;} fields[] = {
        {"system",offsetof(SystemBaseConfigs,system_enabled)},
        {"control",offsetof(ControlConfigs,fan_manual_mode)},
        {"control",offsetof(ControlConfigs,fan_manual_state)},
        {"hardware",offsetof(HardwareConfigs,builtin_led_state)},
        {"hardware",offsetof(HardwareConfigs,ext_led_state)}};
    for(size_t i=0;i<sizeof(fields)/sizeof(fields[0]);++i) {
        ConfigurationFieldUpdate field={fields[i].offset,&invalid,sizeof(invalid)};
        TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,mgr()->UpdateFields(mgr(),fields[i].block,&field,1,true));
    }
    TEST_ASSERT_EQUAL(0,mocked_nvs_set_blob_fake.call_count);
    TEST_ASSERT_EQUAL(ESP_OK,mgr()->GetSnapshot(mgr(),&after));
    TEST_ASSERT_EQUAL_MEMORY(&before,&after,sizeof(before));
}

void test_ConfigThermalAndAlertBoundsRejectFiniteOutliers(void) {
    SystemSettings before, after;
    TEST_ASSERT_EQUAL(ESP_OK,mgr()->GetSnapshot(mgr(),&before));
    float high=TEMP_THRESHOLD_MAX+1, warning=TEMP_THRESHOLD_MIN-1;
    ConfigurationFieldUpdate high_field={offsetof(ControlConfigs,threshold_high),&high,sizeof(high)};
    ConfigurationFieldUpdate warning_field={offsetof(ThermalConfigs,cpu_temp_warning),&warning,sizeof(warning)};
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,mgr()->UpdateFields(mgr(),"control",&high_field,1,true));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,mgr()->UpdateFields(mgr(),"thermal",&warning_field,1,true));
    TEST_ASSERT_EQUAL(0,mocked_nvs_set_blob_fake.call_count);
    TEST_ASSERT_EQUAL(ESP_OK,mgr()->GetSnapshot(mgr(),&after));
    TEST_ASSERT_EQUAL_MEMORY(&before,&after,sizeof(before));
    float low=TEMP_THRESHOLD_MIN;high=TEMP_THRESHOLD_MAX;
    ConfigurationFieldUpdate bounds[]={high_field,{offsetof(ControlConfigs,threshold_low),&low,sizeof(low)}};
    TEST_ASSERT_EQUAL(ESP_OK,mgr()->UpdateFields(mgr(),"control",bounds,2,false));
    TEST_ASSERT_EQUAL(ESP_OK,mgr()->UpdateSettings(mgr(),&before,sizeof(before)));
}

static const struct { const char *ns; size_t offset, size; } verify_blocks[] = {
    {NS_CONFIG_SYSTEM, offsetof(SystemSettings, system), sizeof(SystemBaseConfigs)},
    {NS_CONFIG_CONTROL, offsetof(SystemSettings, control), sizeof(ControlConfigs)},
    {NS_CONFIG_HARDWARE, offsetof(SystemSettings, hardware), sizeof(HardwareConfigs)},
    {NS_CONFIG_CALIB, offsetof(SystemSettings, calibration), sizeof(CalibrationConfigs)},
    {NS_CONFIG_THERMAL, offsetof(SystemSettings, thermal), sizeof(ThermalConfigs)},
    {NS_CONFIG_ZIGBEE, offsetof(SystemSettings, zigbee_reporting), sizeof(ZigbeeReportingConfigs)},
    {NS_CONFIG_LOCALIZATION, offsetof(SystemSettings, localization), sizeof(LocalizationConfigs)}
};
static SystemSettings verify_stored;
static int verify_bad_size, verify_read_failure;
static esp_err_t verify_open(const char *ns, nvs_open_mode_t mode, nvs_handle_t *handle) {
    TEST_ASSERT_EQUAL(NVS_READONLY, mode);
    for (size_t i = 0; i < CONFIG_STATUS_BLOCK_CAPACITY; ++i) {
        if (strcmp(ns, verify_blocks[i].ns) == 0) { *handle = (nvs_handle_t)(i + 1); return ESP_OK; }
    }
    return ESP_ERR_NVS_NOT_FOUND;
}
static esp_err_t verify_read(nvs_handle_t handle, const char *key, void *out, size_t *size) {
    TEST_ASSERT_EQUAL_STRING(NS_CONFIG_KEY, key);
    TEST_ASSERT_TRUE(handle >= 1 && handle <= CONFIG_STATUS_BLOCK_CAPACITY);
    size_t i = (size_t)handle - 1;
    TEST_ASSERT_EQUAL(verify_blocks[i].size, *size);
    if ((int)i == verify_read_failure) return ESP_FAIL;
    memcpy(out, (const uint8_t *)&verify_stored + verify_blocks[i].offset, *size);
    if ((int)i == verify_bad_size) --*size;
    return ESP_OK;
}
static void prepare_default_verification(void) {
    mocked_nvs_open_fake.return_val = ESP_OK;
    TEST_ASSERT_EQUAL(ESP_OK, mgr()->ResetToDefaults(mgr(), NULL));
    TEST_ASSERT_EQUAL(ESP_OK, mgr()->GetSnapshot(mgr(), &verify_stored));
    verify_bad_size = verify_read_failure = -1;
    RESET_FAKE(mocked_nvs_open);
    RESET_FAKE(mocked_nvs_get_blob);
    mocked_nvs_open_fake.custom_fake = verify_open;
    mocked_nvs_get_blob_fake.custom_fake = verify_read;
}
void test_ConfigDefaultVerificationReadsAllPersistedBlocksWithoutWrites(void) {
    prepare_default_verification();
    SystemSettings out;
    unsigned writes = mocked_nvs_set_blob_fake.call_count, commits = mocked_nvs_commit_fake.call_count;
    TEST_ASSERT_EQUAL(ESP_OK, mgr()->VerifyDefaults(mgr(), &out));
    TEST_ASSERT_EQUAL(CONFIG_STATUS_BLOCK_CAPACITY, mocked_nvs_get_blob_fake.call_count);
    TEST_ASSERT_EQUAL(writes, mocked_nvs_set_blob_fake.call_count);
    TEST_ASSERT_EQUAL(commits, mocked_nvs_commit_fake.call_count);
    TEST_ASSERT_EQUAL(verify_stored.system.power_profile, out.system.power_profile);
}
void test_ConfigDefaultVerificationRejectsEveryStoredSchemaMismatch(void) {
    prepare_default_verification();
    SystemSettings before, out;
    memset(&before, 0x5a, sizeof(before));
    for (size_t i = 0; i < CONFIG_STATUS_BLOCK_CAPACITY; ++i) {
        uint8_t *version = (uint8_t *)&verify_stored + verify_blocks[i].offset + verify_blocks[i].size - sizeof(uint32_t);
        *version ^= 1;
        out = before;
        TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, mgr()->VerifyDefaults(mgr(), &out));
        TEST_ASSERT_EQUAL_MEMORY(&before, &out, sizeof(out));
        *version ^= 1;
    }
}
void test_ConfigDefaultVerificationRejectsStoredValueAndLiveChanges(void) {
    prepare_default_verification();
    SystemSettings out;
    verify_stored.calibration.ir_object_cal += 0.1f;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, mgr()->VerifyDefaults(mgr(), &out));
    verify_stored.calibration.ir_object_cal -= 0.1f;
    SystemSettings changed = verify_stored;
    changed.calibration.ir_ambient_cal += 0.1f;
    TEST_ASSERT_EQUAL(ESP_OK, mgr()->UpdateSettings(mgr(), &changed, sizeof(changed)));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, mgr()->VerifyDefaults(mgr(), &out));
}
void test_ConfigDefaultVerificationRejectsReadErrorsLengthsAndLockFailure(void) {
    prepare_default_verification();
    SystemSettings before, out;
    memset(&before, 0x5a, sizeof(before));
    for (int i = 0; i < CONFIG_STATUS_BLOCK_CAPACITY; ++i) {
        verify_bad_size = i; out = before;
        TEST_ASSERT_EQUAL(ESP_ERR_INVALID_SIZE, mgr()->VerifyDefaults(mgr(), &out));
        TEST_ASSERT_EQUAL_MEMORY(&before, &out, sizeof(out));
        verify_bad_size = -1; verify_read_failure = i;
        TEST_ASSERT_EQUAL(ESP_FAIL, mgr()->VerifyDefaults(mgr(), &out));
        TEST_ASSERT_EQUAL_MEMORY(&before, &out, sizeof(out));
        verify_read_failure = -1;
    }
    mocked_xSemaphoreTake_fake.return_val = pdFALSE;
    TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT, mgr()->VerifyDefaults(mgr(), &out));
    TEST_ASSERT_EQUAL_MEMORY(&before, &out, sizeof(out));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, mgr()->VerifyDefaults(NULL, &out));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, mgr()->VerifyDefaults(mgr(), NULL));
}

void test_ConfigFactoryGuardRejectsForeignMutationsAndReleaseWithoutHoldingMutex(void) {
    prepare_default_verification();
    TaskHandle_t previous = mocked_xTaskGetCurrentTaskHandle_fake.return_val;
    mocked_xTaskGetCurrentTaskHandle_fake.return_val = (TaskHandle_t)8;
    TEST_ASSERT_EQUAL(ESP_OK, mgr()->SetFactoryResetGuard(mgr(), true));
    TEST_ASSERT_TRUE(mgr()->IsMutationAllowed(mgr()));
    mocked_xTaskGetCurrentTaskHandle_fake.return_val = (TaskHandle_t)9;
    TEST_ASSERT_FALSE(mgr()->IsMutationAllowed(mgr()));
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FINISHED, mgr()->SetFactoryResetGuard(mgr(), false));
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FINISHED, mgr()->SetFactoryResetGuard(mgr(), true));
    SystemSettings candidate = verify_stored;
    float calibration = 0.1f;
    ConfigurationFieldUpdate field = {offsetof(CalibrationConfigs, ir_object_cal), &calibration, sizeof(calibration)};
    unsigned reads = mocked_nvs_get_blob_fake.call_count, writes = mocked_nvs_set_blob_fake.call_count;
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FINISHED, mgr()->UpdateFields(mgr(), "calibration", &field, 1, true));
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FINISHED, mgr()->UpdateSettings(mgr(), &candidate, sizeof(candidate)));
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FINISHED, mgr()->ResetToDefaults(mgr(), NULL));
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FINISHED, mgr()->Save(mgr(), NULL));
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FINISHED, mgr()->SaveAsync(mgr(), NULL));
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FINISHED, mgr()->Load(mgr(), NULL));
    TEST_ASSERT_EQUAL(reads, mocked_nvs_get_blob_fake.call_count);
    TEST_ASSERT_EQUAL(writes, mocked_nvs_set_blob_fake.call_count);
    TEST_ASSERT_EQUAL(ESP_OK, mgr()->GetSnapshot(mgr(), &candidate));
    TEST_ASSERT_EQUAL_MEMORY(&verify_stored, &candidate, sizeof(candidate));
    mocked_xTaskGetCurrentTaskHandle_fake.return_val = (TaskHandle_t)8;
    TEST_ASSERT_EQUAL(ESP_OK, mgr()->SetFactoryResetGuard(mgr(), false));
    mocked_xTaskGetCurrentTaskHandle_fake.return_val = (TaskHandle_t)9;
    TEST_ASSERT_TRUE(mgr()->IsMutationAllowed(mgr()));
    mocked_xTaskGetCurrentTaskHandle_fake.return_val = previous;
}
void test_ConfigDefaultVerificationIgnoresSchemaPadding(void) {
    prepare_default_verification();
    size_t padding = offsetof(SystemBaseConfigs, temp_unit) + sizeof(verify_stored.system.temp_unit);
    TEST_ASSERT_TRUE(padding < offsetof(SystemBaseConfigs, telemetry_interval_ms));
    ((uint8_t *)&verify_stored.system)[padding] ^= 0xff;
    SystemSettings out;
    TEST_ASSERT_EQUAL(ESP_OK, mgr()->VerifyDefaults(mgr(), &out));
}
void test_ConfigDefaultVerificationCliReadsActualOwnerAndRejectsBadInputOrMismatch(void) {
    prepare_default_verification();
    char *args[] = {"config.verify_defaults", "extra"};
    TEST_ASSERT_EQUAL(0, cmd_config_verify_defaults(1, args));
    unsigned reads = mocked_nvs_get_blob_fake.call_count;
    TEST_ASSERT_NOT_EQUAL(0, cmd_config_verify_defaults(2, args));
    TEST_ASSERT_NOT_EQUAL(0, cmd_config_verify_defaults(1, NULL));
    TEST_ASSERT_EQUAL(reads, mocked_nvs_get_blob_fake.call_count);
    verify_stored.hardware.builtin_led_brightness ^= 1;
    TEST_ASSERT_NOT_EQUAL(0, cmd_config_verify_defaults(1, args));
}
void test_ConfigStoppedOwnerRejectsFactoryGuardBeforeAnyWrites(void) {
    TEST_ASSERT_EQUAL(0, mgr()->base.Stop(&mgr()->base));
    TaskHandle_t previous = mocked_xTaskGetCurrentTaskHandle_fake.return_val;
    mocked_xTaskGetCurrentTaskHandle_fake.return_val = (TaskHandle_t)8;
    unsigned writes = mocked_nvs_set_blob_fake.call_count;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, mgr()->SetFactoryResetGuard(mgr(), true));
    TEST_ASSERT_EQUAL(writes, mocked_nvs_set_blob_fake.call_count);
    TEST_ASSERT_TRUE(mgr()->IsMutationAllowed(mgr()));
    TEST_ASSERT_EQUAL(0, mgr()->base.Start(&mgr()->base));
    mocked_xTaskGetCurrentTaskHandle_fake.return_val = previous;
}

void test_ConfigMgr_LocalTelemetryZeroThroughFieldApiAndPositiveBounds(void);
void test_ConfigEncoderV6MigrationPreservesExistingFieldsAndValidatesNewFields(void) {
    HardwareConfigs backup = mgr()->GetSettings(mgr())->hardware;
    migration_fixture = backup;
    migration_fixture.builtin_led_brightness = 0;
    migration_fixture.ext_led_color_cold_x = 20494;
    migration_fixture.ext_led_color_cold_y = 21561;
    uint32_t old_version = 6;
    memcpy((uint8_t *)&migration_fixture + 68, &old_version, sizeof(old_version));
    migration_size = 72;
    mocked_nvs_open_fake.return_val = ESP_OK;
    mocked_nvs_get_blob_fake.custom_fake = load_migration_fixture;
    TEST_ASSERT_EQUAL(ESP_OK, mgr()->Load(mgr(), "hardware"));
    HardwareConfigs *h = &mgr()->GetSettings(mgr())->hardware;
    TEST_ASSERT_EQUAL(HARDWARE_CONFIG_SCHEMA_VERSION, h->version);
    TEST_ASSERT_EQUAL(0, h->builtin_led_brightness);
    TEST_ASSERT_EQUAL(20494, h->ext_led_color_cold_x);
    TEST_ASSERT_EQUAL(21561, h->ext_led_color_cold_y);
    TEST_ASSERT_EQUAL(DEFAULT_ENCODER_STEPS_PER_DETENT, h->encoder_steps_per_detent);
    uint8_t steps=2, acceleration=0, threshold=50, invert=1;
    ConfigurationFieldUpdate fields[] = {
      {offsetof(HardwareConfigs, encoder_steps_per_detent), &steps, 1},
      {offsetof(HardwareConfigs, encoder_accel_enabled), &acceleration, 1},
      {offsetof(HardwareConfigs, encoder_accel_threshold_ms), &threshold, 1},
      {offsetof(HardwareConfigs, encoder_invert_direction), &invert, 1}
    };
    mocked_nvs_get_blob_fake.custom_fake = NULL;
    TEST_ASSERT_EQUAL(ESP_OK, mgr()->UpdateFields(mgr(), "hardware", fields, 4, true));
    steps=0; TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, mgr()->UpdateFields(mgr(), "hardware", fields, 4, true));
    steps=2; invert=2;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, mgr()->UpdateFields(mgr(), "hardware", fields, 4, true));
    TEST_ASSERT_EQUAL(2, h->encoder_steps_per_detent);
    TEST_ASSERT_EQUAL(1, h->encoder_invert_direction);
    *h=backup;
}

void test_ConfigFreeXYAllZonesValidateAndRollbackOnCommitFailure(void) {
    mocked_nvs_open_fake.return_val=ESP_OK;
    HardwareConfigs before=mgr()->GetSettings(mgr())->hardware;
    uint16_t x=1234,y=4321;
    for (unsigned zone=0;zone<RGB_ZONE_COUNT;++zone) {
        size_t ox,oy;TEST_ASSERT_TRUE(SystemConfigs_RGBZoneOffsets(zone,&ox,&oy));
        ConfigurationFieldUpdate fields[]={{ox,&x,sizeof(x)},{oy,&y,sizeof(y)}};
        TEST_ASSERT_EQUAL(ESP_OK,mgr()->UpdateFields(mgr(),"hardware",fields,2,true));
        uint16_t actualX,actualY;
        TEST_ASSERT_TRUE(SystemConfigs_GetRGBZoneXY(&mgr()->GetSettings(mgr())->hardware,zone,&actualX,&actualY));
        TEST_ASSERT_EQUAL(x,actualX);TEST_ASSERT_EQUAL(y,actualY);
        y=0;TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,mgr()->UpdateFields(mgr(),"hardware",fields,2,true));y=4321;
        x=65535;TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,mgr()->UpdateFields(mgr(),"hardware",fields,2,true));x=1234;
    }
    HardwareConfigs committed=mgr()->GetSettings(mgr())->hardware;
    x=5678;y=8765;
    size_t ox,oy;TEST_ASSERT_TRUE(SystemConfigs_RGBZoneOffsets(0,&ox,&oy));
    ConfigurationFieldUpdate fields[]={{ox,&x,sizeof(x)},{oy,&y,sizeof(y)}};
    mocked_nvs_commit_fake.return_val=ESP_FAIL;
    TEST_ASSERT_EQUAL(ESP_FAIL,mgr()->UpdateFields(mgr(),"hardware",fields,2,true));
    TEST_ASSERT_EQUAL_MEMORY(&committed,&mgr()->GetSettings(mgr())->hardware,sizeof(committed));
    mocked_nvs_commit_fake.return_val=ESP_OK;
    mgr()->GetSettings(mgr())->hardware=before;
}

void test_ConfigDisplayOffRejectedAndLegacyStorageMigrated(void) {
    SystemSettings original = *mgr()->GetSettings(mgr());
    uint8_t mode = 0;
    ConfigurationFieldUpdate field = {offsetof(SystemBaseConfigs, display_mode), &mode, sizeof(mode)};
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, mgr()->UpdateFields(mgr(), "system", &field, 1, false));
    TEST_ASSERT_EQUAL_MEMORY(&original, mgr()->GetSettings(mgr()), sizeof(original));
    system_migration_fixture = original.system;
    system_migration_fixture.display_mode = 0;
    mocked_nvs_open_fake.return_val = ESP_OK;
    mocked_nvs_get_blob_fake.custom_fake = load_system_fixture;
    TEST_ASSERT_EQUAL(ESP_OK, mgr()->Load(mgr(), "system"));
    TEST_ASSERT_EQUAL(DEFAULT_DISPLAY_MODE, mgr()->GetSettings(mgr())->system.display_mode);
    TEST_ASSERT_EQUAL(original.system.oled_timeout_sec, mgr()->GetSettings(mgr())->system.oled_timeout_sec);
    TEST_ASSERT_TRUE(mocked_nvs_commit_fake.call_count > 0);
    mocked_nvs_get_blob_fake.custom_fake = NULL;
    TEST_ASSERT_EQUAL(ESP_OK, mgr()->UpdateSettings(mgr(), &original, sizeof(original)));
}

static uint8_t control_v1_blob[24];
static size_t control_fixture_length;
static esp_err_t load_control_v1(nvs_handle_t handle,const char *key,void *out,size_t *size) {
    (void)handle;(void)key;
    if (*size < control_fixture_length) return ESP_ERR_NVS_INVALID_LENGTH;
    memcpy(out,control_v1_blob,control_fixture_length); *size=control_fixture_length; return ESP_OK;
}
void test_ControlV1MigrationPreservesManualAndThresholdsAndCommitsV2(void) {
    SystemSettings original=*mgr()->GetSettings(mgr());
    mocked_nvs_open_fake.return_val=ESP_OK;
    for (unsigned mode=0;mode<3;++mode) {
        memset(control_v1_blob,0,sizeof(control_v1_blob));
        ControlConfigs legacy=original.control;
        legacy.relay_open_temp=45; legacy.relay_close_temp=30;
        legacy.fan_manual_mode=mode!=0;legacy.fan_manual_state=mode==1;
        memcpy(control_v1_blob,&legacy,18);
        uint32_t version=1;memcpy(control_v1_blob+20,&version,4);control_fixture_length=24;
        mocked_nvs_get_blob_fake.custom_fake=load_control_v1;
        unsigned commits=mocked_nvs_commit_fake.call_count;
        TEST_ASSERT_EQUAL(ESP_OK,mgr()->Load(mgr(),"control"));
        ControlConfigs actual=mgr()->GetSettings(mgr())->control;
        TEST_ASSERT_EQUAL(2,actual.version);TEST_ASSERT_EQUAL(mode!=0,actual.fan_manual_mode);TEST_ASSERT_EQUAL(mode==1,actual.fan_manual_state);
        TEST_ASSERT_FLOAT_WITHIN(.001,45,actual.relay_open_temp);TEST_ASSERT_FLOAT_WITHIN(.001,30,actual.relay_close_temp);
        TEST_ASSERT_EQUAL(20,actual.fan_delta_on_decic);TEST_ASSERT_EQUAL(10,actual.fan_delta_off_decic);
        TEST_ASSERT_EQUAL(0,actual.object_temperature_source);TEST_ASSERT_EQUAL(0,actual.ambient_temperature_source);
        TEST_ASSERT_TRUE(mocked_nvs_commit_fake.call_count>commits);
    }
    mocked_nvs_get_blob_fake.custom_fake=NULL;
    TEST_ASSERT_EQUAL(ESP_OK,mgr()->UpdateSettings(mgr(),&original,sizeof(original)));
}
void test_ControlMigrationUnknownSchemaAndCommitFailurePreserveLiveState(void) {
    SystemSettings original=*mgr()->GetSettings(mgr());
    memset(control_v1_blob,0,sizeof(control_v1_blob));memcpy(control_v1_blob,&original.control,18);
    uint32_t version=1;memcpy(control_v1_blob+20,&version,4);control_fixture_length=24;
    mocked_nvs_open_fake.return_val=ESP_OK;mocked_nvs_get_blob_fake.custom_fake=load_control_v1;
    mocked_nvs_commit_fake.return_val=ESP_FAIL;
    TEST_ASSERT_EQUAL(ESP_FAIL,mgr()->Load(mgr(),"control"));
    TEST_ASSERT_EQUAL_MEMORY(&original,mgr()->GetSettings(mgr()),sizeof(original));
    mocked_nvs_commit_fake.return_val=ESP_OK;version=99;memcpy(control_v1_blob+20,&version,4);
    TEST_ASSERT_EQUAL(ESP_FAIL,mgr()->Load(mgr(),"control"));
    TEST_ASSERT_EQUAL_MEMORY(&original,mgr()->GetSettings(mgr()),sizeof(original));
    control_fixture_length=23;
    TEST_ASSERT_EQUAL(ESP_FAIL,mgr()->Load(mgr(),"control"));
    TEST_ASSERT_EQUAL_MEMORY(&original,mgr()->GetSettings(mgr()),sizeof(original));
    mocked_nvs_get_blob_fake.custom_fake=NULL;
}
void test_ControlSourceAndDeltaFieldsUseRealValidationAndRollback(void) {
    SystemSettings original=*mgr()->GetSettings(mgr());
    int16_t on=-10,off=-20;uint8_t object=2,ambient=1,strategy=1;
    ConfigurationFieldUpdate fields[]={
        {offsetof(ControlConfigs,fan_delta_on_decic),&on,2},{offsetof(ControlConfigs,fan_delta_off_decic),&off,2},
        {offsetof(ControlConfigs,object_temperature_source),&object,1},{offsetof(ControlConfigs,ambient_temperature_source),&ambient,1},
        {offsetof(ControlConfigs,fan_strategy),&strategy,1}};
    mocked_nvs_open_fake.return_val=ESP_OK;
    TEST_ASSERT_EQUAL(ESP_OK,mgr()->UpdateFields(mgr(),"control",fields,5,true));
    ControlConfigs accepted=mgr()->GetSettings(mgr())->control;
    TEST_ASSERT_EQUAL(-10,accepted.fan_delta_on_decic);TEST_ASSERT_EQUAL(2,accepted.object_temperature_source);
    on=-20;TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,mgr()->UpdateFields(mgr(),"control",fields,5,true));
    TEST_ASSERT_EQUAL_MEMORY(&accepted,&mgr()->GetSettings(mgr())->control,sizeof(accepted));
    on=-10;object=4;TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,mgr()->UpdateFields(mgr(),"control",fields,5,true));object=2;
    strategy=2;TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,mgr()->UpdateFields(mgr(),"control",fields,5,true));strategy=1;
    on=-91;TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,mgr()->UpdateFields(mgr(),"control",fields,5,true));on=-10;
    mocked_nvs_commit_fake.return_val=ESP_FAIL;on=0;
    TEST_ASSERT_EQUAL(ESP_FAIL,mgr()->UpdateFields(mgr(),"control",fields,5,true));
    TEST_ASSERT_EQUAL_MEMORY(&accepted,&mgr()->GetSettings(mgr())->control,sizeof(accepted));
    mocked_nvs_commit_fake.return_val=ESP_OK;
    TEST_ASSERT_EQUAL(ESP_OK,mgr()->UpdateSettings(mgr(),&original,sizeof(original)));
}

static LocalizationConfigs locale_saved_blob;
static esp_err_t capture_locale_blob(nvs_handle_t handle, const char *key, const void *data, size_t size) {
    (void)handle;
    TEST_ASSERT_EQUAL_STRING(NS_CONFIG_KEY, key);
    TEST_ASSERT_EQUAL(sizeof(locale_saved_blob), size);
    memcpy(&locale_saved_blob, data, size);
    return ESP_OK;
}
static esp_err_t read_locale_blob(nvs_handle_t handle, const char *key, void *data, size_t *size) {
    (void)handle;
    TEST_ASSERT_EQUAL_STRING(NS_CONFIG_KEY, key);
    TEST_ASSERT_EQUAL(sizeof(locale_saved_blob), *size);
    memcpy(data, &locale_saved_blob, sizeof(locale_saved_blob));
    return ESP_OK;
}
void test_ConfigLocalizationFieldPersistenceLoadAndRemovedCatalogPreserveOtherBlocks(void) {
    mocked_nvs_open_fake.return_val = ESP_OK;
    TEST_ASSERT_EQUAL(ESP_OK, mgr()->ResetToDefaults(mgr(), NULL));
    SystemSettings baseline, snapshot;
    TEST_ASSERT_EQUAL(ESP_OK, mgr()->GetSnapshot(mgr(), &baseline));
    TEST_ASSERT_EQUAL_STRING("tr", baseline.localization.locale);
    const LocalizationCatalog *original_catalog = Localization_Snapshot();
    TEST_ASSERT_EQUAL_STRING("tr", original_catalog->code);
    TEST_ASSERT_EQUAL(1, mgr()->GetVersion(mgr(), "localization"));
    char locale[LOCALIZATION_LOCALE_CAPACITY] = "en";
    ConfigurationFieldUpdate field = {offsetof(LocalizationConfigs, locale), locale, sizeof(locale)};
    mocked_nvs_set_blob_fake.custom_fake = capture_locale_blob;
    TEST_ASSERT_EQUAL(ESP_OK, mgr()->UpdateFields(mgr(), "localization", &field, 1, true));
    mocked_nvs_set_blob_fake.custom_fake = NULL;
    TEST_ASSERT_EQUAL_STRING("en", locale_saved_blob.locale);
    TEST_ASSERT_EQUAL_STRING("en", Localization_Snapshot()->code);
    TEST_ASSERT_EQUAL_STRING("tr", original_catalog->code);
    memcpy(locale, "tr", 3);
    TEST_ASSERT_EQUAL(ESP_OK, mgr()->UpdateFields(mgr(), "localization", &field, 1, false));
    mocked_nvs_get_blob_fake.custom_fake = read_locale_blob;
    TEST_ASSERT_EQUAL(ESP_OK, mgr()->Load(mgr(), "localization"));
    mocked_nvs_get_blob_fake.custom_fake = NULL;
    TEST_ASSERT_EQUAL(ESP_OK, mgr()->GetSnapshot(mgr(), &snapshot));
    TEST_ASSERT_EQUAL_STRING("en", snapshot.localization.locale);
    snapshot.localization = baseline.localization;
    TEST_ASSERT_EQUAL_MEMORY(&baseline, &snapshot, sizeof(snapshot));
    memcpy(locale, "bg", 3);
    TEST_ASSERT_EQUAL(ESP_OK, mgr()->UpdateFields(mgr(), "localization", &field, 1, true));
    TEST_ASSERT_EQUAL(ESP_OK, mgr()->GetSnapshot(mgr(), &snapshot));
    TEST_ASSERT_EQUAL_STRING("bg", snapshot.localization.locale);
    TEST_ASSERT_EQUAL_STRING("tr", Localization_Snapshot()->code);
    TEST_ASSERT_EQUAL(ESP_OK, mgr()->ResetToDefaults(mgr(), "localization"));
}
void test_ConfigLocalizationMalformedCodeAndCommitFailureNeverApply(void) {
    mocked_nvs_open_fake.return_val = ESP_OK;
    TEST_ASSERT_EQUAL(ESP_OK, mgr()->ResetToDefaults(mgr(), NULL));
    SystemSettings baseline, after;
    TEST_ASSERT_EQUAL(ESP_OK, mgr()->GetSnapshot(mgr(), &baseline));
    char locale[LOCALIZATION_LOCALE_CAPACITY];
    ConfigurationFieldUpdate field = {offsetof(LocalizationConfigs, locale), locale, sizeof(locale)};
    const char *invalid[] = {"", "e", "EN", "en-us", "en-USx", "../tr", "a123"};
    unsigned writes = mocked_nvs_set_blob_fake.call_count;
    for (unsigned index = 0; index < sizeof(invalid)/sizeof(invalid[0]); ++index) {
        memset(locale, 0, sizeof(locale));
        memcpy(locale, invalid[index], strlen(invalid[index]));
        TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, mgr()->UpdateFields(mgr(), "localization", &field, 1, true));
    }
    memset(locale, 'a', sizeof(locale));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, mgr()->UpdateFields(mgr(), "localization", &field, 1, true));
    memset(locale, 0, sizeof(locale)); memcpy(locale, "tr", 3); locale[7] = 'x';
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, mgr()->UpdateFields(mgr(), "localization", &field, 1, true));
    TEST_ASSERT_EQUAL(writes, mocked_nvs_set_blob_fake.call_count);
    memset(locale, 0, sizeof(locale)); memcpy(locale, "en", 3);
    mocked_nvs_commit_fake.return_val = ESP_FAIL;
    TEST_ASSERT_EQUAL(ESP_FAIL, mgr()->UpdateFields(mgr(), "localization", &field, 1, true));
    mocked_nvs_commit_fake.return_val = ESP_OK;
    TEST_ASSERT_EQUAL(ESP_OK, mgr()->GetSnapshot(mgr(), &after));
    TEST_ASSERT_EQUAL_MEMORY(&baseline, &after, sizeof(after));
    TEST_ASSERT_EQUAL_STRING("tr", Localization_Snapshot()->code);
}

void test_ConfigHardwareV7StartupMigrationPreservesEncoderAndRGB(void) {
    HardwareConfigs backup = mgr()->GetSettings(mgr())->hardware;
    migration_fixture = backup;
    migration_fixture.version = 7;
    migration_fixture.builtin_led_startup_anim = SOS_RED; /* Former reserved value must not become a boot selection. */
    migration_fixture.encoder_steps_per_detent = 2;
    migration_fixture.encoder_accel_enabled = 0;
    migration_fixture.ext_led_anim = RGBLED_ANIM_RAINBOW;
    HardwareConfigs expected = migration_fixture;
    expected.version = HARDWARE_CONFIG_SCHEMA_VERSION;
    expected.builtin_led_startup_anim = DEFAULT_BUILTIN_LED_STARTUP_ANIM;
    /* V8→V9: ext_led_anim is valid + non-NONE → copied to ext_led_startup_anim */
    expected.ext_led_state = DEFAULT_EXT_LED_STATE;
    expected.ext_led_startup_anim = RGBLED_ANIM_RAINBOW;
    expected.oled_contrast = DEFAULT_OLED_CONTRAST;
    migration_size = sizeof(migration_fixture);
    mocked_nvs_open_fake.return_val = ESP_OK;
    mocked_nvs_get_blob_fake.custom_fake = load_migration_fixture;
    TEST_ASSERT_EQUAL(ESP_OK, mgr()->Load(mgr(), "hardware"));
    TEST_ASSERT_EQUAL_MEMORY(&expected, &mgr()->GetSettings(mgr())->hardware, sizeof(expected));
    mocked_nvs_get_blob_fake.custom_fake = NULL;
    uint8_t selection = 255;
    ConfigurationFieldUpdate field = {offsetof(HardwareConfigs, builtin_led_startup_anim), &selection, 1};
    TEST_ASSERT_EQUAL(ESP_OK, mgr()->UpdateFields(mgr(), "hardware", &field, 1, true));
    TEST_ASSERT_EQUAL(255, mgr()->GetSettings(mgr())->hardware.builtin_led_startup_anim);
    selection = 250;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, mgr()->UpdateFields(mgr(), "hardware", &field, 1, true));
    TEST_ASSERT_EQUAL(255, mgr()->GetSettings(mgr())->hardware.builtin_led_startup_anim);
    mgr()->GetSettings(mgr())->hardware = backup;
}

void test_ConfigHardwareV8MigrationSetsExtLedStateStartupAnimAndOledContrast(void) {
    HardwareConfigs backup = mgr()->GetSettings(mgr())->hardware;
    migration_fixture = backup;
    migration_fixture.version = 8;
    /* Pre-v9: ext_led_anim holds a valid animation that should migrate to ext_led_startup_anim. */
    migration_fixture.ext_led_anim = RGBLED_ANIM_PULSE;
    /* Bytes 6-8 may hold legacy rgb state values; they should be overwritten. */
    migration_fixture.ext_led_state = false;
    migration_fixture.ext_led_startup_anim = 0;
    migration_fixture.oled_contrast = 0;
    HardwareConfigs expected = migration_fixture;
    expected.version = HARDWARE_CONFIG_SCHEMA_VERSION;
    expected.ext_led_state = DEFAULT_EXT_LED_STATE;
    expected.ext_led_startup_anim = RGBLED_ANIM_PULSE;  /* migrated from ext_led_anim */
    expected.oled_contrast = DEFAULT_OLED_CONTRAST;
    migration_size = sizeof(migration_fixture);
    mocked_nvs_open_fake.return_val = ESP_OK;
    mocked_nvs_get_blob_fake.custom_fake = load_migration_fixture;
    TEST_ASSERT_EQUAL(ESP_OK, mgr()->Load(mgr(), "hardware"));
    TEST_ASSERT_EQUAL_MEMORY(&expected, &mgr()->GetSettings(mgr())->hardware, sizeof(expected));
    mocked_nvs_get_blob_fake.custom_fake = NULL;
    /* ext_led_startup_anim must also validate through the animation catalog. */
    uint8_t selection = 250; /* invalid non-catalog value */
    ConfigurationFieldUpdate field = {offsetof(HardwareConfigs, ext_led_startup_anim), &selection, 1};
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, mgr()->UpdateFields(mgr(), "hardware", &field, 1, true));
    /* NONE (0) is valid for startup anim. */
    selection = RGBLED_ANIM_NONE;
    TEST_ASSERT_EQUAL(ESP_OK, mgr()->UpdateFields(mgr(), "hardware", &field, 1, true));
    TEST_ASSERT_EQUAL(RGBLED_ANIM_NONE, mgr()->GetSettings(mgr())->hardware.ext_led_startup_anim);
    /* oled_contrast accepts full 0-255 range via field API. */
    uint8_t contrast = 255;
    ConfigurationFieldUpdate cfield = {offsetof(HardwareConfigs, oled_contrast), &contrast, sizeof(contrast)};
    TEST_ASSERT_EQUAL(ESP_OK, mgr()->UpdateFields(mgr(), "hardware", &cfield, 1, true));
    TEST_ASSERT_EQUAL(255, mgr()->GetSettings(mgr())->hardware.oled_contrast);
    contrast = 0;
    TEST_ASSERT_EQUAL(ESP_OK, mgr()->UpdateFields(mgr(), "hardware", &cfield, 1, true));
    TEST_ASSERT_EQUAL(0, mgr()->GetSettings(mgr())->hardware.oled_contrast);
    mgr()->GetSettings(mgr())->hardware = backup;
}

void test_ConfigDefaultsReadIsPureAndDiffIncludesNewHardwareFields(void) {
    IConfigurationManager *owner=mgr();
    SystemSettings before,defaults,after;
    TEST_ASSERT_EQUAL(ESP_OK,owner->GetSnapshot(owner,&before));
    unsigned opens=mocked_nvs_open_fake.call_count;
    TEST_ASSERT_EQUAL(ESP_OK,owner->GetDefaults(owner,&defaults));
    TEST_ASSERT_EQUAL(opens,mocked_nvs_open_fake.call_count);
    TEST_ASSERT_EQUAL(ESP_OK,owner->GetSnapshot(owner,&after));
    TEST_ASSERT_EQUAL_MEMORY(&before,&after,sizeof(before));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,owner->GetDefaults(NULL,&after));
    TEST_ASSERT_EQUAL_MEMORY(&before,&after,sizeof(before));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,owner->GetDefaults(owner,NULL));
    TEST_ASSERT_EQUAL(ESP_OK,owner->UpdateSettings(owner,&defaults,sizeof(defaults)));
    char *args[]={"config.diff","--machine"};
    captured_config_diff=-1;
    TEST_ASSERT_EQUAL(0,cmd_config_diff(2,args));
    TEST_ASSERT_EQUAL(0,captured_config_diff);
    uint8_t brightness=defaults.hardware.builtin_led_brightness==0 ? 1 : 0;
    ConfigurationFieldUpdate field={offsetof(HardwareConfigs,builtin_led_brightness),&brightness,sizeof(brightness)};
    TEST_ASSERT_EQUAL(ESP_OK,owner->UpdateFields(owner,"hardware",&field,1,false));
    captured_config_diff=-1;
    TEST_ASSERT_EQUAL(0,cmd_config_diff(2,args));
    TEST_ASSERT_EQUAL(1,captured_config_diff);
    TEST_ASSERT_EQUAL(ESP_OK,owner->GetSnapshot(owner,&after));
    TEST_ASSERT_EQUAL(brightness,after.hardware.builtin_led_brightness);
    TEST_ASSERT_EQUAL(ESP_OK,owner->UpdateSettings(owner,&before,sizeof(before)));
}
void test_ConfigShowIncludesCompleteSnapshotWithoutMutation(void) {
    SystemSettings before,after;
    TEST_ASSERT_EQUAL(ESP_OK,mgr()->GetSnapshot(mgr(),&before));
    unsigned opens=mocked_nvs_open_fake.call_count;
    captured_config_fields=0;captured_builtin_startup=false;captured_locale=false;
    char *args[]={"config.show","--machine"};
    TEST_ASSERT_EQUAL(0,cmd_config_show(2,args));
    TEST_ASSERT_GREATER_THAN(70,captured_config_fields);
    TEST_ASSERT_TRUE(captured_builtin_startup);
    TEST_ASSERT_TRUE(captured_locale);
    TEST_ASSERT_EQUAL(opens,mocked_nvs_open_fake.call_count);
    TEST_ASSERT_EQUAL(ESP_OK,mgr()->GetSnapshot(mgr(),&after));
    TEST_ASSERT_EQUAL_MEMORY(&before,&after,sizeof(before));
}
void test_ConfigCheckedFieldsRejectStaleOwnerAndPreservePersistenceFailure(void) {
    SystemSettings original,before,after;
    TEST_ASSERT_EQUAL(ESP_OK,mgr()->GetSnapshot(mgr(),&original));
    uint8_t expected=original.hardware.builtin_led_brightness,value=expected==80 ? 90 : 80;
    ConfigurationFieldUpdate field={offsetof(HardwareConfigs,builtin_led_brightness),&value,sizeof(value)};
    const void *expectedValues[]={&expected};
    mocked_nvs_open_fake.return_val=ESP_OK;mocked_nvs_commit_fake.return_val=ESP_OK;
    TEST_ASSERT_NOT_NULL(mgr()->UpdateFieldsIfUnchanged);
    TEST_ASSERT_EQUAL(ESP_OK,mgr()->UpdateFieldsIfUnchanged(mgr(),"hardware",&field,expectedValues,1,true));
    TEST_ASSERT_EQUAL(ESP_OK,mgr()->GetSnapshot(mgr(),&before));
    TEST_ASSERT_EQUAL(value,before.hardware.builtin_led_brightness);
    unsigned opens=mocked_nvs_open_fake.call_count;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE,mgr()->UpdateFieldsIfUnchanged(mgr(),"hardware",&field,expectedValues,1,true));
    TEST_ASSERT_EQUAL(opens,mocked_nvs_open_fake.call_count);
    expected=value;value=100;mocked_nvs_commit_fake.return_val=ESP_FAIL;
    TEST_ASSERT_EQUAL(ESP_FAIL,mgr()->UpdateFieldsIfUnchanged(mgr(),"hardware",&field,expectedValues,1,true));
    TEST_ASSERT_EQUAL(ESP_OK,mgr()->GetSnapshot(mgr(),&after));TEST_ASSERT_EQUAL_MEMORY(&before,&after,sizeof(before));
    mocked_nvs_commit_fake.return_val=ESP_OK;
    TEST_ASSERT_EQUAL(ESP_OK,mgr()->UpdateSettings(mgr(),&original,sizeof(original)));
}
void test_ConfigCheckedFieldsValidateExpectedPointersAndKeepUnrelatedChanges(void) {
    SystemSettings original,current;
    TEST_ASSERT_EQUAL(ESP_OK,mgr()->GetSnapshot(mgr(),&original));
    uint8_t expected=original.hardware.builtin_led_brightness,value=80,external=40;
    ConfigurationFieldUpdate field={offsetof(HardwareConfigs,builtin_led_brightness),&value,sizeof(value)};
    const void *missing[]={NULL},*expectedValues[]={&expected};
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,mgr()->UpdateFieldsIfUnchanged(mgr(),"hardware",&field,NULL,1,true));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,mgr()->UpdateFieldsIfUnchanged(mgr(),"hardware",&field,missing,1,true));
    ConfigurationFieldUpdate other={offsetof(HardwareConfigs,ext_led_brightness),&external,sizeof(external)};
    mocked_nvs_open_fake.return_val=ESP_OK;mocked_nvs_commit_fake.return_val=ESP_OK;
    TEST_ASSERT_EQUAL(ESP_OK,mgr()->UpdateFields(mgr(),"hardware",&other,1,true));
    TEST_ASSERT_EQUAL(ESP_OK,mgr()->UpdateFieldsIfUnchanged(mgr(),"hardware",&field,expectedValues,1,true));
    TEST_ASSERT_EQUAL(ESP_OK,mgr()->GetSnapshot(mgr(),&current));
    TEST_ASSERT_EQUAL(external,current.hardware.ext_led_brightness);TEST_ASSERT_EQUAL(value,current.hardware.builtin_led_brightness);
    TEST_ASSERT_EQUAL(ESP_OK,mgr()->UpdateSettings(mgr(),&original,sizeof(original)));
}
int main(void) {
    UNITY_BEGIN();


    /* Group 1: Lifecycle */
    RUN_TEST(test_ConfigFactoryLoadFailureDoesNotPublishAndRetriesOwnedResources);
    RUN_TEST(test_ConfigHardwareV7StartupMigrationPreservesEncoderAndRGB);
    RUN_TEST(test_ConfigMgr_GetInstance_Singleton);
    RUN_TEST(test_ConfigMgr_InitialState);
    RUN_TEST(test_ConfigMgr_EarlySubscriptionFailureCanStopBeforeWorkerInit);
    RUN_TEST(test_ConfigMgr_ControlAllocationFailureCanRetry);
    RUN_TEST(test_ConfigMgr_Initialize_And_Start);
    RUN_TEST(test_ConfigMgr_CheckHealth_Healthy);

    /* Group 2: Settings */
    RUN_TEST(test_ConfigMgr_GetSettings_NotNull);
    RUN_TEST(test_ConfigMgr_DefaultValues_Sane);
    RUN_TEST(test_ConfigMgr_UpdateSettings_ValidSize);
    RUN_TEST(test_ConfigMgr_UpdateSettings_InvalidSize);
    RUN_TEST(test_ConfigMgr_UpdateSettings_NullPtr);
    RUN_TEST(test_ConfigMgr_IsRestartRequired_DefaultFalse);

    /* Group 3: Save / Load */
    RUN_TEST(test_ConfigMgr_Save_AllBlocks_Success);
    RUN_TEST(test_ConfigMgr_Save_SingleBlock);
    RUN_TEST(test_ConfigMgr_Save_UnknownBlock);
    RUN_TEST(test_ConfigMgr_Load_PreservesDefaultsOnMiss);

    /* Group 4: Validation & Reset */
    RUN_TEST(test_ConfigMgr_ValidateSettings_DefaultsPass);
    RUN_TEST(test_ConfigMgr_ValidateSettings_InvalidRelayTemps);
    RUN_TEST(test_ConfigMgr_ResetToDefaults_SingleBlock);
    RUN_TEST(test_ConfigMgr_GetVersion_KnownBlocks);

    RUN_TEST(test_ConfigMgr_ThermalRejectsNonFiniteAndInverted);
    RUN_TEST(test_ConfigMgr_MigrationPreservesSettings);
    RUN_TEST(test_ConfigMgr_MigrationV3Layout);
    RUN_TEST(test_ConfigMgr_MigrationV5Brightness);

    /* Group 5: SaveAsync */
    RUN_TEST(test_ConfigMgr_SaveAsync_QueuesCopiedBlockIdentifier);
    RUN_TEST(test_ConfigMgr_WorkerCreationFailureRollsBack);
    RUN_TEST(test_ConfigMgr_SaveAsync_QueueFullDoesNotAllocate);

    /* Group 6: Stop lifecycle */
    RUN_TEST(test_ConfigMgr_Stop_FromStarted);
    RUN_TEST(test_ConfigMgr_Stop_PreservesPendingSave);

    /* Group 7: ValidateSettings extended */
    RUN_TEST(test_ConfigMgr_ValidateSettings_CalibrationOutOfRange);
    RUN_TEST(test_ConfigMgr_ValidateSettings_ThermalOrdering);
    RUN_TEST(test_ConfigMgr_ValidateSettings_InvalidTempUnit);

    /* Group 8: ResetToDefaults all blocks */
    RUN_TEST(test_ConfigMgr_ResetToDefaults_AllBlocks);

    /* Group 9: GetVersion NULL */
    RUN_TEST(test_ConfigMgr_GetVersion_NullBlock);

    /* Group 10: Load with NVS data */
    RUN_TEST(test_ConfigMgr_Load_WithNvsData);

    /* Group 11: Dirty flag tracking */
    RUN_TEST(test_ConfigMgr_IsDirty_AfterUpdate);
    RUN_TEST(test_ConfigMgr_IsDirty_AfterSave);

    RUN_TEST(test_ConfigMgr_FanSetpointBounds);
    RUN_TEST(test_ConfigMgr_UpdateRejectsNonFiniteWithoutMutation);
    RUN_TEST(test_ConfigMgr_UpdateLockFailurePreservesSettings);
    RUN_TEST(test_ConfigMgr_FieldUpdatesPreserveUnrelatedValues);
    RUN_TEST(test_ConfigMgr_FieldCommitFailureDoesNotChangeLiveState);
    RUN_TEST(test_ConfigMgr_FieldCommitOnlyCleansItsOwnBlock);
    RUN_TEST(test_ConfigMgr_FieldValidationRejectsInvalidPair);
    RUN_TEST(test_ConfigMgr_FieldBoundsAndOverlapAreRejected);
    RUN_TEST(test_ConfigMgr_SnapshotAndFieldLockFailure);
    RUN_TEST(test_ConfigMgr_SnapshotRepresentationIsIndependentOfDestinationStorage);
    RUN_TEST(test_ConfigMgr_ReportingZeroBoundsAndPersistence);
    RUN_TEST(test_ConfigMgr_ChangedEventUsesOwnedSnapshot);
    RUN_TEST(test_ConfigMgr_SystemProfileMigrationPreservesExistingSettings);
    RUN_TEST(test_ConfigMgr_MenuAutoExitMigrationPreservesOwnerAndRollback);
    RUN_TEST(test_ConfigMgr_DeveloperPreferenceMigratesAndRollsBackFailedWrites);
    RUN_TEST(test_ConfigMgr_MenuAutoExitWriteValidatesAndPersists);
    RUN_TEST(test_ConfigMgr_PowerProfileRejectsUnknownValue);
    RUN_TEST(test_ConfigMgr_TxProfileMigrationPreservesV2CpuAndDisplay);
    RUN_TEST(test_ConfigMgr_TxProfileFieldValidationPersistenceAndRollback);
    RUN_TEST(test_ConfigMgr_PowerProfilePersistsThroughFieldAPI);
    RUN_TEST(test_ConfigMgr_WorkerReportsSaveFailureAndRecovery);
    RUN_TEST(test_ConfigMgr_StopRaceDrainsAcceptedRequestsBeforeAck);
    RUN_TEST(test_ConfigMgr_SaveEventRejectsUnterminatedAndUnknownPayload);
    RUN_TEST(test_ConfigMgr_PartialSubscriptionsRetainBusyOwnership);
    RUN_TEST(test_ConfigMgr_AllSharedAnimationSelectionsValidateThroughFieldAPI);
    RUN_TEST(test_ConfigMgr_LegacyTemperatureAnimationMigratesToRealColorMode);
    RUN_TEST(test_ConfigMgr_AnimationMigrationCommitFailurePreservesOriginalSelection);
    RUN_TEST(test_ConfigMgr_PrintUsesOwnedSnapshotAndHandlesUnavailableSnapshot);
    RUN_TEST(test_ConfigMgr_ClimateCliUsesRealFieldValidationAndSaveContract);
    RUN_TEST(test_ConfigResetCommitFailurePreservesLiveSettingsAndDirtyFlag);
    RUN_TEST(test_ConfigResetPartialCommitRetainsFailedBlockAndPublishesAppliedBlocks);
    RUN_TEST(test_ConfigResetLockFailureAndUnknownLoadHaveNoSideEffects);
    RUN_TEST(test_ConfigStatusCopiesMetadataAndPreservesOutputOnFailure);
    RUN_TEST(test_ConfigConsoleMetadataFailureDoesNotReportCleanOrVersionZero);
    RUN_TEST(test_ConfigNotificationRejectedUpdateRetriesLatestSnapshot);
    RUN_TEST(test_ConfigNotificationMissingCallbackRetainsUntilWorkerRecovery);
    RUN_TEST(test_ConfigNotificationConcurrentUpdateDoesNotRecursivelyPublishOrLoseLatest);
    RUN_TEST(test_ConfigNotificationPendingSurvivesStopAndRestartWithoutExtraTasks);
    RUN_TEST(test_ConfigLoadShortSuccessfulReadRejectsWithoutPartialMutation);
    RUN_TEST(test_ConfigLoadSdkPartialErrorPreservesAllSettingsAndMetadata);
    RUN_TEST(test_ConfigLoadNonFiniteCalibrationRollsBackWithoutNotification);
    RUN_TEST(test_ConfigLoadRejectsInvalidCandidateBeforeMigrationCommit);
    RUN_TEST(test_ConfigRuntimeLoadPublishesChangedSnapshotOnlyOnce);
    RUN_TEST(test_ConfigConsoleMalformedCommandsCannotReachPersistence);
    RUN_TEST(test_ConfigConsoleMissingCallbacksAreFailures);
    RUN_TEST(test_ConfigConsoleShowPropagatesSnapshotFailure);
    RUN_TEST(test_ConfigConsoleUnknownBlocksLeaveLiveAndNvsUntouched);
    RUN_TEST(test_ConfigModeFieldsRejectSentinelsBeforePersistence);
    RUN_TEST(test_ConfigBooleanFieldRejectsNoncanonicalStorageWithoutMutation);
    RUN_TEST(test_ConfigThermalAndAlertBoundsRejectFiniteOutliers);
    RUN_TEST(test_ConfigDefaultVerificationReadsAllPersistedBlocksWithoutWrites);
    RUN_TEST(test_ConfigDefaultVerificationRejectsEveryStoredSchemaMismatch);
    RUN_TEST(test_ConfigDefaultVerificationRejectsStoredValueAndLiveChanges);
    RUN_TEST(test_ConfigDefaultVerificationRejectsReadErrorsLengthsAndLockFailure);
    RUN_TEST(test_ConfigFactoryGuardRejectsForeignMutationsAndReleaseWithoutHoldingMutex);
    RUN_TEST(test_ConfigDefaultVerificationIgnoresSchemaPadding);
    RUN_TEST(test_ConfigDefaultVerificationCliReadsActualOwnerAndRejectsBadInputOrMismatch);
    RUN_TEST(test_ConfigMgr_LocalTelemetryZeroThroughFieldApiAndPositiveBounds);
    RUN_TEST(test_ConfigEncoderV6MigrationPreservesExistingFieldsAndValidatesNewFields);
    RUN_TEST(test_ConfigStoppedOwnerRejectsFactoryGuardBeforeAnyWrites);
    RUN_TEST(test_ConfigFreeXYAllZonesValidateAndRollbackOnCommitFailure);
    RUN_TEST(test_ConfigDisplayOffRejectedAndLegacyStorageMigrated);
    RUN_TEST(test_ControlV1MigrationPreservesManualAndThresholdsAndCommitsV2);
    RUN_TEST(test_ControlMigrationUnknownSchemaAndCommitFailurePreserveLiveState);
    RUN_TEST(test_ControlSourceAndDeltaFieldsUseRealValidationAndRollback);
    RUN_TEST(test_ConfigLocalizationFieldPersistenceLoadAndRemovedCatalogPreserveOtherBlocks);
    RUN_TEST(test_ConfigLocalizationMalformedCodeAndCommitFailureNeverApply);
    RUN_TEST(test_ConfigDefaultsReadIsPureAndDiffIncludesNewHardwareFields);
    RUN_TEST(test_ConfigHardwareV8MigrationSetsExtLedStateStartupAnimAndOledContrast);
    RUN_TEST(test_ConfigShowIncludesCompleteSnapshotWithoutMutation);
    RUN_TEST(test_ConfigCheckedFieldsRejectStaleOwnerAndPreservePersistenceFailure);
    RUN_TEST(test_ConfigCheckedFieldsValidateExpectedPointersAndKeepUnrelatedChanges);
    return UNITY_END();
}

void test_ConfigMgr_LocalTelemetryZeroThroughFieldApiAndPositiveBounds(void) {
    mocked_nvs_open_fake.return_val = ESP_OK;
    uint32_t interval = 0;
    ConfigurationFieldUpdate field = {offsetof(SystemBaseConfigs, telemetry_interval_ms), &interval, sizeof(interval)};
    TEST_ASSERT_EQUAL(ESP_OK, mgr()->UpdateFields(mgr(), "system", &field, 1, true));
    SystemSettings snapshot;
    TEST_ASSERT_EQUAL(ESP_OK, mgr()->GetSnapshot(mgr(), &snapshot));
    TEST_ASSERT_EQUAL(0, snapshot.system.telemetry_interval_ms);
    interval = TELEMETRY_ACTIVE_MIN_MS - 1;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, mgr()->UpdateFields(mgr(), "system", &field, 1, true));
    interval = TELEMETRY_MAX_MS + 1;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, mgr()->UpdateFields(mgr(), "system", &field, 1, true));
    TEST_ASSERT_EQUAL(ESP_OK, mgr()->GetSnapshot(mgr(), &snapshot));
    TEST_ASSERT_EQUAL(0, snapshot.system.telemetry_interval_ms);
    interval = TELEMETRY_ACTIVE_MIN_MS;
    TEST_ASSERT_EQUAL(ESP_OK, mgr()->UpdateFields(mgr(), "system", &field, 1, true));
}
