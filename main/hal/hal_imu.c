/**
 * @file    hal_imu.c
 * @brief   MPU6050 IMU 驱动 —— I2C 通信 + 姿态角 + 陀螺仪摇晃检测
 *
 * 摇晃检测借鉴 HoloCubic 方案：用陀螺仪（角速度）判断。
 *   慢倾斜 → 角速度小 → 不触发
 *   快速甩 → 角速度大 → 触发
 * 防抖用迟滞回差（flag），值回到死区才重新 arm。
 */

#include "hal_imu.h"
#include "app_config.h"

#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <math.h>

static const char *TAG = "hal_imu";

/* ---- MPU6050 寄存器 ---- */
#define MPU6050_REG_WHO_AM_I      0x75
#define MPU6050_REG_PWR_MGMT_1    0x6B
#define MPU6050_REG_ACCEL_CONFIG  0x1C
#define MPU6050_REG_ACCEL_XOUT_H  0x3B

#define MPU6050_WHO_AM_I_VAL      0x68

/* ---- 加速度计灵敏度（±4g）---- */
#define ACCEL_SENSITIVITY         8192.0f

/* ---- I2C 超时 ---- */
#define I2C_TIMEOUT_MS            100

/* ---- 陀螺仪摇晃检测 ---- */
/* 死区阈值：角速度低于此值视为"静止"，可以重新 arm */
#define SHAKE_REARM_THRESHOLD      3000

/* ================================================================
 *  模块级状态
 * ================================================================ */
static i2c_master_bus_handle_t   s_bus_handle    = NULL;
static i2c_master_dev_handle_t   s_dev_handle    = NULL;
static imu_shake_callback_t      s_shake_cb      = NULL;
static bool                      s_inited        = false;
static bool                      s_shake_armed   = true;
static imu_shake_dir_t           s_shake_dir     = IMU_SHAKE_NONE;
static int64_t                   s_shake_cooldown_us = 0;
#define SHAKE_COOLDOWN_US        300000   /* 触发后 300ms 内不重新 arm */
static float                     s_last_ax       = 0.0f;
static float                     s_last_ay       = 0.0f;
static float                     s_last_az       = 0.0f;
static float                     s_last_pitch    = 0.0f;
static float                     s_last_roll     = 0.0f;
/* ================================================================
 *  公开接口
 * ================================================================ */

void hal_imu_init(void)
{
    if (s_inited) {
        ESP_LOGW(TAG, "Already initialized, skipping");
        return;
    }

    ESP_LOGI(TAG, "--- IMU HAL init start ---");

    /* 1. I2C 主机总线 */
    i2c_master_bus_config_t bus_cfg = {
        .clk_source  = I2C_CLK_SRC_DEFAULT,
        .i2c_port    = (i2c_port_num_t)IMU_I2C_PORT,
        .scl_io_num  = IMU_PIN_SCL,
        .sda_io_num  = IMU_PIN_SDA,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &s_bus_handle));
    ESP_LOGI(TAG, "[1/6] I2C bus created (SCL=GPIO%d, SDA=GPIO%d)",
             IMU_PIN_SCL, IMU_PIN_SDA);

    /* 2. 添加设备 */
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = IMU_I2C_ADDR,
        .scl_speed_hz    = IMU_I2C_FREQ_HZ,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(s_bus_handle, &dev_cfg,
                                               &s_dev_handle));
    ESP_LOGI(TAG, "[2/6] Device at 0x%02X @ %d Hz", IMU_I2C_ADDR, IMU_I2C_FREQ_HZ);

    /* 3. 探测 */
    ESP_ERROR_CHECK(i2c_master_probe(s_bus_handle, IMU_I2C_ADDR, I2C_TIMEOUT_MS));
    ESP_LOGI(TAG, "[3/6] MPU6050 probed");

    /* 4. WHO_AM_I */
    uint8_t who_am_i = 0;
    uint8_t reg = MPU6050_REG_WHO_AM_I;
    ESP_ERROR_CHECK(i2c_master_transmit_receive(
        s_dev_handle, &reg, 1, &who_am_i, 1, I2C_TIMEOUT_MS));
    ESP_LOGI(TAG, "[4/6] WHO_AM_I = 0x%02X", who_am_i);

    /* 5. 唤醒 + 配置 */
    uint8_t wake[2]   = { MPU6050_REG_PWR_MGMT_1, 0x00 };
    uint8_t accel[2]  = { MPU6050_REG_ACCEL_CONFIG, 0x08 };  /* ±4g */
    ESP_ERROR_CHECK(i2c_master_transmit(s_dev_handle, wake, 2, I2C_TIMEOUT_MS));
    ESP_ERROR_CHECK(i2c_master_transmit(s_dev_handle, accel, 2, I2C_TIMEOUT_MS));
    ESP_LOGI(TAG, "[5/6] Accel ±4g");

    s_inited = true;
    ESP_LOGI(TAG, "[6/6] Init done ---");
}

