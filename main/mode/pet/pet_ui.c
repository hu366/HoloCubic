/**
 * @file    pet_ui.c — IMU 倾斜→视角旋转 + 位置飘动 + 归位
 */
#include "pet_ui.h"
#include "pet_anim.h"
#include "pet_motion.h"
#include "app_config.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "pet_ui";

static volatile float s_pitch = 0;
static volatile float s_roll  = 0;

static lv_obj_t  *s_screen = NULL;
static lv_obj_t  *s_img    = NULL;
static lv_obj_t  *s_load_label = NULL;
static pet_data_t s_pet;
static bool       s_first  = true;
static int16_t    s_lx = -1, s_ly = -1;  /* 上次位置 */

static void on_load_progress(uint16_t loaded, uint16_t total)
{
    if (s_load_label) {
        lv_label_set_text_fmt(s_load_label, "Loading %d/%d", loaded, total);
        lv_refr_now(NULL);
    }
}

void pet_ui_init(void)
{
    if (s_screen) {
        lv_obj_delete(s_screen);
        s_screen = NULL;
    }
    s_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(0x001a33), 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
    lv_obj_set_scrollbar_mode(s_screen, LV_SCROLLBAR_MODE_OFF);

    /* 先显示蓝色背景，避免白条纹 */
    lv_screen_load(s_screen);
    lv_refr_now(NULL);

    if (s_first) { pet_anim_init(); s_first = false; }
    memset(&s_pet, 0, sizeof(s_pet));
    s_pet.state = PET_IDLE;
    pet_motion_init(&s_pet);
    s_lx = s_pet.position.x;
    s_ly = s_pet.position.y;

    /* 快速加载中心帧 → 宠物立即出现 */
    bool ok = pet_anim_load_quick(&s_pet);
    ESP_LOGI(TAG, "quick load = %d", ok);

    if (ok) {
        s_img = lv_image_create(s_screen);
        lv_image_set_src(s_img, pet_anim_get_frame());
        lv_obj_set_size(s_img, PET_SPRITE_SIZE, PET_SPRITE_SIZE);
        lv_obj_center(s_img);

        /* 加载进度标签 */
        s_load_label = lv_label_create(s_screen);
        lv_label_set_text(s_load_label, "Loading...");
        lv_obj_align(s_load_label, LV_ALIGN_BOTTOM_MID, 0, -16);
        lv_obj_set_style_text_color(s_load_label, lv_color_hex(0x8899AA), 0);
        lv_refr_now(NULL);  /* 宠物 + 加载提示立即显示 */

        /* 补全剩余 99 帧（约 3 秒，进度标签实时更新） */
        pet_anim_load_remaining(&s_pet, on_load_progress);

        /* 加载完成，移除标签 */
        lv_obj_delete(s_load_label);
        s_load_label = NULL;
        lv_refr_now(NULL);
    }
}

void pet_ui_deinit(void)
{
    pet_anim_unload();
    if (s_img) { lv_obj_delete(s_img); s_img = NULL; }
}

void pet_ui_process(uint32_t dt_ms)
{
    if (!s_img) return;

    float p = s_pitch;
    float r = s_roll;
    bool need_refresh = false;

#if LCD_ROTATION == 90
    /* 左转90°：new_pitch=-old_roll, new_roll=old_pitch */
    float tmp = p;
    p = -r;
    r = tmp;
#elif LCD_ROTATION == 270
    /* 右转90°：new_pitch=old_roll, new_roll=old_pitch */
    float tmp = p;
    p = r;
    r = tmp;
#endif

    /* 视角旋转 */
    pet_anim_event_t ev = pet_anim_tick(&s_pet, p, r, dt_ms);
    if (ev == PET_ANIM_DONE) {
        pet_anim_unload();
        s_pet.state = PET_IDLE;
        /* 快速切回 IDLE 中心帧 → 立即显示 */
        pet_anim_load_quick(&s_pet);
        lv_image_set_src(s_img, pet_anim_get_frame());
        lv_refr_now(NULL);
        need_refresh = false;
        /* 补全剩余帧 */
        pet_anim_load_remaining(&s_pet, NULL);
    }
    if (pet_anim_is_dirty()) {
        lv_image_set_src(s_img, pet_anim_get_frame());
        need_refresh = true;
    }

    /* 位置飘动（pitch→左右, roll→上下, 仅左右取反） */
    pet_motion_tick(&s_pet, -r, p, dt_ms);
    if (s_pet.position.x != s_lx || s_pet.position.y != s_ly) {
        s_lx = s_pet.position.x;
        s_ly = s_pet.position.y;
        lv_obj_set_pos(s_img, s_lx - 64, s_ly - 64);
        need_refresh = true;
    }

    if (need_refresh) lv_refr_now(NULL);
}

void pet_ui_on_tilt(float pitch, float roll) { s_pitch = pitch; s_roll = roll; }
void pet_ui_on_shake(void) {}
lv_obj_t *pet_ui_get_screen(void) { return s_screen; }
