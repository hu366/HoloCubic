/**
 * @file    pomodoro.c
 * @brief   Task 16: 番茄钟 —— 倒计时 + LVGL 环形进度条 + 单按钮轮播 + 旋钮调节
 *
 * 交互（底部只有一个按钮，倾斜前后切换）：
 *   "Start" ↔ "Set Work" ↔ "Set Break" ↔ "Back"  （倾斜切换）
 *   短按触发当前按钮功能。
 *
 *   "Set Work" / "Set Break" 进入调节模式 → 旋钮调节分钟数 → 短按确认。
 *
 * 跨核安全：
 *   Core 1 → pomo_input_cb → 只改数据/设脏标
 *   Core 0 → pomodoro_process_updates → 全部 LVGL 操作
 *   提示音暂用 printf 占位。
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "pomodoro.h"
#include "menu_ui.h"
#include "menu_engine.h"
#include "app_config.h"
#include "app_types.h"
#include "esp_timer.h"

/* ================================================================
 *  静态状态
 * ================================================================ */

static pomodoro_data_t s_pomo;
static menu_page_t    *s_page;
static lv_obj_t       *s_arc;
static lv_obj_t       *s_time_label;
static lv_obj_t       *s_phase_label;
static lv_obj_t       *s_btn_label;    /* 唯一的底部按钮标签 */
static lv_obj_t       *s_btn_bg;       /* 按钮背景 */
static int             s_btn_index;     /* 0=Start 1=Set Work 2=Set Break 3=Back */
static bool            s_adjusting;
static int             s_adjust_for;    /* 1=Work 2=Break */
static int             s_adjust_val;
static uint32_t        s_tick_acc_ms;

/* 跨任务标志 */
static volatile uint32_t s_pending_ticks;
static volatile bool     s_adjust_dirty;
static volatile bool     s_adjust_done;
static volatile bool     s_ui_dirty;
static volatile bool     s_btn_dirty;     /* 按钮索引变化 */
static volatile bool     s_btn_pending;   /* 按钮按下待处理 */
static volatile bool     s_cb_override;   /* 需要替换 sub 回调 */
static volatile int      s_btn_dir;       /* 倾斜方向：1=前倾 -1=后倾 */

/* 按钮切换冷却 */
#define BTN_COOLDOWN_US 300000

/* 按钮动画 */
#define BTN_ANIM_MS 200
#define BTN_W 180
#define BTN_CX ((LCD_HOR_RES - BTN_W) / 2)

static struct {
    bool    on;
    int64_t t0;
    int     dir;       /* 1=右滑(前倾) -1=左滑(后倾) */
    bool    swapped;   /* 中途已换文字 */
} s_btn_anim;

/* ================================================================
 *  内部辅助
 * ================================================================ */

static void refresh(void) { lv_refr_now(NULL); }

static uint32_t phase_total_seconds(void) {
    uint8_t min = s_pomo.is_work_phase ? s_pomo.work_minutes : s_pomo.break_minutes;
    return (uint32_t)min * 60;
}

static uint32_t progress_angle(void) {
    uint32_t total = phase_total_seconds();
    if (total == 0) return 0;
    return 360 - (uint32_t)((uint64_t)s_pomo.remaining_seconds * 360 / total);
}

static void audio_beep(const char *reason) {
    printf("[POMODORO] beep! (%s)\n", reason);
}

/* ================================================================
 *  获取按钮显示文字
 * ================================================================ */

static const char* btn_display_name(int idx) {
    switch (idx) {
    case 0: /* Start / Pause / Resume */
        if (s_adjusting)    return "Start";
        if (s_pomo.running) return "Pause";
        if (s_pomo.remaining_seconds != phase_total_seconds() &&
            s_pomo.remaining_seconds > 0)
            return "Resume";
        return "Start";
    case 1: return "Set Work";
    case 2: return "Set Break";
    case 3: return "Back";
    default: return "???";
    }
}

