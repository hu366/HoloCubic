/**
 * @file    main.c
 * @brief   入口 —— 初始化流程 + 6 FreeRTOS 任务 + 模式切换
 *
 * 初始化顺序：
 *   lv_init → hal_display → hal_imu → hal_touch_encoder
 *   → mode_manager → menu_ui(按需) → xTaskCreate ×6 → 启动调度器
 *
 * 6 个 FreeRTOS 任务：
 *   lvgl(3/12288/0) sensor(4/4096/1)  encoder(5/3072/0)
 *   audio(2/4096/1) network(1/6144/1) logic(2/12288/0)
 *
 * lvgl_task 内轮询 mode_manager + menu_engine 状态并驱动 menu_ui 更新。
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "lvgl.h"
#include "app_config.h"
#include "app_types.h"
#include "hal/hal_display.h"
#include "hal/hal_imu.h"
#include "hal/hal_touch_encoder.h"
#include "mode/mode_manager.h"
#include "mode/menu/menu_engine.h"
#include "mode/menu/menu_ui.h"
#include "svc_wifi.h"
#include "svc_sntp.h"
#include "hal/hal_rtc.h"
#include "hal/hal_sd.h"
#include "mode/pet/pet_ui.h"

/* ================================================================
 *  回调：编码器 → mode_manager
 * ================================================================ */
static void on_encoder_rotate(int8_t step)
{
    mode_manager_on_rotary(step);
}

static void on_encoder_btn(bool sp)
{
    mode_manager_on_btn(sp);
}

/* ================================================================
 *  回调：IMU 摇晃 → mode_manager
 * ================================================================ */
static void on_imu_shake(void)
{
    mode_manager_on_shake(hal_imu_get_shake_dir());
}

/* ================================================================
 *  lvgl_task —— LVGL 刷新（30ms 周期）+ UI 状态机
 * ================================================================ */
static void lvgl_task(void *arg)
{
    (void)arg;

    /* ---- 根据 DEFAULT_STARTUP_MODE 加载初始页面 ---- */
    app_mode_t   last_mode    = DEFAULT_STARTUP_MODE;
    menu_level_t last_level   = MENU_LEVEL_TOP;
    bool         menu_ui_active = false;

    if (DEFAULT_STARTUP_MODE == MODE_MENU) {
        menu_ui_init();
        menu_ui_init_modules();
        lv_screen_load(menu_ui_get_screen());
        lv_refr_now(NULL);
        menu_ui_active = true;
    } else {
        pet_ui_init();
    }

    while (1) {
        app_mode_t cur_mode = mode_manager_get_mode();

        /* ---------- 模式切换 ---------- */
        if (cur_mode != last_mode) {
            if (cur_mode == MODE_PET) {
                /* 切换到 PET 模式 */
                if (menu_ui_active) {
                    menu_ui_deinit();
                    menu_ui_active = false;
                }
                pet_ui_init();
                printf("[MAIN] 切换到 PET 模式\n");
            } else {
                /* 切换到 MENU 模式 */
                pet_ui_deinit();
                menu_ui_init();
                menu_ui_init_modules();  /* 重新注册所有子模块创建器 */
                lv_screen_load(menu_ui_get_screen());
                lv_refr_now(NULL);
                menu_ui_active  = true;
                last_level      = MENU_LEVEL_TOP;
                printf("[MAIN] 切换到 MENU 模式\n");
            }
            last_mode = cur_mode;
        }

        /* ---------- MENU 模式下轮询 engine 状态 ---------- */
        if (cur_mode == MODE_MENU && menu_ui_active
            && !menu_ui_is_animating()) {
            menu_level_t lvl = menu_engine_get_level();

            /* -- 层级变化 -- */
            if (lvl != last_level) {
                if (lvl == MENU_LEVEL_SUB) {
                    menu_ui_enter_sub();
                }
                last_level = lvl;
            }

            /* -- 轮播滑动动画 -- */
            if (lvl == MENU_LEVEL_TOP) {
                menu_scroll_dir_t dir = menu_engine_get_scroll_dir();
                if (dir != MENU_DIR_NONE) {
                    menu_ui_update_carousel(
                        (int)menu_engine_get_selected(), (int)dir);
                    menu_engine_clear_scroll_dir();
                }
            }
        }

        /* 应用子页面 UI 变更（跨核安全：sub_input_cb 设脏标，此处刷新） */
        if (cur_mode == MODE_MENU && menu_ui_active) {
            menu_ui_apply_sub_updates();
        }

        /* 手动驱动菜单滑动动画（必须在 lv_timer_handler 之前） */
        if (cur_mode == MODE_MENU && menu_ui_active) {
            menu_ui_tick_anim();
        }

        /* 处理子模块更新（番茄钟 tick 等，Core 0 安全） */
        if (cur_mode == MODE_MENU && menu_ui_active) {
            menu_ui_process_module_updates();
        }

        /* PET 模式：处理动画帧 + 位置 + 摇晃 */
        if (cur_mode == MODE_PET) {
            pet_ui_process(LVGL_TASK_PERIOD_MS);
        }

        /* LVGL 渲染一帧 */
        lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(LVGL_TASK_PERIOD_MS));
    }
}

/* ================================================================
 *  sensor_task —— IMU 读取 + 倾斜路由（50Hz）
 * ================================================================ */
