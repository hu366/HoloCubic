/**
 * @file    pet_anim.c
 * @brief   Task 42: 宠物动画帧管理实现
 *
 * 架构：
 *  - 角度映射状态（IDLE/HUNGRY/SAD）：加载时全量 PSRAM 缓存，
 *    tick 时 O(1) 查表切换帧指针，零延迟。
 *  - 时序播放状态（EATING/PLAYING/SHOCKED/WALKING）：
 *    双缓冲流式读取（当前帧 + 预读下一帧），播完自动回 IDLE。
 */

#include "pet_anim.h"
#include "app_config.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include "esp_log.h"
#include "esp_heap_caps.h"

static const char *TAG = "pet_anim";

/* ================================================================
 *  内部类型
 * ================================================================ */

typedef enum {
    MODE_NONE = 0,
    MODE_ANGLE,       /* 角度映射 */
    MODE_SEQUENTIAL,  /* 时序播放 */
} anim_mode_t;

/* 预缓存一帧 */
typedef struct {
    uint8_t *data;     /* PSRAM 中的像素数据 */
    size_t   size;     /* 字节数 */
} frame_buf_t;

/* ================================================================
 *  内部状态
 * ================================================================ */

static anim_mode_t    s_mode        = MODE_NONE;
static pet_state_t    s_loaded_state;       /* 当前加载的状态 */

/* ---- 角度映射 ---- */
static frame_buf_t   *s_angle_frames = NULL; /* 全部帧缓存 */
static uint16_t       s_angle_rows   = 0;    /* grid 行数 */
static uint16_t       s_angle_cols   = 0;    /* grid 列数 */
static uint16_t       s_angle_total  = 0;    /* 总帧数 */

/* ---- 时序播放 ---- */
static frame_buf_t    s_seq_buf[2];          /* 双缓冲 */
static uint8_t        s_seq_active  = 0;     /* 当前显示槽位 0/1 */
static uint16_t       s_seq_total   = 0;     /* 总帧数 */
static uint16_t       s_seq_index   = 0;     /* 当前帧号 */
static uint32_t       s_seq_timer   = 0;     /* 帧计时器 ms */

/* ---- 公共 ---- */
static lv_image_dsc_t s_img_dsc;             /* 当前帧的 LVGL 描述符 */
static uint16_t       s_width       = 0;
static uint16_t       s_height      = 0;
static bool           s_dirty       = false;
static uint16_t       s_cur_angle_idx = 0xFFFF; /* 当前角度帧索引 */

/* ================================================================
 *  前向声明
 * ================================================================ */

static bool load_angle_frames(const char *dir, uint16_t rows, uint16_t cols);
static bool load_seq_meta(const char *dir);
static bool read_frame_file(const char *dir, uint16_t idx, frame_buf_t *fb);
static void free_angle_frames(void);
static void free_seq_buffers(void);
static uint16_t angle_to_index(float pitch, float roll);
static const char *state_to_dir(pet_state_t state);
static bool parse_meta(const char *dir, uint16_t *w, uint16_t *h,
                       uint16_t *a, uint16_t *b, uint16_t *c);

/* ================================================================
 *  API
 * ================================================================ */

void pet_anim_init(void)
{
    s_mode          = MODE_NONE;
    s_loaded_state  = PET_IDLE;
    s_dirty         = false;
    s_cur_angle_idx = 0xFFFF;
    s_seq_index     = 0;
    s_seq_timer     = 0;
    s_seq_active    = 0;

    memset(&s_img_dsc, 0, sizeof(s_img_dsc));
    memset(s_seq_buf, 0, sizeof(s_seq_buf));
}

