/**
 * @file    weather.h
 * @brief   Task 18: 天气模块 —— HTTP API 请求 + cJSON 解析 + LVGL 展示
 *
 * 作为 menu_ui 子页面运行。注册到 MENU_ITEM_WEATHER。
 * 依赖 svc_wifi（判断连接状态）和 esp_http_client（发起请求）。
 */

#ifndef WEATHER_H
#define WEATHER_H

#include <stdint.h>
#include <stdbool.h>
#include "app_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/** 注册到 menu_ui + 初始化默认状态。由 menu_ui_init_modules() 调用 */
void weather_init(void);

/** logic_task 每秒调用（纯数据操作，不碰 LVGL） */
void weather_tick(uint32_t dt_ms);

/** lvgl_task 每帧调用（统一执行所有 LVGL 操作） */
void weather_process_updates(void);

/** 解析心知天气 API JSON 响应，填充 weather_data_t。
 *  @return true=成功  false=解析失败（缺字段/格式错误） */
bool weather_parse_json(const char *json, weather_data_t *out);

/** 检查数据是否过期（从未获取 / 距上次更新超过 WEATHER_STALE_MS） */
bool weather_is_stale(const weather_data_t *data);

#ifdef __cplusplus
}
#endif

#endif
