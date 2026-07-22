/**
 * @file    clock_alarm.h
 * @brief   Task 24: 时钟 & 闹钟模块 —— 公开接口
 *
 * 功能：
 *   - LVGL 时钟展示（HH:MM:SS + 日期/星期）
 *   - 闹钟增删改（最大 10 个）+ 7 天重复
 *   - 1Hz 触发检查
 *   - 多闹钟排队触发
 *
 * 跨核安全：
 *   Core 1 (sensor_task) → 输入回调 → 只改数据/设脏标
 *   Core 0 (lvgl_task)   → process_updates → 全部 LVGL 操作
 */

#ifndef CLOCK_ALARM_H
#define CLOCK_ALARM_H

#include <stdint.h>
#include <stdbool.h>
#include "app_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- 生命周期 ---- */
void clock_alarm_init(void);
void clock_alarm_tick(uint32_t dt_ms);
void clock_alarm_process_updates(void);

/* ---- 闹钟 CRUD（供持久化 / 测试调用） ---- */
bool clock_alarm_add(uint8_t hour, uint8_t minute, const bool repeat[7]);
bool clock_alarm_edit(uint8_t id, uint8_t hour, uint8_t minute, const bool repeat[7]);
bool clock_alarm_delete(uint8_t id);
bool clock_alarm_set_enabled(uint8_t id, bool enabled);
const alarm_entry_t* clock_alarm_get_entry(uint8_t id);
uint8_t clock_alarm_get_count(void);
const alarm_data_t* clock_alarm_get_all(void);

/* ---- 触发检查 ---- */
int clock_alarm_check_trigger(uint8_t hour, uint8_t minute, uint8_t weekday);
/* 返回触发的 alarm id，-1 表示无触发。
   多个同时触发时，需多次调用来依次获取（每次返回一个并内部标记已触发）。 */

/* ---- 数据导入导出（供持久化） ---- */
void clock_alarm_set_data(const alarm_data_t *data);

#ifdef __cplusplus
}
#endif

#endif /* CLOCK_ALARM_H */
