/**
 * @file    menu_engine.h
 * @brief   菜单导航引擎 —— TOP/SUB 二级状态机 + 输入路由
 *
 *   TOP  主菜单 5 项横向轮播（左/右倾斜切换，短按进入子页面）
 *   SUB  子页面内（输入转发给回调，menu_ui 管理页面栈）
 */

#ifndef MENU_ENGINE_H
#define MENU_ENGINE_H

#include <stdint.h>
#include <stdbool.h>
#include "app_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- 类型 ---- */

typedef enum {
    MENU_ITEM_POMODORO = 0,
    MENU_ITEM_WEATHER,
    MENU_ITEM_CLOCK,
    MENU_ITEM_MUSIC,
    MENU_ITEM_ANIMATION,
    MENU_ITEM_COUNT
} menu_item_t;

typedef enum {
    MENU_LEVEL_TOP = 0,
    MENU_LEVEL_SUB,
} menu_level_t;

typedef enum {
    MENU_DIR_LEFT  = -1,
    MENU_DIR_NONE  =  0,
    MENU_DIR_RIGHT =  1,
} menu_scroll_dir_t;

/** 子页面输入回调（在 sensor_task Core 1 上下文调用，严禁操作 LVGL） */
typedef void (*menu_sub_cb_t)(imu_tilt_dir_t tilt_dir, int8_t rotary_step,
                              bool btn_short);

/* ---- 生命周期 ---- */

void menu_engine_init(void);

/* ---- 输入 ---- */

void menu_engine_on_rotary(int8_t step);
void menu_engine_on_btn(bool short_press);
void menu_engine_on_tilt(imu_tilt_dir_t dir);
void menu_engine_tick(uint32_t dt_ms);

/* ---- 查询 ---- */

menu_item_t       menu_engine_get_selected(void);
menu_level_t      menu_engine_get_level(void);
const char*       menu_engine_get_item_name(menu_item_t item);
menu_scroll_dir_t menu_engine_get_scroll_dir(void);
void              menu_engine_clear_scroll_dir(void);

/* ---- 子页面交互 ---- */

void menu_engine_set_sub_callback(menu_sub_cb_t cb);

/** menu_ui 页面栈清空后调用，通知 engine 回到 TOP */
void menu_engine_go_back(void);

/** 强制放行旋钮（番茄钟调节模式用），传 false 恢复 MENU_INPUT_MODE 过滤 */
void menu_engine_rotary_override(bool force);

#ifdef __cplusplus
}
#endif

#endif
