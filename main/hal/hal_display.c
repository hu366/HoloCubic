/**
 * @file    hal_display.c
 * @brief   显示驱动实现 —— SPI + ST7789 + LVGL v9.5
 *
 * 使用 ESP-IDF 内置 esp_lcd 组件驱动 ST7789。
 * 双帧缓冲（240×240×2 字节 ×2 = 230KB）分配于 PSRAM。
 */

#include "hal_display.h"
#include "app_config.h"

#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_st7789.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_types.h"
#include "driver/spi_master.h"
#include "driver/ledc.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_check.h"
#include <assert.h>

static const char *TAG = "hal_display";

/* ---------- 模块级句柄 ---------- */
static esp_lcd_panel_io_handle_t s_io_handle  = NULL;
static esp_lcd_panel_handle_t    s_panel      = NULL;
static lv_display_t             *s_lv_display = NULL;

/* ---------- PSRAM 帧缓冲（双缓冲） ---------- */
static uint8_t *s_draw_buf_1 = NULL;
static uint8_t *s_draw_buf_2 = NULL;

/* ================================================================
 *  SPI 总线初始化
 * ================================================================ */
static void spi_bus_init(void)
{
    spi_bus_config_t bus_cfg = {
        .mosi_io_num     = LCD_PIN_MOSI,
        .miso_io_num     = -1,                // 3 线 SPI，不接 MISO
        .sclk_io_num     = LCD_PIN_SCLK,
        .quadwp_io_num   = -1,
        .quadhd_io_num   = -1,
        .max_transfer_sz = LCD_HOR_RES * LCD_VER_RES * 2 + 8,  // 一帧 + 命令头
    };

    ESP_ERROR_CHECK(spi_bus_initialize(LCD_SPI_HOST, &bus_cfg, SPI_DMA_CH_AUTO));
    ESP_LOGI(TAG, "SPI bus initialized (host=%d, sclk=%d, mosi=%d)",
             LCD_SPI_HOST, LCD_PIN_SCLK, LCD_PIN_MOSI);
}

/* ================================================================
 *  ST7789 面板初始化
 * ================================================================ */
static void panel_init(void)
{
    /* ---- 面板 IO（SPI 4 线接口） ---- */
    esp_lcd_panel_io_spi_config_t io_cfg = {
        .cs_gpio_num = LCD_PIN_CS,
        .dc_gpio_num = LCD_PIN_DC,
        .spi_mode    = 0,        // CPOL=0 CPHA=0，ST7789 要求
        .pclk_hz     = LCD_PIXEL_CLOCK_MHZ * 1000 * 1000,
        .trans_queue_depth = 10,
        .lcd_cmd_bits  = 8,
        .lcd_param_bits = 8,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_SPI_HOST,
                                             &io_cfg, &s_io_handle));

    /* ---- ST7789 面板设备 ---- */
    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = LCD_PIN_RST,
        .rgb_ele_order  = LCD_RGB_ELEMENT_ORDER_BGR,  // ST7789 默认 BGR 色序
        .bits_per_pixel = 16,                          // RGB565
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(s_io_handle, &panel_cfg, &s_panel));

    /* ---- 复位 & 初始化序列 ---- */
    ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel));

    /* ---- 开显示（退出睡眠模式） ---- */
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(s_panel, true));

    /* ---- 镜像翻转（全息棱镜适配） ---- */
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(s_panel, MIRROR_X, MIRROR_Y));
    ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(s_panel, false));   // 不交换 XY，竖屏

    /* ---- 颜色反转（ST7789 通常需要） ---- */
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(s_panel, true));

    ESP_LOGI(TAG, "ST7789 panel initialized (240x240, RGB565, 40MHz SPI)");
}

/* ================================================================
 *  背光 PWM（LEDC）
 * ================================================================ */
static void backlight_init(void)
{
    ledc_timer_config_t ledc_timer = {
        .speed_mode       = LEDC_LOW_SPEED_MODE,
        .duty_resolution  = LEDC_TIMER_10_BIT,   // 0~1023
        .timer_num        = LEDC_TIMER_0,
        .freq_hz          = 5000,
        .clk_cfg          = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    ledc_channel_config_t ledc_ch = {
        .gpio_num   = LCD_PIN_BACKLIGHT,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel    = LEDC_CHANNEL_0,
        .timer_sel  = LEDC_TIMER_0,
        .duty       = 1023,         // 100% 亮度
        .hpoint     = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_ch));

    ESP_LOGI(TAG, "Backlight PWM on GPIO %d", LCD_PIN_BACKLIGHT);
}

/* ================================================================
 *  LVGL 显示驱动注册 + 双帧缓冲
 * ================================================================ */
static void lvgl_display_init(void)
{
    /* ---- 在 PSRAM 中分配双帧缓冲 ---- */
    const size_t buf_size = LVGL_DRAW_BUF_SIZE;  // 240*240*2 = 115200

    s_draw_buf_1 = heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_draw_buf_2 = heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

    ESP_LOGI(TAG, "Draw buffer 1 @ %p (SPIRAM)", (void *)s_draw_buf_1);
    ESP_LOGI(TAG, "Draw buffer 2 @ %p (SPIRAM)", (void *)s_draw_buf_2);

    assert(s_draw_buf_1 != NULL);
    assert(s_draw_buf_2 != NULL);

    /* ---- 创建 LVGL display ---- */
    s_lv_display = lv_display_create(LCD_HOR_RES, LCD_VER_RES);
    lv_display_set_flush_cb(s_lv_display, hal_display_flush);
    lv_display_set_buffers(s_lv_display,
                           s_draw_buf_1,
                           s_draw_buf_2,
                           buf_size,
                           LV_DISPLAY_RENDER_MODE_PARTIAL);

    /* ---- 创建默认屏幕 ---- */
    lv_obj_t *scr = lv_display_get_screen_active(s_lv_display);
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    ESP_LOGI(TAG, "LVGL display registered (partial render, dual PSRAM buffer)");
}

/* ================================================================
 *  公开接口
 * ================================================================ */

void hal_display_init(void)
{
    ESP_LOGI(TAG, "--- Display HAL init start ---");

    spi_bus_init();
    ESP_LOGI(TAG, "[1/4] SPI bus ready");

    panel_init();
    ESP_LOGI(TAG, "[2/4] ST7789 panel ready");

    backlight_init();
    ESP_LOGI(TAG, "[3/4] Backlight ready");

    lvgl_display_init();
    ESP_LOGI(TAG, "[4/4] LVGL display ready");

    ESP_LOGI(TAG, "--- Display HAL init done ---");
}

/**
 * LVGL v9 flush 回调。
 * 像素格式为 RGB565（uint8_t 数组，每 2 字节一个像素）。
 * 使用 esp_lcd_panel_draw_bitmap 一次性推送脏区域。
 */
void hal_display_flush(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    /* 跳过 0 面积区域（不应该发生，但防御） */
    if (area->x2 < area->x1 || area->y2 < area->y1) {
        lv_display_flush_ready(disp);
        return;
    }

    int w = area->x2 - area->x1 + 1;
    int h = area->y2 - area->y1 + 1;

    esp_lcd_panel_draw_bitmap(s_panel,
                              area->x1, area->y1,
                              area->x1 + w, area->y1 + h,
                              (const void *)px_map);

    lv_display_flush_ready(disp);
}
