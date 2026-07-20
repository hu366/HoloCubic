/**
 * @file    svc_wifi.h
 * @brief   Wi-Fi 连接管理（STA 模式）—— 接口声明
 *
 * 实现见 svc_wifi.c（Task 20）
 */

#ifndef SVC_WIFI_H
#define SVC_WIFI_H

#include <stdbool.h>
#include "app_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/** 初始化 Wi-Fi 栈（netif + event loop） */
void svc_wifi_init(void);

/** 连接 Wi-Fi。ssid/pass 为空则从 NVS 读取 */
void svc_wifi_connect(const char *ssid, const char *pass);

/** 断开并清理 */
void svc_wifi_disconnect(void);

/** 查询当前连接状态 */
bool svc_wifi_is_connected(void);

/** 注册连接状态变化回调 */
void svc_wifi_set_callback(wifi_connect_cb_t cb);

#ifdef __cplusplus
}
#endif

#endif
