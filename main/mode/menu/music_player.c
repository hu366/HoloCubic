/**
 * @file    music_player.c
 * @brief   音乐播放器 —— 文件浏览器 + MP3-TF-16P 播放控制
 *
 * music_map.txt 格式（位于 ESP32 SD 卡 /sdcard/music_map.txt）：
 *   F <名称>                    — 文件夹
 *   M <文件夹号> <曲目号> <名称>  — 音乐文件（缩进 2 空格表示层级）
 *   例：
 *     F 周杰伦
 *       M 1 1 晴天.mp3
 *       M 1 2 稻香.mp3
 *     M 0 1 孤勇者.mp3
 *
 * 交互：
 *   列表模式：旋钮选文件/文件夹，短按进入文件夹或播放文件
 *   播放后自动进入播放器视图：显示曲名 + 音量 + 控制按钮
 *   长按退出音乐页面
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "music_player.h"
#include "menu_ui.h"
#include "menu_engine.h"
#include "app_config.h"
#include "hal_audio.h"
#include "hal_sd.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "font_noto_16.h"

static const char *TAG = "music";

/* ---- 文件树节点 ---- */
typedef struct music_node {
    char     name[64];
    bool     is_dir;
    uint8_t  folder_num;
    uint16_t track_num;
    int      child_count;
    struct music_node **children;
    struct music_node *parent;
} music_node_t;

#define STACK_MAX 16

/* ---- 列表 UI ---- */
#define LIST_VP_H      190
#define LIST_VP_W      220
#define LIST_VP_X      ((LCD_HOR_RES - LIST_VP_W) / 2)
#define LIST_VP_Y      20
#define LIST_ITEM_H    22
#define LIST_ITEM_GAP  2
#define LIST_ITEM_STEP (LIST_ITEM_H + LIST_ITEM_GAP)
#define LIST_VISIBLE   8

/* ---- 底部控制栏（番茄钟风格：3 按钮可见，2 页滑动） ---- */
#define BTN_W          64
#define BTN_H          28
#define BTN_VISIBLE    3
#define BTN_TOTAL      5
#define BTN_GAP        6
#define BTN_ROW_W      (BTN_VISIBLE * BTN_W + (BTN_VISIBLE - 1) * BTN_GAP)
#define BTN_START_X    ((LCD_HOR_RES - BTN_ROW_W) / 2)
#define BTN_Y          (LCD_VER_RES - 36)
#define BTN_ANIM_MS    200

/* ================================================================
 *  静态状态
 * ================================================================ */

static menu_page_t *s_page;
static lv_obj_t    *s_path_label;
static lv_obj_t    *s_now_playing;   /* 播放器视图：曲目名称 */
static lv_obj_t    *s_vol_label;     /* 音量显示 */

/* 列表 */
static lv_obj_t    *s_list_vp;
static lv_obj_t    *s_list_scroller;
static lv_obj_t    *s_list_items[LIST_VISIBLE + 2];
static int          s_list_sel;

/* 按钮栏（番茄钟风格：3 可见，6 按钮分 2 页滑动） */
/* Page0: [Prev] [Play] [Next]  = idx 0,1,2 */
/* Page1: [Vol-] [Vol+] [Back]  = idx 3,4,5 */
static lv_obj_t    *s_btns[BTN_VISIBLE];
static lv_obj_t    *s_btn_labels[BTN_VISIBLE];
static int          s_btn_index;      /* 0~5 全局 */
static bool         s_in_player;      /* 播放器/列表 */
static bool         s_audio_playing;  /* 播放/暂停状态 */
static struct {
    bool     on;        int64_t  t0;
    int      dir;       bool     swapped;
    int      old_page;
} s_btn_anim;

/* 音量 */
static int          s_volume;

/* 文件树 */
static music_node_t *s_root;
static music_node_t *s_cur_dir;
static music_node_t *s_playing;

/* 跨核标志 */
static volatile bool s_ui_dirty;
static volatile bool s_list_rebuild;
static volatile bool s_btn_pending;
static volatile bool s_rotary_changed;
static volatile int  s_rotary_dir;

