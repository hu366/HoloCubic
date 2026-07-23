/**
 * @file    pet_shake.c
 * @brief   Task 46: 摇晃响应 —— 受惊动画 + 冷却
 */

#include "pet_shake.h"

bool pet_shake_trigger(pet_data_t *pet)
{
    if (!pet) return false;

    /* 动画中或冷却未结束 → 忽略 */
    if (pet->anim_busy || pet->shake_cooldown > 0) {
        return false;
    }

    pet->state          = PET_SHOCKED;
    pet->anim_busy      = true;
    pet->shake_cooldown = PET_SHAKE_COOLDOWN_MS;

    return true;
}

void pet_shake_tick(pet_data_t *pet, uint32_t dt_ms)
{
    if (!pet) return;

    if (pet->shake_cooldown > 0) {
        if (dt_ms >= pet->shake_cooldown) {
            pet->shake_cooldown = 0;
        } else {
            pet->shake_cooldown -= dt_ms;
        }
    }
}