bool pet_anim_load(const pet_data_t *pet)
{
    if (!pet) return false;

    /* 先释放旧数据 */
    pet_anim_unload();

    const char *dir = state_to_dir(pet->state);
    if (!dir) {
        ESP_LOGE(TAG, "Unknown state %d", pet->state);
        return false;
    }

    /* 解析 meta.txt */
    uint16_t w = 0, h = 0, a = 0, b = 0, c = 0;
    if (!parse_meta(dir, &w, &h, &a, &b, &c)) {
        ESP_LOGE(TAG, "Failed to parse meta.txt in %s", dir);
        return false;
    }

    s_width  = w;
    s_height = h;
    s_loaded_state = pet->state;

    /* 初始化 LVGL 图像描述符（RGB565 格式，读文件时从 ARGB8565 转换） */
    memset(&s_img_dsc, 0, sizeof(s_img_dsc));
    s_img_dsc.header.magic  = LV_IMAGE_HEADER_MAGIC;
    s_img_dsc.header.cf     = LV_COLOR_FORMAT_RGB565;
    s_img_dsc.header.w      = w;
    s_img_dsc.header.h      = h;
    s_img_dsc.header.stride = w * 2;   /* RGB565 = 2 字节/像素 */
    s_img_dsc.data          = NULL;
    s_img_dsc.data_size     = (uint32_t)w * h * 2;

    /* 判断模式：a=rows, b=cols, c=0 → 角度映射；否则 → 时序 */
    if (b > 0 && c == 0) {
        /* 角度映射模式 */
        if (!load_angle_frames(dir, a, b)) {
            return false;
        }
        s_mode       = MODE_ANGLE;
        s_angle_rows = a;
        s_angle_cols = b;
        s_angle_total = a * b;

        /* 初始显示中间帧（正视角，pitch=0, roll=0） */
        s_cur_angle_idx = angle_to_index(0.0f, 0.0f);
        s_img_dsc.data  = s_angle_frames[s_cur_angle_idx].data;
        s_img_dsc.data_size = s_angle_frames[s_cur_angle_idx].size;
        s_dirty = true;

        ESP_LOGI(TAG, "Angle mode: %ux%u grid, %u frames, %u bytes each",
                 a, b, s_angle_total, (unsigned)s_angle_frames[0].size);
    } else {
        /* 时序播放模式：a=帧数, b/c 不用 */
        if (!load_seq_meta(dir)) {
            return false;
        }
        s_mode      = MODE_SEQUENTIAL;
        s_seq_total = a;
        s_seq_index = 0;
        s_seq_timer = 0;
        s_seq_active = 0;

        /* 预读首帧 */
        if (!read_frame_file(dir, 0, &s_seq_buf[0])) {
            ESP_LOGE(TAG, "Failed to read frame 0");
            return false;
        }
        s_img_dsc.data      = s_seq_buf[0].data;
        s_img_dsc.data_size = s_seq_buf[0].size;
        s_dirty = true;

        /* 若帧数 >1，预读第二帧 */
        if (s_seq_total > 1) {
            if (read_frame_file(dir, 1, &s_seq_buf[1])) {
                s_seq_active = 0;  /* 当前显示槽 0，槽 1 已预读 */
            }
        }

        ESP_LOGI(TAG, "Sequential mode: %u frames", s_seq_total);
    }

    return true;
}

void pet_anim_unload(void)
{
    s_mode = MODE_NONE;

    free_angle_frames();
    free_seq_buffers();

    s_img_dsc.data = NULL;
    s_dirty        = false;
    s_cur_angle_idx = 0xFFFF;
    s_seq_index    = 0;
    s_seq_timer    = 0;
}

pet_anim_event_t pet_anim_tick(pet_data_t *pet,
                               float pitch_deg, float roll_deg, uint32_t dt_ms)
{
    if (s_mode == MODE_NONE || !pet) return PET_ANIM_NONE;

    if (s_mode == MODE_ANGLE) {
        /* ---- 角度映射：根据 pitch/roll 选帧 ---- */
        uint16_t idx = angle_to_index(pitch_deg, roll_deg);
        if (idx != s_cur_angle_idx) {
            s_cur_angle_idx = idx;
            s_img_dsc.data = s_angle_frames[idx].data;
            s_img_dsc.data_size = s_angle_frames[idx].size;
            s_dirty = true;
        }
        return PET_ANIM_NONE;
    }

    /* ---- 时序播放 ---- */
    s_seq_timer += dt_ms;

    while (s_seq_timer >= PET_ANIM_FRAME_MS) {
        s_seq_timer -= PET_ANIM_FRAME_MS;
        s_seq_index++;

        if (s_seq_index >= s_seq_total) {
            /* 动画播放完毕 */
            pet->anim_busy = false;
            pet->state     = PET_IDLE;
            s_mode         = MODE_NONE;
            ESP_LOGI(TAG, "Animation done, back to IDLE");
            return PET_ANIM_DONE;
        }

        /* 切换到预读槽位 */
        uint8_t prev_active = s_seq_active;
        s_seq_active = (uint8_t)(1 - s_seq_active);

        /* 显示预读的帧 */
        s_img_dsc.data      = s_seq_buf[s_seq_active].data;
        s_img_dsc.data_size = s_seq_buf[s_seq_active].size;
        s_dirty = true;

        /* 释放旧槽位，预读下一帧 */
        const char *dir = state_to_dir(pet->state);
        if (s_seq_buf[prev_active].data) {
            heap_caps_free(s_seq_buf[prev_active].data);
            s_seq_buf[prev_active].data = NULL;
        }
        if (dir && (s_seq_index + 1) < s_seq_total) {
            read_frame_file(dir, s_seq_index + 1, &s_seq_buf[prev_active]);
        }
    }

    return PET_ANIM_PLAYING;
}

