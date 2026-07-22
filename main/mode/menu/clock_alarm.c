/**
 * @file    clock_alarm.c
 * @brief   Task 24: 时钟 & 闹钟 —— LVGL 展示 + 弹窗式闹钟向导
 *
 * 交互（底部按钮，倾斜/旋钮切换）：
 *   "Add" ↔ "Edit" ↔ "Del" ↔ "Back"
 *
 * 闹钟添加/编辑为弹窗多步向导（hour → minute → repeat → confirm）。
 * 长按退回/取消弹窗。
 *
 * 跨核安全：
 *   Core 1 → clock_input_cb → 只改数据/设脏标
 *   Core 0 → clock_alarm_process_updates → 全部 LVGL 操作
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include "clock_alarm.h"
#include "menu_ui.h"
#include "menu_engine.h"
#include "app_config.h"
#include "esp_timer.h"

/* ================================================================
 *  常量
 * ================================================================ */

#define BTN_COOLDOWN_US  300000
#define BTN_ANIM_MS      200
#define BTN_W            180
#define BTN_CX           ((LCD_HOR_RES - BTN_W) / 2)

static const int BTN_COUNT = 4;

#define ALARM_LIST_MAX 5
#define RING_TIMEOUT_S  30

/* 弹窗尺寸 */
#define POPUP_W         180
#define POPUP_H         170
#define POPUP_X         ((LCD_HOR_RES - POPUP_W) / 2)
#define POPUP_Y         ((LCD_VER_RES - POPUP_H) / 2)

/* ================================================================
 *  向导步骤
 * ================================================================ */

typedef enum {
    WIZ_NONE = 0,
    WIZ_HOUR,         /* 调节小时 */
    WIZ_MINUTE,       /* 调节分钟 */
    WIZ_REPEAT,       /* 选择重复模式（预设） */
    WIZ_CONFIRM,      /* 确认保存 */
} wizard_step_t;

/* 重复预设 */
typedef enum {
    REPEAT_EVERYDAY = 0,
    REPEAT_WEEKDAYS,
    REPEAT_WEEKEND,
    REPEAT_ONCE,
    REPEAT_COUNT
} repeat_preset_t;

static const char* repeat_preset_name(repeat_preset_t p) {
    static const char *names[] = {"Every day", "Weekdays", "Weekend", "Once"};
    return (p < REPEAT_COUNT) ? names[p] : "???";
}

static void repeat_apply_preset(bool repeat[7], repeat_preset_t p) {
    switch (p) {
    case REPEAT_EVERYDAY:
        for (int i = 0; i < 7; i++) repeat[i] = true;
        break;
    case REPEAT_WEEKDAYS:
        for (int i = 0; i < 5; i++) repeat[i] = true;
        repeat[5] = false; repeat[6] = false;
        break;
    case REPEAT_WEEKEND:
        for (int i = 0; i < 5; i++) repeat[i] = false;
        repeat[5] = true; repeat[6] = true;
        break;
    case REPEAT_ONCE:
        for (int i = 0; i < 7; i++) repeat[i] = false;
        break;
    default: break;
    }
}

/* ================================================================
 *  静态状态
 * ================================================================ */

static alarm_data_t    s_alarms;
static bool            s_entry_valid[MAX_ALARMS];

static menu_page_t    *s_page;
static lv_obj_t       *s_time_label;
static lv_obj_t       *s_date_label;
static lv_obj_t       *s_btn_label;
static lv_obj_t       *s_btn_bg;
static lv_obj_t       *s_alarm_list[ALARM_LIST_MAX];
static lv_obj_t       *s_alarm_list_bg[ALARM_LIST_MAX];

static int             s_btn_index;
static uint32_t        s_tick_acc_ms;

/* 向导数据 */
static wizard_step_t   s_wizard;
static uint8_t         s_wiz_hour;
static uint8_t         s_wiz_minute;
static bool            s_wiz_repeat[7];
static uint8_t         s_wiz_edit_id;
static int             s_wiz_preset;      /* 重复预设索引 */
static bool            s_wiz_confirm_save; /* CONFIRM：true=Save false=Cancel */

/* 弹窗对象（无需 7 个日期标签了） */
static lv_obj_t *s_popup_overlay;
static lv_obj_t *s_popup_box;
static lv_obj_t *s_popup_title;
static lv_obj_t *s_popup_value;
static lv_obj_t *s_popup_hint;
static lv_obj_t *s_popup_days[7];   /* 7 个日期字符标签（REPEAT 步骤） */
static lv_obj_t *s_popup_btn_bg;
static lv_obj_t *s_popup_btn_lbl;

/* 删除/编辑列表 */
static bool            s_del_mode;
static int             s_del_index;
static bool            s_edit_list_mode;
static int             s_edit_list_index;
static int             s_edit_list_ids[MAX_ALARMS];
static int             s_edit_list_count;

/* 闹钟触发队列 */
static int             s_trigger_queue[MAX_ALARMS];
static int             s_trigger_head;
static int             s_trigger_tail;
static bool            s_ringing;
static uint32_t        s_ring_start_s;
static struct tm       s_last_tm;

