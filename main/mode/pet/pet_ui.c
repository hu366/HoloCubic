/**
 * @file    pet_ui.c — IMU 倾斜→视角旋转 + 位置飘动 + 归位
 */
#include "pet_ui.h"
#include "pet_anim.h"
#include "pet_motion.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "pet_ui";

static volatile float s_pitch = 0;
static volatile float s_roll  = 0;

static lv_obj_t  *s_screen = NULL;
static lv_obj_t  *s_img    = NULL;
static pet_data_t s_pet;
static bool       s_first  = true;
static int16_t    s_lx = -1, s_ly = -1;  /* 上次位置 */

void pet_ui_init(void)
{
    s_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(0x001a33), 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
    lv_obj_set_scrollbar_mode(s_screen, LV_SCROLLBAR_MODE_OFF);

    if (s_first) { pet_anim_init(); s_first = false; }
    memset(&s_pet, 0, sizeof(s_pet));
    s_pet.state = PET_IDLE;
    pet_motion_init(&s_pet);
    s_lx = s_pet.position.x;
    s_ly = s_pet.position.y;

    bool ok = pet_anim_load(&s_pet);
    ESP_LOGI(TAG, "load idle = %d", ok);

    if (ok) {
        s_img = lv_image_create(s_screen);
        lv_image_set_src(s_img, pet_anim_get_frame());
        lv_obj_set_size(s_img, PET_SPRITE_SIZE, PET_SPRITE_SIZE);
        lv_obj_center(s_img);
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

    /* 视角旋转 */
    pet_anim_event_t ev = pet_anim_tick(&s_pet, p, r, dt_ms);
    if (ev == PET_ANIM_DONE) {
        pet_anim_unload();
        s_pet.state = PET_IDLE;
        pet_anim_load(&s_pet);
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