/* 音量弹窗 */
static lv_obj_t    *s_vol_popup;
static lv_obj_t    *s_vol_popup_label;
static bool         s_vol_adjusting;

/* ================================================================
 *  文件树
 * ================================================================ */

static music_node_t* node_new(const char *name, bool is_dir,
                               uint8_t fn, uint16_t tn) {
    music_node_t *n = calloc(1, sizeof(music_node_t));
    if (!n) return NULL;
    strncpy(n->name, name, 63);
    n->is_dir     = is_dir;
    n->folder_num = fn;
    n->track_num  = tn;
    return n;
}

static void node_add(music_node_t *p, music_node_t *c) {
    p->children = realloc(p->children,
                          (p->child_count + 1) * sizeof(music_node_t *));
    p->children[p->child_count++] = c;
    c->parent = p;
}

static bool parse_music_map(void) {
    char path[64];
    snprintf(path, sizeof(path), "%s/music_map.txt", SD_MOUNT_POINT);
    FILE *f = fopen(path, "r");
    if (!f) {
        ESP_LOGW(TAG, "cannot open %s", path);
        return false;
    }

    s_root = node_new("/", true, 0, 0);
    if (!s_root) { fclose(f); return false; }

    music_node_t *stack[STACK_MAX];
    int depth = 0;
    stack[0] = s_root;

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        size_t len = strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r'))
            line[--len] = '\0';
        if (len == 0 || line[0] == '#') continue;

        int indent = 0;
        while (line[indent] == ' ') indent++;
        char *rest = line + indent;

        if (rest[0] == 'F' && rest[1] == ' ') {
            char *nm = rest + 2;
            while (*nm == ' ') nm++;
            if (!*nm) continue;

            depth = indent / 2;
            if (depth >= STACK_MAX) depth = STACK_MAX - 1;

            music_node_t *dir = node_new(nm, true, 0, 0);
            if (dir) {
                node_add(stack[depth], dir);
                stack[depth + 1] = dir;
            }
        } else if (rest[0] == 'M' && rest[1] == ' ') {
            int fn, tn;
            char nm[64];
            if (sscanf(rest + 2, "%d %d %63[^\n]", &fn, &tn, nm) >= 3) {
                depth = indent / 2;
                if (depth >= STACK_MAX) depth = STACK_MAX - 1;

                music_node_t *file = node_new(nm, false, (uint8_t)fn, (uint16_t)tn);
                if (file) node_add(stack[depth], file);
            }
        }
    }

    fclose(f);
    ESP_LOGI(TAG, "music_map loaded, root items=%d", s_root->child_count);
    return (s_root->child_count > 0);
}

/* ================================================================
 *  播放控制
 * ================================================================ */

static void refresh(void) { lv_refr_now(NULL); }

/* 选歌但不播，等着用户按 Play */
static void do_select(music_node_t *node) {
    if (!node || node->is_dir) return;
    s_playing = node;
    s_audio_playing = false;
    if (s_now_playing)
        lv_label_set_text(s_now_playing, node->name);
    ESP_LOGI(TAG, "select: %s (f=%d t=%d)",
             node->name, node->folder_num, node->track_num);
}

/* 开始/继续播放当前选中的曲目 */
static void do_play(void) {
    if (!s_playing) return;
    if (s_playing->folder_num == 0)
        hal_audio_play_track(s_playing->track_num);
    else
        hal_audio_play_folder_track(s_playing->folder_num, (uint8_t)s_playing->track_num);
    s_audio_playing = true;
    ESP_LOGI(TAG, "play start: %s", s_playing->name);
}

static void do_next(void) {
    if (!s_playing) return;
    music_node_t *p = s_playing->parent;
    if (!p) return;
    int pos = -1;
    for (int i = 0; i < p->child_count; i++)
        if (p->children[i] == s_playing) { pos = i; break; }
    for (int i = pos + 1; i < p->child_count; i++)
        if (!p->children[i]->is_dir) { do_select(p->children[i]); do_play(); return; }
}

