/**
 * @file    pet_engine.c
 * @brief   Task 40 — 7 状态宠物引擎（状态机 + 需求衰减 + 喂食/玩耍 + 离线衰减）
 *
 * 衰减策略：步进计数。hunger 每 10s 减 1，happiness 每 20s 减 1。
 * 毫秒累加器跟踪部分步进，保证长时间运行无累积误差。
 *
 * 状态：IDLE / HUNGRY / SAD / SHOCKED / EATING / PLAYING / WALKING
 * 边界：hunger/happiness ∈ [0, 100]
 * 喂食：hunger += 25, state → EATING
 * 玩耍：happiness += 25, state → PLAYING
 * 摇晃：任意状态 → SHOCKED, 2s 冷却
 */

#include "pet_engine.h"
#include "app_config.h"
#include <sys/time.h>
#include <stdlib.h>

/* 步进门槛（毫秒） */
#define HUNGER_STEP_MS    10000   /*  10s → 1 hunger */
#define HAPPINESS_STEP_MS 20000   /*  20s → 1 happiness */

/* 毫秒累加器 */
static uint32_t s_hunger_decay_ms    = 0;
static uint32_t s_happiness_decay_ms = 0;

static pet_state_t evaluate_state(const pet_data_t *pet)
{
    if (pet->hunger < PET_HUNGER_THRESHOLD) {
        return PET_HUNGRY;
    }
    if (pet->happiness < PET_HAPPINESS_THRESHOLD) {
        return PET_SAD;
    }
    return PET_IDLE;
}

/* ================================================================
 *  API
 * ================================================================ */

void pet_engine_init(pet_data_t *pet)
{
    s_hunger_decay_ms    = 0;
    s_happiness_decay_ms = 0;

    pet->hunger         = 100;
    pet->happiness      = 100;
    pet->state          = PET_IDLE;
    pet->anim_busy      = false;
    pet->shake_cooldown = 0;
    pet->idle_timer     = 0;
    pet->walk_timer     = 0;

    struct timeval tv;
    gettimeofday(&tv, NULL);
    pet->last_tick = (uint64_t)tv.tv_sec;
}

void pet_engine_sync(pet_data_t *pet)
{
    /* 手动改结构体值时重置累加器，从当前值开始计数 */
    (void)pet;
    s_hunger_decay_ms    = 0;
    s_happiness_decay_ms = 0;
}

void pet_engine_tick(pet_data_t *pet, uint32_t dt_ms)
{
    /* ---- 1. 步进衰减 ---- */
    s_hunger_decay_ms += dt_ms;
    while (s_hunger_decay_ms >= HUNGER_STEP_MS) {
        s_hunger_decay_ms -= HUNGER_STEP_MS;
        if (pet->hunger > 0) pet->hunger--;
    }

    s_happiness_decay_ms += dt_ms;
    while (s_happiness_decay_ms >= HAPPINESS_STEP_MS) {
        s_happiness_decay_ms -= HAPPINESS_STEP_MS;
        if (pet->happiness > 0) pet->happiness--;
    }

    /* ---- 2. 摇晃冷却递减 ---- */
    if (pet->shake_cooldown > 0) {
        if (pet->shake_cooldown <= dt_ms) {
            pet->shake_cooldown = 0;
            if (pet->state == PET_SHOCKED) {
                pet->state     = evaluate_state(pet);
                pet->anim_busy = false;
            }
        } else {
            pet->shake_cooldown -= dt_ms;
        }
    }

    /* ---- 3. SHOCKED 期间跳过阈值检测 ---- */
    if (pet->state == PET_SHOCKED) {
        return;
    }

    /* ---- 4. EATING / PLAYING 动画中 ---- */
    if (pet->anim_busy) {
        return;
    }

    /* ---- 5. 阈值检测 → 状态迁移 ---- */
    pet_state_t desired = evaluate_state(pet);
    if (desired != PET_IDLE && pet->state != desired) {
        pet->state = desired;
    }

    pet->idle_timer += dt_ms;
    pet->walk_timer += dt_ms;
}

void pet_engine_feed(pet_data_t *pet)
{
    uint16_t v = (uint16_t)pet->hunger + PET_FEED_AMOUNT;
    pet->hunger = (v > 100) ? 100 : (uint8_t)v;

    pet->state     = PET_EATING;
    pet->anim_busy = true;
}

void pet_engine_play(pet_data_t *pet)
{
    uint16_t v = (uint16_t)pet->happiness + PET_PLAY_AMOUNT;
    pet->happiness = (v > 100) ? 100 : (uint8_t)v;

    pet->state     = PET_PLAYING;
    pet->anim_busy = true;
}

void pet_engine_on_shake(pet_data_t *pet)
{
    pet->state          = PET_SHOCKED;
    pet->shake_cooldown = PET_SHAKE_COOLDOWN_MS;
    pet->anim_busy      = true;
}

void pet_engine_apply_offline_decay(pet_data_t *pet, uint64_t current_time)
{
    if (current_time <= pet->last_tick) {
        pet->last_tick = current_time;
        return;
    }

    uint64_t elapsed_s = current_time - pet->last_tick;

    /* 步进衰减 */
    uint64_t hs = elapsed_s / 10;   /* hunger:  每 10s 减 1 */
    uint64_t ps = elapsed_s / 20;   /* happiness: 每 20s 减 1 */

    pet->hunger    = (hs >= pet->hunger)    ? 0 : (uint8_t)(pet->hunger    - hs);
    pet->happiness = (ps >= pet->happiness) ? 0 : (uint8_t)(pet->happiness - ps);

    /* 离线已处理，重置在线累加器 */
    s_hunger_decay_ms    = 0;
    s_happiness_decay_ms = 0;

    pet->last_tick = current_time;
    pet->state     = evaluate_state(pet);
}
