/**
 * @file    mode_manager.h
 * @brief   模式管理器 —— 模式切换状态机（PET ↔ MENU）+ 输入路由
 *
 * 长按编码器按键 → 切换模式
 * 短按 / 旋转 / 倾斜 / 摇晃 / tick → 路由到当前模式处理器
 */

#ifndef MODE_MANAGER_H
#define MODE_MANAGER_H

#include "app_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/** 初始化模式管理器，默认进入 PET 模式 */
void mode_manager_init(void);

/** 编码器旋转事件 @param step >0 顺时针，<0 逆时针 */
void mode_manager_on_rotary(int8_t step);

/** 编码器按键事件 @param short_press true=短按 false=长按（≥800ms） */
void mode_manager_on_btn(bool short_press);

/** IMU 倾斜事件（角度 + 方向） */
void mode_manager_on_tilt(imu_angles_t angles, imu_tilt_dir_t dir);

/** IMU 摇晃事件 @param dir 摇晃方向（左/右） */
void mode_manager_on_shake(imu_shake_dir_t dir);

/** 每帧逻辑 tick @param dt_ms 距上次调用的毫秒数 */
void mode_manager_tick(uint32_t dt_ms);

/** 获取当前模式（供测试/调试使用） */
app_mode_t mode_manager_get_mode(void);

#ifdef __cplusplus
}
#endif

#endif /* MODE_MANAGER_H */