static void do_prev(void) {
    if (!s_playing) return;
    music_node_t *p = s_playing->parent;
    if (!p) return;
    int pos = -1;
    for (int i = 0; i < p->child_count; i++)
        if (p->children[i] == s_playing) { pos = i; break; }
    for (int i = pos - 1; i >= 0; i--)
        if (!p->children[i]->is_dir) { do_select(p->children[i]); do_play(); return; }
}

/* ================================================================
 *  列表 UI（就地高亮，参照 weather 城市列表模式）
 * ================================================================ */

static void rebuild_list(void) {
    /* 销毁旧项 */
    for (int i = 0; i < LIST_VISIBLE + 2; i++) {
        if (s_list_items[i]) { lv_obj_delete(s_list_items[i]); s_list_items[i] = NULL; }
    }
    if (!s_cur_dir) return;

    /* 列表：..(Up) + 文件/文件夹 + < Back */
    bool has_up = (s_cur_dir != s_root);
    int total   = s_cur_dir->child_count + (has_up ? 1 : 0) + 1;  /* +1 for Back */
    int back_idx = total - 1;

    lv_obj_set_size(s_list_scroller, LIST_VP_W - 4, total * LIST_ITEM_STEP);

    int visible_start = s_list_sel - LIST_VISIBLE / 2;
    if (visible_start < 0) visible_start = 0;
    if (visible_start > total - LIST_VISIBLE) visible_start = total - LIST_VISIBLE;
    if (visible_start < 0) visible_start = 0;

    for (int i = 0; i < total; i++) {
        if (i < visible_start || i >= visible_start + LIST_VISIBLE + 2) continue;

        int slot = i - visible_start;
        lv_obj_t *item = lv_obj_create(s_list_scroller);
        lv_obj_set_size(item, LIST_VP_W - 4, LIST_ITEM_H);
        lv_obj_set_pos(item, 0, i * LIST_ITEM_STEP);
        lv_obj_set_style_border_width(item, 0, 0);
        lv_obj_set_style_radius(item, 4, 0);
        lv_obj_set_style_pad_all(item, 0, 0);
        lv_obj_set_scrollbar_mode(item, LV_SCROLLBAR_MODE_OFF);

        lv_obj_t *label = lv_label_create(item);
        lv_obj_set_style_text_font(label, &font_noto_16, 0);
        lv_obj_set_style_border_width(label, 0, 0);
        lv_obj_set_style_bg_opa(label, LV_OPA_TRANSP, 0);

        char buf[80];
        bool is_dir = false, is_special = false;
        if (has_up && i == 0) {
            snprintf(buf, sizeof(buf), ".. (Up)");
            is_special = true;
        } else if (i == back_idx) {
            snprintf(buf, sizeof(buf), "< Back");
            is_special = true;
        } else {
            int ci = has_up ? (i - 1) : i;
            music_node_t *n = s_cur_dir->children[ci];
            is_dir = n->is_dir;
            snprintf(buf, sizeof(buf), "%s %s",
                     is_dir ? "[D]" : "[*]", n->name);
        }
        lv_label_set_text(label, buf);

        bool sel = (i == s_list_sel);
        lv_obj_set_style_bg_color(item,
            sel ? lv_color_hex(0x2563EB) : lv_color_hex(0x1A1A2E), 0);
        lv_obj_set_style_bg_opa(item, sel ? LV_OPA_COVER : LV_OPA_10, 0);
        lv_obj_set_style_text_color(label,
            sel ? lv_color_white() :
                  (is_special ? lv_color_hex(0xFF6666) :
                   (is_dir ? lv_color_hex(0xFFCC00) : lv_color_hex(0xCCCCCC))), 0);
        lv_obj_center(label);

        s_list_items[slot] = item;
    }

    /* 滚动到合适位置 */
    int scroll_y = visible_start * LIST_ITEM_STEP;
    lv_obj_set_y(s_list_scroller, 2 - scroll_y);
}

