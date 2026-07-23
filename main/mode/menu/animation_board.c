/**
 * @file    animation_board.c
 * @brief   动画看板 —— SD 卡视频列表选择 + PSRAM 全量预加载 + 循环播放
 *
 * 交互：旋钮选择视频 → 短按确认 → 加载进度条 → 全屏循环播放
 *       长按编码器 → 切回宠物模式（退出）
 */

#include "animation_board.h"
#include "menu_ui.h"
#include "menu_engine.h"
#include "hal_sd.h"
#include "app_config.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"

static const char *TAG = "anim_board";

#define FRAME_SIZE       (240U * 240U * 2U)
#define MAX_LIST_ITEMS   17          /* 最多 16 个视频 + 1 个 Back */

/* ================================================================
 *  播放器状态
 * ================================================================ */

static anim_board_info_t s_info;
static uint8_t **s_frames     = NULL;
static uint16_t  s_frame_idx  = 0;
static uint32_t  s_frame_timer = 0;
static bool      s_playing    = false;
static bool      s_dirty      = false;
static lv_image_dsc_t s_img_dsc;

/* ================================================================
 *  列表选择状态
 * ================================================================ */

static char      **s_video_names = NULL;
static size_t      s_video_count = 0;
static int         s_sel_idx     = 0;
static int         s_total_items = 0;  /* 视频数 + 1(Back) */
static lv_obj_t   *s_list_items[MAX_LIST_ITEMS];
static lv_obj_t   *s_list_scroller = NULL;
static lv_obj_t   *s_list_viewport = NULL;
static bool        s_in_list       = false;
static bool        s_need_init_list = false;  /* 延迟创建列表 */

/* ================================================================
 *  页面状态
 * ================================================================ */

static lv_obj_t    *s_page_img   = NULL;
static menu_page_t *s_page       = NULL;
static bool         s_page_active = false;
static lv_obj_t    *s_load_bar   = NULL;
static lv_obj_t    *s_load_pct   = NULL;

/* ================================================================
 *  跨核标志（Core1→Core0）
 * ================================================================ */

static volatile bool s_sel_pending = false;
static volatile int  s_sel_dir     = 0;     /* ±1=移动焦点, 0=无操作 */
static volatile bool s_confirm     = false; /* 确认选择 */

/* ================================================================
 *  前向声明
 * ================================================================ */

static bool parse_meta(const char *dir, anim_board_info_t *info);
static void list_create(void);
static void list_destroy(void);
static void list_update_cursor(void);
static void load_and_play(const char *video_name);
static void on_load_progress(uint16_t loaded, uint16_t total);
static void list_input_cb(imu_tilt_dir_t tilt, int8_t rotary, bool btn_short);
static void free_video_list(void);

/* ================================================================
 *  播放器 API
 * ================================================================ */

static bool anim_load_frames(const char *video_name)
{
    anim_board_unload();

    char dir[64];
    snprintf(dir, sizeof(dir), "%s/animations/%s", SD_MOUNT_POINT, video_name);

    if (!parse_meta(dir, &s_info)) {
        ESP_LOGE(TAG, "Failed to parse meta.txt for %s", video_name);
        return false;
    }

    s_frames = heap_caps_calloc(s_info.frame_count, sizeof(uint8_t *),
                                 MALLOC_CAP_SPIRAM);
    if (!s_frames) {
        ESP_LOGE(TAG, "PSRAM calloc %u ptrs failed", s_info.frame_count);
        return false;
    }

    uint16_t loaded = 0;
    for (uint16_t i = 0; i < s_info.frame_count; i++) {
        s_frames[i] = heap_caps_malloc(FRAME_SIZE, MALLOC_CAP_SPIRAM);
        if (!s_frames[i]) {
            ESP_LOGW(TAG, "PSRAM OOM at %u/%u", i, s_info.frame_count);
            s_info.frame_count = i;
            break;
        }

        char path[256];
        snprintf(path, sizeof(path), "%s/%03d.bin", dir, i);
        FILE *f = fopen(path, "rb");
        if (!f) { s_info.frame_count = i; break; }

        size_t rd = fread(s_frames[i], 1, FRAME_SIZE, f);
        fclose(f);
        if (rd != FRAME_SIZE) {
            heap_caps_free(s_frames[i]);
            s_frames[i] = NULL;
            s_info.frame_count = i;
            break;
        }
        loaded++;

        if (loaded % 3 == 0) on_load_progress(loaded, s_info.frame_count);
    }

    if (loaded == 0) { anim_board_unload(); return false; }

    memset(&s_img_dsc, 0, sizeof(s_img_dsc));
    s_img_dsc.header.magic  = LV_IMAGE_HEADER_MAGIC;
    s_img_dsc.header.cf     = LV_COLOR_FORMAT_RGB565;
    s_img_dsc.header.w      = s_info.width;
    s_img_dsc.header.h      = s_info.height;
    s_img_dsc.header.stride = s_info.width * 2;
    s_img_dsc.data          = s_frames[0];
    s_img_dsc.data_size     = FRAME_SIZE;

    s_frame_idx   = 0;
    s_frame_timer = 0;
    s_playing     = true;
    s_dirty       = true;

    ESP_LOGI(TAG, "Loaded %s: %ux%u, %u frames, %u fps (%.1f MB)",
             video_name, s_info.width, s_info.height,
             loaded, s_info.fps,
             (float)(loaded * FRAME_SIZE) / (1024 * 1024));
    return true;
}

