/**
 * @file    weather.c
 * @brief   天气模块 —— 心知天气 API v3 + cJSON 解析 + LVGL 展示
 *
 * 请求流程：公网 IP 查询(多源回退) → 心知天气 API → cJSON 解析 → LVGL 展示
 * 免费用户返回：location.name / now.text / now.code / now.temperature
 *
 * 跨核安全：
 *   Core 1 → weather_input_cb → 只设脏标
 *   Core 0 → weather_process_updates → 全部 LVGL 操作
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include "weather.h"
#include "menu_ui.h"
#include "menu_engine.h"
#include "app_config.h"
#include "app_types.h"
#include "svc_wifi.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "cJSON.h"

static const char *TAG = "weather";

/* ================================================================
 *  静态状态
 * ================================================================ */

static weather_data_t s_data;
static menu_page_t   *s_page;
static lv_obj_t      *s_city_label;
static lv_obj_t      *s_temp_label;
static lv_obj_t      *s_desc_label;
static lv_obj_t      *s_status_label;  /* "Not Connected" / "Loading..." / "" */
static lv_obj_t      *s_btn_label;
static lv_obj_t      *s_btn_bg;
static int            s_btn_index;     /* 0=Refresh 1=Back */
static uint32_t       s_fetch_timer_ms;
static bool           s_has_data;
static bool           s_fetching;

/* 按钮动画 */
#define BTN_ANIM_MS 200
#define BTN_W       180
#define BTN_CX      ((LCD_HOR_RES - BTN_W) / 2)

static struct {
    bool    on;
    int64_t t0;
    int     dir;
    bool    swapped;
} s_btn_anim;

/* 跨任务标志 */
static volatile bool s_ui_dirty;
static volatile bool s_btn_dirty;
static volatile int  s_btn_dir;
static volatile bool s_btn_pending;
static volatile bool s_cb_override;
static volatile bool s_fetch_done;
static volatile bool s_fetch_ok;

/* HTTP 响应缓冲 */
#define HTTP_BUF_SIZE 4096
static char          s_http_buf[HTTP_BUF_SIZE];
static volatile int  s_http_buf_len;
static volatile bool s_http_overflow;

/* ================================================================
 *  内部辅助
 * ================================================================ */

static void refresh(void) { lv_refr_now(NULL); }

static const char* btn_name(int idx) {
    return (idx == 0) ? "Refresh" : "Back";
}

/* ================================================================
 *  JSON 解析 —— 心知天气 v3 格式
 *
 *  {
 *    "results": [{
 *      "location": { "name": "北京", ... },
 *      "now": {
 *        "text": "多云",         // 天气现象文字
 *        "code": "4",            // 天气现象代码
 *        "temperature": "14"     // 温度（字符串）
 *      },
 *      "last_update": "2025-01-01T08:00:00+08:00"
 *    }]
 *  }
 * ================================================================ */

