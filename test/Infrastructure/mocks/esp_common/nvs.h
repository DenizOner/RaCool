/**
 * @file nvs.h
 * @brief Minimal NVS (Non-Volatile Storage) mock for native unit tests.
 *
 * Declares only the types, constants, and function signatures needed by
 * the production code under test. All functions are redirected to FFF fakes
 * via the macro definitions in test_mocks.h.
 */

#ifndef NVS_H
#define NVS_H

#include <stdint.h>
#include <stddef.h>

typedef uint32_t nvs_handle_t;

typedef enum {
    NVS_READONLY,
    NVS_READWRITE
} nvs_open_mode_t;

/* -------------------------------------------------------------------------
 * NVS error codes (subset matching ESP-IDF values)
 * ------------------------------------------------------------------------- */
#define ESP_ERR_NVS_BASE              0x1100
#define ESP_ERR_NVS_NOT_FOUND         (ESP_ERR_NVS_BASE + 0x08)  /**< Key not found */
#define ESP_ERR_NVS_INVALID_LENGTH    (ESP_ERR_NVS_BASE + 0x09)  /**< Length mismatch */
#define ESP_ERR_NVS_NO_FREE_PAGES    (ESP_ERR_NVS_BASE + 0x0d)  /**< Partition full */
#define ESP_ERR_NVS_NEW_VERSION_FOUND (ESP_ERR_NVS_BASE + 0x10)  /**< NVS version changed */
#define ESP_ERR_NVS_INVALID_NAME      (ESP_ERR_NVS_BASE + 0x07)  /**< Invalid namespace */

/* -------------------------------------------------------------------------
 * NVS function declarations (implemented by FFF fakes)
 * ------------------------------------------------------------------------- */
int32_t nvs_open(const char *name, nvs_open_mode_t open_mode, nvs_handle_t *out_handle);
void    nvs_close(nvs_handle_t handle);
int32_t nvs_set_blob(nvs_handle_t handle, const char *key, const void *value, size_t length);
int32_t nvs_get_blob(nvs_handle_t handle, const char *key, void *out_value, size_t *length);
int32_t nvs_commit(nvs_handle_t handle);
int32_t nvs_get_u8(nvs_handle_t handle, const char *key, uint8_t *out_value);
int32_t nvs_set_u8(nvs_handle_t handle, const char *key, uint8_t value);
int32_t nvs_get_u32(nvs_handle_t handle, const char *key, uint32_t *out_value);
int32_t nvs_set_u32(nvs_handle_t handle, const char *key, uint32_t value);
int32_t nvs_get_i32(nvs_handle_t handle, const char *key, int32_t *out_value);
int32_t nvs_set_i32(nvs_handle_t handle, const char *key, int32_t value);
int32_t nvs_get_str(nvs_handle_t handle, const char *key, char *out_value, size_t *length);
int32_t nvs_set_str(nvs_handle_t handle, const char *key, const char *value);
int32_t nvs_erase_all(nvs_handle_t handle);

#endif /* NVS_H */
