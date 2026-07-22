# main 组件 CMakeLists.txt
idf_component_register(
    SRCS
        "main.c"
        # --- HAL 层 ---
        "hal/hal_touch_encoder.c"
        "hal/hal_imu.c"
        "hal/hal_display.c"
        # "hal/hal_audio.c"
        "hal/hal_sd.c"
        # "hal/hal_battery.c"
        "hal/hal_rtc.c"
        # --- 模式管理器 ---
        "mode/mode_manager.c"
        # --- 菜单模式 ---
        "mode/menu/menu_engine.c"
        "mode/menu/menu_ui.c"
        # --- 宠物模式 ---
        "mode/pet/pet_engine.c"
        # "mode/pet/pet_anim.c"
        # "mode/pet/pet_ui.c"
        # --- 服务层 ---
        "service/svc_wifi.c"
        "service/svc_http.c"
        "service/svc_sntp.c"
        # "service/svc_persistence.c"
        # "service/svc_volume.c"
        # "service/svc_power.c"
        # --- 菜单子模块 ---
        "mode/menu/pomodoro.c"
        "mode/menu/weather.c"
        "mode/menu/clock_alarm.c"
        # "mode/menu/music_player.c"
        # "mode/menu/animation_board.c"
    INCLUDE_DIRS
        "."
        "hal"
        "mode"
        "mode/menu"
        "mode/pet"
        "service"
    REQUIRES
        esp_driver_gpio
        esp_driver_pcnt
        esp_driver_i2c
        esp_driver_spi
        esp_driver_sdspi
        esp_driver_ledc
        esp_lcd
        esp_timer
        esp_wifi
        esp_netif
        nvs_flash
        esp_event
        esp_http_client
        sdmmc
        fatfs
        cjson
        lvgl
)
