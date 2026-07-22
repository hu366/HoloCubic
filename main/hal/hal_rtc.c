/**
 * @file    hal_rtc.c
 * @brief   Task 28: 片上 RTC 读写封装
 *
 * 实现：使用 ESP32-S3 RTC 慢速内存（RTC_DATA_ATTR）存储 Unix 时间戳。
 *       深度睡眠唤醒/软件复位后保留；断电丢失。
 *
 * 使用流程：
 *   hal_rtc_init()          → 启动时调用，恢复上次保存的时间
 *   hal_rtc_get_time()      → 获取已保存的时间戳
 *   hal_rtc_set_time(t)     → SNTP 同步成功后更新
 */

#include <stdio.h>
#include <string.h>
#include "hal_rtc.h"
#include "esp_attr.h"

/* ================================================================
 *  RTC 慢速内存（深度睡眠/软件复位后保留，断电丢失）
 * ================================================================ */

#define RTC_MAGIC  0xACCE5500  /* 数据有效性魔数 */

RTC_DATA_ATTR static struct {
    uint32_t magic;     /* 魔数：写入过的标记 */
    uint32_t time_low;  /* Unix 时间戳低 32 位 */
    uint32_t time_high; /* Unix 时间戳高 32 位（time_t 在 ESP32 上是 64 位） */
} s_rtc_data;

static bool s_inited = false;

/* ================================================================
 *  公开接口
 * ================================================================ */

void hal_rtc_init(void) {
    if (s_inited) return;

    if (s_rtc_data.magic != RTC_MAGIC) {
        /* 首次启动或断电后数据丢失，初始化为 0 */
        s_rtc_data.magic     = RTC_MAGIC;
        s_rtc_data.time_low  = 0;
        s_rtc_data.time_high = 0;
    }
    s_inited = true;
}

void hal_rtc_set_time(time_t t) {
    if (!s_inited) hal_rtc_init();

    s_rtc_data.magic     = RTC_MAGIC;
    s_rtc_data.time_low  = (uint32_t)(t & 0xFFFFFFFF);
    s_rtc_data.time_high = (uint32_t)((uint64_t)t >> 32);
}

time_t hal_rtc_get_time(void) {
    if (!s_inited) hal_rtc_init();

    if (s_rtc_data.magic != RTC_MAGIC) {
        return (time_t)0;
    }

    uint64_t t = ((uint64_t)s_rtc_data.time_high << 32) | s_rtc_data.time_low;
    return (time_t)t;
}
