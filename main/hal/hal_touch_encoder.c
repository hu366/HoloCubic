/**
 * @file    hal_touch_encoder.c
 * @brief   旋转编码器驱动实现 —— PCNT 硬件脉冲计数 + 按键去抖状态机
 *
 * 使用 ESP-IDF pulse_cnt 驱动进行正交解码。
 * 按键通过 5ms 周期定时器轮询 GPIO 电平，运行去抖状态机。
 */

#include "hal_touch_encoder.h"
#include "app_config.h"

#include "driver/pulse_cnt.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_log.h"

static const char *TAG = "hal_encoder";

/* ================================================================
 *  模块级状态
 * ================================================================ */
static encoder_callback_t     s_rot_cb  = NULL;
static encoder_btn_callback_t s_btn_cb  = NULL;
static bool                   s_inited  = false;

static pcnt_unit_handle_t     s_pcnt_unit  = NULL;
static pcnt_channel_handle_t  s_pcnt_chan  = NULL;
static esp_timer_handle_t     s_poll_timer = NULL;

/* ================================================================
 *  按键去抖状态机
 * ================================================================ */
typedef enum {
    BTN_IDLE,          // 等待按下
    BTN_DEBOUNCE,      // 去抖中（刚检测到低电平）
    BTN_PRESSED,       // 确认按下，追踪持续时间
} btn_state_t;

static btn_state_t s_btn_state            = BTN_IDLE;
static uint32_t    s_btn_press_start_ms   = 0;
static uint32_t    s_btn_debounce_start_ms = 0;
static bool        s_long_press_handled   = false;

/* ---- 轮询周期（微秒） ---- */
#define ENCODER_POLL_PERIOD_US  5000    // 5 ms

/* ================================================================
 *  周期轮询回调：读 PCNT + 按键去抖
 * ================================================================ */
static void encoder_poll_cb(void *arg)
{
    (void)arg;
    /* ---------- 1. 读取 PCNT 计数值 ---------- */
    int pulse_count = 0;
    pcnt_unit_get_count(s_pcnt_unit, &pulse_count);

    if (pulse_count != 0) {
        pcnt_unit_clear_count(s_pcnt_unit);

        if (s_rot_cb) {
            /* 截断到 int8_t 范围（5ms 周期内足以覆盖任何旋转速度） */
            if (pulse_count > 127) {
                pulse_count = 127;
            } else if (pulse_count < -128) {
                pulse_count = -128;
            }
            s_rot_cb((int8_t)pulse_count);
        }
    }

    /* ---------- 2. 按键去抖状态机 ---------- */
    bool     btn_pressed = (gpio_get_level(ENCODER_PIN_BTN) == 0);  // 低电平有效
    uint32_t now_ms      = (uint32_t)(esp_timer_get_time() / 1000);

    switch (s_btn_state) {

    case BTN_IDLE:
        if (btn_pressed) {
            s_btn_state             = BTN_DEBOUNCE;
            s_btn_debounce_start_ms = now_ms;
        }
        break;

    case BTN_DEBOUNCE: {
        uint32_t elapsed = now_ms - s_btn_debounce_start_ms;
        if (elapsed >= ENCODER_BTN_DEBOUNCE_MS) {
            if (btn_pressed) {
                /* 确认按下 */
                s_btn_state          = BTN_PRESSED;
                s_btn_press_start_ms = now_ms;
                s_long_press_handled = false;
            } else {
                /* 毛刺，退回 IDLE */
                s_btn_state = BTN_IDLE;
            }
        }
        break;
    }

    case BTN_PRESSED: {
        uint32_t elapsed = now_ms - s_btn_press_start_ms;

        if (!btn_pressed) {
            /* 按键释放 —— 根据持续时间判断短按/长按 */
            /* 注意：长按回调和释放回调都会触发 btn_cb；
             * 若长按已触发过回调，释放时不再重复触发 */
            if (s_long_press_handled) {
                /* 长按已在持续期间回调，释放时不再通知 */
            } else {
                /* 短按：持续时间 < 长按阈值 */
                if (s_btn_cb) {
                    s_btn_cb(true);     // short_press = true
                }
            }
            s_btn_state = BTN_IDLE;
        } else if (!s_long_press_handled &&
                   elapsed >= ENCODER_BTN_LONG_PRESS_MS) {
            /* 长按：持续按压达到阈值，立即回调 */
            s_long_press_handled = true;
            if (s_btn_cb) {
                s_btn_cb(false);        // short_press = false → 长按
            }
        }
        break;
    }
    } /* switch */
}

