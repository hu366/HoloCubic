/**
 * @file    svc_sntp.c
 * @brief   Task 26: SNTP 时间同步 —— 使用 esp_sntp（经典 API，v6.0.2 兼容）
 *
 * 依赖：WiFi 已连接后调用 svc_sntp_sync()。
 *      同步成功后系统时间（time()）自动更新。
 */

#include <stdio.h>
#include "svc_sntp.h"
#include "esp_sntp.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "svc_sntp";

static bool s_inited = false;
static bool s_synced = false;

void svc_sntp_init(void) {
    if (s_inited) {
        ESP_LOGW(TAG, "already inited");
        return;
    }

    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_init();
    s_inited = true;

    /* 检查是否已同步（esp_sntp_init 自动启动同步） */
    if (sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) {
        s_synced = true;
        ESP_LOGI(TAG, "already synced at init time");
    } else {
        ESP_LOGI(TAG, "init ok, waiting for sync...");
    }
}

void svc_sntp_sync(void) {
    if (!s_inited) {
        svc_sntp_init();
    }

    /* 已同步则跳过 */
    if (s_synced) {
        ESP_LOGI(TAG, "already synced");
        return;
    }

    sntp_sync_status_t status = sntp_get_sync_status();
    if (status == SNTP_SYNC_STATUS_COMPLETED) {
        s_synced = true;
        ESP_LOGI(TAG, "sync completed");
        return;
    }

    /* 等待同步完成（带超时 10s） */
    ESP_LOGI(TAG, "waiting for sync (timeout 10s)...");
    for (int i = 0; i < 20; i++) {
        vTaskDelay(pdMS_TO_TICKS(500));
        status = sntp_get_sync_status();
        if (status == SNTP_SYNC_STATUS_COMPLETED) {
            s_synced = true;
            ESP_LOGI(TAG, "sync completed");
            return;
        }
    }

    ESP_LOGW(TAG, "sync timeout after 10s");
}

bool svc_sntp_is_synced(void) {
    if (s_synced) return true;
    if (s_inited && sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) {
        s_synced = true;
        return true;
    }
    return false;
}
