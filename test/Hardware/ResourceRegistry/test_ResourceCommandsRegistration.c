#include "test_mocks.h"
#include "Infrastructure/support/console_registration_fixture.h"
#include "Hardware/ResourceRegistry/ResourceRegistryCommands.c"
void setUp(void) { registration_calls = registration_fail_at = 0; }
void tearDown(void) {}
void test_ResourceRegistration_RejectsMissingBoundary(void) {
    check_registration_inputs(ResourceRegistryCommands_Register);
}
void test_ResourceRegistration_PreservesFailureAndAllowsRetry(void) {
    check_registration_failures(ResourceRegistryCommands_Register, 1);
}

#include "../../Infrastructure/support/console_capture.h"
static int owner_result; static unsigned owner_calls;
static bool registry_valid(IResourceRegistry *self) { (void)self; return false; }
static int32_t registry_reset(IResourceRegistry *self) { (void)self; return -4; }
static bool registry_available(IResourceRegistry *self,ResourceId_t id) { (void)self; (void)id; return true; }
static int32_t registry_owner(IResourceRegistry *self,ResourceId_t id,char *output,size_t capacity) {
    (void)self; (void)id; (void)output; (void)capacity; ++owner_calls; return owner_result;
}
static IResourceRegistry console_registry={.Validate=registry_valid,.Reset=registry_reset,.IsAvailable=registry_available,.GetOwner=registry_owner};
static IService *registry_locator(const char *name) {
    return !strcmp(name,SVC_NAME_RESOURCE_REGISTRY) ? (IService *)&console_registry : NULL;
}
void test_ResourceConsole_RejectsMalformedCheckBeforeOwnerLookup(void) {
    mocked_ServiceLocator_GetService_fake.custom_fake=registry_locator; owner_calls=0;
    const char *invalid[]={"-1","1x","4294967296","999999999999999999999"," 1",""};
    for (unsigned i=0;i<sizeof(invalid)/sizeof(invalid[0]);++i) {
        char *args[]={"RESREG","CHECK","1",(char *)invalid[i]}; TEST_ASSERT_NOT_EQUAL(0,cmd_resreg(4,args));
    }
    char *bad_type[]={"RESREG","CHECK","5","0"}; TEST_ASSERT_NOT_EQUAL(0,cmd_resreg(4,bad_type));
    TEST_ASSERT_NOT_EQUAL(0,cmd_resreg(0,NULL));
    TEST_ASSERT_EQUAL_UINT(0,owner_calls);
}
void test_ResourceConsole_PropagatesOwnerValidationAndResetFailure(void) {
    mocked_ServiceLocator_GetService_fake.custom_fake=registry_locator; owner_calls=0; owner_result=-4;
    char *check[]={"RESREG","CHECK","1","0"}; TEST_ASSERT_NOT_EQUAL(0,cmd_resreg(4,check));
    TEST_ASSERT_EQUAL_UINT(1,owner_calls);
    char *validate[]={"RESREG","VALIDATE"}; TEST_ASSERT_NOT_EQUAL(0,cmd_resreg(2,validate));
    char *reset[]={"RESREG","RESET"}; TEST_ASSERT_NOT_EQUAL(0,cmd_resreg(2,reset));
}
void test_ResourceConsole_UsesResponseLanguageForUnreservedOwner(void) {
    mocked_ServiceLocator_GetService_fake.custom_fake=registry_locator; owner_calls=0; owner_result=-2;
    const LocalizationCatalog *saved=Localization_Snapshot(); char output[512]; char *args[]={"RESREG","CHECK","1","0"};
    TEST_ASSERT_TRUE(Localization_Apply("tr"));
    TEST_ASSERT_EQUAL(0,test_capture_console(cmd_resreg,4,args,output,sizeof(output)));
    TEST_ASSERT_NOT_NULL(strstr(output,Localization_Text(Localization_Find("tr"),LOC_CLI_RESOURCE_VALUE_FREE)));
    TEST_ASSERT_TRUE(Localization_Apply("en"));
    TEST_ASSERT_EQUAL(0,test_capture_console(cmd_resreg,4,args,output,sizeof(output)));
    TEST_ASSERT_NOT_NULL(strstr(output,"Owner = Free"));
    TEST_ASSERT_TRUE(Localization_Apply(saved->code));
}
