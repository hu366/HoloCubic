/**
 * @file    pet_shake.h
 * @brief   Task 46: 摇晃响应 —— 受惊动画 + 冷却
 */

#ifndef PET_SHAKE_H
#define PET_SHAKE_H

#include "app_types.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 摇晃冷却时间（毫秒） */
#define PET_SHAKE_COOLDOWN_MS  2000

/**
 * @brief 摇晃触发：进入 SHOCKED 状态 + 播受惊动画
 *
 * - 若 pet->anim_busy 或冷却未结束 → 忽略
 * - 否则：state→SHOCKED, anim_busy=true, 启动冷却
 *
 * @return true 触发成功, false 被冷却 / 动画中忽略
 */
bool pet_shake_trigger(pet_data_t *pet);

/**
 * @brief 每帧更新冷却计时（logic_task 调用）
 *
 * @param pet   宠物数据
 * @param dt_ms 距上次 tick 的毫秒数
 */
void pet_shake_tick(pet_data_t *pet, uint32_t dt_ms);

#ifdef __cplusplus
}
#endif

#endif /* PET_SHAKE_H */
