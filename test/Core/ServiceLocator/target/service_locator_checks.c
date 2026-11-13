#include "Core/ServiceLocator/ServiceLocator.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "unity.h"
#include <stdatomic.h>
#include <string.h>
#include "../../../../src/Core/ServiceLocator/ServiceLocator.c"
#include "../../../../src/Core/ServiceLocator/ServiceLocatorConfig.c"

static SemaphoreHandle_t entered, release_callback, finished;
static atomic_bool batch_running;
static atomic_int batch_result;
static bool binding_owned;
static bool appended_binding_owned;
static ServiceState fixture_state(IService *self) { (void)self; return SERVICE_STATE_INITIALIZED; }
static int32_t blocked_start(IService *self) {
    (void)self;
    xSemaphoreGive(entered);
    xSemaphoreTake(release_callback, portMAX_DELAY);
    return 0;
}
static unsigned alias_starts,alias_stops;
static int32_t alias_start(IService *self) {(void)self;alias_starts++;return 0;}
static int32_t alias_stop(IService *self) {(void)self;alias_stops++;return 0;}
static IService fixture = {.Start = blocked_start, .GetState = fixture_state};
static IService appended_fixture = {.GetState = fixture_state};
static void lifecycle_worker(void *arg) {
    (void)arg;
    atomic_store(&batch_result, GetServiceLocator()->StartAllServices());
    xSemaphoreGive(finished);
    atomic_store(&batch_running, false);
    vTaskDelete(NULL);
}
void target_service_locator_cleanup(void) {
    if (release_callback) xSemaphoreGive(release_callback);
    for (unsigned i = 0; i < 200 && atomic_load(&batch_running); ++i)
        vTaskDelay(pdMS_TO_TICKS(10));
    TEST_ASSERT_FALSE(atomic_load(&batch_running));
    if (binding_owned) {
        TEST_ASSERT_EQUAL(0, GetServiceLocator()->UnregisterService("target_locator_fixture"));
        binding_owned = false;
    }
    if (appended_binding_owned) {
        TEST_ASSERT_EQUAL(0, GetServiceLocator()->UnregisterService("target_locator_append"));
        appended_binding_owned = false;
    }
    if (entered) { vSemaphoreDelete(entered); entered = NULL; }
    if (release_callback) { vSemaphoreDelete(release_callback); release_callback = NULL; }
    if (finished) { vSemaphoreDelete(finished); finished = NULL; }
}
void target_service_locator_verify_lifetime(void) {
    IService *existing[SERVICE_LOCATOR_MAX_SERVICES]; uint32_t count = 99;
    TEST_ASSERT_EQUAL(0, ServiceLocator_CopyServices(existing, SERVICE_LOCATOR_MAX_SERVICES, &count));
    // Never start or remove unrelated bindings from this isolated target image.
    TEST_ASSERT_EQUAL(0, count);
    entered = xSemaphoreCreateBinary(); release_callback = xSemaphoreCreateBinary(); finished = xSemaphoreCreateBinary();
    TEST_ASSERT_NOT_NULL(entered); TEST_ASSERT_NOT_NULL(release_callback); TEST_ASSERT_NOT_NULL(finished);
    TEST_ASSERT_EQUAL(0, ServiceLocator_RegisterService("target_locator_fixture", &fixture));
    binding_owned = true;
    atomic_store(&batch_running, true);
    BaseType_t created = xTaskCreate(lifecycle_worker, "locator_check", 4096, NULL, 5, NULL);
    if (created != pdPASS) atomic_store(&batch_running, false);
    TEST_ASSERT_EQUAL(pdPASS, created);
    TEST_ASSERT_EQUAL(pdTRUE, xSemaphoreTake(entered, pdMS_TO_TICKS(2000)));
    TEST_ASSERT_EQUAL_PTR(&fixture, ServiceLocator_GetService("target_locator_fixture"));
    char name[SERVICE_LOCATOR_NAME_CAPACITY];
    TEST_ASSERT_EQUAL(0, ServiceLocator_CopyServiceName(&fixture, name, sizeof(name)));
    TEST_ASSERT_EQUAL_STRING("target_locator_fixture", name);
    TEST_ASSERT_EQUAL(-2, GetServiceLocator()->UnregisterService("target_locator_fixture"));
    TEST_ASSERT_EQUAL(-2, ServiceLocator_Deinit());
    TEST_ASSERT_EQUAL(-2, GetServiceLocator()->StopAllServices());
    TEST_ASSERT_EQUAL(0, ServiceLocator_RegisterService("target_locator_append", &appended_fixture));
    appended_binding_owned = true;
    xSemaphoreGive(release_callback);
    TEST_ASSERT_EQUAL(pdTRUE, xSemaphoreTake(finished, pdMS_TO_TICKS(2000)));
    TEST_ASSERT_EQUAL(0, atomic_load(&batch_result));
    TEST_ASSERT_EQUAL(0, GetServiceLocator()->UnregisterService("target_locator_fixture"));
    binding_owned = false;
    TEST_ASSERT_NOT_EQUAL(0,GetServiceLocator()->StartAllServices());
    TEST_ASSERT_NOT_EQUAL(0,GetServiceLocator()->StopAllServices());
    TEST_ASSERT_FALSE(atomic_load(&lifecycleActive));
    TEST_ASSERT_EQUAL_PTR(&appended_fixture,ServiceLocator_GetService("target_locator_append"));
    TEST_ASSERT_EQUAL(0, GetServiceLocator()->UnregisterService("target_locator_append"));
    appended_binding_owned = false;
    TEST_ASSERT_EQUAL_STRING("target_locator_fixture", name);
    TEST_ASSERT_EQUAL(0, ServiceLocator_CopyServices(existing, SERVICE_LOCATOR_MAX_SERVICES, &count));
    TEST_ASSERT_EQUAL(0, count);
    fixture.Start=alias_start;fixture.Stop=alias_stop;alias_starts=alias_stops=0;
    TEST_ASSERT_EQUAL(0,ServiceLocator_RegisterService("target_locator_fixture",&fixture));binding_owned=true;
    TEST_ASSERT_EQUAL(0,ServiceLocator_RegisterService("target_locator_append",&fixture));appended_binding_owned=true;
    TEST_ASSERT_EQUAL(0,GetServiceLocator()->StartAllServices());TEST_ASSERT_EQUAL(1,alias_starts);
    TEST_ASSERT_EQUAL(0,GetServiceLocator()->StopAllServices());TEST_ASSERT_EQUAL(1,alias_stops);
    TEST_ASSERT_EQUAL(0,ServiceLocator_CopyServices(existing,SERVICE_LOCATOR_MAX_SERVICES,&count));TEST_ASSERT_EQUAL(1,count);
    ServiceLocatorBinding bindings[2];
    TEST_ASSERT_EQUAL(0,ServiceLocator_CopyBindings(bindings,2,&count));TEST_ASSERT_EQUAL(2,count);
    TEST_ASSERT_EQUAL(0,GetServiceLocator()->UnregisterService("target_locator_fixture"));binding_owned=false;
    TEST_ASSERT_EQUAL(0,GetServiceLocator()->UnregisterService("target_locator_append"));appended_binding_owned=false;
    TEST_ASSERT_EQUAL_STRING("target_locator_fixture",bindings[0].name);
    TEST_ASSERT_EQUAL_STRING("target_locator_append",bindings[1].name);
    fixture.Start=blocked_start;fixture.Stop=NULL;
}
