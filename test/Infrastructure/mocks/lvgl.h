#ifndef MOCK_LVGL_H
#define MOCK_LVGL_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// LVGL basic types mock
typedef struct _lv_obj_t lv_obj_t;
typedef struct _lv_group_t lv_group_t;
typedef struct _lv_indev_t lv_indev_t;
typedef uint32_t lv_color_t;
typedef int lv_opa_t;
typedef struct { int32_t x, y; } lv_point_t;
typedef int lv_text_flag_t;
#define LV_COORD_MAX INT32_MAX
#define LV_TEXT_FLAG_NONE 0

typedef struct {
    // dummy font struct
} lv_font_t;

/* Deterministic host boundary metrics only. Real font kerning, glyph widths and
 * clipping are verified by the target LVGL suite and captured SDK pixels. */
static inline void lv_text_get_size(lv_point_t *size, const char *text,
        const lv_font_t *font, int32_t letter_space, int32_t line_space,
        int32_t max_width, lv_text_flag_t flags) {
    (void)font; (void)line_space; (void)max_width; (void)flags;
    int32_t characters = 0;
    for (const unsigned char *p = (const unsigned char *)text; p && *p; ++p) {
        if ((*p & 0xc0u) != 0x80u) ++characters;
    }
    size->x = characters * 6 + (characters > 0 ? characters - 1 : 0) * letter_space;
    size->y = characters > 0 ? 11 : 0;
}

#define LV_FONT_DECLARE(name) extern const lv_font_t name;
const lv_font_t lv_font_montserrat_10 = {};
const lv_font_t lv_font_montserrat_14 = {};
const lv_font_t lv_font_montserrat_20 = {};
const lv_font_t racool_font_10 = {};
const lv_font_t racool_font_14 = {};
const lv_font_t racool_font_20 = {};

// Alignments and states
#define LV_ALIGN_TOP_LEFT 0
#define LV_ALIGN_TOP_RIGHT 1
#define LV_ALIGN_BOTTOM_LEFT 2
#define LV_ALIGN_BOTTOM_RIGHT 3
#define LV_ALIGN_CENTER 4
#define LV_ALIGN_OUT_RIGHT_BOTTOM 5
#define LV_ALIGN_TOP_MID 6
#define LV_ALIGN_RIGHT_MID 7

// LVGL widget states (mirrors real LVGL values)
typedef uint16_t lv_state_t;
#define LV_STATE_DEFAULT  ((lv_state_t)0x0000)
#define LV_STATE_CHECKED  ((lv_state_t)0x0001)
#define LV_STATE_FOCUSED  ((lv_state_t)0x0002)
#define LV_STATE_FOCUS_KEY ((lv_state_t)0x0004)
#define LV_STATE_EDITED   ((lv_state_t)0x0008)
#define LV_STATE_HOVERED  ((lv_state_t)0x0010)
#define LV_STATE_PRESSED  ((lv_state_t)0x0020)
#define LV_STATE_SCROLLED ((lv_state_t)0x0040)
#define LV_STATE_DISABLED ((lv_state_t)0x0080)

#define LV_OPA_COVER 255
#define LV_OPA_TRANSP 0
#define LV_BORDER_SIDE_TOP 1
#define LV_BORDER_SIDE_BOTTOM 2
#define LV_BORDER_SIDE_LEFT 4
#define LV_BORDER_SIDE_RIGHT 8
#define LV_BORDER_SIDE_NONE 0
#define LV_BORDER_SIDE_FULL 15
#define LV_TEXT_ALIGN_LEFT 0
#define LV_TEXT_ALIGN_CENTER 1
#define LV_TEXT_ALIGN_RIGHT 2
#define LV_PART_MAIN 0
#define LV_PART_INDICATOR 1
#define LV_PART_SCROLLBAR 2
#define LV_DIR_VER 12
#define LV_SCROLLBAR_MODE_AUTO 2
#define LV_ANIM_OFF 0
#define LV_ANIM_ON 1
#define LV_INDEV_TYPE_ENCODER 1

#define LV_OBJ_FLAG_HIDDEN 0x01
static inline void lv_obj_set_scrollable(lv_obj_t *obj, bool enabled) { (void)obj; (void)enabled; }
static inline void lv_obj_set_scroll_dir(lv_obj_t *obj, int dir) { (void)obj; (void)dir; }
static inline void lv_obj_set_scrollbar_mode(lv_obj_t *obj, int mode) { (void)obj; (void)mode; }
static inline void lv_obj_set_style_pad_right(lv_obj_t *obj, int pad, int selector) { (void)obj; (void)pad; (void)selector; }
static inline void lv_obj_set_style_width(lv_obj_t *obj, int width, int selector) { (void)obj; (void)width; (void)selector; }

