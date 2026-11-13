#ifndef CONSOLE_REGISTRATION_FIXTURE_H
#define CONSOLE_REGISTRATION_FIXTURE_H
#include "Communication/DebugConsole/IDebugConsole.h"
#include "unity.h"

static unsigned registration_calls, registration_fail_at;
static esp_err_t registration_failure;
static esp_err_t register_command(IDebugConsole *self, const esp_console_cmd_t *command,
                                  CommandCategory category) {
    (void)self;
    TEST_ASSERT_NOT_NULL(command);
    TEST_ASSERT_NOT_NULL(command->command);
    TEST_ASSERT_NOT_NULL(command->func);
    TEST_ASSERT_LESS_THAN(CMD_CAT_MAX, category);
    ++registration_calls;
    return registration_calls == registration_fail_at ? registration_failure : ESP_OK;
}
static IDebugConsole registration_console = {.RegisterCommandWithCategory = register_command};
static void check_registration_inputs(esp_err_t (*register_all)(IDebugConsole *)) {
    IDebugConsole missing_method = {0};
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, register_all(NULL));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, register_all(&missing_method));
    TEST_ASSERT_EQUAL(0, registration_calls);
}
static void check_registration_failures(esp_err_t (*register_all)(IDebugConsole *), unsigned count) {
    const esp_err_t errors[] = {ESP_ERR_NO_MEM, ESP_ERR_TIMEOUT};
    for (unsigned e = 0; e < sizeof(errors) / sizeof(errors[0]); ++e) {
        for (unsigned position = 1; position <= count; ++position) {
            registration_calls = 0;
            registration_fail_at = position;
            registration_failure = errors[e];
            TEST_ASSERT_EQUAL(errors[e], register_all(&registration_console));
            TEST_ASSERT_EQUAL(position, registration_calls);
            /* A partial failure must not poison a retry or conceal later errors. */
            registration_calls = registration_fail_at = 0;
            TEST_ASSERT_EQUAL(ESP_OK, register_all(&registration_console));
            TEST_ASSERT_EQUAL(count, registration_calls);
        }
    }
}
#endif
