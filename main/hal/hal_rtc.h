/**
 * @file    hal_rtc.h
 * @brief   Task 28: 片上 RTC 读写封装 —— 公开接口
 *
 * 存储位置：RTC 慢速内存（RTC_DATA_ATTR）
 * 持久化范围：深度睡眠唤醒、软件复位后保留；断电丢失。
 *
 * 数据模型：存储最后一次写入的 Unix 时间戳。
 *           主流程：SNTP 同步后 → hal_rtc_set_time(当前时间)
 *                   启动时 → hal_rtc_get_time() 获取上次已知时间
 */

#ifndef HAL_RTC_H
#define HAL_RTC_H

#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 初始化 RTC 内存区域，恢复之前保存的时间戳 */
void   hal_rtc_init(void);

/** 写入当前时间戳到 RTC 慢速内存（SNTP 同步后调用） */
void   hal_rtc_set_time(time_t t);

/** 读取 RTC 慢速内存中保存的时间戳 */
time_t hal_rtc_get_time(void);

#ifdef __cplusplus
}
#endif

#endif /* HAL_RTC_H */
