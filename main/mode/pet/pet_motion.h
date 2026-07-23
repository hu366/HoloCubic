/**
 * @file    pet_motion.h
 * @brief   Task 44: 倾斜→位置映射 & 平滑移动
 *
 * 倾斜→速度映射（三段式）：
 *  死区 ±5° → v=0
 *  线性 ±5°~±45° → v 线性增长到 MAX_SPEED
 *  饱和 ±45°→ v=±MAX_SPEED
 *
 * 行为：
 *  - 每帧按 dt 累加位移，边界裁剪 [0,240]
 *  - 死区持续 3s 后缓动归位（home_x, home_y）
 *  - anim_busy 期间停止移动
 */

#ifndef PET_MOTION_H
#define PET_MOTION_H

#include "app_types.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 最大移动速度（像素/秒） */
#define PET_MOTION_MAX_SPEED    80

/** 死区角度阈值（度） */
#define PET_MOTION_DEADZONE     5.0f

/** 饱和角度（度） */
#define PET_MOTION_SATURATION   45.0f

/** 空闲多少秒后自动归位 */
#define PET_MOTION_IDLE_SEC     3

/** 归位缓动速度（像素/秒） */
#define PET_MOTION_HOME_SPEED   30

/** 屏幕区域 */
#define PET_MOTION_SCREEN_W     240
#define PET_MOTION_SCREEN_H     240

/**
 * @brief 初始化位置：屏幕中央 = home
 */
void pet_motion_init(pet_data_t *pet);

/**
 * @brief 每帧移动更新
 *
 * @param pet       宠物数据（读写 position / idle_timer）
 * @param pitch_deg IMU 俯仰角
 * @param roll_deg  IMU 横滚角
 * @param dt_ms     距上次 tick 的毫秒数
 */
void pet_motion_tick(pet_data_t *pet, float pitch_deg, float roll_deg, uint32_t dt_ms);

#ifdef __cplusplus
}
#endif

#endif /* PET_MOTION_H */