/* 跨任务标志 */
static volatile bool   s_ui_dirty;
static volatile bool   s_btn_dirty;
static volatile bool   s_btn_pending;
static volatile int    s_btn_dir;
static volatile bool   s_wiz_dirty;
static volatile bool   s_wiz_done;
static volatile bool   s_wiz_cancel;    /* 弹窗取消 */
static volatile bool   s_cb_override;
static volatile bool   s_time_dirty;

/* 按钮动画 */
static struct { bool on; int64_t t0; int dir; bool swapped; } s_btn_anim;

/* ================================================================
 *  内部辅助
 * ================================================================ */

static void refresh(void) { lv_refr_now(NULL); }

static const char* weekday_name(int wday) {
    static const char *n[] = {"Mon","Tue","Wed","Thu","Fri","Sat","Sun"};
    return (wday >= 0 && wday < 7) ? n[wday] : "???";
}

static int tm_weekday_to_idx(int tm_wday) {
    return (tm_wday == 0) ? 6 : (tm_wday - 1);
}

static void label_style(lv_obj_t *l) {
    lv_obj_set_style_border_width(l, 0, 0);
    lv_obj_set_style_bg_opa(l, LV_OPA_TRANSP, 0);
}

/* ================================================================
 *  闹钟 CRUD（纯数据操作）
 * ================================================================ */

static int find_valid_slot(void) {
    for (int i = 0; i < MAX_ALARMS; i++)
        if (!s_entry_valid[i]) return i;
    return -1;
}

static int count_valid(void) {
    int n = 0;
    for (int i = 0; i < MAX_ALARMS; i++)
        if (s_entry_valid[i]) n++;
    return n;
}

bool clock_alarm_add(uint8_t hour, uint8_t minute, const bool repeat[7]) {
    if (hour > 23 || minute > 59) return false;
    int slot = find_valid_slot();
    if (slot < 0) return false;
    s_entry_valid[slot] = true;
    s_alarms.entries[slot].id     = (uint8_t)slot;
    s_alarms.entries[slot].hour   = hour;
    s_alarms.entries[slot].minute = minute;
    s_alarms.entries[slot].enabled = true;
    memcpy(s_alarms.entries[slot].repeat, repeat, 7 * sizeof(bool));
    s_alarms.count = (uint8_t)count_valid();
    return true;
}

bool clock_alarm_edit(uint8_t id, uint8_t hour, uint8_t minute, const bool repeat[7]) {
    if (id >= MAX_ALARMS || !s_entry_valid[id]) return false;
    if (hour > 23 || minute > 59) return false;
    s_alarms.entries[id].hour   = hour;
    s_alarms.entries[id].minute = minute;
    memcpy(s_alarms.entries[id].repeat, repeat, 7 * sizeof(bool));
    return true;
}

bool clock_alarm_delete(uint8_t id) {
    if (id >= MAX_ALARMS || !s_entry_valid[id]) return false;
    s_entry_valid[id] = false;
    memset(&s_alarms.entries[id], 0, sizeof(alarm_entry_t));
    s_alarms.count = (uint8_t)count_valid();
    return true;
}

bool clock_alarm_set_enabled(uint8_t id, bool enabled) {
    if (id >= MAX_ALARMS || !s_entry_valid[id]) return false;
    s_alarms.entries[id].enabled = enabled;
    return true;
}

const alarm_entry_t* clock_alarm_get_entry(uint8_t id) {
    if (id >= MAX_ALARMS || !s_entry_valid[id]) return NULL;
    return &s_alarms.entries[id];
}

uint8_t clock_alarm_get_count(void) {
    return (uint8_t)count_valid();
}

const alarm_data_t* clock_alarm_get_all(void) {
    return &s_alarms;
}

void clock_alarm_set_data(const alarm_data_t *data) {
    if (!data) return;
    memcpy(&s_alarms, data, sizeof(alarm_data_t));
    memset(s_entry_valid, 0, sizeof(s_entry_valid));
    for (int i = 0; i < MAX_ALARMS && i < data->count; i++) {
        if (data->entries[i].hour <= 23 && data->entries[i].minute <= 59)
            s_entry_valid[i] = true;
    }
}

/* ================================================================
 *  触发检查
 * ================================================================ */

int clock_alarm_check_trigger(uint8_t hour, uint8_t minute, uint8_t weekday) {
    if (s_trigger_head != s_trigger_tail) {
        int id = s_trigger_queue[s_trigger_head];
        s_trigger_head = (s_trigger_head + 1) % MAX_ALARMS;
        return id;
    }
    for (int i = 0; i < MAX_ALARMS; i++) {
        if (!s_entry_valid[i]) continue;
        alarm_entry_t *e = &s_alarms.entries[i];
        if (!e->enabled) continue;
        if (e->hour != hour || e->minute != minute) continue;
        if (!e->repeat[weekday]) continue;
        int next_tail = (s_trigger_tail + 1) % MAX_ALARMS;
        if (next_tail == s_trigger_head) continue;
        s_trigger_queue[s_trigger_tail] = i;
        s_trigger_tail = next_tail;
    }
    if (s_trigger_head != s_trigger_tail) {
        int id = s_trigger_queue[s_trigger_head];
        s_trigger_head = (s_trigger_head + 1) % MAX_ALARMS;
        return id;
    }
    return -1;
}

/* ---- 前向声明 ---- */
static void update_time_display(void);
static void hide_alarm_list(void);

