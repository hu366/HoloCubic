/**
 * @file    menu_ui.h
 * @brief   菜单 UI 公开接口：轮播 + 栈式子页面框架
 *
 * === 子页面框架用法 ===
 *
 * 1. 创建页面：
 *      const char *btns[] = {"Back", "Start", "Stop"};
 *      menu_page_t *pg = menu_ui_page_create("Title", btns, 3, my_callback);
 *
 * 2. 注册到菜单项：
 *      menu_ui_register_creator(MENU_ITEM_POMODORO, my_creator);
 *
 * 3. 按钮回调（在 lvgl_task / Core 0 上下文执行，可安全操作 LVGL）：
 *      void my_callback(int index) {
 *          if (index == 0) return;  // Back 框架已自动处理
 *          // index 1 = Start, index 2 = Stop ...
 *      }
 *
 * 4. 嵌套子页面：
 *      void my_callback(int index) {
 *          if (index == 1) {
 *              const char *btns[] = {"Back", "Confirm", "Cancel"};
 *              menu_page_t *sub = menu_ui_page_create("Sub", btns, 3, sub_cb);
 *              menu_ui_push_page(sub);
 *          }
 *      }
 */

#ifndef MENU_UI_H
#define MENU_UI_H

#include "lvgl.h"
#include "menu_engine.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================
 *  menu_page_t —— 一个子页面实例
 * ================================================================ */

typedef struct menu_page_s {
    lv_obj_t  *container;        /* 页面容器（可直接往里加自定义控件） */
    lv_obj_t  *title_label;      /* 标题标签 */
    int        btn_count;        /* 按钮数量（含 Back，Back 固定 index=0） */
    lv_obj_t **btn_containers;  /* 按钮容器数组 */
    lv_obj_t **btn_labels;      /* 按钮标签数组 */
    lv_obj_t  *scroller;        /* 滚动层（框架内部用） */
    lv_obj_t  *focus_cursor;    /* 焦点游标（可替换样式） */
    int        focus_index;      /* 当前焦点按钮 */
    const char *title;           /* 页面标题 */
    void     (*on_btn)(int index); /* 按钮回调（Core 0 上下文，可安全操作 LVGL）。
                                      index=0 为 Back，框架已处理返回上一级 */
} menu_page_t;

typedef menu_page_t* (*menu_page_creator_t)(void);

/* ================================================================
 *  生命周期（main.c 调用）
 * ================================================================ */

void      menu_ui_init(void);
void      menu_ui_deinit(void);
lv_obj_t* menu_ui_get_screen(void);

/* ================================================================
 *  轮播
 * ================================================================ */

void menu_ui_update_carousel(int selected, int dir);

/* ================================================================
 *  子页面栈
 * ================================================================ */

/** 创建一个按钮列表页面。buttons[0] 必须是 "Back"。on_btn 在 Core 0 执行 */
menu_page_t* menu_ui_page_create(const char *title,
                                  const char *buttons[], int count,
                                  void (*on_btn)(int index));

/** lvgl_task 检测到 MENU_LEVEL_SUB 时调用 */
void menu_ui_enter_sub(void);

/** 推入页面到栈顶并播放滑入动画（仅在 Core 0 调用） */
void menu_ui_push_page(menu_page_t *page);

/** 弹出栈顶页面并播放滑出动画（Back 自动调，也可手动调用） */
void menu_ui_go_back(void);

/** 注册菜单项 → 页面创建器。短按菜单项时框架自动调用创建器→push */
void menu_ui_register_creator(menu_item_t item, menu_page_creator_t creator);

/* ================================================================
 *  内部 tick（lvgl_task Core 0 每帧调用）
 * ================================================================ */

void menu_ui_tick_anim(void);
bool menu_ui_is_animating(void);
void menu_ui_apply_sub_updates(void);

/* ================================================================
 *  测试入口
 * ================================================================ */

/** 注册默认可嵌套测试页面 */
void menu_ui_test_nested(void);

/* ================================================================
 *  聚合初始化（供 main.c 调用，避免每个模块单独 init）
 * ================================================================ */

/** 初始化所有菜单子模块（pomodoro/weather/clock 等） */
void menu_ui_init_modules(void);

/** logic_task 每秒调用（纯数据 tick，不操作 LVGL） */
void menu_ui_tick_modules(uint32_t dt_ms);

/** lvgl_task 每帧调用（统一处理子模块 LVGL 更新 + 待处理的 tick） */
void menu_ui_process_module_updates(void);

/** 恢复默认子页面输入回调（调整模式结束后用） */
void menu_ui_restore_default_input(void);

/** ease-out 缓动函数（0→256），供各模块动画使用 */
int32_t menu_ui_ease_out(int32_t t_ms, int32_t duration_ms);

#ifdef __cplusplus
}
#endif

#endif
