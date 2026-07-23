/**
 * @file    pet_motion.c
 * @brief   Task 44: 倾斜→速度映射 + 平滑位置更新 + 归位缓动
 */

#include "pet_motion.h"
#include <math.h>

/* ================================================================
 *  内部函数
 * ================================================================ */

/** 三段式倾斜→速度映射：死区→线性→饱和 */
static float tilt_to_velocity(float angle_deg)
{
    float abs_a = fabsf(angle_deg);

    /* 死区 */
    if (abs_a <= PET_MOTION_DEADZONE) return 0.0f;

    /* 饱和 */
    if (abs_a >= PET_MOTION_SATURATION)
        return (angle_deg > 0) ? PET_MOTION_MAX_SPEED : -PET_MOTION_MAX_SPEED;

    /* 线性映射 */
    float ratio = (abs_a - PET_MOTION_DEADZONE)
                / (PET_MOTION_SATURATION - PET_MOTION_DEADZONE);
    float speed = ratio * PET_MOTION_MAX_SPEED;
    return (angle_deg > 0) ? speed : -speed;
}

/** 裁剪到屏幕范围（上/左边界放宽以补偿镜像偏移） */
static int16_t clamp_pos(float val, int16_t half)
{
    int min = -64;
    int max = 240 + 64;
    if (val < min) return (int16_t)min;
    if (val > max) return (int16_t)max;
    return (int16_t)val;
}

/** 向目标缓动一步 */
static int16_t ease_toward(int16_t cur, int16_t target,
                           float speed_pps, uint32_t dt_ms)
{
    float max_step = speed_pps * dt_ms / 1000.0f;
    float diff = (float)(target - cur);

    if (fabsf(diff) <= max_step) return target;

    return cur + (int16_t)((diff > 0 ? 1 : -1) * max_step);
}

/* ================================================================
 *  API 实现
 * ================================================================ */

void pet_motion_init(pet_data_t *pet)
{
    if (!pet) return;

    /* 屏幕中央 */
    int16_t center_x = PET_MOTION_SCREEN_W / 2;
    int16_t center_y = PET_MOTION_SCREEN_H / 2;

    pet->position.x     = center_x;
    pet->position.y     = center_y;
    pet->position.home_x = center_x;
    pet->position.home_y = center_y;
    pet->idle_timer     = 0;
}

void pet_motion_tick(pet_data_t *pet, float pitch_deg, float roll_deg, uint32_t dt_ms)
{
    if (!pet) return;

    /* anim_busy 时暂停移动（位置保持不变） */
    if (pet->anim_busy) {
        pet->idle_timer = 0;
        return;
    }

    float vx = tilt_to_velocity(roll_deg);   /* 横滚→左右 */
    float vy = tilt_to_velocity(pitch_deg);  /* 俯仰→上下：前倾(+)=宠物上移 */

    /* 精灵半宽（后续 pet_ui 设置 sprite 大小时用 PET_SPRITE_SIZE/2） */
    const int16_t half = 64;  /* 128px精灵 / 2，完整留在 240×240 屏内 */

    if (fabsf(vx) < 0.01f && fabsf(vy) < 0.01f) {
        /* 死区：累加空闲时间 */
        pet->idle_timer += dt_ms;

        /* 超过 3 秒 → 缓动归位 */
        if (pet->idle_timer >= PET_MOTION_IDLE_SEC * 1000) {
            int16_t nx = ease_toward(pet->position.x,
                                     pet->position.home_x,
                                     PET_MOTION_HOME_SPEED, dt_ms);
            int16_t ny = ease_toward(pet->position.y,
                                     pet->position.home_y,
                                     PET_MOTION_HOME_SPEED, dt_ms);
            pet->position.x = nx;
            pet->position.y = ny;
        }
    } else {
        /* 有倾斜：重置空闲，更新位置 */
        pet->idle_timer = 0;

        float dx = vx * dt_ms / 1000.0f;
        float dy = vy * dt_ms / 1000.0f;

        float new_x = (float)pet->position.x + dx;
        float new_y = (float)pet->position.y + dy;

        pet->position.x = clamp_pos(new_x, half);
        pet->position.y = clamp_pos(new_y, half);
    }
}
