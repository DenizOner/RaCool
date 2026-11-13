#ifndef ESP_CHIP_INFO_H
#define ESP_CHIP_INFO_H

#include <stdint.h>

typedef struct {
    int model;
    int cores;
    int revision;
    uint32_t features;
} esp_chip_info_t;

#define CHIP_ESP32C6 12
#define CHIP_FEATURE_EMB_FLASH (1U << 0)
#define CHIP_FEATURE_WIFI_BGN (1U << 1)
#define CHIP_FEATURE_BLE (1U << 4)
#define CHIP_FEATURE_BT (1U << 5)
#define CHIP_FEATURE_IEEE802154 (1U << 6)

#endif /* ESP_CHIP_INFO_H */
