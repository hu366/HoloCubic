/**
 * @file    mode_manager.c
 * @brief   Task 8: 模式管理器实现 —— 状态机 + 输入路由
 *
 * 所有事件仅打印接收确认，不模拟具体行为（避免误导后续任务）。
 * 长按按键 = 切换 PET ↔ MENU 模式。
 */

#include <stdio.h>
#include "mode_manager.h"

/* ----------------------------------------------------------------
 *  内部状态
 * ---------------------------------------------------------------- */
static app_mode_t     s_current_mode  = MODE_PET;
static imu_tilt_dir_t s_last_tilt_dir = (imu_tilt_dir_t)-1; /* 哨兵，确保首次触发 */
static uint8_t        s_tick_counter  = 0;                  /* tick 降频 */

/* ----------------------------------------------------------------
 *  辅助
 * ---------------------------------------------------------------- */
static const char *mode_str(app_mode_t m)
{
    return (m == MODE_PET) ? "PET" : "MENU";
}

static const char *tilt_str(imu_tilt_dir_t d)
{
    switch (d) {
    case IMU_TILT_FRONT: return "前倾";
    case IMU_TILT_BACK:  return "后倾";
    case IMU_TILT_LEFT:  return "左倾";
    case IMU_TILT_RIGHT: return "右倾";
    default:             return "水平";
    }
}

static const char *shake_str(imu_shake_dir_t d)
{
    return (d == IMU_SHAKE_LEFT) ? "左甩" : "右甩";
}

/* ================================================================
 *  公开接口
 * ================================================================ */

void mode_manager_init(void)
{
    s_current_mode = MODE_PET;
    printf("[MM] 初始化完成，当前模式: PET\n");
}

void mode_manager_on_rotary(int8_t step)
{
    printf("[MM] 收到: %s / 旋钮 %+d 步\n",
           mode_str(s_current_mode), step);
}

void mode_manager_on_btn(bool short_press)
{
    if (!short_press) {
        /* 长按 → 切换模式 */
        s_current_mode = (s_current_mode == MODE_PET) ? MODE_MENU : MODE_PET;
        s_last_tilt_dir = (imu_tilt_dir_t)-1;
        printf("[MM] 收到: 按键-长按 → 切换模式 → %s\n",
               mode_str(s_current_mode));
    } else {
        printf("[MM] 收到: %s / 按键-短按\n", mode_str(s_current_mode));
    }
}

void mode_manager_on_tilt(imu_angles_t angles, imu_tilt_dir_t dir)
{
    /* 方向没变就跳过，避免 50Hz 刷屏 */
    if (dir == s_last_tilt_dir) return;
    s_last_tilt_dir = dir;

    printf("[MM] 收到: %s / 倾斜-%s, pitch=%+.1f° roll=%+.1f°\n",
           mode_str(s_current_mode), tilt_str(dir),
           angles.pitch, angles.roll);
}

void mode_manager_on_shake(imu_shake_dir_t dir)
{
    printf("[MM] 收到: %s / 摇晃-%s\n",
           mode_str(s_current_mode), shake_str(dir));
}

void mode_manager_tick(uint32_t dt_ms)
{
    /* 每 5 秒打印一次 */
    s_tick_counter++;
    if (s_tick_counter < 5) return;
    s_tick_counter = 0;

    printf("[MM] 收到: %s / 心跳 %lums\n",
           mode_str(s_current_mode), (unsigned long)(dt_ms * 5));
}

app_mode_t mode_manager_get_mode(void)
{
    return s_current_mode;
}
