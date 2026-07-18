/**
 * @file    app_types.h
 * @brief   公共枚举 / 结构体 / 回调类型别名
 */

#ifndef APP_TYPES_H
#define APP_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <sys/time.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================
 *  1. 应用全局状态
 * ================================================================ */

typedef enum {
    MODE_PET = 0,   // 宠物模式
    MODE_MENU       // 菜单模式
} app_mode_t;

typedef struct {
    app_mode_t mode;
    uint8_t    volume;          // 0 ~ 30（映射到 DFPlayer 音量）
    int8_t     battery_pct;     // 电量百分比，-1 表示未检测
    bool       sd_ok;           // SD 卡是否成功挂载
    bool       wifi_connected;
} app_state_t;

/* ================================================================
 *  2. 宠物数据
 * ================================================================ */

typedef enum {
    PET_IDLE,         // 空闲 (≈50%~100% 需求)
    PET_HUNGRY,       // 饥饿 (<30%)
    PET_SAD,          // 不开心 (<30%)
    PET_SHOCKED,      // 摇晃反应中
    PET_EATING,       // 喂食动画中
    PET_PLAYING,      // 玩耍动画中
    PET_WALKING       // 随机走动
} pet_state_t;

typedef struct {
    // 需求值 0 ~ 100
    uint8_t hunger;
    uint8_t happiness;

    // 时间戳（Unix 秒），用于计算离线衰减
    uint64_t last_tick;

    // 位置（屏幕坐标）
    struct {
        int16_t x;
        int16_t y;
        int16_t home_x;
        int16_t home_y;
    } position;

    // 动画计时
    uint32_t idle_timer;       // 距离上次小动作的 ms 数
    uint32_t walk_timer;       // 距离上次走动的 ms 数
    uint32_t shake_cooldown;   // 摇晃冷却剩余 ms
    bool     anim_busy;        // 当前是否播放不可打断动画

    pet_state_t state;
} pet_data_t;

/* ================================================================
 *  3. 番茄钟数据
 * ================================================================ */

typedef struct {
    uint8_t  work_minutes;       // 工作时长 1 ~ 99
    uint8_t  break_minutes;      // 休息时长 1 ~ 99
    bool     is_work_phase;      // true = 工作，false = 休息
    uint32_t remaining_seconds;  // 剩余秒数
    bool     running;            // 是否正在计时
} pomodoro_data_t;

/* ================================================================
 *  4. 闹钟数据
 * ================================================================ */

#define MAX_ALARMS 10

typedef struct {
    uint8_t id;
    uint8_t hour;          // 0 ~ 23
    uint8_t minute;        // 0 ~ 59
    bool    enabled;
    bool    repeat[7];     // 周一 ~ 周日 (0=周一)
} alarm_entry_t;

typedef struct {
    alarm_entry_t entries[MAX_ALARMS];
    uint8_t       count;        // 当前闹钟数量
} alarm_data_t;

/* ================================================================
 *  5. 天气数据
 * ================================================================ */

typedef struct {
    char     city[32];
    int8_t   temperature;       // 摄氏度
    char     description[32];   // 例如 "多云"
    char     icon_code[8];      // 天气图标编号
    uint64_t last_update;       // 上次更新时间戳 (Unix 秒)
    bool     stale;             // 数据是否过期
} weather_data_t;

/* ================================================================
 *  6. IMU 角度
 * ================================================================ */

typedef struct {
    float pitch;        // 俯仰角 °
    float roll;         // 横滚角 °
} imu_angles_t;

typedef struct {
    float ax;           // X 轴加速度 (g)
    float ay;           // Y 轴加速度 (g)
    float az;           // Z 轴加速度 (g)
} imu_accel_t;

/** 倾斜方向 */
typedef enum {
    IMU_TILT_LEVEL = 0,   // 水平
    IMU_TILT_FRONT,        // 前倾
    IMU_TILT_BACK,         // 后倾
    IMU_TILT_LEFT,         // 左倾
    IMU_TILT_RIGHT,        // 右倾
} imu_tilt_dir_t;

/** 摇晃方向 */
typedef enum {
    IMU_SHAKE_NONE = 0,    // 无摇晃
    IMU_SHAKE_LEFT,         // 左摇晃
    IMU_SHAKE_RIGHT,        // 右摇晃
} imu_shake_dir_t;

/* ================================================================
 *  7. 回调类型别名
 * ================================================================ */

/** 编码器旋转回调 @param step >0 顺时针，<0 逆时针 */
typedef void (*encoder_callback_t)(int8_t step);

/** 编码器按键回调 @param short_press true=短按 (<800ms) false=长按 */
typedef void (*encoder_btn_callback_t)(bool short_press);

/** IMU 摇晃检测回调 */
typedef void (*imu_shake_callback_t)(void);

/** Wi-Fi 连接状态回调 @param connected 是否已连接 */
typedef void (*wifi_connect_cb_t)(bool connected);

/** 天气数据获取回调 @param data 天气数据指针 @param ok 是否获取成功 */
typedef void (*weather_callback_t)(const weather_data_t *data, bool ok);

#ifdef __cplusplus
}
#endif

#endif /* APP_TYPES_H */
