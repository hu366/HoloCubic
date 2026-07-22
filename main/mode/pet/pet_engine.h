/**
 * @file    pet_engine.h
 * @brief   宠物引擎 —— 7 状态状态机、需求衰减、喂食/玩耍、离线衰减
 */

#ifndef PET_ENGINE_H
#define PET_ENGINE_H

#include "app_types.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化宠物数据为默认值
 *        hunger=100, happiness=100, state=PET_IDLE, last_tick 设为当前时间
 */
void pet_engine_init(pet_data_t *pet);

/**
 * @brief 将结构体的 hunger/happiness 同步到内部累加器。
 *        在外部直接修改 pet->hunger / pet->happiness 后必须调用。
 */
void pet_engine_sync(pet_data_t *pet);

/**
 * @brief 每帧逻辑更新（由 logic_task 以 ~1Hz 驱动，也可传递非 1s 的 dt_ms）
 *        - 按 dt_ms 衰减 hunger / happiness
 *        - 检查阈值，触发 PET_HUNGRY / PET_SAD
 *        - 冷却计时递减（shake_cooldown）
 *        - idle_timer / walk_timer 递增
 *
 * @param pet   宠物数据指针
 * @param dt_ms 距离上次 tick 的毫秒数
 */
void pet_engine_tick(pet_data_t *pet, uint32_t dt_ms);

/**
 * @brief 喂食：hunger += PET_FEED_AMOUNT（上限 100），状态切到 PET_EATING
 */
void pet_engine_feed(pet_data_t *pet);

/**
 * @brief 玩耍：happiness += PET_PLAY_AMOUNT（上限 100），状态切到 PET_PLAYING
 */
void pet_engine_play(pet_data_t *pet);

/**
 * @brief 摇晃响应：任意状态 → PET_SHOCKED，启动冷却计时
 */
void pet_engine_on_shake(pet_data_t *pet);

/**
 * @brief 离线衰减：根据 last_tick 和 current_time 的差值
 *        对 hunger/happiness 施加离线衰减，并更新 last_tick。
 *        在从 NVS 恢复宠物数据后调用。
 *
 * @param pet          宠物数据指针
 * @param current_time 当前 Unix 时间戳（秒）
 */
void pet_engine_apply_offline_decay(pet_data_t *pet, uint64_t current_time);

#ifdef __cplusplus
}
#endif

#endif /* PET_ENGINE_H */