static void sensor_task(void *arg)
{
    (void)arg;
    uint32_t tick_accum = 0;
    int64_t  last_us    = esp_timer_get_time();

    while (1) {
        int64_t  now_us = esp_timer_get_time();
        uint32_t dt_ms  = (uint32_t)((now_us - last_us) / 1000);
        last_us = now_us;

        /* 读取 IMU 倾斜 → 路由到 mode_manager */
        mode_manager_on_tilt(hal_imu_get_angles(), hal_imu_get_tilt_dir());

        /* 每秒触发一次 mode_manager_tick */
        tick_accum += dt_ms;
        if (tick_accum >= 1000) {
            tick_accum -= 1000;
            mode_manager_tick(1000);
        }

        vTaskDelay(pdMS_TO_TICKS(SENSOR_TASK_PERIOD_MS));
    }
}

/* ================================================================
 *  encoder_task —— 桩
 * ================================================================ */
static void encoder_task(void *arg)
{
    (void)arg;
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

/* ================================================================
 *  audio_task —— 桩
 * ================================================================ */
static void audio_task(void *arg)
{
    (void)arg;
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

/* ================================================================
 *  network_task —— 桩
 * ================================================================ */
static void network_task(void *arg)
{
    (void)arg;
    bool synced = false;
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(3000));

        if (!svc_wifi_is_connected()) continue;

        if (!synced) {
            svc_sntp_init();
            svc_sntp_sync();
            if (svc_sntp_is_synced()) {
                synced = true;
                /* 设置时区为北京时间（UTC+8） */
                setenv("TZ", "CST-8", 1);
                tzset();
                /* 同步成功后写 RTC */
                hal_rtc_set_time(time(NULL));
                printf("[NETWORK] SNTP synced, RTC saved\n");
            }
        }
    }
}

/* ================================================================
 *  logic_task —— 每秒 tick 各模块逻辑（番茄钟/闹钟/电池 等）
 * ================================================================ */
static void logic_task(void *arg)
{
    (void)arg;
    uint32_t tick_accum = 0;
    int64_t  last_us    = esp_timer_get_time();

    while (1) {
        int64_t  now_us = esp_timer_get_time();
        uint32_t dt_ms  = (uint32_t)((now_us - last_us) / 1000);
        last_us = now_us;

        tick_accum += dt_ms;
        if (tick_accum >= 1000) {
            tick_accum -= 1000;
            menu_ui_tick_modules(1000);
        }

        vTaskDelay(pdMS_TO_TICKS(LOGIC_TICK_PERIOD_MS));
    }
}

/* ================================================================
 *  主入口
 * ================================================================ */
void app_main(void)
{
    printf("\n===== 全息棱镜宠物助手 启动 =====\n\n");

    /* ---- 1. LVGL 内核 ---- */
    lv_init();
    printf("[INIT] lv_init() done\n");

    /* ---- 2. HAL: 显示 ---- */
    hal_display_init();
    printf("[INIT] hal_display_init() done\n");

    /* ---- 3. HAL: IMU ---- */
    hal_imu_init();
    hal_imu_set_shake_callback(on_imu_shake);
    printf("[INIT] hal_imu_init() done\n");

    /* ---- 4. HAL: 编码器 ---- */
    hal_touch_encoder_init(on_encoder_rotate, on_encoder_btn);
    printf("[INIT] hal_touch_encoder_init() done\n");

    /* ---- 5. SD 卡 ---- */
    if (hal_sd_mount()) {
        printf("[INIT] hal_sd_mount() OK\n");
    } else {
        printf("[INIT] hal_sd_mount() FAILED (no card?)\n");
    }

    /* ---- 6. 服务层 ---- */
    svc_wifi_init();
    printf("[INIT] svc_wifi_init() done\n");
    hal_rtc_init();
    printf("[INIT] hal_rtc_init() done\n");

    /* ---- 7. 模式管理器 ---- */
    mode_manager_init();
    printf("[INIT] mode_manager_init() done\n");

    /* ---- 8. 创建 FreeRTOS 任务 ---- */
    BaseType_t ret;

    ret = xTaskCreatePinnedToCore(lvgl_task,    "lvgl",    12288, NULL, 3, NULL, 0);
    printf("[TASK] lvgl_task    create: %s\n", ret == pdPASS ? "OK" : "FAIL");

    ret = xTaskCreatePinnedToCore(sensor_task,  "sensor",  4096, NULL, 4, NULL, 1);
    printf("[TASK] sensor_task  create: %s\n", ret == pdPASS ? "OK" : "FAIL");

    ret = xTaskCreatePinnedToCore(encoder_task, "encoder", 3072, NULL, 5, NULL, 0);
    printf("[TASK] encoder_task create: %s\n", ret == pdPASS ? "OK" : "FAIL");

    ret = xTaskCreatePinnedToCore(audio_task,   "audio",   4096, NULL, 2, NULL, 1);
    printf("[TASK] audio_task   create: %s\n", ret == pdPASS ? "OK" : "FAIL");

    ret = xTaskCreatePinnedToCore(network_task, "network", 6144, NULL, 1, NULL, 1);
    printf("[TASK] network_task create: %s\n", ret == pdPASS ? "OK" : "FAIL");

    ret = xTaskCreatePinnedToCore(logic_task,   "logic",   12288, NULL, 2, NULL, 0);
    printf("[TASK] logic_task   create: %s\n", ret == pdPASS ? "OK" : "FAIL");

    printf("\n[INIT] All 6 tasks created\n");
    printf("\n===== 初始化完成 =====\n\n");
}
