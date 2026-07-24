/**
 * @file    mode_manager.c
 * @brief   Task 8: 模式管理器实现 —— 状态机 + 输入路由
 *
 * 所有事件仅打印接收确认，不模拟具体行为（避免误导后续任务）。
 * 长按按键 = 切换 PET ↔ MENU 模式。
 */

#include <stdio.h>
#include "mode_manager.h"
#include "menu/menu_engine.h"
#include "pet/pet_ui.h"
#include "app_config.h"

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

/* ================================================================
 *  公开接口
 * ================================================================ */

void mode_manager_init(void)
{
    s_current_mode = DEFAULT_STARTUP_MODE;
    menu_engine_init();
    printf("[MM] 初始化完成，当前模式: %s\n", mode_str(s_current_mode));
}

void mode_manager_on_rotary(int8_t step)
{
    if (s_current_mode == MODE_MENU) {
        menu_engine_on_rotary(step);
    } else {
        printf("[MM] 收到: %s / 旋钮 %+d 步\n",
               mode_str(s_current_mode), step);
    }
}

void mode_manager_on_btn(bool short_press)
{
    if (!short_press) {
        /* 长按：子页面内 → 返回；主页面 → 切换模式 */
        if (s_current_mode == MODE_MENU
            && menu_engine_get_level() == MENU_LEVEL_SUB) {
            menu_engine_go_back();
            printf("[MM] 收到: 按键-长按 → 子页面返回\n");
        } else {
            s_current_mode = (s_current_mode == MODE_PET) ? MODE_MENU : MODE_PET;
            s_last_tilt_dir = (imu_tilt_dir_t)-1;
            if (s_current_mode == MODE_MENU) {
                menu_engine_init();
            }
            printf("[MM] 收到: 按键-长按 → 切换模式 → %s\n",
                   mode_str(s_current_mode));
        }
    } else if (s_current_mode == MODE_MENU) {
        menu_engine_on_btn(true);
    } else {
        printf("[MM] 收到: %s / 按键-短按\n", mode_str(s_current_mode));
    }
}

void mode_manager_on_tilt(imu_angles_t angles, imu_tilt_dir_t dir)
{
    if (s_current_mode == MODE_MENU) {
        if (dir == s_last_tilt_dir) return;
        s_last_tilt_dir = dir;
        menu_engine_on_tilt(dir);
    } else {
        /* PET 模式：每帧传连续角度，不跳 */
        pet_ui_on_tilt(angles.pitch, angles.roll);
    }
}

void mode_manager_on_shake(imu_shake_dir_t dir)
{
    /* MENU 模式不需要摇晃检测 */
    if (s_current_mode == MODE_MENU) return;

    pet_ui_on_shake();
}

void mode_manager_tick(uint32_t dt_ms)
{
    /* 每 5 秒打印一次（PET 模式） */
    s_tick_counter++;
    if (s_tick_counter >= 5) {
        s_tick_counter = 0;
        if (s_current_mode != MODE_MENU) {
            printf("[MM] 收到: %s / 心跳 %lums\n",
                   mode_str(s_current_mode), (unsigned long)(dt_ms * 5));
        }
    }
    if (s_current_mode == MODE_MENU) {
        menu_engine_tick(dt_ms);
    }
}

app_mode_t mode_manager_get_mode(void)
{
    return s_current_mode;
}