bool weather_parse_json(const char *json, weather_data_t *out) {
    if (!json || !json[0] || !out) return false;

    memset(out, 0, sizeof(*out));

    cJSON *root = cJSON_Parse(json);
    if (!root) {
        ESP_LOGW(TAG, "cJSON parse failed");
        return false;
    }

    /* results 数组 */
    cJSON *results = cJSON_GetObjectItem(root, "results");
    if (!results || !cJSON_IsArray(results)) {
        cJSON_Delete(root);
        ESP_LOGW(TAG, "missing results array");
        return false;
    }
    cJSON *result0 = cJSON_GetArrayItem(results, 0);
    if (!result0) {
        cJSON_Delete(root);
        ESP_LOGW(TAG, "results array empty");
        return false;
    }

    /* location.name → city */
    cJSON *location = cJSON_GetObjectItem(result0, "location");
    if (location) {
        cJSON *name = cJSON_GetObjectItem(location, "name");
        if (name && cJSON_IsString(name)) {
            strncpy(out->city, name->valuestring, sizeof(out->city) - 1);
        }
    }

    /* now.text → description, now.code → icon_code, now.temperature → temperature */
    cJSON *now = cJSON_GetObjectItem(result0, "now");
    if (!now) {
        cJSON_Delete(root);
        ESP_LOGW(TAG, "missing now object");
        return false;
    }

    cJSON *text = cJSON_GetObjectItem(now, "text");
    if (text && cJSON_IsString(text)) {
        strncpy(out->description, text->valuestring, sizeof(out->description) - 1);
    }

    cJSON *code = cJSON_GetObjectItem(now, "code");
    if (code && cJSON_IsString(code)) {
        strncpy(out->icon_code, code->valuestring, sizeof(out->icon_code) - 1);
    }

    /* temperature 是字符串，需 atoi 转换 */
    cJSON *temp = cJSON_GetObjectItem(now, "temperature");
    if (!temp || !cJSON_IsString(temp)) {
        cJSON_Delete(root);
        ESP_LOGW(TAG, "missing temperature");
        return false;
    }
    out->temperature = (int8_t)atoi(temp->valuestring);

    cJSON_Delete(root);

    out->last_update = (uint64_t)time(NULL);
    out->stale = false;
    return true;
}

bool weather_is_stale(const weather_data_t *data) {
    if (!data) return true;
    if (data->stale) return true;
    if (data->last_update == 0) return true;

    uint64_t now = (uint64_t)time(NULL);
    uint64_t age_ms = (now > data->last_update)
                      ? (now - data->last_update) * 1000
                      : 0;
    return (age_ms > WEATHER_STALE_MS);
}

/* ================================================================
 *  HTTP 流式回调
 * ================================================================ */

static int http_chunk_handler(esp_http_client_event_t *evt) {
    if (evt->event_id == HTTP_EVENT_ON_DATA) {
        int remaining = HTTP_BUF_SIZE - s_http_buf_len - 1;
        if (remaining > 0) {
            int copy = (evt->data_len < remaining) ? evt->data_len : remaining;
            memcpy(s_http_buf + s_http_buf_len, evt->data, copy);
            s_http_buf_len += copy;
            s_http_buf[s_http_buf_len] = '\0';
        } else {
            s_http_overflow = true;
        }
    }
    return ESP_OK;
}

/* ---- IP 查询专用缓冲和回调（ipify 返回纯文本 IP，不超 15 字节） ---- */
static char s_ip_buf[16];
static volatile int s_ip_buf_len;

static int ip_fetch_handler(esp_http_client_event_t *evt) {
    if (evt->event_id == HTTP_EVENT_ON_DATA) {
        int remaining = (int)sizeof(s_ip_buf) - s_ip_buf_len - 1;
        if (remaining > 0) {
            int copy = (evt->data_len < remaining) ? evt->data_len : remaining;
            memcpy(s_ip_buf + s_ip_buf_len, evt->data, copy);
            s_ip_buf_len += copy;
            s_ip_buf[s_ip_buf_len] = '\0';
        }
    }
    return ESP_OK;
}

/* ================================================================
 *  发起 HTTP 请求（在 logic_task 上下文调用）
 *
 *  两步走：
 *    1. ipify.org   → 获取公网 IP（超时 5s）
 *    2. 心知天气 API → 用公网 IP 作为 location 查天气
 *    IP 获取失败时回退到 WEATHER_API_LOCATION
 * ================================================================ */

