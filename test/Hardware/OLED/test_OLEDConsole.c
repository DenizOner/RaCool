#include "unity.h"
#include "Infrastructure/support/test_mocks.h"
#define OLED_GetModule OLEDConsoleTest_GetModule
#include "Hardware/OLED/OLEDConsoleCommands.c"
#undef OLED_GetModule
#include "Infrastructure/support/console_capture.h"
static IOLED fakeOLED;
static unsigned powerCalls, contrastCalls, invertCalls;
static esp_err_t sdkResult;
IOLED *OLEDConsoleTest_GetModule(void) { return &fakeOLED; }
static ServiceState State(IService *self) { (void)self; return SERVICE_STATE_INITIALIZED; }
static esp_err_t Power(IOLED *self, bool enabled) { (void)self; (void)enabled; ++powerCalls; return sdkResult; }
static esp_err_t Contrast(IOLED *self, uint8_t value) { (void)self; (void)value; ++contrastCalls; return sdkResult; }
static esp_err_t Invert(IOLED *self, bool value) { (void)self; (void)value; ++invertCalls; return sdkResult; }
void setUp(void) { fakeOLED=(IOLED){.base.GetState=State,.SetPower=Power,.SetContrast=Contrast,.InvertDisplay=Invert}; powerCalls=contrastCalls=invertCalls=0; sdkResult=ESP_OK; }
void tearDown(void) {}
void test_OLEDConsoleRejectsInvalidByteBeforeDriverWrite(void) {
    const char *invalid[]={"", "-1", "+1", " 1", "1x", "256", "999999999999999999"};
    for(unsigned i=0;i<sizeof(invalid)/sizeof(invalid[0]);++i) {
        char *args[]={"oled","contrast",(char *)invalid[i]}; TEST_ASSERT_NOT_EQUAL(0,cmd_oled_handler(3,args));
    }
    char *invert[]={"oled","invert","2"}; TEST_ASSERT_NOT_EQUAL(0,cmd_oled_handler(3,invert));
    TEST_ASSERT_EQUAL(0,contrastCalls); TEST_ASSERT_EQUAL(0,invertCalls); TEST_ASSERT_EQUAL(0,powerCalls);
}
void test_OLEDConsoleSdkFailuresAreNotReportedApplied(void) {
    char *on[]={"oled","on"}; char *contrast[]={"oled","contrast","255"}; char *invert[]={"oled","invert","1"};
    sdkResult=ESP_FAIL;
    TEST_ASSERT_NOT_EQUAL(0,cmd_oled_handler(2,on)); TEST_ASSERT_NOT_EQUAL(0,cmd_oled_handler(3,contrast)); TEST_ASSERT_NOT_EQUAL(0,cmd_oled_handler(3,invert));
    sdkResult=ESP_OK;
    TEST_ASSERT_EQUAL(0,cmd_oled_handler(3,contrast)); TEST_ASSERT_EQUAL(0,cmd_oled_handler(3,invert));
    TEST_ASSERT_EQUAL(1,powerCalls);
}
void test_OLEDConsoleImmediateLanguageAndSettingsDoNotRequestWake(void) {
    const LocalizationCatalog *baseline=Localization_Snapshot(); char output[1024]; char *status[]={"oled","status"};
    for(unsigned language=0;language<2;++language) {
        TEST_ASSERT_TRUE(Localization_Apply(language ? "en" : "tr"));
        TEST_ASSERT_EQUAL(0,test_capture_console(cmd_oled_handler,2,status,output,sizeof(output)));
        TEST_ASSERT_NOT_NULL(strstr(output,language ? "OLED Status" : "OLED Durumu"));
        char *contrast[]={"oled","contrast","0"}; TEST_ASSERT_EQUAL(0,cmd_oled_handler(3,contrast));
    }
    TEST_ASSERT_EQUAL(0,powerCalls); TEST_ASSERT_EQUAL(2,contrastCalls);
    TEST_ASSERT_TRUE(Localization_Apply(baseline->code));
}
