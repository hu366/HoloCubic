/**
 * @file    menu_ui.c
 * @brief   菜单 UI：轮播滑动 + 栈式子页面(可滚动按钮列表) + 嵌套 push/pop 动画
 *
 * 架构：
 *   Core 1 (sensor_task) → stack_input_cb → 只改 focus_index / 设 s_btn_pending
 *   Core 0 (lvgl_task)   → apply_sub_updates / tick_anim → 所有 LVGL 操作 + 动画
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "menu_ui.h"
#include "app_config.h"
#include "esp_timer.h"
#include "pomodoro.h"
#include "weather.h"

/* ================================================================
 *  常量
 * ================================================================ */

#define ANIM_MS   200
#define SCROLL_MS 150
#define STACK_MAX 6
#define BTN_H     38
#define BTN_GAP   8
#define BTN_STEP  (BTN_H + BTN_GAP)
#define VISIBLE   3
#define VP_H      (VISIBLE * BTN_STEP - BTN_GAP)
#define SUB_COOLDOWN_US 250000

/* ================================================================
 *  显示名（ASCII，Task 73-74 中文字库就绪后替换）
 * ================================================================ */

static const char* s_names[MENU_ITEM_COUNT] = {
    "Pomodoro", "Weather", "Clock", "Music", "Animation",
};

/* ================================================================
 *  动画引擎
 * ================================================================ */

typedef enum { A_NONE=0, A_PAGE, A_CAROUSEL, A_SCROLL } anim_t;

static struct {
    bool    on;   anim_t kind;  int64_t t0;
    lv_obj_t *o1, *o2;  int32_t x1s,x1e, x2s,x2e;
    lv_obj_t *scroller, *cursor;  int32_t sy_s,sy_e, cy_s,cy_e;
} s_anim;

/* ================================================================
 *  轮播
 * ================================================================ */

static lv_obj_t *s_screen   = NULL;
static lv_obj_t *s_carousel = NULL;
static int       s_carousel_sel = 0;

/* ================================================================
 *  子页面栈 + 页面注册表
 * ================================================================ */

static menu_page_t *s_stack[STACK_MAX];
static int          s_depth = 0;
static menu_page_creator_t s_creators[MENU_ITEM_COUNT] = {0};
static int64_t s_sub_tilt_last_us = 0;
static bool    s_sub_tilt_armed   = true; /* 必须回正才能再次触发 */

/* Core 1 → Core 0 通信 */
static int  s_btn_pending = -1;  /* -1=无, >=0 按钮被按下 */
static bool s_focus_dirty = false;

/* ================================================================
 *  内部辅助
 * ================================================================ */

static void refresh(void)        { lv_refr_now(NULL); }
static int  sw(void)             { return LCD_HOR_RES; }
int32_t menu_ui_ease_out(int32_t t, int32_t d) {
    int32_t p = (t * 256) / d; if (p>256) p=256;
    return 256 - ((256-p)*(256-p))/256;
}
static void lbl_style(lv_obj_t *l) {
    lv_obj_set_style_border_width(l, 0, 0);
    lv_obj_set_style_bg_opa(l, LV_OPA_TRANSP, 0);
}
static void lbl_color(lv_obj_t *l, bool on) {
    lv_obj_set_style_text_color(l, on ? lv_color_hex(0x4FC3F7)
                                      : lv_color_hex(0x888888), 0);
}
static menu_page_t* page_top(void) { return s_depth>0 ? s_stack[s_depth-1] : NULL; }

/* ================================================================
 *  轮播项
 * ================================================================ */