static void weather_do_fetch(void) {
    s_http_buf_len  = 0;
    s_http_overflow = false;
    s_fetching      = true;

    /* ---- 第 1 步：查询公网 IP（多源回退） ---- */
    const char *ip_urls[] = {
        "http://ip.3322.net",
        "http://icanhazip.com",
        "http://ifconfig.me",
    };
    bool ip_ok = false;

    for (int i = 0; i < 3 && !ip_ok; i++) {
        s_ip_buf_len = 0;
        memset(s_ip_buf, 0, sizeof(s_ip_buf));

        esp_http_client_config_t ip_cfg = {
            .url               = ip_urls[i],
            .timeout_ms        = 4000,
            .buffer_size       = 256,
            .event_handler     = ip_fetch_handler,
        };
        esp_http_client_handle_t ip_client = esp_http_client_init(&ip_cfg);
        if (ip_client) {
            esp_err_t ip_err = esp_http_client_perform(ip_client);
            int ip_status = (ip_err == ESP_OK)
                            ? esp_http_client_get_status_code(ip_client) : 0;
            esp_http_client_cleanup(ip_client);

            if (ip_err == ESP_OK && ip_status == 200 && s_ip_buf_len > 0) {
                /* 去掉末尾的 \r \n 空格 */
                while (s_ip_buf_len > 0) {
                    char c = s_ip_buf[s_ip_buf_len - 1];
                    if (c == '\r' || c == '\n' || c == ' ') {
                        s_ip_buf[--s_ip_buf_len] = '\0';
                    } else break;
                }
                if (s_ip_buf_len > 0) {
                    ESP_LOGI(TAG, "public IP: %s", s_ip_buf);
                    ip_ok = true;
                }
            } else {
                ESP_LOGW(TAG, "IP src %d failed (err=%d status=%d)",
                         i, ip_err, ip_status);
            }
        } else {
            ESP_LOGW(TAG, "IP src %d client init failed", i);
        }
    }

    if (!ip_ok) {
        ESP_LOGW(TAG, "all IP sources failed, use fallback");
        s_ip_buf[0] = '\0';
    }

    /* ---- 第 2 步：用公网 IP（或回退位置）查天气 ---- */
    const char *location = (s_ip_buf[0] != '\0') ? s_ip_buf
                                                  : WEATHER_API_LOCATION;
    char url[512];
    snprintf(url, sizeof(url), WEATHER_API_URL_FMT,
             WEATHER_API_KEY, location);

    esp_http_client_config_t cfg = {
        .url               = url,
        .timeout_ms        = WEATHER_HTTP_TIMEOUT_MS,
        .buffer_size       = 512,
        .event_handler     = http_chunk_handler,
        .disable_auto_redirect = false,
    };

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        ESP_LOGE(TAG, "esp_http_client_init failed");
        s_fetch_done = true;
        s_fetch_ok   = false;
        s_fetching   = false;
        return;
    }

    esp_err_t err = esp_http_client_perform(client);
    int status = (err == ESP_OK) ? esp_http_client_get_status_code(client) : 0;
    esp_http_client_cleanup(client);

    s_fetch_ok   = (err == ESP_OK && status == 200 && !s_http_overflow);
    s_fetch_done = true;
    s_fetching   = false;

    if (s_fetch_ok) {
        weather_data_t parsed;
        if (weather_parse_json(s_http_buf, &parsed)) {
            memcpy(&s_data, &parsed, sizeof(s_data));
            s_has_data = true;
            ESP_LOGI(TAG, "fetch OK: %s %dC %s",
                     s_data.city, s_data.temperature, s_data.description);
        } else {
            s_fetch_ok = false;
            ESP_LOGW(TAG, "JSON parse failed");
        }
    } else {
        ESP_LOGW(TAG, "HTTP request failed (err=%d status=%d overflow=%d)",
                 err, status, s_http_overflow);
    }
    s_ui_dirty = true;
}

/* ================================================================
 *  UI 更新（仅在 Core 0 调用）
 * ================================================================ */

