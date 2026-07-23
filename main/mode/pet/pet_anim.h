/**
 * @file    pet_anim.h
 * @brief   Task 42: 宠物动画帧管理 —— 角度映射 + 时序播放 + 双缓冲
 *
 * 两种帧选择模式：
 *  - 角度映射（IDLE/HUNGRY/SAD）：IMU pitch/roll → 预渲染视角帧 → 直接显示
 *  - 时序播放（EATING/PLAYING/SHOCKED/WALKING）：按时间轮播序列帧
 *
 * SD 卡目录布局：
 *   /sdcard/pet_frames/<state>/
 *     meta.txt    → 宽 高 行数 列数   (角度映射)
 *                 → 宽 高 帧数        (时序播放)
 *     000.bin ... NNN.bin  → ARGB8565 原始帧数据
 */

#ifndef PET_ANIM_H
#define PET_ANIM_H

#include "app_types.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/** 精灵帧尺寸（像素，正方形） */
#define PET_SPRITE_SIZE     128

/** 时序动画每帧间隔（毫秒），即 10 fps */
#define PET_ANIM_FRAME_MS   100

/** IDLE 角度范围（度），俯仰/横滚各 ±45° */
#define PET_ANGLE_MAX_DEG   45.0f

/** 角度映射网格档数（10°步长 × 90°范围 = 10 档） */
#define PET_ANGLE_GRID      10

/** 动画事件 */
typedef enum {
    PET_ANIM_NONE   = 0,   /* 无事件 / 角度模式帧切换 */
    PET_ANIM_PLAYING,      /* 时序动画播放中 */
    PET_ANIM_DONE,         /* 时序动画播放完毕 */
} pet_anim_event_t;

/* ================================================================
 *  API
 * ================================================================ */

/** 初始化动画模块（不加载任何帧） */
void pet_anim_init(void);

/**
 * @brief 为 pet->state 加载动画帧
 *
 * - 角度映射状态：从 SD 卡读全部帧到 PSRAM 缓存
 * - 时序播放状态：读 meta.txt 获取帧数，逐帧流式读取
 *
 * @return true 成功, false 失败（无 SD / 缺文件 / 内存不足）
 */
bool pet_anim_load(const pet_data_t *pet);

/** 释放所有缓存帧数据 */
void pet_anim_unload(void);

/**
 * @brief 每帧更新（lvgl_task 以 30ms 间隔调用）
 *
 * - 角度映射模式：根据 pitch_deg / roll_deg 选择最邻近帧
 * - 时序模式：按 PET_ANIM_FRAME_MS 推进帧索引
 * - 时序动画播完后自动：pet->anim_busy = false, pet->state = PET_IDLE
 *
 * @param pet       宠物数据（读 state，写 anim_busy/state）
 * @param pitch_deg IMU 俯仰角（度）
 * @param roll_deg  IMU 横滚角（度）
 * @param dt_ms     距上次 tick 的毫秒数
 * @return 动画事件
 */
pet_anim_event_t pet_anim_tick(pet_data_t *pet,
                               float pitch_deg, float roll_deg, uint32_t dt_ms);

/**
 * @brief 获取当前帧的 LVGL 图像描述符
 *
 * 返回的指针在下次 pet_anim_tick() 前有效，
 * 可直接传给 lv_image_set_src()。
 *
 * @return 图像描述符指针；未加载时返回 NULL
 */
const lv_image_dsc_t *pet_anim_get_frame(void);

/**
 * @brief 帧是否已切换（用于 pet_ui 判断是否需要刷新 LVGL 图像对象）
 *
 * 每次调用后自动清除脏标。
 *
 * @return true  自上次检查以来帧已切换
 * @return false 帧未变化
 */
bool pet_anim_is_dirty(void);

#ifdef __cplusplus
}
#endif

#endif /* PET_ANIM_H */