static lv_obj_t* make_carousel(int idx, int x) {
    lv_obj_t *c = lv_obj_create(s_screen);
    lv_obj_set_size(c, sw(), LCD_VER_RES);
    lv_obj_set_style_bg_opa(c, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(c, 0, 0);
    lv_obj_set_style_pad_all(c, 0, 0);
    if (x) lv_obj_set_x(c, x);

    lv_obj_t *t = lv_label_create(c);
    lv_label_set_text(t, s_names[idx]);
    lv_obj_set_style_text_font(t, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(t, lv_color_white(), 0);
    lbl_style(t); lv_obj_center(t);

    char b[16]; snprintf(b, sizeof(b), "%d/%d", idx+1, MENU_ITEM_COUNT);
    lv_obj_t *p = lv_label_create(c);
    lv_label_set_text(p, b);
    lv_obj_set_style_text_font(p, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(p, lv_color_hex(0x888888), 0);
    lbl_style(p); lv_obj_align(p, LV_ALIGN_BOTTOM_MID, 0, -12);

    for (int i=0; i<MENU_ITEM_COUNT; i++) {
        lv_obj_t *d = lv_obj_create(c);
        lv_obj_set_size(d, 8, 8); lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(d, (i==idx) ? lv_color_white():lv_color_hex(0x333333), 0);
        lv_obj_set_style_border_width(d, 0, 0);
        lv_obj_align(d, LV_ALIGN_BOTTOM_MID, (i-2)*16, -30);
    }
    return c;
}

/* ================================================================
 *  按钮列表滚动计算
 * ================================================================ */

static int scroll_target(int idx, int count) {
    if (count <= VISIBLE) return 0;
    int mid = VISIBLE/2, ideal = (mid - idx) * BTN_STEP;
    int max_y = 0, min_y = -(count - VISIBLE) * BTN_STEP;
    return (ideal > max_y) ? max_y : (ideal < min_y) ? min_y : ideal;
}

static int cursor_vp_y(int focus, int scroller_y) {
    int y = scroller_y + focus * BTN_STEP;
    int max_y = (VISIBLE-1) * BTN_STEP;
    return (y < 0) ? 0 : (y > max_y) ? max_y : y;
}

static void page_apply_focus(menu_page_t *pg, bool anim) {
    if (!pg || !pg->scroller) return;
    int tgt_sy = scroll_target(pg->focus_index, pg->btn_count);

    for (int i=0; i<pg->btn_count; i++)
        if (pg->btn_labels) lbl_color(pg->btn_labels[i], i == pg->focus_index);

    if (pg->focus_cursor)
        lv_obj_set_y(pg->focus_cursor, cursor_vp_y(pg->focus_index, tgt_sy));

    int cur_sy = lv_obj_get_y(pg->scroller);
    int cur_cy = pg->focus_cursor ? lv_obj_get_y(pg->focus_cursor) : 0;
    int tgt_cy = cursor_vp_y(pg->focus_index, tgt_sy);

    if (anim && (tgt_sy != cur_sy || tgt_cy != cur_cy)) {
        s_anim.on=true; s_anim.kind=A_SCROLL; s_anim.t0=esp_timer_get_time();
        s_anim.scroller=pg->scroller; s_anim.sy_s=cur_sy; s_anim.sy_e=tgt_sy;
        s_anim.cursor=pg->focus_cursor; s_anim.cy_s=cur_cy; s_anim.cy_e=tgt_cy;
    } else {
        lv_obj_set_y(pg->scroller, tgt_sy);
        if (pg->focus_cursor) lv_obj_set_y(pg->focus_cursor, tgt_cy);
    }
    refresh();
}

/* ================================================================
 *  页面栈操作
 * ================================================================ */

static void page_destroy(menu_page_t *pg) {
    if (!pg) return;
    if (pg->container) lv_obj_delete(pg->container);
    free(pg->btn_containers); free(pg->btn_labels); free(pg);
}

static void page_push(menu_page_t *pg) {
    if (s_depth < STACK_MAX) s_stack[s_depth++] = pg;
    else page_destroy(pg);
}

static menu_page_t* page_pop(void) {
    return (s_depth>0) ? s_stack[--s_depth] : NULL;
}

/* ---- 页面间滑动动画 ---- */
static void anim_page_slide(lv_obj_t *slide_out, lv_obj_t *slide_in) {
    s_anim.on=true; s_anim.kind=A_PAGE; s_anim.t0=esp_timer_get_time();
    s_anim.o1=slide_out;  s_anim.x1s=0;  s_anim.x1e=-sw();
    s_anim.o2=slide_in;   s_anim.x2s=sw(); s_anim.x2e=0;
}

static void anim_page_slide_reverse(lv_obj_t *slide_out, lv_obj_t *slide_in) {
    s_anim.on=true; s_anim.kind=A_PAGE; s_anim.t0=esp_timer_get_time();
    s_anim.o1=slide_out;  s_anim.x1s=0;  s_anim.x1e=sw();
    s_anim.o2=slide_in;   s_anim.x2s=-sw(); s_anim.x2e=0;
}

/* ================================================================
 *  Core 1 → Core 0 输入回调
 * ================================================================ */

static void stack_input_cb(imu_tilt_dir_t tilt, int8_t rotary, bool btn_short) {
    menu_page_t *pg = page_top();
    if (!pg) return;

    if (btn_short) {
        s_btn_pending = pg->focus_index;
        return;
    }

    int64_t now = esp_timer_get_time();

    /* ---- 旋钮切换（MENU_INPUT_MODE=1 时倾斜已被 menu_engine 过滤掉） ---- */
    if (rotary != 0) {
        if (now - s_sub_tilt_last_us < SUB_COOLDOWN_US) return;
        s_sub_tilt_last_us = now;

        int old = pg->focus_index;
        if (rotary > 0)
            pg->focus_index = (pg->focus_index + 1) % pg->btn_count;
        else
            pg->focus_index = (pg->focus_index == 0) ? (pg->btn_count - 1) : (pg->focus_index - 1);

        if (pg->focus_index != old) {
            s_focus_dirty = true;
            printf("[MENU-UI] focus %d->%d\n", old, pg->focus_index);
        }
        return;
    }

    /* ---- 倾斜切换（MENU_INPUT_MODE=0 时旋钮已被过滤掉） ---- */
    if (tilt == IMU_TILT_LEVEL) {
        s_sub_tilt_armed = true;
        return;
    }
    if (tilt != IMU_TILT_FRONT && tilt != IMU_TILT_BACK) return;
    if (!s_sub_tilt_armed) return;

    if (now - s_sub_tilt_last_us < SUB_COOLDOWN_US) return;
    s_sub_tilt_last_us = now;

    int old = pg->focus_index;
    if (tilt == IMU_TILT_FRONT)
        pg->focus_index = (pg->focus_index+1) % pg->btn_count;
    else
        pg->focus_index = (pg->focus_index==0) ? (pg->btn_count-1) : (pg->focus_index-1);

    if (pg->focus_index != old) {
        s_focus_dirty = true;
        s_sub_tilt_armed = false;
        printf("[MENU-UI] focus %d->%d\n", old, pg->focus_index);
    }
}

/* ================================================================
 *  公开：menu_ui_page_create
 * ================================================================ */

menu_page_t* menu_ui_page_create(const char *title,
                                  const char *buttons[], int count,
                                  void (*on_btn)(int index))
{
    if (!s_screen || count<1) return NULL;

    menu_page_t *pg = calloc(1, sizeof(menu_page_t));
    if (!pg) return NULL;
    pg->title=title; pg->btn_count=count; pg->focus_index=0; pg->on_btn=on_btn;
    pg->btn_containers = calloc(count, sizeof(lv_obj_t*));
    pg->btn_labels     = calloc(count, sizeof(lv_obj_t*));

    pg->container = lv_obj_create(s_screen);
    lv_obj_set_size(pg->container, sw(), LCD_VER_RES);
    lv_obj_set_x(pg->container, sw());
    lv_obj_set_style_bg_color(pg->container, lv_color_hex(0x16213e), 0);
    lv_obj_set_style_border_width(pg->container, 0, 0);
    lv_obj_set_style_pad_all(pg->container, 0, 0);

    pg->title_label = lv_label_create(pg->container);
    lv_label_set_text(pg->title_label, title);
    lv_obj_set_style_text_font(pg->title_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(pg->title_label, lv_color_white(), 0);
    lbl_style(pg->title_label);
    lv_obj_align(pg->title_label, LV_ALIGN_TOP_MID, 0, 10);

    int vp_y = (LCD_VER_RES - VP_H)/2 + 10;
    lv_obj_t *vp = lv_obj_create(pg->container);
    lv_obj_set_size(vp, 200, VP_H);
    lv_obj_set_style_bg_opa(vp, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(vp, 0, 0);
    lv_obj_set_style_clip_corner(vp, true, 0);
    lv_obj_set_style_pad_all(vp, 0, 0);
    lv_obj_align(vp, LV_ALIGN_TOP_MID, 0, vp_y);

    int sc_h = count * BTN_STEP;
    lv_obj_t *sc = lv_obj_create(vp);
    lv_obj_set_size(sc, 200, sc_h);
    lv_obj_set_style_bg_opa(sc, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(sc, 0, 0);
    lv_obj_set_style_pad_all(sc, 0, 0);
    pg->scroller = sc;

    for (int i=0; i<count; i++) {
        lv_obj_t *btn = lv_obj_create(sc);
        lv_obj_set_size(btn, 190, BTN_H);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x0f3460), 0);
        lv_obj_set_style_border_width(btn, 0, 0);
        lv_obj_set_style_radius(btn, 8, 0);
        lv_obj_align(btn, LV_ALIGN_TOP_MID, 0, i*BTN_STEP);
        pg->btn_containers[i] = btn;

        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, buttons[i]);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
        lbl_style(lbl); lv_obj_center(lbl);
        pg->btn_labels[i] = lbl;
    }

    pg->focus_cursor = lv_obj_create(vp);
    lv_obj_set_size(pg->focus_cursor, 196, BTN_H+4);
    lv_obj_set_style_bg_opa(pg->focus_cursor, LV_OPA_10, 0);
    lv_obj_set_style_bg_color(pg->focus_cursor, lv_color_hex(0x4FC3F7), 0);
    lv_obj_set_style_border_color(pg->focus_cursor, lv_color_hex(0x4FC3F7), 0);
    lv_obj_set_style_border_width(pg->focus_cursor, 2, 0);
    lv_obj_set_style_border_opa(pg->focus_cursor, LV_OPA_60, 0);
    lv_obj_set_style_radius(pg->focus_cursor, 10, 0);

    lv_obj_t *hint = lv_label_create(pg->container);
    lv_label_set_text(hint, "< Tilt ^v >");
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(0x444444), 0);
    lbl_style(hint);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -8);

    page_apply_focus(pg, false);
    return pg;
}

/* ================================================================
 *  公开：menu_ui_push_page / menu_ui_go_back
 * ================================================================ */

void menu_ui_push_page(menu_page_t *page) {
    if (!page || s_anim.on) return;
    if (s_depth == 0) return;  /* 需要先有 carousel→sub 的入口 */

    menu_page_t *prev = page_top();
    page_push(page);
    menu_engine_set_sub_callback(stack_input_cb);
    s_sub_tilt_last_us = 0;
    s_sub_tilt_armed   = true;
    anim_page_slide(prev->container, page->container);
    printf("[MENU-UI] push \"%s\" depth=%d\n", page->title, s_depth);
}

void menu_ui_go_back(void) {
    if (!s_screen || s_anim.on || s_depth == 0) return;

    menu_page_t *cur = page_top();
    if (s_depth == 1) {
        lv_obj_set_x(s_carousel, -sw());
        anim_page_slide_reverse(cur->container, s_carousel);
    } else {
        menu_page_t *prev = s_stack[s_depth-2];
        lv_obj_set_x(prev->container, -sw());
        anim_page_slide_reverse(cur->container, prev->container);
    }
    printf("[MENU-UI] pop \"%s\" depth=%d->%d\n", cur->title, s_depth, s_depth-1);
}

/* ================================================================
 *  公开：注册表 + 入口
 * ================================================================ */

void menu_ui_register_creator(menu_item_t item, menu_page_creator_t cb) {
    if (item < MENU_ITEM_COUNT) s_creators[item] = cb;
}

/* 占位回调：未注册的菜单项显示 "Coming Soon" */
static void placeholder_back_cb(int index) {
    if (index == 0) menu_ui_go_back();
}

static menu_page_t* placeholder_creator(void) {
    const char *name = s_names[s_carousel_sel];
    const char *btns[] = {"Back"};
    menu_page_t *pg = menu_ui_page_create(name, btns, 1, placeholder_back_cb);
    if (!pg) return NULL;

    lv_obj_t *msg = lv_label_create(pg->container);
    lv_label_set_text(msg, "Coming Soon");
    lv_obj_set_style_text_font(msg, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(msg, lv_color_hex(0x666666), 0);
    lv_obj_set_style_border_width(msg, 0, 0);
    lv_obj_set_style_bg_opa(msg, LV_OPA_TRANSP, 0);
    lv_obj_align(msg, LV_ALIGN_CENTER, 0, 0);
    printf("[MENU-UI] placeholder for \"%s\"\n", name);
    return pg;
}

void menu_ui_enter_sub(void) {
    if (!s_screen || s_anim.on) return;
    menu_page_creator_t cb = s_creators[s_carousel_sel];
    if (!cb) cb = placeholder_creator; /* 未注册的显示占位页 */
    if (!cb) return;

    menu_page_t *pg = cb();
    if (!pg) return;
    page_push(pg);
    menu_engine_set_sub_callback(stack_input_cb);
    s_sub_tilt_last_us = 0;
    s_sub_tilt_armed   = true;
    anim_page_slide(s_carousel, pg->container);
    printf("[MENU-UI] enter_sub -> \"%s\"\n", pg->title);
}

/* ================================================================
 *  公开：生命周期
 * ================================================================ */

void menu_ui_init(void) {
    memset(&s_anim, 0, sizeof(s_anim));
    s_depth=0; s_btn_pending=-1; s_focus_dirty=false;

    s_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(0x1a1a2e), 0);
    s_carousel = make_carousel(MENU_ITEM_POMODORO, 0);
    s_carousel_sel = MENU_ITEM_POMODORO;
    printf("[MENU-UI] init ok\n");
}

void menu_ui_deinit(void) {
    s_anim.on = false;
    while (s_depth > 0) page_destroy(page_pop());
    if (s_carousel) { lv_obj_delete(s_carousel); s_carousel=NULL; }
    if (s_screen)   { lv_obj_delete(s_screen);   s_screen=NULL; }
    s_carousel_sel=0;
    printf("[MENU-UI] deinit ok\n");
}

lv_obj_t* menu_ui_get_screen(void) { return s_screen; }

/* ================================================================
 *  公开：轮播
 * ================================================================ */

void menu_ui_update_carousel(int selected, int dir) {
    if (!s_screen || !s_carousel || s_anim.on) return;
    int w=sw(), nx=(dir>0)?w:-w, oe=(dir>0)?-w:w;
    lv_obj_t *nw = make_carousel(selected, nx);
    s_anim.on=true; s_anim.kind=A_CAROUSEL; s_anim.t0=esp_timer_get_time();
    s_anim.o1=s_carousel; s_anim.x1s=0; s_anim.x1e=oe;
    s_anim.o2=nw;          s_anim.x2s=nx; s_anim.x2e=0;
    s_carousel=NULL; s_carousel_sel=selected;
}

/* ================================================================
 *  手动动画 tick
 * ================================================================ */

void menu_ui_tick_anim(void) {
    if (!s_anim.on) return;
    int64_t ms = (esp_timer_get_time() - s_anim.t0) / 1000;
    int dur = (s_anim.kind == A_SCROLL) ? SCROLL_MS : ANIM_MS;

    if (s_anim.kind == A_SCROLL) {
        if (!s_anim.scroller) { s_anim.on=false; return; }
        if (ms >= dur) {
            lv_obj_set_y(s_anim.scroller, s_anim.sy_e);
            if (s_anim.cursor) lv_obj_set_y(s_anim.cursor, s_anim.cy_e);
            refresh(); s_anim.on=false; return;
        }
        int32_t e = menu_ui_ease_out((int32_t)ms, dur);
        int32_t sy = s_anim.sy_s + ((s_anim.sy_e-s_anim.sy_s)*e)/256;
        int32_t cy = s_anim.cy_s + ((s_anim.cy_e-s_anim.cy_s)*e)/256;
        lv_obj_set_y(s_anim.scroller, sy);
        if (s_anim.cursor) lv_obj_set_y(s_anim.cursor, cy);
        refresh(); return;
    }

    if (ms >= dur) {
        int32_t e = menu_ui_ease_out(dur, dur);
        lv_obj_set_x(s_anim.o1, s_anim.x1s + ((s_anim.x1e-s_anim.x1s)*e)/256);
        lv_obj_set_x(s_anim.o2, s_anim.x2s + ((s_anim.x2e-s_anim.x2s)*e)/256);
        refresh();
        if (s_anim.kind == A_CAROUSEL) {
            lv_obj_delete(s_anim.o1); s_carousel = s_anim.o2;
        } else if (s_anim.kind == A_PAGE) {
            menu_page_t *top = page_top();
            if (top && top->container == s_anim.o1) { page_pop(); page_destroy(top); }
            if (s_depth == 0) { menu_engine_set_sub_callback(NULL); menu_engine_go_back(); }
        }
        s_anim.on = false; return;
    }

    int32_t e = menu_ui_ease_out((int32_t)ms, dur);
    lv_obj_set_x(s_anim.o1, s_anim.x1s + ((s_anim.x1e-s_anim.x1s)*e)/256);
    lv_obj_set_x(s_anim.o2, s_anim.x2s + ((s_anim.x2e-s_anim.x2s)*e)/256);
    refresh();
}

bool menu_ui_is_animating(void) { return s_anim.on; }

/* ================================================================
 *  Core 0 统一处理：按钮回调 + 焦点刷新
 * ================================================================ */

void menu_ui_apply_sub_updates(void) {
    /* 焦点变化 */
    if (s_focus_dirty) {
        s_focus_dirty = false;
        menu_page_t *pg = page_top();
        if (pg) page_apply_focus(pg, true);
    }
    /* 按钮按下（Core 0 安全执行回调）。
       各页面自行处理 Back（不再由框架硬编码 index 0） */
    if (s_btn_pending >= 0 && !s_anim.on) {
        int idx = s_btn_pending;
        s_btn_pending = -1;
        menu_page_t *pg = page_top();
        if (pg && pg->on_btn) {
            pg->on_btn(idx);  /* Core 0 上下文，可安全操作 LVGL */
        }
    }
}

/* ================================================================
 *  聚合初始化（供 main.c 调用）
 * ================================================================ */

void menu_ui_restore_default_input(void) {
    menu_engine_set_sub_callback(stack_input_cb);
}

void menu_ui_init_modules(void) {
    pomodoro_init();
    weather_init();
    /* 后续模块：clock_alarm_init(); 等 */
}

void menu_ui_tick_modules(uint32_t dt_ms) {
    pomodoro_tick(dt_ms);
    weather_tick(dt_ms);
    /* 后续模块：clock_alarm_tick(dt_ms); 等 */
}

void menu_ui_process_module_updates(void) {
    pomodoro_process_updates();
    weather_process_updates();
    /* 后续模块：clock_alarm_process_updates(); 等 */
}

/* ================================================================
 *  测试：默认创建器 + 嵌套回调
 * ================================================================ */

static void test_nested_cb(int index) {
    if (index == 0) { menu_ui_go_back(); return; }
    char title[32];
    snprintf(title, sizeof(title), "Lv%d Btn%d", s_depth+1, index);
    const char *btns[] = {"Back", "Go Deeper"};
    menu_page_t *sub = menu_ui_page_create(title, btns, 2, test_nested_cb);
    menu_ui_push_page(sub);
}

static menu_page_t* test_creator(void) {
    const char *btns[] = {"Back", "Btn 1", "Btn 2", "Btn 3", "Btn 4", "Btn 5"};
    return menu_ui_page_create(s_names[s_carousel_sel], btns, 6, test_nested_cb);
}

void menu_ui_test_nested(void) {
    menu_ui_register_creator(MENU_ITEM_POMODORO, test_creator);
    printf("[MENU-UI] test nested pages registered\n");
}
