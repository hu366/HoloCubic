/**
 * @file    test_menu_ui.c
 * @brief   Task 13: menu_ui LVGL 渲染测试
 *
 * 独立测试文件，不编译进主固件。
 * 测试：主菜单轮播页面创建/销毁、选中高亮标志、页面切换动画方向、
 *       确认弹窗显隐、图标/标签坐标布局。
 *
 * UI 测试在无硬件时多是 API 契约验证。实际渲染效果需上板确认。
 */

#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* ---- 避免依赖 LVGL 头文件，用 intptr_t 代替 lv_obj_t* ---- */
typedef void* lv_obj_handle_t;
#define LV_NULL  NULL

/* ---- 菜单项（与 menu_engine.h 一致） ---- */
typedef enum {
    MI_POMODORO = 0,
    MI_WEATHER,
    MI_CLOCK,
    MI_MUSIC,
    MI_ANIMATION,
    MI_COUNT
} mi_t;

/* ---- 滚动方向（与 menu_engine.h 一致） ---- */
typedef enum {
    SCROLL_NONE  =  0,
    SCROLL_LEFT  = -1,
    SCROLL_RIGHT =  1,
} scroll_dir_t;

/* ---- 确认弹窗按钮 ---- */
typedef enum {
    CONFIRM_NONE = 0,
    CONFIRM_YES,
    CONFIRM_NO,
} confirm_btn_t;

/* ================================================================
 *  API 声明（与 Task 14 menu_ui.h 一致）
 * ================================================================ */

void menu_ui_init(void);
lv_obj_handle_t menu_ui_get_screen(void);

/** 刷新轮播：根据 selected_item 显示对应图标，scroll_dir 决定动画方向 */
void menu_ui_update_carousel(int selected, scroll_dir_t dir);

/** 进入子页面 → 销毁轮播 UI */
void menu_ui_enter_sub(void);

/** 退出子页面 → 重建轮播 UI */
void menu_ui_exit_sub(void);

/** 确认退出弹窗显隐 + 焦点更新 */
void menu_ui_show_confirm_dialog(bool yes_focused);
void menu_ui_hide_confirm_dialog(void);

/** 确认弹窗当前状态 */
bool menu_ui_is_confirm_showing(void);

/** 获取当前选中的菜单项索引（缓存值） */
int menu_ui_get_cached_selection(void);

/* ================================================================
 *  Stub 实现 —— 有意不实现实际行为
 * ================================================================ */

static lv_obj_handle_t s_stub_screen    = LV_NULL;
static bool            s_stub_confirm   = false;
static int             s_stub_selection = -1;  /* 哨兵值 */

void menu_ui_init(void)                       { s_stub_screen = LV_NULL; }
lv_obj_handle_t menu_ui_get_screen(void)       { return s_stub_screen; }
void menu_ui_update_carousel(int sel, scroll_dir_t dir) { (void)sel; (void)dir; }
void menu_ui_enter_sub(void)                   { s_stub_screen = LV_NULL; }
void menu_ui_exit_sub(void)                    { s_stub_screen = LV_NULL; }
void menu_ui_show_confirm_dialog(bool yf)       { (void)yf; s_stub_confirm = false; /* 故意不设 true */ }
void menu_ui_hide_confirm_dialog(void)          { /* no-op */ }
bool menu_ui_is_confirm_showing(void)           { return s_stub_confirm; }
int  menu_ui_get_cached_selection(void)         { return s_stub_selection; }

/* ================================================================
 *  测试框架
 * ================================================================ */

static int s_pass = 0;
static int s_fail = 0;

#define ASSERT(cond, msg) do {                              \
    if (cond) {                                             \
        printf("  [PASS] %s\n", msg); s_pass++;             \
    } else {                                                \
        printf("  [FAIL] %s\n", msg); s_fail++;             \
    }                                                       \
} while(0)

/* ================================================================
 *  类型级别测试（不依赖实现，预计 PASS）
 * ================================================================ */

static void test_type_item_count(void) {
    ASSERT(MI_COUNT == 5, "MI_COUNT == 5");
}

static void test_type_scroll_values(void) {
    ASSERT(SCROLL_LEFT  == -1, "SCROLL_LEFT == -1");
    ASSERT(SCROLL_NONE  ==  0, "SCROLL_NONE == 0");
    ASSERT(SCROLL_RIGHT ==  1, "SCROLL_RIGHT == 1");
}

static void test_type_confirm_values(void) {
    ASSERT(CONFIRM_NONE == 0, "CONFIRM_NONE == 0");
    ASSERT(CONFIRM_YES  == 1, "CONFIRM_YES == 1");
    ASSERT(CONFIRM_NO   == 2, "CONFIRM_NO == 2");
}

