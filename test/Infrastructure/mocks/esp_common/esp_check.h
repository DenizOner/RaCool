#ifndef ESP_CHECK_H
#define ESP_CHECK_H

#include "esp_err.h"
#include "esp_log.h"

#ifndef ESP_RETURN_ON_FALSE
#define ESP_RETURN_ON_FALSE(a, err_val, tag, format, ...) do { \
    if (!(a)) { \
        ESP_LOGE(tag, "%s(%d): " format, __FUNCTION__, __LINE__, ##__VA_ARGS__); \
        return (err_val); \
    } \
} while(0)
#endif

#ifndef ESP_RETURN_ON_ERROR
#define ESP_RETURN_ON_ERROR(x, tag, format, ...) do { \
    esp_err_t err_rc_ = (x); \
    if (err_rc_ != ESP_OK) { \
        ESP_LOGE(tag, "%s(%d): " format, __FUNCTION__, __LINE__, ##__VA_ARGS__); \
        return err_rc_; \
    } \
} while(0)
#endif

#endif // ESP_CHECK_H
