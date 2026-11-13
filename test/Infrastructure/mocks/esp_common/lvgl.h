#ifndef MOCK_LVGL_H
#define MOCK_LVGL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

typedef enum {
    LV_INDEV_TYPE_NONE,
    LV_INDEV_TYPE_POINTER,
    LV_INDEV_TYPE_KEYPAD,
    LV_INDEV_TYPE_BUTTON,
    LV_INDEV_TYPE_ENCODER,
} lv_indev_type_t;

typedef enum {
    LV_INDEV_STATE_RELEASED = 0,
    LV_INDEV_STATE_PRESSED
} lv_indev_state_t;

typedef struct _lv_indev_t {
    int dummy;
} lv_indev_t;

typedef struct {
    int32_t enc_diff;
    lv_indev_state_t state;
} lv_indev_data_t;

static inline lv_indev_t * lv_indev_create(void) {
    return (lv_indev_t *)1;
}

static inline void lv_indev_set_type(lv_indev_t * indev, lv_indev_type_t type) {
    (void)indev;
    (void)type;
}

static inline void lv_indev_set_read_cb(lv_indev_t * indev, void (*read_cb)(lv_indev_t *, lv_indev_data_t *)) {
    (void)indev;
    (void)read_cb;
}

#ifdef __cplusplus
}
#endif

#endif // MOCK_LVGL_H