static void weather_update_ui(void) {
    if (!s_page || !s_page->container) return;

    if (!svc_wifi_is_connected()) {
        if (s_city_label)   lv_label_set_text(s_city_label, "");
        if (s_temp_label)   lv_label_set_text(s_temp_label, "No WiFi");
        if (s_desc_label)   lv_label_set_text(s_desc_label, "");
        if (s_status_label) lv_label_set_text(s_status_label, "Not Connected");
    } else if (s_fetching) {
        if (s_temp_label)   lv_label_set_text(s_temp_label, "...");
        if (s_status_label) lv_label_set_text(s_status_label, "Loading...");
    } else if (!s_has_data) {
        if (s_temp_label)   lv_label_set_text(s_temp_label, "--C");
        if (s_status_label) lv_label_set_text(s_status_label, "No Data");
    } else {
        if (s_city_label) lv_label_set_text(s_city_label, s_data.city);

        char buf[16];
        snprintf(buf, sizeof(buf), "%dC", s_data.temperature);
        if (s_temp_label) lv_label_set_text(s_temp_label, buf);

        if (s_desc_label) lv_label_set_text(s_desc_label, s_data.description);

        bool stale = weather_is_stale(&s_data);
        if (s_status_label) {
            lv_label_set_text(s_status_label, stale ? "(outdated)" : "");
        }
    }
    refresh();
}

static void weather_update_btn_label(void) {
    if (!s_btn_label) return;
    lv_label_set_text(s_btn_label, btn_name(s_btn_index));
}

/* ================================================================
 *  输入回调（Core 1，只设脏标，严禁操作 LVGL）
 * ================================================================ */

static int64_t s_last_tilt_us = 0;

static void weather_input_cb(imu_tilt_dir_t tilt, int8_t rotary, bool btn_short) {
    if (btn_short) {
        s_btn_pending = true;
        return;
    }

    if (rotary != 0) {
        int64_t now = esp_timer_get_time();
        if (now - s_last_tilt_us < 300000UL) return;
        s_last_tilt_us = now;

        int old = s_btn_index;
        if (rotary > 0)
            s_btn_index = (s_btn_index + 1) % 2;
        else
            s_btn_index = (s_btn_index == 0) ? 1 : 0;

        if (s_btn_index != old) {
            s_btn_dir   = (rotary > 0) ? 1 : -1;
            s_btn_dirty = true;
        }
        return;
    }

    if (tilt != IMU_TILT_FRONT && tilt != IMU_TILT_BACK) return;

    int64_t now = esp_timer_get_time();
    if (now - s_last_tilt_us < 300000UL) return;
    s_last_tilt_us = now;

    int old = s_btn_index;
    if (tilt == IMU_TILT_FRONT)
        s_btn_index = (s_btn_index + 1) % 2;
    else
        s_btn_index = (s_btn_index == 0) ? 1 : 0;

    if (s_btn_index != old) {
        s_btn_dir   = (tilt == IMU_TILT_FRONT) ? 1 : -1;
        s_btn_dirty = true;
    }
}

/* ================================================================
 *  页面创建器
 * ================================================================ */

static void on_container_delete(lv_event_t *e) {
    (void)e;
    s_page         = NULL;
    s_city_label   = NULL;
    s_temp_label   = NULL;
    s_desc_label   = NULL;
    s_status_label = NULL;
    s_btn_label    = NULL;
    s_btn_bg       = NULL;
    ESP_LOGI(TAG, "page destroyed");
}