const lv_image_dsc_t *pet_anim_get_frame(void)
{
    return (s_mode != MODE_NONE) ? &s_img_dsc : NULL;
}

bool pet_anim_is_dirty(void)
{
    bool ret = s_dirty;
    s_dirty = false;
    return ret;
}

/* ================================================================
 *  内部函数
 * ================================================================ */

/** pitch → row (前倾=俯角→大行号), roll → col (右倾→大列号) */
static uint16_t angle_to_index(float pitch, float roll)
{
    /* 灵敏度增益：2x → 倾斜 22.5° 即达满幅 */
    pitch *= 2.0f;
    roll  *= 2.0f;

    /* clamp 到 ±PET_ANGLE_MAX_DEG */
    if      (pitch >  PET_ANGLE_MAX_DEG) pitch =  PET_ANGLE_MAX_DEG;
    else if (pitch < -PET_ANGLE_MAX_DEG) pitch = -PET_ANGLE_MAX_DEG;
    if      (roll  >  PET_ANGLE_MAX_DEG) roll  =  PET_ANGLE_MAX_DEG;
    else if (roll  < -PET_ANGLE_MAX_DEG) roll  = -PET_ANGLE_MAX_DEG;

    /* pitch→col(左右), roll→row(上下), 双轴取反 */
    float pitch_norm = (PET_ANGLE_MAX_DEG + roll)  / (2.0f * PET_ANGLE_MAX_DEG);
    float roll_norm  = (PET_ANGLE_MAX_DEG - pitch) / (2.0f * PET_ANGLE_MAX_DEG);

    int row = (int)(pitch_norm * (s_angle_rows - 1) + 0.5f);
    int col = (int)(roll_norm  * (s_angle_cols - 1) + 0.5f);

    /* 边界保护 */
    if (row < 0) row = 0;
    if (row >= (int)s_angle_rows) row = s_angle_rows - 1;
    if (col < 0) col = 0;
    if (col >= (int)s_angle_cols) col = s_angle_cols - 1;

    return (uint16_t)(row * s_angle_cols + col);
}

/** pet_state_t → SD 卡子目录名 */
static const char *state_to_dir(pet_state_t state)
{
    switch (state) {
    case PET_IDLE:      return "pet_frames/idle";
    case PET_HUNGRY:    return "pet_frames/hungry";
    case PET_SAD:       return "pet_frames/sad";
    case PET_SHOCKED:   return "pet_frames/shocked";
    case PET_EATING:    return "pet_frames/eating";
    case PET_PLAYING:   return "pet_frames/playing";
    case PET_WALKING:   return "pet_frames/walking";
    default:            return NULL;
    }
}

/** 解析 meta.txt
 *  - 角度映射："W H ROWS COLS" → a=rows, b=cols, c=0
 *  - 时序播放："W H COUNT"     → a=count, b=0, c=0
 */
static bool parse_meta(const char *dir, uint16_t *w, uint16_t *h,
                       uint16_t *a, uint16_t *b, uint16_t *c)
{
    char path[256];
    snprintf(path, sizeof(path), "%s/%s/meta.txt", SD_MOUNT_POINT, dir);

    FILE *f = fopen(path, "r");
    if (!f) {
        ESP_LOGE(TAG, "Cannot open %s", path);
        return false;
    }

    int n = fscanf(f, "%hu %hu %hu %hu %hu", w, h, a, b, c);
    fclose(f);

    if (n < 3) {
        ESP_LOGE(TAG, "meta.txt parse error: got %d fields, need >=3", n);
        return false;
    }
    if (*w == 0 || *h == 0) {
        ESP_LOGE(TAG, "meta.txt: invalid size %ux%u", *w, *h);
        return false;
    }

    /* 3 个字段 = 时序；4+ 字段 = 角度映射 */
    if (n == 3) {
        *b = 0;
        *c = 0;
    } else if (n == 4) {
        *c = 0;
    }
    /* n == 5: a=rows, b=cols, c 忽略 */

    return true;
}

