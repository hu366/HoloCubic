/**
 * @file    hal_sd.h
 * @brief   Task 32: SD 卡 HAL 驱动 —— 公开接口
 *
 * 硬件：SPI3 接口 SD 卡，FatFS 文件系统
 * 挂载点：/sdcard（由 app_config.h 的 SD_MOUNT_POINT 定义）
 *
 * 典型使用流程：
 *   hal_sd_mount()           → 挂载 SD 卡
 *   hal_sd_list_dir(...)     → 列出目录内容
 *   hal_sd_free_dir_list(...) → 释放目录列表内存
 *   hal_sd_unmount()         → 卸载 SD 卡
 */

#ifndef HAL_SD_H
#define HAL_SD_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 挂载 SD 卡（SPI 模式 + FatFS）
 *
 * 初始化 SPI3 总线（SD_PIN_SCLK/MOSI/MISO/CS），
 * 检测并初始化 SD 卡，挂载 FatFS 到 SD_MOUNT_POINT。
 *
 * @return true  挂载成功
 * @return false 挂载失败（无卡 / 卡损坏 / SPI 通信失败 / FatFS 异常）
 *
 * @note 幂等性：若已挂载则直接返回 true，不重复初始化。
 */
bool hal_sd_mount(void);

/**
 * @brief 卸载 SD 卡并释放 SPI 总线
 *
 * 卸载 FatFS 文件系统，释放 sdmmc_card 句柄，释放 SPI3 总线。
 * 若未挂载则无操作。
 *
 * @note 幂等性：可安全重复调用。
 */
void hal_sd_unmount(void);

/**
 * @brief 列出目录下的所有条目名称
 *
 * 遍历指定路径，返回所有条目名称（包括文件和子目录）。
 * 排序：目录在前、文件在后，各自按名称升序。
 * 跳过 "." 和 ".."。
 *
 * @param[in]  path  绝对路径（如 "/sdcard" 或 "/sdcard/music"），
 *                   NULL 时返回 false。
 * @param[out] names 条目名称数组，调用者通过 hal_sd_free_dir_list() 释放。
 *                   失败时置为 NULL。
 * @param[out] count 条目数量，失败时置为 0。
 *
 * @return true  成功
 * @return false 失败（未挂载 / 路径不存在 / 无法打开目录）
 */
bool hal_sd_list_dir(const char *path, char ***names, size_t *count);

/**
 * @brief 释放 hal_sd_list_dir() 返回的目录列表内存
 *
 * @param names  条目名称数组（hal_sd_list_dir 返回）
 * @param count  条目数量
 *
 * @note NULL 安全：names == NULL 时无操作。
 */
void hal_sd_free_dir_list(char **names, size_t count);

#ifdef __cplusplus
}
#endif

#endif /* HAL_SD_H */
