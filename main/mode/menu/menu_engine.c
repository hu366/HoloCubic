/**
 * @file    menu_engine.c
 * @brief   菜单导航引擎 —— TOP/SUB 二级状态机
 *
 *   TOP  主菜单轮播（左/右倾斜切换，短按进入子页面）
 *   SUB  子页面内（输入全部转发给注册的回调，menu_ui 管理页面栈）
 */

#include <stdio.h>
#include "menu_engine.h"
#include "app_config.h"
#include "esp_timer.h"

static menu_item_t       s_selected       = MENU_ITEM_POMODORO;
static menu_level_t      s_level          = MENU_LEVEL_TOP;
static menu_scroll_dir_t s_scroll_dir     = MENU_DIR_NONE;
static int64_t           s_last_scroll_us = 0;
static menu_sub_cb_t     s_sub_cb         = NULL;

static const char* s_item_names[MENU_ITEM_COUNT] = {
    "番茄钟", "天气", "时钟", "音乐", "动画看板",
};

static bool scroll_cooldown_ok(void) {
    int64_t now = esp_timer_get_time();
    if (now - s_last_scroll_us < (int64_t)(MENU_SCROLL_COOLDOWN_MS * 1000))
        return false;
    s_last_scroll_us = now;
    return true;
}

/* ---- 生命周期 ---- */

void menu_engine_init(void) {
    s_selected = MENU_ITEM_POMODORO;
    s_level    = MENU_LEVEL_TOP;
    s_scroll_dir = MENU_DIR_NONE;
    s_last_scroll_us = 0;
    s_sub_cb = NULL;
    printf("[MENU] init ok\n");
}

/* ---- 查询 ---- */

menu_item_t       menu_engine_get_selected(void)     { return s_selected; }
menu_level_t      menu_engine_get_level(void)        { return s_level; }
menu_scroll_dir_t menu_engine_get_scroll_dir(void)   { return s_scroll_dir; }
void              menu_engine_clear_scroll_dir(void) { s_scroll_dir = MENU_DIR_NONE; }

const char* menu_engine_get_item_name(menu_item_t item) {
    return (item < MENU_ITEM_COUNT) ? s_item_names[item] : "???";
}

/* ---- 输入 ---- */

void menu_engine_on_tilt(imu_tilt_dir_t dir) {
    switch (s_level) {
    case MENU_LEVEL_TOP:
        if (dir == IMU_TILT_RIGHT) {
            if (!scroll_cooldown_ok()) return;
            s_selected = (s_selected + 1) % MENU_ITEM_COUNT;
            s_scroll_dir = MENU_DIR_RIGHT;
        } else if (dir == IMU_TILT_LEFT) {
            if (!scroll_cooldown_ok()) return;
            s_selected = (s_selected == 0) ? (MENU_ITEM_COUNT - 1)
                                           : (s_selected - 1);
            s_scroll_dir = MENU_DIR_LEFT;
        }
        if (s_scroll_dir != MENU_DIR_NONE)
            printf("[MENU] -> %s\n", s_item_names[s_selected]);
        break;
    case MENU_LEVEL_SUB:
        if (s_sub_cb) s_sub_cb(dir, 0, false);
        break;
    }
}

void menu_engine_on_btn(bool short_press) {
    if (!short_press) return;

    switch (s_level) {
    case MENU_LEVEL_TOP:
        s_level = MENU_LEVEL_SUB;
        s_scroll_dir = MENU_DIR_NONE;
        printf("[MENU] 进入: %s\n", s_item_names[s_selected]);
        break;
    case MENU_LEVEL_SUB:
        if (s_sub_cb) s_sub_cb(IMU_TILT_LEVEL, 0, true);
        break;
    }
}

void menu_engine_on_rotary(int8_t step) {
    if (s_level == MENU_LEVEL_SUB && s_sub_cb)
        s_sub_cb(IMU_TILT_LEVEL, step, false);
    (void)step;
}

void menu_engine_tick(uint32_t dt_ms) { (void)dt_ms; }

/* ---- 子页面交互 ---- */

void menu_engine_set_sub_callback(menu_sub_cb_t cb) { s_sub_cb = cb; }

void menu_engine_go_back(void) {
    s_level = MENU_LEVEL_TOP;
    s_scroll_dir = MENU_DIR_NONE;
    s_sub_cb = NULL;
    printf("[MENU] <- TOP\n");
}