/* 旋钮滚动时直接重建列表（简单可靠） */
static void scroll_list(int dir) {
    if (!s_cur_dir) return;
    bool has_up = (s_cur_dir != s_root);
    int total   = s_cur_dir->child_count + (has_up ? 1 : 0) + 1;  /* +1 Back */
    if (total <= 1) return;

    s_list_sel += dir;
    if (s_list_sel < 0) s_list_sel = total - 1;
    if (s_list_sel >= total) s_list_sel = 0;
    rebuild_list();
}

/* ================================================================
 *  路径显示
 * ================================================================ */

static void update_path(void) {
    if (!s_path_label || !s_cur_dir) return;
    char buf[128] = "/music";
    music_node_t *p = s_cur_dir;
    char parts[6][32];
    int cnt = 0;
    while (p && p != s_root && cnt < 6) {
        strncpy(parts[cnt], p->name, 31); parts[cnt][31] = '\0';
        cnt++; p = p->parent;
    }
    for (int i = cnt - 1; i >= 0; i--) {
        snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), "/%s", parts[i]);
    }
    lv_label_set_text(s_path_label, buf);
}

/* ================================================================
 *  按钮标签
 * ================================================================ */

static const char* const BTN_NAMES[] = {
    "Prev", "Play", "Next", "Vol", "Back"
};
#define BTN_PAGE(idx)  ((idx) / BTN_VISIBLE)
#define BTN_LOCAL(idx) ((idx) % BTN_VISIBLE)

static int page_of(int idx) { return idx / BTN_VISIBLE; }

static const char* btn_name_for(int gi) {
    if (gi == 1) return s_audio_playing ? "Paus" : "Play";
    return BTN_NAMES[gi];
}

static void set_btn_texts(int page) {
    for (int i = 0; i < BTN_VISIBLE; i++) {
        int gi = page * BTN_VISIBLE + i;
        if (gi < BTN_TOTAL && s_btn_labels[i])
            lv_label_set_text(s_btn_labels[i], btn_name_for(gi));
    }
}

static void highlight_btns(void) {
    int p = page_of(s_btn_index);
    int li = BTN_LOCAL(s_btn_index);
    for (int i = 0; i < BTN_VISIBLE; i++) {
        if (!s_btns[i]) continue;
        bool sel = s_in_player && (i == li) && (i + p * BTN_VISIBLE == s_btn_index);
        lv_obj_set_style_border_color(s_btns[i],
            sel ? lv_color_hex(0x4FC3F7) : lv_color_hex(0x333355), 0);
        if (s_btn_labels[i])
            lv_obj_set_style_text_color(s_btn_labels[i],
                sel ? lv_color_hex(0x4FC3F7) : lv_color_hex(0xAAAACC), 0);
    }
}

static void activate_btn(void) {
    switch (s_btn_index) {
    case 0: do_prev(); break;                     /* Prev */
    case 1:                                       /* Play/Pause toggle */
        if (!s_playing) break;
        if (s_audio_playing) {
            /* 正在播放 → 暂停 */
            hal_audio_pause();
            s_audio_playing = false;
        } else {
            /* 停止/暂停 → 播放 */
            do_play();
        }
        if (page_of(s_btn_index) == 0 && s_btn_labels[1])
            lv_label_set_text(s_btn_labels[1],
                              s_audio_playing ? "Paus" : "Play");
        break;
    case 2: do_next(); break;                     /* Next */
    case 3:                                       /* Vol → 弹窗旋钮调音量 */
        s_vol_adjusting = true;
        menu_engine_rotary_override(true);
        if (s_vol_popup) lv_obj_remove_flag(s_vol_popup, LV_OBJ_FLAG_HIDDEN);
        break;
    case 4: s_in_player = false; break;           /* Back */
    }
    s_ui_dirty = true;
}

/* ================================================================
 *  输入回调（Core 1）
 * ================================================================ */

static int64_t s_last_input = 0;
#define COOLDOWN_US 200000

