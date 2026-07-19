/**
 * @file    pomodoro.h
 * @brief   番茄钟：倒计时逻辑 + LVGL 环形进度条 UI
 *
 * 作为 menu_ui 子页面运行。注册到 MENU_ITEM_POMODORO。
 * 提示音暂用 printf 占位，待 hal_audio 就绪后替换。
 */

#ifndef POMODORO_H
#define POMODORO_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 注册到 menu_ui + 初始化默认状态。由 menu_ui_init_modules() 调用 */
void pomodoro_init(void);

/** logic_task 每秒调用（纯数据操作，不碰 LVGL） */
void pomodoro_tick(uint32_t dt_ms);

/** lvgl_task 每帧调用（统一执行所有 LVGL 操作 + 处理待处理的 tick） */
void pomodoro_process_updates(void);

#ifdef __cplusplus
}
#endif

#endif