#define LV_FLEX_FLOW_COLUMN 0
#define LV_FLEX_FLOW_ROW 1
#define LV_SIZE_CONTENT -1
#define LV_LABEL_LONG_MODE_SCROLL_CIRCULAR 1
#define LV_LABEL_LONG_MODE_CLIP 2
#define LV_LABEL_LONG_MODE_DOTS 3
static inline int lv_pct(int percent) { return percent; }
static inline void lv_obj_set_flex_flow(lv_obj_t *obj, int flow) { (void)obj; (void)flow; }
static inline void lv_obj_set_flex_grow(lv_obj_t *obj, int grow) { (void)obj; (void)grow; }
static inline void lv_obj_set_style_pad_row(lv_obj_t *obj, int pad, int selector) { (void)obj; (void)pad; (void)selector; }
static inline void lv_obj_set_style_pad_left(lv_obj_t *obj, int pad, int selector) { (void)obj; (void)pad; (void)selector; }
static inline void lv_label_set_long_mode(lv_obj_t *obj, int mode) { (void)obj; (void)mode; }

// LVGL symbols
#define LV_SYMBOL_WIFI "WIFI"
#define LV_SYMBOL_LOOP "LOOP"
#define LV_SYMBOL_POWER "POWER"
#define LV_SYMBOL_EDIT "EDIT"
#define LV_SYMBOL_IMAGE "IMAGE"
#define LV_SYMBOL_SETTINGS "SETTINGS"

struct _lv_indev_t {
    int dummy;
};

// Colors
static inline lv_color_t lv_color_white(void) { return 0xFFFFFF; }
static inline lv_color_t lv_color_black(void) { return 0x000000; }
static inline lv_color_t lv_color_hex(uint32_t hex) { return hex; }

// Stubs for functions
static inline lv_obj_t *lv_obj_create(lv_obj_t *parent) { (void)parent; return (lv_obj_t *)1; }
static inline lv_obj_t *lv_label_create(lv_obj_t *parent) { (void)parent; return (lv_obj_t *)1; }
static inline lv_obj_t *lv_list_create(lv_obj_t *parent) { (void)parent; return (lv_obj_t *)1; }
static inline lv_obj_t *lv_bar_create(lv_obj_t *parent) { (void)parent; return (lv_obj_t *)1; }
static inline lv_group_t *lv_group_create(void) { return (lv_group_t *)1; }

static inline void lv_obj_set_size(lv_obj_t *obj, int w, int h) { (void)obj; (void)w; (void)h; }
static inline void lv_obj_align(lv_obj_t *obj, int align, int x, int y) { (void)obj; (void)align; (void)x; (void)y; }
static inline void lv_obj_align_to(lv_obj_t *obj, lv_obj_t *base, int align, int x, int y) { (void)obj; (void)base; (void)align; (void)x; (void)y; }
static inline void lv_obj_set_style_text_font(lv_obj_t *obj, const lv_font_t *font, int selector) { (void)obj; (void)font; (void)selector; }
static inline void lv_obj_set_style_text_align(lv_obj_t *obj, int align, int selector) { (void)obj; (void)align; (void)selector; }
static inline void lv_obj_set_style_text_color(lv_obj_t *obj, lv_color_t color, int selector) { (void)obj; (void)color; (void)selector; }
static inline void lv_obj_set_style_bg_color(lv_obj_t *obj, lv_color_t color, int selector) { (void)obj; (void)color; (void)selector; }
static inline void lv_obj_set_style_bg_opa(lv_obj_t *obj, lv_opa_t opa, int selector) { (void)obj; (void)opa; (void)selector; }
static inline void lv_obj_set_style_border_color(lv_obj_t *obj, lv_color_t color, int selector) { (void)obj; (void)color; (void)selector; }
static inline void lv_obj_set_style_border_width(lv_obj_t *obj, int width, int selector) { (void)obj; (void)width; (void)selector; }
static inline void lv_obj_set_style_border_side(lv_obj_t *obj, int side, int selector) { (void)obj; (void)side; (void)selector; }
static inline void lv_obj_set_style_radius(lv_obj_t *obj, int r, int selector) { (void)obj; (void)r; (void)selector; }
static inline void lv_obj_set_style_pad_all(lv_obj_t *obj, int pad, int selector) { (void)obj; (void)pad; (void)selector; }
static inline void lv_obj_set_style_pad_hor(lv_obj_t *obj, int pad, int selector) { (void)obj; (void)pad; (void)selector; }
static inline void lv_obj_set_style_pad_top(lv_obj_t *obj, int pad, int selector) { (void)obj; (void)pad; (void)selector; }
static inline void lv_obj_set_style_outline_width(lv_obj_t *obj, int w, int selector) { (void)obj; (void)w; (void)selector; }

static inline void lv_label_set_text(lv_obj_t *obj, const char *text) { (void)obj; (void)text; }
static inline void lv_bar_set_range(lv_obj_t *obj, int min, int max) { (void)obj; (void)min; (void)max; }
static inline void lv_bar_set_value(lv_obj_t *obj, int val, int anim) { (void)obj; (void)val; (void)anim; }

