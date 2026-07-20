/**
 * @file    svc_http.h
 * @brief   HTTP 客户端封装 —— 接口声明
 *
 * 实现见 svc_http.c（Task 22）
 */

#ifndef SVC_HTTP_H
#define SVC_HTTP_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** HTTP 响应回调
 *  @param data      响应数据块
 *  @param data_len  数据长度
 *  @param user_data 用户指针
 *  @return          ESP_OK=继续 其他=停止
 */
typedef int (*svc_http_chunk_cb_t)(const char *data, size_t data_len,
                                   void *user_data);

/** 发起 HTTP GET 请求，流式回调每块响应数据
 *  @param url       完整 URL
 *  @param chunk_cb  每块数据回调
 *  @param user_data 透传给回调
 *  @return true=请求完成  false=失败（连接/超时/非 200）
 */
bool svc_http_get_stream(const char *url, svc_http_chunk_cb_t chunk_cb,
                         void *user_data);

#ifdef __cplusplus
}
#endif

#endif
