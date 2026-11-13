#include "test_mocks.h"
#include "Infrastructure/support/console_registration_fixture.h"
#include "Management/ConfigurationManager/ConfigurationManagerCommands.c"
esp_err_t SystemConfigs_Print(struct IConfigurationManager *manager) {
    (void)manager;
    TEST_FAIL_MESSAGE("Registration must not execute a diagnostic handler");
    return ESP_FAIL;
}
void setUp(void) { registration_calls = registration_fail_at = 0; }
void tearDown(void) {}
void test_ConfigRegistration_RejectsMissingBoundary(void) {
    check_registration_inputs(ConfigurationManagerCommands_Register);
}
void test_ConfigRegistration_PreservesEveryFailureAndAllowsRetry(void) {
    check_registration_failures(ConfigurationManagerCommands_Register, 9);
}

static unsigned reset_calls;
static const char *reset_block;
static esp_err_t record_reset(IConfigurationManager *manager, const char *block) {
    (void)manager;
    ++reset_calls;
    reset_block = block;
    return ESP_OK;
}

void test_ConfigResetRequiresExplicitConfirmationBeforeOwnerMutation(void) {
    IConfigurationManager owner = {.ResetToDefaults = record_reset};
    RESET_FAKE(mocked_ServiceLocator_GetService);
    mocked_ServiceLocator_GetService_fake.return_val = &owner.base;
    reset_calls = 0; reset_block = NULL;
    char *missing[] = {"config.reset"};
    char *wrong[] = {"config.reset", "system", "wrong"};
    char *all[] = {"config.reset", "CONFIRM"};
    char *block[] = {"config.reset", "system", "CONFIRM", "--machine"};
    TEST_ASSERT_NOT_EQUAL(0, cmd_config_reset(1, missing));
    TEST_ASSERT_NOT_EQUAL(0, cmd_config_reset(3, wrong));
    TEST_ASSERT_EQUAL(0, reset_calls);
    TEST_ASSERT_EQUAL(0, cmd_config_reset(2, all));
    TEST_ASSERT_EQUAL(1, reset_calls);
    TEST_ASSERT_NULL(reset_block);
    TEST_ASSERT_EQUAL(0, cmd_config_reset(4, block));
    TEST_ASSERT_EQUAL(2, reset_calls);
    TEST_ASSERT_EQUAL_STRING("system", reset_block);
    mocked_ServiceLocator_GetService_fake.return_val = NULL;
}