static inline void lv_obj_clean(lv_obj_t *obj) { (void)obj; }

static inline void lv_obj_add_flag(lv_obj_t *obj, uint32_t f) { (void)obj; (void)f; }
static inline void lv_obj_remove_flag(lv_obj_t *obj, uint32_t f) { (void)obj; (void)f; }
static inline lv_obj_t *lv_list_add_button(lv_obj_t *list, const void *icon, const char *text) { (void)list; (void)icon; (void)text; return (lv_obj_t *)1; }
static inline void lv_obj_scroll_to_view(lv_obj_t *obj, int anim) { (void)obj; (void)anim; }
static inline void lv_obj_scroll_to_y(lv_obj_t *obj, int y, int anim) { (void)obj; (void)y; (void)anim; }
static inline void lv_obj_update_layout(lv_obj_t *obj) { (void)obj; }

typedef enum {
    LV_SCREEN_LOAD_ANIM_NONE,
    LV_SCREEN_LOAD_ANIM_OVER_LEFT,
    LV_SCREEN_LOAD_ANIM_OVER_RIGHT,
    LV_SCREEN_LOAD_ANIM_OVER_TOP,
    LV_SCREEN_LOAD_ANIM_OVER_BOTTOM,
    LV_SCREEN_LOAD_ANIM_MOVE_LEFT,
    LV_SCREEN_LOAD_ANIM_MOVE_RIGHT,
    LV_SCREEN_LOAD_ANIM_MOVE_TOP,
    LV_SCREEN_LOAD_ANIM_MOVE_BOTTOM,
    LV_SCREEN_LOAD_ANIM_FADE_IN,
    LV_SCREEN_LOAD_ANIM_FADE_OUT,
} lv_screen_load_anim_t;

static inline lv_obj_t *lv_screen_active(void) { return (lv_obj_t *)1; }
static inline void lv_screen_load(lv_obj_t *obj) { (void)obj; }
static inline void lv_screen_load_anim(lv_obj_t *scr, lv_screen_load_anim_t anim_type, uint32_t time, uint32_t delay, bool auto_del) {
    (void)scr; (void)anim_type; (void)time; (void)delay; (void)auto_del;
}

static inline uint32_t lv_obj_get_child_count(const lv_obj_t *obj) { (void)obj; return 0; }
static inline uint32_t lv_obj_get_child_cnt(const lv_obj_t *obj) { (void)obj; return 0; }
static inline lv_obj_t *lv_obj_get_child(const lv_obj_t *obj, int32_t idx) { (void)obj; (void)idx; return (lv_obj_t *)1; }

static inline void lv_group_set_default(lv_group_t *g) { (void)g; }
static inline lv_indev_t *lv_indev_create(void) { return (lv_indev_t *)1; }
static inline void lv_indev_delete(lv_indev_t *indev) { (void)indev; }
static inline void lv_indev_set_type(lv_indev_t *indev, int type) { (void)indev; (void)type; }
static inline void lv_indev_set_read_cb(lv_indev_t *indev, void *cb) { (void)indev; (void)cb; }
static inline lv_indev_t *lv_indev_get_next(lv_indev_t *indev) { (void)indev; return NULL; }
static inline int lv_indev_get_type(lv_indev_t *indev) { (void)indev; return LV_INDEV_TYPE_ENCODER; }
static inline void lv_indev_set_group(lv_indev_t *indev, lv_group_t *g) { (void)indev; (void)g; }

static inline void lv_obj_set_hidden(lv_obj_t *obj, bool hidden) { (void)obj; (void)hidden; }
#define lv_obj_clear_flag lv_obj_remove_flag

#define LV_IMAGE_HEADER_MAGIC 0x19
#define LV_COLOR_FORMAT_A8 1
typedef struct { uint32_t magic, cf, w, h, stride; } lv_image_header_t;
typedef struct { lv_image_header_t header; uint32_t data_size; const uint8_t *data; } lv_image_dsc_t;
static inline lv_obj_t *lv_image_create(lv_obj_t *parent) { (void)parent; return (lv_obj_t *)1; }
static inline void lv_image_set_src(lv_obj_t *obj, const void *src) { (void)obj; (void)src; }
static inline void lv_obj_set_style_image_recolor(lv_obj_t *obj, lv_color_t color, int selector) { (void)obj; (void)color; (void)selector; }
static inline void lv_obj_set_style_image_recolor_opa(lv_obj_t *obj, lv_opa_t opa, int selector) { (void)obj; (void)opa; (void)selector; }


#define LV_BAR_MODE_NORMAL 0
#define LV_BAR_MODE_RANGE 1
static inline void lv_bar_set_mode(lv_obj_t *obj,int mode) { (void)obj; (void)mode; }
static inline void lv_bar_set_start_value(lv_obj_t *obj,int val,int anim) { (void)obj; (void)val; (void)anim; }

#endif // MOCK_LVGL_H