bool anim_board_load(const char *name, anim_board_info_t *info,
                     anim_load_progress_cb_t cb)
{
    (void)cb; /* 内部用 on_load_progress */
    return anim_load_frames(name);
}

void anim_board_unload(void)
{
    s_playing = false;
    s_dirty   = false;
    s_img_dsc.data = NULL;

    if (s_frames) {
        for (uint16_t i = 0; i < s_info.frame_count; i++) {
            if (s_frames[i]) heap_caps_free(s_frames[i]);
        }
        heap_caps_free(s_frames);
        s_frames = NULL;
    }
    memset(&s_info, 0, sizeof(s_info));
}

bool anim_board_tick(uint32_t dt_ms)
{
    if (!s_playing) return false;

    uint32_t frame_ms = 1000U / s_info.fps;
    s_frame_timer += dt_ms;

    while (s_frame_timer >= frame_ms) {
        s_frame_timer -= frame_ms;
        s_frame_idx++;
        if (s_frame_idx >= s_info.frame_count) s_frame_idx = 0; /* 循环 */

        s_img_dsc.data = s_frames[s_frame_idx];
        s_dirty = true;
    }

    return true;
}

const lv_image_dsc_t *anim_board_get_frame(void)
{
    return s_playing ? &s_img_dsc : NULL;
}

bool anim_board_is_dirty(void)
{
    bool ret = s_dirty;
    s_dirty = false;
    return ret;
}

bool anim_board_is_playing(void) { return s_playing; }

/* ================================================================
 *  meta.txt 解析
 * ================================================================ */

static bool parse_meta(const char *dir, anim_board_info_t *info)
{
    char path[256];
    snprintf(path, sizeof(path), "%s/meta.txt", dir);

    FILE *f = fopen(path, "r");
    if (!f) { ESP_LOGE(TAG, "Cannot open %s", path); return false; }

    int n = fscanf(f, "%hu %hu %hu %hhu",
                   &info->width, &info->height,
                   &info->frame_count, &info->fps);
    fclose(f);

    if (n != 4) { ESP_LOGE(TAG, "meta.txt parse error: %d fields", n); return false; }

    const char *slash = strrchr(dir, '/');
    if (slash) strncpy(info->name, slash + 1, sizeof(info->name) - 1);

    return true;
}

/* ================================================================
 *  列表 UI
 * ================================================================ */

static void list_input_cb(imu_tilt_dir_t tilt, int8_t rotary, bool btn_short)
{
    (void)tilt;
    if (!s_in_list) return;

    if (btn_short) { s_confirm = true; return; }
    if (rotary != 0) {
        s_sel_dir = (rotary > 0) ? 1 : -1;
        ESP_LOGI(TAG, "rotary %d", rotary);
    }
}

static volatile bool s_back_to_list = false;

static void play_input_cb(imu_tilt_dir_t tilt, int8_t rotary, bool btn_short)
{
    (void)tilt;
    (void)rotary;
    if (btn_short) s_back_to_list = true;
}

static void return_to_list(void)
{
    /* 卸载当前视频 */
    anim_board_unload();
    s_page_active = false;
    if (s_page_img) { lv_obj_delete(s_page_img); s_page_img = NULL; }

    /* 清理旧的列表相关对象 */
    list_destroy();
    free_video_list();

    /* 重新扫描并显示列表 */
    list_create();
    if (s_video_count == 1) {
        list_destroy();
        load_and_play(s_video_names[0]);
        free_video_list();
    }
    lv_refr_now(NULL);
}