/* ================================================================
 *  状态测试（依赖实现，预计 FAIL）
 * ================================================================ */

static void test_init_creates_screen(void) {
    menu_ui_init();
    ASSERT(menu_ui_get_screen() != LV_NULL,
           "init: screen created (non-NULL)");
}

static void test_update_carousel_sets_selection(void) {
    menu_ui_init();
    menu_ui_update_carousel(MI_WEATHER, SCROLL_RIGHT);
    ASSERT(menu_ui_get_cached_selection() == MI_WEATHER,
           "update: cached selection = WEATHER after scroll");
}

static void test_enter_sub_destroys_carousel(void) {
    /* 先 init，然后进入子页面 */
    menu_ui_init();
    menu_ui_update_carousel(MI_POMODORO, SCROLL_NONE);
    menu_ui_enter_sub();
    ASSERT(menu_ui_get_screen() == LV_NULL,
           "enter_sub: carousel screen destroyed (NULL)");
}

static void test_exit_sub_recreates_carousel(void) {
    menu_ui_init();
    menu_ui_enter_sub();
    menu_ui_exit_sub();
    ASSERT(menu_ui_get_screen() != LV_NULL,
           "exit_sub: carousel screen recreated (non-NULL)");
}

static void test_confirm_dialog_show(void) {
    menu_ui_init();
    menu_ui_show_confirm_dialog(true);
    ASSERT(menu_ui_is_confirm_showing() == true,
           "show_confirm_dialog: is_confirm_showing = true");
}

static void test_confirm_dialog_hide(void) {
    menu_ui_init();
    menu_ui_show_confirm_dialog(true);
    menu_ui_hide_confirm_dialog();
    ASSERT(menu_ui_is_confirm_showing() == false,
           "hide_confirm_dialog: is_confirm_showing = false");
}

static void test_scroll_left_animation(void) {
    menu_ui_init();
    /* 左倾 → 当前项滑出右边，新项从左边滑入 */
    menu_ui_update_carousel(MI_ANIMATION, SCROLL_LEFT);
    ASSERT(menu_ui_get_cached_selection() == MI_ANIMATION,
           "scroll LEFT: selection = ANIMATION (wrap from 0)");
}

static void test_scroll_right_animation(void) {
    menu_ui_init();
    menu_ui_update_carousel(MI_POMODORO, SCROLL_NONE);
    menu_ui_update_carousel(MI_WEATHER, SCROLL_RIGHT);
    ASSERT(menu_ui_get_cached_selection() == MI_WEATHER,
           "scroll RIGHT: selection = WEATHER");
}

static void test_confirm_yes_vs_no_focus(void) {
    menu_ui_init();
    /* 焦点在"是" */
    menu_ui_show_confirm_dialog(true);
    /* 焦点切换到"否" → stub 不追踪焦点，此测试验证 API 存在 */
    menu_ui_show_confirm_dialog(false);
    ASSERT(menu_ui_is_confirm_showing() == true,
           "confirm focus toggle: dialog stays showing");
}

static void test_multiple_enter_exit_sub(void) {
    /* 反复进入/退出子页面不崩溃 */
    menu_ui_init();
    for (int i = 0; i < 3; i++) {
        menu_ui_enter_sub();
        ASSERT(menu_ui_get_screen() == LV_NULL,
               "enter/exit cycle: screen NULL in sub");
        menu_ui_exit_sub();
        ASSERT(menu_ui_get_screen() != LV_NULL,
               "enter/exit cycle: screen restored after exit");
    }
}

/* ================================================================
 *  运行入口
 * ================================================================ */

void test_menu_ui_run_all(void) {
    s_pass = 0;
    s_fail = 0;

    printf("\n========== menu_ui tests ==========\n\n");

    printf("--- 类型测试（不依赖实现）---\n");
    test_type_item_count();
    test_type_scroll_values();
    test_type_confirm_values();

    printf("\n--- 状态测试（依赖实现，预计 FAIL）---\n");
    test_init_creates_screen();
    test_update_carousel_sets_selection();
    test_enter_sub_destroys_carousel();
    test_exit_sub_recreates_carousel();
    test_confirm_dialog_show();
    test_confirm_dialog_hide();
    test_scroll_left_animation();
    test_scroll_right_animation();
    test_confirm_yes_vs_no_focus();
    test_multiple_enter_exit_sub();

    printf("\n========== %d PASS, %d FAIL ==========\n\n", s_pass, s_fail);
}
