#include "unity.h"
#include "../../../../src/Hardware/PinManager/PinManager.c"
#include "../../../../src/Hardware/PinManager/PinManagerConfig.c"

void target_rotary_set_pin_manager(IPinManager *pins);
void target_rotary_worker_verify_shutdown(void);
void target_rotary_worker_cleanup(void);
void target_rotary_typed_pin_verify_ownership(void) {
    IPinManager *pins = PinManager_GetInstance();
    TEST_ASSERT_NOT_NULL(pins);
    TEST_ASSERT_EQUAL(0, pins->base.Initialize(&pins->base, NULL));
    TEST_ASSERT_EQUAL(0, pins->base.Start(&pins->base));
    target_rotary_set_pin_manager(pins);
    target_rotary_worker_verify_shutdown();
    target_rotary_worker_cleanup();
    target_rotary_set_pin_manager(NULL);
    TEST_ASSERT_EQUAL(0, pins->base.Stop(&pins->base));
}
