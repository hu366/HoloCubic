/**
 * @file    hal_display.h
 * @brief   显示驱动抽象层 —— SPI 驱动 ST7789，对接 LVGL v9.5
 */

#ifndef HAL_DISPLAY_H
#define HAL_DISPLAY_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化显示硬件
 *
 * - 初始化 SPI 主机（SPI2_HOST）
 * - 配置 ST7789 面板（240×240 RGB565）
 * - 在 PSRAM 中分配双帧缓冲（230KB）
 * - 注册 LVGL display driver
 * - 开启背光
 */
void hal_display_init(void);

/**
 * @brief LVGL flush 回调 —— 将脏区域像素推送到屏幕
 *
 * @param disp   LVGL display 对象
 * @param area   脏区域（屏幕坐标）
 * @param px_map 像素数据（RGB565 格式）
 */
void hal_display_flush(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map);

/**
 * @brief 运行时切换屏幕镜像
 *
 * @param mirrored  true=镜像开启, false=镜像关闭
 */
void hal_display_set_mirror(bool mirrored);

/**
 * @brief 查询当前镜像状态
 */
bool hal_display_is_mirrored(void);

#ifdef __cplusplus
}
#endif

#endif /* HAL_DISPLAY_H */
