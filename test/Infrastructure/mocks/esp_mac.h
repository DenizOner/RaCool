#ifndef MOCK_ESP_MAC_H
#define MOCK_ESP_MAC_H
#include <stdint.h>
#include "esp_err.h"
typedef enum { ESP_MAC_WIFI_STA, ESP_MAC_BT, ESP_MAC_IEEE802154 } esp_mac_type_t;
esp_err_t esp_read_mac(uint8_t *, esp_mac_type_t);
#endif