/* ================================================================
 *  弹窗
 * ================================================================ */

static void popup_destroy(void) {
    if (s_popup_overlay) {
        lv_obj_delete(s_popup_overlay);
        s_popup_overlay = NULL;
    }
    s_popup_box   = NULL;
    s_popup_title  = NULL;
    s_popup_value  = NULL;
    s_popup_hint   = NULL;
    s_popup_btn_bg = NULL;
    s_popup_btn_lbl = NULL;
    memset(s_popup_days, 0, sizeof(s_popup_days));
}

static void popup_create(void) {
    if (!s_page || !s_page->container) return;

    /* 半透明遮罩 */
    s_popup_overlay = lv_obj_create(s_page->container);
    lv_obj_set_size(s_popup_overlay, LCD_HOR_RES, LCD_VER_RES);
    lv_obj_set_pos(s_popup_overlay, 0, 0);
    lv_obj_set_style_bg_color(s_popup_overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_popup_overlay, LV_OPA_50, 0);
    lv_obj_set_style_border_width(s_popup_overlay, 0, 0);
    lv_obj_set_style_pad_all(s_popup_overlay, 0, 0);
    lv_obj_add_flag(s_popup_overlay, LV_OBJ_FLAG_CLICKABLE); /* 阻挡点击穿透 */

    /* 弹窗盒子 */
    s_popup_box = lv_obj_create(s_popup_overlay);
    lv_obj_set_size(s_popup_box, POPUP_W, POPUP_H);
    lv_obj_set_pos(s_popup_box, POPUP_X, POPUP_Y);
    lv_obj_set_style_bg_color(s_popup_box, lv_color_hex(0x1a1a2e), 0);
    lv_obj_set_style_border_color(s_popup_box, lv_color_hex(0x4FC3F7), 0);
    lv_obj_set_style_border_width(s_popup_box, 2, 0);
    lv_obj_set_style_border_opa(s_popup_box, LV_OPA_60, 0);
    lv_obj_set_style_radius(s_popup_box, 12, 0);
    lv_obj_set_style_pad_all(s_popup_box, 0, 0);
    lv_obj_remove_flag(s_popup_box, LV_OBJ_FLAG_SCROLLABLE);

    /* 标题 */
    s_popup_title = lv_label_create(s_popup_box);
    lv_obj_set_style_text_font(s_popup_title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_popup_title, lv_color_hex(0x4FC3F7), 0);
    label_style(s_popup_title);
    lv_obj_align(s_popup_title, LV_ALIGN_TOP_MID, 0, 10);

    /* 大数值 */
    s_popup_value = lv_label_create(s_popup_box);
    lv_obj_set_style_text_font(s_popup_value, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_popup_value, lv_color_hex(0xFFB74D), 0);
    label_style(s_popup_value);
    lv_obj_align(s_popup_value, LV_ALIGN_CENTER, 0, -5);

    /* 提示文字 */
    s_popup_hint = lv_label_create(s_popup_box);
    lv_obj_set_style_text_font(s_popup_hint, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_popup_hint, lv_color_hex(0x666666), 0);
    label_style(s_popup_hint);
    lv_obj_align(s_popup_hint, LV_ALIGN_CENTER, 0, 25);

    /* 底部按钮 */
    s_popup_btn_bg = lv_obj_create(s_popup_box);
    lv_obj_set_size(s_popup_btn_bg, 120, 34);
    lv_obj_set_style_bg_color(s_popup_btn_bg, lv_color_hex(0x0f3460), 0);
    lv_obj_set_style_border_width(s_popup_btn_bg, 0, 0);
    lv_obj_set_style_radius(s_popup_btn_bg, 8, 0);
    lv_obj_align(s_popup_btn_bg, LV_ALIGN_BOTTOM_MID, 0, -12);
    lv_obj_remove_flag(s_popup_btn_bg, LV_OBJ_FLAG_SCROLLABLE);

    s_popup_btn_lbl = lv_label_create(s_popup_btn_bg);
    lv_obj_set_style_text_font(s_popup_btn_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_popup_btn_lbl, lv_color_hex(0x4FC3F7), 0);
    label_style(s_popup_btn_lbl);
    lv_obj_center(s_popup_btn_lbl);

    /* REPEAT 步骤：7 个日期标签 */
    for (int i = 0; i < 7; i++) {
        s_popup_days[i] = lv_label_create(s_popup_box);
        lv_obj_set_style_text_font(s_popup_days[i], &lv_font_montserrat_14, 0);
        label_style(s_popup_days[i]);
        lv_obj_add_flag(s_popup_days[i], LV_OBJ_FLAG_HIDDEN);
    }
}

