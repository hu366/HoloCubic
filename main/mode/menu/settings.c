/**
 * @file    settings.c
 * @brief   设置页面 —— 屏幕镜像切换
 *
 * 页面结构：2 个按钮（"Mirror: ON"/OFF" + "Back"）
 * 点击 Mirror 按钮即时切换镜像，按钮文字同步更新。
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "settings.h"
#include "menu_ui.h"
#include "menu_engine.h"
#include "app_config.h"
#include "hal_display.h"
#include "esp_log.h"

static const char *TAG = "settings";

static menu_page_t *s_page = NULL;
static lv_obj_t    *s_mirror_label = NULL;

/* ---- 按钮回调（Core 0 安全执行 LVGL 操作） ---- */
static void on_btn(int index)
{
    switch (index) {
    case 0: /* Mirror 切换 */
        {
            bool mirrored = hal_display_is_mirrored();
            hal_display_set_mirror(!mirrored);
            if (s_mirror_label) {
                lv_label_set_text(s_mirror_label,
                    hal_display_is_mirrored() ? "Mirror: ON" : "Mirror: OFF");
            }
            ESP_LOGI(TAG, "Mirror toggled -> %s",
                     hal_display_is_mirrored() ? "ON" : "OFF");
        }
        break;
    case 1: /* Back */
        menu_ui_go_back();
        break;
    }
}

/* ---- 页面创建器 ---- */
static void on_container_delete(lv_event_t *e)
{
    (void)e;
    s_page         = NULL;
    s_mirror_label = NULL;
}

static menu_page_t* settings_creator(void)
{
    const char *btns[] = {
        "Mirror: OFF",  /* 会被下面的初始化覆盖 */
        "Back",
    };
    menu_page_t *pg = menu_ui_page_create("Settings", btns, 2, on_btn);
    if (!pg) return NULL;

    s_page         = pg;
    s_mirror_label = pg->btn_labels[0];

    /* 刷新初始状态 */
    lv_label_set_text(s_mirror_label,
        hal_display_is_mirrored() ? "Mirror: ON" : "Mirror: OFF");

    lv_obj_add_event_cb(pg->container, on_container_delete, LV_EVENT_DELETE, NULL);

    ESP_LOGI(TAG, "page created");
    return pg;
}

/* ================================================================
 *  公开 API
 * ================================================================ */

void settings_init(void)
{
    s_page         = NULL;
    s_mirror_label = NULL;
    menu_ui_register_creator(MENU_ITEM_SETTINGS, settings_creator);
    ESP_LOGI(TAG, "init ok");
}

void settings_tick(uint32_t dt_ms) { (void)dt_ms; }

void settings_process_updates(void) {}
