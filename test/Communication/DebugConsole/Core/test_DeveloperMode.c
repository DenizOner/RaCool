#include "unity.h"
#include "Communication/DebugConsole/Core/DeveloperMode.h"

void setUp(void) { DeveloperMode_Reset(); }
void tearDown(void) { DeveloperMode_Reset(); }

void test_DeveloperModeDefaultsOffAndCanBeToggled(void) {
    TEST_ASSERT_FALSE(DeveloperMode_IsEnabled());
    DeveloperMode_SetEnabled(true);
    TEST_ASSERT_TRUE(DeveloperMode_IsEnabled());
    DeveloperMode_SetEnabled(false);
    TEST_ASSERT_FALSE(DeveloperMode_IsEnabled());
}

void test_DeveloperModeResetClosesActiveSession(void) {
    DeveloperMode_SetEnabled(true);
    DeveloperMode_Reset();
    TEST_ASSERT_FALSE(DeveloperMode_IsEnabled());
}

void test_DeveloperModeSeparatesProductAndDeveloperSubcommands(void) {
    const char *product[] = {"CLIMATE MODE ON", "CLIMATE EXTERNAL 1 23.5", "ZB TX Normal",
        "HW TEMP", "HW RGB OFF", "menu auto-exit off", "power mode balanced",
        "ir_temp", "rgb anim 1", "SYSTEM VERSION", "SYSTEM CONFIG SET brightness 12", "RELAY ON",
        "\"CLIMATE\" \"MODE\" ON", "LANGUAGE SET \"tr\""};
    const char *developer[] = {"CLIMATE TEST", "CLIMATE ACTION", "ZB DIAG --machine",
        "HW TEMP RESET", "HW GPIO", "menu nav 12 --machine", "power locks",
        "ir_temp status --machine", "ir_temp raw", "LOGS tail 5", "DEVELOPER TOOLS diagnostics", "SYSTEM TASKS", "SYSTEM CONFIG", "sys reset", "RELAY RESET_STATS", "bled status", "rgb status --machine", "rgb trace", "power status", "unregistered"};
    for (unsigned i = 0; i < sizeof(product) / sizeof(product[0]); ++i)
        TEST_ASSERT_TRUE_MESSAGE(DeveloperMode_IsUserLine(product[i]), product[i]);
    for (unsigned i = 0; i < sizeof(developer) / sizeof(developer[0]); ++i)
        TEST_ASSERT_FALSE_MESSAGE(DeveloperMode_IsUserLine(developer[i]), developer[i]);
    TEST_ASSERT_TRUE(DeveloperMode_IsUserLine("CC MODE ON"));
    TEST_ASSERT_FALSE(DeveloperMode_IsVisibleUserRoot("CC"));
    TEST_ASSERT_TRUE(DeveloperMode_IsVisibleUserRoot("CLIMATE"));
}

void test_DeveloperModeRejectsAmbiguousOrOversizedCommandTokens(void) {
    TEST_ASSERT_FALSE(DeveloperMode_IsUserLine(NULL));
    TEST_ASSERT_FALSE(DeveloperMode_IsUserLine("  "));
    TEST_ASSERT_FALSE(DeveloperMode_IsUserLine("\"SYSTEM\" TASKS"));
    TEST_ASSERT_FALSE(DeveloperMode_IsUserLine("SYSTEM \\\u0054ASKS"));
    TEST_ASSERT_FALSE(DeveloperMode_IsUserLine("SYSTEM 12345678901234567890123456789012"));
}