/** 读取 ARGB8565 帧 → PSRAM → 转换为 RGB565（剥掉 Alpha 字节） */
static bool read_frame_file(const char *dir, uint16_t idx, frame_buf_t *fb)
{
    char path[256];
    snprintf(path, sizeof(path), "%s/%s/%03d.bin", SD_MOUNT_POINT, dir, idx);

    FILE *f = fopen(path, "rb");
    if (!f) {
        ESP_LOGE(TAG, "Cannot open %s", path);
        return false;
    }

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    rewind(f);

    if (fsize <= 0) {
        ESP_LOGE(TAG, "Empty file %s", path);
        fclose(f);
        return false;
    }

    /* 暂存原始 ARGB8565 数据 */
    uint8_t *raw = (uint8_t *)heap_caps_malloc((size_t)fsize, MALLOC_CAP_SPIRAM);
    if (!raw) {
        ESP_LOGE(TAG, "PSRAM alloc %ld failed", fsize);
        fclose(f);
        return false;
    }

    size_t read = fread(raw, 1, (size_t)fsize, f);
    fclose(f);

    if (read != (size_t)fsize) {
        ESP_LOGE(TAG, "Short read %u/%ld", (unsigned)read, fsize);
        heap_caps_free(raw);
        return false;
    }

    /* ARGB8565(3B/px) → RGB565(2B/px): 剥掉每像素的第 3 字节(Alpha) */
    uint32_t px_count = s_width * s_height;
    size_t   out_size = (size_t)px_count * 2;

    fb->data = (uint8_t *)heap_caps_malloc(out_size, MALLOC_CAP_SPIRAM);
    if (!fb->data) {
        ESP_LOGE(TAG, "PSRAM alloc %u failed", (unsigned)out_size);
        heap_caps_free(raw);
        return false;
    }

    for (uint32_t i = 0; i < px_count; i++) {
        if (raw[i * 3 + 2] == 0) {
            /* 透明像素 → 蓝色 #001a33 → RGB565 0x00C6 */
            fb->data[i * 2]     = 0xC6;
            fb->data[i * 2 + 1] = 0x00;
        } else {
            fb->data[i * 2]     = raw[i * 3];
            fb->data[i * 2 + 1] = raw[i * 3 + 1];
        }
    }

    heap_caps_free(raw);
    fb->size = out_size;
    return true;
}

/** 加载全部角度映射帧到 PSRAM */
static bool load_angle_frames(const char *dir, uint16_t rows, uint16_t cols)
{
    uint16_t total = rows * cols;

    s_angle_frames = (frame_buf_t *)heap_caps_calloc(total, sizeof(frame_buf_t),
                                                      MALLOC_CAP_SPIRAM);
    if (!s_angle_frames) {
        ESP_LOGE(TAG, "PSRAM calloc %u failed", total);
        return false;
    }
    s_angle_total = total;

    for (uint16_t i = 0; i < total; i++) {
        if (!read_frame_file(dir, i, &s_angle_frames[i])) {
            ESP_LOGE(TAG, "Load angle frame %u/%u failed", i + 1, total);
            /* 清理已加载的 */
            for (uint16_t j = 0; j < i; j++) {
                heap_caps_free(s_angle_frames[j].data);
            }
            heap_caps_free(s_angle_frames);
            s_angle_frames = NULL;
            s_angle_total  = 0;
            return false;
        }
    }

    return true;
}

/** 时序模式：仅验证帧数和首帧 */
static bool load_seq_meta(const char *dir)
{
    /* 帧已有 meta 信息（总数从 parse_meta 得到）。
     * 只需要验证至少能读第一帧。 */
    char path[256];
    snprintf(path, sizeof(path), "%s/%s/000.bin", SD_MOUNT_POINT, dir);

    FILE *f = fopen(path, "r");
    if (!f) {
        ESP_LOGE(TAG, "First frame not found: %s", path);
        return false;
    }
    fclose(f);
    return true;
}

static void free_angle_frames(void)
{
    if (!s_angle_frames) return;

    for (uint16_t i = 0; i < s_angle_total; i++) {
        if (s_angle_frames[i].data) {
            heap_caps_free(s_angle_frames[i].data);
        }
    }
    heap_caps_free(s_angle_frames);
    s_angle_frames = NULL;
    s_angle_total  = 0;
    s_angle_rows   = 0;
    s_angle_cols   = 0;
}

static void free_seq_buffers(void)
{
    for (int i = 0; i < 2; i++) {
        if (s_seq_buf[i].data) {
            heap_caps_free(s_seq_buf[i].data);
            s_seq_buf[i].data = NULL;
            s_seq_buf[i].size = 0;
        }
    }
    s_seq_total = 0;
    s_seq_index = 0;
    s_seq_timer = 0;
}