static void music_input_cb(imu_tilt_dir_t tilt, int8_t rotary, bool btn_short) {
    (void)tilt;
    if (s_vol_adjusting) {
        if (btn_short) {
            s_vol_adjusting = false;
            menu_engine_rotary_override(false);
            if (s_vol_popup) lv_obj_add_flag(s_vol_popup, LV_OBJ_FLAG_HIDDEN);
            s_ui_dirty = true;
            return;
        }
        if (rotary != 0) {
            int v = (int)s_volume + rotary;
            if (v < 0) v = 0;
            if (v > 30) v = 30;
            s_volume = (uint8_t)v;
            hal_audio_set_volume((uint8_t)s_volume);
            s_ui_dirty = true;
        }
        return;
    }
    if (rotary != 0) {
        int64_t now = esp_timer_get_time();
        if (now - s_last_input < COOLDOWN_US) return;
        s_last_input  = now;
        s_rotary_dir  = rotary;
        s_rotary_changed = true;
    }
    if (btn_short) s_btn_pending = true;
}

/* ================================================================
 *  页面创建器
 * ================================================================ */

static void on_delete(lv_event_t *e) {
    (void)e;
    s_page        = NULL;
    s_path_label  = NULL;
    s_now_playing = NULL;
    s_vol_label   = NULL;
    s_list_vp     = NULL;
    s_list_scroller = NULL;
    s_vol_popup       = NULL;
    s_vol_popup_label  = NULL;
    memset(s_list_items, 0, sizeof(s_list_items));
    memset(s_btns, 0, sizeof(s_btns));
    memset(s_btn_labels, 0, sizeof(s_btn_labels));
    ESP_LOGI(TAG, "page destroyed");
}

