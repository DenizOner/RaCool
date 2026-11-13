#include "unity.h"
#include "Communication/Zigbee/ZigbeeRadioBoundary.h"

static esp_err_t enableResult, disableResult;
esp_err_t __real_esp_ieee802154_enable(void) { return enableResult; }
esp_err_t __real_esp_ieee802154_disable(void) { return disableResult; }
esp_err_t __wrap_esp_ieee802154_enable(void);
esp_err_t __wrap_esp_ieee802154_disable(void);

void setUp(void) { ZigbeeRadioBoundary_Begin(); enableResult = disableResult = ESP_OK; }
void tearDown(void) {}
void test_RadioBoundaryPreservesDriverReturnAndFirstFailure(void) {
    enableResult = ESP_ERR_NO_MEM; disableResult = ESP_FAIL;
    TEST_ASSERT_EQUAL(ESP_ERR_NO_MEM, __wrap_esp_ieee802154_enable());
    TEST_ASSERT_EQUAL(ESP_FAIL, __wrap_esp_ieee802154_disable());
    TEST_ASSERT_EQUAL(ESP_ERR_NO_MEM, ZigbeeRadioBoundary_Result());
}
void test_RadioBoundarySuccessCannotEraseFailure(void) {
    disableResult = ESP_ERR_INVALID_STATE;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, __wrap_esp_ieee802154_disable());
    TEST_ASSERT_EQUAL(ESP_OK, __wrap_esp_ieee802154_enable());
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, ZigbeeRadioBoundary_Result());
}
void test_RadioBoundaryNewOwnerOperationResetsEvidence(void) {
    enableResult = ESP_FAIL;
    __wrap_esp_ieee802154_enable();
    ZigbeeRadioBoundary_Begin();
    enableResult = ESP_OK;
    TEST_ASSERT_EQUAL(ESP_OK, __wrap_esp_ieee802154_enable());
    TEST_ASSERT_EQUAL(ESP_OK, ZigbeeRadioBoundary_Result());
}