/* ================================================================
 *  UI 更新（仅在 Core 0 调用）
 * ================================================================ */

static void pomo_update_time_label(void) {
    if (!s_time_label) return;
    uint32_t sec = s_pomo.remaining_seconds;
    char buf[16];
    snprintf(buf, sizeof(buf), "%02lu:%02lu",
             (unsigned long)(sec / 60), (unsigned long)(sec % 60));
    lv_label_set_text(s_time_label, buf);
}

static void pomo_update_phase_label(void) {
    if (!s_phase_label) return;
    lv_label_set_text(s_phase_label,
                      s_pomo.is_work_phase ? "Work" : "Break");
    lv_obj_set_style_text_color(s_phase_label,
        s_pomo.is_work_phase ? lv_color_hex(0x4FC3F7)
                             : lv_color_hex(0x66BB6A), 0);
}

static void pomo_update_arc(void) {
    if (!s_arc) return;
    lv_arc_set_value(s_arc, (int32_t)progress_angle());
    lv_obj_set_style_arc_color(s_arc,
        s_pomo.is_work_phase ? lv_color_hex(0x4FC3F7)
                             : lv_color_hex(0x66BB6A),
        LV_PART_INDICATOR);
}

static void pomo_update_btn_label(void) {
    if (!s_btn_label) return;
    lv_label_set_text(s_btn_label, btn_display_name(s_btn_index));
}

static void pomo_update_ui(void) {
    pomo_update_time_label();
    pomo_update_phase_label();
    pomo_update_arc();
    pomo_update_btn_label();
    refresh();
}

/* ================================================================
 *  调节模式标题
 * ================================================================ */

static void pomo_set_adjust_title(void) {
    if (!s_page || !s_page->title_label) return;
    char buf[24];
    snprintf(buf, sizeof(buf), "%s: %d min",
             (s_adjust_for == 1) ? "Work" : "Break", s_adjust_val);
    lv_label_set_text(s_page->title_label, buf);
}

static void pomo_restore_title(void) {
    if (!s_page || !s_page->title_label) return;
    lv_label_set_text(s_page->title_label, "Pomodoro");
}

/* ================================================================
 *  倒计时逻辑（纯数据，不碰 LVGL）
 * ================================================================ */

static void pomo_do_tick_data(void) {
    if (!s_pomo.running) return;
    if (s_pomo.remaining_seconds == 0) return;

    s_pomo.remaining_seconds--;

    if (s_pomo.remaining_seconds == 0) {
        const char *from = s_pomo.is_work_phase ? "Work" : "Break";
        s_pomo.is_work_phase = !s_pomo.is_work_phase;
        s_pomo.remaining_seconds = phase_total_seconds();
        const char *to = s_pomo.is_work_phase ? "Work" : "Break";
        printf("[POMODORO] phase switch: %s -> %s\n", from, to);
        audio_beep("phase_switch");
    }
}

/* ================================================================
 *  按钮激活（Core 0 调用）
 * ================================================================ */

static void pomo_activate_btn(void) {
    switch (s_btn_index) {
    case 0: /* Start / Pause / Resume */
        if (s_adjusting) break;
        if (s_pomo.running) {
            s_pomo.running = false;
            printf("[POMODORO] paused\n");
        } else {
            if (s_pomo.remaining_seconds == 0)
                s_pomo.remaining_seconds = phase_total_seconds();
            s_pomo.running = true;
            printf("[POMODORO] started (%s, %lu sec)\n",
                   s_pomo.is_work_phase ? "Work" : "Break",
                   (unsigned long)s_pomo.remaining_seconds);
        }
        break;

    case 1: /* Set Work */
    case 2: /* Set Break */
        if (s_adjusting) break;
        s_adjusting  = true;
        s_adjust_for = s_btn_index;
        s_adjust_val = (s_btn_index == 1) ? s_pomo.work_minutes
                                           : s_pomo.break_minutes;
        menu_engine_rotary_override(true); /* 调节模式强制放行旋钮 */
        pomo_set_adjust_title();
        printf("[POMODORO] adjusting %s (current=%d)\n",
               (s_btn_index == 1) ? "Work" : "Break", s_adjust_val);
        break;

    case 3: /* Back */
        menu_ui_go_back();
        return;
    }

    s_tick_acc_ms = 0;
    s_ui_dirty = true;
}