imu_angles_t hal_imu_get_angles(void)
{
    imu_angles_t angles = { 0.0f, 0.0f };

    if (!s_dev_handle) return angles;

    /* 读取 6 字节加速度计 */
    uint8_t reg = MPU6050_REG_ACCEL_XOUT_H;
    uint8_t buf[6] = {0};

    esp_err_t ret = i2c_master_transmit_receive(
        s_dev_handle, &reg, 1, buf, 6, I2C_TIMEOUT_MS);

    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "I2C read: %s", esp_err_to_name(ret));
        return angles;
    }

    int16_t ax_raw = (int16_t)((buf[0] << 8) | buf[1]);
    int16_t ay_raw = (int16_t)((buf[2] << 8) | buf[3]);
    int16_t az_raw = (int16_t)((buf[4] << 8) | buf[5]);
    // int16_t gz_raw = (int16_t)((buf[12] << 8) | buf[13]);  /* 暂未使用 */

    /* 加速度 → g */
    float ax = (float)ax_raw / ACCEL_SENSITIVITY;
    float ay = (float)ay_raw / ACCEL_SENSITIVITY;
    float az = (float)az_raw / ACCEL_SENSITIVITY;

    s_last_ax = ax;
    s_last_ay = ay;
    s_last_az = az;

    /* 姿态角（X/Y轴交换：左→前，右→上） */
    angles.pitch = atan2f(ay, sqrtf(ax * ax + az * az))
                   * 180.0f / (float)M_PI;
    angles.roll  = atan2f(ax, az) * 180.0f / (float)M_PI;

    s_last_pitch = angles.pitch;
    s_last_roll  = angles.roll;

    /* ---- 摇晃检测（HoloCubic 方案：加速度计 + 迟滞回差）---- */
    /*
     * 直接用加速度计原始值 ax_raw 与阈值比较。
     * 迟滞回差（flag）：触发后 flag 清零，ax_raw 回到死区才重新 arm。
     * 一次摇晃只触发一次，持续倾斜也只触发一次（不会连续报）。
     */
    if (s_shake_armed && s_shake_cb) {
        if (ax_raw > SHAKE_ACCEL_THRESHOLD) {
            s_shake_armed = false;
            s_shake_dir   = IMU_SHAKE_LEFT;
            s_shake_cooldown_us = esp_timer_get_time() + SHAKE_COOLDOWN_US;
            s_shake_cb();
        } else if (ax_raw < -SHAKE_ACCEL_THRESHOLD) {
            s_shake_armed = false;
            s_shake_dir   = IMU_SHAKE_RIGHT;
            s_shake_cooldown_us = esp_timer_get_time() + SHAKE_COOLDOWN_US;
            s_shake_cb();
        }
    }

    /* 冷却期满 + 值回死区 → 重新 arm */
    if (!s_shake_armed && esp_timer_get_time() > s_shake_cooldown_us) {
        if (ax_raw > -SHAKE_REARM_THRESHOLD && ax_raw < SHAKE_REARM_THRESHOLD) {
            s_shake_armed = true;
        }
    }

    return angles;
}

imu_tilt_dir_t hal_imu_get_tilt_dir(void)
{
    float p = s_last_pitch;
    float r = s_last_roll;
    float t = IMU_TILT_THRESHOLD_DEG;

    imu_tilt_dir_t raw;
    /* X/Y 轴交换后：r(X-based)→左/右，p(Y-based)→前/后 */
    if      (r < -t) raw = IMU_TILT_LEFT;
    else if (r >  t) raw = IMU_TILT_RIGHT;
    else if (p >  t) raw = IMU_TILT_FRONT;
    else if (p < -t) raw = IMU_TILT_BACK;
    else             raw = IMU_TILT_LEVEL;

    /* 去抖：方向必须连续 3 帧不变（60ms @ 50Hz）才算有效。
       拍打/撞击的加速度尖峰通常只持续 1~2 帧，会被过滤掉。 */
    #define TILT_DEBOUNCE 3
    static imu_tilt_dir_t s_last_raw = IMU_TILT_LEVEL;
    static int            s_stable   = 0;

    if (raw == s_last_raw) {
        if (s_stable < TILT_DEBOUNCE) s_stable++;
    } else {
        s_last_raw = raw;
        s_stable = 1;
    }

    return (s_stable >= TILT_DEBOUNCE) ? raw : IMU_TILT_LEVEL;
}

imu_shake_dir_t hal_imu_get_shake_dir(void)
{
    return s_shake_dir;
}

imu_accel_t hal_imu_get_accel(void)
{
    imu_accel_t a = { s_last_ax, s_last_ay, s_last_az };
    return a;
}

void hal_imu_set_shake_callback(imu_shake_callback_t cb)
{
    s_shake_cb = cb;
}
