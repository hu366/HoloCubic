/**
 * @file    svc_http.c
 * @brief   Task 22: HTTP 客户端封装 —— 流式响应 + 超时 + 错误码映射
 *
 * 基于 esp_http_client，封装 svc_http_get_stream。
 * 流式回调每块响应数据，不累积完整响应（PSRAM 友好）。
 * 超时 10s，只认 200 OK。
 */

#include "esp_http_client.h"
#include "esp_log.h"
#include "svc_http.h"
#include "app_config.h"

static const char *TAG = "svc_http";

/* ---- 回调上下文 ---- */
typedef struct {
    svc_http_chunk_cb_t chunk_cb;
    void               *user_data;
    int                 status_code;
    bool                has_error;
} http_ctx_t;

/* ================================================================
 *  esp_http_client 事件回调
 * ================================================================ */

static esp_err_t http_event_handler(esp_http_client_event_t *evt) {
    http_ctx_t *ctx = (http_ctx_t *)evt->user_data;

    switch (evt->event_id) {
    case HTTP_EVENT_ON_DATA:
        if (ctx->chunk_cb && !ctx->has_error) {
            int ret = ctx->chunk_cb(evt->data, evt->data_len, ctx->user_data);
            if (ret != 0) {
                ESP_LOGW(TAG, "chunk callback requested stop");
                ctx->has_error = true;
            }
        }
        break;

    case HTTP_EVENT_ON_FINISH:
        /* 正常结束，不做额外动作 */
        break;

    case HTTP_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "disconnected");
        ctx->has_error = true;
        break;

    case HTTP_EVENT_ERROR:
        ESP_LOGE(TAG, "HTTP error");
        ctx->has_error = true;
        break;

    default:
        break;
    }

    return ESP_OK;
}

/* ================================================================
 *  公开 API
 * ================================================================ */

bool svc_http_get_stream(const char *url, svc_http_chunk_cb_t chunk_cb,
                         void *user_data) {
    if (!url || !url[0]) {
        ESP_LOGE(TAG, "empty URL");
        return false;
    }

    http_ctx_t ctx = {
        .chunk_cb   = chunk_cb,
        .user_data  = user_data,
        .status_code = 0,
        .has_error  = false,
    };

    esp_http_client_config_t cfg = {
        .url              = url,
        .timeout_ms       = WEATHER_HTTP_TIMEOUT_MS,
        .buffer_size      = 512,
        .event_handler    = http_event_handler,
        .user_data        = &ctx,
        .disable_auto_redirect = false,
    };

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        ESP_LOGE(TAG, "esp_http_client_init failed");
        return false;
    }

    esp_err_t err = esp_http_client_perform(client);
    ctx.status_code = (err == ESP_OK)
                      ? esp_http_client_get_status_code(client)
                      : 0;

    esp_http_client_cleanup(client);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "perform failed: %d", err);
        return false;
    }

    if (ctx.status_code != 200) {
        ESP_LOGW(TAG, "HTTP status %d (expected 200)", ctx.status_code);
        return false;
    }

    if (ctx.has_error) {
        ESP_LOGW(TAG, "request had errors");
        return false;
    }

    ESP_LOGI(TAG, "GET %s → 200 OK", url);
    return true;
}