/* ================================================================
 *  输入回调（Core 1，只改数据/设脏标，严禁操作 LVGL）
 * ================================================================ */

static int64_t s_last_tilt_us = 0;

static void pomo_input_cb(imu_tilt_dir_t tilt, int8_t rotary, bool btn_short) {
    /* ---- 调节模式：旋钮改值，短按确认 ---- */
    if (s_adjusting) {
        if (btn_short) {
            if (s_adjust_for == 1)
                s_pomo.work_minutes = (uint8_t)s_adjust_val;
            else
                s_pomo.break_minutes = (uint8_t)s_adjust_val;

            if (!s_pomo.running) {
                if ((s_adjust_for == 1 && s_pomo.is_work_phase) ||
                    (s_adjust_for == 2 && !s_pomo.is_work_phase)) {
                    s_pomo.remaining_seconds = phase_total_seconds();
                }
            }

            s_adjusting   = false;
            s_adjust_done = true;
            s_ui_dirty    = true;
            menu_engine_rotary_override(false);
            printf("[POMODORO] %s set to %d min\n",
                   (s_adjust_for == 1) ? "Work" : "Break", s_adjust_val);
            return;
        }
        if (rotary != 0) {
            s_adjust_val += rotary;
            if (s_adjust_val < 1)  s_adjust_val = 1;
            if (s_adjust_val > 99) s_adjust_val = 99;
            s_adjust_dirty = true;
            printf("[POMODORO] adjust %s: %d min\n",
                   (s_adjust_for == 1) ? "Work" : "Break", s_adjust_val);
        }
        return;
    }

    /* ---- 正常模式：短按触发按钮 ---- */
    if (btn_short) {
        s_btn_pending = true;
        return;
    }

    /* ---- 正常模式：旋钮切换按钮 ---- */
    if (rotary != 0) {
        int64_t now = esp_timer_get_time();
        if (now - s_last_tilt_us < BTN_COOLDOWN_US) return;
        s_last_tilt_us = now;

        int old = s_btn_index;
        if (rotary > 0) {
            s_btn_index = (s_btn_index + 1) % 4;
            s_btn_dir   = 1;
        } else {
            s_btn_index = (s_btn_index == 0) ? 3 : (s_btn_index - 1);
            s_btn_dir   = -1;
        }
        if (s_btn_index != old) {
            s_btn_dirty = true;
            printf("[POMODORO] btn: %s\n", btn_display_name(s_btn_index));
        }
        return;
    }

    /* ---- 正常模式：倾斜切换按钮 ---- */
    if (tilt != IMU_TILT_FRONT && tilt != IMU_TILT_BACK) return;

    int64_t now = esp_timer_get_time();
    if (now - s_last_tilt_us < BTN_COOLDOWN_US) return;
    s_last_tilt_us = now;

    int old = s_btn_index;
    if (tilt == IMU_TILT_FRONT) {
        s_btn_index = (s_btn_index + 1) % 4;
        s_btn_dir   = 1;
    } else {
        s_btn_index = (s_btn_index == 0) ? 3 : (s_btn_index - 1);
        s_btn_dir   = -1;
    }

    if (s_btn_index != old) {
        s_btn_dirty = true;
        printf("[POMODORO] btn: %s\n", btn_display_name(s_btn_index));
    }
}

/* ================================================================
 *  页面创建器
 * ================================================================ */

static void on_container_delete(lv_event_t *e) {
    (void)e;
    s_page        = NULL;
    s_arc         = NULL;
    s_time_label  = NULL;
    s_phase_label = NULL;
    s_btn_label   = NULL;
    s_btn_bg      = NULL;
    printf("[POMODORO] page destroyed\n");
}

