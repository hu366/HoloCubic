/**
 * @file    animation_board.h
 * @brief   动画看板 —— SD 卡 RGB565 帧序列播放器
 *
 * 用法:
 *   1. PC 端: python scripts/video_to_frames.py video.mp4 -o frames/
 *   2. 把 frames/ 目录放入 SD 卡: /sdcard/animations/<name>/
 *   3. ESP32: anim_board_load("name") → anim_board_tick() 驱动播放
 *
 * SD 卡目录布局:
 *   /sdcard/animations/<name>/
 *     meta.txt    → "240 240 300 10" (宽 高 帧数 fps)
 *     000.bin ... NNN.bin  → RGB565 原始帧 (大端序)
 */

#ifndef ANIMATION_BOARD_H
#define ANIMATION_BOARD_H

#include <stdint.h>
#include <stdbool.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/** 视频元信息 */
typedef struct {
    char     name[32];       /* 视频名称（目录名） */
    uint16_t width;          /* 帧宽 */
    uint16_t height;         /* 帧高 */
    uint16_t frame_count;    /* 总帧数 */
    uint8_t  fps;            /* 目标帧率 */
} anim_board_info_t;

/** 释放所有缓冲 */
void anim_board_unload(void);

/**
 * @brief 每帧更新（lvgl_task 调用）
 * @param dt_ms  距上次调用的毫秒数
 * @return true  帧已切换，需刷新显示
 */
bool anim_board_tick(uint32_t dt_ms);

/** 获取当前帧 LVGL 图像描述符 */
const lv_image_dsc_t *anim_board_get_frame(void);

/** 脏标（调用后自动清除） */
bool anim_board_is_dirty(void);

/** 是否正在播放 */
bool anim_board_is_playing(void);

/** 加载进度回调（loaded=已加载帧数, total=总帧数） */
typedef void (*anim_load_progress_cb_t)(uint16_t loaded, uint16_t total);

/**
 * @brief 加载视频（全部帧预载入 PSRAM）
 * @param name  SD 卡 /sdcard/animations/<name>/ 下的视频
 * @param info  输出元信息（可为 NULL）
 * @param cb    加载进度回调，每 3 帧调用一次（可为 NULL）
 */
bool anim_board_load(const char *name, anim_board_info_t *info,
                     anim_load_progress_cb_t cb);

/* ================================================================
 *  菜单集成
 * ================================================================ */

/** 注册到菜单系统（由 menu_ui_init_modules 调用） */
void animation_board_init(void);

/** lvgl_task 每帧调用（推进帧 + 刷新 LVGL） */
void animation_board_process_updates(void);

#ifdef __cplusplus
}
#endif

#endif /* ANIMATION_BOARD_H */
