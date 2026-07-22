/**
 * @file    svc_sntp.h
 * @brief   Task 26: SNTP 时间同步服务 —— 公开接口
 *
 * 依赖：WiFi 已连接（svc_wifi_is_connected() == true）
 *      同步后系统时间自动更新，hal_rtc 可从中读取并持久化。
 */

#ifndef SVC_SNTP_H
#define SVC_SNTP_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 初始化 SNTP 客户端（注册处理函数，不触发同步） */
void svc_sntp_init(void);

/** 发起 SNTP 时间同步请求（异步，不阻塞） */
void svc_sntp_sync(void);

/** 查询 SNTP 是否已同步成功
 *  @return true=时间已从 NTP 服务器获取并设置到系统时钟 */
bool svc_sntp_is_synced(void);

#ifdef __cplusplus
}
#endif

#endif /* SVC_SNTP_H */
