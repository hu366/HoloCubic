/**
 * @file    hal_audio.h
 * @brief   MP3-TF-16P (YX5200) UART 驱动封装
 *
 * 指令帧格式（带校验）：
 *   7E FF 06 CMD FB DH DL CHK_H CHK_L EF
 * 也可以不带校验（省略 CHK_H CHK_L）：
 *   7E FF 06 CMD FB DH DL EF
 *
 * 关键时序：
 *   - 上电后等 1.5s 初始化
 *   - 选设备后等 200ms 再发播放指令
 */

#ifndef HAL_AUDIO_H
#define HAL_AUDIO_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 初始化 UART，上电后调用（内部会等待模块初始化完成） */
void hal_audio_init(void);

/** 设置音量 0~30 */
void hal_audio_set_volume(uint8_t vol);

/** 查询音量（阻塞等待返回），返回 0~30，失败返回 0xFF */
uint8_t hal_audio_get_volume(void);

/** 指定设备 = TF 卡（上电后只需调用一次） */
void hal_audio_select_tf(void);

/** 播放根目录按物理序号（1~2999） */
void hal_audio_play_track(uint16_t num);

/** 播放指定文件夹曲目：folder 1~99, track 1~255 */
void hal_audio_play_folder_track(uint8_t folder, uint8_t track);

/** 播放/暂停/停止/下一曲/上一曲 */
void hal_audio_play(void);
void hal_audio_pause(void);
void hal_audio_stop(void);
void hal_audio_next(void);
void hal_audio_prev(void);

/** 全部循环（1=开 0=关） */
void hal_audio_loop_all(bool on);

/** 查询 TF 卡当前曲目（阻塞等待），返回 1~N，失败返回 0 */
uint16_t hal_audio_get_current_track(void);

/** 查询 TF 卡总文件数（阻塞等待），返回 0~N，失败返回 0 */
uint16_t hal_audio_get_total_tracks(void);

#ifdef __cplusplus
}
#endif

#endif