static menu_page_t* pomo_creator(void) {
    /* 分配最小 menu_page_t */
    menu_page_t *pg = calloc(1, sizeof(menu_page_t));
    if (!pg) return NULL;
    pg->title = "Pomodoro";
    pg->btn_count = 0;

    lv_obj_t *scr = menu_ui_get_screen();
    if (!scr) { free(pg); return NULL; }

    /* ---- 容器（与 menu_ui_page_create 相同规格） ---- */
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
    lv_label_set_text(pg->title_label, "Pomodoro");
    lv_obj_set_style_text_font(pg->title_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(pg->title_label, lv_color_white(), 0);
    lv_obj_set_style_border_width(pg->title_label, 0, 0);
    lv_obj_set_style_bg_opa(pg->title_label, LV_OPA_TRANSP, 0);
    lv_obj_align(pg->title_label, LV_ALIGN_TOP_MID, 0, 4);

    /* ---- 环形进度条 ---- */
    s_arc = lv_arc_create(pg->container);
    lv_obj_set_size(s_arc, 150, 150);
    lv_obj_align(s_arc, LV_ALIGN_CENTER, 0, -10);
    lv_arc_set_range(s_arc, 0, 360);
    lv_arc_set_bg_angles(s_arc, 0, 360);
    lv_arc_set_rotation(s_arc, 270);
    lv_arc_set_value(s_arc, 0);
    lv_obj_set_style_arc_color(s_arc, lv_color_hex(0x333333), LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_arc, 10, LV_PART_MAIN);
    lv_obj_set_style_arc_color(s_arc, lv_color_hex(0x4FC3F7), LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(s_arc, 10, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(s_arc, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_style_pad_all(s_arc, 0, LV_PART_KNOB);

    /* ---- 倒计时文字（叠在弧中央） ---- */
    s_time_label = lv_label_create(pg->container);
    lv_obj_set_style_text_font(s_time_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_time_label, lv_color_white(), 0);
    lv_obj_set_style_border_width(s_time_label, 0, 0);
    lv_obj_set_style_bg_opa(s_time_label, LV_OPA_TRANSP, 0);
    lv_obj_align(s_time_label, LV_ALIGN_CENTER, 0, -30);

    /* ---- 阶段标签 ---- */
    s_phase_label = lv_label_create(pg->container);
    lv_obj_set_style_text_font(s_phase_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_border_width(s_phase_label, 0, 0);
    lv_obj_set_style_bg_opa(s_phase_label, LV_OPA_TRANSP, 0);
    lv_obj_align(s_phase_label, LV_ALIGN_CENTER, 0, 5);

    /* ---- 底部单个按钮（绝对定位，便于动画滑入滑出） ---- */
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
    s_adjusting   = false;
    s_tick_acc_ms = 0;
    s_cb_override = true;
    memset(&s_btn_anim, 0, sizeof(s_btn_anim));

    pomo_update_ui();
    printf("[POMODORO] page created\n");
    return pg;
}

/* ================================================================
 *  公开 API
 * ================================================================ */

void pomodoro_init(void) {
    s_pomo.work_minutes     = POMODORO_WORK_DEFAULT;
    s_pomo.break_minutes    = POMODORO_BREAK_DEFAULT;
    s_pomo.is_work_phase    = true;
    s_pomo.remaining_seconds = (uint32_t)POMODORO_WORK_DEFAULT * 60;
    s_pomo.running          = false;

    s_page         = NULL;
    s_arc          = NULL;
    s_time_label   = NULL;
    s_phase_label  = NULL;
    s_btn_label    = NULL;
    s_btn_bg       = NULL;
    s_btn_index    = 0;
    s_adjusting    = false;
    s_tick_acc_ms  = 0;
    s_pending_ticks = 0;
    s_adjust_dirty  = false;
    s_adjust_done   = false;
    s_ui_dirty      = false;
    s_btn_dirty     = false;
    s_btn_pending   = false;
    s_cb_override   = false;

    menu_ui_register_creator(MENU_ITEM_POMODORO, pomo_creator);
    printf("[POMODORO] init ok (work=%d min, break=%d min)\n",
           POMODORO_WORK_DEFAULT, POMODORO_BREAK_DEFAULT);
}

void pomodoro_tick(uint32_t dt_ms) {
    s_tick_acc_ms += dt_ms;
    while (s_tick_acc_ms >= 1000) {
        s_tick_acc_ms -= 1000;
        if (s_pending_ticks < 10) s_pending_ticks++;
    }
}

void pomodoro_process_updates(void) {
    /* ---- 按钮切换动画 tick ---- */
    if (s_btn_anim.on) {
        int64_t elapsed = (esp_timer_get_time() - s_btn_anim.t0) / 1000;
        if (elapsed >= BTN_ANIM_MS) {
            /* 动画结束，归位 */
            lv_obj_set_x(s_btn_bg, BTN_CX);
            pomo_update_btn_label();
            refresh();
            s_btn_anim.on = false;
        } else {
            int half = BTN_ANIM_MS / 2;
            int dir  = s_btn_anim.dir;
            int32_t x;
            if (elapsed < half) {
                /* 前半段：滑出 */
                int32_t e = menu_ui_ease_out((int32_t)elapsed, half);
                x = BTN_CX - (dir * LCD_HOR_RES * e) / 256;
            } else {
                /* 后半段：滑入 */
                if (!s_btn_anim.swapped) {
                    s_btn_anim.swapped = true;
                    pomo_update_btn_label();
                }
                int32_t e = menu_ui_ease_out((int32_t)(elapsed - half), half);
                x = (dir > 0) ? (LCD_HOR_RES - (LCD_HOR_RES - BTN_CX) * e / 256)
                              : (-BTN_W + (BTN_CX + BTN_W) * e / 256);
            }
            lv_obj_set_x(s_btn_bg, x);
            refresh();
        }
    }

    /* 替换 sub 回调（menu_ui_enter_sub 会设为 stack_input_cb，我们覆盖它） */
    if (s_cb_override && s_page) {
        s_cb_override = false;
        menu_engine_set_sub_callback(pomo_input_cb);
        s_last_tilt_us = 0;
    }

    /* 按钮索引变化 → 启动切换动画 */
    if (s_btn_dirty && !s_btn_anim.on) {
        s_btn_dirty = false;
        if (s_page && s_btn_bg) {
            s_btn_anim.on      = true;
            s_btn_anim.t0      = esp_timer_get_time();
            s_btn_anim.dir     = s_btn_dir;
            s_btn_anim.swapped = false;
        }
    }

    /* 按钮按下 → 激活当前按钮 */
    if (s_btn_pending) {
        s_btn_pending = false;
        pomo_activate_btn();
    }

    /* 调节值变化 → 更新标题 */
    if (s_adjust_dirty) {
        s_adjust_dirty = false;
        if (s_adjusting) {
            pomo_set_adjust_title();
            pomo_update_btn_label();
            refresh();
        }
    }

    /* 调节确认 → 恢复 UI + 恢复默认输入回调 */
    if (s_adjust_done) {
        s_adjust_done = false;
        menu_ui_restore_default_input();
        menu_engine_rotary_override(false);
        s_cb_override = true; /* 下次进调节后再覆盖 */
        pomo_restore_title();
        pomo_update_ui();
    }

    /* 通用 UI 刷新 */
    if (s_ui_dirty) {
        s_ui_dirty = false;
        pomo_update_ui();
    }

    /* 处理倒计时 tick */
    while (s_pending_ticks > 0) {
        s_pending_ticks--;
        pomo_do_tick_data();
        if (s_page && s_page->container) {
            pomo_update_time_label();
            pomo_update_phase_label();
            pomo_update_arc();
            pomo_update_btn_label();
            refresh();
        }
    }
}
