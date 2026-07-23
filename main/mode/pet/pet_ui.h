/**
 * @file    pet_ui.h
 * @brief   宠物模式 LVGL 页面 —— 串起 pet_anim + pet_motion + pet_shake
 */

#ifndef PET_UI_H
#define PET_UI_H

#include "lvgl.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 创建宠物屏幕、精灵图像、初始化所有模块 */
void pet_ui_init(void);

/** 释放资源 */
void pet_ui_deinit(void);

/**
 * @brief 每帧处理（lvgl_task 调用，30ms 周期，Core 0）
 *
 * - 读 volatile IMU 角度
 * - pet_anim_tick → 切换帧
 * - pet_motion_tick → 更新位置
 * - 检查摇晃标志 → pet_shake_trigger
 * - 刷新 LVGL 图像对象
 */
void pet_ui_process(uint32_t dt_ms);

/**
 * @brief 设置 IMU 角度（sensor_task 回调，Core 1，可跨核调用）
 *
 * 只设 volatile 标志，不操作 LVGL。
 */
void pet_ui_on_tilt(float pitch, float roll);

/**
 * @brief 摇晃触发（sensor_task 回调，Core 1，可跨核调用）
 */
void pet_ui_on_shake(void);

/** 获取宠物 LVGL 屏幕对象 */
lv_obj_t *pet_ui_get_screen(void);

#ifdef __cplusplus
}
#endif

#endif /* PET_UI_H */