static void list_create(void)
{
    /* 扫描 SD 卡 */
    free_video_list();
    char **entries = NULL;
    size_t count = 0;
    char anim_path[64];
    snprintf(anim_path, sizeof(anim_path), "%s/animations", SD_MOUNT_POINT);

    if (!hal_sd_list_dir(anim_path, &entries, &count) || count == 0) {
        ESP_LOGW(TAG, "No videos found in %s", anim_path);
        return;
    }

    /* 筛选目录（以 '/' 结尾） */
    for (size_t i = 0; i < count; i++) {
        size_t len = strlen(entries[i]);
        if (len > 1 && entries[i][len - 1] == '/') {
            entries[i][len - 1] = '\0'; /* 去掉末尾 / */
            s_video_names = realloc(s_video_names,
                                     (s_video_count + 1) * sizeof(char *));
            s_video_names[s_video_count] = strdup(entries[i]);
            s_video_count++;
            if (s_video_count >= MAX_LIST_ITEMS) break;
        }
    }
    hal_sd_free_dir_list(entries, count);

    if (s_video_count == 0) {
        ESP_LOGW(TAG, "No video directories found");
        return;
    }

    s_in_list = true;
    s_sel_idx = 0;

    #define LIST_VP_W  (LCD_HOR_RES - 40)
    #define LIST_ITEM_STEP  40
    #define LIST_VISIBLE    5
    #define LIST_VP_HEIGHT  (LIST_VISIBLE * LIST_ITEM_STEP)
    s_total_items = (int)s_video_count + 1;  /* +1 for Back */
    #define LIST_SCROLLER_H (s_total_items * LIST_ITEM_STEP)
    s_list_viewport = lv_obj_create(s_page->container);
    lv_obj_set_size(s_list_viewport, LIST_VP_W, LIST_VP_HEIGHT);
    lv_obj_align(s_list_viewport, LV_ALIGN_TOP_MID, 0, 28);
    lv_obj_set_style_bg_opa(s_list_viewport, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_list_viewport, 0, 0);
    lv_obj_set_style_pad_all(s_list_viewport, 0, 0);
    lv_obj_set_style_clip_corner(s_list_viewport, true, 0);

    /* 列表容器（视口内，通过移动 Y 实现滚动） */
    s_list_scroller = lv_obj_create(s_list_viewport);
    lv_obj_set_size(s_list_scroller, LIST_VP_W, LIST_SCROLLER_H);
    lv_obj_set_style_bg_opa(s_list_scroller, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_list_scroller, 0, 0);
    lv_obj_set_style_pad_all(s_list_scroller, 0, 0);

    /* 列表项 */
    for (size_t i = 0; i < s_video_count; i++) {
        lv_obj_t *item = lv_obj_create(s_list_scroller);
        lv_obj_set_size(item, LIST_VP_W, LIST_ITEM_STEP);
        lv_obj_align(item, LV_ALIGN_TOP_MID, 0, (int32_t)(i * LIST_ITEM_STEP));
        lv_obj_set_style_bg_opa(item, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(item, 0, 0);
        lv_obj_set_style_pad_all(item, 0, 0);
        lv_obj_set_style_radius(item, 4, 0);

        lv_obj_t *label = lv_label_create(item);
        lv_label_set_text(label, s_video_names[i]);
        lv_obj_set_style_text_color(label, lv_color_hex(0xCCCCCC), 0);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
        lv_obj_align(label, LV_ALIGN_LEFT_MID, 8, 0);

        s_list_items[i] = item;
    }

    /* Back 项（灰色分隔线效果） */
    {
        int back_i = (int)s_video_count;
        lv_obj_t *item = lv_obj_create(s_list_scroller);
        lv_obj_set_size(item, LIST_VP_W, LIST_ITEM_STEP);
        lv_obj_align(item, LV_ALIGN_TOP_MID, 0, back_i * LIST_ITEM_STEP);
        lv_obj_set_style_bg_opa(item, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(item, 0, 0);
        lv_obj_set_style_pad_all(item, 0, 0);
        lv_obj_set_style_radius(item, 4, 0);

        lv_obj_t *label = lv_label_create(item);
        lv_label_set_text(label, "Back");
        lv_obj_set_style_text_color(label, lv_color_hex(0x888888), 0);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
        lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);

        s_list_items[back_i] = item;
    }

    /* 初始高亮 */
    list_update_cursor();

    /* 注册输入回调 */
    menu_engine_set_sub_callback(list_input_cb);

    ESP_LOGI(TAG, "List: %u videos", (unsigned)s_video_count);
}

static void list_destroy(void)
{
    s_in_list = false;
    if (s_list_viewport) { lv_obj_delete(s_list_viewport); s_list_viewport = NULL; }
    s_list_scroller = NULL;
    for (int i = 0; i < MAX_LIST_ITEMS; i++) s_list_items[i] = NULL;
}

static void list_update_cursor(void)
{
    if (!s_list_scroller) return;

    /* 文字颜色 + 背景高亮切换 */
    for (int i = 0; i < s_total_items; i++) {
        if (!s_list_items[i]) continue;
        lv_obj_t *lbl = lv_obj_get_child(s_list_items[i], 0);
        if (lbl) {
            lv_obj_set_style_text_color(lbl,
                ((int)i == s_sel_idx) ? lv_color_hex(0x4FC3F7) : lv_color_hex(0xCCCCCC), 0);
        }
        lv_obj_set_style_bg_opa(s_list_items[i],
            ((int)i == s_sel_idx) ? LV_OPA_20 : LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_color(s_list_items[i], lv_color_hex(0x4FC3F7), 0);
    }

    /* 移动列表容器使选中项居中 */
    int ideal = -(s_sel_idx - LIST_VISIBLE / 2) * LIST_ITEM_STEP;
    int max_y = 0;
    int min_y = -(s_total_items - LIST_VISIBLE) * LIST_ITEM_STEP;
    if (ideal > max_y) ideal = max_y;
    if (ideal < min_y) ideal = min_y;
    lv_obj_set_y(s_list_scroller, ideal);

    lv_refr_now(NULL);
}

static void free_video_list(void)
{
    if (s_video_names) {
        for (size_t i = 0; i < s_video_count; i++) free(s_video_names[i]);
        free(s_video_names);
        s_video_names = NULL;
    }
    s_video_count = 0;
}

/* ================================================================
 *  加载进度
 * ================================================================ */

static void on_load_progress(uint16_t loaded, uint16_t total)
{
    if (s_load_bar && total > 0) {
        int pct = (int)((uint32_t)loaded * 100 / total);
        lv_bar_set_value(s_load_bar, pct, LV_ANIM_OFF);
    }
    if (s_load_pct) {
        lv_label_set_text_fmt(s_load_pct, "%u/%u", loaded, total);
    }
    lv_refr_now(NULL);
}

/* ================================================================
 *  加载并播放
 * ================================================================ */

static void load_and_play(const char *video_name)
{
    /* 进度条 */
    s_load_bar = lv_bar_create(s_page->container);
    lv_obj_set_size(s_load_bar, 180, 12);
    lv_obj_align(s_load_bar, LV_ALIGN_CENTER, 0, 20);
    lv_obj_set_style_bg_color(s_load_bar, lv_color_hex(0x333333), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_load_bar, lv_color_hex(0x4FC3F7), LV_PART_INDICATOR);
    lv_bar_set_range(s_load_bar, 0, 100);
    lv_bar_set_value(s_load_bar, 0, LV_ANIM_OFF);

    s_load_pct = lv_label_create(s_page->container);
    lv_obj_align(s_load_pct, LV_ALIGN_BOTTOM_MID, 0, -60);
    lv_obj_set_style_text_color(s_load_pct, lv_color_hex(0x888888), 0);
    lv_label_set_text(s_load_pct, "0/0");
    lv_refr_now(NULL);

    /* 加载帧 */
    s_page_active = anim_load_frames(video_name);

    /* 移除进度 */
    if (s_load_bar) { lv_obj_delete(s_load_bar); s_load_bar = NULL; }
    if (s_load_pct) { lv_obj_delete(s_load_pct); s_load_pct = NULL; }

    if (s_page_active) {
        s_page_img = lv_image_create(s_page->container);
        lv_image_set_src(s_page_img, anim_board_get_frame());
        lv_obj_set_size(s_page_img, LCD_HOR_RES, LCD_VER_RES);
        lv_obj_center(s_page_img);
        lv_refr_now(NULL);

        /* 注册短按回调：返回列表 */
        s_back_to_list = false;
        menu_engine_set_sub_callback(play_input_cb);
    }
}

/* ================================================================
 *  页面生命周期
 * ================================================================ */

static void on_container_delete(lv_event_t *e)
{
    (void)e;
    if (s_page_active) {
        anim_board_unload();
        s_page_active = false;
        s_page_img = NULL;
    }
    s_page = NULL;
    list_destroy();
    free_video_list();
}

static menu_page_t* anim_creator(void)
{
    menu_page_t *pg = calloc(1, sizeof(menu_page_t));
    if (!pg) return NULL;
    pg->title = "Animation";
    pg->btn_count     = 0;
    pg->btn_containers = NULL;
    pg->btn_labels     = NULL;
    pg->on_btn         = NULL;
    pg->focus_index    = 0;

    lv_obj_t *scr = menu_ui_get_screen();
    if (!scr) { free(pg); return NULL; }

    /* 容器（加载期间可见） */
    pg->container = lv_obj_create(scr);
    lv_obj_set_size(pg->container, LCD_HOR_RES, LCD_VER_RES);
    lv_obj_set_x(pg->container, 0);
    lv_obj_set_style_bg_color(pg->container, lv_color_black(), 0);
    lv_obj_set_style_border_width(pg->container, 0, 0);
    lv_obj_set_style_pad_all(pg->container, 0, 0);
    lv_obj_add_event_cb(pg->container, on_container_delete, LV_EVENT_DELETE, NULL);

    s_page = pg;  /* 提前赋值，list_create 里用 s_page->container */

    /* 标题 */
    pg->title_label = lv_label_create(pg->container);
    lv_label_set_text(pg->title_label, "Select Video");
    lv_obj_set_style_text_font(pg->title_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(pg->title_label, lv_color_white(), 0);
    lv_obj_set_style_border_width(pg->title_label, 0, 0);
    lv_obj_set_style_bg_opa(pg->title_label, LV_OPA_TRANSP, 0);
    lv_obj_align(pg->title_label, LV_ALIGN_TOP_MID, 0, 4);

    /* 延迟创建列表（等页面滑入动画完成） */
    s_need_init_list = true;
    lv_refr_now(NULL);

    /* 移到屏幕外，让滑入动画运作 */
    lv_obj_set_x(pg->container, LCD_HOR_RES);

    return pg;
}

static void init_list_deferred(void)
{
    if (!s_need_init_list) return;
    s_need_init_list = false;

    /* 容器移回屏幕 */
    if (s_page && s_page->container) {
        lv_obj_set_x(s_page->container, 0);
    }

    list_create();

    if (s_video_count == 0) {
        lv_obj_t *err = lv_label_create(s_page->container);
        lv_label_set_text(err, "No videos found\n"
                           "Put folders in\n/sdcard/animations/");
        lv_obj_set_style_text_color(err, lv_color_hex(0x888888), 0);
        lv_obj_set_style_text_font(err, &lv_font_montserrat_14, 0);
        lv_obj_center(err);
    } else if (s_video_count == 1) {
        list_destroy();
        load_and_play(s_video_names[0]);
        free_video_list();
    }
    lv_refr_now(NULL);
}

/* ================================================================
 *  菜单集成
 * ================================================================ */

void animation_board_init(void)
{
    menu_ui_register_creator(MENU_ITEM_ANIMATION, anim_creator);
    printf("[ANIM] registered\n");
}

void animation_board_process_updates(void)
{
    /* ---- 延迟初始化列表（页面滑入动画完成后） ---- */
    if (s_need_init_list) {
        ESP_LOGI(TAG, "deferred init...");
        init_list_deferred();
        return;
    }

    /* ---- 列表导航（Core0） ---- */
    if (s_in_list) {
        if (s_sel_dir != 0) {
            int dir = s_sel_dir;
            s_sel_dir = 0;
            int old = s_sel_idx;
            s_sel_idx = (s_sel_idx + dir + s_total_items) % s_total_items;
            if (s_sel_idx != old) list_update_cursor();
        }
        if (s_confirm) {
            s_confirm = false;
            if (s_sel_idx == s_total_items - 1) {
                /* Back */
                list_destroy();
                free_video_list();
                menu_ui_go_back();
            } else if (s_sel_idx >= 0 && (size_t)s_sel_idx < s_video_count) {
                char *name = strdup(s_video_names[s_sel_idx]);
                list_destroy();
                free_video_list();
                load_and_play(name);
                free(name);
            }
        }
        return;
    }

    /* ---- 播放中短按 → 返回列表 ---- */
    if (s_page_active && s_back_to_list) {
        s_back_to_list = false;
        return_to_list();
        return;
    }

    /* ---- 播放 ---- */
    if (!s_page_active || !s_page_img) return;

    static int64_t last_us = 0;
    int64_t now_us = esp_timer_get_time();
    if (last_us == 0) last_us = now_us;
    uint32_t dt_ms = (uint32_t)((now_us - last_us) / 1000);
    last_us = now_us;
    if (dt_ms == 0) return;

    if (anim_board_tick(dt_ms)) {
        lv_image_set_src(s_page_img, anim_board_get_frame());
        lv_refr_now(NULL);
    }
}
