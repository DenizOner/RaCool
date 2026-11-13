#include "unity.h"
#include "Infrastructure/support/test_mocks.h"
#include "Core/SystemInitializer/ISystemInitializer.h"
static SystemInitializer *InitializerConsoleTest_Module(void);
#define GetSystemInitializer InitializerConsoleTest_Module
#include "Core/SystemInitializer/SystemInitializerConsoleCommands.c"
#undef GetSystemInitializer
static SystemInitializer initializer;
static SystemInitializer *InitializerConsoleTest_Module(void) { return &initializer; }
static ServiceState State(IService *self) { (void)self; return SERVICE_STATE_STARTED; }
void setUp(void) { initializer=(SystemInitializer){0}; }
void tearDown(void) {}
void test_InitializerConsoleMissingStateIsRejected(void) {
    char *args[]={"sys_init_status"}; TEST_ASSERT_NOT_EQUAL(0,cmd_sys_init_status(1,args));
    initializer.base.GetState=State; TEST_ASSERT_EQUAL(0,cmd_sys_init_status(1,args));
    TEST_ASSERT_NOT_EQUAL(0,cmd_sys_init_status(2,args));
}
