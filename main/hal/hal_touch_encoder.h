/**
 * @file    hal_touch_encoder.h
 * @brief   旋转编码器驱动抽象层 —— PCNT 硬件脉冲计数 + 按键去抖
 */

#ifndef HAL_TOUCH_ENCODER_H
#define HAL_TOUCH_ENCODER_H

#include "app_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化旋转编码器
 *
 * - 初始化 PCNT 硬件单元，A/B 相正交解码
 * - 配置按键 GPIO（上拉输入）
 * - 启动 5ms 周期定时器：轮询 PCNT 计数值 + 按键去抖状态机
 *
 * @param rot_cb 旋转回调（step >0 顺时针，<0 逆时针），可为 NULL
 * @param btn_cb 按键回调（short_press: true=短按 <800ms, false=长按 ≥800ms），可为 NULL
 *
 * @note  重复调用仅更新回调指针，不会重复初始化硬件。
 */
void hal_touch_encoder_init(encoder_callback_t rot_cb, encoder_btn_callback_t btn_cb);

#ifdef __cplusplus
}
#endif

#endif /* HAL_TOUCH_ENCODER_H */
