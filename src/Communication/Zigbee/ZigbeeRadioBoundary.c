#include "Communication/Zigbee/ZigbeeRadioBoundary.h"
#include <stdatomic.h>

static atomic_int radioResult = ESP_OK;

/* These signatures match ESP-IDF's public native radio enable/disable API.
 * --wrap redirects only callers; __real symbols resolve to the SDK driver. */
extern esp_err_t __real_esp_ieee802154_enable(void);
extern esp_err_t __real_esp_ieee802154_disable(void);

void ZigbeeRadioBoundary_Begin(void) { atomic_store(&radioResult, ESP_OK); }
esp_err_t ZigbeeRadioBoundary_Result(void) { return atomic_load(&radioResult); }

static esp_err_t RecordRadioResult(esp_err_t result) {
    if (result != ESP_OK) {
        int expected = ESP_OK;
        atomic_compare_exchange_strong(&radioResult, &expected, result);
    }
    return result;
}

esp_err_t __wrap_esp_ieee802154_enable(void) {
    return RecordRadioResult(__real_esp_ieee802154_enable());
}

esp_err_t __wrap_esp_ieee802154_disable(void) {
    return RecordRadioResult(__real_esp_ieee802154_disable());
}
