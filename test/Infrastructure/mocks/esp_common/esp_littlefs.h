#ifndef ESP_LITTLEFS_H
#define ESP_LITTLEFS_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    const char *base_path;
    const char *partition_label;
    size_t format_if_mount_failed;
    bool dont_mount;
} esp_vfs_littlefs_conf_t;

#endif /* ESP_LITTLEFS_H */