static menu_page_t* weather_creator(void) {
    menu_page_t *pg = calloc(1, sizeof(menu_page_t));
    if (!pg) return NULL;
    pg->title     = "Weather";
    pg->btn_count = 0;

    lv_obj_t *scr = menu_ui_get_screen();
    if (!scr) { free(pg); return NULL; }

    /* ---- 容器 ---- */
    pg->container = lv_obj_create(scr);
    lv_obj_set_size(pg->container, LCD_HOR_RES, LCD_VER_RES);
    lv_obj_set_x(pg->container, LCD_HOR_RES);
    lv_obj_set_style_bg_color(pg->container, lv_color_hex(0x16213e), 0);
    lv_obj_set_style_border_width(pg->container, 0, 0);
    lv_obj_set_style_pad_all(pg->container, 0, 0);
    lv_obj_add_event_cb(pg->container, on_container_delete,
                        LV_EVENT_DELETE, NULL);

    /* ---- 标题 ---- */
    pg->title_label = lv_label_create(pg->container);
    lv_label_set_text(pg->title_label, "Weather");
    lv_obj_set_style_text_font(pg->title_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(pg->title_label, lv_color_white(), 0);
    lv_obj_set_style_border_width(pg->title_label, 0, 0);
    lv_obj_set_style_bg_opa(pg->title_label, LV_OPA_TRANSP, 0);
    lv_obj_align(pg->title_label, LV_ALIGN_TOP_MID, 0, 4);

    /* ---- 城市名 ---- */
    s_city_label = lv_label_create(pg->container);
    lv_obj_set_style_text_font(s_city_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_city_label, lv_color_hex(0xAAAAAA), 0);
    lv_obj_set_style_border_width(s_city_label, 0, 0);
    lv_obj_set_style_bg_opa(s_city_label, LV_OPA_TRANSP, 0);
    lv_obj_align(s_city_label, LV_ALIGN_CENTER, 0, -55);

    /* ---- 温度（大字） ---- */
    s_temp_label = lv_label_create(pg->container);
    lv_obj_set_style_text_font(s_temp_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_temp_label, lv_color_hex(0x4FC3F7), 0);
    lv_obj_set_style_border_width(s_temp_label, 0, 0);
    lv_obj_set_style_bg_opa(s_temp_label, LV_OPA_TRANSP, 0);
    lv_label_set_text(s_temp_label, "No WiFi");
    lv_obj_align(s_temp_label, LV_ALIGN_CENTER, 0, -15);

    /* ---- 描述 ---- */
    s_desc_label = lv_label_create(pg->container);
    lv_obj_set_style_text_font(s_desc_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_desc_label, lv_color_hex(0xCCCCCC), 0);
    lv_obj_set_style_border_width(s_desc_label, 0, 0);
    lv_obj_set_style_bg_opa(s_desc_label, LV_OPA_TRANSP, 0);
    lv_obj_align(s_desc_label, LV_ALIGN_CENTER, 0, 18);

    /* ---- 状态文字 ---- */
    s_status_label = lv_label_create(pg->container);
    lv_obj_set_style_text_font(s_status_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_status_label, lv_color_hex(0xFFA726), 0);
    lv_obj_set_style_border_width(s_status_label, 0, 0);
    lv_obj_set_style_bg_opa(s_status_label, LV_OPA_TRANSP, 0);
    lv_label_set_text(s_status_label, "Not Connected");
    lv_obj_align(s_status_label, LV_ALIGN_CENTER, 0, 40);

    /* ---- 底部按钮 ---- */
    s_btn_bg = lv_obj_create(pg->container);
    lv_obj_set_size(s_btn_bg, BTN_W, 38);
    lv_obj_set_pos(s_btn_bg, BTN_CX, LCD_VER_RES - 45);
    lv_obj_set_style_bg_color(s_btn_bg, lv_color_hex(0x0f3460), 0);
    lv_obj_set_style_border_width(s_btn_bg, 0, 0);
    lv_obj_set_style_radius(s_btn_bg, 10, 0);
    lv_obj_remove_flag(s_btn_bg, LV_OBJ_FLAG_SCROLLABLE);

    s_btn_label = lv_label_create(s_btn_bg);
    lv_obj_set_style_text_font(s_btn_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_btn_label, lv_color_hex(0x4FC3F7), 0);
    lv_obj_set_style_border_width(s_btn_label, 0, 0);
    lv_obj_set_style_bg_opa(s_btn_label, LV_OPA_TRANSP, 0);
    lv_obj_center(s_btn_label);

    /* ---- 状态初始化 ---- */
    s_page        = pg;
    s_btn_index   = 0;
    s_cb_override = true;
    memset(&s_btn_anim, 0, sizeof(s_btn_anim));

    weather_update_btn_label();
    weather_update_ui();
    ESP_LOGI(TAG, "page created");
    return pg;
}

/* ================================================================
 *  公开 API
 * ================================================================ */

void weather_init(void) {
    memset(&s_data, 0, sizeof(s_data));
    s_data.temperature = 0;
    s_data.stale       = true;

    s_page         = NULL;
    s_city_label   = NULL;
    s_temp_label   = NULL;
    s_desc_label   = NULL;
    s_status_label = NULL;
    s_btn_label    = NULL;
    s_btn_bg       = NULL;
    s_btn_index    = 0;
    s_has_data     = false;
    s_fetching     = false;

    s_fetch_timer_ms = 0;
    s_ui_dirty       = false;
    s_btn_dirty      = false;
    s_btn_pending    = false;
    s_cb_override    = false;
    s_fetch_done     = false;
    s_fetch_ok       = false;

    menu_ui_register_creator(MENU_ITEM_WEATHER, weather_creator);
    ESP_LOGI(TAG, "init ok");
}

void weather_tick(uint32_t dt_ms) {
    if (!s_fetching && svc_wifi_is_connected()) {
        s_fetch_timer_ms += dt_ms;
        if (s_fetch_timer_ms >= WEATHER_UPDATE_INTERVAL_MS || !s_has_data) {
            s_fetch_timer_ms = 0;
            weather_do_fetch();
        }
    }
}

void weather_process_updates(void) {
    /* ---- 按钮切换动画 ---- */
    if (s_btn_anim.on) {
        int64_t elapsed = (esp_timer_get_time() - s_btn_anim.t0) / 1000;
        if (elapsed >= BTN_ANIM_MS) {
            lv_obj_set_x(s_btn_bg, BTN_CX);
            weather_update_btn_label();
            refresh();
            s_btn_anim.on = false;
        } else {
            int half = BTN_ANIM_MS / 2;
            int dir  = s_btn_anim.dir;
            int32_t x;
            if (elapsed < half) {
                int32_t e = menu_ui_ease_out((int32_t)elapsed, half);
                x = BTN_CX - (dir * LCD_HOR_RES * e) / 256;
            } else {
                if (!s_btn_anim.swapped) {
                    s_btn_anim.swapped = true;
                    weather_update_btn_label();
                }
                int32_t e = menu_ui_ease_out((int32_t)(elapsed - half), half);
                x = (dir > 0) ? (LCD_HOR_RES - (LCD_HOR_RES - BTN_CX) * e / 256)
                              : (-BTN_W + (BTN_CX + BTN_W) * e / 256);
            }
            lv_obj_set_x(s_btn_bg, x);
            refresh();
        }
    }

    /* 替换 sub 回调 */
    if (s_cb_override && s_page) {
        s_cb_override = false;
        menu_engine_set_sub_callback(weather_input_cb);
        s_last_tilt_us = 0;
    }

    /* 按钮索引变化 → 动画 */
    if (s_btn_dirty && !s_btn_anim.on) {
        s_btn_dirty = false;
        if (s_page && s_btn_bg) {
            s_btn_anim.on      = true;
            s_btn_anim.t0      = esp_timer_get_time();
            s_btn_anim.dir     = s_btn_dir;
            s_btn_anim.swapped = false;
        }
    }

    /* 按钮按下 */
    if (s_btn_pending) {
        s_btn_pending = false;
        switch (s_btn_index) {
        case 0: /* Refresh */
            if (svc_wifi_is_connected() && !s_fetching) {
                s_fetch_timer_ms = WEATHER_UPDATE_INTERVAL_MS;
                s_ui_dirty = true;
            }
            break;
        case 1: /* Back */
            menu_ui_go_back();
            return;
        }
    }

    /* HTTP 请求完成 → 刷新 UI */
    if (s_fetch_done) {
        s_fetch_done = false;
        if (!s_fetch_ok && !s_has_data) {
            s_ui_dirty = true;
        }
        if (s_page && s_page->container) {
            weather_update_ui();
        }
    }

    /* 通用 UI 刷新 */
    if (s_ui_dirty) {
        s_ui_dirty = false;
        if (s_page && s_page->container) {
            weather_update_ui();
        }
    }
}