static menu_page_t* music_creator(void) {
    menu_page_t *pg = calloc(1, sizeof(menu_page_t));
    if (!pg) return NULL;
    pg->title     = "Music";
    pg->btn_count = 0;

    lv_obj_t *scr = menu_ui_get_screen();
    if (!scr) { free(pg); return NULL; }

    /* 容器 */
    pg->container = lv_obj_create(scr);
    lv_obj_set_size(pg->container, LCD_HOR_RES, LCD_VER_RES);
    lv_obj_set_x(pg->container, LCD_HOR_RES);
    lv_obj_set_style_bg_color(pg->container, lv_color_hex(0x16213e), 0);
    lv_obj_set_style_border_width(pg->container, 0, 0);
    lv_obj_set_style_pad_all(pg->container, 0, 0);
    lv_obj_add_event_cb(pg->container, on_delete, LV_EVENT_DELETE, NULL);

    /* 路径标签 */
    s_path_label = lv_label_create(pg->container);
    lv_obj_set_style_text_font(s_path_label, &font_noto_16, 0);
    lv_obj_set_style_text_color(s_path_label, lv_color_hex(0xAAAAAA), 0);
    lv_obj_set_style_border_width(s_path_label, 0, 0);
    lv_obj_set_style_bg_opa(s_path_label, LV_OPA_TRANSP, 0);
    lv_obj_align(s_path_label, LV_ALIGN_TOP_LEFT, 5, 2);

    /* 正在播放曲目名 */
    s_now_playing = lv_label_create(pg->container);
    lv_obj_set_style_text_font(s_now_playing, &font_noto_16, 0);
    lv_obj_set_style_text_color(s_now_playing, lv_color_hex(0x4FC3F7), 0);
    lv_obj_set_style_border_width(s_now_playing, 0, 0);
    lv_obj_set_style_bg_opa(s_now_playing, LV_OPA_TRANSP, 0);
    lv_obj_align(s_now_playing, LV_ALIGN_CENTER, 0, -20);
    lv_label_set_text(s_now_playing, "No track");
    lv_obj_add_flag(s_now_playing, LV_OBJ_FLAG_HIDDEN);   /* 列表模式不显示 */

    /* 音量标签 */
    s_vol_label = lv_label_create(pg->container);
    lv_obj_set_style_text_font(s_vol_label, &font_noto_16, 0);
    lv_obj_set_style_text_color(s_vol_label, lv_color_hex(0xCCCCCC), 0);
    lv_obj_set_style_border_width(s_vol_label, 0, 0);
    lv_obj_set_style_bg_opa(s_vol_label, LV_OPA_TRANSP, 0);
    lv_obj_align(s_vol_label, LV_ALIGN_CENTER, 0, 5);
    lv_obj_add_flag(s_vol_label, LV_OBJ_FLAG_HIDDEN);      /* 列表模式不显示 */

    /* 列表视口 */
    s_list_vp = lv_obj_create(pg->container);
    lv_obj_set_size(s_list_vp, LIST_VP_W, LIST_VP_H);
    lv_obj_set_pos(s_list_vp, LIST_VP_X, LIST_VP_Y);
    lv_obj_set_style_bg_color(s_list_vp, lv_color_hex(0x0B0B20), 0);
    lv_obj_set_style_border_width(s_list_vp, 1, 0);
    lv_obj_set_style_border_color(s_list_vp, lv_color_hex(0x333355), 0);
    lv_obj_set_style_radius(s_list_vp, 6, 0);
    lv_obj_set_style_pad_all(s_list_vp, 2, 0);
    lv_obj_set_style_clip_corner(s_list_vp, true, 0);
    lv_obj_set_scrollbar_mode(s_list_vp, LV_SCROLLBAR_MODE_OFF);

    /* 滚动层 */
    s_list_scroller = lv_obj_create(s_list_vp);
    lv_obj_set_size(s_list_scroller, LIST_VP_W - 4, LIST_VP_H);
    lv_obj_set_pos(s_list_scroller, 0, 0);
    lv_obj_set_style_bg_opa(s_list_scroller, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_list_scroller, 0, 0);
    lv_obj_set_style_pad_all(s_list_scroller, 0, 0);
    lv_obj_set_scrollbar_mode(s_list_scroller, LV_SCROLLBAR_MODE_OFF);

    /* 底部 3 按钮（番茄钟风格） */
    for (int i = 0; i < BTN_VISIBLE; i++) {
        lv_obj_t *btn = lv_obj_create(pg->container);
        lv_obj_set_size(btn, BTN_W, BTN_H);
        lv_obj_set_pos(btn, BTN_START_X + i * (BTN_W + BTN_GAP), BTN_Y);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x1A1A2E), 0);
        lv_obj_set_style_border_width(btn, 1, 0);
        lv_obj_set_style_border_color(btn, lv_color_hex(0x333355), 0);
        lv_obj_set_style_radius(btn, 5, 0);
        lv_obj_set_scrollbar_mode(btn, LV_SCROLLBAR_MODE_OFF);

        lv_obj_t *lbl = lv_label_create(btn);
        lv_obj_set_style_text_font(lbl, &font_noto_16, 0);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0xAAAACC), 0);
        lv_obj_set_style_border_width(lbl, 0, 0);
        lv_obj_set_style_bg_opa(lbl, LV_OPA_TRANSP, 0);
        lv_label_set_text(lbl, BTN_NAMES[i]);  /* 默认 Page0 */
        lv_obj_center(lbl);

        s_btns[i]       = btn;
        s_btn_labels[i] = lbl;
    }

    /* 音量弹窗（半透明遮罩 + 大字音量） */
    s_vol_popup = lv_obj_create(pg->container);
    lv_obj_set_size(s_vol_popup, 160, 80);
    lv_obj_align(s_vol_popup, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(s_vol_popup, lv_color_hex(0x0F3460), 0);
    lv_obj_set_style_border_width(s_vol_popup, 2, 0);
    lv_obj_set_style_border_color(s_vol_popup, lv_color_hex(0x4FC3F7), 0);
    lv_obj_set_style_radius(s_vol_popup, 10, 0);
    lv_obj_set_scrollbar_mode(s_vol_popup, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(s_vol_popup, LV_OBJ_FLAG_HIDDEN);

    s_vol_popup_label = lv_label_create(s_vol_popup);
    lv_obj_set_style_text_font(s_vol_popup_label, &font_noto_16, 0);
    lv_obj_set_style_text_color(s_vol_popup_label, lv_color_white(), 0);
    lv_obj_center(s_vol_popup_label);

    /* 初始化状态 */
    s_page        = pg;
    s_cur_dir     = s_root;
    s_playing     = NULL;
    s_list_sel    = 0;
    s_btn_index   = 0;
    s_in_player      = false;
    s_audio_playing  = false;
    s_volume         = AUDIO_VOLUME_DEFAULT;
    memset(&s_btn_anim, 0, sizeof(s_btn_anim));

    memset(s_list_items, 0, sizeof(s_list_items));

    update_path();
    set_btn_texts(0);
    rebuild_list();

    /* 根据状态显隐 */
    if (s_in_player) {
        lv_obj_add_flag(s_list_vp, LV_OBJ_FLAG_HIDDEN);
    }
    {
        char vbuf[16];
        snprintf(vbuf, sizeof(vbuf), "Vol: %d", s_volume);
        lv_label_set_text(s_vol_label, vbuf);
    }
    refresh();

    /* 请求替换输入回调（在 process_updates 中执行） */
    s_rotary_changed = false;
    s_btn_pending    = false;

    ESP_LOGI(TAG, "page created");
    return pg;
}

/* ================================================================
 *  公开 API
 * ================================================================ */

void music_player_init(void) {
    s_root        = NULL;
    s_cur_dir     = NULL;
    s_playing     = NULL;
    s_page        = NULL;
    s_volume         = AUDIO_VOLUME_DEFAULT;
    s_in_player      = false;
    s_audio_playing  = false;

    s_ui_dirty        = false;
    s_list_rebuild    = false;
    s_btn_pending     = false;
    s_rotary_changed  = false;

    if (!parse_music_map())
        ESP_LOGW(TAG, "no music_map.txt on SD card");

    menu_ui_register_creator(MENU_ITEM_MUSIC, music_creator);
    ESP_LOGI(TAG, "init ok");
}

void music_player_tick(uint32_t dt_ms) {
    (void)dt_ms;
}

void music_player_process_updates(void) {
    /* 首次进入：替换输入回调 */
    static bool cb_set = false;
    if (!cb_set && s_page) {
        cb_set = true;
        menu_engine_set_sub_callback(music_input_cb);
        s_last_input = 0;
        return;
    }
    if (!s_page) {
        cb_set = false;
        return;
    }

    /* ---- 按钮页滑动动画 ---- */
    if (s_btn_anim.on) {
        int64_t el = (esp_timer_get_time() - s_btn_anim.t0) / 1000;
        if (el >= BTN_ANIM_MS) {
            /* 动画结束：归位 + 显示新页文字 */
            for (int i = 0; i < BTN_VISIBLE; i++) {
                if (s_btns[i])
                    lv_obj_set_x(s_btns[i], BTN_START_X + i * (BTN_W + BTN_GAP));
            }
            int np = page_of(s_btn_index);
            set_btn_texts(np);
            highlight_btns();
            refresh();
            s_btn_anim.on = false;
        } else {
            int half = BTN_ANIM_MS / 2;
            int dir  = s_btn_anim.dir;
            for (int i = 0; i < BTN_VISIBLE; i++) {
                if (!s_btns[i]) continue;
                if (el < half) {
                    /* 前半段：滑出 */
                    int32_t e = menu_ui_ease_out((int32_t)el, half);
                    int32_t base = BTN_START_X + i * (BTN_W + BTN_GAP);
                    lv_obj_set_x(s_btns[i], base + (dir * LCD_HOR_RES * e) / 256);
                } else {
                    /* 后半段：滑入 */
                    if (!s_btn_anim.swapped) {
                        s_btn_anim.swapped = true;
                        int np = page_of(s_btn_index);
                        set_btn_texts(np);
                        highlight_btns();
                    }
                    int32_t e = menu_ui_ease_out((int32_t)(el - half), half);
                    for (int j = 0; j < BTN_VISIBLE; j++) {
                        if (!s_btns[j]) continue;
                        int32_t base = BTN_START_X + j * (BTN_W + BTN_GAP);
                        int32_t x = (dir > 0)
                            ? (LCD_HOR_RES - (LCD_HOR_RES - base) * e / 256)
                            : (-BTN_W + (base + BTN_W) * e / 256);
                        lv_obj_set_x(s_btns[j], x);
                    }
                }
            }
            refresh();
        }
    }

    /* 旋钮事件 */
    if (s_rotary_changed && !s_btn_anim.on) {
        s_rotary_changed = false;

        if (s_in_player) {
            int opg = page_of(s_btn_index);
            s_btn_index += s_rotary_dir;
            if (s_btn_index < 0) s_btn_index = BTN_TOTAL - 1;
            if (s_btn_index >= BTN_TOTAL) s_btn_index = 0;
            int npg = page_of(s_btn_index);

            if (npg != opg) {
                s_btn_anim.on      = true;
                s_btn_anim.t0      = esp_timer_get_time();
                s_btn_anim.dir     = -s_rotary_dir;  /* CW→页右移,旧页左滑 */
                s_btn_anim.swapped = false;
                s_btn_anim.old_page = opg;
            } else {
                highlight_btns();
            }
        } else {
            /* 列表模式：旋钮滚动列表 */
            bool has_up = (s_cur_dir && s_cur_dir != s_root);
            int total = (s_cur_dir ? s_cur_dir->child_count : 0) + (has_up ? 1 : 0) + 1;
            if (total <= 1) {
                s_in_player = true;
                s_btn_index = 0;
            } else {
                scroll_list(s_rotary_dir);
            }
        }
        refresh();
    }

    /* 短按 */
    if (s_btn_pending) {
        s_btn_pending = false;

        if (s_in_player) {
            activate_btn();
        } else {
            /* 列表模式 */
            bool has_up = (s_cur_dir && s_cur_dir != s_root);
            int total = (s_cur_dir ? s_cur_dir->child_count : 0) + (has_up ? 1 : 0) + 1;
            int back_idx = total - 1;

            if (s_list_sel == back_idx) {
                /* < Back → 退出音乐页面 */
                menu_ui_go_back();
            } else if (has_up && s_list_sel == 0) {
                /* ".." → 返回上级 */
                if (s_cur_dir && s_cur_dir->parent) {
                    s_cur_dir  = s_cur_dir->parent;
                    s_list_sel = 0;
                    s_list_rebuild = true;
                }
            } else if (s_cur_dir && total > 0) {
                int ci = has_up ? (s_list_sel - 1) : s_list_sel;
                if (ci >= 0 && ci < s_cur_dir->child_count) {
                    music_node_t *n = s_cur_dir->children[ci];
                    if (n->is_dir) {
                        s_cur_dir  = n;
                        s_list_sel = 0;
                        s_list_rebuild = true;
                    } else {
                        /* 选中文件 → 进入播放器，等用户按 Play */
                        do_select(n);
                        s_in_player = true;
                        s_btn_index = 0;
                    }
                }
            }
        }
        s_ui_dirty = true;
    }

    /* 列表重建 */
    if (s_list_rebuild) {
        s_list_rebuild = false;
        rebuild_list();
    }

    /* 通用 UI 更新 */
    if (s_ui_dirty) {
        s_ui_dirty = false;
        update_path();
        {
            char vbuf[16];
            snprintf(vbuf, sizeof(vbuf), "Vol: %d", s_volume);
            if (s_vol_label) lv_label_set_text(s_vol_label, vbuf);
            if (s_vol_popup_label) {
                snprintf(vbuf, sizeof(vbuf), "Vol: %d", s_volume);
                lv_label_set_text(s_vol_popup_label, vbuf);
            }
        }
        /* 显隐切换 */
        if (s_in_player) {
            lv_obj_add_flag(s_list_vp, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(s_now_playing, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(s_vol_label, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_remove_flag(s_list_vp, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(s_now_playing, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(s_vol_label, LV_OBJ_FLAG_HIDDEN);
        }
        /* Play/Paus 按钮动态文字 */
        if (page_of(s_btn_index) == 0 && s_btn_labels[1])
            lv_label_set_text(s_btn_labels[1],
                              s_audio_playing ? "Paus" : "Play");
        highlight_btns();
        refresh();
    }
}
