#include "i2c_test_fixture.h"
#include "Hardware/I2CManager/I2CManagerConsoleCommands.c"
#include "Infrastructure/support/console_capture.h"
void setUp(void) { FixtureReset(); }
void tearDown(void) {}
void test_I2CConsole_ScanFailureReturnsFailure(void) {
    FixtureStart();
    mocked_i2c_master_probe_fake.return_val = ESP_ERR_TIMEOUT;
    char *args[] = {"i2c", "scan"};
    TEST_ASSERT_EQUAL(-1, cmd_i2c_handler(2, args));
}
void test_I2CConsole_EmptyBusScanSucceeds(void) {
    FixtureStart();
    char *args[] = {"i2c", "scan"};
    TEST_ASSERT_EQUAL(0, cmd_i2c_handler(2, args));
}
void test_I2CConsole_UnknownCommandIsRejected(void) {
    char *args[] = {"i2c", "unknown"};
    TEST_ASSERT_EQUAL(-1, cmd_i2c_handler(2, args));
    TEST_ASSERT_EQUAL(0, mocked_i2c_master_probe_fake.call_count);
}

void test_I2CConsole_MalformedArgumentsNeverProbeTheBus(void) {
    TEST_ASSERT_EQUAL(-1, cmd_i2c_handler(0, NULL));
    char *missing[] = {"i2c", NULL}; TEST_ASSERT_EQUAL(-1, cmd_i2c_handler(2, missing));
    missing[1] = ""; TEST_ASSERT_EQUAL(-1, cmd_i2c_handler(2, missing));
    char *extra[] = {"i2c", "scan", "extra"}; TEST_ASSERT_EQUAL(-1, cmd_i2c_handler(3, extra));
    TEST_ASSERT_EQUAL(0, mocked_i2c_master_probe_fake.call_count);
}
void test_I2CConsole_MissingCallbacksCannotClaimStatusOrScanSuccess(void) {
    II2CManager *i2c = I2CManager_GetInstance(); II2CManager saved = *i2c;
    i2c->base.GetState = NULL; char *status[] = {"i2c", "status"}; int st = cmd_i2c_handler(2, status);
    i2c->Scan = NULL; char *scan[] = {"i2c", "scan"}; int probe = cmd_i2c_handler(2, scan);
    *i2c = saved;
    TEST_ASSERT_EQUAL(-1, st); TEST_ASSERT_EQUAL(-1, probe);
    TEST_ASSERT_EQUAL(0, mocked_i2c_master_probe_fake.call_count);
}

void test_I2CConsoleRecoveryPropagatesSdkFailureAndRejectsLeasedBus(void) {
    II2CManager *manager = FixtureStart();
    char *args[] = {"i2c", "recover"};
    i2c_master_bus_handle_t bus;
    int owner;
    TEST_ASSERT_EQUAL(ESP_OK, manager->AcquireBus(manager, &owner, &bus));
    TEST_ASSERT_EQUAL(-1, cmd_i2c_handler(2, args));
    TEST_ASSERT_EQUAL(0, mocked_i2c_master_bus_reset_fake.call_count);
    TEST_ASSERT_EQUAL(ESP_OK, manager->ReleaseBus(manager, &owner));
    mocked_i2c_master_bus_reset_fake.return_val = ESP_ERR_TIMEOUT;
    TEST_ASSERT_EQUAL(-1, cmd_i2c_handler(2, args));
    mocked_i2c_master_bus_reset_fake.return_val = ESP_OK;
    TEST_ASSERT_EQUAL(0, cmd_i2c_handler(2, args));
}

void test_I2CConsoleMachineSummaryAndLocalizedHumanOutput(void) {
    FixtureStart();char *args[]={"i2c","status","--machine"};char en[1024],tr[1024];
    TEST_ASSERT_TRUE(Localization_Apply("en"));
    TEST_ASSERT_EQUAL(0,test_capture_console(cmd_i2c_handler,3,args,en,sizeof(en)));
    TEST_ASSERT_NOT_NULL(strstr(en,"type=i2c_status state=2 ready=1 devices=0 owners=0"));
    TEST_ASSERT_TRUE(Localization_Apply("tr"));
    TEST_ASSERT_EQUAL(0,test_capture_console(cmd_i2c_handler,3,args,tr,sizeof(tr)));
    TEST_ASSERT_EQUAL_STRING(en,tr);
    TEST_ASSERT_EQUAL(0,test_capture_console(cmd_i2c_handler,2,args,tr,sizeof(tr)));
    TEST_ASSERT_NOT_NULL(strstr(tr,"I2C Yöneticisi Durumu"));
    II2CManager *manager=I2CManager_GetInstance();II2CManager saved=*manager;
    manager->GetDiagnostics=NULL;
    int result=test_capture_console(cmd_i2c_handler,3,args,tr,sizeof(tr));
    *manager=saved;
    TEST_ASSERT_EQUAL(-1,result);TEST_ASSERT_NULL(strstr(tr,"type=i2c_status"));
    TEST_ASSERT_TRUE(Localization_Apply("en"));
}

void test_I2CConsoleMachineRecoveryPreservesLeasedBus(void) {
    II2CManager *manager=FixtureStart();int owner;i2c_master_bus_handle_t bus;
    TEST_ASSERT_EQUAL(ESP_OK,manager->AcquireBus(manager,&owner,&bus));
    char *args[]={"i2c","recover","--machine"};char output[1024];
    TEST_ASSERT_EQUAL(-1,test_capture_console(cmd_i2c_handler,3,args,output,sizeof(output)));
    TEST_ASSERT_NOT_NULL(strstr(output,"type=i2c_command command=recover result=rejected"));
    TEST_ASSERT_EQUAL(0,mocked_i2c_master_bus_reset_fake.call_count);
    TEST_ASSERT_EQUAL(ESP_OK,manager->ReleaseBus(manager,&owner));
}
