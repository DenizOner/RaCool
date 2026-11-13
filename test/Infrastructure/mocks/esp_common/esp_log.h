#ifndef ESP_LOG_H
#define ESP_LOG_H

#include <stdio.h>
#include <stdarg.h>

typedef int (*vprintf_like_t)(const char *, va_list);
vprintf_like_t esp_log_set_vprintf(vprintf_like_t function);

typedef enum {
  ESP_LOG_NONE,
  ESP_LOG_ERROR,
  ESP_LOG_WARN,
  ESP_LOG_INFO,
  ESP_LOG_DEBUG,
  ESP_LOG_VERBOSE
} esp_log_level_t;

esp_log_level_t esp_log_level_get(const char *tag);
void esp_log_writev(esp_log_level_t level, const char *tag, const char *format, va_list args);

#define ESP_LOGE(tag, format, ...) printf("E (%s): " format "\n", tag, ##__VA_ARGS__)
#define ESP_LOGW(tag, format, ...) printf("W (%s): " format "\n", tag, ##__VA_ARGS__)
#define ESP_LOGI(tag, format, ...) printf("I (%s): " format "\n", tag, ##__VA_ARGS__)
#define ESP_LOGD(tag, format, ...) printf("D (%s): " format "\n", tag, ##__VA_ARGS__)
#define ESP_LOGV(tag, format, ...) printf("V (%s): " format "\n", tag, ##__VA_ARGS__)

#endif
