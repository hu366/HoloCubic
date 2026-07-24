/**
 * @file    music_player.h
 * @brief   音乐播放器 —— SD 卡文件浏览器 + MP3-TF-16P 播放控制
 *
 * 从 ESP32 SPI SD 卡读取 music_map.txt 构建文件树，
 * 用户浏览选歌 → 映射为编号发给 MP3-TF-16P 播放。
 *
 * 跨核安全：
 *   Core 1 → music_input_cb → 只设脏标
 *   Core 0 → music_player_process_updates → 全部 LVGL 操作
 */

#ifndef MUSIC_PLAYER_H
#define MUSIC_PLAYER_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void music_player_init(void);
void music_player_tick(uint32_t dt_ms);
void music_player_process_updates(void);

#ifdef __cplusplus
}
#endif

#endif
