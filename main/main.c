/**
 * @file    main.c
 * @brief   入口 —— 初始化 HAL，编码器 + IMU 事件全部路由到 mode_manager
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "app_config.h"
#include "hal/hal_imu.h"
#include "hal/hal_touch_encoder.h"
#include "mode/mode_manager.h"

/* ---- 回调：编码器 → mode_manager ---- */
static void on_encoder_rotate(int8_t step) { mode_manager_on_rotary(step); }
static void on_encoder_btn(bool sp)        { mode_manager_on_btn(sp); }

/* ---- 回调：IMU 摇晃 → mode_manager ---- */
static void on_imu_shake(void)
{
    mode_manager_on_shake(hal_imu_get_shake_dir());
}

/* ---- 主入口 ---- */
void app_main(void)
{
    /* 初始化 HAL */
    hal_imu_init();
    hal_imu_set_shake_callback(on_imu_shake);
    hal_touch_encoder_init(on_encoder_rotate, on_encoder_btn);

    /* 初始化模式管理器 */
    mode_manager_init();

    /* 主循环：50Hz 读 IMU 倾斜 + 1Hz tick */
    uint32_t tick_accum  = 0;
    int64_t  last_us     = esp_timer_get_time();

    while (1) {
        int64_t now_us = esp_timer_get_time();
        uint32_t dt_ms = (uint32_t)((now_us - last_us) / 1000);
        last_us = now_us;

        /* 倾斜：每帧都调用，但 mode_manager 内部只在方向变化时打印 */
        mode_manager_on_tilt(hal_imu_get_angles(), hal_imu_get_tilt_dir());

        /* tick：1Hz */
        tick_accum += dt_ms;
        if (tick_accum >= 1000) {
            tick_accum -= 1000;
            mode_manager_tick(1000);
        }

        vTaskDelay(pdMS_TO_TICKS(SENSOR_TASK_PERIOD_MS));
    }
}