static void popup_refresh(void) {
    if (!s_popup_box) return;

    switch (s_wizard) {
    case WIZ_HOUR: {
        lv_label_set_text(s_popup_title, "Set Hour");
        char v[8]; snprintf(v, sizeof(v), "%02d", s_wiz_hour);
        lv_label_set_text(s_popup_value, v);
        lv_label_set_text(s_popup_hint, "Rotate to change");
        lv_label_set_text(s_popup_btn_lbl, "Next");
        lv_obj_remove_flag(s_popup_value, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_popup_hint, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_popup_btn_bg, LV_OBJ_FLAG_HIDDEN);
        for (int i = 0; i < 7; i++)
            lv_obj_add_flag(s_popup_days[i], LV_OBJ_FLAG_HIDDEN);
        break;
    }
    case WIZ_MINUTE: {
        lv_label_set_text(s_popup_title, "Set Minute");
        char v[8]; snprintf(v, sizeof(v), "%02d", s_wiz_minute);
        lv_label_set_text(s_popup_value, v);
        lv_label_set_text(s_popup_hint, "Rotate to change");
        lv_label_set_text(s_popup_btn_lbl, "Next");
        lv_obj_remove_flag(s_popup_value, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_popup_hint, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_popup_btn_bg, LV_OBJ_FLAG_HIDDEN);
        for (int i = 0; i < 7; i++)
            lv_obj_add_flag(s_popup_days[i], LV_OBJ_FLAG_HIDDEN);
        break;
    }
    case WIZ_REPEAT: {
        lv_label_set_text(s_popup_title, "Repeat");
        /* 预设名称作为大数值显示 */
        lv_label_set_text(s_popup_value, repeat_preset_name((repeat_preset_t)s_wiz_preset));
        lv_obj_remove_flag(s_popup_value, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(s_popup_hint, "Rotate to choose");
        lv_label_set_text(s_popup_btn_lbl, "Next");
        lv_obj_remove_flag(s_popup_btn_bg, LV_OBJ_FLAG_HIDDEN);
        for (int i = 0; i < 7; i++)
            lv_obj_add_flag(s_popup_days[i], LV_OBJ_FLAG_HIDDEN);
        break;
    }
    case WIZ_CONFIRM: {
        lv_label_set_text(s_popup_title, "Save Alarm?");
        char v[32];
        char rbuf[9] = "";
        for (int i = 0; i < 7; i++)
            rbuf[i] = s_wiz_repeat[i] ? "MTWTFSS"[i] : '.';
        rbuf[7] = '\0';
        snprintf(v, sizeof(v), "%02d:%02d  %s", s_wiz_hour, s_wiz_minute, rbuf);
        lv_label_set_text(s_popup_value, v);
        lv_obj_remove_flag(s_popup_value, LV_OBJ_FLAG_HIDDEN);
        /* 按钮：旋钮切换 Save ↔ Cancel */
        lv_label_set_text(s_popup_btn_lbl, s_wiz_confirm_save ? "Save" : "Cancel");
        lv_label_set_text(s_popup_hint, "Rot:switch  Press:ok");
        lv_obj_remove_flag(s_popup_btn_bg, LV_OBJ_FLAG_HIDDEN);
        for (int i = 0; i < 7; i++)
            lv_obj_add_flag(s_popup_days[i], LV_OBJ_FLAG_HIDDEN);
        break;
    }
    default: break;
    }
    refresh();
}

/* ================================================================
 *  向导控制
 * ================================================================ */

static void wizard_start(bool is_edit) {
    s_wizard = WIZ_HOUR;
    if (is_edit) {
        s_wiz_edit_id = (uint8_t)s_edit_list_ids[s_edit_list_index];
        const alarm_entry_t *e = &s_alarms.entries[s_wiz_edit_id];
        s_wiz_hour   = e->hour;
        s_wiz_minute = e->minute;
        memcpy(s_wiz_repeat, e->repeat, 7 * sizeof(bool));
    } else {
        s_wiz_edit_id = 255;
        s_wiz_hour    = 7;
        s_wiz_minute  = 0;
        memset(s_wiz_repeat, false, sizeof(s_wiz_repeat));
    }
    /* 根据现有 repeat 匹配最接近的预设 */
    s_wiz_preset = 0;
    if (memcmp(s_wiz_repeat, (bool[7]){true,true,true,true,true,true,true}, 7) == 0)
        s_wiz_preset = REPEAT_EVERYDAY;
    else if (s_wiz_repeat[0] && s_wiz_repeat[4] && !s_wiz_repeat[5] && !s_wiz_repeat[6])
        s_wiz_preset = REPEAT_WEEKDAYS;
    else if (!s_wiz_repeat[0] && !s_wiz_repeat[4] && s_wiz_repeat[5] && s_wiz_repeat[6])
        s_wiz_preset = REPEAT_WEEKEND;
    else
        s_wiz_preset = REPEAT_ONCE;

    s_wiz_confirm_save  = true;
    menu_engine_rotary_override(true);

    /* 创建弹窗 */
    popup_create();
    popup_refresh();
}

static void wizard_save(void) {
    bool ok;
    if (s_wiz_edit_id == 255)
        ok = clock_alarm_add(s_wiz_hour, s_wiz_minute, s_wiz_repeat);
    else
        ok = clock_alarm_edit(s_wiz_edit_id, s_wiz_hour, s_wiz_minute, s_wiz_repeat);
    printf("[CLOCK] alarm %s id=%d %02d:%02d\n",
           ok ? "saved" : "FAILED",
           (s_wiz_edit_id == 255) ? count_valid() - 1 : s_wiz_edit_id,
           s_wiz_hour, s_wiz_minute);
    popup_destroy();
    s_wizard     = WIZ_NONE;
    s_del_mode   = false;
    s_edit_list_mode = false;
    s_wiz_done   = true;
    menu_engine_rotary_override(false);
    hide_alarm_list();
}

static void wizard_cancel(void) {
    printf("[CLOCK] wizard cancelled\n");
    popup_destroy();
    s_wizard     = WIZ_NONE;
    s_del_mode   = false;
    s_edit_list_mode = false;
    s_wiz_done   = true;
    menu_engine_rotary_override(false);
    hide_alarm_list();
}

static void wizard_next(void) {
    switch (s_wizard) {
    case WIZ_HOUR:     s_wizard = WIZ_MINUTE;  break;
    case WIZ_MINUTE:   s_wizard = WIZ_REPEAT;  break;
    case WIZ_REPEAT:
        /* 应用预设到 repeat 数组 */
        repeat_apply_preset(s_wiz_repeat, (repeat_preset_t)s_wiz_preset);
        s_wizard = WIZ_CONFIRM;
        break;
    case WIZ_CONFIRM:  wizard_save(); return;
    default: return;
    }
    popup_refresh();
    refresh();
}

/* ================================================================
 *  闹钟列表展示（删除/编辑模式共用）
 * ================================================================ */

static void show_alarm_list(const int *ids, int count, int focus, bool show_cancel) {
    int total = show_cancel ? count + 1 : count;
    if (s_time_label) lv_obj_add_flag(s_time_label, LV_OBJ_FLAG_HIDDEN);
    if (s_date_label) lv_obj_add_flag(s_date_label, LV_OBJ_FLAG_HIDDEN);

    for (int i = 0; i < ALARM_LIST_MAX; i++) {
        if (!s_alarm_list[i]) continue;
        if (i < total) {
            char buf[40];
            if (show_cancel && i == count) {
                snprintf(buf, sizeof(buf), "-- Cancel --");
                lv_obj_set_style_bg_color(s_alarm_list_bg[i],
                    (i == focus) ? lv_color_hex(0x4a2020) : lv_color_hex(0x2a1a1a), 0);
            } else {
                const alarm_entry_t *e = &s_alarms.entries[ids[i]];
                char rbuf[8] = "";
                for (int d = 0; d < 7; d++)
                    rbuf[d] = e->repeat[d] ? "MTWTFSS"[d] : '.';
                rbuf[7] = '\0';
                snprintf(buf, sizeof(buf), "%02d:%02d %s %s",
                         e->hour, e->minute,
                         e->enabled ? "ON " : "OFF", rbuf);
                lv_obj_set_style_bg_color(s_alarm_list_bg[i],
                    (i == focus) ? lv_color_hex(0x1a5276) : lv_color_hex(0x0f3460), 0);
            }
            lv_label_set_text(s_alarm_list[i], buf);
            lv_obj_remove_flag(s_alarm_list_bg[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_alarm_list_bg[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void hide_alarm_list(void) {
    if (s_time_label) lv_obj_remove_flag(s_time_label, LV_OBJ_FLAG_HIDDEN);
    if (s_date_label) lv_obj_remove_flag(s_date_label, LV_OBJ_FLAG_HIDDEN);
    for (int i = 0; i < ALARM_LIST_MAX; i++) {
        if (s_alarm_list_bg[i])
            lv_obj_add_flag(s_alarm_list_bg[i], LV_OBJ_FLAG_HIDDEN);
    }
    update_time_display();
}

static void update_alarm_list_display(void) {
    if (s_del_mode) {
        int ids[MAX_ALARMS]; int n = 0;
        for (int i = 0; i < MAX_ALARMS; i++)
            if (s_entry_valid[i]) ids[n++] = i;
        show_alarm_list(ids, n, s_del_index, true);
    } else if (s_edit_list_mode) {
        show_alarm_list(s_edit_list_ids, s_edit_list_count, s_edit_list_index, true);
    }
}

/* ================================================================
 *  按钮显示名称
 * ================================================================ */

static const char* btn_name(int idx) {
    switch (idx) {
    case 0: return "Add";
    case 1: return "Edit";
    case 2: return "Del";
    case 3: return "Back";
    default: return "???";
    }
}

/* ================================================================
 *  UI 更新
 * ================================================================ */

static void update_time_display(void) {
    if (!s_time_label || !s_date_label) return;
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    char tbuf[16];
    snprintf(tbuf, sizeof(tbuf), "%02d:%02d:%02d",
             tm.tm_hour, tm.tm_min, tm.tm_sec);
    lv_label_set_text(s_time_label, tbuf);
    char dbuf[32];
    snprintf(dbuf, sizeof(dbuf), "%s  %04d-%02d-%02d",
             weekday_name(tm_weekday_to_idx(tm.tm_wday)),
             tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
    lv_label_set_text(s_date_label, dbuf);
}

static void update_btn_label(void) {
    if (!s_btn_label) return;
    lv_label_set_text(s_btn_label, btn_name(s_btn_index));
}

static void update_all_ui(void) {
    update_time_display();
    update_btn_label();
    if (s_del_mode || s_edit_list_mode) update_alarm_list_display();
    refresh();
}

/* ================================================================
 *  按钮激活（Core 0 调用）
 * ================================================================ */

static void activate_btn(void) {
    /* ===== 列表模式：按 encoder = 确认列表当前选中的动作 ===== */
    if (s_del_mode) {
        int n = count_valid();
        if (n == 0 || s_del_index == n) {
            /* 选中 Cancel → 退出列表 */
            s_del_mode = false; s_ui_dirty = true;
            menu_engine_rotary_override(false);
            hide_alarm_list();
        } else {
            int ids[MAX_ALARMS]; int ni = 0;
            for (int i = 0; i < MAX_ALARMS; i++)
                if (s_entry_valid[i]) ids[ni++] = i;
            clock_alarm_delete((uint8_t)ids[s_del_index]);
            printf("[CLOCK] deleted alarm id=%d\n", ids[s_del_index]);
            s_del_mode = false; s_ui_dirty = true;
            menu_engine_rotary_override(false);
            hide_alarm_list();
        }
        return;
    }
    if (s_edit_list_mode) {
        if (s_edit_list_count == 0 || s_edit_list_index == s_edit_list_count) {
            /* 选中 Cancel → 退出列表 */
            s_edit_list_mode = false; s_ui_dirty = true;
            menu_engine_rotary_override(false);
            hide_alarm_list();
        } else {
            wizard_start(true);
            s_edit_list_mode = false; s_del_mode = false;
        }
        return;
    }

    /* ===== 正常模式 ===== */
    switch (s_btn_index) {
    case 0: /* Add */
        wizard_start(false);
        break;
    case 1: { /* Edit */
        s_edit_list_count = 0;
        for (int i = 0; i < MAX_ALARMS; i++)
            if (s_entry_valid[i])
                s_edit_list_ids[s_edit_list_count++] = i;
        if (s_edit_list_count == 0) {
            printf("[CLOCK] no alarms to edit\n");
            break;
        }
        s_edit_list_mode  = true;
        s_edit_list_index = 0;
        s_del_mode   = false;
        s_wizard     = WIZ_NONE;
        menu_engine_rotary_override(true);
        break;
    }
    case 2: { /* Del */
        int n = count_valid();
        if (n == 0) { printf("[CLOCK] no alarms to delete\n"); break; }
        s_del_mode   = true;
        s_del_index  = 0;
        s_edit_list_mode = false;
        menu_engine_rotary_override(true);
        break;
    }
    case 3: menu_ui_go_back(); return;
    }
    s_ui_dirty = true;
}

/* ================================================================
 *  输入回调（Core 1，严禁操作 LVGL）
 * ================================================================ */

static int64_t s_last_input_us = 0;

static void clock_input_cb(imu_tilt_dir_t tilt, int8_t rotary, bool btn_short) {
    /* 响铃中：任意按键停止 */
    if (s_ringing) {
        if (btn_short) { s_ringing = false; s_ui_dirty = true;
            printf("[CLOCK] alarm silenced\n"); }
        return;
    }

    /* ---- 弹窗向导模式 ---- */
    if (s_wizard != WIZ_NONE && s_popup_overlay) {
        /* 短按：Next / Save */
        if (btn_short) {
            s_btn_pending = true;
            return;
        }
        /* 旋钮改值 */
        if (rotary != 0) {
            switch (s_wizard) {
            case WIZ_HOUR:
                s_wiz_hour = (s_wiz_hour + rotary + 24) % 24;
                break;
            case WIZ_MINUTE:
                s_wiz_minute = (s_wiz_minute + rotary + 60) % 60;
                break;
            case WIZ_REPEAT:
                s_wiz_preset = (s_wiz_preset + rotary + REPEAT_COUNT) % REPEAT_COUNT;
                break;
            case WIZ_CONFIRM:
                s_wiz_confirm_save = !s_wiz_confirm_save;
                break;
            default: break;
            }
            s_wiz_dirty = true;
        }
        return;
    }

    /* ---- REPEAT 步骤下短按 toggle 当前日（在 btn_pending 中处理） ---- */
    /* （已统一走 s_btn_pending 路径，activate_btn → wizard_next） */

    /* ---- 删除/编辑列表模式：旋钮改焦点，短按走 s_btn_pending → activate_btn ---- */
    if (s_del_mode) {
        if (btn_short) { s_btn_pending = true; return; }
        if (rotary != 0) {
            int n = count_valid();
            int total = n + 1;
            if (total > 0) { s_del_index = (s_del_index + rotary + total) % total; s_ui_dirty = true; }
        }
        return;
    }

    if (s_edit_list_mode) {
        if (btn_short) { s_btn_pending = true; return; }
        if (rotary != 0) {
            int total = s_edit_list_count + 1;
            if (total > 0) {
                s_edit_list_index = (s_edit_list_index + rotary + total) % total;
                s_ui_dirty = true;
            }
        }
        return;
    }

    /* ---- 正常模式 ---- */
    if (btn_short) { s_btn_pending = true; return; }

    int64_t now = esp_timer_get_time();
    if (now - s_last_input_us < BTN_COOLDOWN_US) return;
    s_last_input_us = now;

    if (tilt != IMU_TILT_FRONT && tilt != IMU_TILT_BACK && rotary == 0) return;

    int dir = (rotary > 0 || tilt == IMU_TILT_FRONT) ? 1 : -1;
    int old = s_btn_index;
    if (dir > 0)
        s_btn_index = (s_btn_index + 1) % BTN_COUNT;
    else
        s_btn_index = (s_btn_index == 0) ? (BTN_COUNT - 1) : (s_btn_index - 1);

    if (s_btn_index != old) {
        s_btn_dirty = true; s_btn_dir = dir;
        printf("[CLOCK] btn: %s\n", btn_name(s_btn_index));
    }
}

/* ================================================================
 *  页面创建器
 * ================================================================ */

static void on_container_delete(lv_event_t *e) {
    (void)e;
    popup_destroy();
    s_page         = NULL;
    s_time_label   = NULL;
    s_date_label   = NULL;
    s_btn_label    = NULL;
    s_btn_bg       = NULL;
    memset(s_alarm_list, 0, sizeof(s_alarm_list));
    memset(s_alarm_list_bg, 0, sizeof(s_alarm_list_bg));
    s_del_mode      = false;
    s_edit_list_mode = false;
    s_ringing       = false;
    menu_engine_rotary_override(false);
    printf("[CLOCK] page destroyed\n");
}

static menu_page_t* clock_creator(void) {
    menu_page_t *pg = calloc(1, sizeof(menu_page_t));
    if (!pg) return NULL;
    pg->title = "Clock"; pg->btn_count = 0;

    lv_obj_t *scr = menu_ui_get_screen();
    if (!scr) { free(pg); return NULL; }

    pg->container = lv_obj_create(scr);
    lv_obj_set_size(pg->container, LCD_HOR_RES, LCD_VER_RES);
    lv_obj_set_x(pg->container, LCD_HOR_RES);
    lv_obj_set_style_bg_color(pg->container, lv_color_hex(0x16213e), 0);
    lv_obj_set_style_border_width(pg->container, 0, 0);
    lv_obj_set_style_pad_all(pg->container, 0, 0);
    lv_obj_add_event_cb(pg->container, on_container_delete, LV_EVENT_DELETE, NULL);

    /* 标题 */
    pg->title_label = lv_label_create(pg->container);
    lv_label_set_text(pg->title_label, "Clock");
    lv_obj_set_style_text_font(pg->title_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(pg->title_label, lv_color_white(), 0);
    label_style(pg->title_label);
    lv_obj_align(pg->title_label, LV_ALIGN_TOP_MID, 0, 4);

    /* 时间 */
    s_time_label = lv_label_create(pg->container);
    lv_obj_set_style_text_font(s_time_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_time_label, lv_color_white(), 0);
    label_style(s_time_label);
    lv_obj_align(s_time_label, LV_ALIGN_CENTER, 0, -25);

    /* 日期 */
    s_date_label = lv_label_create(pg->container);
    lv_obj_set_style_text_font(s_date_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_date_label, lv_color_hex(0x888888), 0);
    label_style(s_date_label);
    lv_obj_align(s_date_label, LV_ALIGN_CENTER, 0, 10);

    /* 闹钟列表（最多 5 条） */
    for (int i = 0; i < ALARM_LIST_MAX; i++) {
        s_alarm_list_bg[i] = lv_obj_create(pg->container);
        lv_obj_set_size(s_alarm_list_bg[i], 200, 30);
        lv_obj_set_style_bg_color(s_alarm_list_bg[i], lv_color_hex(0x0f3460), 0);
        lv_obj_set_style_border_width(s_alarm_list_bg[i], 0, 0);
        lv_obj_set_style_radius(s_alarm_list_bg[i], 6, 0);
        lv_obj_align(s_alarm_list_bg[i], LV_ALIGN_CENTER, 0, -30 + i * 36);
        lv_obj_add_flag(s_alarm_list_bg[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_alarm_list_bg[i], LV_OBJ_FLAG_SCROLLABLE);

        s_alarm_list[i] = lv_label_create(s_alarm_list_bg[i]);
        lv_obj_set_style_text_font(s_alarm_list[i], &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(s_alarm_list[i], lv_color_white(), 0);
        label_style(s_alarm_list[i]);
        lv_obj_center(s_alarm_list[i]);
    }

    /* 底部按钮 */
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
    label_style(s_btn_label);
    lv_obj_center(s_btn_label);

    /* 状态初始化 */
    s_page       = pg;
    s_btn_index  = 0;
    s_wizard     = WIZ_NONE;
    s_del_mode   = false;
    s_edit_list_mode = false;
    s_cb_override    = true;
    memset(&s_btn_anim, 0, sizeof(s_btn_anim));
    popup_destroy();

    update_time_display();
    update_btn_label();
    refresh();
    printf("[CLOCK] page created\n");
    return pg;
}

/* ================================================================
 *  公开 API
 * ================================================================ */

void clock_alarm_init(void) {
    memset(&s_alarms, 0, sizeof(s_alarms));
    memset(s_entry_valid, 0, sizeof(s_entry_valid));
    s_alarms.count = 0;

    s_page         = NULL;
    s_time_label   = NULL;
    s_date_label   = NULL;
    s_btn_label    = NULL;
    s_btn_bg       = NULL;
    memset(s_alarm_list, 0, sizeof(s_alarm_list));
    memset(s_alarm_list_bg, 0, sizeof(s_alarm_list_bg));

    s_btn_index     = 0;
    s_wizard        = WIZ_NONE;
    s_del_mode      = false;
    s_edit_list_mode = false;
    s_tick_acc_ms   = 0;
    s_wiz_edit_id   = 255;
    s_ringing       = false;
    s_ring_start_s  = 0;
    s_trigger_head  = 0;
    s_trigger_tail  = 0;
    memset(&s_last_tm, 0, sizeof(s_last_tm));

    s_ui_dirty      = false;
    s_btn_dirty     = false;
    s_btn_pending   = false;
    s_wiz_dirty     = false;
    s_wiz_done      = false;
    s_wiz_cancel    = false;
    s_cb_override   = false;
    s_time_dirty    = false;

    popup_destroy();
    menu_ui_register_creator(MENU_ITEM_CLOCK, clock_creator);
    printf("[CLOCK] init ok\n");
}

void clock_alarm_tick(uint32_t dt_ms) {
    s_tick_acc_ms += dt_ms;
    if (s_tick_acc_ms >= 1000) {
        s_tick_acc_ms -= 1000;
        s_time_dirty = true;

        time_t now = time(NULL);
        struct tm tm;
        localtime_r(&now, &tm);

        if (tm.tm_hour != s_last_tm.tm_hour ||
            tm.tm_min  != s_last_tm.tm_min  ||
            tm.tm_sec  != s_last_tm.tm_sec) {
            s_last_tm = tm;
            int wday  = tm_weekday_to_idx(tm.tm_wday);
            int trig  = clock_alarm_check_trigger(
                (uint8_t)tm.tm_hour, (uint8_t)tm.tm_min, (uint8_t)wday);
            if (trig >= 0) {
                s_ringing      = true;
                s_ring_start_s = (uint32_t)now;
                printf("[CLOCK] ALARM! id=%d %02d:%02d\n",
                       trig, tm.tm_hour, tm.tm_min);
            }
        }

        if (s_ringing) {
            uint32_t elapsed = (uint32_t)now - s_ring_start_s;
            if (elapsed >= RING_TIMEOUT_S) {
                s_ringing = false;
                printf("[CLOCK] alarm auto-stopped (timeout)\n");
            }
        }
    }
}

void clock_alarm_process_updates(void) {
    /* 按钮动画 */
    if (s_btn_anim.on) {
        int64_t elapsed = (esp_timer_get_time() - s_btn_anim.t0) / 1000;
        if (elapsed >= BTN_ANIM_MS) {
            lv_obj_set_x(s_btn_bg, BTN_CX);
            update_btn_label(); refresh();
            s_btn_anim.on = false;
        } else {
            int half = BTN_ANIM_MS / 2, dir = s_btn_anim.dir;
            int32_t x;
            if (elapsed < half) {
                int32_t e = menu_ui_ease_out((int32_t)elapsed, half);
                x = BTN_CX - (dir * LCD_HOR_RES * e) / 256;
            } else {
                if (!s_btn_anim.swapped) { s_btn_anim.swapped = true; update_btn_label(); }
                int32_t e = menu_ui_ease_out((int32_t)(elapsed - half), half);
                x = (dir > 0) ? (LCD_HOR_RES - (LCD_HOR_RES - BTN_CX) * e / 256)
                              : (-BTN_W + (BTN_CX + BTN_W) * e / 256);
            }
            lv_obj_set_x(s_btn_bg, x); refresh();
        }
    }

    /* 替换 sub 回调 */
    if (s_cb_override && s_page) {
        s_cb_override  = false;
        s_last_input_us = 0;
        menu_engine_set_sub_callback(clock_input_cb);
    }

    /* 按钮切换动画 */
    if (s_btn_dirty && !s_btn_anim.on) {
        s_btn_dirty = false;
        if (s_page && s_btn_bg) {
            s_btn_anim.on = true; s_btn_anim.t0 = esp_timer_get_time();
            s_btn_anim.dir = s_btn_dir; s_btn_anim.swapped = false;
        }
    }

    /* 按钮按下 */
    if (s_btn_pending) {
        s_btn_pending = false;
        /* 弹窗里：Next/Save + REPEAT toggle */
        if (s_wizard != WIZ_NONE && s_popup_overlay) {
            if (s_wizard == WIZ_CONFIRM && !s_wiz_confirm_save) {
                wizard_cancel();
            } else {
                wizard_next();
            }
        } else {
            activate_btn();
        }
    }

    /* 向导值变化 → 刷新弹窗 */
    if (s_wiz_dirty) {
        s_wiz_dirty = false;
        popup_refresh();
    }

    /* 向导确认/取消完成 */
    if (s_wiz_done) {
        s_wiz_done = false;
        menu_ui_restore_default_input();
        menu_engine_rotary_override(false);
        s_cb_override = true;
        s_del_mode    = false;
        s_edit_list_mode = false;
        update_all_ui();
    }

    /* 弹窗取消 */
    if (s_wiz_cancel) {
        s_wiz_cancel = false;
        wizard_cancel();
    }

    /* 通用 UI 刷新 */
    if (s_ui_dirty) {
        s_ui_dirty = false;
        if (s_page && s_page->container) update_all_ui();
    }

    /* 每秒更新时间 */
    if (s_time_dirty) {
        s_time_dirty = false;
        if (s_page && s_page->container && s_wizard == WIZ_NONE)
            update_all_ui();
    }
}
