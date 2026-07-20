/**
 * @file    svc_wifi.c
 * @brief   Task 20: Wi-Fi 连接管理 —— STA 模式 + 自动重连 + NVS 凭据
 *
 * 状态机：IDLE → CONNECTING → CONNECTED / FAILED
 * 自动重连最多 WIFI_CONNECT_RETRY_MAX 次，间隔 WIFI_CONNECT_RETRY_INTERVAL_MS。
 * 凭据优先级：svc_wifi_connect() 参数 > NVS 存储 > app_config.h 默认值。
 */

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "svc_wifi.h"
#include "app_config.h"

static const char *TAG = "svc_wifi";

/* ---- 内部状态 ---- */
typedef enum {
    WIFI_STATE_IDLE = 0,
    WIFI_STATE_CONNECTING,
    WIFI_STATE_CONNECTED,
    WIFI_STATE_FAILED,
} wifi_state_t;

static wifi_state_t       s_state       = WIFI_STATE_IDLE;
static wifi_connect_cb_t  s_cb          = NULL;
static esp_netif_t       *s_netif       = NULL;
static int                s_retry_count = 0;
static bool               s_initialized = false;

/* ================================================================
 *  Event Handler
 * ================================================================ */

static void wifi_event_handler(void *arg, esp_event_base_t base,
                               int32_t id, void *data)
{
    (void)arg;

    if (base == WIFI_EVENT) {
        switch (id) {
        case WIFI_EVENT_STA_START:
            ESP_LOGI(TAG, "STA started, connecting...");
            esp_wifi_connect();
            s_state = WIFI_STATE_CONNECTING;
            break;

        case WIFI_EVENT_STA_DISCONNECTED: {
            wifi_event_sta_disconnected_t *d =
                (wifi_event_sta_disconnected_t *)data;
            ESP_LOGW(TAG, "STA disconnected (reason=%d)", d->reason);
            s_state = WIFI_STATE_IDLE;

            if (s_retry_count < WIFI_CONNECT_RETRY_MAX) {
                s_retry_count++;
                ESP_LOGI(TAG, "retry %d/%d in %d ms",
                         s_retry_count, WIFI_CONNECT_RETRY_MAX,
                         WIFI_CONNECT_RETRY_INTERVAL_MS);
                vTaskDelay(pdMS_TO_TICKS(WIFI_CONNECT_RETRY_INTERVAL_MS));
                esp_wifi_connect();
                s_state = WIFI_STATE_CONNECTING;
            } else {
                ESP_LOGE(TAG, "max retry reached, giving up");
                s_state = WIFI_STATE_FAILED;
                if (s_cb) s_cb(false);
            }
            break;
        }
        default:
            break;
        }
    } else if (base == IP_EVENT) {
        if (id == IP_EVENT_STA_GOT_IP) {
            ip_event_got_ip_t *ip = (ip_event_got_ip_t *)data;
            ESP_LOGI(TAG, "got IP: " IPSTR, IP2STR(&ip->ip_info.ip));
            s_state       = WIFI_STATE_CONNECTED;
            s_retry_count = 0;
            if (s_cb) s_cb(true);
        }
    }
}

/* ================================================================
 *  NVS 凭据读写
 * ================================================================ */

static bool load_creds(char *ssid, size_t ssid_len,
                       char *pass, size_t pass_len) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) return false;

    size_t len = ssid_len;
    err = nvs_get_str(handle, NVS_KEY_WIFI_SSID, ssid, &len);
    if (err != ESP_OK) { nvs_close(handle); return false; }

    len = pass_len;
    err = nvs_get_str(handle, NVS_KEY_WIFI_PASS, pass, &len);
    nvs_close(handle);
    return (err == ESP_OK);
}

/* ================================================================
 *  公开 API
 * ================================================================ */

void svc_wifi_init(void) {
    if (s_initialized) return;
    s_initialized = true;

    /* NVS */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    /* netif + event loop */
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    s_netif = esp_netif_create_default_wifi_sta();

    /* WiFi init */
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    /* 事件注册 */
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                &wifi_event_handler, NULL));

    /* 设为 STA 模式 */
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

    /* 配置 Wi-Fi —— 优先 NVS，其次 app_config.h 默认值 */
    wifi_config_t wifi_cfg = {0};
    char ssid[33] = {0}, pass[65] = {0};
    bool have_creds = false;

    if (load_creds(ssid, sizeof(ssid), pass, sizeof(pass))) {
        have_creds = true;
        ESP_LOGI(TAG, "loaded stored creds for SSID: %s", ssid);
    } else if (strlen(WIFI_DEFAULT_SSID) > 0 &&
               strcmp(WIFI_DEFAULT_SSID, "你的WiFi名") != 0) {
        /* NVS 无凭据 → 回退到 app_config.h 编译期配置 */
        strncpy(ssid, WIFI_DEFAULT_SSID, sizeof(ssid) - 1);
        strncpy(pass, WIFI_DEFAULT_PASS, sizeof(pass) - 1);
        have_creds = true;
        ESP_LOGI(TAG, "using default SSID from app_config: %s", ssid);
    } else {
        ESP_LOGW(TAG, "no WiFi credentials (set WIFI_DEFAULT_SSID in app_config.h)");
    }

    if (have_creds) {
        strncpy((char *)wifi_cfg.sta.ssid, ssid, 32);
        strncpy((char *)wifi_cfg.sta.password, pass, 64);
        wifi_cfg.sta.scan_method = WIFI_SCAN_METHOD;
        wifi_cfg.sta.sort_method = WIFI_SORT_METHOD;
        wifi_cfg.sta.threshold.authmode = WIFI_AUTH_MODE;
    }

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    s_state = WIFI_STATE_IDLE;
    ESP_LOGI(TAG, "init done");
}

void svc_wifi_connect(const char *ssid, const char *pass) {
    if (!s_initialized) {
        svc_wifi_init();
    }

    /* 更新配置 */
    if (ssid && ssid[0]) {
        wifi_config_t cfg = {0};
        strncpy((char *)cfg.sta.ssid, ssid, 32);
        if (pass && pass[0])
            strncpy((char *)cfg.sta.password, pass, 64);
        cfg.sta.scan_method = WIFI_SCAN_METHOD;
        cfg.sta.sort_method = WIFI_SORT_METHOD;
        cfg.sta.threshold.authmode = WIFI_AUTH_MODE;

        esp_wifi_disconnect();
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &cfg));

        /* 保存到 NVS */
        nvs_handle_t handle;
        esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
        if (err == ESP_OK) {
            nvs_set_str(handle, NVS_KEY_WIFI_SSID, ssid);
            if (pass && pass[0])
                nvs_set_str(handle, NVS_KEY_WIFI_PASS, pass);
            nvs_commit(handle);
            nvs_close(handle);
        }
    }

    s_retry_count = 0;
    s_state = WIFI_STATE_CONNECTING;
    esp_wifi_connect();
    ESP_LOGI(TAG, "connecting...");
}

void svc_wifi_disconnect(void) {
    if (!s_initialized) return;
    esp_wifi_disconnect();
    s_state = WIFI_STATE_IDLE;
    s_retry_count = 0;
    if (s_cb) s_cb(false);
    ESP_LOGI(TAG, "disconnected");
}

bool svc_wifi_is_connected(void) {
    return (s_state == WIFI_STATE_CONNECTED);
}

void svc_wifi_set_callback(wifi_connect_cb_t cb) {
    s_cb = cb;
}