/* ================================================================
 *  公开接口
 * ================================================================ */

void hal_touch_encoder_init(encoder_callback_t rot_cb, encoder_btn_callback_t btn_cb)
{
    /* 已初始化则仅更新回调 */
    if (s_inited) {
        s_rot_cb = rot_cb;
        s_btn_cb = btn_cb;
        ESP_LOGW(TAG, "Already initialized —— only updating callbacks");
        return;
    }

    ESP_LOGI(TAG, "--- Encoder HAL init start ---");

    s_rot_cb = rot_cb;
    s_btn_cb = btn_cb;

    /* ---- 1. 初始化 PCNT 硬件单元 ---- */
    pcnt_unit_config_t unit_cfg = {
        .low_limit  = -100,
        .high_limit = 100,
    };
    ESP_ERROR_CHECK(pcnt_new_unit(&unit_cfg, &s_pcnt_unit));

    pcnt_chan_config_t chan_cfg = {
        .edge_gpio_num  = ENCODER_PIN_A,
        .level_gpio_num = ENCODER_PIN_B,
    };
    ESP_ERROR_CHECK(pcnt_new_channel(s_pcnt_unit, &chan_cfg, &s_pcnt_chan));

    /* A/B 脚开内部上拉，防止悬空时噪声导致误计数 */
    gpio_config_t enc_gpio_cfg = {
        .pin_bit_mask = (1ULL << ENCODER_PIN_A) | (1ULL << ENCODER_PIN_B),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&enc_gpio_cfg);

    /*
     * 正交解码配置：
     *   - 信号上升沿 → 减计数；下降沿 → 加计数
     *   - B 相高电平时 → 反转方向（加→减、减→加）
     *   等效于标准 4× 正交解码
     */
    pcnt_channel_set_edge_action(s_pcnt_chan,
                                 PCNT_CHANNEL_EDGE_ACTION_DECREASE,
                                 PCNT_CHANNEL_EDGE_ACTION_INCREASE);
    pcnt_channel_set_level_action(s_pcnt_chan,
                                  PCNT_CHANNEL_LEVEL_ACTION_KEEP,
                                  PCNT_CHANNEL_LEVEL_ACTION_INVERSE);

    ESP_ERROR_CHECK(pcnt_unit_enable(s_pcnt_unit));
    ESP_ERROR_CHECK(pcnt_unit_clear_count(s_pcnt_unit));
    ESP_ERROR_CHECK(pcnt_unit_start(s_pcnt_unit));

    ESP_LOGI(TAG, "[1/3] PCNT initialized (A=GPIO%d, B=GPIO%d)",
             ENCODER_PIN_A, ENCODER_PIN_B);

    /* ---- 2. 按键 GPIO ---- */
    gpio_config_t btn_gpio_cfg = {
        .pin_bit_mask = (1ULL << ENCODER_PIN_BTN),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&btn_gpio_cfg));

    ESP_LOGI(TAG, "[2/3] Button GPIO initialized (pin=%d, pull-up)",
             ENCODER_PIN_BTN);

    /* ---- 3. 启动周期轮询定时器 ---- */
    const esp_timer_create_args_t timer_args = {
        .callback        = encoder_poll_cb,
        .arg             = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name            = "encoder_poll",
    };
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &s_poll_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(s_poll_timer, ENCODER_POLL_PERIOD_US));

    s_inited = true;

    ESP_LOGI(TAG, "[3/3] Poll timer started (%d us period)",
             (int)ENCODER_POLL_PERIOD_US);
    ESP_LOGI(TAG, "--- Encoder HAL init done ---");
}
