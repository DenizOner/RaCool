#include "unity.h"
#include "Infrastructure/support/test_mocks.h"
#define PinManager_GetInstance PinConsoleTest_GetInstance
#include "Hardware/PinManager/PinManagerConsoleCommands.c"
#undef PinManager_GetInstance
static IPinManager fakePins;
static unsigned lookups, getCalls, dumps;
static bool available, valid;
static int32_t dumpResult;
IPinManager *PinConsoleTest_GetInstance(void) { ++lookups; return available ? &fakePins : NULL; }
static gpio_num_t GetPin(IPinManager *self, PinFunction_t function) { (void)self; ++getCalls; return (gpio_num_t)function; }
static bool Validate(IPinManager *self) { (void)self; return valid; }
static int32_t Dump(IPinManager *self) { (void)self; ++dumps; return dumpResult; }
void setUp(void) {
    fakePins = (IPinManager){.GetPin=GetPin, .Validate=Validate, .DumpPinMap=Dump};
    lookups = getCalls = dumps = 0; available = valid = true; dumpResult = 0;
}
void tearDown(void) {}
void test_PinConsoleRejectsMalformedAndOverflowWithoutServiceAccess(void) {
    const char *values[] = {"", "0", "-1", "+1", " 1", "1.0", "1junk", "4294967296", "18446744073709551616", "99999"};
    for (unsigned i = 0; i < sizeof(values)/sizeof(values[0]); ++i) {
        char *args[] = {"pin", "get", (char *)values[i]};
        TEST_ASSERT_NOT_EQUAL(0, cmd_pin_handler(3, args));
    }
    char *missing[] = {"pin", "get"}; TEST_ASSERT_NOT_EQUAL(0, cmd_pin_handler(2, missing));
    char *extra[] = {"pin", "status", "extra"}; TEST_ASSERT_NOT_EQUAL(0, cmd_pin_handler(3, extra));
    TEST_ASSERT_NOT_EQUAL(0, cmd_pin_handler(1, NULL));
    TEST_ASSERT_EQUAL(0, lookups); TEST_ASSERT_EQUAL(0, getCalls);
}
void test_PinConsoleValidBoundsAndFailurePropagation(void) {
    char number[32]; snprintf(number, sizeof(number), "%u", (unsigned)(PIN_FUNC_MAX_COUNT-1));
    char *get[] = {"pin", "get", "1"}; TEST_ASSERT_EQUAL(0, cmd_pin_handler(3, get));
    get[2]=number; TEST_ASSERT_EQUAL(0, cmd_pin_handler(3, get)); TEST_ASSERT_EQUAL(2, getCalls);
    available=false; TEST_ASSERT_NOT_EQUAL(0, cmd_pin_handler(3, get)); available=true;
    char *status[]={"pin", "status"}; valid=false; TEST_ASSERT_NOT_EQUAL(0, cmd_pin_handler(2, status));
    valid=true; dumpResult=-4; TEST_ASSERT_NOT_EQUAL(0, cmd_pin_handler(2, status));
    char *dump[]={"pin", "dump"}; TEST_ASSERT_NOT_EQUAL(0, cmd_pin_handler(2, dump));
    dumpResult=0; TEST_ASSERT_EQUAL(0, cmd_pin_handler(2, dump));
}
